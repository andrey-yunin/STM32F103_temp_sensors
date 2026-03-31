# CAN Protocol: 5. Low-Level Command Set

---

This document lists the low-level commands that are sent by the Conductor to the various Executor nodes over the CAN bus. These are the primitive hardware-level operations.

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

| Byte(s) | Parameter  | Type    | Description                                      |
|---------|------------|---------|--------------------------------------------------|
| `0-1`   | Command Code | `UINT16`| `0x0101` (MOTOR_ROTATE)                          |
| `2`     | `motor_id` | `UINT8` | The ID of the motor on the target Executor board. |
| `3-6`   | `steps`    | `INT32` | Number of steps to rotate (can be negative). Stored as Little-Endian. |
| `7`     | `speed`    | `UINT8` | Speed, packed as `real_speed / 4` (shift right 2). Range: 0–1020, step 4. Executor unpacks: `real_speed = packed_speed << 2`. |

---

### 0x0201: PUMP_RUN_DURATION

Activates a pump for a specific duration.

*   **Executor Target**: **Pump and Valve Control Board** (Address `0x30`)
*   **Payload Parameters (DLC = 7)**:

| Byte(s) | Parameter     | Type     | Description                                      |
|---------|---------------|----------|--------------------------------------------------|
| `0-1`   | Command Code  | `UINT16` | `0x0201` (PUMP_RUN_DURATION)                     |
| `2`     | `pump_id`     | `UINT8`  | The ID of the pump on the target Executor board.  |
| `3-6`   | `duration_ms` | `UINT32` | Duration to run the pump, in milliseconds. Stored as Little-Endian. |
| `7`     | `reserved`    | `UINT8`  | Must be 0. (Padding for DLC consistency, or future use).       |

> **Примечание:** `PUMP_RUN_DURATION` определена в спецификации как резервная команда. В текущей прошивке используются раздельные `PUMP_START` (0x0202) и `PUMP_STOP` (0x0203), что соответствует архитектуре рецептов (START + WAIT_MS + STOP как отдельные шаги).

---

### 0x0102: MOTOR_HOME

Initiates homing sequence for a specific motor (move towards home switch until trigger).

*   **Executor Target**: **Stepper Motor Control Board** (Address `0x20`)
*   **Payload Parameters (DLC = 5)**:

| Byte(s) | Parameter    | Type     | Description                                      |
|---------|-------------|----------|--------------------------------------------------|
| `0-1`   | Command Code | `UINT16` | `0x0102` (MOTOR_HOME)                            |
| `2`     | `motor_id`   | `UINT8`  | The ID of the motor on the target Executor board. |
| `3-4`   | `speed`      | `UINT16` | Speed for homing movement. Stored as Little-Endian. |

---

### 0x0103: MOTOR_START_CONTINUOUS

Starts continuous rotation of a motor (e.g., mixer paddle). Motor runs until `MOTOR_STOP` (0x0104) is received.

*   **Executor Target**: **Stepper Motor Control Board** (Address `0x20`)
*   **Payload Parameters (DLC = 4)**:

| Byte(s) | Parameter    | Type     | Description                                      |
|---------|-------------|----------|--------------------------------------------------|
| `0-1`   | Command Code | `UINT16` | `0x0103` (MOTOR_START_CONTINUOUS)                |
| `2`     | `motor_id`   | `UINT8`  | The ID of the motor on the target Executor board. |
| `3`     | `speed`      | `UINT8`  | Speed/power setting for continuous rotation.      |

---

### 0x0104: MOTOR_STOP

Stops a motor that is running continuously (counterpart to `MOTOR_START_CONTINUOUS`).

*   **Executor Target**: **Stepper Motor Control Board** (Address `0x20`)
*   **Payload Parameters (DLC = 3)**:

| Byte(s) | Parameter    | Type     | Description                                      |
|---------|-------------|----------|--------------------------------------------------|
| `0-1`   | Command Code | `UINT16` | `0x0104` (MOTOR_STOP)                            |
| `2`     | `motor_id`   | `UINT8`  | The ID of the motor to stop.                     |

---

### 0x0202: PUMP_START

Starts a pump. Pump runs until `PUMP_STOP` (0x0203) is received.

*   **Executor Target**: **Pump and Valve Control Board** (Address `0x30`)
*   **Payload Parameters (DLC = 3)**:

| Byte(s) | Parameter    | Type     | Description                                      |
|---------|-------------|----------|--------------------------------------------------|
| `0-1`   | Command Code | `UINT16` | `0x0202` (PUMP_START)                            |
| `2`     | `pump_id`    | `UINT8`  | The ID of the pump on the target Executor board.  |

---

### 0x0203: PUMP_STOP

Stops a running pump (counterpart to `PUMP_START`).

*   **Executor Target**: **Pump and Valve Control Board** (Address `0x30`)
*   **Payload Parameters (DLC = 3)**:

| Byte(s) | Parameter    | Type     | Description                                      |
|---------|-------------|----------|--------------------------------------------------|
| `0-1`   | Command Code | `UINT16` | `0x0203` (PUMP_STOP)                             |
| `2`     | `pump_id`    | `UINT8`  | The ID of the pump to stop.                       |

---

### 0x0401: PHOTOMETER_SCAN

Initiates a photometric scan with a given wavelength mask.

*   **Executor Target**: **Photometer Board** (Address TBD)
*   **Payload Parameters (DLC = 4)**:

| Byte(s) | Parameter         | Type     | Description                                      |
|---------|-------------------|----------|--------------------------------------------------|
| `0-1`   | Command Code      | `UINT16` | `0x0401` (PHOTOMETER_SCAN)                       |
| `2`     | `photometer_id`   | `UINT8`  | The ID of the photometer.                        |
| `3`     | `wavelength_mask` | `UINT8`  | Bitmask of wavelengths to scan.                  |
