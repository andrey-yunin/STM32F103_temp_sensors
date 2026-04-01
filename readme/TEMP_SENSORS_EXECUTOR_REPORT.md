# Report on Completed Work & Future Plan: Temperature Sensors Executor

## 1. Objective

This document summarizes the development process for the new Temperature Sensors Executor (`STM32F103`). The goal is to create a dedicated module capable of reading data from 8x DS18B20 temperature sensors via the 1-Wire protocol and reporting the values via CAN bus. The project is based on the `STM32F103_pumps_valves` project.

## 2. Architectural Decisions

-   **MCU:** STM32F103
-   **Sensors:** 8x DS18B20 on a single 1-Wire bus.
-   **Addressing:** A unique, hard-coded `performer_id` of `2` is used for this Executor.
-   **RTOS Structure:** The project utilizes three tasks:
    1.  `task_can_handler`: For low-level CAN communication.
    2.  `task_dispatcher`: For processing incoming commands.
    3.  `task_temp_monitor`: A new dedicated task to manage the DS18B20 sensors, including discovery, periodic polling, **averaging multiple readings**, and storing the results.
-   **Global Variables:** Global variables, including the `g_latest_temperatures` array for sharing data between tasks, are centralized in `app_globals.h` and `app_globals.c`.

---

## 3. Summary of Completed Work

### 3.1. Phase 1: Project Setup & Configuration

-   **Action:** Copied the `STM32F103_pumps_valves` project to `STM32F103_temp_sensors`.
-   **Action:** Renamed all project files (`.ioc`, `.project`, `.launch`) to `STM32F103_temp_sensors` and updated their internal content.
-   **Action:** Assigned a unique `performer_id = 2` in `task_dispatcher.c`.
-   **Status:** Completed.

### 3.2. Phase 2: Driver Implementation

-   **Action (CubeMX):** Configured a single GPIO pin for the 1-Wire bus (e.g., `PA0`) as `Output Open-Drain`. Configured a general-purpose timer (`TIM3`) for microsecond delays.
-   **Action (Configuration):** Centralized `DS18B20_MAX_SENSORS` constant in `app_config.h`.
-   **Action (Driver Files):** Created `ds18b20.h` and `ds18b20.c`. Implemented low-level 1-Wire functions (`OneWire_Reset`, `OneWire_WriteBit`, `OneWire_ReadBit`, etc.) and higher-level (stubbed) functions for DS18B20 control.
-   **Status:** Completed.

### 3.3. Phase 3: Application Logic

-   **Action (Global Variable):** The global array `float g_latest_temperatures[DS18B20_MAX_SENSORS];` was correctly declared in `app_globals.h` and defined in `app_globals.c`.
-   **Action (New Task):** The new FreeRTOS task, `task_temp_monitor`, was defined and created in `main.c`. The corresponding files `task_temp_monitor.h` and `task_temp_monitor.c` were created with initial logic to periodically trigger sensor measurements, store ROM codes (stubbed), and update `g_latest_temperatures`.
-   **Action (Protocol):** The command `CMD_GET_TEMPERATURE = 0x12` was added to the shared `command_protocol.h`.
-   **Action (Handler):** A `case` for `CMD_GET_TEMPERATURE` was added to `task_dispatcher.c`. This handler reads the pre-calculated temperature from the `g_latest_temperatures` array and sends it back to the Director as a CAN response.
- **Status:** Completed. The project compiles without errors or warnings.

---

## 4. Phase 4: Industrial Refactoring (Benchmark: step_motors_refactored)

This phase focuses on upgrading the communication layer to professional industrial standards (Advanced Level) by adopting the architecture from the `step_motors_refactored` project.

### 4.1. Infrastructure & Protocol Layer (Step 1)
- [ ] Create `App/inc/can_protocol.h` following the benchmark structure (29-bit Extended ID).
- [ ] Define standardized command codes: `0x9011` (SENSOR_GET_TEMP) and `0x9010` (SENSOR_GET_ALL_TEMPS).
- [ ] Update `App/inc/app_queues.h`: Implement `can_rx_queue`, `can_tx_queue`, and `parser_queue` with proper typing.
- [ ] Define Event-Driven flags (`FLAG_CAN_RX`, `FLAG_CAN_TX`) in `app_config.h`.

### 4.2. Transport Layer - CAN Handler (Step 2)
- [ ] Refactor `App/src/tasks/task_can_handler.c` to use `osThreadFlagsWait` (Event-Driven).
- [ ] Implement hardware filtering for `CAN_ADDR_THERMO_BOARD` (0x40).
- [ ] Implement standardized response helpers (`CAN_SendAck`, `CAN_SendNack`, `CAN_SendData`, `CAN_SendDone`).

### 4.3. Application Logic - Dispatcher/Parser (Step 3)
- [ ] Refactor `App/src/tasks/task_dispatcher.c` to align with the benchmark's `task_command_parser.c`.
- [ ] Implement full transaction lifecycle (ACK -> DATA -> DONE) for sensor commands.
- [ ] Implement conversion logic for temperature data (`float` to `int16_t` 0.1°C resolution).

### 4.4. System Robustness & Standards
- [ ] Implement error handling for invalid sensor IDs (`CAN_ERR_INVALID_SENSOR_ID`).
- [ ] Verify thread-safe data access between polling and communication tasks.
- [x] Perform a final code audit against the industrial benchmark.

---

## 5. Phase 5: Industrial 1-Wire & DS18B20 Management

This phase focuses on the reliability and precision of the temperature sensing layer on a shared 1-Wire bus.
## 5. Phase 5: Industrial 1-Wire & DS18B20 Management

### 5.1. Low-Level Driver Enhancement (Phase 5.1) - Согласовано
- [x] **Direct Register Access (DRA):** Полный отказ от `HAL_GPIO_Init` в рабочих циклах. Использование регистров `BSRR/BRR` (запись) и `IDR` (чтение) для STM32F103. Пин зафиксирован в режиме `Open-Drain`.
- [x] **Search ROM Algorithm:** Реализация полноценного алгоритма поиска (переписи) всех 64-битных адресов датчиков на шине.
- [x] **Match ROM Protocol:** Переход на адресный опрос датчиков по их уникальным ID (исключение коллизий на общей шине).

### 5.2. Sensor Management Logic (Task Temp Monitor) - Согласовано
- [x] **Parallel Polling (Broadcast):** Все датчики запускают измерение одновременно командой `SKIP_ROM` + `0x44`.
- [x] **Non-blocking Delay:** Ожидание готовности (750мс) реализовано через `osDelay`, не блокируя другие задачи.
- [x] **Mapping-based Polling:** Задача опрашивает только привязанные ROM ID из таблицы конфигурации (подготовлено).

---

## 6. Phase 6: Service Layer & Persistent Storage (Industrial Mapping)

Данная фаза внедряет промышленный стандарт обслуживания "в поле" без необходимости перепрошивки Дирижера.

### 6.1. Persistent Storage (Internal Flash) - Согласовано
- [ ] **Flash Driver (`app_flash`):** Реализация записи/чтения в последнюю страницу Flash (Page 63 - 0x0800FC00).
- [ ] **Config Structure:** Хранение `MagicKey`, `PerformerID`, `Mapping Table` (8x ROM ID) и `CRC16`.
- [ ] **Factory State Handling:** Автоматическая инициализация настроек по умолчанию при первом запуске.

### 6.2. Service Protocol (0xFxxx Range) - Согласовано
- [ ] **Universal Commands (0xF0xx):** `SRV_GET_INFO`, `SRV_REBOOT`, `SRV_FLASH_COMMIT`.
- [ ] **Thermo-specific Commands (0xF1xx):** `SRV_SCAN_1WIRE`, `SRV_GET_PHYS_ID`, `SRV_SET_CHANNEL_MAP`.
- [ ] **Mapping Workflow:** Реализация процедуры "Теплого пальца" (Discovery -> Identification -> Mapping -> Commit).

### 6.3. System Integration
- [ ] **Dispatcher Update:** Обработка сервисных команд и отправка DATA-ответов с результатами сканирования.
- [ ] **Task Update:** Синхронизация `task_temp_monitor` с актуальной таблицей маппинга в RAM.

### 5.3. Error Handling & Diagnostics
- [ ] Implement `SENSOR_OFFLINE` status reporting via CAN.
- [ ] Add retry logic for CRC errors.
- [ ] Implement temperature out-of-range detection.

### 5.4. Initial Driver Audit (March 31, 2026)
Following a deep code review of `ds18b20.c`, the following findings were recorded:
- **Strengths:** Microsecond delays using `TIM3` are precise; `CRC8` algorithm is already implemented.
- **Weaknesses:** GPIO switching via `HAL_GPIO_Init` is too slow for high-speed 1-Wire communication; `DS18B20_Init` is currently a stub that only supports one sensor via `SKIP_ROM`.
- **Verdict:** The driver is not yet ready for the multi-sensor shared bus (8 sensors). A full implementation of the **Search ROM** algorithm and optimization of GPIO access (Direct Register Access) are required.

---

## 6. Testing & Validation Plan

- [x] **Internal Architecture Audit:** Verified compliance with `step_motors_refactored` benchmark.
- [ ] **1-Wire Multi-Sensor Discovery:** Verify that all 8 sensors are detected and their unique ROM codes are stored.
- [ ] **Transaction Trace:** Validate the COMMAND-ACK-DATA-DONE flow via CAN analyzer.
- [ ] **Data Precision:** Confirm 0.1°C resolution in CAN frames.

---

## 7. Current Status & Future Work

- **Status:** 
    - [x] Phase 4 (Industrial CAN Refactoring) is **100% Completed**. Project compiles and follows DDS-240 standards.
    - [ ] Phase 5 (Industrial 1-Wire) is in **Research/Audit** stage.
- **Next Action (Tomorrow):** Start Phase 5.1 (GPIO optimization) and Phase 5.2 (Search ROM algorithm implementation).


### 5.1. Protocol Compliance Validation
- [ ] **Extended ID Test:** Verify correct assembly/disassembly of the 29-bit ID fields.
- [ ] **Transaction Sequence:** Confirm the COMMAND-ACK-DATA-DONE flow via CAN analyzer.
- [ ] **Data Precision:** Validate `int16_t` conversion accuracy (25.4°C -> `0x00FE`).

### 5.2. Integration Testing
- [ ] **End-to-End:** Test communication between the H7 Motherboard (Conductor) and this F103 module.
- [ ] **Error Handling:** Verify `NACK` generation for invalid requests.

---

## 6. Testing Phase (Legacy/Initial)

### 6.1. Update Director for Testing (Archive)
-   **Action:** The Director's (`STM32H723_mother_board`) `command_protocol.h` was updated to include `CMD_GET_TEMPERATURE`.
-   **Action:** The temporary test block in the Director's `task_can_handler.c` was modified to send a `CMD_GET_TEMPERATURE` command for sensor `0` to Performer ID `2`.
-   **Status:** Completed (Initial test setup).

### 6.2. Full Physical Test
-   **Status:** PENDING.
-   **Action:**
    1.  **Hardware Setup:** Connect via CAN transceivers.
    2.  **Firmware:** Flash updated firmware to both boards.
    3.  **Execution:** Power on and verify communication.