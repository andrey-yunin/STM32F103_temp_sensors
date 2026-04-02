# Техническое задание: Analyzer Service Tool (DDS-240 CLI)

## 1. Общие сведения
Настоящее ТЗ описывает требования к разработке консольного (CLI) программного обеспечения для настройки, калибровки и диагностики узлов биохимического анализатора, работающих по протоколу DDS-240 (CAN).

## 2. Архитектура программного обеспечения
Система строится на базе языка **Python 3.10+** с использованием объектно-ориентированного подхода (ООП) и многоуровневой архитектуры.

### 2.1. Уровни абстракции (Layered Design)
1.  **Transport Layer (CAN HAL):** Абстракция над физическим адаптером (библиотека `python-can`). Поддержка PCAN, SLCAN, BUSMASTER.
2.  **Protocol Layer (DDS-240 Engine):** Управление транзакциями (REQ-ACK-DATA-DONE). Реализация тайм-аутов, повторных попыток (Retry) и сборки многопакетных данных (UID).
3.  **Device Model Layer:** Иерархия классов устройств.
    *   `BaseExecutor`: Базовая логика 0xF0xx (Info, Reboot, Flash).
    *   `ThermoExecutor`: Специфика 0xF1xx (1-Wire Scan, Mapping).
    *   `MotionExecutor`: Специфика 0xF2xx (будущие модули).
4.  **UI/Shell Layer:** Интерактивная оболочка (библиотека `prompt_toolkit`).

## 3. Модель управления (Cisco-like CLI)
ПО должно имитировать работу Cisco IOS для обеспечения привычной иерархической навигации.

### 3.1. Режимы доступа (Modes)
*   **User EXEC Mode (`Analyzer>`):** Базовый мониторинг (чтение температур, статусов).
*   **Privileged EXEC Mode (`Analyzer#`):** Доступ к сервисным командам (сканирование, тесты). Вход по команде `enable`.
*   **Global Configuration Mode (`Analyzer(config)#`):** Режим изменения параметров. Вход по `configure terminal`.
*   **Context Modes:** Вход в контекст конкретной платы или ресурса:
    *   `Analyzer(config-board 0x40)#`
    *   `Analyzer(config-sensor 1)#`

### 3.2. Основной набор команд
| Команда | Режим | Описание |
| :--- | :--- | :--- |
| `show inventory` | User | Список всех обнаруженных плат (Type ID, UID). |
| `show temp` | User | Таблица температур (в контексте платы). |
| `scan nodes` | Privileged | Поиск новых устройств на шине (0x02-0x7F). |
| `cd <id>` | Privileged | Переход в контекст платы. |
| `scan 1-wire` | Config-Board | Запуск физического поиска датчиков DS18B20. |
| `map <logical> <rom>` | Config-Board | Привязка ROM ID к логическому каналу. |
| `write memory` | Config-Board | Сохранение настроек во Flash (FLASH_COMMIT). |
| `reload` | Privileged | Перезагрузка платы (SRV_REBOOT). |

## 4. Требования к транзакционному менеджеру
ПО обязано гарантировать надежность связи через механизмы:
*   **Handshake:** Каждая команда считается отправленной только после получения `ACK`.
*   **Timeout:** Дефолтный тайм-аут ожидания ответа — 200 мс.
*   **Serialization:** Автоматическая упаковка аргументов команд в Little-Endian и распаковка ответов в физические величины (float, int).

## 5. Интерактивные функции (UX)
*   **Context Help (`?`):** Вывод списка доступных команд для текущего уровня.
*   **Tab-Completion:** Автодополнение имен команд, адресов плат и физических ROM ID.
*   **Live Update (Warm Finger):** Режим `monitor temperatures`, в котором таблица обновляется в реальном времени с цветовой индикацией изменений.

## 6. Масштабируемость и плагинная архитектура (Plugin System)
ПО проектируется как универсальная оболочка для всей экосистемы анализатора. 
*   **Принцип "Plug-and-Play":** Приложение не имеет жестко прописанных зависимостей от типов плат. 
*   **Dynamic Loading:** При обнаружении нового узла на шине `DDS240Engine` запрашивает его `Type ID`. `DeviceFactory` автоматически загружает соответствующий модуль из папки `executors/`.
*   **Унифицированный Дирижер:** Плата Дирижера (Conductor, 0x10) рассматривается как полноценный узел сети. Сервисное ПО обеспечивает доступ к её диагностическим ресурсам (статус очередей, логи системы, управление питанием).

## 7. Логирование и диагностика
*   Ведение циклического лога всех CAN-транзакций в формате `.log` или `.json`.
*   Возможность включения режима отладки (`terminal monitor`) для отображения сырых фреймов в консоли.

---
*Документ является первой итерацией и подлежит расширению при добавлении новых типов исполнителей.*

## 8. Подробная спецификация классов (Detailed Class Design)

### 8.1. Transport Layer (Hardware Abstraction)
*   **`ABC CanDriver`**: Абстрактный базовый класс для работы с CAN-шиной.
    *   Методы: `connect()`, `disconnect()`, `send_frame(id, data)`, `recv_frame(timeout)`.
*   **`PcanDriver(CanDriver)`**, **`SocketCanDriver(CanDriver)`**: Реализации под конкретные адаптеры (Peak-System, CANable/SLCAN).

### 8.2. Protocol Layer (DDS-240 Engine)
*   **`class CanFrame`**: Модель CAN-сообщения.
    *   Свойства: `priority`, `msg_type`, `dst_addr`, `src_addr`, `dlc`, `data`.
    *   Методы: `to_raw_id()` (сборка 29-bit), `from_raw_id(id)` (парсинг).
*   **`class Transaction`**: Объект управления жизненным циклом одной команды.
    *   Состояния: `IDLE`, `WAIT_ACK`, `RECEIVING_DATA`, `COMPLETED`, `TIMEOUT`, `NACK_ERROR`.
    *   Методы: `start()`, `process_rx(frame)`, `cancel()`.
*   **`class MessagePacker`**: Статический утилитарный класс для сериализации данных.
    *   Методы: `pack_uint16(val)`, `unpack_float(data)`, `unpack_uid(frames_list)`.

### 8.3. Device Model Layer (Executor Hierarchy)
Иерархия классов должна поддерживать расширение под все типы исполнителей:
*   **`class BaseExecutor`**: Базовый класс для любого узла на шине.
    *   Поля: `node_id`, `device_type`, `uid` (96-bit), `fw_version`.
    *   Методы: `get_info()`, `reboot(magic)`, `commit_flash()`.
*   **`class ThermoExecutor(BaseExecutor)`**: Специализация для термоплаты (0x40).
    *   Поля: `sensors_count`, `mapping_table[8]`.
    *   Методы: `scan_bus()`, `get_phys_id(index)`, `set_channel_map(logical, rom)`.
*   **`class MotionExecutor(BaseExecutor)`**: Специализация для плат управления шаговыми двигателями (0x20).
*   **`class FluidicExecutor(BaseExecutor)`**: Специализация для управления насосами и клапанами (0x30).
*   **`class ConductorExecutor(BaseExecutor)`**: Диагностика и управление материнской платой (0x10).
*   **`class DeviceFactory`**: Паттерн "Фабрика" для динамического создания объектов.
    *   Метод: `create_executor(type_id, node_id)` — возвращает объект нужного класса.

### 8.4. UI/Shell Layer (Cisco Framework)
*   **`class CiscoShell`**: Главный цикл интерактивной оболочки.
    *   Поля: `current_context`, `session_state`, `command_registry`.
    *   Методы: `cmd_loop()`, `parse_input()`, `execute_command()`.
*   **`class CommandRegistry`**: Реестр доступных команд для каждого режима.
    *   Методы: `get_available_commands(context)`, `register_command(trigger, command_class)`.
*   **`class ContextManager`**: Управление приглашением строки (`prompt`) и контекстной памятью адресов.

## 9. Формат хранения конфигурации (The Inventory File)
ПО должно поддерживать сохранение "Слепка системы" (Snapshot) в формате JSON или YAML для быстрого восстановления настроек при замене плат.

**Пример структуры (inventory.json):**
```json
{
  "system_name": "Analyzer-Alpha-01",
  "nodes": [
    {
      "node_id": "0x40",
      "type": "ThermoBoard",
      "uid": "34002F000B51353232333643",
      "mapping": {
        "1": "28-EABB-01-00-00-00-DE",
        "2": "28-FF12-05-00-00-00-BC"
      }
    }
  ]
}
```
*Наличие UID в файле инвентаризации позволяет ПО автоматически предложить восстановление маппинга, если оно обнаружит на шине плату с тем же физическим ID чипа.*

### 9.1. Алгоритм синхронизации (UID Matching)
При загрузке файла инвентаризации ПО выполняет сопоставление (Match):
1. **UID Match + NodeID Match:** Узел готов к работе.
2. **UID Match + NodeID Mismatch:** ПО обнаружило плату на другом адресе (например, 0x7F). Выводится предупреждение: `Node 0x40 found at 0x7F. Execute 'rebind' to fix?`.
3. **UID Mismatch + NodeID Match:** Конфликт оборудования. Выводится ошибка: `Address 0x40 occupied by unknown device (UID: ...).`.

---

## 10. Файловая структура проекта (Project Layout)

Для обеспечения максимальной изоляции слоев и удобства масштабирования используется модульная структура пакетов Python.

```text
analyzer_service_tool/
├── main.py                 # Точка входа: инициализация драйверов и запуск CLI
├── requirements.txt        # Зависимости: python-can, prompt_toolkit, rich, pyyaml
├── config.yaml             # Глобальные настройки: дефолтные адреса, тайм-ауты
│
├── core/                   # Пакет логики протокола (DDS-240 Engine)
│   ├── __init__.py
│   ├── frame.py            # class CanFrame (сборка/разборка 29-bit ID)
│   ├── transaction.py      # class Transaction (FSM, Handshake, Retry logic)
│   ├── packer.py           # class MessagePacker (сериализация Little-Endian)
│   └── factory.py          # class DeviceFactory (динамическое создание Executor)
│
├── transport/              # Пакет аппаратных драйверов (HAL)
│   ├── __init__.py
│   ├── base.py             # ABC CanDriver (интерфейс драйвера)
│   ├── pcan.py             # class PcanDriver (реализация для Peak-System)
│   └── socketcan.py        # class SocketCanDriver (Linux/SLCAN/CANable)
│
├── executors/              # Пакет моделей устройств (Device Models)
│   ├── __init__.py
│   ├── base.py             # class BaseExecutor (общая логика 0xF0xx)
│   ├── thermo.py           # class ThermoExecutor (логика 0x40 и 0xF1xx)
│   └── motion.py           # class MotionExecutor (логика 0x20 - заготовка)
│
├── shell/                  # Пакет интерфейса пользователя (Cisco CLI)
│   ├── __init__.py
│   ├── engine.py           # class CiscoShell (цикл обработки ввода)
│   ├── commands.py         # class CommandRegistry (реестр триггеров и классов)
│   └── context.py          # class ContextManager (навигация cd/exit/prompt)
│
├── utils/                  # Вспомогательные утилиты
│   ├── __init__.py
│   ├── logger.py           # Настройка циклического логирования транзакций
│   └── helpers.py          # Математические функции преобразования типов
│
└── inventory/              # Директория для хранения снимков системы (Snapshots)
    └── system_alpha.json   # Файлы инвентаризации с привязкой по UID
```

### Взаимосвязь структуры и архитектуры:

1.  **Инъекция зависимостей**: `main.py` выбирает нужный драйвер из `transport/` и передает его в `DDS240Engine`. Остальные слои не знают, какой именно адаптер используется.
2.  **Динамическая загрузка**: `core/factory.py` сканирует папку `executors/` при обнаружении нового `Type ID`, что позволяет добавлять поддержку новых плат простым копированием файла без изменения ядра.
3.  **Изоляция Shell**: Логика команд в `shell/commands.py` вызывает высокоуровневые методы объектов из `executors/` (например, `executor.get_temp()`), не вникая в структуру CAN-фреймов.
4.  **Безопасность транзакций**: Класс `Transaction` инкапсулирует в себе все ожидания `ACK` и сборку `DATA`, предотвращая блокировку основного интерфейса при сбоях на шине.

---

## 11. Ссылочные документы и спецификации
При реализации ПО необходимо руководствоваться следующей документацией проекта (папка `readme/`):
1.  **[Service Infrastructure Concept](../readme/SERVICE_INFRASTRUCTURE_CONCEPT.md)** — архитектурные принципы и модель безопасности.
2.  **[Service Commands Layer (0xFxxx)](../readme/Commands_API/User_Commands/service_commands.md)** — детальная структура байтов сервисных команд.
3.  **[CAN Protocol: Application Layer](../readme/Commands_API/CAN_Protocol/3_Application_Layer.md)** — жизненный цикл транзакций (ACK-DATA-DONE).
4.  **[Operational Commands (0x0xxx)](../readme/Commands_API/User_Commands/commands.md)** — описание рабочих команд системы.
5.  **[Executor Project Report](../readme/TEMP_SENSORS_EXECUTOR_REPORT.md)** — текущий статус реализации прошивок исполнителей.

---

## 12. Обработка ошибок и диагностика (Error Mapping)
ПО должно содержать централизованный реестр кодов ошибок (Global Error Registry) для перевода системных кодов NACK в человекочитаемые сообщения.

| Код ошибки | Системное имя | Сообщение для пользователя |
| :--- | :--- | :--- |
| `0x0001` | `ERR_UNKNOWN_CMD` | Команда не поддерживается данной версией прошивки. |
| `0x0002` | `ERR_INVALID_PARAM` | Недопустимый параметр (ID датчика, значение и т.д.). |
| `0x0003` | `ERR_HARDWARE_FAILURE` | Аппаратный сбой исполнителя (например, КЗ на шине 1-Wire). |
| `0x0004` | `ERR_FLASH_WRITE` | Ошибка записи в энергонезависимую память. |

## 13. Автоматизация и пакетный режим (Batch Mode)
Для использования ПО в составе автоматизированных стендов тестирования (заводской контроль) необходимо реализовать запуск без входа в интерактивную оболочку.

**Пример вызова из внешней системы:**
```bash
python main.py --target 0x40 --exec "scan 1-wire; show temp; exit" --output report.json
```
*В данном режиме ПО должно возвращать код завершения (Exit Code) и формировать отчет в формате JSON.*

## 14. Превентивная валидация и безопасность (Safety)
Для минимизации нагрузки на CAN-шину и предотвращения аварийных ситуаций ПО реализует "Pre-flight Check":
*   **Диапазонная валидация:** Проверка параметров (0-7 для датчиков, допустимые токи для моторов) на стороне Python *до* отправки фрейма.
*   **Состояние связи (Liveness Check):** В режиме контекста платы ПО может выполнять фоновый опрос (раз в 5-10 сек). При потере связи — уведомление инженера.
*   **Защита от записи (Write Protection):** Команды изменения конфигурации (например, `write memory`) блокируются, если плата сообщает о выполнении активного рабочего цикла.

---
*Документ подготовлен для реализации сервисного ПО биохимического анализатора. Версия 1.1 (Deep Audit).*
