# CAN Protocol: 5. Low-Level Command Set

---

This document lists the low-level commands that are sent by the Conductor to the various Executor nodes over the CAN bus. These are the primitive hardware-level operations.

All `Conductor <-> Executor` frames use strict `DLC=8`. Tables below describe the used payload bytes; unused bytes are zero-padded.

`DONE` always means that the command reached its defined postcondition. For finite commands this is physical completion; for state commands this is reaching the requested state.

Scope rule:

*   Host-level commands are not defined in this file. The Conductor translates Host API commands into one or more low-level Executor commands.
*   Internal firmware enums such as `CMD_MOVE_RELATIVE`, `CMD_SET_SPEED` or `CMD_STOP` are implementation details and are not part of the CAN contract.
*   `ACK`, `NACK`, `DATA` and `DONE` in this file always refer to the low-level `cmd_code` carried in the CAN payload.
*   A low-level Executor `DONE` completes only one atomic command for the Conductor. Host-level `DONE` is sent by the Conductor only after the whole Host recipe/job has completed.
*   Internal executor events such as timer expiry, step-counter completion or switch trigger are not protocol `DONE` events. They are local completion events that may lead the executor task to send low-level `DONE`.

## Command Groups

*   **0x01xx**: Motor Commands
*   **0x02xx**: Pump Commands
*   **0x03xx**: Sensor Commands
*   ... and so on

---

### 0x0101: MOTOR_ROTATE

Rotates a specific motor by a given number of steps.

*   **Executor Target**: **Stepper Motor Control Board** (Address `0x20`)
*   **Payload Parameters (DLC = 8)**:
*   **DONE Postcondition**: requested steps have been generated, the motor has stopped, and the axis state has been updated.
*   **Speed Ownership**: `speed` is part of the recipe-level atomic action contract. The Conductor selects this command's requested target/max speed from recipe/action configuration or a technological motion profile.
*   **Speed Validation**: the Motion Executor validates `speed` against local axis limits. Unsafe or unsupported speed values must not start movement; the baseline response is `NACK INVALID_PARAM`.
*   **Acceleration Ownership**: acceleration is not part of this payload. Acceleration is local Motion Executor motion-profile configuration implemented by the motion planner/driver.
*   **Validation**: `steps=0` may complete immediately without STEP generation. `steps!=0` with unpacked `speed=0` is invalid.
*   **Shared Timer Resource**: if the target board maps multiple motors to the same timer base, the executor may reject a command with `MOTOR_BUSY` when another motor in the same timer group is active. For the current Motion STM32F103 baseline, motors `0..3` share `TIM1` and motors `4..7` share `TIM2`.

| Byte(s) | Parameter  | Type    | Description                                      |
|---------|------------|---------|--------------------------------------------------|
| `0-1`   | Command Code | `UINT16`| `0x0101` (MOTOR_ROTATE)                          |
| `2`     | `motor_id` | `UINT8` | The ID of the motor on the target Executor board. |
| `3-6`   | `steps`    | `INT32` | Number of steps to rotate (can be negative). Stored as Little-Endian. |
| `7`     | `speed`    | `UINT8` | Target/max speed for this atomic move, packed as `real_speed / 4` (shift right 2). Range: 0–1020, step 4. Executor unpacks: `real_speed = packed_speed << 2`. |

---

### 0x0201: PUMP_RUN_DURATION

Activates a pump for a specific duration. This is the preferred recipe-level pump primitive.

*   **Executor Target**: **Pump and Valve Control Board** (Address `0x30`)
*   **Payload Parameters (DLC = 8)**:
*   **DONE Postcondition**: the pump has run for `duration_ms`, has been switched off, and the operation has completed successfully.

| Byte(s) | Parameter     | Type     | Description                                      |
|---------|---------------|----------|--------------------------------------------------|
| `0-1`   | Command Code  | `UINT16` | `0x0201` (PUMP_RUN_DURATION)                     |
| `2`     | `pump_id`     | `UINT8`  | The ID of the pump on the target Executor board.  |
| `3-6`   | `duration_ms` | `UINT32` | Duration to run the pump, in milliseconds. Stored as Little-Endian. |
| `7`     | `reserved`    | `UINT8`  | Must be 0. (Padding for DLC consistency, or future use).       |

> **Migration note:** `PUMP_RUN_DURATION` replaces `PUMP_START + WAIT_MS + PUMP_STOP` as the preferred recipe primitive for dosing by time. The Conductor calculates `duration_ms` from the Host volume and calibration model, then the Fluidics Executor owns timing, switch-off and `DONE`.

---

### 0x0102: MOTOR_HOME

Initiates homing sequence for a specific motor (move towards home switch until trigger).

*   **Executor Target**: **Stepper Motor Control Board** (Address `0x20`)
*   **Payload Parameters (DLC = 8)**:
*   **DONE Postcondition**: home condition has been reached, the motor has stopped, and the homed/position state has been updated.

| Byte(s) | Parameter    | Type     | Description                                      |
|---------|-------------|----------|--------------------------------------------------|
| `0-1`   | Command Code | `UINT16` | `0x0102` (MOTOR_HOME)                            |
| `2`     | `motor_id`   | `UINT8`  | The ID of the motor on the target Executor board. |
| `3-4`   | `speed`      | `UINT16` | Speed for homing movement. Stored as Little-Endian. |

---

### 0x0103: MOTOR_START_CONTINUOUS

Starts continuous rotation of a motor (e.g., mixer paddle). Motor runs until `MOTOR_STOP` (0x0104) is received.

*   **Executor Target**: **Stepper Motor Control Board** (Address `0x20`)
*   **Payload Parameters (DLC = 8)**:
*   **DONE Postcondition**: continuous motion mode has been entered and local protections are active.

| Byte(s) | Parameter    | Type     | Description                                      |
|---------|-------------|----------|--------------------------------------------------|
| `0-1`   | Command Code | `UINT16` | `0x0103` (MOTOR_START_CONTINUOUS)                |
| `2`     | `motor_id`   | `UINT8`  | The ID of the motor on the target Executor board. |
| `3`     | `speed`      | `UINT8`  | Speed/power setting for continuous rotation.      |

---

### 0x0104: MOTOR_STOP

Stops a motor that is running continuously (counterpart to `MOTOR_START_CONTINUOUS`).

*   **Executor Target**: **Stepper Motor Control Board** (Address `0x20`)
*   **Payload Parameters (DLC = 8)**:
*   **DONE Postcondition**: motor output has stopped and the axis has left continuous/active motion state.

| Byte(s) | Parameter    | Type     | Description                                      |
|---------|-------------|----------|--------------------------------------------------|
| `0-1`   | Command Code | `UINT16` | `0x0104` (MOTOR_STOP)                            |
| `2`     | `motor_id`   | `UINT8`  | The ID of the motor to stop.                     |

---

### 0x0202: PUMP_START

Starts a pump. Pump runs until `PUMP_STOP` (0x0203), safety timeout, abort or fault. This command is retained for service/manual control; recipe dosing should use `PUMP_RUN_DURATION`.

*   **Executor Target**: **Pump and Valve Control Board** (Address `0x30`)
*   **Payload Parameters (DLC = 8)**:
*   **DONE Postcondition**: pump output is ON and local safety timeout is armed.

| Byte(s) | Parameter    | Type     | Description                                      |
|---------|-------------|----------|--------------------------------------------------|
| `0-1`   | Command Code | `UINT16` | `0x0202` (PUMP_START)                            |
| `2`     | `pump_id`    | `UINT8`  | The ID of the pump on the target Executor board.  |
| `3-6`   | `timeout_ms` | `UINT32` | Safety timeout. `0` means executor default. Stored as Little-Endian. |
| `7`     | `reserved`   | `UINT8`  | Must be 0. |

---

### 0x0203: PUMP_STOP

Stops a running pump (counterpart to `PUMP_START`).

*   **Executor Target**: **Pump and Valve Control Board** (Address `0x30`)
*   **Payload Parameters (DLC = 8)**:
*   **DONE Postcondition**: pump output is OFF and the safety timer for this pump is stopped.

| Byte(s) | Parameter    | Type     | Description                                      |
|---------|-------------|----------|--------------------------------------------------|
| `0-1`   | Command Code | `UINT16` | `0x0203` (PUMP_STOP)                             |
| `2`     | `pump_id`    | `UINT8`  | The ID of the pump to stop.                       |

---

### 0x0401: PHOTOMETER_SCAN

Initiates a photometric scan with a given wavelength mask.

*   **Executor Target**: **Photometer Board** (Address TBD)
*   **Payload Parameters (DLC = 8)**:

| Byte(s) | Parameter         | Type     | Description                                      |
|---------|-------------------|----------|--------------------------------------------------|
| `0-1`   | Command Code      | `UINT16` | `0x0401` (PHOTOMETER_SCAN)                       |
| `2`     | `photometer_id`   | `UINT8`  | The ID of the photometer.                        |
| `3`     | `wavelength_mask` | `UINT8`  | Bitmask of wavelengths to scan.                  |
