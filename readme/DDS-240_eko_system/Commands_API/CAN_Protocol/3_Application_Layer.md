# CAN Protocol: 3. Application Layer

---

This document describes the application-level protocol for command execution, data transfer, and logging between the **Conductor** and **Executors** over the CAN bus.

## 3.1. Command Execution Flow

The typical command execution sequence is as follows:

1.  **Conductor -> Executor**: The Conductor sends a `COMMAND` frame with **DLC=8** and zero padding.
2.  **Executor -> Conductor**: The Executor immediately responds with an `ACK` frame (**DLC=8**) upon receiving and successfully parsing the command.
    *   If parsing fails or the command is invalid, the Executor sends a `NACK` frame (**DLC=8**) with an appropriate error code.
3.  **Executor -> Conductor**: If the command requires returning data (e.g., `READ_ADC`), the Executor sends one or more `DATA` frames (**DLC=8**).
4.  **Executor -> Conductor**: Once the command has fully completed its execution contract (e.g., a motor has stopped moving or a pump has finished `RUN_DURATION` and switched off), the Executor sends a `DONE` frame (**DLC=8**).

```
┌───────────┐                                 ┌──────────┐
│ Conductor │                                 │ Executor │
└───────────┘                                 └──────────┘
     │            COMMAND Frame (DLC=8)          │
     │ ────────────────────────────────────────> │
     │                                           │
     │               ACK Frame (DLC=8)           │
     │ <──────────────────────────────────────── │
     │                                           │
     │      (Optional) DATA Frame(s) (DLC=8)     │
     │ <──────────────────────────────────────── │
     │                                           │
     │               DONE Frame (DLC=8)          │
     │ <──────────────────────────────────────── │
     │                                           │
```

## 3.2. Sub-Payload Structures

As defined in `2_Frame_Format.md`, messages with `Message Type == 11` use the first byte of their payload to define a `Sub-Type`.

### `DONE` Sub-Payload (DLC = 8)

*   **Sub-Type**: `0x01`

| Byte(s)  | Field Name      | Description                                                 |
|----------|-----------------|-------------------------------------------------------------|
| `0`      | Sub-Type (`0x01`)| Identifies this frame as a `DONE` message.                    |
| `1-2`    | **Low-Level** Command Code | The 16-bit code of the command that has finished.           |
| `3-7`    | Status/channel/padding | Domain-specific channel/status bytes followed by zero padding. |

### `DATA` Sub-Payload (DLC = 8)

*   **Sub-Type**: `0x02`

| Byte(s)  | Field Name      | Description                                                 |
|----------|-----------------|-------------------------------------------------------------|
| `0`      | Sub-Type (`0x02`)| Identifies this frame as a `DATA` message.                    |
| `1`      | Sequence Info   | Contains sequence number and end-of-transmission flag.      |
| `2-7`    | Data            | 1 to 6 bytes of payload data.                               |

**Sequence Info Byte (Byte 1):**

*   **Bit 7 (EOT)**: `1` if this is the last frame in the sequence; `0` otherwise.
*   **Bits 6-0**: Sequence number (0-127). Increments for each frame in a multi-frame transfer.

### `LOG` Sub-Payload (DLC = 8)

*   **Sub-Type**: `0x03`

| Byte(s)  | Field Name      | Description                                                 |
|----------|-----------------|-------------------------------------------------------------|
| `0`      | Sub-Type (`0x03`)| Identifies this frame as a `LOG` message.                     |
| `1`      | Log Level       | `1`=ERROR, `2`=WARN, `3`=INFO, `4`=DEBUG.                   |
| `2-7`    | Log Message     | 1 to 6 bytes of a UTF-8 encoded string. Log messages longer than 6 bytes will be sent as multiple `LOG` frames. |
