# CAN Protocol: 6. Parameter Packing Strategy

---

## 6.1. Design Philosophy

The primary goal of the parameter packing strategy is to maximize the efficiency of the CAN bus. This is achieved by minimizing the number of bytes sent for each command while maintaining clarity and alignment for easy parsing on the resource-constrained STM32F103 Executor nodes.

Key principles:
1.  **Variable DLC**: The Data Length Code (DLC) for `COMMAND` frames is not fixed. Each command uses only the number of bytes required for its code and parameters.
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
    *   `speed`: `UINT8`. A profile or index for speed/acceleration.

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

*   **Proposed Payload Structure (DLC = 7)**:

| Byte(s) | Field Name      | Type     | Value/Example   | Justification                                      |
|---------|---------------|----------|-----------------|----------------------------------------------------|
| `0-1`   | Command Code  | `UINT16` | `0x0201`        | Identifies the `PUMP_RUN_DURATION` action.           |
| `2`     | `pump_id`     | `UINT8`  | `0x01`          | A single byte is sufficient.                       |
| `3-6`   | `duration_ms` | `UINT32` | `1000`          | A 32-bit unsigned integer allows for long durations (up to ~49 days), preventing overflow issues. Stored little-endian. |
| `7`     | `reserved`    | `UINT8`  | `0x00`          | This byte is currently unused, resulting in a DLC of 7. It's explicitly reserved for future use or can be set to 0. This is an acceptable trade-off for simplicity of parser and consistency if all COMMANDs would be 8 bytes. |

*   **Efficiency Analysis**: This structure uses a total of 7 bytes effectively. It demonstrates the use of a DLC less than 8 when not all bytes are critically needed.