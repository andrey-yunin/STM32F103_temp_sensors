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
- [x] Create `App/inc/can_protocol.h` following the benchmark structure (29-bit Extended ID).
- [x] Define standardized command codes: `0x9011` (SENSOR_GET_TEMP) and `0x9010` (SENSOR_GET_ALL_TEMPS).
- [x] Update `App/inc/app_queues.h`: Implement `can_rx_queue`, `can_tx_queue`, and `parser_queue` with proper typing.
- [x] Define Event-Driven flags (`FLAG_CAN_RX`, `FLAG_CAN_TX`) in `app_config.h`.

### 4.2. Transport Layer - CAN Handler (Step 2)
- [x] Refactor `App/src/tasks/task_can_handler.c` to use `osThreadFlagsWait` (Event-Driven).
- [x] Implement hardware filtering for `CAN_ADDR_THERMO_BOARD` (динамический ID через AppConfig).
- [x] Implement standardized response helpers (`CAN_SendAck`, `CAN_SendNack`, `CAN_SendData`, `CAN_SendDone`).

### 4.3. Application Logic - Dispatcher/Parser (Step 3)
- [x] Refactor `App/src/tasks/task_dispatcher.c` to align with the benchmark's `task_command_parser.c`.
- [x] Implement full transaction lifecycle (ACK -> DATA -> DONE) for sensor commands.
- [x] Implement conversion logic for temperature data (`float` to `int16_t` 0.1°C resolution).

### 4.4. System Robustness & Standards
- [x] Implement error handling for invalid sensor IDs (`CAN_ERR_INVALID_SENSOR_ID`).
- [x] Verify thread-safe data access between polling and communication tasks.
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
- [x] **Flash Driver (`app_flash`):** Реализация записи/чтения в последнюю страницу Flash.
- [x] **Config Structure:** Хранение `MagicKey`, `PerformerID`, `Mapping Table` (8x ROM ID) и `CRC16`.
- [x] **Mutex Protection:** Внедрение `osMutex` для безопасного доступа к `g_app_config`.

### 6.2. Service Protocol (0xFxxx Range) - 100% Completed
- [x] **Architectural Design:** Внедрена концепция "Identity Object" (Type ID + Unique UID + Instance).
- [x] **Universal Commands (0xF0xx):** Реализованы `SRV_GET_INFO` (возврат 96-bit UID чипа), `SRV_REBOOT` (Magic Key `0xDEAD`), `SRV_FLASH_COMMIT`.
- [x] **Thermo-specific Commands (0xF1xx):** Реализованы `SRV_SCAN_1WIRE`, `SRV_GET_PHYS_ID`, `SRV_SET_CHANNEL_MAP`.
- [x] **Security:** Внедрена проверка Magic Keys и аппаратная фильтрация по DstAddr.

### 6.3. System Integration - 100% Completed
- [x] **Dispatcher Update:** Полная поддержка сервисных транзакций (ACK -> DATA -> DONE).
- [x] **Data Integrity:** Расширена структура `ParsedCanCommand_t` (8 байт данных) для ROM ID.
- [x] **Encapsulation:** Все зависимости и инклуды приведены в соответствие с промышленными стандартами.

---

## 7. Current Status & Future Work

- **Status:**
    - [x] Phase 4 (Industrial CAN Refactoring) is **100% Completed**.
    - [x] Phase 5 (1-Wire Implementation) is **100% Completed**.
    - [x] Phase 6 (Service Layer & Flash Storage) is **100% Completed**.
- **Result:** Проект полностью соответствует архитектурному стандарту DDS-240 и готов к комплексным тестам.

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
    - [x] Phase 6.4 (Advanced Encapsulation) is **100% Completed**. Global variables removed.
    - [x] Phase 5.1 & 5.2 (1-Wire Implementation) is **100% Completed**. Sensors polled correctly via Match ROM.
- **Next Action (Tomorrow):** Start Phase 6.2 (Service Layer) and 6.3 (System Integration) for remote sensor mapping.


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

    ---

    ## 8. Final Project Status (April 2, 2026)

    ### 8.1. Summary of Advanced Features
    - **Identity Management:** Unique 96-bit MCU UID is now part of the `SRV_GET_INFO` response.
    - **Dynamic Addressing:** NodeID is fully configurable via CAN and stored in Flash.
    - **Transactional Service Layer:** Complete implementation of DDS-240 service range (0xFxxx).
    - **Benchmark Status:** This project is officially designated as the **"Golden Template"** for industrial CAN executors in the SmartHeater ecosystem.

    ### 8.2. Transfer to Next Modules
    The architecture (Flash storage, Dynamic CAN filtering, and Service Dispatcher) is ready to be ported to:
    1. Stepper Motor Boards (`0x20`)
    2. Pump/Valve Boards (`0x30`)
    3. Conductor Board (`0x10`)

    ---

## 9. Финальная синхронизация с экосистемой (Апрель 2026) - 100% COMPLETED

Проект приведен к финальному промышленному стандарту согласно аудиту `step_motors_refactored`.

### 9.1. Сетевой уровень (CAN Upgrade)
*   [x] **Broadcast Support:** bxCAN переведен в режим открытого фильтра. Внедрена программная фильтрация `DstAddr` (NodeID + 0x00) в `task_can_handler.c`.
*   [x] **TX Reliability (Mailbox Guard):** Реализовано ожидание свободных Mailbox перед отправкой с таймаутом 10мс.

### 9.2. Код-стайл и Стандартизация (Dispatcher)
*   [x] **Error Constants:** Литералы ошибок заменены на стандартные константы `CAN_ERR_INVALID_KEY`, `CAN_ERR_FLASH_WRITE`, `CAN_ERR_INVALID_PARAM`.
*   [x] **Factory Reset:** Реализована обработка команды `0xF006` с проверкой Magic Key `0xFACE`.

### 9.3. Целостность данных (Flash & Safety)
*   [x] **CRC16 Checksum:** Внедрен расчет CRC16 (Poly 0xA001, Modbus) для структуры `AppConfig_t`.
*   [x] **Encapsulation:** Логика стирания Flash вынесена в `AppConfig_FactoryReset()`.

---

## 10. Итоговый статус (6 Апреля 2026)

- **Статус:** **STABLE / PRODUCTION READY**
- **Архитектура:** DDS-240 Compliant.
- **Интеграция:** Полная совместимость с Дирижером (H7) и сервисными инструментами экосистемы.
- **Результат:** Проект утвержден как "Golden Template" для модулей исполнения на базе STM32F103.

---

## 11. КРИТИЧЕСКИЙ АУДИТ И ВЫЯВЛЕННЫЕ ПРОБЛЕМЫ (8 Апреля 2026)

В ходе интеграции с Дирижером (согласно `CONDUCTOR_INTEGRATION_GUIDE`, п. 9) выявлены фундаментальные несоответствия, делающие текущую версию неработоспособной в рамках обновленной Экосистемы 2.0.

### 11.1. Проблема MTU (Физический уровень CAN)
Команда `SRV_SET_CHANNEL_MAP (0xF103)` требует передачи **8 байт ROM ID**. При текущей структуре кадра (2 байта код + 1 байт индекс) полезная нагрузка в одном кадре ограничена **5 байтами** (DLC=8).
**Текущий код содержит логическую ошибку:** диспетчер ожидает `data_len >= 8`, что физически невозможно получить в одном стандартном CAN-фрейме. Команда всегда возвращает NACK.

### 11.2. Несогласованность Магических Ключей
Ключи для критических операций (`REBOOT`, `FACTORY_RESET`) в проекте (`0xDEAD`, `0xFACE`) не соответствуют Директиве 2.0 (`0x55AA`, `0xDEAD`). Это блокирует возможность централизованного управления флотом устройств со стороны Дирижера.

### 11.3. Отсутствие контроля Strict DLC=8
Текущий транспортный уровень допускает прием кадров переменной длины. Дирижера требует жесткой фильтрации `DLC=8` для защиты от помех и унификации обработки на стороне исполнителей.

---

## 12. ПЛАН СТРАТЕГИЧЕСКОЙ КОРРЕКТИРОВКИ (Directive 2.0 Alignment) — ВЫПОЛНЕНО

### 12.1. Унификация транспорта (Transport Layer)
- [x] **Strict DLC=8:** Модернизация `task_can_handler.c` для игнорирования любых команд `COMMAND` с DLC != 8.
- [x] **Response DLC=8:** Приведение всех ответов (ACK/NACK/DONE/DATA) к DLC=8 для полной унификации трафика.

### 12.2. Синхронизация Сервисного Слоя (Service Layer)
- [x] **Magic Keys Sync:** Замена ключей в `can_protocol.h` на общесистемные (`0x55AA` и `0xDEAD`).
- [x] **Service Constants:** Актуализация кодов ошибок.

### 12.3. Исправление Динамической Адресации (1-Wire Mapping)
- [x] **Multi-Frame Protocol:** Разделение команды `SET_CHANNEL_MAP` на две фазы:
    - `0xF103 (PART 1)`: Передача первых 4 байт ROM ID.
    - `0xF105 (PART 2)`: Передача оставшихся 4 байт и финализация записи в RAM/Flash.
- [x] **Dispatcher Update:** Реализация промежуточного буфера для сборки ROM ID в `task_dispatcher.c`.

### 12.4. Валидация и Ре-аттестация
- [x] Сквозное тестирование цепочки: Дирижер (H7) -> Команда (DLC8) -> Исполнитель (F103).
- [x] Проверка корректности записи 1-Wire Map во Flash после исправлений.
- [x] **Golden Template 2.0 Status:** Проект полностью соответствует Директиве 2.0 от 08.04.2026.

---

## 13. ЗАКЛЮЧЕНИЕ ПО МОДЕРНИЗАЦИИ (Directive 2.0)
Работы по приведению исполнителя термодатчиков к стандарту Экосистемы 2.0 завершены. Все выявленные критические ошибки (несоответствие MTU, устаревшие ключи) устранены. Проект утвержден как **"Golden Template 2.0 Compliant"** и готов к тиражированию на другие модули системы.

---

## 14. Корректирующий аудит после возврата к последнему коммиту (08.05.2026)

Статус разделов 9-13 выше признан историческим и не является текущим промышленным статусом проекта. После сверки с актуальной общей экосистемой DDS-240, `dds240_global_config.h`, `CONDUCTOR_INTEGRATION_GUIDE.md`, Motion Executor и Fluidics Executor текущий статус Thermo Executor: **требуется целевой рефакторинг перед промышленной приемкой**.

Код в рамках этого аудита не изменялся.

### 14.1. Подтверждено как уже реализованное

- [x] DS18B20 Search ROM / Match ROM logic.
- [x] 1-Wire mapping через Flash.
- [x] Двухфазная передача ROM ID через `F103` / `F105`.
- [x] Базовые команды `0x9010 SENSOR_GET_ALL_TEMPS` и `0x9011 SENSOR_GET_TEMP`.
- [x] Базовые service-команды `F001/F002/F003/F005/F006`.
- [x] Физический `DLC=8` для ответов ACK/NACK/DATA/DONE.

### 14.2. Подтвержденные несоответствия

- [x] `DeviceType` Thermo смешан с NodeID: исправлено в `can_protocol.h`, теперь Thermo DeviceType `0x02` при NodeID `0x40`.
- [x] Обязательные service-команды `F004 GET_UID` и `F007 GET_STATUS`: обе реализованы в dispatcher, проект собирается.
- [~] Общая диагностика `GET_STATUS` с метриками `0x0001..0x0012`: `F007` реализован, константы, `CanDiagnostics_t` и snapshot API добавлены; инкремент счетчиков еще требуется.
- [~] TX diagnostics: добавлен central TX helper, считаются `TX_TOTAL` и `TX_QUEUE_OVERFLOW`; mailbox timeout / HAL error еще требуют отдельного блока.
- [ ] Wrong DLC сейчас обрабатывается NACK-ом, а должен быть silent drop + diagnostic counter.
- [x] Команда `F104 GET_CHANNEL_MAP` реализована: возвращает текущий ROM ID канала двумя DATA-кадрами.
- [ ] CAN timing и `TransmitFifoPriority` не совпадают с проверенными Motion/Fluidics исполнителями.
- [ ] Последняя Flash page не защищена linker script.
- [ ] TIM3 для 1-Wire `delay_us()` требует явного подтверждения запуска.

### 14.3. Scoped NACK registry

В Motion/Fluidics и Thermo одинаковые числовые NACK-коды могут иметь разные доменные имена. Это допустимо, потому что NACK интерпретируется в контексте транзакции: source NodeID + command code + error code.

Фактическое состояние по трем проектам:

- [x] Motion: `0x0002 INVALID_MOTOR_ID`, `0x0003 MOTOR_BUSY`.
- [x] Fluidics: `0x0002 INVALID_DEVICE_ID`, `0x0003 DEVICE_BUSY`.
- [x] Thermo: `0x0002 INVALID_SENSOR_ID`, `0x0003 SENSOR_FAILURE`, `0x0007 BUSY`.
- [x] Новые числовые значения для Thermo-specific NACK не назначаются.
- [x] Эти scoped aliases добавлены в глобальный конфиг.
- [x] Интеграционный guide обновлен: Thermo NACK registry теперь описывает scoped semantics для `INVALID_SENSOR_ID`, `SENSOR_FAILURE` и `THERMO_BUSY`.
- [ ] Дирижер должен явно транслировать Thermo `SENSOR_FAILURE` в Host/API `TEMP_DATA.status = 3` и/или ошибки группы `0x80xx`.

`CAN_ERR_SENSOR_FAILURE = 0x0003` в Thermo-коде не является самостоятельным блокером и не должен меняться механически.

### 14.4. Актуальный план

Рабочий план с маркерами выполнения вынесен в `readme/THERMO_EXECUTOR_REFACTORING_PLAN.md`.

---

## 15. Сессия рефакторинга 08.05.2026: выполненные корректировки

Статус на конец сессии: кодовая часть текущего большого рефакторинга собирается без ошибок и предупреждений. Лабораторная приемка на CANable и реальной 1-Wire шине еще не выполнялась.

### 15.1. CAN transport и диагностика

- [x] Очереди CAN RX/TX/dispatcher/thermo приведены к разделению ответственности.
- [x] Dispatcher больше не выполняет прямые DS18B20 операции; доменные команды передаются владельцу 1-Wire шины, `task_temp_monitor`.
- [x] Добавлен единый TX helper для ACK/NACK/DATA/DONE.
- [x] Реализованы counters для `F007 GET_STATUS`: RX/TX totals, queue overflows, silent drops, mailbox timeout, HAL errors, CAN error callback, warning/passive/bus-off, last HAL error, last ESR.
- [x] `HAL_CAN_ErrorCallback()` подключен к диагностике через CAN SCE IRQ.
- [x] RX callback оставлен в USER CODE блоке и выполняет только короткую IRQ-операцию: чтение FIFO0, постановка кадра в очередь, учет overflow.
- [x] Кадры с Standard ID, wrong dst, wrong type и wrong DLC отбрасываются без NACK и учитываются в диагностике.

### 15.2. Service layer DDS-240

- [x] Разделены NodeID Thermo `0x40` и DeviceType Thermo `0x02`.
- [x] Реализованы `F004 GET_UID`, `F007 GET_STATUS`, `F104 GET_CHANNEL_MAP`.
- [x] `F001 GET_DEVICE_INFO` возвращает DeviceType, firmware version, количество каналов и UID фрагментами.
- [x] `F103/F105` сохранены как двухфазная запись 64-bit ROM ID при строгом `DLC=8`.

### 15.3. Flash/config

- [x] `AppConfig_t` приведен к фиксированному layout: `performer_id` хранится как 8-bit CAN NodeID.
- [x] CRC16 конфигурации считается через `offsetof(AppConfig_t, checksum)`, строго до поля `checksum`.
- [x] `AppConfig_SetPerformerID()` сохраняет только младший байт NodeID.
- [ ] Последняя страница Flash пока не зарезервирована в linker script; это остается отдельным блоком.

### 15.4. DS18B20 / 1-Wire domain

- [x] Добавлен публичный валидатор `DS18B20_IsValidROM()`: family code `0x28` + CRC8.
- [x] `OneWire_CRC8()` приведена к `const`-correct сигнатуре.
- [x] `F103/F105` защищены от смешивания половин ROM разных каналов: добавлено pending-состояние с проверкой `sensor_id`.
- [x] `F105` принимает только валидный DS18B20 ROM или пустой канал `FF FF FF FF FF FF FF FF`.
- [x] Runtime опрос temperature mapping использует `DS18B20_IsValidROM()`, а не только проверку первого байта.

### 15.5. Проверка сборки

Команда проверки:

```bash
PATH=/home/andrey/st/stm32cubeide_1.19.0/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.linux64_1.0.0.202410170706/tools/bin:$PATH make -B -j14 all
```

Результат:

```text
Finished building target: STM32F103_temp_sensors.elf
text: 36456, data: 100, bss: 10072, dec: 46628, hex: b624
```

Compiler warnings в финальном выводе сборки не обнаружены.

### 15.6. Оставшаяся часть перед промышленной приемкой

- [x] Привести CAN hardware profile к эталону STM32F103: bit timing и `TransmitFifoPriority`.
- [x] Подтвердить запуск TIM3 для `delay_us()` в runtime.
- [x] Зарезервировать config Flash page в linker script.
- [x] Согласовать модель `TEMP_DATA.status` на уровне Executor -> Conductor -> Host.
- [x] Закрыть Thermo safety baseline: безопасное состояние 1-Wire, запрет ложного нормального результата при fault/stale, корректный `ACK without DONE` recovery.
- [ ] Реализовать обязательный блок экосистемы: Thermo safe-state hook, RTOS heartbeat и IWDG supervisor.
- [ ] Проверить watchdog idle, fault-injection зависания критической задачи и recovery/discovery после reset.
- [ ] Выполнить лабораторную приемку с CANable и несколькими DS18B20 на одной 1-Wire шине.

---

## 16. Сессия 12.05.2026: CAN hardware profile через CubeMX

Блок E из `THERMO_EXECUTOR_REFACTORING_PLAN.md` закрыт по кодовой части.

- [x] CAN bit timing Thermo Executor приведен к эталону STM32F103 экосистемы: `APB1=32 MHz`, `Prescaler=2`, `BS1=11TQ`, `BS2=4TQ`, `SJW=1TQ`.
- [x] Расчетный bitrate теперь `1 Mbit/s`, sample point `75%`.
- [x] `TransmitFifoPriority = ENABLE` включен для сохранения порядка многокадровых ответов `ACK -> DATA... -> DONE`.
- [x] Изменение выполнено через CubeMX; `STM32F103_temp_sensors.ioc` и `Core/Src/main.c` синхронизированы.
- [x] Профиль совпадает с Motion Executor и Fluidics Executor.
- [x] Сборка после генерации CubeMX подтверждена пользователем.
- [~] CubeMX также нормализовал окончания строк в `Core/Src/stm32f1xx_it.c` и `Core/Inc/stm32f1xx_it.h`; функциональных изменений в этих файлах при аудите не выявлено.

Остается лабораторная приемка блока E:

- [ ] CANable должен подтвердить стабильный обмен на `1 Mbit/s`.
- [ ] `F001` / `F007` должны подтвердить порядок многокадровых ответов: все `DATA` до `DONE`.

---

## 17. Сессия 12.05.2026: TIM3 runtime для 1-Wire

Блок F из `THERMO_EXECUTOR_REFACTORING_PLAN.md` закрыт по кодовой части запуска таймера.

- [x] Подтверждено, что до правки в проекте отсутствовал вызов `HAL_TIM_Base_Start(&htim3)`.
- [x] `delay_us()` в `App/src/ds18b20.c` использует счетчик TIM3; без запущенного таймера 1-Wire операции могли зависнуть в цикле ожидания.
- [x] Запуск TIM3 добавлен в `Core/Src/main.c` внутри `USER CODE BEGIN TIM3_Init 2`, после полной HAL-настройки TIM3.
- [x] TIM3 сохраняет микросекундную базу: `APB1 timer clock = 64 MHz`, `Prescaler = 63`, `1 tick = 1 us`.
- [x] Аудит вставки подтвержден: код находится в USER CODE блоке функции `MX_TIM3_Init()` и не должен стираться CubeMX.

Остается приемка блока F:

- [x] Сборка после добавления `HAL_TIM_Base_Start(&htim3)` подтверждена пользователем.
- [ ] Проверить `F101` scan на реальной 1-Wire шине с несколькими DS18B20.
- [ ] Проверить `F102` для каждого найденного ROM.
- [ ] Подтвердить, что CRC-invalid ROM не попадает в mapping.

---

## 18. Сессия 12.05.2026: Flash/config page protection

Блок G из `THERMO_EXECUTOR_REFACTORING_PLAN.md` закрыт по кодовой части защиты Flash page.

- [x] Подтверждено, что `AppConfig_Commit()` и `AppConfig_FactoryReset()` работают со страницей `APP_CONFIG_FLASH_ADDR = 0x0800FC00`.
- [x] Для STM32F103C8 это последняя 1K Flash page: `0x0800FC00..0x0800FFFF`.
- [x] `STM32F103C8TX_FLASH.ld` изменен: application FLASH ограничен до `63K`, чтобы компоновщик не размещал код/rodata на странице конфигурации.
- [x] Такой подход совпадает с экосистемным требованием DDS-240: config page должна быть исключена из области приложения в linker script или зарезервирована отдельной секцией.
- [~] После CubeMX regeneration linker script нужно проверять повторно, потому что CubeIDE может вернуть MCU-default `FLASH LENGTH = 64K` при пересоздании/миграции проекта.

Остается приемка блока G:

- [x] Сборка после изменения linker script подтверждена пользователем.
- [ ] Проверить `F003 + reboot`: mapping сохраняется.
- [ ] Проверить `F006 + reboot`: плата возвращается к default NodeID `0x40` и пустому mapping.
- [ ] Проверить `F104` после reboot: возвращает сохраненный mapping.

---

## 19. Сессия 12.05.2026: Fault/status contract для Thermo

Блок H из `THERMO_EXECUTOR_REFACTORING_PLAN.md` согласован архитектурно и реализован в пределах утвержденного контракта.

### 19.1. Разделение ответственности

- [x] Thermo Executor не формирует Host-level `TEMP_DATA.status = 0..3`.
- [x] Thermo Executor не принимает решений `normal/overheat/underheat`, потому что эти статусы зависят от технологических порогов, recipe context, freshness policy и mapping/cache Дирижера.
- [x] Thermo Executor отвечает только за low-level результат: корректность команды, доступность доменного ресурса, наличие active/mapped канала, успешность 1-Wire read и CRC.
- [x] Дирижер формирует Host-level `TEMP_DATA.status`:
  - `0` normal;
  - `1` overheat;
  - `2` underheat;
  - `3` error.

### 19.2. Executor-level контракт `DONE/NACK`

- [x] После `NACK` Thermo Executor никогда не отправляет `DONE`.
- [x] `GET_TEMP (0x9011)`: если запрошенный active/mapped канал дал валидную свежую температуру, Executor отправляет `ACK -> DATA(temp) -> DONE`.
- [x] `GET_TEMP (0x9011)`: если канал не active/mapped, ROM invalid, датчик не ответил, scratchpad CRC failed или нет валидного значения, Executor отправляет `ACK -> NACK SENSOR_FAILURE`, без `DONE`.
- [x] `GET_ALL_TEMPS (0x9010)`: если есть active/mapped каналы и хотя бы один active/mapped канал дал валидную температуру, Executor отправляет `ACK -> DATA(valid channels...) -> DONE`.
- [x] `GET_ALL_TEMPS (0x9010)`: если active/mapped каналы есть, но ни один не дал валидной температуры, Executor отправляет `ACK -> NACK SENSOR_FAILURE`, без `DONE`.
- [x] `GET_ALL_TEMPS (0x9010)`: если active/mapped каналов нет, Executor отправляет `ACK -> NACK SENSOR_FAILURE`, без `DONE`.
- [x] После `DONE` по `GET_ALL_TEMPS` Дирижер считает отсутствующие active/mapped каналы ошибочными и формирует для них `TEMP_DATA.status = 3`.

### 19.3. Ошибки, не являющиеся `TEMP_DATA.status`

- [x] `CAN_ERR_INVALID_SENSOR_ID`, `CAN_ERR_UNKNOWN_CMD`, `CAN_ERR_BUSY`, service NACK и transport/protocol timeout не превращаются в температурный `status=3` отдельного канала.
- [x] Эти события являются ошибками операции, маршрутизации, сервиса или связи на стороне Дирижера.

### 19.4. Кодовая реализация и сборка

- [x] В `App/src/tasks/task_temp_monitor.c` добавлен helper active/mapped канала: активным считается только канал с валидным DS18B20 ROM из `AppConfig`.
- [x] `GET_TEMP (0x9011)` больше не возвращает старый snapshot для непривязанного/невалидного канала; такой случай завершается `NACK SENSOR_FAILURE`, без `DONE`.
- [x] `GET_ALL_TEMPS (0x9010)` отдает `DATA` только по active/mapped каналам с валидным текущим значением.
- [x] `GET_ALL_TEMPS (0x9010)` завершает `DONE` только если есть хотя бы один active/mapped канал и хотя бы один валидный результат.
- [x] Если active/mapped каналов нет или все active/mapped каналы сейчас fault/stale, `GET_ALL_TEMPS` завершается `NACK SENSOR_FAILURE`, без `DONE`.
- [x] Сборка после изменения подтверждена пользователем как чистая.

---

## 20. Сессия 12.05.2026: Thermo safety baseline и safe-state hook

Блок I.1 из `THERMO_EXECUTOR_REFACTORING_PLAN.md` закрыт по базовому safe-state и сборке. IWDG supervisor и heartbeat остаются отдельными следующими шагами блока I.

### 20.1. Safety baseline

- [x] Thermo Executor не имеет силовых выходов; безопасное состояние платы - отпущенная 1-Wire шина.
- [x] Fault/stale/missing sensor не трактуется как нормальная температура: температурный контракт блока H завершает такие случаи `NACK SENSOR_FAILURE` или отсутствующим каналом для Host `TEMP_DATA.status = 3` на стороне Дирижера.
- [x] Незавершенная команда после `ACK` не считается успешной без `DONE`; Дирижер обязан обработать это как operation timeout/fault recovery.
- [x] После fault/watchdog recovery Дирижер выполняет rediscovery перед возвратом Thermo Executor в сценарий.

### 20.2. Кодовая реализация

- [x] Добавлен `App/inc/app_safety.h` и `App/src/app_safety.c`.
- [x] Добавлен `AppSafety_EnterSafeState()` как доменный safe-state hook Thermo Executor.
- [x] Добавлен `DS18B20_BusRelease()` в драйвер DS18B20: для open-drain PA0 запись HIGH отпускает bus в idle/high-Z состояние.
- [x] Startup safe-state вызывается в `Core/Src/main.c` внутри `USER CODE BEGIN MX_GPIO_Init_2`, после настройки `ONE_WIRE_BUS_Pin` как `GPIO_MODE_OUTPUT_OD`.
- [x] `Error_Handler()` вызывает safe-state до `__disable_irq()`.
- [x] `HardFault_Handler()`, `MemManage_Handler()`, `BusFault_Handler()` и `UsageFault_Handler()` вызывают safe-state из USER CODE блоков fault handlers.
- [x] Safe-state path не использует FreeRTOS, CAN, heap и не отправляет `DONE/NACK`.
- [x] Сборка после I.1 подтверждена пользователем как чистая.

### 20.3. Открытая часть блока I

- [ ] Добавить RTOS resource checks для task handles и mutex.
- [ ] Добавить heartbeat API для CAN transport, Dispatcher и Temp Monitor.
- [ ] Добавить `task_watchdog` как единственного владельца refresh IWDG.
- [ ] Включить и настроить IWDG по STM32F103 reference profile.
- [ ] Проверить idle runtime без ложного reset и fault-injection зависания критической задачи.
