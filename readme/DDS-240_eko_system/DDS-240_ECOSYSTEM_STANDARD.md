# DDS-240 ECOSYSTEM STANDARD (Advanced Level)
**Версия:** 1.4 (Апрель 2026)
**Статус:** Обязателен для всех Executor-устройств на базе STM32.

## 1. Архитектурный фундамент
Все устройства должны строиться на базе FreeRTOS (CMSIS_V2) с четким разделением ответственности между задачами.

### 1.0. Глобальный конфиг экосистемы
Канонический набор общих констант DDS-240 вынесен в файл `dds240_global_config.h`.

Практический порядок доведения новых STM32 Executor-плат описан в `EXECUTOR_INDUSTRIALIZATION_PLAYBOOK.md`. Этот playbook является рабочей инструкцией для инженера или AI-ассистента, когда контекст предыдущих сессий недоступен.

Этот файл является источником для новых плат и Дирижера по следующим группам:

*   CAN bitrate, strict executor DLC, 29-bit Extended ID, NodeID и DeviceType.
*   Message type, response subtype, service command `0xF001..0xF007`.
*   Общие `GET_STATUS` metric_id и базовые NACK-коды.
*   Требования `ACK without DONE`, TX FIFO ordering, IWDG supervisor, safe-state и RTOS resource checks.

Локальные `can_protocol.h` конкретной платы могут сохранять доменные алиасы, но новые общие значения должны сначала добавляться в `dds240_global_config.h`, а затем синхронизироваться в доменные заголовки.

#### 1.0.1. Правила работы с `dds240_global_config.h`

`dds240_global_config.h` является reference config экосистемы, а не требованием немедленно подключить его во все существующие прошивки.

Правила применения:

*   **Текущие стабильные исполнители не переписываются автоматически.** Если локальный `can_protocol.h` уже проверен на железе, он может оставаться локальным до планового этапа интеграции.
*   **Новые платы и Дирижер сверяются с global config первыми.** При разработке Motion, Thermo, Fluidics v2 или Conductor packer/parser значения NodeID, DeviceType, service-команд, NACK-кодов, DLC policy и `GET_STATUS` берутся из `dds240_global_config.h`.
*   **Общие изменения сначала в global config.** Новая service-команда, общая метрика `GET_STATUS`, общий NACK-код, изменение CAN policy, watchdog/safe-state policy или recovery rule сначала добавляются в `dds240_global_config.h`, затем переносятся в локальные реализации.
*   **Доменные команды могут оставаться локальными.** Команды конкретного исполнителя (`Fluidics 0x020x`, `Motion 0x010x`, `Thermo 0x901x`) допускается хранить в локальном протоколе, если они не являются общим контрактом всей экосистемы.
*   **Противоречия запрещены.** Локальный протокол платы не должен расходиться с global config по CAN ID layout, strict DLC, NodeID, DeviceType, service `0xF001..0xF007`, Magic Keys, common NACK registry и common `GET_STATUS` metric_id.
*   **Интеграция выполняется поэтапно.** Для каждой платы сначала проводится diff-аудит локального протокола против `dds240_global_config.h`, затем перенос значений, сборка, CAN-регрессия и запись результата в отчет.

Практический порядок для будущих работ:

1. Открыть `dds240_global_config.h`.
2. Сверить локальный `can_protocol.h` или Conductor packer/parser по общим блокам.
3. Перенести только общие значения, не затрагивая проверенную доменную логику без необходимости.
4. Собрать проект и пройти базовый CAN smoke-test: `GET_DEVICE_INFO`, `GET_UID`, strict DLC, foreign address и ACK/DATA/DONE ordering.
5. После базового smoke-test пройти диагностический CAN-набор: `GET_STATUS`, NACK matrix, broadcast, CAN fault counters и очереди.
6. Зафиксировать результат в интеграционном отчете конкретного исполнителя.

### 1.1. Обязательные задачи (Tasks)
1.  **Transport Task (`task_can_handler`)**: Приоритет `High`. Только прием/передача сырых фреймов.
2.  **Dispatcher Task (`task_dispatcher`)**: Приоритет `Normal`. Прикладная логика, транзакции, сервисный слой.
3.  **Application Task(s)**: Приоритет `Realtime` (движение) или `BelowNormal` (мониторинг). Прямое взаимодействие с железом.

### 1.2. Межзадачное взаимодействие (IPC)
*   **Очереди (`osMessageQueue`)**: Категорически запрещено использование глобальных переменных для передачи команд. Только очереди с фиксированным размером структур.
*   **Флаги (`osThreadFlags`)**: Использование для Event-driven уведомлений (например, `FLAG_CAN_RX`).
*   **Инкапсуляция**: Глобальные данные (Shadow RAM Config) должны быть скрыты внутри модулей (`static`) и доступны только через Thread-safe API (Mutex-protected).

### 1.3. RTOS Resource Provisioning
Каждый Executor обязан явно проверять создание всех RTOS-объектов. Молчаливый отказ `osThreadNew`, `osMessageQueueNew`, `osTimerNew` или аналогичного API запрещен.

Обязательные правила:

*   После создания очередей, задач, таймеров, mutex/semaphore проверяется `NULL`/ошибка возврата.
*   Если не создана критическая задача или очередь, Executor переводит выходы в safe state и входит в `Error_Handler()` до приема команд.
*   Если не создан или не запускается доменный safety timer, исполнитель не включает нагрузку и завершает команду прикладным отказом (`NACK`, например `DEVICE_BUSY`), без `DONE`.
*   `configTOTAL_HEAP_SIZE` должен рассчитываться под фактическое количество задач, очередей, timer task, software timers и диагностических сервисов. CubeMX default не считается промышленной настройкой.
*   После добавления диагностических сервисов, очередей или software timers требуется повторная проверка старта задач и регрессия `ACK/DONE` на доменных командах.

Проверенный исторический ориентир для STM32F103 Fluidics был `configTOTAL_HEAP_SIZE = 8192` при трех задачах приложения, CAN/Dispatcher/Fluidics очередях, timer task и 16 one-shot software timers. Текущий Fluidics baseline 05.05.2026 после IWDG/watchdog-task и finite `PUMP_RUN_DURATION` использует `configTOTAL_HEAP_SIZE = 10240`. Для Motion, Thermo и будущих плат значение рассчитывается отдельно, но правило проверки RTOS-объектов обязательно такое же.

---

## 2. Стандарт CAN-связи (Транспорт)

### 2.1. Физический уровень
*   **Bitrate**: 1 Mbit/s.
*   **Executor DLC Policy**: Strict DLC=8 for `Conductor -> Executor` and `Executor -> Conductor` CAN frames. Unused bytes are padded with `0x00`.
*   **Host Boundary**: This executor policy does not change `Host -> Conductor` / `User_Commands`, where command-specific dynamic DLC remains allowed.
*   **ID Format**: 29-bit Extended ID.
*   **Addressing**: Динамический `NodeID` (0x02..0x7F). Адрес `0x10` зарезервирован за Дирижером.
*   **Reference Timing**: STM32F103 executor profile verified at APB1=32 MHz, Prescaler=2, BS1=11TQ, BS2=4TQ, SJW=1TQ, sample point 75%.

### 2.2. Фильтрация и надежность
*   **Broadcast**: Каждое устройство ОБЯЗАНО принимать сообщения с `DstAddr = 0x00`.
*   **Mailbox Guard**: Перед вызовом `HAL_CAN_AddTxMessage` обязательна проверка наличия свободных ящиков (`HAL_CAN_GetTxMailboxesFreeLevel`) с таймаутом ожидания до 10мс.
*   **TX FIFO Ordering**: Для STM32 bxCAN Executor-узлов обязательна настройка `TransmitFifoPriority = ENABLE`. Многокадровые ответы должны сохранять порядок постановки `DATA...DONE`; `DONE` является терминальным кадром ответа и не должен обгонять последние DATA-кадры.
*   **Transport Envelope Drop**: Кадры с невалидной транспортной оболочкой игнорируются без `ACK/NACK`. К таким кадрам относятся: не Extended ID, чужой `DstAddr`, неверный `Message Type`, `DLC != 8` для executor-уровня.
*   **Conductor ACK Timeout**: Дирижер обязан трактовать отсутствие `ACK` в заданное окно как transport/protocol timeout и применять retry/fault policy.

### 2.3. Жизненный цикл транзакции
Любая команда от Дирижера должна пройти цикл:
1.  **ACK (0x01)**: Немедленное подтверждение приема (отправляет Диспетчер).
2.  **DATA (0x03, Sub:0x02)**: (Опционально) Передача промежуточных результатов.
3.  **DONE (0x03, Sub:0x01)**: Финальное подтверждение успешного выполнения атомарного действия.
4.  **NACK (0x02)**: Ошибка выполнения с кодом из единого реестра.

`NACK` отправляется только для транспортно валидных `COMMAND`-кадров (`Extended ID`, наш/broadcast `DstAddr`, `Message Type=COMMAND`, `DLC=8`), которые не прошли прикладную валидацию.

Для любого многокадрового ответа порядок обязателен: `ACK -> DATA... -> DONE`. После `DONE` в рамках этой транзакции не должно приходить DATA-кадров. Дирижер имеет право считать `DONE` концом ответа.

`DONE` имеет единый принцип на всех внешних интерфейсах DDS-240: команда успешно завершена по своему контракту. Уровень интерфейса обязан быть явным:

*   `Host DONE` отправляет Дирижер только после завершения всего Host recipe/job.
*   `Executor DONE` отправляет исполнитель Дирижеру только для одной low-level atomic-команды.
*   Внутренние события прошивки не называются `DONE`; для них используются термины `COMPLETE`, `EXPIRED`, `STOPPED`, `FAULT`.
*   Разные устройства не меняют смысл `DONE`; различаются только постусловия конкретных команд.

Обязательные правила:

*   Спецификация каждой команды обязана явно задавать постусловие `DONE`.
*   Для finite-команд (`MOTOR_ROTATE`, `MOTOR_HOME`, `PUMP_RUN_DURATION`, будущие `MIX_RUN_MS`, `SCAN_SINGLE`) `DONE` отправляется только после фактического завершения операции и перевода ресурса в конечное безопасное/штатное состояние.
*   Для state-команд (`PUMP_START`, `PUMP_STOP`, `VALVE_OPEN`, `VALVE_CLOSE`, `MOTOR_START_CONTINUOUS`, `MOTOR_STOP`) `DONE` отправляется после достижения требуемого состояния.
*   Протокольный `DONE` должен отправляться из task context после обновления состояния ресурса. ISR/callback может только остановить критический выход и поставить внутреннее completion-событие.
*   Аварийное отключение, watchdog recovery, fault-handler safe-state и защитный auto-off не являются штатным `DONE` для текущей команды. Если связь и состояние Executor позволяют, такой сценарий должен фиксироваться через fault/status/recovery, иначе Дирижер обнаруживает его по operation timeout.
*   Рецепты Дирижера должны по возможности строиться из finite-команд. Команды удержания состояния используются для ручного режима, сервиса, диагностики и случаев, где состояние действительно должно удерживаться до отдельной команды.

### 2.3.1. Обязанности Дирижера при промышленной эксплуатации
Дирижер является владельцем сценария и обязан отличать транспортные ошибки от прикладных отказов:

*   **ACK timeout**: отсутствие `ACK` не является `NACK`; это transport/protocol timeout с отдельной retry/fault policy.
*   **Operation timeout**: для каждой исполнительной команды Дирижер задает верхний предел ожидания `DONE`. Safety timeout исполнителя не заменяет сценарный timeout Дирижера.
*   **ACK without DONE**: если `ACK` получен, но `DONE` не пришел в operation timeout, Дирижер считает команду невыполненной или неизвестно завершенной. Возможные причины: зависание доменной задачи, отказ RTOS-ресурса, IWDG recovery. Дирижер не продолжает recipe step как успешный, выполняет recovery/discovery и сверяет состояние узла.
*   **Finite actuator ownership**: длительность физической операции должен вести ближайший к железу Executor, если операция измеряется локально доступной величиной. Для насоса базовая величина - время работы, поэтому рецептные дозирующие шаги должны использовать `PUMP_RUN_DURATION(duration_ms)`, а не `PUMP_START + WAIT_MS + PUMP_STOP`.
*   **Volume translation**: если Host задает объем жидкости, Дирижер переводит объем в `duration_ms` по калибровочной модели и передает Executor уже физический параметр времени. Executor включает насос, сам выдерживает это время, выключает насос и только потом отправляет `DONE`.
*   **Recipe pause ownership**: `WAIT_MS` на стороне Дирижера используется для технологических пауз между операциями, а не как основной способ дозирования насосом.
*   **Motion speed ownership**: Host-команды верхнего уровня задают технологическую цель, а не скорость двигателя. `speed` является контрактом recipe-level atomic action: Дирижер выбирает его для `MOTOR_ROTATE/HOME/START_CONTINUOUS` из recipe/action config или технологического профиля и передает Executor в low-level payload.
*   **Motion speed validation**: Motion Executor обязан валидировать command `speed` против локального профиля оси (`min_speed`, `max_safe_speed`, timer/driver limits). Небезопасная или невалидная скорость не должна запускать движение; базовая политика - `NACK INVALID_PARAM`.
*   **Motion acceleration ownership**: ускорение не является частью текущего `MOTOR_ROTATE 0x0101` payload. Acceleration является локальным motion-profile параметром Executor и реализуется в motion planner/driver как профиль разгона/торможения конкретной оси.
*   **Motion finite validation**: для ненулевого `MOTOR_ROTATE.steps` значение `speed=0` должно отклоняться как invalid parameter. Для `steps=0` допустим быстрый successful completion без запуска STEP.
*   **Motion timer-group resource lock**: если Motion Executor использует общий timer base для нескольких каналов, resource lock действует на всю timer group, а не только на `motor_id`. Для текущего Motion STM32F103 industrial baseline: `TIM1 group = motors 0..3`, `TIM2 group = motors 4..7`, максимум один active motion profile на группу.
*   **Resource lock**: пока ресурс занят шагом рецепта, Дирижер запрещает конфликтующие команды на тот же канал или shared hardware group. Разрешены только диагностические запросы, штатный stop/close и аварийная recovery-политика.
*   **Service recovery**: после `COMMIT`, `REBOOT`, `FACTORY_RESET` и смены `NodeID` Дирижер выполняет повторный discovery и сверку `NodeID`, `device_type`, `UID`.
*   **Timeout encoding**: длительности на executor-уровне передаются как `uint32_t` Little-Endian в payload bytes 3-6 при `DLC=8`.
*   **Host boundary**: динамический DLC допустим только на уровне `Host -> Conductor`; на уровне `Conductor <-> Executor` Дирижер обязан формировать strict `DLC=8`.

### 2.3.2. Host Command Translation Boundary

Host-команды верхнего уровня являются стабильным внешним API DDS-240. Дирижер обязан парсить их строго по документации `Commands_API/User_Commands` и не менять порядок, типы или смысл параметров ради удобства executor-уровня.

Host payload не является low-level payload. Между Host-командой и CAN-командой исполнителю всегда есть слой технологического перевода:

1. `Host parser` разбирает команду верхнего уровня в канонические поля API.
2. `Recipe selector` выбирает сценарий и технологические шаги.
3. `Parameter translator / calibrator` переводит пользовательские величины в физические параметры исполнителей.
4. `Device mapping` выбирает плату, NodeID, физический канал и shared-resource group.
5. `Low-level packer` формирует strict `DLC=8` atomic-команды `Conductor -> Executor`.
6. `Job manager` ожидает `ACK/DONE` по каждой atomic-команде и отправляет Host `DONE` только после завершения всего Host job/recipe.

Примеры перевода:

| Host-величина | Внутренний перевод Дирижера | Executor-команда |
|:--------------|:----------------------------|:-----------------|
| `volume_ul` для насоса | `duration_ms = calibrator(pump_id, volume_ul, operation_type)` | `PUMP_RUN_DURATION(pump_id, duration_ms)` |
| `cuvette` / `slot` | позиция, `steps`, профиль скорости | `MOTOR_ROTATE/MOTOR_HOME/...` |
| технологическая пауза | локальный recipe timer Дирижера | `WAIT_MS` без команды исполнителю |

Если калибровка неизвестна, просрочена или возвращает недопустимое значение, Дирижер не должен запускать исполнитель по default-параметру. Host-команда завершается ошибкой уровня Host/job, а low-level команда на исполнитель не отправляется.

Для Fluidics это правило означает: Host-команда с объемом жидкости не превращается в `PUMP_START + WAIT_MS + PUMP_STOP`. Дирижер переводит объем в `duration_ms`, отправляет `PUMP_RUN_DURATION`, а Fluidics владеет локальным временем, выключением насоса и low-level `DONE`.

### 2.4. Приемочный CAN-набор для каждого Executor
Каждая плата-исполнитель считается совместимой с DDS-240 только после прохождения общего набора CAN-тестов:

*   **Discovery**: `0xF001 GET_DEVICE_INFO` возвращает `ACK -> DATA... -> DONE`.
*   **UID Readout**: `0xF004 GET_UID` возвращает полный UID исполнителя в DATA-кадрах и завершает транзакцию `DONE`.
*   **Multi-frame Ordering**: `0xF001` и `0xF007` возвращают все DATA-кадры до DONE; после DONE нет DATA той же транзакции.
*   **Unknown Command**: неизвестный `cmd_code` возвращает `NACK`.
*   **Invalid Channel**: индекс канала вне диапазона возвращает `NACK`.
*   **Invalid Resource Type**: команда не своего типа ресурса возвращает `NACK` (например, команда насоса на индекс клапана).
*   **Invalid Magic Key**: сервисная команда с неверным ключом возвращает `NACK`.
*   **Broadcast**: команда с `DstAddr = 0x00` принимается, если команда разрешена для broadcast.
*   **Foreign Destination**: команда с чужим `DstAddr` игнорируется без ACK/NACK.
*   **Strict Frame Format**: все executor-кадры используют 29-bit Extended ID и `DLC=8`; кадры с `DLC != 8` игнорируются без ACK/NACK и обрабатываются Дирижером по ACK timeout.
*   **RTOS Startup Integrity**: все критические задачи, очереди и таймеры создаются успешно; при отказе Executor уходит в safe state/Error_Handler, а не принимает команды в частично рабочем состоянии.

Для поэтапного доведения платы используется следующая градация:

*   **Level A / базовый CAN smoke-test**: `GET_DEVICE_INFO`, `GET_UID`, strict `DLC=8`, foreign destination, ACK/DATA/DONE ordering и базовая сборка прошивки. Этот уровень разрешает переход к физическим тестам без нагрузки.
*   **Level B / диагностический CAN уровень**: `GET_STATUS (0xF007)`, NACK matrix, broadcast policy, счетчики transport drops, RX/TX/application queue overflow и CAN fault callbacks. Этот уровень нужен для объяснимой диагностики отказов на стенде.
*   **Level C / промышленный уровень**: safe-state hook, watchdog/recovery, fault-handler safe state, operation timeout policy и доменные тесты с физическими нагрузками.

### 2.5. Safety / Failsafe Policy
Этот раздел фиксирует утвержденную общую safety-концепцию DDS-240. Она является baseline для всех Executor-плат и основана на уже проверенном Fluidics pattern.

Safety-логика делится на два уровня:

*   **Baseline / обязательный уровень** - должен быть реализован перед watchdog и физическими нагрузками.
*   **Hardening / усиление по возможности** - допустимые улучшения реализации, которые не меняют архитектурный контракт и не являются блокером, если baseline уже прошел приемку.

Каждый Executor обязан иметь явно описанное безопасное состояние:

*   При старте и после reset все исполнительные выходы переводятся в safe state.
*   При потере связи исполнитель не должен оставаться в опасном активном состоянии без ограничения по времени.
*   Длительные действия должны иметь timeout или иной аппаратно-программный предел.
*   Если safety timer или иной механизм ограничения не может быть запущен, исполнитель обязан оставить/вернуть канал в safe state и завершить команду ошибкой (`NACK` без `DONE`).
*   Автоотключение по safety timeout должно быть задокументировано: отправляется диагностическое событие или состояние фиксируется для последующего запроса.
*   Независимый watchdog (`IWDG`) обязателен для STM32 Executor-плат. Кормление watchdog выполняет только supervisor-задача после проверки heartbeat всех критических задач; отдельным доменным задачам запрещено напрямую вызывать refresh watchdog.
*   Safe state определяется доменом платы: Fluidics - насосы/клапаны OFF, Motion - остановка движения, Thermo - отсутствие опасного управляющего воздействия.

### 2.5.1. Mandatory Safe-State Hook
Каждая STM32 Executor-плата обязана иметь доменный safe-state hook: одну функцию или минимальный набор функций, которые переводят все исполнительные выходы платы в безопасное состояние.

Требования к safe-state hook:

*   Вызывается после инициализации GPIO, до приема исполнительных команд.
*   Вызывается из `Error_Handler()`.
*   Вызывается из fault handlers до входа в бесконечный аварийный цикл: `NMI_Handler`, `HardFault_Handler`, `MemManage_Handler`, `BusFault_Handler`, `UsageFault_Handler`.
*   Не использует RTOS-очереди, mutex, динамическую память, CAN-отправку или длительные задержки.
*   Не должен зависеть от работоспособности планировщика FreeRTOS.
*   Допускает прямую работу с GPIO/HAL или регистрами, необходимую для безопасного выключения выходов.

Baseline допускает HAL-вызовы для GPIO, если они не используют RTOS/CAN/dynamic memory и фактически подтверждены на стенде. Это соответствует проверенному Fluidics pattern (`PumpsValves_AllOff()`): выходы переводятся в safe state при старте, в `Error_Handler()`, fault handlers и watchdog recovery.

Доменная трактовка:

*   **Fluidics**: все насосы и клапаны переводятся в `OFF`.
*   **Motion**: прекращаются STEP-импульсы, драйверы переводятся в disable/safe stop, если это поддержано железом.
*   **Thermo**: нагреватели, ШИМ и силовые выходы переводятся в `OFF`; датчики могут оставаться пассивными.

Если safe-state hook был вызван из fault handler, Executor не обязан отправлять `NACK/DONE`: связь может быть недоступна, а приоритетом является физическая безопасность. Дирижер обязан обнаружить такую ситуацию по timeout связи и выполнить recovery/discovery после восстановления узла.

#### 2.5.1.1. Optional Safe-State Hardening

Следующие меры являются рекомендациями по усилению, а не обязательным baseline:

*   Использовать прямую запись в GPIO-регистры (`BSRR/BRR`) вместо HAL для критического fault path.
*   Явно отключать timer outputs через регистры (`CCER`, `CR1`, `BDTR`, `DIER`, pending flags), если плата использует PWM/STEP.
*   Повторять safe-state после инициализации периферии, которая могла изменить состояние выходов или alternate-function конфигурацию.
*   Минимизировать количество кода в fault path и избегать любых вызовов, поведение которых зависит от внешнего состояния, блокировок или callback-ов.

Hardening выполняется после baseline-приемки или вместе с ней, если это не увеличивает риск и не ломает уже проверенный pattern. Для уже проверенной платы не требуется пересматривать принятый HAL-level safe-state без отдельного решения.

### 2.6. CAN Fault Handling
Каждый Executor должен иметь единый минимум диагностики CAN:

*   Обработка `bus-off` и восстановление после перезапуска CAN-контроллера.
*   Счетчики CAN-ошибок: bus errors, error warning, error passive, bus-off, restart count.
*   Диагностическая service-команда или DATA/LOG-ответ для чтения состояния транспорта.
*   Зафиксированное поведение при переполнении RX/TX очередей.

### 2.7. Watchdog
Для промышленной готовности STM32 Executor-платы должны использовать аппаратный IWDG:

*   IWDG обслуживается только при штатной работе основных задач.
*   После watchdog reset GPIO переводятся в safe state до приема новых команд.
*   Причина последнего reset должна быть доступна через сервисный статус или диагностический ответ.
*   Supervisor-задача является единственным местом вызова refresh IWDG. Доменные задачи только публикуют heartbeat/progress marker.
*   Минимальная приемка IWDG: длительный idle без ложного reset; затем fault-injection зависания критической задачи с подтверждением safe state до reset и восстановлением связи после reset.
*   Минимальная приемка fault handlers: управляемый вход в fault handler после включения тестовой нагрузки должен немедленно перевести доменные выходы в safe state, не отправлять `DONE` и завершиться аппаратным reset/recovery.
*   Дирижер должен быть готов к сценарию `ACK` без `DONE`: Executor мог уйти в watchdog recovery после безопасного отключения выходов.

---

## 3. Сервисный слой и Идентификация (0xFxxx)
Каждое устройство обязано поддерживать диапазон команд `0xF001 - 0xF00F` для обслуживания.

### 3.1. Обязательные сервисные команды
*   **`0xF001` (Get Info)**: Возврат типа устройства, версии прошивки и UID чипа.
*   **`0xF002` (Reboot)**: Перезагрузка по Magic Key `0x55AA`.
*   **`0xF003` (Commit)**: Сохранение текущих настроек из RAM во Flash.
*   **`0xF004` (Get UID)**: Возврат полного UID чипа в DATA-кадрах.
*   **`0xF005` (Set NodeID)**: Изменение сетевого адреса устройства.
*   **`0xF006` (Factory Reset)**: Очистка Flash-конфигурации по Magic Key `0xDEAD`.
*   **`0xF007` (Get Status)**: Возврат диагностического статуса исполнителя, включая CAN fault counters и счетчики переполнения очередей.

### 3.2. Единый формат `GET_DEVICE_INFO`
Ответ `0xF001` должен начинаться с ACK, затем передавать один или несколько DATA-кадров и завершаться DONE.

Первый DATA-кадр обязан иметь следующий формат:

| Byte | Поле | Описание |
|:-----|:-----|:---------|
| 0 | `0x02` | Sub-Type DATA |
| 1 | `0x80` | EOT=1 для одиночного фрагмента или sequence info для многофрагментного ответа |
| 2 | `device_type` | Тип устройства из раздела 3.4 |
| 3 | `fw_major` | Major версия прошивки |
| 4 | `fw_minor` | Minor версия прошивки |
| 5 | `channel_count` | Количество каналов/ресурсов платы |
| 6-7 | `uid0..uid1` | Начало MCU UID или `0x00` |

Если UID не помещается в первый DATA-кадр, исполнитель передает дополнительные DATA-кадры перед DONE.

### 3.2.1. Единый формат `GET_STATUS (0xF007)`
Ответ `0xF007` должен начинаться с ACK, затем передавать набор DATA-кадров и завершаться DONE.

Каждый DATA-кадр статуса содержит одну метрику:

| Byte | Поле | Описание |
|:-----|:-----|:---------|
| 0 | `0x02` | Sub-Type DATA |
| 1 | `0x80` | EOT=1 для текущей метрики |
| 2-3 | `metric_id` | Идентификатор метрики, `uint16_t LE` |
| 4-7 | `value` | Значение метрики, `uint32_t LE` |

Обязательные CAN diagnostic metrics для STM32 Executor:

| Metric ID | Метрика |
|:----------|:--------|
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

Дирижер обязан читать `0xF007` при вводе узла в работу, после recovery/discovery и при подозрении на деградацию шины. Формат метрик общий для Fluidics, Motion и Thermo; доменные платы могут добавлять собственные metric_id выше `0x1000`.

### 3.2.2. Правила контроля `GET_STATUS` на стороне Дирижера
Счетчики `GET_STATUS` являются монотонными диагностическими счетчиками. Исполнитель только считает события и отдает снимок. Решение о деградации, retry, recovery или остановке сценария принимает Дирижер.

Дирижер хранит предыдущий успешный снимок для каждого Executor-узла и после каждого нового успешного `GET_STATUS` считает дельты:

```text
delta = current_value - previous_value
```

Для `uint32_t` счетчиков расчет дельты должен выполняться с учетом естественного переполнения `uint32_t`. Первый успешный `GET_STATUS` после старта, discovery или recovery считается baseline: Дирижер запоминает его без поднятия fault по старым значениям.

Рекомендуемые моменты опроса:

*   после `GET_DEVICE_INFO` при вводе узла в работу;
*   периодически в фоне, например раз в 1-5 секунд;
*   сразу после `ACK timeout`, `DONE timeout` или подозрительного `NACK`;
*   после `COMMIT`, `REBOOT`, `FACTORY_RESET`, смены NodeID и повторного discovery;
*   перед запуском критического сценария, если требуется чистый health-baseline.

`GET_STATUS` не заменяет timeout текущей команды. Он нужен для диагностики причины после события. Например, если команда получила `ACK timeout`, а следующий `GET_STATUS` отвечает и показывает `delta dropped_wrong_dlc > 0`, значит исполнитель жив, но команда была отброшена как невалидная транспортная оболочка.

### 3.2.3. Decision Matrix для Дирижера
Дирижер должен трактовать дельты метрик следующим образом:

| Метрика / группа | Типовая причина | Решение Дирижера |
|:-----------------|:----------------|:-----------------|
| `dropped_wrong_dlc > 0` | Ошибка packer/protocol boundary, кадр не `DLC=8` | Не ретраить тот же кадр. Зафиксировать software/protocol fault, проверить формирование Conductor -> Executor кадра. |
| `dropped_not_ext > 0` | Отправлен Standard ID или на шине чужой трафик | Проверить CAN packer и фильтрацию. Для собственных команд Дирижера - software fault. |
| `dropped_wrong_dst > 0` | Неверный NodeID, устаревший адрес после `SET_NODE_ID`, чужой трафик | Выполнить discovery, сверить NodeID/UID, обновить маршрутизацию. |
| `dropped_wrong_type > 0` | На Executor попали не `COMMAND` кадры | Проверить маршрутизацию, bridge/switch, packer message type. |
| `rx_queue_overflow > 0` | Transport task не успевает принять поток кадров | Снизить частоту команд, проверить нагрузку шины и приоритеты задач. |
| `dispatcher_queue_overflow > 0` | Dispatcher перегружен валидными командами | Снизить частоту команд к узлу, увеличить очередь/производительность, проверить сценарий. |
| `app_queue_overflow > 0` | Доменная задача не успевает принимать команды | Снизить частоту доменных команд, проверить выполнение длительных операций и размер очереди. |
| `tx_queue_overflow > 0` | Узел генерирует ответы быстрее, чем transport task передает | Проверить частоту запросов, DATA-ответы и состояние CAN TX. |
| `tx_mailbox_timeout > 0` | bxCAN mailbox долго не освобождается | Проверить ACK на шине, физику, терминаторы, загрузку шины, bus state. |
| `tx_hal_error > 0` | HAL отказал при постановке кадра в CAN TX | Читать `last_hal_error`/`last_esr`, выполнить recovery policy для узла. |
| `error_warning/passive > 0` | Деградация физического уровня CAN | Пометить узел/шину как degraded, проверить кабель, питание, скорость, терминаторы. |
| `bus_off_count > 0` | Тяжелая CAN-авария | Вывести узел из активного сценария, выполнить recovery/discovery после восстановления. |

Транспортные drops (`wrong_dlc`, `not_ext`, `wrong_dst`, `wrong_type`) не должны приводить к `NACK`: кадр не дошел до прикладной логики. Для текущей команды Дирижер увидит `ACK timeout`, а `GET_STATUS` используется для уточнения причины.

### 3.3. Адреса узлов (Node Address)
*   `0x10`: Conductor (Дирижер)
*   `0x20`: Motion Controller (Моторы)
*   `0x30`: Pump/Valve Controller (Помпы/Клапаны)
*   `0x40`: Thermo Controller (Термодатчики)

### 3.4. Типы устройств в `GET_DEVICE_INFO`
Эти значения передаются в payload ответа `0xF001` и не являются CAN NodeID.
*   `0x01`: Motion Controller.
*   `0x02`: Thermo Controller.
*   `0x03`: Fluidic Controller.

### 3.5. Перспективная систематизация общих Executor-слоев
После стабилизации CAN transport и service-команд на нескольких Executor-платах экосистема должна перейти от копирования одинакового кода к общим библиотекам.

Этот раздел является перспективным направлением индустриализации, а не обязательным условием текущего доведения плат до промышленного стандарта. Motion, Fluidics, Thermo и новые Executor-платы могут закрывать текущий industrial checklist с локальной реализацией, если она соответствует этому стандарту, проходит сборку и физические CAN-тесты. При этом новый код нужно писать с такими границами, чтобы общий transport/service слой можно было вынести позже без изменения CAN-контракта.

Перспективный единый каркас Executor-платы:

```text
RTOS tasks:
- task_can_handler
- task_dispatcher
- task_<domain>_controller
- task_watchdog

Common/local modules:
- app_config
- app_queues
- can_protocol / can_transport
- app_watchdog
- app_flash
- device_mapping
- diagnostics / status
- safe_state
- <domain>_driver
```

Единым должен быть каркас: CAN transport, dispatcher, service-команды, watchdog-supervisor, diagnostics/status, Flash/config policy, device mapping и safe-state policy. Различаться должны только доменный контроллер, драйверы железа, конкретная карта устройств и физическая реализация safe-state.

Примеры доменной подстановки:

| Executor | Доменная задача | Драйверы/модули домена |
|----------|-----------------|------------------------|
| Motion | `task_motion_controller` | `motion_driver`, `tmc2209_driver`, motion mapping |
| Fluidics | `task_pump_controller` | `pumps_valves_gpio`, pump/valve mapping, finite timers |
| Thermo | `task_temp_monitor` | `ds18b20`, temperature mapping |
| Sensors | `task_sensor_controller` | `sensor_driver`, position sensor mapping |

Дополнительные задачи допускаются только при физической необходимости домена. Пример: отдельный manager для TMC2209 у Motion или будущий ADC/DMA service mode у Sensors. Такие задачи не меняют общий executor-каркас и должны быть явно обоснованы в ТЗ конкретной платы.

Перспективный набор common-модулей:

*   `dds240_can_codec` - чистый HAL-free слой разборки/сборки 29-bit CAN ID и payload ответов.
*   `dds240_can_transport` - общая transport policy для Executor: strict `DLC=8`, Extended ID, `COMMAND`, destination/broadcast, transport drops, TX ordering, mailbox guard.
*   `dds240_executor_service` - общий обработчик service-команд `0xF001..0xF007`.
*   `dds240_diagnostics` - common counters, snapshot API и packing `GET_STATUS`.
*   `dds240_board_port` - обязательный локальный адаптер конкретной платы к HAL, FreeRTOS, Flash, reset и safe-state.

#### 3.5.1. Что можно выносить в common modules

Общим для всех STM32 Executor-плат считается:

*   `CAN_BUILD_ID`, `CAN_GET_PRIORITY`, `CAN_GET_MSG_TYPE`, `CAN_GET_DST`, `CAN_GET_SRC` и их DDS-240-эквиваленты;
*   константы message type, response subtype, service-команд `0xF001..0xF007`, Magic Keys, common NACK registry и `GET_STATUS` metric_id;
*   упаковка `ACK`, `NACK`, `DATA`, `DONE` с `DLC=8` и zero padding;
*   валидация transport envelope: Extended ID, `COMMAND`, destination/broadcast, strict `DLC=8`;
*   правило "нет `NACK` на невалидную транспортную оболочку";
*   общий обработчик `GET_DEVICE_INFO`, `GET_UID`, `REBOOT`, `COMMIT`, `SET_NODE_ID`, `FACTORY_RESET`, `GET_STATUS`;
*   структура и snapshot common diagnostics counters;
*   учет transport drops, RX/TX/dispatcher/application queue overflow, TX mailbox timeout, HAL CAN error и CAN error callback/status counters;
*   единая упаковка `GET_STATUS` metrics `0x0001..0x0012`.

#### 3.5.2. Что должно оставаться в board port/adaptor

Common modules не должны владеть доменной логикой и аппаратной конкретикой платы. В них запрещено переносить:

*   управление моторами, насосами, клапанами, термоканалами и другими нагрузками;
*   конкретный `CAN_HandleTypeDef hcan`, имена FreeRTOS очередей, задач, mutex и thread flags;
*   CubeMX-generated `MX_CAN_Init()`, GPIO remap, CAN RX/TX пины и NVIC priority конкретного проекта;
*   имена IRQ handlers из startup/CubeMX, например `USB_LP_CAN1_RX0_IRQHandler()` и `CAN1_SCE_IRQHandler()`;
*   адрес Flash page, layout конфигурации и способ записи Flash без port/adaptor слоя;
*   `Error_Handler()`, watchdog policy, reset policy и safe-state конкретной платы;
*   доменные команды `0x010x`, `0x020x`, `0x901x` и будущие команды конкретных исполнителей.

#### 3.5.3. CAN configuration и filters

Стандарт задает reference CAN profile и transport policy, но не требует переносить CubeMX/HAL init в common library.

Для STM32F103 bxCAN reference profile:

```text
APB1 = 32 MHz
bitrate = 1 Mbit/s
Prescaler = 2
BS1 = 11TQ
BS2 = 4TQ
SJW = 1TQ
sample point = 75%
TransmitFifoPriority = ENABLE
AutoRetransmission = ENABLE
```

Локально в проекте остаются:

*   выбор пинов CAN RX/TX и remap;
*   CubeMX-generated `MX_CAN_Init()`;
*   `hcan` handle;
*   NVIC priorities;
*   конкретная hardware filter configuration.

Общая policy фильтрации: Executor принимает кадры для своего `DstAddr` и `CAN_ADDR_BROADCAST`, а невалидную transport envelope игнорирует без ответа. Реализация может быть аппаратной, программной или смешанной.

Если Standard ID отсекается аппаратным bxCAN-фильтром до firmware, счетчик `dropped_not_ext` может не расти. Это допустимый вариант реализации, если поведение зафиксировано в отчете: ответа нет, Дирижер видит `ACK timeout`, а `GET_STATUS` не обязан показывать `DROP_NOT_EXT` для кадра, который не дошел до software.

#### 3.5.4. IRQ и HAL callbacks

IRQ handlers остаются локальным glue-кодом конкретного проекта, потому что их имена и подключение зависят от startup/CubeMX. Перспективная common-библиотека может предоставлять только callback API, который вызывается из локальных HAL callbacks.

Пример допустимой границы:

```c
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
    DDS240_CAN_OnRxPending(hcan);
}

void HAL_CAN_ErrorCallback(CAN_HandleTypeDef *hcan)
{
    DDS240_CAN_OnError(HAL_CAN_GetError(hcan), hcan->Instance->ESR);
}
```

Common transport layer может реализовать `DDS240_CAN_OnRxPending()`, `DDS240_CAN_OnError()` и счетчики, но локальный board port отвечает за очередь RX, task wakeup, `hcan`, IRQ enable/disable и связь с FreeRTOS objects.

#### 3.5.5. Service layer через board port

Правильная граница service-библиотеки - common core + board port/adaptor:

```c
typedef struct {
    uint8_t device_type;
    uint8_t default_node_id;
    uint8_t channel_count;
    uint8_t fw_major;
    uint8_t fw_minor;
} Dds240ExecutorInfo_t;

typedef struct {
    void (*get_uid)(uint8_t uid[12]);
    uint8_t (*get_node_id)(void);
    void (*set_node_id_ram)(uint8_t node_id);
    bool (*commit_config)(void);
    void (*factory_reset)(void);
    void (*request_reboot)(void);
    void (*get_status)(Dds240StatusSnapshot_t *snapshot);
    bool (*send_frame)(const Dds240TxFrame_t *frame);
} Dds240ExecutorPort_t;
```

Диспетчер конкретной платы должен сначала дать common service layer шанс обработать `0xFxxx`, а затем передавать нераспознанные команды в доменный обработчик:

```c
if (DDS240_Service_TryHandle(&cmd, &executor_info, &executor_port)) {
    return;
}

/* Далее только доменные команды конкретной платы. */
Motion_DispatchCommand(&cmd);
```

Текущая задача промышленного доведения плат - не создать эти библиотеки немедленно, а обеспечить одинаковое поведение и чистые границы. Вынос в common modules выполняется отдельным этапом после того, как поведение подтверждено минимум на двух типах Executor-плат и отражено в отчетах.

---

## 4. Энергонезависимая память (Flash)

### 4.1. Размещение
Использование **последней страницы** Flash-памяти МК (например, Page 63 для STM32F103C8).

Последняя страница Flash, используемая под конфигурацию, должна быть исключена из области приложения в linker script или зарезервирована отдельной секцией. `COMMIT` не должен иметь возможности стереть исполняемый код.

Для STM32F103C8 с 64K Flash и конфигурацией на `0x0800FC00` область приложения в linker script должна заканчиваться до последней страницы, например `FLASH LENGTH = 63K`. Аналогичное правило применяется ко всем Executor-платам: адрес конфигурации, erase page/sector и linker memory map должны быть согласованы в документации и в проекте.

### 4.2. Целостность данных
*   **Magic Key**: Проверка `0x55AAEEFF` в начале структуры.
*   **CRC16**: Обязательный расчет контрольной суммы всей структуры (Poly 0xA001). При несовпадении CRC устройство загружает «Заводские настройки» в RAM, но не перезаписывает Flash автоматически.
*   **Атомарность**: Стирание страницы перед записью новых данных.

---

## 5. Стандарты кодирования (Coding Style)

1.  **Запрет "Магических чисел"**: Все смещения, биты, команды и ошибки должны быть определены в `can_protocol.h` или `app_config.h`.
2.  **Префиксы функций**:
    *   `CAN_xxx`: Функции сетевого уровня.
    *   `AppConfig_xxx`: Функции работы с конфигурацией.
    *   `MotionDriver_xxx`: Функции работы с железом.
3.  **Безопасность**: Каждое обращение к периферии через HAL должно проверять возвращаемый статус (`HAL_OK`).

---

## 6. Регрессионная сверка протокола
Перед выпуском прошивки или обновлением документации необходимо сверять:

*   `can_protocol_*.h` каждой платы с `CONDUCTOR_INTEGRATION_GUIDE.md`.
*   NodeID, `device_type`, command codes, NACK-коды и Magic Keys.
*   Форматы ACK/NACK/DONE/DATA с фактическими ответами прошивки.
*   SocketCAN-примеры с реальными кадрами на шине.
*   Границу уровней: `Host -> Conductor` может использовать динамический DLC, `Conductor <-> Executor` обязан использовать strict `DLC=8`.

---

## Чек-лист соответствия (Compliance Checklist)
- [ ] Используется 29-битный ID?
- [ ] Принимается ли Broadcast (ID 0x00)?
- [ ] Отправляется ли ACK немедленно?
- [ ] Возвращается ли DONE после штатного завершения?
- [ ] Возвращается ли NACK на ошибочные команды?
- [ ] Все ли executor-кадры имеют DLC=8?
- [ ] Защищен ли доступ к конфигу через Mutex?
- [ ] Есть ли CRC16 во Flash?
- [ ] Поддерживается ли команда 0xF001 (Info)?
- [ ] Реализован ли единый формат `GET_DEVICE_INFO`?
- [ ] Определено ли safe state?
- [ ] Реализован ли доменный safe-state hook?
- [ ] Вызывается ли safe-state hook из `Error_Handler()` и fault handlers?
- [ ] Не зависит ли safe-state hook от FreeRTOS/CAN/динамической памяти?
- [ ] Реализован ли watchdog?
- [ ] Проверен ли watchdog в idle и при fault-injection зависания критической задачи?
- [ ] Есть ли диагностика CAN fault state?
- [ ] Исключена ли Flash config page из области приложения в linker script?
- [ ] Совпадает ли адрес config page в коде, linker script и документации?
- [ ] Синхронизированы ли общие константы с `dds240_global_config.h`?
- [ ] Отделены ли common transport/service/diagnostics границы от доменной логики так, чтобы в будущем вынести их в общие Executor-библиотеки?
