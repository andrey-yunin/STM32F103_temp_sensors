# CAN Protocol: 2. Frame Format

---

## 2.1. CAN ID Structure (29-bit Extended)

The 29-bit CAN ID is structured to convey priority, message type, source, and destination, enabling robust filtering and routing.

| Bits       | Length | Field Name         | Description                                                 |
|------------|--------|--------------------|-------------------------------------------------------------|
| 28-26      | 3 bits | Priority           | Message priority (0 = highest, 7 = lowest).                 |
| 25-24      | 2 bits | Message Type       | Defines the purpose of the frame (see table below).         |
| 23-16      | 8 bits | Destination Address| Address of the recipient node.                              |
| 15-8       | 8 bits | Source Address     | Address of the sender node.                                 |
| 7-0        | 8 bits | Reserved           | Reserved for future use (must be 0).                        |

### Message Types

| Value (binary) | Name    | Description                                       |
|----------------|---------|---------------------------------------------------|
| `00`           | `COMMAND` | A command from the Conductor to an Executor.      |
| `01`           | `ACK`     | Positive acknowledgment of a command.             |
| `10`           | `NACK`    | Negative acknowledgment (error).                  |
| `11`           | `DATA` / `DONE` / `LOG` | Asynchronous message from an Executor to the Conductor. |

### Node Addresses

A table of node addresses must be maintained here.

| Address (hex) | Node Name                    | Description                                |
|---------------|------------------------------|--------------------------------------------|
| `0x00`        | Broadcast                    | All nodes.                                 |
| `0x01`        | Host Controller (PC)         | The main controlling computer (communicates via USB). |
| `0x10`        | Conductor (Main Board)       | The STM32H723 main board.                  |
| `0x20`        | Stepper Motor Control Board  | Manages stepper motors (STM32F103).        |
| `0x30`        | Pump and Valve Control Board | Manages pumps and valves (STM32F103).      |
| `0x40`        | Thermosensor Board           | Manages temperature sensors (STM32F103).   |
| ...           | ...                          | ...                                        |

## 2.2. Data Payload & DLC

The Data Length Code (DLC) and payload structure are **variable** and depend on the `Message Type`. See `6_Parameter_Packing.md` for a detailed justification.

### `COMMAND` Payload (DLC is command-specific)

| Byte(s)  | Field Name      | Description                                                 |
|----------|-----------------|-------------------------------------------------------------|
| `0-1`    | **Low-Level** Command Code | The 16-bit code for a hardware-level action. See `5_Low_Level_Commands.md`. |
| `2-7`    | Parameters      | Command-specific, low-level parameters. Unused bytes are zero-padded. |

All `Conductor <-> Executor` frames use strict `DLC=8`. Older notes about logical payload length describe used bytes only and must not be interpreted as variable CAN DLC on the executor bus.

### `ACK` / `NACK` Payload (DLC = 8)

| Byte(s)  | Field Name      | Description                                                 |
|----------|-----------------|-------------------------------------------------------------|
| `0-1`    | **Low-Level** Command Code | The 16-bit code of the command being acknowledged.          |
| `2-3`    | Error Code      | For `NACK` only. A 16-bit code detailing the error. 0 for `ACK`. |

### `DATA` / `DONE` / `LOG` Payload (`Message Type == 11`, DLC = 8)

A sub-type in the first byte differentiates these messages. Unused bytes are zero-padded.

| Sub-Type | Payload Description                                          |
|----------|--------------------------------------------------------------|
| `DONE`   | `0x01` (Sub-Type) + 16-bit Low-Level Command Code + optional channel/status bytes, then zero padding |
| `DATA`   | `0x02` (Sub-Type) + Sequence Info + 1-6 bytes of data.       |
| `LOG`    | `0x03` (Sub-Type) + Log Level + 1-6 bytes of message.        |

*For detailed payload structures, see `3_Application_Layer.md`.*
