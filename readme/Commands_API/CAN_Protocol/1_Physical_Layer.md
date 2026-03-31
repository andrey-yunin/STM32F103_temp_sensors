# CAN Protocol: 1. Physical & Data Link Layer

---

## 1.1. Physical Layer

| Parameter         | Value              | Notes                               |
|-------------------|--------------------|-------------------------------------|
| **Bit Rate**      | 1 Mbit/s           |                                     |
| **CAN Standard**  | Classical CAN      | Compatible with STM32F103.          |
| **Termination**   | 120 Ohm            | Standard termination is required.   |
| **Connector**     | DB9 Male           | On the device side.                 |

### Pinout (DB9 Connector)

| Pin | Signal  | Description         |
|-----|---------|---------------------|
| 2   | CAN-L   | CAN Low Line        |
| 3   | GND     | Ground              |
| 7   | CAN-H   | CAN High Line       |

## 1.2. Data Link Layer

| Parameter          | Value              | Notes                               |
|--------------------|--------------------|-------------------------------------|
| **CAN ID**         | 29-bit (Extended)  | See `2_Frame_Format.md` for details. |
| **DLC**            | 8 bytes            | Max payload for Classical CAN.      |