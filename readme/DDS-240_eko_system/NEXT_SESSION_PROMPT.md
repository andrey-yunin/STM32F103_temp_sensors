# Next Session Prompt: Thermo Executor Refactoring

Дата контрольной точки: 08.05.2026

Рабочая директория Thermo Executor:

```text
/home/andrey/STM32CubeIDE/workspace_1.19.0/STM32F103_temp_sensors
```

## Роль и правила работы

- Работать в режиме учителя-консультанта.
- Не предлагать временные решения, заглушки и обходные варианты.
- Не менять общую экосистему DDS-240 под одну плату.
- `DLC=8` на CAN шине Conductor <-> Executor не менять.
- Если нужны правки в Cube-generated файлах, ручной код размещать в USER CODE блоках. IRQ containers могут быть сгенерированы Cube.
- Каждый логически законченный кодовый блок сопровождается пояснением, комментариями в коде и сборкой перед коммитом.

## Читать перед стартом

- `readme/THERMO_EXECUTOR_REFACTORING_PLAN.md`
- `readme/TEMP_SENSORS_EXECUTOR_REPORT.md`
- `readme/DDS-240_eko_system/DDS-240_ECOSYSTEM_STANDARD.md`
- `readme/DDS-240_eko_system/CONDUCTOR_INTEGRATION_GUIDE.md`
- `readme/DDS-240_eko_system/dds240_global_config.h`
- Motion/Fluidics эталоны:
  - `/home/andrey/STM32CubeIDE/workspace_1.19.0/STM32F103_step_motors_refactored`
  - `/home/andrey/STM32CubeIDE/workspace_1.19.0/STM32F103_pumps_valves`

Старая папка `readme/Commands_API` в Thermo project была удалена намеренно. Не восстанавливать ее.

## Текущий статус

Кодовая часть большого рефакторинга Thermo Executor на 08.05.2026 собирается без ошибок и предупреждений.

Проверочная команда:

```bash
PATH=/home/andrey/st/stm32cubeide_1.19.0/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.linux64_1.0.0.202410170706/tools/bin:$PATH make -B -j14 all
```

Последний результат:

```text
Finished building target: STM32F103_temp_sensors.elf
text=36456, data=100, bss=10072, dec=46628, hex=b624
```

## Выполнено

- CAN transport переведен на event-driven RX/TX queues.
- Добавлен central TX helper для ACK/NACK/DATA/DONE.
- Реализован strict transport filtering: Extended ID, dst, msg type, `DLC=8`.
- Transport-invalid frames silently dropped и учитываются counters.
- Реализован `F007 GET_STATUS` с diagnostics metrics `0x0001..0x0012`.
- CAN SCE IRQ подключен к `HAL_CAN_ErrorCallback()` и diagnostics counters.
- Dispatcher не выполняет прямые DS18B20 операции; Thermo commands идут в `thermo_queue`.
- Реализованы/проверены `F004 GET_UID`, `F007 GET_STATUS`, `F104 GET_CHANNEL_MAP`.
- `AppConfig_t` исправлен: 8-bit `performer_id`, checksum через `offsetof(AppConfig_t, checksum)`.
- Добавлен `DS18B20_IsValidROM()` и `const`-correct `OneWire_CRC8()`.
- `F103/F105` защищены pending-состоянием от смешивания половин ROM разных каналов.
- Runtime mapping poll использует проверку family code `0x28` + CRC8.

## Оставшиеся блоки

### 1. CAN hardware profile

- Сверить CAN timing Thermo с Motion/Fluidics.
- Проверить APB1 clock и расчет 1 Mbit/s.
- Привести `TransmitFifoPriority` к утвержденному стандарту, если он подтвержден образцами.
- После правки собрать проект.

### 2. TIM3 / 1-Wire runtime

- Подтвердить, стартует ли TIM3 до первого `delay_us()`.
- Если старта нет, добавить `HAL_TIM_Base_Start(&htim3)` в корректный USER CODE блок.
- Проверить `F101/F102` на реальной 1-Wire шине с несколькими DS18B20.

### 3. Flash/config protection

- Зарезервировать последнюю Flash page в linker script.
- Проверить, что firmware image не занимает `0x0800FC00`.
- Проверить `F003 + reboot` и `F006 + reboot`.

### 4. Fault/status contract

- Согласовать, где формируется Host-level `TEMP_DATA.status`.
- Определить соответствие stale/CRC/missing sensor/1-Wire timeout к Host/API.
- Не вводить неутвержденные статусы в Thermo executor.

### 5. Safety / Watchdog / safe-state

- Зафиксировать Thermo safety baseline: 1-Wire bus safe idle, fault/stale не выдается как normal temperature, `ACK without DONE` не считается успехом.
- Реализовать Thermo safe-state hook: release 1-Wire bus, прервать доменную операцию, не отправлять `DONE` из fault path.
- Добавить heartbeat CAN/Dispatcher/Temp Monitor.
- Добавить `task_watchdog`: единственное место refresh IWDG.
- Использовать reference profile STM32F103: Prescaler `256`, Reload `624`, если не будет аппаратной причины отклониться.
- Проверить idle без ложного reset и fault-injection зависания критической задачи.

### 6. Лабораторная приемка

- CANable: 1 Mbit/s, Extended ID, strict `DLC=8`.
- Проверить `F001`, `F004`, baseline/final `F007`.
- Проверить negative transport tests.
- Проверить `F101/F102/F103/F105/F003/F002/F104`.
- Проверить `0x9010/0x9011`.
- Проверить fault behavior при отключении датчика.

## Следующая задача

Начать с блока CAN hardware profile. Сначала прочитать текущие настройки Thermo в `Core/Src/main.c` и `.ioc`, затем сравнить с Motion/Fluidics. Код править только после точного понимания, какие параметры являются общим стандартом DDS-240 для STM32F103.
