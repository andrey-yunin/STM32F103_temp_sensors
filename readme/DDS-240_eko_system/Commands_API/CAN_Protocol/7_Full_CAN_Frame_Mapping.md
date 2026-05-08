# CAN Protocol: 7. Full CAN Frame Mapping for Programmers

---

This document serves as a detailed guide for programmers implementing the CAN protocol on both the Conductor and Executor nodes. It bridges the gap between the high-level command definitions and their low-level representation within CAN frame payloads, ensuring efficient and consistent data handling.

It is crucial to refer to the following companion documents for full context:
*   `1_Physical_Layer.md`: Physical and Data Link Layer specifications.
*   `2_Frame_Format.md`: Detailed CAN ID structure and basic payload layouts.
*   `3_Application_Layer.md`: Command execution flow and Sub-Payload structures.
*   `5_Low_Level_Commands.md`: Definitions of low-level commands.
*   `6_Parameter_Packing.md`: Justification for parameter packing efficiency.

---

## 7.1. High-Level (Host <-> Conductor) vs. Low-Level (Conductor <-> Executor) Overview

*   **Host to Conductor**: Communication typically happens over USB. The Host sends high-level user commands (e.g., `WASH_STATION_WASH` from `User_Commands/commands.md`). The Conductor is responsible for parsing these, translating high-level parameters into low-level ones using `param_translator`, and orchestrating the necessary low-level CAN commands to Executors.
*   **Conductor to Executor**: This document focuses on the **low-level CAN communication** between the Conductor and the Executor nodes. The Conductor acts as the "master" and sends low-level commands to Executors, which then perform physical actions and respond.

---

## 7.2. CAN Frame Structure Overview

All CAN frames discussed here adhere to:
*   **CAN Standard**: Classical CAN
*   **Bit Rate**: 1 Mbit/s
*   **CAN ID**: 29-bit Extended, structured as defined in `2_Frame_Format.md`.
*   **Payload**: strict `DLC=8` on the executor CAN bus. Used bytes are packed as per `6_Parameter_Packing.md`; unused bytes are zero-padded.

---

## 7.3. Detailed Frame Mappings

### Scenario 1: Conductor sends `MOTOR_ROTATE (0x0101)` Command to Motion Executor

**Purpose**: Command a motor to rotate by a specific number of steps.
**Low-Level Command Code**: `0x0101` (from `5_Low_Level_Commands.md`)
**Target Executor Address**: `0x20` (Motion Executor)
**Source Address**: `0x10` (Conductor)
**Parameters**: `motor_id = 0x00`, `steps = -500 (0xFFFFFE0C)`, `speed = 0x0A`

#### **CAN ID Construction**

| Bits       | Length | Field Name         | Value (binary) | Value (hex) | Description                 |
|------------|--------|--------------------|----------------|-------------|-----------------------------|
| 28-26      | 3 bits | Priority           | `000`          | `0`         | Highest priority command    |
| 25-24      | 2 bits | Message Type       | `00`           | `0`         | `COMMAND`                   |
| 23-16      | 8 bits | Destination Address| `00100000`     | `20`        | `0x20` (Motion Executor) |
| 15-8       | 8 bits | Source Address     | `00010000`     | `10`        | `0x10` (Conductor)          |
| 7-0        | 8 bits | Reserved           | `00000000`     | `00`        | Must be 0                   |
| **Full 29-bit ID** | | | | `0x00201000`|                             |

#### **Payload Construction (DLC = 8)**

| Byte(s) | Field Name      | Type    | Value (hex) | Description                                       |
|---------|-----------------|---------|-------------|---------------------------------------------------|
| `0-1`   | Command Code    | `UINT16`| `01 01`     | `0x0101` (MOTOR_ROTATE) - Little-Endian           |
| `2`     | `motor_id`      | `UINT8` | `00`        | Motor ID 0                                        |
| `3-6`   | `steps`         | `INT32` | `0C FE FF FF` | -500 (0xFFFFFE0C) - Little-Endian               |
| `7`     | `speed`         | `UINT8` | `0A`        | Speed setting 10                                  |
| **Full Payload** | | | `01 01 00 0C FE FF FF 0A` |                                                   |

---

### Scenario 2: Executor sends `ACK` for `MOTOR_ROTATE (0x0101)` to Conductor

**Purpose**: Acknowledge successful receipt and parsing of `MOTOR_ROTATE`.
**Acknowledged Command Code**: `0x0101`
**Source Address**: `0x20` (Motion Executor)
**Destination Address**: `0x10` (Conductor)
**Error Code**: `0x0000` (No error)

#### **CAN ID Construction**

| Bits       | Length | Field Name         | Value (binary) | Value (hex) | Description                 |
|------------|--------|--------------------|----------------|-------------|-----------------------------|
| 28-26      | 3 bits | Priority           | `001`          | `1`         | Normal priority response    |
| 25-24      | 2 bits | Message Type       | `01`           | `1`         | `ACK`                       |
| 23-16      | 8 bits | Destination Address| `00010000`     | `10`        | `0x10` (Conductor)          |
| 15-8       | 8 bits | Source Address     | `00100000`     | `20`        | `0x20` (Motion Executor) |
| 7-0        | 8 bits | Reserved           | `00000000`     | `00`        | Must be 0                   |
| **Full 29-bit ID** | | | | `0x05102000`|                             |

#### **Payload Construction (DLC = 8)**

| Byte(s) | Field Name      | Type    | Value (hex) | Description                                       |
|---------|-----------------|---------|-------------|---------------------------------------------------|
| `0-1`   | Command Code    | `UINT16`| `01 01`     | `0x0101` (MOTOR_ROTATE) - Little-Endian           |
| `2-3`   | Error Code      | `UINT16`| `00 00`     | No error                                          |
| `4-7`   | Padding         | -       | `00 00 00 00` | Zero padding                                    |
| **Full Payload** | | | `01 01 00 00 00 00 00 00` |                                           |

---

### Scenario 3: Executor sends `DONE` for `MOTOR_ROTATE (0x0101)` to Conductor

**Purpose**: Signal completion of `MOTOR_ROTATE`.
**Completed Command Code**: `0x0101`
**Source Address**: `0x20` (Motion Executor)
**Destination Address**: `0x10` (Conductor)

#### **CAN ID Construction**

| Bits       | Length | Field Name         | Value (binary) | Value (hex) | Description                 |
|------------|--------|--------------------|----------------|-------------|-----------------------------|
| 28-26      | 3 bits | Priority           | `001`          | `1`         | Normal priority response    |
| 25-24      | 2 bits | Message Type       | `11`           | `3`         | `DATA`/`DONE`/`LOG`         |
| 23-16      | 8 bits | Destination Address| `00010000`     | `10`        | `0x10` (Conductor)          |
| 15-8       | 8 bits | Source Address     | `00100000`     | `20`        | `0x20` (Motion Executor) |
| 7-0        | 8 bits | Reserved           | `00000000`     | `00`        | Must be 0                   |
| **Full 29-bit ID** | | | | `0x07102000`|                             |

#### **Payload Construction (DLC = 8)**

| Byte(s) | Field Name      | Type    | Value (hex) | Description                                       |
|---------|-----------------|---------|-------------|---------------------------------------------------|
| `0`     | Sub-Type        | `UINT8` | `01`        | `0x01` (DONE)                                     |
| `1-2`   | Command Code    | `UINT16`| `01 01`     | `0x0101` (MOTOR_ROTATE) - Little-Endian           |
| `3-7`   | Padding/status  | -       | `00 00 00 00 00` | Domain status or zero padding                 |
| **Full Payload** | | | `01 01 01 00 00 00 00 00` |                                      |

---

### Scenario 4: Executor sends `NACK` for `MOTOR_ROTATE (0x0101)` to Conductor (e.g., Invalid Motor ID)

**Purpose**: Signal an error in executing `MOTOR_ROTATE`.
**Acknowledged Command Code**: `0x0101`
**Source Address**: `0x20` (Motion Executor)
**Destination Address**: `0x10` (Conductor)
**Error Code**: `0x0001` (Example: Invalid Motor ID)

#### **CAN ID Construction**

| Bits       | Length | Field Name         | Value (binary) | Value (hex) | Description                 |
|------------|--------|--------------------|----------------|-------------|-----------------------------|
| 28-26      | 3 bits | Priority           | `001`          | `1`         | Normal priority response    |
| 25-24      | 2 bits | Message Type       | `10`           | `2`         | `NACK`                      |
| 23-16      | 8 bits | Destination Address| `00010000`     | `10`        | `0x10` (Conductor)          |
| 15-8       | 8 bits | Source Address     | `00100000`     | `20`        | `0x20` (Motion Executor) |
| 7-0        | 8 bits | Reserved           | `00000000`     | `00`        | Must be 0                   |
| **Full 29-bit ID** | | | | `0x06102000`|                             |

#### **Payload Construction (DLC = 8)**

| Byte(s) | Field Name      | Type    | Value (hex) | Description                                       |
|---------|-----------------|---------|-------------|---------------------------------------------------|
| `0-1`   | Command Code    | `UINT16`| `01 01`     | `0x0101` (MOTOR_ROTATE) - Little-Endian           |
| `2-3`   | Error Code      | `UINT16`| `01 00`     | `0x0001` (Invalid Motor ID) - Little-Endian       |
| `4-7`   | Padding         | -       | `00 00 00 00` | Zero padding                                    |
| **Full Payload** | | | `01 01 01 00 00 00 00 00` |                                           |

---

### Scenario 5: Executor sends `DATA` (e.g., Sensor Reading) to Conductor

**Purpose**: Transfer a sensor reading from an Executor to the Conductor.
**Source Address**: `0x30` (Reaction Disk Executor)
**Destination Address**: `0x10` (Conductor)
**Data**: Example temperature `25.5` degrees Celsius (represented as `255` x 0.1°C = `0x00FF`)

#### **CAN ID Construction**

| Bits       | Length | Field Name         | Value (binary) | Value (hex) | Description                 |
|------------|--------|--------------------|----------------|-------------|-----------------------------|
| 28-26      | 3 bits | Priority           | `001`          | `1`         | Normal priority response    |
| 25-24      | 2 bits | Message Type       | `11`           | `3`         | `DATA`/`DONE`/`LOG`         |
| 23-16      | 8 bits | Destination Address| `00010000`     | `10`        | `0x10` (Conductor)          |
| 15-8       | 8 bits | Source Address     | `00110000`     | `30`        | `0x30` (Reaction Disk Executor) |
| 7-0        | 8 bits | Reserved           | `00000000`     | `00`        | Must be 0                   |
| **Full 29-bit ID** | | | | `0x07103000`|                             |

#### **Payload Construction (DLC = 8)**

| Byte(s) | Field Name      | Type    | Value (hex) | Description                                       |
|---------|-----------------|---------|-------------|---------------------------------------------------|
| `0`     | Sub-Type        | `UINT8` | `02`        | `0x02` (DATA)                                     |
| `1`     | Sequence Info   | `UINT8` | `80`        | Sequence 0, End of Transmission                   |
| `2-3`   | Temperature     | `INT16` | `FF 00`     | `25.5` (255 x 0.1°C) - Little-Endian              |
| `4-7`   | Padding         | -       | `00 00 00 00` | Zero padding                                    |
| **Full Payload** | | | `02 80 FF 00 00 00 00 00` |                                           |

---

### Scenario 6: Executor sends `LOG` (e.g., Warning Message) to Conductor

**Purpose**: Send an asynchronous log message from an Executor.
**Source Address**: `0x30` (Reaction Disk Executor)
**Destination Address**: `0x10` (Conductor)
**Log Message**: "Warn"

#### **CAN ID Construction**

| Bits       | Length | Field Name         | Value (binary) | Value (hex) | Description                 |
|------------|--------|--------------------|----------------|-------------|-----------------------------|
| 28-26      | 3 bits | Priority           | `001`          | `1`         | Normal priority response    |
| 25-24      | 2 bits | Message Type       | `11`           | `3`         | `DATA`/`DONE`/`LOG`         |
| 23-16      | 8 bits | Destination Address| `00010000`     | `10`        | `0x10` (Conductor)          |
| 15-8       | 8 bits | Source Address     | `00110000`     | `30`        | `0x30` (Reaction Disk Executor) |
| 7-0        | 8 bits | Reserved           | `00000000`     | `00`        | Must be 0                   |
| **Full 29-bit ID** | | | | `0x07103000`|                             |

#### **Payload Construction (DLC = 8)**

| Byte(s) | Field Name      | Type    | Value (hex) | Description                                       |
|---------|-----------------|---------|-------------|---------------------------------------------------|
| `0`     | Sub-Type        | `UINT8` | `03`        | `0x03` (LOG)                                      |
| `1`     | Log Level       | `UINT8` | `02`        | `0x02` (WARN)                                     |
| `2-5`   | Log Message     | `STRING`| `57 61 72 6E` | "Warn" (UTF-8, up to 6 bytes)                     |
| `6-7`   | Padding         | -       | `00 00`     | Zero padding                                      |
| **Full Payload** | | | `03 02 57 61 72 6E 00 00` |                                             |

---

This document should provide a very clear and explicit guide for a programmer on how to construct and interpret CAN frames at the byte level.
