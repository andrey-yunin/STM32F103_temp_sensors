# Conductor Integration Guide: DDS-240 Executors
**Экосистема:** DDS-240
**Версия спецификации:** 1.1 (Апрель 2026)
**Статус:** Обязателен для реализации на стороне Дирижера (Conductor).

Данный документ описывает интерфейс взаимодействия Дирижера с платами-исполнителями DDS-240:
Fluidics, Thermo, Motion и будущими Executor-платами на общем CAN-транспорте.

## 0. Граница уровней: Host API и Executor CAN

В DDS-240 есть два разных контракта, и их нельзя смешивать.

**Host -> Conductor** описан в `Commands_API/User_Commands`.

- Это внешний пользовательский API.
- Порядок, типы и смысл Host-параметров не меняются ради удобства исполнителей.
- Если Host-команда задает `volume`, `cuvette`, `cycles` или другой технологический параметр, Дирижер сначала парсит его по Host API.
- Динамический DLC на этом уровне допустим, если он задан Host API.

**Conductor -> Executor** описан этим guide, `DDS-240_ECOSYSTEM_STANDARD.md` и `dds240_global_config.h`.

- Это низкоуровневый CAN-контракт между Дирижером и STM32 Executor-платами.
- Здесь действует Директива 2.0: 29-bit Extended ID, strict `DLC=8`, Little-Endian payload, `ACK/NACK/DATA/DONE`.
- Executor payload не является Host payload.
- Дирижер обязан выполнить перевод: `Host command -> recipe -> translator/calibrator -> device mapping -> executor packer`.

Практический пример для Fluidics: `WASH_STATION_FILL(volume, cuvette)` не меняется на Host-уровне. Дирижер парсит `volume` и `cuvette`, рассчитывает `duration_ms` через калибровку и отправляет Fluidics уже executor-команду `PUMP_RUN_DURATION(ch_idx, duration_ms)` с `DLC=8`.

Для интеграции Дирижера читать в таком порядке:

1. `Commands_API/User_Commands` - внешний Host API.
2. `DDS-240_ECOSYSTEM_STANDARD.md` - общие правила экосистемы.
3. `dds240_global_config.h` - канонические константы NodeID, command IDs, NACK и `GET_STATUS`.
4. Этот `CONDUCTOR_INTEGRATION_GUIDE.md` - упаковка и поведение `Conductor <-> Executor`.
5. `FLUIDICS_PUMP_RUN_DURATION_MIGRATION.md` - специальный посыл по finite-дозированию насосами.

## 0.1. Быстрый вход для Дирижера

Минимальный маршрут интеграции без смешивания уровней:

1. Принять Host-команду строго по `Commands_API/User_Commands`.
2. Не менять Host payload, порядок параметров и смысл команды.
3. Выбрать recipe/action на стороне Дирижера.
4. Перевести Host-параметры в физические параметры исполнителя через translator/calibrator.
5. Выбрать плату, NodeID и физический `ch_idx` через device mapping.
6. Упаковать executor-команду по Директиве 2.0: Extended ID, strict `DLC=8`, Little-Endian.
7. Отправить executor-команду и ждать `ACK` в ACK timeout.
8. После `ACK` ждать `DONE` в operation timeout, рассчитанный под конкретную atomic-команду.
9. При `NACK` считать atomic step невыполненным и применять policy ошибки recipe/job.
10. Host `DONE` отправлять только после успешного завершения всего recipe/job, а не после отдельного executor `DONE`.

Критично для Fluidics:

- Host-команды моющей станции остаются Host-командами.
- Дозирование насосом в recipe выполнять через `PUMP_RUN_DURATION(ch_idx, duration_ms)`.
- `PUMP_START -> WAIT_MS -> PUMP_STOP` не использовать как основной способ дозирования.
- `FAST_DONE` timeout не применять к `PUMP_RUN_DURATION`; timeout считать как `duration_ms + margin`.
- `duration_ms=0` не отправлять на Fluidics; это ошибка recipe/calibration.
- Пока канал выполняет `PUMP_RUN_DURATION`, Дирижер держит resource lock.
- `NACK DEVICE_BUSY` и `NACK INVALID_PARAM` не являются успешным завершением шага.

# 1. Интеграция исполнителя насосов и клапанов (Fluidics) — УНИФИКАЦИЯ 2.0
**NodeID Исполнителя**: `0x30`.

## 1.1. Сетевые параметры (Transport Layer)
- **Протокол**: bxCAN (STM32), 1 Mbit/s.
- **Формат ID**: 29-bit Extended.
- **NodeID Исполнителя**: `0x30`.
- **Broadcast**: `0x00`.
- **Bit Timing STM32F103**: APB1=32 МГц, Prescaler=2, BS1=11TQ, BS2=4TQ, SJW=1TQ, sample point 75%.
- **Пины платы Fluidics**: PA11 = CAN_RX, PA12 = CAN_TX.
- **Граница уровня**: данный раздел описывает только `Conductor <-> Executor`. Документация `Host -> Conductor` / `User_Commands` сохраняет динамический DLC.

**Проверенные ID для пары Conductor `0x10` <-> Fluidics `0x30`:**

| Направление | Тип | CAN ID |
|:------------|:----|:-------|
| Conductor -> Fluidics | COMMAND | `00301000` |
| Fluidics -> Conductor | ACK | `05103000` |
| Fluidics -> Conductor | NACK | `06103000` |
| Fluidics -> Conductor | DATA / DONE / LOG | `07103000` |

## 1.2. Структура командного фрейма (Payload) — Директива 2.0
Дирижер упаковывает данные в формате **Little-Endian**. **Строгий DLC=8 для всех команд.**

| Байты | Поле | Тип | Описание |
|:------|:-----|:----|:---------|
| 0-1   | `cmd_code` | uint16 | Код команды (напр. 0x0202). |
| 2     | `ch_idx`   | uint8  | **0-based индекс** канала на плате (0-15). |
| 3-6   | `parameter`| mixed  | Timeout (uint32) или 0x00000000. |
| 7     | `reserved` | uint8  | Всегда `0x00`. |

### 1.2.1. Форматы ответов Fluidics

| Ответ | Payload DLC=8 | Описание |
|:------|:--------------|:---------|
| ACK | `cmd_lo cmd_hi 00 00 00 00 00 00` | Команда принята диспетчером. |
| NACK | `cmd_lo cmd_hi err_lo err_hi 00 00 00 00` | Ошибка выполнения или валидации. |
| DONE | `01 cmd_lo cmd_hi ch_idx 00 00 00 00` | Атомарное действие команды штатно выполнено. |
| DATA | `02 80 data0 data1 data2 data3 data4 data5` | Однокадровый DATA или фрагмент сервисного ответа. |

## 1.3. Реестр логических устройств (Fluidics Device Mapping)
Исполнитель оперирует физическими индексами каналов.

### 1.3.1. Насосы (Pumps 0-12)
- **Индексы 0-12**: Соответствуют физическим насосам платы.
- **Команды**: `START (0x0202)`, `STOP (0x0203)`, `RUN_DURATION (0x0201)` для запуска на заданное время.

### 1.3.2. Клапаны (Valves 13-15)
- **Индексы 13-15**: Соответствуют физическим клапанам платы.
- **Команды**: `OPEN (0x0204)`, `CLOSE (0x0205)`.

## 1.4. Спецификация параметров команд

| Код | Команда | Параметры (байты 3-6) | Описание |
|:----|:--------|:----------------------|:---------|
| `0x0201` | **PUMP_RUN_DURATION**| `duration_ms` (uint32, LE) | Основная рецептная команда насоса: включить насос на заданное время, затем выключить. `duration_ms=0` невалиден. |
| `0x0202` | **PUMP_START**  | `timeout` (uint32, LE) | Сервисная/ручная команда: включить насос до `PUMP_STOP` или safety timeout. 0 = Default (60с). |
| `0x0203` | **PUMP_STOP**   | - | Выкл. Насос. |
| `0x0204` | **VALVE_OPEN**  | `timeout` (uint32, LE) | Открыть клапан. 0 = Default (300с). |
| `0x0205` | **VALVE_CLOSE** | - | Закрыть клапан. |

**Команды для стенда SocketCAN:**

| Операция | Команда |
|:---------|:--------|
| Get Info | `cansend can0 00301000#01F0000000000000` |
| Pump 0 Run 2000 ms | `cansend can0 00301000#010200D007000000` |
| Pump 0 Start | `cansend can0 00301000#0202000000000000` |
| Pump 0 Stop | `cansend can0 00301000#0302000000000000` |
| Valve 13 Open | `cansend can0 00301000#04020D0000000000` |
| Valve 13 Close | `cansend can0 00301000#05020D0000000000` |

## 1.5. Жизненный цикл команды (Transaction)
Дирижер обязан отслеживать состояние транзакции по следующей схеме:

1. **SEND**: Дирижер отправляет команду (DLC=8).
2. **ACK (0x01)**: Дирижер должен получить подтверждение приема в течение **50 мс**.
3. **DONE (0x03, Sub:0x01)**: Дирижер ожидает сигнал завершения.
   - Для `PUMP_RUN_DURATION` — после истечения `duration_ms`, выключения насоса и завершения атомарной операции.
   - Для `PUMP_START/PUMP_STOP/VALVE_OPEN/VALVE_CLOSE` — после достижения требуемого состояния и запуска/остановки локальной защиты.

`DONE` подтверждает завершение команды по ее контракту. Для `PUMP_RUN_DURATION` это означает, что насос уже выключен после штатной отработки времени. Для state-команд `START/STOP/OPEN/CLOSE` это означает, что ресурс переведен в запрошенное состояние.

Если `ACK` получен, но `DONE` не пришел в operation timeout, Дирижер обязан считать атомарную команду невыполненной или неизвестно завершенной. Для Fluidics это может означать внутренний отказ доменной задачи или IWDG recovery: исполнитель мог успеть перевести выход в safe state и затем уйти в reset без `NACK/DONE`. После такого события Дирижер выполняет recovery/discovery и не продолжает recipe step как успешно завершенный.

### 1.5.1. Transport timeout policy

Плата Fluidics не отправляет `NACK` на невалидную транспортную оболочку. Кадры с `DLC != 8`, не Extended ID, чужим `DstAddr` или не `COMMAND` типом игнорируются без ответа. Дирижер обязан обрабатывать это как отсутствие `ACK` в окне ожидания и применять свою retry/fault policy.

`NACK` используется только для транспортно валидных `COMMAND`-кадров с `DLC=8`, когда ошибка относится к прикладному уровню: неизвестная команда, неверный индекс, неверный тип ресурса, неверный Magic Key.

NACK-код интерпретируется не отдельно, а в контексте транзакции: `src NodeID + cmd_code + error_code`. Поэтому одинаковое числовое значение может иметь разные доменные alias-имена на разных исполнителях, если это зафиксировано в `dds240_global_config.h` и в разделе конкретного исполнителя. Например, `0x0003` у Motion является `MOTOR_BUSY`, у Fluidics - `DEVICE_BUSY`, а у Thermo - `SENSOR_FAILURE`.

### 1.5.2. Промышленное послание Дирижеру

До закрытия промышленного блока Fluidics Дирижер должен учитывать следующие ограничения и обязанности:

- **Non-zero timeout**: `PUMP_START` с ненулевым timeout проверен на физической нагрузке; timeout кодируется как `uint32_t` Little-Endian в payload bytes 3-6 (`parsed.data[0..3]`).
- **RUN_DURATION**: `0x0201 PUMP_RUN_DURATION` должен стать основной рецептной командой насоса. `DONE` должен отправляться только после штатного выключения насоса по истечении `duration_ms`.
- **Zero timeout policy**: для `START/OPEN` timeout `0` означает default safety timeout исполнителя; для `RUN_DURATION` timeout `0` невалиден.
- **Safety timer failure policy**: если исполнитель не смог создать или запустить safety timer, нагрузка должна остаться/вернуться в `OFF`, а транзакция завершиться `NACK`, без `DONE`.
- **Operation supervision**: Дирижер обязан иметь собственный верхний timeout операции. Safety timeout исполнителя является последним уровнем защиты железа, но не заменяет контроль сценария на стороне Дирижера.
- **Volume-to-duration translation**: если Host-команда задает объем жидкости, Дирижер рассчитывает `duration_ms` по калибровке насоса и передает Executor команду `PUMP_RUN_DURATION`. Executor сам включает насос, выдерживает время, выключает насос и отправляет `DONE`.
- **Recipe timing ownership**: Дирижер ведет таймеры рецепта только для технологических пауз и operation timeout. Он не должен использовать `WAIT_MS` как основной механизм дозирования насосом, если операция может быть передана Executor как `PUMP_RUN_DURATION`.
- **Resource lock**: после успешного `START/OPEN` Дирижер помечает канал как `ACTIVE/BUSY` и не отправляет на него конфликтующие команды до штатного `STOP/CLOSE`, аварийного stop или recovery-процедуры.
- **No ACK**: отсутствие `ACK` в окне ожидания трактуется как transport/protocol timeout. Это не `NACK`; Дирижер применяет retry/fault policy.
- **Multi-frame ordering**: для `GET_DEVICE_INFO`, `GET_STATUS` и любых будущих DATA-ответов Дирижер считает `DONE` концом ответа. Исполнитель обязан передать все DATA до DONE; после DONE DATA той же транзакции быть не должно. Для STM32 bxCAN требуется `TransmitFifoPriority = ENABLE`.
- **RTOS resource integrity**: исполнитель обязан проверять создание задач, очередей и таймеров. Частично запущенная прошивка, которая отвечает `ACK`, но не обслуживает доменную задачу и не отправляет `DONE`, недопустима.
- **Fluidics heap baseline**: для текущей Fluidics WIP перед тестами 05.05.2026 используется `configTOTAL_HEAP_SIZE = 10240`. Историческое значение `8192` восстановило работу после отказа при `4096`, но после IWDG/watchdog-task и finite `PUMP_RUN_DURATION` baseline пересчитан и зафиксирован заново.
- **IWDG recovery**: при зависании критической задачи исполнитель может сначала отправить `ACK`, затем не отправить `DONE`, перевести выходы в safe state и уйти в аппаратный reset без `NACK/DONE`. Дирижер обязан обработать это как operation timeout, считать команду невыполненной/неподтвержденной, выполнить повторный discovery, сверить NodeID/UID и только после recovery возвращать узел в сценарий.
- **SET_NODE_ID transition**: текущая Fluidics-реализация отправляет `ACK` со старого NodeID, а `DONE` уже с нового NodeID. Дирижер должен принять оба адреса в рамках этой транзакции.
- **COMMIT/REBOOT/FACTORY_RESET**: после этих сервисных команд Дирижер должен ожидать временную потерю связи, затем выполнять повторный discovery (`0xF001`) и сверку NodeID/UID.
- **GET_STATUS**: Дирижер должен поддержать `0xF007` как общий диагностический запрос Executor-плат. Ответ состоит из DATA-метрик `metric_id:uint16 LE + value:uint32 LE`, затем `DONE`.

### 1.5.3. Контракт перевода Host-команд в Fluidics-команды

Команды `Host -> Conductor` являются точкой отсчета и не меняются при переходе Fluidics на атомарные finite-команды. Дирижер обязан парсить Host payload по `Commands_API/User_Commands`, затем через recipe/translator/calibrator получить физический параметр для нужной платы и устройства.

Обязательный pipeline для насосных recipe-команд:

```text
Host command
  -> parser: канонические поля Host API
  -> recipe selector: технологический сценарий
  -> calibrator: volume_ul -> duration_ms
  -> device mapping: system pump -> Fluidics NodeID/ch_idx
  -> packer: PUMP_RUN_DURATION(ch_idx, duration_ms), DLC=8
  -> job manager: wait Executor DONE, then continue recipe
```

Для `WASH_STATION_FILL` канонический порядок Host-параметров:

```text
WASH_STATION_FILL(volume:uint16, cuvette:uint16)
```

Дирижер не должен менять этот порядок и не должен трактовать executor-level layout как Host-level layout. Пример для `volume=500`, `cuvette=10`: parser извлекает `volume_ul=500` и `cuvette=10`; calibrator рассчитывает `duration_ms`; device mapping выбирает физический насос заполнения; packer отправляет Fluidics одну команду `0x0201 PUMP_RUN_DURATION`.

Запрещенный рецептный шаблон для дозирования насосом:

```text
PUMP_START -> WAIT_MS(duration_ms) -> PUMP_STOP
```

Требуемый шаблон:

```text
PUMP_RUN_DURATION(pump_id, duration_ms)
```

Инварианты приемки Conductor:

- для штатного дозирования не отправлять `0x0202 PUMP_START` и `0x0203 PUMP_STOP`;
- `WAIT_MS` использовать только для технологических пауз, не для удержания насоса включенным;
- при отсутствии валидной калибровки завершать Host/job ошибкой, без запуска насоса по default;
- operation timeout Дирижера для `PUMP_RUN_DURATION` рассчитывать как `duration_ms + margin`;
- low-level `DONE` от Fluidics продвигает только один atomic step;
- Host `DONE` отправляется только после завершения всего recipe/job.

## 1.6. Требования к валидации на стороне Дирижера

### 1.6.1. Предотвращение ошибок (Safety First)
- **Type Check**: Дирижеру запрещено отправлять команду `PUMP_START` на `ID 13` (клапан). Исполнитель вернет `NACK (0x0002)`.
- **Zero Duration**: Команда `0x0201` (RUN_DURATION) с `timeout = 0` является невалидной.
- **DLC Check**: Кадры с `DLC != 8` текущая прошивка отбрасывает на транспортном уровне без ACK/NACK; Дирижер фиксирует это по ACK timeout.
- **Timeout LE**: Ненулевой timeout должен кодироваться как `uint32_t` Little-Endian в байтах payload 3-6.

### 1.6.2. Сервисный аудит (Discovery)
При включении Дирижер отправляет `0xF001 (Get Info)`.
**Ожидаемый ответ для данного исполнителя:**
- `Device Type`: `0x03` (Fluidic Controller).
- `Channels`: `16` (13 насосов + 3 клапана).
- Ответ: ACK `05103000`, три DATA-кадра `07103000`, затем DONE `07103000`.
- Первый DATA payload: `02 80 03 FW_MAJOR FW_MINOR 10 UID0 UID1`.

### 1.6.3. Диагностический статус `GET_STATUS (0xF007)`
Дирижер отправляет:

```bash
cansend can0 00301000#07F0000000000000
```

Ожидаемый жизненный цикл: `ACK -> DATA... -> DONE`.

Для `GET_STATUS` порядок кадров является частью контракта. Все DATA-метрики должны прийти до DONE. Если DATA появляется после DONE, это ошибка реализации Executor TX ordering, а не особенность Дирижера.

Каждый DATA payload после первых двух служебных байтов имеет формат:

```text
02 80 metric_lo metric_hi value0 value1 value2 value3
```

Минимальный набор метрик Fluidics совпадает с общим стандартом DDS-240:

| Metric ID | Значение |
|:----------|:---------|
| `0x0001` | RX frames accepted by transport task |
| `0x0002` | TX frames submitted to HAL |
| `0x0003` | RX queue overflow |
| `0x0004` | TX queue overflow |
| `0x0005` | Dispatcher queue overflow |
| `0x0006` | Dropped frame: not Extended ID |
| `0x0007` | Dropped frame: foreign destination |
| `0x0008` | Dropped frame: non-COMMAND message type |
| `0x0009` | Dropped frame: invalid executor DLC |
| `0x000A` | TX mailbox guard timeout |
| `0x000B` | `HAL_CAN_AddTxMessage` error |
| `0x000C` | CAN error callback count |
| `0x000D` | CAN error-warning count |
| `0x000E` | CAN error-passive count |
| `0x000F` | CAN bus-off count |
| `0x0010` | Last HAL CAN error code |
| `0x0011` | Last CAN ESR register snapshot |
| `0x0012` | Application/domain queue overflow |

#### 1.6.3.1. Как Дирижер использует `GET_STATUS`
`GET_STATUS` не является ответом на текущую команду и не заменяет `ACK/DONE` timeout. Это диагностический снимок, который Дирижер использует для объяснения причины уже возникшего события.

Алгоритм контроля:

1. Первый успешный `GET_STATUS` после discovery сохранить как baseline.
2. После каждого следующего `GET_STATUS` считать дельту по каждой метрике: `delta = current - previous`.
3. Принять решение по дельтам.
4. Сохранить текущий снимок как новый baseline.

Рекомендуемые события для опроса:

- после первого `GET_DEVICE_INFO`;
- периодически в фоне, например раз в 1-5 секунд;
- сразу после `ACK timeout`, `DONE timeout` или неожиданного `NACK`;
- после `REBOOT`, `COMMIT`, `FACTORY_RESET`, `SET_NODE_ID` и повторного discovery;
- перед критическим сценарием, если нужен свежий health-baseline.

Пример: если команда получила `ACK timeout`, а следующий `GET_STATUS` отвечает и показывает `delta dropped_wrong_dlc = 1`, значит исполнитель жив и видел кадр, но отбросил его из-за неверного DLC. Это software/protocol fault на стороне формирователя команды; повторять тот же кадр бессмысленно.

#### 1.6.3.2. Матрица решений для Дирижера

| Дельта | Решение |
|:-------|:--------|
| `dropped_wrong_dlc > 0` | Не ретраить тот же кадр. Проверить packer: Conductor -> Executor обязан быть `DLC=8`. |
| `dropped_not_ext > 0` | Проверить, что используются 29-bit Extended ID. Для собственной команды это software fault. |
| `dropped_wrong_dst > 0` | Выполнить discovery, сверить NodeID/UID и маршрутизацию. |
| `dropped_wrong_type > 0` | Проверить message type: Executor должен получать `COMMAND`. |
| `rx_queue_overflow > 0` | Снизить частоту команд, проверить нагрузку шины и приоритет transport task. |
| `dispatcher_queue_overflow > 0` | Снизить частоту валидных команд к узлу, проверить Dispatcher и размер очереди. |
| `app_queue_overflow > 0` | Доменная задача перегружена; проверить сценарий и длительность операций. |
| `tx_queue_overflow > 0` | Исполнитель не успевает передавать ответы; проверить частоту запросов и DATA-ответы. |
| `tx_mailbox_timeout > 0` | Проверить физику CAN, ACK, терминаторы, загрузку шины и состояние адаптера. |
| `tx_hal_error > 0` | Читать `last_hal_error`/`last_esr`, выполнить recovery policy. |
| `error_warning/passive > 0` | Пометить шину или узел как degraded, проверить кабель/питание/скорость/терминацию. |
| `bus_off_count > 0` | Вывести узел из активного сценария, выполнить recovery/discovery после восстановления. |

Если по доменной команде получен `ACK`, но `DONE` не приходит, а `GET_STATUS` не показывает переполнений очередей и CAN fault counters, Дирижер должен трактовать это как внутренний fault исполнителя: доменная задача не работает, не создана или заблокирована. Для STM32 Executor это требует проверки RTOS resource integrity: heap, task handles, queues, software timers.

## 1.7. Коды ошибок NACK (Error Registry)

Этот раздел описывает Fluidics semantics. Общий принцип для всех исполнителей: NACK payload трактуется в контексте `src NodeID + cmd_code + error_code`; scoped aliases по Motion/Fluidics/Thermo зафиксированы в `dds240_global_config.h`.

| Код | Причина | Действие Дирижера |
|:----|:--------|:------------------|
| `0x0001` | Unknown Command | Проверить код в словаре. |
| `0x0002` | Invalid Channel | Проверить индекс (0-15) и тип (насос/клапан). |
| `0x0003` | Device Busy | Повторить команду после DONE или STOP. |
| `0x0004` | Invalid Key | Проверить Magic Key сервисной команды. |
| `0x0005` | Flash Error     | Ошибка записи настроек. |

Для Fluidics код `0x0003` также может использоваться как отказ запуска нагрузки, если защитный таймер недоступен. В этом случае Дирижер не должен ждать `DONE` и обязан считать команду невыполненной.

## 1.8. Статус проверки Fluidics (21.04.2026)

Этот раздел фиксирует фактические результаты уже проведенных проверок Fluidics. Он содержит историческую семантику `PUMP_START/RUN_DURATION`, действовавшую на момент тестов. Целевая семантика после решения 23.04.2026 описана выше: `PUMP_RUN_DURATION` должен отправлять `DONE` только после выключения насоса по истечении `duration_ms`.

- CANable / Linux SocketCAN: PASS на 1 Mbit/s.
- `GET_DEVICE_INFO (0xF001)`: получены ACK, 3 DATA, DONE.
- `PUMP_START (0x0202)` и `PUMP_STOP (0x0203)` для канала 0: получены ACK и DONE.
- 20.04.2026: проверена работа через пассивный CAN-switch, рассчитанный на подключение до 16 CAN-устройств.
- 20.04.2026: причина предыдущего отказа локализована на физическом уровне - поврежденный разъем кабеля.
- 20.04.2026: физическая нагрузка каналов 0 и 1 проверена командой `load-cycle 0 1 3 1 0.5`; кулеры вращались, индивидуальный и совместный режимы отработали.
- После нагрузочного теста `can0` остался `ERROR-ACTIVE`, новые `bus-errors`, `bus-off`, RX/TX errors не появились.
- 20.04.2026: NACK matrix basic PASS: неверный тип ресурса, индекс вне диапазона, неизвестная команда, неверный Magic Key.
- 20.04.2026: Addressing PASS: чужой `DstAddr=0x20` игнорируется, broadcast `DstAddr=0x00` принимается.
- 20.04.2026: `GET_UID (0xF004)` PASS; UID совпадает с фрагментами `GET_DEVICE_INFO`.
- 20.04.2026: `SET_NODE_ID` в RAM PASS (`0x30 -> 0x31 -> 0x30`). Текущая реализация отправляет `ACK` со старого NodeID, а `DONE` уже с нового NodeID.
- 20.04.2026: `REBOOT (0xF002)`, `COMMIT (0xF003)`, `SET_NODE_ID + COMMIT + REBOOT` persistence и `FACTORY_RESET (0xF006)` PASS; после финального reset плата отвечает на default `0x30`.
- 20.04.2026: transport negative tests PASS: короткий DLC, Standard ID и non-COMMAND frame игнорируются без ACK/NACK.
- Проект соответствует общей executor-архитектуре DDS-240 по CAN ID, NodeID, strict DLC=8, ACK/DONE, 0-based индексам, сервисным `0xF001/0xF007`, FreeRTOS tasks/queues, Mailbox Guard, TX FIFO ordering и RTOS resource checks.
- 20.04.2026: исправлен и проверен разбор ненулевого timeout для `PUMP_START`; кадр `00301000#020200D007000000` включил нагрузку канала 0 и автоотключил ее примерно через 2 секунды.
- 20.04.2026: `RUN_DURATION (0x0201)` с timeout `2000 ms` проверен кадром `00301000#010200D007000000`; нагрузка отработала штатно.
- 20.04.2026: добавлен guard запуска safety timer: нагрузка включается только после успешного `osTimerStart()`. После изменения регрессия `GET_DEVICE_INFO`, `PUMP_START/STOP` и `RUN_DURATION 2000 ms` прошла штатно.
- 20.04.2026: реализован safe-state hook `PumpsValves_AllOff()` для старта, `Error_Handler()` и fault handlers. После изменения регрессия `PUMP_START/STOP` и `RUN_DURATION 2000 ms` прошла штатно.
- 20.04.2026: Flash config page `0x0800FC00..0x0800FFFF` исключена из области приложения в linker script (`FLASH LENGTH = 63K`); сборка прошла с запасом до reserved page.
- 21.04.2026: реализован и проверен `GET_STATUS (0xF007)`: `ACK -> DATA metrics 0x0001..0x0012 -> DONE`.
- 21.04.2026: выявлено и устранено переупорядочивание многокадрового ответа `GET_STATUS`; `TransmitFifoPriority = ENABLE` обязателен, после изменения DONE приходит последним.
- 21.04.2026: выявлен RTOS resource fault при `configTOTAL_HEAP_SIZE = 4096`: плата отвечала `ACK`, но доменная задача Fluidics не отправляла `DONE`. Увеличение heap до `8192` и проверка `osThreadNew()` восстановили штатную работу.
- 21.04.2026: после увеличения heap регрессия `load-cycle 0 1 3 1 0.5` PASS: каналы 0 и 1 получили ACK/DONE в индивидуальном и совместном циклах.
- 21.04.2026: default safety timeout насоса PASS: `PUMP_START` с timeout `0` на канале 0 дал `ACK/DONE`, нагрузка включилась и самостоятельно отключилась примерно через 60 секунд. `DONE` зафиксирован как подтверждение старта GPIO/safety timer, а не окончания 60-секундного интервала.
- 21.04.2026: fault-injection отказа safety timer PASS на канале 0: при принудительном отказе запуска таймера команда `PUMP_START` дала `ACK`, затем `NACK DEVICE_BUSY (0x0003)`, без `DONE`. Проверяется обязательное правило: при недоступной защите нагрузка не должна включаться.
- 21.04.2026: после выключения fault-injection регрессия production-сборки PASS: `pump-cycle 0 1` и `GET_DEVICE_INFO` вернули штатные `ACK/DONE`.
- 21.04.2026: IWDG integration build PASS: включен аппаратный IWDG (`Prescaler=256`, `Reload=624`, около 4 секунд), добавлена supervisor-задача, которая кормит watchdog только при heartbeat задач CAN, Dispatcher и Fluidics.
- 21.04.2026: IWDG normal runtime / idle 120s PASS: после 120 секунд без CAN-команд `GET_DEVICE_INFO` вернул `ACK -> DATA -> DONE`, ложного watchdog reset в простое не было.
- 21.04.2026: IWDG fault-injection / Fluidics task stall after output ON PASS: `PUMP_START 0` дал `ACK`, затем `DONE not found`; физическая нагрузка включилась, после срабатывания supervisor перешла в `OFF`, через 8 секунд плата снова ответила на `GET_DEVICE_INFO` (`ACK -> DATA -> DONE`) после IWDG recovery.
- 21.04.2026: fault handler safe-state PASS: тестовый вход в `HardFault_Handler()` после `PUMP_START 0` дал `ACK`, затем `DONE not found`; физическая нагрузка кратко включилась и была отключена fault handler через `PumpsValves_AllOff()`, после IWDG reset плата снова ответила на `GET_DEVICE_INFO` (`ACK -> DATA -> DONE`).
- 21.04.2026: fault handler safe-state повторно подтвержден после явной перепрошивки тестовой сборкой: нагрузка сделала краткий `ON -> OFF`, `PUMP_START 0` дал `ACK` без `DONE`, после `sleep 8` `GET_DEVICE_INFO` вернул `ACK -> DATA -> DONE`.
- 21.04.2026: final production smoke-test PASS после возврата всех fault-injection флагов в `0` и перепрошивки МК: `GET_DEVICE_INFO`, `pump-cycle 0 1`, затем `sleep 10` и повторный `GET_DEVICE_INFO` вернули штатные `ACK/DONE`.
- 21.04.2026: добавлен глобальный конфиг экосистемы `dds240_global_config.h`: CAN transport, NodeID/DeviceType, service commands `0xF001..0xF007`, `GET_STATUS` metrics, NACK registry, Fluidics defaults, IWDG/safe-state/RTOS reference constants.
- 21.04.2026: зафиксирован workflow использования `dds240_global_config.h`: файл остается reference config в общей документации; текущие проверенные локальные протоколы не переписываются автоматически; новые платы и Дирижер интегрируют общие значения из него поэтапно через diff-аудит, сборку и CAN-регрессию.
- 21.04.2026: добавлен `EXECUTOR_INDUSTRIALIZATION_PLAYBOOK.md` - самодостаточная инструкция для будущего доведения Motion, Thermo и новых Executor-плат до промышленного стандарта без необходимости иметь контекст этого чата.
- 20.04.2026: post-flash runtime-регрессия `GET_INFO -> COMMIT -> REBOOT -> GET_INFO` PASS; после перезагрузки плата ответила на NodeID `0x30` с теми же UID-фрагментами.
- 05.05.2026: текущая Fluidics WIP после CubeMX/RTOS regeneration собирается с `configTOTAL_HEAP_SIZE=10240`, `task_dispatcher=256*4`; `F001 GET_DEVICE_INFO` PASS без нагрузок. Новая finite-семантика `PUMP_RUN_DURATION 2000 ms` подтверждена no-load: `ACK` сразу, delayed `DONE` примерно через 1995 ms. Отказы `duration_ms=0 -> NACK 0x0006` и busy-channel -> `NACK 0x0003` подтверждены no-load. `GET_STATUS` после finite-command цикла чистый: `RX_TOTAL=8`, `TX_TOTAL=18`, fault/drop/overflow counters `0`.
- 05.05.2026: после исправления замятого контакта CAN-разъема прототипный стенд с двумя fan-load каналами `ch_idx=0/1` прошел post-fix нагрузочную приемку: одиночный и совместный `PUMP_RUN_DURATION`, busy-channel `ACK + NACK 0x0003`, invalid-duration `ACK + NACK 0x0006` без включения нагрузки. Финальный `GET_STATUS`: `RX_TOTAL=12`, `TX_TOTAL=47`, metrics `0x0003..0x0012 = 0`; SocketCAN оставался `ERROR-ACTIVE`, все `error-warn/error-pass/bus-errors/bus-off = 0`.
- Посыл Дирижеру после 05.05.2026: дозирование насосом должно строиться как один finite executor step `PUMP_RUN_DURATION(duration_ms)`, а не как recipe-side `PUMP_START -> WAIT_MS -> PUMP_STOP`. `DONE` по `PUMP_RUN_DURATION` приходит только после OFF и продвигает один atomic step; Host `DONE` отправляется только после завершения всего recipe. Operation timeout рассчитывается как `duration_ms + margin`; fixed `FAST_DONE` timeout к этой команде не применять.
- До расширенного приемочного теста остаются: клапаны 13-15, физический прогон насосов 2-12, default safety timeout клапанов.
- Промышленное доведение Fluidics по safety/watchdog/fault-handler блоку закрыто для канала 0; остаются расширенные физические проверки остальных каналов.

---

# 7. Интеграция платы термодатчиков (Thermo) — УНИФИКАЦИЯ 2.0
**NodeID Исполнителя термодатчиков**: `0x40`.

> **ВАЖНО:** Данная спецификация приведена в соответствие с **Директивой 2.0 (см. Раздел 9)**. Все команды от Дирижера обязаны иметь DLC=8. Физическая адресация 1-Wire ROM ID переведена на двухфазный протокол для обеспечения совместимости с MTU классического CAN.

## 7.1. Сетевые параметры (Transport Layer)
- **Протокол**: bxCAN (STM32), 1 Mbit/s.
- **Формат ID**: 29-bit Extended.
- **NodeID Исполнителя**: `0x40`.
- **Broadcast**: `0x00`.

## 7.2. Структура командного фрейма (Payload) — Директива 2.0
Дирижер упаковывает данные в формате **Little-Endian**. **Строгий DLC=8 для всех команд.**

| Байты | Поле | Тип | Описание |
|:------|:-----|:----|:---------|
| 0-1   | `cmd_code` | uint16 | Код команды (напр. 0x9011). |
| 2     | `ch_idx`   | uint8  | **0-based индекс** датчика (0-7). |
| 3-6   | `parameter`| mixed  | ROM ID (uint32), Thresholds и т.д. |
| 7     | `reserved` | uint8  | Всегда `0x00`. |

## 7.3. Реестр логических устройств (Thermo Device Mapping)
Дирижер адресует датчики по их внутренним индексам в таблице маппинга.

### 7.3.1. Термодатчики (Sensors 0-7)
- **Команды**:
    - `GET_TEMP (0x9011)`: Запрос текущего значения температуры.
    - `GET_ALL_TEMPS (0x9010)`: Запрос данных по всем датчикам (потоковый режим).
    - `SCAN_1WIRE (0xF101)`: Запуск поиска новых устройств на шине.
    - `GET_PHYS_ID (0xF102)`: Получение найденного 64-bit ROM ID по индексу сканирования.
    - `SET_MAP_P1 (0xF103)`: Фаза 1 записи ROM ID в mapping.
    - `GET_CHANNEL_MAP (0xF104)`: Чтение текущего ROM ID логического канала.
    - `SET_MAP_P2 (0xF105)`: Фаза 2 записи ROM ID в mapping.

## 7.4. Спецификация параметров команд

| Код | Команда | Параметры (байты 3-6) | Описание |
|:----|:--------|:----------------------|:---------|
| `0x9011` | **GET_TEMP** | - | Ответ: DATA (int16, 0.1°C). |
| `0x9010` | **GET_ALL**  | - | Циклическая отправка DATA для всех датчиков. |
| `0xF101` | **SCAN_1WIRE** | - | Поиск устройств на 1-Wire bus, ответ DATA с количеством найденных ROM. |
| `0xF102` | **GET_PHYS_ID** | `ch_idx`/`phys_idx` в байте 2 | Ответ DATA с индексом и 64-bit ROM ID, разбитым на strict-DLC кадры. |
| `0xF103` | **SET_MAP_P1** | `rom_low` (uint32) | Фаза 1: Передача первых 4 байт ROM ID. |
| `0xF104` | **GET_CHANNEL_MAP** | - | Ответ DATA с текущим ROM ID логического канала, разбитым на strict-DLC кадры. |
| `0xF105` | **SET_MAP_P2** | `rom_high` (uint32) | Фаза 2: Передача оставшихся 4 байт + Запись. |

## 7.5. Жизненный цикл команды (Transaction)
Дирижер обязан отслеживать состояние транзакции по следующей схеме:

1. **SEND**: Дирижер отправляет команду (DLC=8).
2. **ACK (0x01)**: Дирижер должен получить подтверждение приема в течение **50 мс**.
3. **DATA (0x02)**: Исполнитель отправляет значение температуры (инт16, 0.1°С).
4. **DONE (0x01)**: Дирижер ожидает сигнал завершения в течение **100 мс** после ACK.

## 7.6. Требования к валидации на стороне Дирижера

### 7.6.1. Формат данных (Data Representation)
- Значение температуры `37.5°C` передается как `375` (`0x0177`). Тип `int16_t`.
- При `GET_ALL_TEMPS (0x9010)` приходят кадры DATA: байт 2 — индекс, байты 3-4 — значение. **DLC ответа всегда 8.**

### 7.6.2. Сервисный аудит (Discovery)
При включении Дирижер отправляет `0xF001 (Get Info)`.
**Ожидаемый ответ для данного исполнителя:**
- `Device Type`: `0x02` (Thermo Board).
- `Channels`: `8` (DS18B20 Sensors).

## 7.7. Коды ошибок NACK (Error Registry)

Thermo NACK-коды имеют scoped semantics: Дирижер трактует их в контексте `src NodeID=0x40` и `cmd_code`. Совпадение числа `0x0003` с Motion/Fluidics busy-кодами не является конфликтом, потому что в Thermo-домене это `SENSOR_FAILURE`.

| Код | Причина | Действие Дирижера |
|:----|:--------|:------------------|
| `0x0001` | Unknown Command | Проверить версию FW исполнителя. |
| `0x0002` | Invalid Sensor ID | Проверить 0-based индекс датчика (0-7). |
| `0x0003` | Sensor Failure | Проверить физическое подключение 1-Wire. |
| `0x0004` | Invalid Key | Проверить Magic Key (0x55AA / 0xDEAD). |
| `0x0005` | Flash Error | Ошибка записи настроек во Flash. |
| `0x0006` | Invalid Param | Проверить payload-параметры команды. |
| `0x0007` | Thermo Busy | Повторить команду после завершения текущей операции. |

Кадры с неверной транспортной оболочкой (`DLC != 8`, Standard ID, чужой `DstAddr`, не `COMMAND`) не являются прикладочной ошибкой Thermo и должны отбрасываться на transport layer без ACK/NACK. Дирижер видит это как ACK timeout и может уточнить причину через `F007 GET_STATUS`.

## 7.8. Сервисные команды (Maintenance) — Единые Ключи

### 7.8.1. Общие service-команды Executor

| Код | Команда | Параметры | Описание |
|:----|:--------|:----------|:---------|
| `0xF001` | **GET_DEVICE_INFO** | - | DeviceType, версия, количество каналов, UID fragments. |
| `0xF002` | **REBOOT** | `key` (0x55AA) | Программная перезагрузка. |
| `0xF003` | **COMMIT** | - | Сохранение текущих настроек во Flash. |
| `0xF004` | **GET_UID** | - | Полный UID исполнителя в DATA-кадрах. |
| `0xF005` | **SET_NODE_ID** | `new_id` в байте 2 | Изменение NodeID в RAM, сохранение через `F003`. |
| `0xF006` | **FACTORY**| `key` (0xDEAD) | Сброс к заводским настройкам. |
| `0xF007` | **GET_STATUS** | - | Диагностический статус: DATA-метрики `metric_id:uint16 LE + value:uint32 LE`. |

### 7.8.2. Thermo service/install-команды

| Код | Команда | Параметры | Описание |
|:----|:--------|:----------|:---------|
| `0xF101` | **SCAN_1WIRE** | - | Поиск DS18B20 на 1-Wire bus. |
| `0xF102` | **GET_PHYS_ID** | `phys_idx`/`ch_idx` в байте 2 | Чтение ROM ID найденного датчика. |
| `0xF103` | **SET_MAP_P1** | `ch_idx`, `rom[0..3]` | Фаза 1 записи mapping. |
| `0xF104` | **GET_CHANNEL_MAP** | `ch_idx` | Чтение ROM ID, привязанного к логическому каналу. |
| `0xF105` | **SET_MAP_P2** | `ch_idx`, `rom[4..7]` | Фаза 2 записи mapping. |

---

# 7.9. Интеграция Sensor Executor: датчики положения и юстировки — ПРОЕКТ
**Предварительный NodeID исполнителя датчиков положения**: `0x50`.  
**Предварительный DeviceType**: `0x04`.

> Раздел описывает интеграционный контракт для Дирижера. Техническое задание на саму плату ведется отдельно: `readme/DDS-240_eko_system/Technical_Assignment_20260507_Sensor_Position_Executor.md`.

## 7.9.1. Роль в системе

Sensor Executor обслуживает датчики положения, Холла, оптодатчики и датчики крышек. Он не управляет моторами, насосами или клапанами.

Разделение ответственности:

- Sensor Executor читает GPIO/ADC, фильтрует сигналы и отдает нормализованное состояние;
- Дирижер хранит системное состояние, кэш датчиков, offset и принимает решение `allow/block/adjust`;
- Motion/Fluidics выполняют физические действия по командам Дирижера;
- Host получает состояние через существующие команды `0x90xx`.

## 7.9.2. Сетевые параметры

- **Протокол**: bxCAN, 1 Mbit/s.
- **Формат ID**: 29-bit Extended.
- **NodeID исполнителя**: `0x50` предварительно.
- **Broadcast**: `0x00`.
- **DLC команд от Дирижера**: строго `8`.
- **Endian**: Little-Endian.

Текущий список занятых NodeID:

| NodeID | Узел |
|--------|------|
| `0x10` | Conductor |
| `0x20` | Motion |
| `0x30` | Fluidics |
| `0x40` | Thermo |
| `0x50` | Sensors, предварительно |

## 7.9.3. Discovery и обязательные service-команды

При старте Дирижер должен выполнить discovery нового узла через broadcast или прямой NodeID.

Минимальный service-набор:

| Код | Команда | Назначение |
|-----|---------|------------|
| `0xF001` | `GET_DEVICE_INFO` | Проверить DeviceType, количество каналов, версию |
| `0xF002` | `REBOOT` | Перезагрузка по ключу |
| `0xF003` | `COMMIT` | Сохранение конфигурации во Flash исполнителя |
| `0xF006` | `FACTORY_RESET` | Сброс конфигурации по ключу |
| `0xF007` | `GET_STATUS` | Диагностика transport/domain metrics |

Ожидаемые поля `GET_DEVICE_INFO` требуют уточнения после назначения окончательного DeviceType и количества входов.

## 7.9.4. Host-команды, маршрутизируемые через Дирижер

Дирижер принимает команды Host `0x90xx` и маршрутизирует позиционную часть на Sensor Executor.

Минимальный набор:

| Host API | Назначение | Действие Дирижера |
|----------|------------|-------------------|
| `0x9020 SENSOR_GET_ALL_POSITIONS` | Все датчики положения | Ответить из свежего кэша или запросить Sensor Executor |
| `0x9021 SENSOR_GET_POSITION` | Один датчик положения | Проверить `sensor_id`, вернуть нормализованное состояние |
| `0x9040 SENSOR_GET_COVERS` | Состояние крышек | Использовать данные Sensor Executor, если крышки подключены к нему |
| `0x90F0 SENSOR_LIST` | Список датчиков | Вернуть `active/calibrated` флаги |
| `0x9050/0x9060 SENSOR_CONFIG` | Конфигурация датчиков | Формат для датчиков положения требует отдельного решения |

Температуры остаются в Thermo Executor. Жидкостные датчики на текущем этапе не входят в этот интеграционный блок.

## 7.9.5. Логические датчики положения

Базовые `sensor_id` берутся из Host API:

| ID | Имя | Назначение |
|----|-----|------------|
| `0x01` | `POS_REACTION_HOME` | Home реакционного диска |
| `0x02` | `POS_REAGENT_HOME` | Home ротора реагентов |
| `0x03` | `POS_SAMPLE_HOME` | Home диска образцов |
| `0x10` | `POS_DISPENSER_Z` | Датчик Z дозатора |
| `0x11` | `POS_DISPENSER_X` | Датчик X дозатора |
| `0x20` | `POS_MIXER_Z` | Датчик Z миксера |
| `0x30` | `COVER_MAIN` | Основная крышка |
| `0x31` | `COVER_REAGENT` | Крышка реагентов |
| `0x32` | `COVER_SAMPLE` | Крышка образцов |

Дирижер не должен работать с сырым GPIO. Он работает только с логическим `sensor_id` и нормализованным состоянием.

## 7.9.6. Кэш состояния в Дирижере

Дирижер должен хранить локальный кэш состояния датчиков:

```text
sensor_id
state
active
calibrated
fault
quality
last_update_ms
```

Правила:

- если кэш свежий, Host-запрос можно закрыть без CAN-запроса к исполнителю;
- если кэш устарел, Дирижер запрашивает Sensor Executor;
- если Sensor Executor недоступен, зависимые операции блокируются;
- `stale/fault/degraded` не должны трактоваться как штатное `inactive`.

Timeout свежести кэша требует отдельного решения для каждой группы датчиков.

## 7.9.7. Интеграция юстировки и offset

Для позиционных датчиков с ADC-профилем Sensor Executor выполняет низкоуровневую обработку:

- чтение ADC;
- фильтрацию;
- поиск пика, центра окна или фронта;
- оценку качества профиля;
- подготовку компактного результата.

Для текущей архитектуры принято решение использовать сервисный режим медленного вращения. Дирижер не ловит фронт датчика через CAN во время быстрого непрерывного движения.

Рабочая схема:

```text
Motion: выполнить малый шаг или малый пакет шагов
Motion: остановиться и отдать DONE
Дирижер: запросить Sensor Executor
Sensor Executor: вернуть digital/ADC состояние
Дирижер: повторить цикл и построить профиль
```

Так задержки CAN/RTOS не влияют на точку измерения, потому что измерение выполняется после остановки.

Рекомендуемые стартовые параметры сервисного сканирования:

| Фаза | Скорость Motion | Шаг сканирования |
|------|-----------------|------------------|
| Coarse search | `200..500 steps/s` | `20..50 steps` |
| Backoff | `100..300 steps/s` | до выхода из зоны |
| Fine scan | `50..100 steps/s` | `1..5 steps` |
| Verification | `50..100 steps/s` | `1..5 steps` |

После остановки перед чтением Sensor Executor рекомендуется выдержка `10..30 ms`. Финальные значения подбираются по механике ротора и требуемой точности offset.

В штатном режиме Дирижер получает не поток ADC-точек, а итог:

```text
sensor_id = POS_REACTION_HOME
reference_step_delta = +8
peak_adc = 3120
quality = OK
status = AUTO_ADJUST_CANDIDATE
```

Offset, допуски автоюстировки и решение `keep/adjust/stop` принадлежат Дирижеру.

Рабочая логика:

```text
abs(drift) <= deadband
    -> ничего не менять

deadband < abs(drift) <= auto_adjust_limit
    -> Дирижер корректирует offset

abs(drift) > auto_adjust_limit
    -> Дирижер останавливает или блокирует процесс
```

Коррекция Flash не должна выполняться по одному измерению. Стабильность drift подтверждается несколькими проходами.

## 7.9.8. Ошибки и состояние ресурса

Дирижер должен связывать ошибки Sensor Executor с ошибками Host API:

| Ситуация | Рекомендуемая реакция |
|----------|------------------------|
| Датчик положения неисправен | `ERR_SENS_POS_GENERAL` |
| Home не подтвержден | `ERR_SENS_POS_HOME` |
| Крышка открыта | `ERR_SENS_COVER_OPEN` |
| Ошибка датчика крышки | `ERR_SENS_COVER_ERROR` |
| Данные устарели | Повторный опрос, затем degraded/fault |
| Датчик не `calibrated` | Запрет зависимой технологической операции |

Sensor Executor не должен напрямую останавливать Motion/Fluidics. Он сообщает состояние или fault, а Дирижер принимает системное решение.

## 7.9.9. Открытые решения для интеграции

Перед реализацией в Дирижере нужно зафиксировать:

1. Окончательный NodeID.
2. Окончательный DeviceType.
3. Количество физических входов и каналов ADC.
4. Формат DATA-ответов для `0x9020`, `0x9021`, `0x90F0`.
5. Формат сервисного результата ADC-профиля.
6. Timeout свежести кэша датчиков в Дирижере.
7. Какие параметры датчиков положения открываются через `0x9050/0x9060`.
8. Какие датчики требуют локальной аппаратной защиты или дублирования сигнала на Motion/Fluidics.

---

# 8. Интеграция платы управления шаговыми двигателями (Motion) — УНИФИКАЦИЯ 2.0
**NodeID Исполнителя моторов**: `0x20`.

## 8.1. Сетевые параметры (Transport Layer)
- **Протокол**: bxCAN (STM32), 1 Mbit/s.
- **Формат ID**: 29-bit Extended.
- **NodeID Исполнителя**: `0x20`.
- **Broadcast**: `0x00`.

**Проверенные ID для пары Conductor `0x10` <-> Motion `0x20`:**

| Направление | Тип | CAN ID |
|:------------|:----|:-------|
| Conductor -> Motion | COMMAND | `00201000` |
| Motion -> Conductor | ACK | `05102000` |
| Motion -> Conductor | NACK | `06102000` |
| Motion -> Conductor | DATA / DONE / LOG | `07102000` |

## 8.2. Структура командного фрейма (Payload) — Директива 2.0
Дирижер упаковывает данные в формате **Little-Endian**. **Строгий DLC=8 для всех команд.**

| Байты | Поле | Тип | Описание |
|:------|:-----|:----|:---------|
| 0-1   | `cmd_code` | uint16 | Код команды (напр. 0x0101). |
| 2     | `ch_idx`   | uint8  | **0-based индекс** канала на плате (0-7). |
| 3-6   | `parameter`| mixed  | Steps (int32), Speed (uint16/uint8) и т.д. |
| 7     | `reserved` | uint8  | Всегда `0x00`. |

## 8.3. Реестр логических устройств (Motion Device Mapping)
Плата оперирует физическими индексами осей. Трансляция логических ID Хоста в индексы 0-7 выполняется на стороне Дирижера.

### 8.3.1. Шаговые двигатели (Motors 0-7)
- **Команды**:
    - `ROTATE (0x0101)`: Относительное движение.
    - `HOME (0x0102)`: Поиск начальной позиции (в 0).
    - `START_CONTINUOUS (0x0103)`: Непрерывное вращение.
    - `STOP (0x0104)`: Экстренная остановка.

## 8.4. Спецификация параметров команд

| Код | Команда | Параметры (байты 3-6) | Описание |
|:----|:--------|:----------------------|:---------|
| `0x0101` | **ROTATE** | `steps` (int32, LE), `speed_div_4` (byte 7) | `steps`: >0 CW, <0 CCW. `speed` = `speed_div_4 * 4`. |
| `0x0102` | **HOME**   | `speed` (uint16, LE) | Скорость поиска дома в шагах/сек. |
| `0x0103` | **START_CONT** | `speed_div_100` (byte 3) | Непрерывное вращение. `speed` = `speed_div_100 * 100`. |
| `0x0104` | **STOP**   | - | Немедленная остановка мотора. |

Правила владения параметрами Motion:

- Host API задает технологическую цель: слот, кювету, позицию, объем или тип операции. Host API не должен требовать от пользователя скорость/ускорение двигателя для обычных recipe-команд.
- `speed` является контрактом recipe-level atomic action. Дирижер выбирает `speed` для low-level Motion-команды из recipe/action config или технологического профиля.
- Для `MOTOR_ROTATE` Дирижер передает `steps` и `speed`; это контракт атомарной команды исполнителю.
- Motion Executor валидирует `speed` против локального профиля оси. Если скорость выходит за безопасные пределы, ожидаемая реакция - `NACK INVALID_PARAM`, а не запуск движения.
- Для ненулевого `steps` значение `speed=0` является invalid-param и не должно запускать движение.
- `acceleration` не входит в текущий payload `MOTOR_ROTATE 0x0101`; ускорение является локальным motion-profile параметром Motion Executor и реализуется в motion planner/driver.
- `SET_SPEED/SET_ACCELERATION` не являются Host-рецептными манипуляциями в текущем внешнем контракте; если они появятся как сервисные команды, их `DONE` означает только "параметр применен".

Ресурсная модель текущего Motion Executor:

- `TIM1 group`: motor `0..3`, общий timer base `PSC/ARR`, максимум один active motion profile одновременно.
- `TIM2 group`: motor `4..7`, общий timer base `PSC/ARR`, максимум один active motion profile одновременно.
- Параллельно допустимы максимум два движения: одно в `TIM1 group` и одно в `TIM2 group`.
- Команда на свободный motor_id, но занятую timer group, должна рассматриваться как конфликт ресурса и получать `MOTOR_BUSY`.
- Дирижер должен учитывать этот shared resource lock при планировании параллельных atomic actions. Для расчета operation timeout нужно добавлять запас на локальный acceleration/deceleration profile исполнителя.

## 8.5. Жизненный цикл команды (Transaction)
Дирижер обязан отслеживать состояние транзакции по следующей схеме:

1. **SEND**: Дирижер отправляет команду (DLC=8).
2. **ACK (0x01)**: Дирижер должен получить подтверждение приема в течение **50 мс**.
3. **DONE (0x03, Sub:0x01)**: Дирижер ожидает сигнал завершения.
   - Для `STOP / START_CONT` — в течение **100 мс** после ACK.
   - Для `ROTATE / HOME` — по факту завершения физического движения.

Корреляция `DONE`:

- `DONE` от Motion Executor завершает только одну low-level atomic-команду Дирижера.
- Host не получает этот `DONE` напрямую.
- `Host DONE` отправляет Дирижер только после завершения всего recipe/job, то есть после успешного завершения всех atomic actions.
- Внутренние события Motion (`steps_remaining == 0`, `home_switch_triggered`, `move_complete`) не являются протокольным `DONE`; они только дают задаче Motion основание отправить low-level CAN `DONE`.
- `STOP` не является штатной частью завершения `ROTATE`. Если `ROTATE` прерван через `STOP`, это отдельная команда остановки со своим `DONE`, а исходное finite movement не должно считаться успешно завершенным по своему контракту.

## 8.6. Требования к валидации на стороне Дирижера

### 8.6.1. Предотвращение ошибок (Safety First)
- **Motor Busy**: Если мотор уже выполняет движение (`ROTATE/HOME`), повторная команда (кроме штатно разрешенного `STOP/recovery`) вернет `NACK (0x0003)`.
- **Finite Motion Timeout**: Operation timeout для `MOTOR_ROTATE` должен рассчитываться из `abs(steps) / speed` с технологическим запасом. Если `ACK` получен, но `DONE` не пришел, рецепт не продолжается как успешный.

### 8.6.2. Сервисный аудит (Discovery)
При включении Дирижер отправляет `0xF001 (Get Info)`.
**Ожидаемый ответ для данного исполнителя:**
- `Device Type`: `0x01` (Motion Controller).
- `Channels`: `8` (Stepper Motors).
- Ответ: ACK `05102000`, три DATA-кадра `07102000`, затем DONE `07102000`.
- Первый DATA payload: `02 80 01 FW_MAJOR FW_MINOR 08 UID0 UID1`.

## 8.7. Коды ошибок NACK (Error Registry)
| Код | Причина | Действие Дирижера |
|:----|:--------|:------------------|
| `0x0001` | Unknown Command | Проверить код команды в словаре. |
| `0x0002` | Invalid Motor ID | Проверить индекс канала (0-7). |
| `0x0003` | Motor Busy | Дождаться `DONE` или отправить `STOP`. |
| `0x0004` | Invalid Key | Проверить Magic Key сервисной команды. |

## 8.8. Сервисные команды (Maintenance) — Единые Ключи
| Код | Команда | Параметры | Описание |
|:----|:--------|:----------|:---------|
| `0xF001` | **GET_DEVICE_INFO** | - | Паспорт исполнителя: тип устройства, версия FW, количество каналов и UID-фрагменты. |
| `0xF002` | **REBOOT** | `key` (0x55AA) | Программная перезагрузка. |
| `0xF003` | **COMMIT** | - | Сохранение текущих настроек во Flash. |
| `0xF004` | **GET_UID** | - | Полный UID исполнителя в DATA-кадрах. |
| `0xF005` | **SET_NODE_ID** | `new_id` (byte 2) | Установка нового CAN NodeID. |
| `0xF006` | **FACTORY**| `key` (0xDEAD) | Сброс к заводским настройкам и перезагрузка. |
| `0xF007` | **GET_STATUS** | - | Диагностический статус: DATA-метрики `metric_id:uint16 LE + value:uint32 LE`. |

## 8.9. Статус проверки Motion (23.04.2026)

- CANable / Linux SocketCAN: PASS на 1 Mbit/s.
- Проект собран после приведения CAN transport к требованиям экосистемы: strict `DLC=8`, 29-bit Extended ID, `TransmitFifoPriority = ENABLE`, ACK/DATA/DONE через единый TX path.
- `GET_DEVICE_INFO (0xF001)` PASS: получены ACK, 3 DATA, DONE.
- `GET_UID (0xF004)` PASS: получены ACK, 2 DATA, DONE; UID `0C 22 07 14 52 16 30 30 30 30 30 32`.
- Negative/addressing acceptance PASS: unknown command вернул `NACK 0x0001`, invalid motor id вернул `NACK 0x0002`, чужой `DstAddr` и short `DLC` отброшены без ответа, broadcast `F001` принят.
- Доменные команды без нагрузок PASS: `STOP` для каналов `0` и `7`, `HOME` без движения, `START_CONTINUOUS speed=0`, `ROTATE + STOP`, `MOTOR_BUSY`.
- `GET_STATUS (0xF007)` PASS: получены ACK, DATA-метрики `0x0001..0x0012`, DONE. Порядок многокадрового ответа корректный: DATA до DONE.
- Transport diagnostics PASS: `DROP_WRONG_DLC`, `DROP_WRONG_DST`, `DROP_WRONG_TYPE` растут адресно; queue overflow, HAL CAN error и CAN fault counters в нормальном прогоне остались `0`.
- Standard ID на текущей настройке Motion отсекается аппаратным bxCAN-фильтром до firmware: ответа нет, `DROP_NOT_EXT` не растет. Для Дирижера это остается `ACK timeout`; счетчик `DROP_NOT_EXT` нельзя использовать как обязательное подтверждение Standard-ID ошибки на этой конфигурации фильтра.
- Статус на 23.04.2026: CAN smoke-test, расширенный CAN acceptance, `GET_STATUS` diagnostics и доменный CAN-путь Motion без нагрузок пройдены. Физическое движение, сервисы `F002/F003/F005/F006`, safe-state и IWDG оставались следующими этапами на момент этого исторического прогона.

Проверенные команды SocketCAN:

```bash
# Окно 1: пассивно смотрим шину и проверяем порядок ACK/DATA/DONE.
candump -x -t a can0

# Окно 2: запрашиваем паспорт Motion executor.
cansend can0 00201000#01F0000000000000

# Окно 2: запрашиваем полный UID Motion executor.
cansend can0 00201000#04F0000000000000

# Окно 2: запрашиваем диагностический статус Motion executor.
cansend can0 00201000#07F0000000000000
```

Фактический ответ `GET_DEVICE_INFO`:

```text
TX 00201000 [8] 01 F0 00 00 00 00 00 00
RX 05102000 [8] 01 F0 00 00 00 00 00 00
RX 07102000 [8] 02 80 01 01 00 08 0C 22
RX 07102000 [8] 02 80 07 14 52 16 30 30
RX 07102000 [8] 02 80 30 30 30 32 00 00
RX 07102000 [8] 01 01 F0 00 00 00 00 00
```

Фактический ответ `GET_UID`:

```text
TX 00201000 [8] 04 F0 00 00 00 00 00 00
RX 05102000 [8] 04 F0 00 00 00 00 00 00
RX 07102000 [8] 02 80 0C 22 07 14 52 16
RX 07102000 [8] 02 80 30 30 30 30 30 32
RX 07102000 [8] 01 04 F0 00 00 00 00 00
```

Фактические результаты расширенного CAN acceptance:

| Тест | Команда | Результат |
|:-----|:--------|:----------|
| Unknown command | `cansend can0 00201000#FFFF000000000000` | `ACK`, затем `NACK 0x0001`. |
| Invalid motor id | `cansend can0 00201000#0101080000000000` | `ACK`, затем `NACK 0x0002`. |
| Foreign destination | `cansend can0 00211000#01F0000000000000` | Ответ отсутствует. |
| Short DLC | `cansend can0 00201000#01F0` | Ответ отсутствует. |
| Broadcast discovery | `cansend can0 00001000#01F0000000000000` | `ACK`, 3 DATA, `DONE` от NodeID `0x20`. |

Фактические результаты `GET_STATUS` diagnostics:

| Тест | Команда | Результат |
|:-----|:--------|:----------|
| Baseline `GET_STATUS` | `cansend can0 00201000#07F0000000000000` | `ACK`, 18 DATA-метрик `0x0001..0x0012`, затем `DONE`. |
| Short DLC counter | `cansend can0 00201000#01F0` | Следующий `F007`: `DROP_WRONG_DLC (0x0009) = 1`. |
| Foreign destination counter | `cansend can0 00211000#01F0000000000000` | Следующий `F007`: `DROP_WRONG_DST (0x0007) = 1`. |
| Wrong message type counter | `cansend can0 05201000#01F0000000000000` | Следующий `F007`: `DROP_WRONG_TYPE (0x0008) = 1`. |
| Standard ID | `cansend can0 120#01F0000000000000` | Ответ отсутствует, `DROP_NOT_EXT (0x0006) = 0`, кадр отсекается hardware filter. |

Финальный диагностический снимок после negative tests:

```text
RX_TOTAL=5, TX_TOTAL=81,
DROP_NOT_EXT=0, DROP_WRONG_DST=1, DROP_WRONG_TYPE=1, DROP_WRONG_DLC=1,
RX/TX/DISPATCHER/APP queue overflows=0,
TX mailbox/HAL CAN error/CAN warning/passive/bus-off/last error/last ESR=0
```

Фактические результаты доменных команд без нагрузок:

| Тест | Команда | Результат |
|:-----|:--------|:----------|
| STOP motor 0 | `cansend can0 00201000#0401000000000000` | `ACK`, затем `DONE` с `device_id=0`. |
| STOP motor 7 | `cansend can0 00201000#0401070000000000` | `ACK`, затем `DONE` с `device_id=7`. |
| HOME motor 0 | `cansend can0 00201000#020100C800000000` | `ACK`, затем быстрый `DONE`, так как текущая позиция уже `0`. |
| START_CONT speed=0 | `cansend can0 00201000#0301000000000000` | `ACK`, затем `DONE`; запуск PWM не требуется. |
| Исторический ROTATE + STOP | `cansend can0 00201000#0101000100000032`, затем `cansend can0 00201000#0401000000000000` | Для старой no-load прошивки: `ROTATE` давал `ACK` без `DONE`; `STOP` давал `ACK + DONE`. |
| MOTOR_BUSY | два `ROTATE` подряд на канал `0`, затем `STOP` | Второй `ROTATE` дает `ACK + NACK 0x0003`; `STOP` возвращает `ACK + DONE`. |

Историческое ограничение no-load прошивки на момент этого прогона: `ROTATE` переводил канал в active-state, но автоматический подсчет шагов, остановка и `DONE` по фактическому завершению еще не были реализованы.

Статус текущей ветки Motion Executor от 24.04.2026: `MOTOR_ROTATE` реализован как finite command через счетчик STEP-событий TIM1/TIM2 PWM path. Дирижер должен ожидать `ACK`, затем самостоятельный low-level `DONE` без штатного `STOP`. `STOP` остается отдельной отменой/остановкой active movement и не является частью нормального завершения `ROTATE`.

## 8.10. Статус проверки Motion (04.05.2026)

После перепрошивки текущей веткой Motion no-load CAN regression закрыта без подключенных нагрузок:

| Проверка | Фактический результат |
|:---------|:----------------------|
| `MOTOR_ROTATE steps=100 speed=100 sps` | `ACK`, затем самостоятельный `DONE` примерно через 1 s без штатного `STOP`. |
| `MOTOR_ROTATE steps!=0 speed=0` | `ACK`, затем `NACK 0x0006 INVALID_PARAM`; движение не запускается. |
| Конфликт внутри `TIM1 group` | Вторая команда в группе `motor0..3` получает `ACK + NACK 0x0003 MOTOR_BUSY`. |
| Конфликт внутри `TIM2 group` | Вторая команда в группе `motor4..7` получает `ACK + NACK 0x0003 MOTOR_BUSY`. |
| Параллельные разные TIM-группы | `motor0` и `motor4` стартуют независимо, оба получают `ACK`, затем отдельные `DONE`; `MOTOR_BUSY` не возникает. |
| `START_CONTINUOUS speed>0` + `STOP` | `START_CONTINUOUS` получает `ACK + DONE` после входа в continuous state; `STOP` получает `ACK + DONE` и освобождает ресурс группы. |
| `F002 REBOOT` | Неверный key дает `NACK 0x0004`, верный `0x55AA` дает `ACK + DONE` и reset. |
| `F003 COMMIT` | `ACK + DONE`, после команды узел продолжает отвечать на `F001`. |
| `F005 SET_NODE_ID` | `ACK` приходит от старого NodeID, `DONE` от нового NodeID; discovery по новому адресу проходит. |
| `F006 FACTORY_RESET` | Неверный key дает `NACK 0x0004`, верный `0xDEAD` дает `ACK + DONE`, reset и возврат к default `0x20`. |
| Финальный `F007 GET_STATUS` | Queue/drop/CAN fault counters в нормальном прогоне остаются `0`; многокадровый ответ завершается `DONE`. |

Требования к Дирижеру после этого прогона:

- Для finite `MOTOR_ROTATE` штатное завершение - `ACK`, затем `DONE` от Motion; `STOP` не используется как нормальный признак завершения.
- Operation timeout для `MOTOR_ROTATE` рассчитывается из `abs(steps) / speed` с запасом на локальный motion profile исполнителя.
- `speed=0` при ненулевых `steps` должен считаться ошибкой параметров еще на стороне планировщика Дирижера; если команда все же отправлена, Motion вернет `NACK 0x0006`.
- Планировщик Дирижера должен учитывать shared resource lock: одновременно допустимо одно движение в `TIM1 group` и одно движение в `TIM2 group`; конфликт внутри группы ожидаемо дает `MOTOR_BUSY`.
- При `F005 SET_NODE_ID` Дирижер должен принять переходный ответ `ACK` со старого NodeID и финальный `DONE` с нового NodeID, затем выполнить discovery по новому адресу.
- После `REBOOT` или `FACTORY_RESET` Дирижер должен выполнить recovery/discovery и заново сверить `NodeID`, `device_type`, `channels` и UID.

Открытые блоки Motion на уровне физики: STEP/EN еще не измерены логическим анализатором/осциллографом, реальные драйверы/моторы с нагрузкой не подключались, настоящий `HOME` не закрыт без home-condition, CAN fault/stress counters не форсировались.

---

# 9. АНАЛИЗ И АРХИТЕКТУРНОЕ РЕШЕНИЕ: УНИФИКАЦИЯ ЭКОСИСТЕМЫ (Директива 2.0)
**Дата:** 8 апреля 2026 г.
**Автор:** Дирижер (Gemini CLI)

На основе аудита текущих спецификаций и требований ПО Хоста, Дирижер выносит следующее решение по модернизации взаимодействия Дирижер <-> Исполнитель.

## 9.1. Выявленные проблемы текущей реализации
1. **Переменный DLC**: Использование разной длины кадров (3, 4, 5, 8 байт) усложняет фильтрацию и обработку на исполнителях с bxCAN.
2. **Разрыв ID исполнителей**: Использование глобальных ID (140-147) на платах моторов противоречит принципу модульности и маппингу Хоста.
3. **Разные Magic Keys**: Несогласованные ключи для сервисных команд (0x55AA vs 0xDEAD) требуют ветвления логики Дирижера.

## 9.2. Утвержденная архитектурная стратегия
Дирижер выступает в роли "умного моста", скрывая физическую топологию от Хоста. Исполнители переводятся на максимально упрощенный и унифицированный протокол.

### 9.2.1. Унификация транспорта
- **Строгий DLC=8**: ВСЕ команды типа `COMMAND` от Дирижера должны иметь DLC=8. Неиспользуемые байты заполняются `0x00`.
- **0-based Индексация**: Платы-исполнители больше не используют глобальные ID. Каждая плата оперирует индексами своих каналов:
  - Motion: моторы `0..7`.
  - Fluidic: насосы `0..12`, клапаны `13..15`.
  - Thermo: датчики `0..7`.

### 9.2.2. Унифицированная структура кадра (8 байт)
| Байты | Поле | Тип | Описание |
|:------|:-----|:----|:---------|
| 0-1   | `cmd_code` | uint16 | Код команды (Little-Endian). |
| 2     | `ch_idx`   | uint8  | **0-based индекс** канала на плате. |
| 3-6   | `parameter`| mixed  | Steps (int32), Duration (uint32), Speed и т.д. |
| 7     | `reserved` | uint8  | Всегда `0x00`. |

### 9.2.3. Единый Сервисный Слой и Ключи
Все исполнители обязаны поддерживать единые Magic Keys:
- **REBOOT (0xF002)**: Key = `0x55AA`.
- **FACTORY_RESET (0xF006)**: Key = `0xDEAD`.

## 9.3. Требования к доработке Исполнителей
1. Перейти на 0-based индексацию каналов.
2. Внедрить проверку `DLC == 8`.
3. Синхронизировать Magic Keys.
