# CAN Protocol: 4. Examples

---

This document provides concrete examples of the CAN communication flow, distinguishing between two levels:

1.  **High-Level Communication (Host <-> Conductor)**: User commands sent from the Host to the Conductor, typically over USB. The Conductor processes these commands.
2.  **Low-Level Communication (Conductor <-> Executor)**: Commands sent from the Conductor to the various Executor nodes (e.g., STM32F103-based boards) over CAN. These are hardware-specific actions.

---

## 4.1. High-Level Example: Host sends `GET_STATUS (0x1000)` to Conductor (USB/Internal Processing)

This example describes a high-level command from the Host. This interaction is primarily via USB and internal processing on the Conductor. If the Conductor needs to query an Executor for status, that would involve a Low-Level CAN communication.

**Assumptions:**
*   Host Controller Address: `0x01`
*   Conductor Address: `0x10`

### Step 1: Host sends `GET_STATUS` Command (via USB to Conductor)
 7            │ speed        │ UINT8  │ 0A    
*   **Command Code**: `0x1000` (GET_STATUS)
*   **Parameters**: None
*   **Conductor Action**: The Conductor processes this and may query internal states or send low-level commands to Executors.

### Step 2: Conductor internally processes or queries Executors

(This step involves internal logic and potentially low-level CAN communication which is not detailed here for simplicity of the high-level example. For instance, the Conductor might send low-level `GET_STATUS` commands to various Executors and aggregate their responses.)

### Step 3: Conductor sends `ACK` to Host (via USB)

### Step 4: Conductor sends `DATA` to Host (via USB)

The Conductor returns the status data for the entire system.

*   **Payload Parameters**:
    | Byte(s) | Parameter  | Type    | Value   | Description                                      |
    |---------|------------|---------|---------|--------------------------------------------------|
    | `0`     | Sub-Type   | `UINT8` | `0x02`  | `DATA` message.                                  |
    | `1`     | Sequence Info | `UINT8` | `0x80`  | Sequence 0, End of Transmission (for single frame). |
    | `2`     | `status`   | `UINT8` | `0x01`  | System status: `0x00`=Off, `0x01`=Ready, `0x02`=Busy, `0x03`=Error. |
    | `3-4`   | `error_code` | `UINT16`| `0x0000`| Current error code (0 if no error).              |
    | `5-7`   | Reserved   | `UINT8` | `0x00`  | (Fill with zeros)                                |

### Step 5: Conductor sends `DONE` to Host (via USB)

---

## 4.2. High-Level Example: Host sends `WASH_STATION_WASH (0x4000)` to Conductor (USB/Internal Processing)

This is a recipe command. The Host sends the command to the Conductor, and the Conductor orchestrates the low-level actions.

**Assumptions:**
*   Host Controller Address: `0x01`
*   Conductor Address: `0x10`
*   Motion Executor Address: `0x20`
*   Fluidics Executor Address: `0x30`

### Step 1: Host sends `WASH_STATION_WASH` Command (via USB to Conductor)

*   **Command Code**: `0x4000` (WASH_STATION_WASH)
*   **Parameters**: `cycles=2`, `cuvette=5` (as defined in `User_Commands/commands.md`)
*   **Conductor Action**: The Conductor receives this, sends ACK via USB, then translates high-level parameters into low-level motor steps and pump durations using calibration and `param_translator`.

### Step 2: Conductor orchestrates Low-Level CAN Communication with Executor

(This involves multiple low-level CAN frames between the Conductor and Executors. For example, the Conductor sends `MOTOR_ROTATE` to the Motion Executor, calculates pump `duration_ms` from the requested volume/calibration, sends `PUMP_RUN_DURATION` to the Fluidics Executor, and waits for `DONE` from each atomic command before proceeding.)

**Example Low-Level Sequence (Conductor -> Executor for part of the recipe):**

*   **Conductor sends `MOTOR_ROTATE` (0x0101) Command to Motion Executor (0x20)**
    *   **CAN ID**: `0x00201000`
    *   **DLC**: `8`
    *   **Payload Parameters**:
        | Byte(s) | Parameter  | Type    | Value   | Description                                      |
        |---------|------------|---------|---------|--------------------------------------------------|
        | `0-1`   | Command Code | `UINT16`| `0x0101`| `MOTOR_ROTATE`                                   |
        | `2`     | `motor_id` | `UINT8` | `0x00`  | Motor ID on Executor.                            |
        | `3-6`   | `steps`    | `INT32` | `-500`  | Number of steps (Little-Endian).                 |
        | `7`     | `speed`    | `UINT8` | `0x0A`  | Speed profile/setting.                           |

*   **Motion Executor sends `ACK` to Conductor**
    *   **CAN ID**: `0x05102000`
    *   **DLC**: `8`
    *   **Payload Parameters**:
        | Byte(s) | Parameter  | Type    | Value   | Description                                      |
        |---------|------------|---------|---------|--------------------------------------------------|
        | `0-1`   | Command Code | `UINT16`| `0x0101`| Acknowledged command.                            |
        | `2-3`   | Error Code | `UINT16`| `0x0000`| No error.                                        |

*   **Motion Executor sends `DONE` to Conductor after the movement has physically completed**
    *   **CAN ID**: `0x07102000`
    *   **DLC**: `8`
    *   **Payload Parameters**:
        | Byte(s) | Parameter  | Type    | Value   | Description                                      |
        |---------|------------|---------|---------|--------------------------------------------------|
        | `0`     | Sub-Type   | `UINT8` | `0x01`  | `DONE` message.                                  |
        | `1-2`   | Command Code | `UINT16`| `0x0101`| Completed command.                               |
        | `3-7`   | Padding/status | - | `0x00...` | Domain-specific status/padding.               |

*   **Conductor sends `PUMP_RUN_DURATION` (0x0201) Command to Fluidics Executor (0x30)**
    *   **CAN ID**: `0x00301000`
    *   **DLC**: `8`
    *   **Payload example**: `01 02 00 D0 07 00 00 00` = pump 0, 2000 ms.

*   **Fluidics Executor sends `DONE` only after the pump has run for 2000 ms and has been switched OFF.**

(This sequence repeats for other low-level actions required to complete the `WASH_STATION_WASH` recipe.)

### Step 3: Conductor sends `ACK` to Host (via USB)

(This ACK is sent after the initial high-level command is received and accepted by the Conductor, NOT after the entire recipe is complete.)

### Step 4: Conductor sends `DONE` to Host (via USB)

(This DONE is sent after all low-level actions for the `WASH_STATION_WASH` recipe have been successfully completed by the Executors and orchestrated by the Conductor.)

---

## 4.3. Low-Level Example: Conductor sends `MOTOR_ROTATE (0x0101)` to Executor

This example describes a low-level command from the Conductor to an Executor.

**Scenario**: The Conductor needs to tell the "Reaction Disk Executor" (address `0x30`) to rotate its motor by `-500` steps. The low-level command for this is `MOTOR_ROTATE (0x0101)`.

### Step 1: Conductor sends `COMMAND` to Executor

*   **CAN ID**: `0x00301000`
*   **DLC**: `8` <-- **CORRECTED HERE**
*   **Payload Parameters**:
    | Byte(s) | Parameter  | Type    | Value   | Description                                      |
    |---------|------------|---------|---------|--------------------------------------------------|
    | `0-1`   | Command Code | `UINT16`| `0x0101`| `MOTOR_ROTATE`                                   |
    | `2`     | `motor_id` | `UINT8` | `0x00`  | Motor ID on Executor.                            |
    | `3-6`   | `steps`    | `INT32` | `-500`  | Number of steps (Little-Endian).                 |
    | `7`     | `speed`    | `UINT8` | `0x0A`  | Speed profile/setting.                           |

### Step 2: Executor sends `ACK` to Conductor

*   **CAN ID**: `0x05103000`
*   **DLC**: `4`
    *   **Payload Parameters**:
        | Byte(s) | Parameter  | Type    | Value   | Description                                      |
        |---------|------------|---------|---------|--------------------------------------------------|
        | `0-1`   | Command Code | `UINT16`| `0x0101`| Acknowledged command.                            |
        | `2-3`   | Error Code | `UINT16`| `0x0000`| No error.                                        |

### Step 3: Executor performs the physical action

The motor on the Reaction Disk rotates by -500 steps.

### Step 4: Executor sends `DONE` to Conductor

*   **CAN ID**: `0x07103000`
*   **DLC**: `3`
*   **Payload Parameters**:
    | Byte(s) | Parameter  | Type    | Value   | Description                                      |
    |---------|------------|---------|---------|--------------------------------------------------|
    | `0`     | Sub-Type   | `UINT8` | `0x01`  | `DONE` message.                                  |
    | `1-2`   | Command Code | `UINT16`| `0x0101`| Completed command.                               |
