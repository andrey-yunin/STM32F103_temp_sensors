# CAN Protocol: 6. Parameter Packing Strategy

---

## 6.1. Design Philosophy

The primary goal of the parameter packing strategy is to keep executor parsing deterministic and easy to validate on resource-constrained STM32F103 nodes. The executor bus uses strict `DLC=8`; efficiency is achieved by compact field layouts inside the fixed frame, with unused bytes set to zero.

Key principles:
1.  **Strict executor DLC**: `Conductor <-> Executor` frames use `DLC=8`. Command tables describe used bytes; unused bytes are zero padding.
2.  **Byte Alignment**: Multi-byte values (like `UINT16`, `INT32`) should, whenever possible, start on an even byte index (`2`, `4`, `6`) to simplify memory access on the Executor, though this is a soft rule that can be broken for higher density.
3.  **No Unused Bytes (where possible)**: A command's defined payload should ideally not contain "Reserved" or unused bytes, but sometimes a small amount of padding is accepted for alignment or future-proofing. The payload length is defined by the command that uses it.

---

## 6.2. Analysis of Low-Level Commands

This section provides a justification for the payload structure of each low-level command.

### `0x0101: MOTOR_ROTATE`

This is a high-frequency, critical command for which we must optimize packing.

*   **Parameters**:
    *   `motor_id`: `UINT8`. Identifies the motor on the executor board. Typically a small number (e.g., 0-3).
    *   `steps`: `INT32`. The number of steps to move. This requires a full 32 bits to accommodate large movements and both positive/negative directions.
    *   `speed`: `UINT8`. Packed target/max speed for this atomic move. It is not an acceleration profile id.

*   **Contract Notes**:
    *   The Conductor owns speed selection as part of the recipe-level atomic action and packs it into the command.
    *   The Motion Executor validates `speed` against local axis limits before starting STEP generation.
    *   Acceleration is not carried by `MOTOR_ROTATE 0x0101`; it remains local Motion Executor motion-profile configuration implemented by the planner/driver.
    *   For non-zero `steps`, unpacked `speed=0` is invalid. For `steps=0`, the executor may complete immediately without STEP generation.

*   **Proposed Payload Structure (DLC = 8)**:

| Byte(s) | Field Name      | Type    | Value/Example   | Justification                                      |
|---------|-----------------|---------|-----------------|----------------------------------------------------|
| `0-1`   | Command Code    | `UINT16`| `0x0101`        | Identifies the `MOTOR_ROTATE` action.              |
| `2`     | `motor_id`      | `UINT8` | `0x00`          | A single byte is sufficient (up to 256 motors per node). |
| `3-6`   | `steps`         | `INT32` | `-500`          | A 32-bit signed integer is required for precision and direction. Stored little-endian. |
| `7`     | `speed`         | `UINT8` | `0x0A`          | Packed as `real_speed / 4` (shift >>2). Range 0–1020, step 4. Executor unpacks: `speed << 2`. |

*   **Efficiency Analysis**: This structure uses all 8 bytes of the CAN payload. It is tightly packed with no wasted space within the defined payload for this command. Using `INT32` for steps is crucial for sufficient range, even if it uses 4 bytes.

### `0x0201: PUMP_RUN_DURATION`

This command is simpler and requires fewer parameters.

*   **Parameters**:
    *   `pump_id`: `UINT8`.
    *   `duration_ms`: `UINT32`.
    *   `duration_ms=0` is invalid for this finite operation.

*   **Proposed Payload Structure (DLC = 8)**:

| Byte(s) | Field Name      | Type     | Value/Example   | Justification                                      |
|---------|---------------|----------|-----------------|----------------------------------------------------|
| `0-1`   | Command Code  | `UINT16` | `0x0201`        | Identifies the `PUMP_RUN_DURATION` action.           |
| `2`     | `pump_id`     | `UINT8`  | `0x01`          | A single byte is sufficient.                       |
| `3-6`   | `duration_ms` | `UINT32` | `1000`          | A 32-bit unsigned integer allows for long durations (up to ~49 days), preventing overflow issues. Stored little-endian. |
| `7`     | `reserved`    | `UINT8`  | `0x00`          | Reserved and zero-padded for strict executor DLC. |

*   **Execution contract**: the Fluidics Executor turns the pump on, holds it for `duration_ms`, turns it off, then sends `DONE`. This command is the preferred primitive for recipe-level pump dosing.
