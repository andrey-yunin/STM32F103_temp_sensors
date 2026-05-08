# Thermo Executor Refactoring Plan

Дата обновления: 08.05.2026
Проект: `STM32F103_temp_sensors`

Цель: довести Thermo Executor до промышленного стандарта DDS-240 без изменения общих правил экосистемы под специфику одной платы. Все изменения выполняются блоками: пояснение -> код -> сборка -> фиксация результата.

Статусы:

- `[ ]` не выполнено;
- `[x]` выполнено и подтверждено кодом/сборкой;
- `[~]` частично выполнено, требуется стендовая проверка или уточнение контракта.

## 1. Правила работ

- [x] `DLC=8` на CAN шине Conductor <-> Executor не меняется.
- [x] Переменный DLC допустим только на уровне Host <-> Conductor.
- [x] Motion/Fluidics используются как эталон общего исполнительного каркаса, но Thermo-domain логика DS18B20 остается специфичной.
- [x] Глобальный config дополняется Thermo-командами и Thermo-specific aliases, а не используется как повод ломать рабочую доменную модель.
- [x] Все ручные правки в Cube-generated файлах размещаются в USER CODE блоках, кроме IRQ containers, созданных Cube.
- [x] Каждый логически законченный блок должен собираться без ошибок и предупреждений перед коммитом.

## 2. Выполнено в текущем большом рефакторинге

### 2.1. Аудит и контракт DDS-240

- [x] Проверены общие документы экосистемы DDS-240.
- [x] Сверены Motion Executor, Fluidics Executor и Thermo Executor.
- [x] Зафиксировано разделение NodeID и DeviceType: Thermo NodeID `0x40`, Thermo DeviceType `0x02`.
- [x] Принята scoped-модель NACK: одинаковые числовые error codes могут иметь разные доменные aliases на разных исполнителях.
- [x] `CAN_ERR_SENSOR_FAILURE = 0x0003` оставлен как Thermo-specific alias, без назначения нового числа.

### 2.2. Protocol constants

- [x] Добавлены/синхронизированы `F001..F007`.
- [x] Добавлены Thermo service commands `F101..F105`.
- [x] Добавлены `CAN_STATUS_*` metrics `0x0001..0x0012`.
- [x] `F001 GET_DEVICE_INFO` возвращает DeviceType Thermo `0x02`.
- [x] Локальный `can_protocol.h` согласован с фактическим глобальным config по Thermo.

### 2.3. CAN transport diagnostics

- [x] Добавлен `CanDiagnostics_t`.
- [x] Добавлен snapshot API для `F007`.
- [x] Добавлен central TX helper для ACK/NACK/DATA/DONE.
- [x] Считаются `rx_total` и `tx_total`.
- [x] Считаются RX/TX/dispatcher/app queue overflows.
- [x] Считаются silent drops: Standard ID, wrong dst, wrong type, wrong DLC.
- [x] Wrong DLC не получает NACK; transport-invalid frame silently dropped + counter.
- [x] Считаются CAN error callback, warning, passive, bus-off, last HAL error, last ESR.
- [x] `HAL_CAN_ErrorCallback()` подключен в `Core/Src/stm32f1xx_it.c`.
- [x] RX callback учитывает overflow очереди и не выполняет parsing в IRQ.

Приемка:

- [x] `F007` выдает metrics `0x0001..0x0012`.
- [x] Проект собирается без warnings после полного rebuild.
- [ ] Рост counters на реальной CAN шине должен быть подтвержден CANable тестами.

### 2.4. Service layer

- [x] Реализован `F004 GET_UID`.
- [x] Реализован `F007 GET_STATUS`.
- [x] Реализован `F104 GET_CHANNEL_MAP`.
- [x] `F103/F105` используются как двухфазная запись ROM ID при строгом `DLC=8`.
- [x] `F001` и `F004` согласованы по UID fragments.

Приемка:

- [x] `F004 -> ACK -> DATA -> DATA -> DONE`.
- [x] `F007 -> ACK -> DATA metrics -> DONE`.
- [x] `F104` возвращает ROM ID канала двумя DATA frames.
- [ ] Последовательности должны быть подтверждены CANable trace.

### 2.5. Flash/config

- [x] `AppConfig_t` приведен к фиксированному layout.
- [x] `performer_id` хранится как `uint8_t`, потому что CAN NodeID 8-битный.
- [x] CRC16 считается через `offsetof(AppConfig_t, checksum)`, строго до поля `checksum`.
- [x] `AppConfig_SetPerformerID()` сохраняет младший байт NodeID.
- [ ] Последняя Flash page пока не зарезервирована в linker script.
- [ ] Persistence через `F003 + reboot` и factory reset через `F006 + reboot` требуют лабораторной проверки.

### 2.6. DS18B20 / 1-Wire runtime integrity

- [x] Search ROM и Match ROM уже реализованы и сохранены.
- [x] Добавлен публичный `DS18B20_IsValidROM()`.
- [x] `OneWire_CRC8()` приведена к `const`-correct сигнатуре.
- [x] Runtime mapping poll использует валидатор ROM: family code `0x28` + CRC8.
- [x] Записи temperature snapshot в рабочем цикле выполняются через `TempMonitor_SetTemperature()`.
- [x] `F103/F105` защищены pending-состоянием от смешивания половин ROM разных каналов.
- [x] `F105` принимает только валидный DS18B20 ROM или пустой ROM `FF FF FF FF FF FF FF FF`.
- [ ] Запуск TIM3 для `delay_us()` в runtime должен быть подтвержден и при необходимости добавлен отдельным блоком.
- [ ] Требуется согласованная модель состояния канала вместо одного sentinel `-999.0f`.

## 3. Финальная проверка 08.05.2026

Команда:

```bash
PATH=/home/andrey/st/stm32cubeide_1.19.0/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.linux64_1.0.0.202410170706/tools/bin:$PATH make -B -j14 all
```

Результат:

- [x] Rebuild выполнен.
- [x] Ошибок компиляции нет.
- [x] Compiler warnings в финальном выводе нет.
- [x] ELF сформирован: `STM32F103_temp_sensors.elf`.
- [x] Size: `text=36456`, `data=100`, `bss=10072`, `dec=46628`, `hex=b624`.

## 4. Оставшиеся кодовые блоки

### Блок E: CAN hardware profile

- [ ] Привести CAN bit timing к эталону STM32F103 экосистемы.
- [ ] Проверить APB1 clock и расчет 1 Mbit/s.
- [ ] Включить `TransmitFifoPriority = ENABLE`, если это подтвержденный общий стандарт для порядка `ACK -> DATA... -> DONE`.
- [ ] Проверить, что изменение сделано через Cube или сохранено в корректных USER/Cube-controlled местах.
- [ ] Сборка проекта.

Приемка:

- [ ] CANable видит стабильный обмен 1 Mbit/s.
- [ ] Многокадровые ответы не переупорядочиваются.

### Блок F: TIM3 / 1-Wire runtime

- [ ] Подтвердить, стартует ли TIM3 до первого вызова `delay_us()`.
- [ ] Если старта нет, добавить `HAL_TIM_Base_Start(&htim3)` в корректный USER CODE блок после init периферии.
- [ ] Проверить `F101` scan на реальной 1-Wire шине с несколькими DS18B20.
- [ ] Проверить `F102` для каждого найденного ROM.
- [ ] Сборка проекта.

Приемка:

- [ ] `F101` находит несколько датчиков на одной шине.
- [ ] `F102` возвращает ROM ID каждого найденного датчика.
- [ ] CRC-invalid ROM не попадает в mapping.

### Блок G: Flash/config protection

- [ ] Зарезервировать последнюю Flash page в linker script.
- [ ] Проверить, что firmware image не занимает `APP_CONFIG_FLASH_ADDR = 0x0800FC00`.
- [ ] Проверить `F003 + reboot`: mapping сохраняется.
- [ ] Проверить `F006 + reboot`: плата возвращается к default NodeID `0x40` и пустому mapping.
- [ ] Сборка проекта.

Приемка:

- [ ] `F104` после reboot возвращает сохраненный mapping.
- [ ] После factory reset mapping пустой, NodeID default.

### Блок H: Fault/status contract

- [ ] Согласовать, где формируется Host-level `TEMP_DATA.status`: Executor, Conductor cache/mapping или Host aggregation.
- [ ] Описать соответствие Thermo NACK/канального состояния к Host statuses.
- [ ] Определить поведение при stale value, CRC error, missing ROM, 1-Wire timeout.
- [ ] После согласования обновить код только в пределах утвержденного контракта.

Приемка:

- [ ] Host получает однозначный status для normal/overheat/underheat/error или явную ошибку `0x80xx`.
- [ ] Thermo executor не отправляет неутвержденные статусы.

### Блок I: Safety / Watchdog / safe-state / fault path

- [ ] Зафиксировать Thermo safety baseline:
  - [ ] исполнитель не имеет силовых выходов, но обязан безопасно отпустить 1-Wire bus;
  - [ ] fault/stale/missing sensor не должен трактоваться как нормальная температура;
  - [ ] незавершенная команда после `ACK` не считается успешной без `DONE`;
  - [ ] после watchdog/fault recovery Дирижер выполняет rediscovery перед продолжением сценария.
- [ ] Определить Thermo safe-state hook:
  - [ ] 1-Wire bus released to idle/high-Z open-drain state;
  - [ ] активная DS18B20 транзакция считается прерванной;
  - [ ] штатный `DONE` из fault/watchdog path не отправляется.
- [ ] Вызывать Thermo safe-state hook при старте, `Error_Handler()` и fault handlers.
- [ ] Добавить RTOS resource checks для queues/tasks/mutex.
- [ ] Добавить heartbeat API для критических задач: CAN transport, Dispatcher, Temp Monitor.
- [ ] Добавить `task_watchdog` как единственного владельца `HAL_IWDG_Refresh()`.
- [ ] Настроить IWDG по общему STM32F103 reference profile: Prescaler `256`, Reload `624`, если не будет аппаратной причины отклониться.
- [ ] Если heartbeat критической задачи не продвинулся, supervisor вызывает safe-state hook и прекращает refresh IWDG.
- [ ] Сборка проекта.

Приемка:

- [ ] Safety baseline описан в отчете и не противоречит DDS-240 ecosystem standard.
- [ ] Idle-тест без CAN-команд не вызывает ложный watchdog reset.
- [ ] Fault-injection зависания критической задачи приводит к safe-state и аппаратному reset.
- [ ] После reset Conductor выполняет rediscovery через `F001`.
- [ ] Сценарий `ACK` без `DONE` трактуется как fault/recovery, а не как успешная команда.

## 5. Лабораторная приемка с CANable и несколькими DS18B20

- [ ] Подготовить CANable: 1 Mbit/s, Extended ID, strict `DLC=8`.
- [ ] Проверить discovery: `F001`.
- [ ] Проверить UID: `F004`.
- [ ] Снять baseline: `F007`.
- [ ] Проверить negative transport tests: short DLC, Standard ID, wrong dst, wrong type.
- [ ] Проверить `F101` scan на нескольких DS18B20.
- [ ] Проверить `F102` для всех найденных ROM.
- [ ] Привязать минимум два датчика через `F103/F105`.
- [ ] Сохранить mapping через `F003`.
- [ ] Перезагрузить через `F002`.
- [ ] Проверить persistence mapping через `F104`.
- [ ] Проверить `0x9010 SENSOR_GET_ALL_TEMPS`.
- [ ] Проверить `0x9011 SENSOR_GET_TEMP` для каждого канала.
- [ ] Проверить invalid sensor id.
- [ ] Проверить отключение одного датчика и согласованный fault/status behavior.
- [ ] Снять финальный `F007`.
- [ ] Обновить отчет исполнителя по результатам лабораторной приемки.

## 6. Следующий шаг

Следующая сессия начинается с блока E: CAN hardware profile. Перед правкой нужно еще раз сверить эталонные параметры CAN в Motion/Fluidics и текущие настройки Cube для Thermo.
