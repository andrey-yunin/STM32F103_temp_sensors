# Fluidics / Conductor Migration Note: Pump Finite Commands

**Дата:** 23.04.2026; актуализировано 05.05.2026
**Статус:** целевая finite-логика `PUMP_RUN_DURATION -> ACK immediately, DONE only after OFF`, отказ `duration_ms=0 -> NACK INVALID_PARAM`, busy-channel `NACK DEVICE_BUSY` и чистая диагностика подтверждены no-load CAN-тестами и post-fix нагрузочными тестами на двух реальных fan-load каналах `ch_idx=0/1`.

## 0. Текущая точка входа 05.05.2026

Короткий baseline для тестирования: `readme/FLUIDICS_CURRENT_TEST_BASELINE_20260505.md`.

Подтверждено в текущем цикле:

- текущая прошивка собирается;
- RTOS baseline после CubeMX regeneration: `configTOTAL_HEAP_SIZE=10240`;
- `F001 GET_DEVICE_INFO` отвечает от Fluidics NodeID `0x30`;
- `PUMP_RUN_DURATION 2000 ms` отвечает `ACK` сразу и `DONE` примерно через 1995 ms;
- `PUMP_RUN_DURATION duration_ms=0` отвечает `ACK`, затем `NACK INVALID_PARAM (0x0006)`, без штатного `DONE`;
- повторный `PUMP_RUN_DURATION` на занятый канал отвечает `ACK`, затем `NACK DEVICE_BUSY (0x0003)`, без штатного `DONE` для второй команды;
- `GET_STATUS` после no-load finite-command цикла вернул `ACK -> 18 DATA -> DONE`; `RX_TOTAL=8`, `TX_TOTAL=18`, fault/drop/overflow counters равны `0`;
- после исправления контакта CAN-разъема прототипный стенд с двумя fan-load нагрузками `ch_idx=0/1` прошел одиночный запуск, совместный запуск, busy-channel и invalid-duration safety;
- финальный post-load/post-safety `GET_STATUS` clean: `RX_TOTAL=12`, `TX_TOTAL=47`, metrics `0x0003..0x0012 = 0`;
- SocketCAN после ремонта разъема оставался `ERROR-ACTIVE`, `error-warn/error-pass/bus-errors/bus-off = 0`;
- команды верхнего уровня `Host -> Conductor` не меняются.

Следующие проверки:

- физический прогон насосов `2..12`;
- физический прогон клапанов `13..15`;
- финальная сверка GPIO-to-load mapping.

## 1. Решение

Единый смысл `DONE` для DDS-240:

- `ACK` - команда принята в обработку;
- `DONE` - команда завершена по своему контракту;
- `NACK`/`ERROR` - команда не выполнена;
- аварийный safe-state, watchdog recovery и защитный fault не являются штатным `DONE`.

Для насосов основным рецептным primitive становится:

```text
PUMP_RUN_DURATION(pump_id, duration_ms)
```

Дирижер рассчитывает `duration_ms` из Host-параметров и калибровки. Fluidics Executor сам включает насос, выдерживает время, выключает насос и только после этого отправляет `DONE`.

`PUMP_START` и `PUMP_STOP` остаются для ручного режима, сервиса, диагностики и аварийного управления, но не должны быть основным способом дозирования в рецептах.

## 2. Послание исполнителю насосов

Требуемое изменение логики Fluidics Executor:

1. Реализовать `0x0201 PUMP_RUN_DURATION`.
2. Payload:
   - bytes `0..1`: `cmd_code = 0x0201` Little-Endian;
   - byte `2`: `pump_id` / `ch_idx`;
   - bytes `3..6`: `duration_ms:uint32` Little-Endian;
   - byte `7`: `0x00`.
3. `duration_ms=0` отклонять `NACK INVALID_PARAM` или доменным эквивалентом.
4. При получении команды:
   - проверить индекс насоса;
   - проверить занятость канала;
   - включить насос только после успешного старта локального one-shot timer;
   - пометить канал `BUSY/RUNNING`;
   - по истечении timer выключить насос;
   - снять `BUSY/RUNNING`;
   - отправить `DONE` по `0x0201`.
5. Если timer не создан/не стартовал, насос оставить `OFF`, вернуть `NACK`, `DONE` не отправлять.
6. При fault handler, watchdog recovery или safe-state выключить все насосы/клапаны. `DONE` из fault path не отправлять.
7. `PUMP_START/PUMP_STOP` сохранить как state/service-команды:
   - `PUMP_START DONE` = насос включен, safety timeout активирован;
   - `PUMP_STOP DONE` = насос выключен.

Минимальная приемка Fluidics v2:

- `PUMP_RUN_DURATION 2000 ms`: `ACK`, насос ON около 2000 ms, насос OFF, затем `DONE`;
- `PUMP_RUN_DURATION duration=0`: `ACK + NACK`, насос остается OFF;
- повторная команда на занятый насос: `ACK + NACK DEVICE_BUSY`;
- fault-injection после ON: насос уходит OFF, `DONE` не отправляется, Дирижер видит operation timeout/recovery.

## 3. Послание Дирижеру

Требуемое изменение логики Conductor:

Короткий посыл: **Дирижер не дозирует насос через `START -> WAIT -> STOP`; Дирижер рассчитывает `duration_ms`, отправляет один `PUMP_RUN_DURATION`, держит ресурс канала занятым и ждет executor `DONE` только после физического выключения насоса.**

Базовое требование: Host-команды верхнего уровня не менять. Дирижер должен парсить их по `Commands_API/User_Commands`, а не подгонять Host payload под executor payload. Для `WASH_STATION_FILL` порядок параметров остается:

```text
WASH_STATION_FILL(volume:uint16, cuvette:uint16)
```

1. Для Host-команд с объемом жидкости рассчитывать время работы насоса через калибратор:

```text
duration_ms = Fluidics_CalcPumpDurationMs(pump_id, volume_ul, operation_type)
```

2. Использовать полный pipeline перевода:

```text
Host parser -> recipe selector -> parameter translator/calibrator -> device mapping -> low-level packer -> job manager
```

3. В рецептах заменить дозирование через:

```text
PUMP_START -> WAIT_MS -> PUMP_STOP
```

на одну атомарную команду:

```text
PUMP_RUN_DURATION(pump_id, duration_ms)
```

4. `WAIT_MS` оставить только для технологических пауз, стабилизации, выдержки реакции и других пауз, которые не являются локально управляемой физикой исполнительного устройства.
5. `FAST_DONE`/короткое окно 100 ms нельзя применять к `PUMP_RUN_DURATION`. Operation timeout Дирижера для `PUMP_RUN_DURATION` должен быть больше `duration_ms` на технологический запас:

```text
operation_timeout_ms = duration_ms + transport_margin_ms + executor_margin_ms
```

6. Host `DONE` отправлять только после завершения всего рецепта. Executor `DONE` по `PUMP_RUN_DURATION` продвигает только один атомарный шаг рецепта.
7. Если `ACK` получен, но `DONE` не пришел в operation timeout, рецепт не продолжается как успешный. Дирижер выполняет recovery/discovery и сверку состояния узла.
8. `PUMP_START/PUMP_STOP` оставить доступными для manual/service flows, но не использовать в основных recipe-командах дозирования.
9. Если калибратор не может выдать валидный `duration_ms`, Дирижер завершает Host/job ошибкой и не отправляет команду на Fluidics.
10. `duration_ms=0` должен отбрасываться еще на стороне Дирижера как invalid recipe/calibration result. Если такая команда все же отправлена, Fluidics отвечает `ACK + NACK INVALID_PARAM (0x0006)`, не включает нагрузку и не отправляет `DONE`.
11. Пока канал находится в активном `PUMP_RUN_DURATION`, Дирижер должен держать channel resource lock. Повторная команда на тот же канал является конфликтом и ожидаемо получает `ACK + NACK DEVICE_BUSY (0x0003)`, без второго `DONE`.
12. Параллельные `PUMP_RUN_DURATION` на разных каналах допустимы, если recipe и аппаратная карта разрешают такую гидравлику. На прототипе `ch_idx=0 + ch_idx=1` подтверждены как независимые concurrent finite actions.
13. После серии Fluidics-команд Дирижеру полезно периодически опрашивать `GET_STATUS`; рост metrics `0x0003..0x0012` или SocketCAN/CAN fault counters является degraded condition, даже если отдельная команда получила `DONE`.

Минимальная приемка Conductor v2:

- Host `WASH_STATION_FILL(volume_ul, cuvette)` формирует `PUMP_RUN_DURATION` с рассчитанным `duration_ms`;
- Host `WASH_STATION_FILL` парсит `volume` первым, `cuvette` вторым, как указано в Host API;
- Host `WASH_STATION_WASH` не использует `START_PUMP + WAIT_MS + STOP_PUMP` для дозирующих шагов;
- `DONE` от Fluidics по `PUMP_RUN_DURATION` приходит после выключения насоса и только тогда продвигает рецепт;
- повторная recipe-команда на тот же насос до `DONE` не отправляется планировщиком; если конфликт пришел извне, Дирижер трактует `NACK DEVICE_BUSY` как невыполненный шаг, а не как успешное завершение;
- `duration_ms=0` не отправляется на исполнитель; если получен `NACK INVALID_PARAM`, recipe/job завершается ошибкой параметров;
- operation timeout для finite pump шага рассчитывается от `duration_ms`, а не от fixed fast-done timeout;
- Host `DONE` приходит только после завершения всех atomic-шагов recipe.

## 4. Открытые проектные решения

До реализации нужно зафиксировать:

- таблицу калибровки `pump_id -> ul_per_ms` или `ms_per_ul`;
- минимальное и максимальное допустимое `duration_ms`;
- округление объема в миллисекунды;
- отдельные коэффициенты для забора, подачи и слива, если гидравлика отличается;
- поведение при неизвестной калибровке: `NACK`/Host error, а не запуск по default.
