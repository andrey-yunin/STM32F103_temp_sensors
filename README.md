# STM32F103 Temperature Sensors Executor (DDS-240 Standard)

![STM32](https://img.shields.io/badge/MCU-STM32F103-blue.svg)
![FreeRTOS](https://img.shields.io/badge/RTOS-FreeRTOS%20(CMSIS--V2)-green.svg)
![Protocol](https://img.shields.io/badge/Protocol-DDS--240%20(CAN%202.0B)-orange.svg)
![Sensors](https://img.shields.io/badge/Sensors-8x%20DS18B20-red.svg)

Industrial-grade execution module for high-precision temperature monitoring in biochemical analyzers. This firmware manages up to 8 DS18B20 sensors on a shared 1-Wire bus, providing data via a transactional CAN protocol (DDS-240).

## 🚀 Key Features

- **Industrial 1-Wire Driver**: Direct Register Access (DRA) using `BSRR/BRR` registers for precise bit-banging timings on STM32F103.
- **Auto-Discovery**: Full implementation of the Maxim Integrated Search ROM algorithm for automatic detection of all sensors on the bus.
- **DDS-240 CAN Protocol**: 29-bit Extended ID communication with full transaction lifecycle management (COMMAND -> ACK -> DATA -> DONE).
- **Advanced Service Layer (0xFxxx)**: Remote management capabilities, including "Warm Finger" sensor mapping and identification without reflashing.
- **Identity Management**: Unique identification using 96-bit factory-programmed MCU UID and 8-bit Device Type (0x40).
- **Fail-Safe Storage**: Internal Flash-based persistent storage for sensor mapping with CRC16 and MagicKey validation.
- **Thread-Safe Architecture**: Fully asynchronous task-based design using FreeRTOS Mutexes, Semaphores, and Message Queues.

## 🏗 System Architecture

The firmware is structured into three specialized RTOS tasks to ensure high availability and responsiveness:

1.  **CAN Handler Task**: Manages low-level bxCAN hardware, interrupt-driven RX/TX buffering, and hardware-level destination address filtering.
2.  **Dispatcher Task**: Implements the application-level command parser and handles the Command/Response state machine (Service & Operational commands).
3.  **Temperature Monitor Task**: Executes the industrial polling cycle (Broadcast Start -> RTOS Wait -> Match ROM Sequential Read) for all mapped sensors.

## 📡 Service & Identity

Each board is uniquely identified and managed within the analyzer ecosystem:
- **Device Type**: `0x40` (Thermo Board).
- **Unique ID**: 96-bit MCU UID used for physical instance identification.
- **Service API**:
    - `0xF001`: Request Device Info (Type, FW Version, 96-bit UID).
    - `0xF002`: Remote Reboot with 0xDEAD Magic Key protection.
    - `0xF101`: Trigger 1-Wire Bus Scan and discovery.
    - `0xF103`: Map physical ROM ID to a logical channel (e.g., "Incubator", "Reagents").

## 🛠 Hardware Requirements

- **MCU**: STM32F103C8T6.
- **CAN Transceiver**: TJA1050 / SN65HVD230.
- **Sensors**: DS18B20 (up to 8 units) on a single bus.
- **Timer Configuration**: `TIM3` configured for 1µs resolution for precise 1-Wire delays.

## 📂 Project Structure

- `App/src/tasks/`: Implementation of the RTOS tasks and application logic.
- `App/src/ds18b20.c`: Optimized 1-Wire driver with full Search ROM support.
- `App/src/app_flash.c`: Reliable persistent configuration management.
- `readme/`: Detailed specifications of the CAN protocol and architectural concepts.

## 📖 Documentation

Detailed documentation is available in the `readme/` directory:
- [Service Infrastructure Concept](readme/SERVICE_INFRASTRUCTURE_CONCEPT.md)
- [CAN Protocol: Application Layer](readme/Commands_API/CAN_Protocol/3_Application_Layer.md)
- [Low-Level Command Set](readme/Commands_API/CAN_Protocol/5_Low_Level_Commands.md)

---
*Developed as part of the SmartHeater & Analyzer ecosystem. Advanced Level Engineering.*
