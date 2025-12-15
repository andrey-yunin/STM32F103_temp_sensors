# Подробный отчет о разработке прошивки для "Исполнителя" на STM32F103

## Оглавление

1.  **Начальная постановка задачи и анализ требований**
    *   1.1. Цель проекта
    *   1.2. Обзор аппаратной платформы
    *   1.3. Общая концепция архитектуры "Исполнителя"
2.  **Этап 1: Конфигурация проекта STM32CubeIDE и аппаратных ресурсов**
    *   2.1. Выбор и подтверждение микроконтроллера
    *   2.2. Настройка тактирования (Clock Configuration)
    *   2.3. Настройка периферии
        *   2.3.1. FDCAN (Controller Area Network)
        *   2.3.2. USART (для драйверов TMC2209)
        *   2.3.3. GPIO (для STEP/DIR/ENABLE)
        *   2.3.4. TIM2 (для генерации STEP-импульсов)
    *   2.4. Настройка FreeRTOS
        *   2.4.1. Выбор CMSIS_V2
        *   2.4.2. Корректировка размера кучи (Heap Size)
        *   2.4.3. Устранение ошибки "RAM overflow"
    *   2.5. Управление версиями (Git и GitHub)
        *   2.5.1. Инициализация локального репозитория
        *   2.5.2. Настройка `.gitignore`
        *   2.5.3. Загрузка проекта на GitHub
3.  **Этап 2: Проектирование и реализация программной архитектуры**
    *   3.1. Определение и уточнение архитектуры задач FreeRTOS
        *   3.1.1. Финальная архитектура FreeRTOS (4 задачи, 5 очередей)
    *   3.2. Проектирование CAN-адресации для мульти-устройств
        *   3.2.1. Иерархическая схема CAN ID
        *   3.2.2. Механизм определения `Performer_ID`
    *   3.3. **Концепция работы с глобальными переменными (Золотое Правило)**
        *   3.3.1. Категории глобальных переменных
        *   3.3.2. Файлы для определений и объявлений `extern`
    *   3.4. Создание файловой структуры приложения и "скелетов" модулей
        *   3.4.1. Структура папок `App/inc` и `App/src`
        *   3.4.2. Настройка `Source Location` и `Include Paths` в IDE
    *   3.5. Реализация вспомогательных модулей
        *   3.5.1. `app_config.h` (Константы и структуры конфигурации)
        *   3.5.2. `app_queues.h` (Объявления хэндлов очередей FreeRTOS)
        *   3.5.3. `app_globals.h` и `app_globals.c` (Глобальные переменные приложения)
        *   3.5.4. `command_protocol.h` (Определение команд CAN)
        *   3.5.5. `tmc2209_driver.h/.c` (Низкоуровневый драйвер TMC2209)
        *   3.5.6. `motion_planner.h/.c` (Планировщик движения)
        *   3.5.7. `motor_gpio.h/.c` (Абстракция GPIO для моторов)
4.  **Этап 3: Интеграция задач FreeRTOS и логики (Подготовка к Первому Тесту)**
    *   4.1. Настройка очередей FreeRTOS в `main.c`
    *   4.2. Подключение задач к `main.c`
    *   4.3. Реализация `Task_CAN_Handler`
    *   4.4. Реализация `Task_Command_Parser`
    *   4.5. Реализация `Task_TMC_Manager`
    *   4.6. Реализация `Task_Motion_Controller` и его прерывания
5.  **План Первого Теста и дальнейшие шаги**
    *   5.1. Необходимое оборудование и подключение
    *   5.2. Пример тестовой CAN-команды
    *   5.3. Ожидаемый результат
    *   5.4. Последующие улучшения и доработки

---

## 1. Начальная постановка задачи и анализ требований

### 1.1. Цель проекта
Создание прошивки для "Исполнителя" (Performer) в системе биохимического анализатора. "Исполнитель" является подчиненным устройством, управляемым "Дирижером" (Conductor) по CAN-шине, и отвечает за точное управление 8 шаговыми двигателями.

### 1.2. Обзор аппаратной платформы
*   **Микроконтроллер:** STM32F103C8T6 (серия STM32F1, 20 КБ RAM).
*   **Исполнительные узлы:** 8 драйверов шаговых двигателей TMC2209.
*   **Связь с "Дирижером":** CAN-шина.
*   **Внутренняя архитектура:** Использование FreeRTOS для многозадачности.

### 1.3. Общая концепция архитектуры "Исполнителя"
Прошивка будет модульной, с использованием FreeRTOS для управления concurrency. Основные компоненты включают:
*   Обработчик CAN-сообщений.
*   Парсер команд.
*   Контроллер движения (генерация STEP/DIR).
*   Менеджер драйверов TMC2209 (конфигурация по UART).

---

## 2. Этап 1: Конфигурация проекта STM32CubeIDE и аппаратных ресурсов

### 2.1. Выбор и подтверждение микроконтроллера
Изначально было предположение об STM32H723, но в ходе аудита проекта `STM32F103_step_motors.ioc` подтвержден микроконтроллер **STM32F103C8T6**. Все настройки и код адаптированы под эту платформу.

### 2.2. Настройка тактирования (Clock Configuration)
Система настроена на работу от внутреннего высокоскоростного генератора (HSI) с использованием PLL для достижения максимальной стабильной частоты.
*   **Источник:** HSI (8 MHz).
*   **PLL Multiplier:** x16 (HSI/2 -> 4 MHz * 16 = 64 MHz).
*   **SYSCLK (HCLK):** 64 MHz.
*   **APB1 Clock:** 32 MHz (что критично для CAN).

### 2.3. Настройка периферии

#### 2.3.1. FDCAN (Controller Area Network)
*   Включен FDCAN1.
*   **Скорость:** 500 кбит/с, достигнуто настройкой `Prescaler=4`, `Time Segment 1=11`, `Time Segment 2=4`.
*   **Прерывания:** `CAN1 RX0 interrupts` включены в `NVIC Settings`.

#### 2.3.2. USART (для драйверов TMC2209)
*   **USART1 и USART2** включены в асинхронном режиме.
*   **Скорость:** 115200 бит/с.
*   **Адресация:** 8 драйверов распределены на 2 UART (4 на USART1, 4 на USART2), с уникальной адресацией (0-3) на каждом UART через пины MS1/MS2.

#### 2.3.3. GPIO (для STEP/DIR/ENABLE)
*   Для каждого из 8 моторов выделены пины GPIO: `STEP_X` (GPIOA), `DIR_X` (GPIOB), `EN_X` (GPIOB).
*   Все пины сконфигурированы как `GPIO_Output`.

#### 2.3.4. TIM2 (для генерации STEP-импульсов)
*   `TIM2` сконфигурирован как 32-битный таймер общего назначения.
*   Режим: Output Compare No Output на Channel 1.
*   **Прерывания:** `TIM2 global interrupt` включены в `NVIC Settings`.

### 2.4. Настройка FreeRTOS

#### 2.4.1. Выбор CMSIS_V2
FreeRTOS включен и настроен с использованием интерфейса CMSIS_V2.

#### 2.4.2. Корректировка размера кучи (Heap Size)
*   Изначально `configTOTAL_HEAP_SIZE` был 3072 байта.
*   **Проблема:** Возникла ошибка "RAM overflow" при компоновке.
*   **Решение:** Увеличен до `4096` байт.

#### 2.4.3. Устранение ошибки "RAM overflow"
Ошибка была решена путем уменьшения стеков задач (до 128-256 слов) и корректировки `configTOTAL_HEAP_SIZE`.

### 2.5. Управление версиями (Git и GitHub)

*   **Инициализация:** Локальный Git-репозиторий инициализирован в корне проекта.
*   **`.gitignore`:** Настроен для игнорирования файлов сборки STM32CubeIDE.
*   **GitHub:** Проект успешно загружен на удаленный репозиторий GitHub с использованием Personal Access Token.

---

## 3. Этап 2: Проектирование и реализация программной архитектуры

### 3.1. Определение и уточнение архитектуры задач FreeRTOS

В ходе обсуждения была сформирована следующая архитектура для обеспечения модульности и эффективного использования ресурсов.

#### 3.1.1. Финальная архитектура FreeRTOS (4 задачи, 5 очередей)
*   **Задачи (4 шт.):**
    1.  `Task_CAN_Handler`: Обработка входящих/исходящих CAN-сообщений.
    2.  `Task_Command_Parser`: Парсинг и диспетчеризация CAN-команд.
    3.  `Task_Motion_Controller`: Генерация STEP/DIR сигналов, управление движением.
    4.  `Task_TMC_Manager`: Конфигурация и мониторинг драйверов TMC2209.
*   **Очереди (5 шт.):** Для асинхронной и потокобезопасной связи между задачами и прерываниями.
    1.  `can_rx_queueHandle` (ISR -> `Task_CAN_Handler`)
    2.  `can_tx_queueHandle` (Различные задачи -> `Task_CAN_Handler`)
    3.  `parser_queueHandle` (`Task_CAN_Handler` -> `Task_Command_Parser`)
    4.  `motion_queueHandle` (`Task_Command_Parser` -> `Task_Motion_Controller`)
    5.  `tmc_manager_queueHandle` (`Task_Command_Parser` -> `Task_TMC_Manager`)

### 3.2. Проектирование CAN-адресации для мульти-устройств

#### 3.2.1. Иерархическая схема CAN ID
Для поддержки до 16 "исполнителей", каждый с 8 моторами, используется иерархическая схема кодирования ID в 11-битном `StdId` CAN-фрейма:
`StdId = 0x100 | (ID_Исполнителя << 3) | ID_Мотора`
*   `ID_Исполнителя`: 4 бита (0-15).
*   `ID_Мотора`: 3 бита (0-7).
*   `0x100`: Базовый адрес для команд.

#### 3.2.2. Механизм определения `Performer_ID`
*   `Performer_ID` хранится в Flash-памяти микроконтроллера.
*   Механизм первоначального "провизионинга" (установки ID) будет реализован позже (например, через широковещательную CAN-команду, использующую уникальный серийный номер STM32).
*   На данном этапе `g_performer_id` инициализируется в `0xFF` и для теста принимается как `0`.

### 3.3. **Концепция работы с глобальными переменными (Золотое Правило)**

Для обеспечения чистоты кода, предотвращения ошибок "multiple definition" и повышения безопасности, принята следующая концепция:

#### 3.3.1. Категории глобальных переменных
1.  **Хэндлы периферии HAL** (`hcan`, `htim2`, `huartX`): Генерируются CubeMX в `main.c`, объявляются `extern` в `main.h`. Доступ через `#include "main.h"`.
2.  **Хэндлы объектов FreeRTOS** (`..._queueHandle`): Определяются в `main.c`, объявляются `extern` в `App/inc/app_queues.h`. Доступ через `#include "app_queues.h"`.
3.  **Прочие глобальные переменные приложения** (`g_performer_id`, `motor_states`, `tmc_drivers`, `g_active_motor_id`): Определяются в `App/src/app_globals.c`, объявляются `extern` в `App/inc/app_globals.h`. Доступ через `#include "app_globals.h"`.

#### 3.3.2. Файлы для определений и объявлений `extern`
*   **`App/inc/app_globals.h`**: Содержит `extern` объявления для `g_performer_id`, `g_active_motor_id`, `motor_states`, `tmc_drivers`.
*   **`App/src/app_globals.c`**: Содержит определения для `g_performer_id`, `g_active_motor_id`, `motor_states`, `tmc_drivers`.

### 3.4. Создание файловой структуры приложения и "скелетов" модулей
*   Созданы папки `App/inc` (для заголовочных файлов) и `App/src` (для файлов исходного кода), с подпапками `tasks`.
*   Настройка `Source Location` и `Include Paths` в IDE выполнена для корректной сборки проекта.

### 3.5. Реализация вспомогательных модулей

#### 3.5.1. `app_config.h` (Константы и структуры конфигурации)
Централизованный файл для констант приложения и FreeRTOS очередей.
```c
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "main.h" // Для CAN_RxHeaderTypeDef, CAN_TxHeaderTypeDef
#include "cmsis_os.h" // Для FreeRTOS типов

// =============================================================================
//                             ОБЩИЕ НАСТРОЙКИ ПРИЛОЖЕНИЯ
// =============================================================================

#define MOTOR_COUNT                 8 // Общее количество моторов в системе
#define CAN_DATA_MAX_LEN            8 // Максимальная длина поля данных CAN-фрейма (в байтах)

// =============================================================================
//                             НАСТРОЙКИ ОЧЕРЕДЕЙ FREERTOS
// =============================================================================

// -- ДЛИНА ОЧЕРЕДЕЙ (количество элементов) --
#define CAN_RX_QUEUE_LEN            10
#define CAN_TX_QUEUE_LEN            10
#define PARSER_QUEUE_LEN            10
#define MOTION_QUEUE_LEN            5
#define TMC_MANAGER_QUEUE_LEN       5

// -- СТРУКТУРЫ ЭЛЕМЕНТОВ ОЧЕРЕДЕЙ --
// Структура для хранения полного входящего CAN-фрейма (header + data)
typedef struct {
    CAN_RxHeaderTypeDef header;
    uint8_t data[CAN_DATA_MAX_LEN];
} CanRxFrame_t;

// Структура для хранения полного исходящего CAN-фрейма (header + data)
typedef struct {
    CAN_TxHeaderTypeDef header;
    uint8_t data[CAN_DATA_MAX_LEN];
} CanTxFrame_t;

// Структура задания для Task_Motion_Controller
typedef struct {
    uint8_t motor_id;
    uint8_t direction; // Будет перезаписано MotionPlanner
    uint32_t steps;    // Количество шагов для относительного движения (или целевая позиция для абсолютного)
    uint32_t speed_steps_per_sec; // Желаемая скорость
    uint32_t acceleration_steps_per_sec2; // Желаемое ускорение
} MotionCommand_t;

#endif // APP_CONFIG_H
```

#### 3.5.2. `app_queues.h` (Объявления хэндлов очередей FreeRTOS)
```c
#ifndef APP_QUEUES_H
#define APP_QUEUES_H

#include "cmsis_os.h" // Для osMessageQueueId_t

extern osMessageQueueId_t can_rx_queueHandle;      // Для приема сырых CAN-фреймов (ISR -> CAN Handler)
extern osMessageQueueId_t can_tx_queueHandle;      // Для отправки CAN-сообщений (любая задача -> CAN Handler)
extern osMessageQueueId_t parser_queueHandle;      // Для передачи CAN-фреймов (CAN Handler -> Command Parser)
extern osMessageQueueId_t motion_queueHandle;      // Для передачи заданий движения (Command Parser -> Motion Controller)
extern osMessageQueueId_t tmc_manager_queueHandle; // Для передачи команд TMC (Command Parser -> TMC Manager)

#endif // APP_QUEUES_H
```

#### 3.5.3. `app_globals.h` и `app_globals.c` (Глобальные переменные приложения)
`app_globals.h`:
```c
#ifndef APP_GLOBALS_H
#define APP_GLOBALS_H

#include <stdint.h>
#include "motion_planner.h" // Для MotorMotionState_t
#include "tmc2209_driver.h" // Для TMC2209_Handle_t
#include "app_config.h"     // Для MOTOR_COUNT

extern uint8_t g_performer_id;
extern volatile int8_t g_active_motor_id;
extern MotorMotionState_t motor_states[MOTOR_COUNT];
extern TMC2209_Handle_t tmc_drivers[MOTOR_COUNT];

#endif // APP_GLOBALS_H
```
`app_globals.c`:
```c
#include "app_globals.h"

uint8_t g_performer_id = 0xFF;
volatile int8_t g_active_motor_id = -1;
MotorMotionState_t motor_states[MOTOR_COUNT];
TMC2209_Handle_t tmc_drivers[MOTOR_COUNT];
```

#### 3.5.4. `command_protocol.h` (Определение команд CAN)
```c
#ifndef COMMAND_PROTOCOL_H
#define COMMAND_PROTOCOL_H

#include <stdint.h>

typedef enum {
    CMD_MOVE_ABSOLUTE       = 0x01,
    CMD_MOVE_RELATIVE       = 0x02,
    CMD_SET_SPEED           = 0x03,
    CMD_SET_ACCELERATION    = 0x04,
    CMD_STOP                = 0x05,
    CMD_GET_STATUS          = 0x06,
    CMD_SET_CURRENT         = 0x07,
    CMD_ENABLE_MOTOR        = 0x08,
    CMD_PERFORMER_ID_SET    = 0x09,
} CommandID_t;

typedef struct {
    uint8_t     motor_id;
    CommandID_t command_id;
    int32_t     payload;
} CAN_Command_t;

#endif // COMMAND_PROTOCOL_H
```

#### 3.5.5. `tmc2209_driver.h/.c` (Низкоуровневый драйвер TMC2209)
`tmc2209_driver.h`:
```c
#ifndef TMC2209_DRIVER_H
#define TMC2209_DRIVER_H

#include <stdint.h>
#include "main.h" // Для HAL_UART_HandleTypeDef

#define TMC2209_UART_SYNC_BYTE      0x05
#define TMC2209_UART_SLAVE_ADDRESS  0x00
#define TMC2209_UART_READ         0x00
#define TMC2209_UART_WRITE        0x80

typedef enum {
    TMC2209_GCONF       = 0x00,
    TMC2209_GSTAT       = 0x01,
    TMC2209_IOIN        = 0x04,
    TMC2209_PWMCONF     = 0x30,
    TMC2209_CHOPCONF    = 0x6C,
    TMC2209_DRVSTATUS   = 0x6F,
    TMC2209_VACTUAL     = 0x22,
    TMC2209_MSCNT       = 0x6A,
    TMC2209_MSCURACT    = 0x6B,
    TMC2209_SGTHRS      = 0x40,
    TMC2209_COOLCONF    = 0x42,
    TMC2209_TPOWERDOWN  = 0x31,
    TMC2209_IHOLD_IRUN  = 0x10,
} TMC2209_Register_t;

typedef struct {
    UART_HandleTypeDef* huart;
    uint8_t             slave_addr;
} TMC2209_Handle_t;

HAL_StatusTypeDef TMC2209_Init(TMC2209_Handle_t* htmc, UART_HandleTypeDef* huart, uint8_t slave_addr);
HAL_StatusTypeDef TMC2209_WriteRegister(TMC2209_Handle_t* htmc, TMC2209_Register_t reg, uint32_t value);
HAL_StatusTypeDef TMC2209_ReadRegister(TMC2209_Handle_t* htmc, TMC2209_Register_t reg, uint32_t* value);
HAL_StatusTypeDef TMC2209_SetMotorCurrent(TMC2209_Handle_t* htmc, uint8_t run_current_percent, uint8_t hold_current_percent);
HAL_StatusTypeDef TMC2209_SetMicrosteps(TMC2209_Handle_t* htmc, uint16_t microsteps);
HAL_StatusTypeDef TMC2209_SetSpreadCycle(TMC2209_Handle_t* htmc, uint8_t enable);

#endif // TMC2209_DRIVER_H
```
`tmc2209_driver.c`:
```c
#include "tmc2209_driver.h"
#include <string.h>

static const uint8_t tmc_crc_table[256] = {
    0x00, 0x07, 0x0E, 0x09, 0x1C, 0x1B, 0x12, 0x15, 0x38, 0x3F, 0x36, 0x31, 0x24, 0x23, 0x2A, 0x2D,
    0x70, 0x77, 0x7E, 0x79, 0x6C, 0x6B, 0x62, 0x65, 0x48, 0x4F, 0x46, 0x41, 0x54, 0x53, 0x5A, 0x5D,
    0xE0, 0xE7, 0xEE, 0xE9, 0xFC, 0xFB, 0xF2, 0xF5, 0xD8, 0xDF, 0xD6, 0xD1, 0xC4, 0xC3, 0xCA, 0xCD,
    0x90, 0x97, 0x9E, 0x99, 0x8C, 0x8B, 0x82, 0x85, 0xA8, 0xAF, 0xA6, 0xA1, 0xB4, 0xB3, 0xBA, 0xBD,
    0xC7, 0xC0, 0xC9, 0xCE, 0xDB, 0xDC, 0xD5, 0xD2, 0xFF, 0xF8, 0xF1, 0xF6, 0xE3, 0xE4, 0xED, 0xEA,
    0xB7, 0xB0, 0xB9, 0xBE, 0xAB, 0xAC, 0xA5, 0xA2, 0x8F, 0x88, 0x81, 0x86, 0x93, 0x94, 0x9D, 0x9A,
    0x27, 0x20, 0x29, 0x2E, 0x3B, 0x3C, 0x35, 0x32, 0x1F, 0x18, 0x11, 0x16, 0x03, 0x04, 0x0D, 0x0A,
    0x57, 0x50, 0x59, 0x5E, 0x4B, 0x4C, 0x45, 0x42, 0x6F, 0x68, 0x61, 0x66, 0x73, 0x74, 0x7D, 0x7A,
    0x89, 0x8E, 0x87, 0x80, 0x95, 0x92, 0x9B, 0x9C, 0xB1, 0xB6, 0xBF, 0xB8, 0xAD, 0xAA, 0xA3, 0xA4,
    0xF9, 0xFE, 0xF7, 0xF0, 0xE5, 0xE2, 0xEB, 0xEC, 0xC1, 0xC6, 0xCF, 0xC8, 0xDD, 0xDA, 0xD3, 0xD4,
    0x69, 0x6E, 0x67, 0x60, 0x75, 0x72, 0x7B, 0x7C, 0x51, 0x56, 0x5F, 0x58, 0x4D, 0x4A, 0x43, 0x44,
    0x19, 0x1E, 0x17, 0x10, 0x05, 0x02, 0x0B, 0x0C, 0x21, 0x26, 0x2F, 0x28, 0x3D, 0x3A, 0x33, 0x34,
    0x4E, 0x49, 0x40, 0x47, 0x52, 0x55, 0x5C, 0x5B, 0x76, 0x71, 0x78, 0x7F, 0x6A, 0x6D, 0x64, 0x63,
    0x3E, 0x39, 0x30, 0x37, 0x22, 0x25, 0x2C, 0x2B, 0x06, 0x01, 0x08, 0x0F, 0x1A, 0x1D, 0x14, 0x13,
    0xAE, 0xA9, 0xA0, 0xA7, 0xB2, 0xB5, 0xBC, 0xBB, 0x96, 0x91, 0x98, 0x9F, 0x8A, 0x8D, 0x84, 0x83,
    0xDE, 0xD9, 0xD0, 0xD7, 0xC2, 0xC5, 0xCC, 0xCB, 0xE6, 0xE1, 0xE8, 0xEF, 0xFA, 0xFD, 0xF4, 0xF3
};

static uint8_t tmc_crc8(uint8_t *data, size_t length) {
    uint8_t crc = 0;
    for (size_t i = 0; i < length; i++) {
        crc = tmc_crc_table[crc ^ data[i]];
    }
    return crc;
}

HAL_StatusTypeDef TMC2209_Init(TMC2209_Handle_t* htmc, UART_HandleTypeDef* huart, uint8_t slave_addr) {
    if (htmc == NULL || huart == NULL || slave_addr > 3) {
        return HAL_ERROR;
    }
    htmc->huart = huart;
    htmc->slave_addr = slave_addr;
    return HAL_OK;
}

HAL_StatusTypeDef TMC2209_WriteRegister(TMC2209_Handle_t* htmc, TMC2209_Register_t reg, uint32_t value) {
    uint8_t tx_buf[8];
    tx_buf[0] = TMC2209_UART_SYNC_BYTE;
    tx_buf[1] = htmc->slave_addr;
    tx_buf[2] = TMC2209_UART_WRITE | reg;
    tx_buf[3] = (uint8_t)(value >> 24);
    tx_buf[4] = (uint8_t)(value >> 16);
    tx_buf[5] = (uint8_t)(value >> 8);
    tx_buf[6] = (uint8_t)(value);
    tx_buf[7] = tmc_crc8(tx_buf, 7);

    return HAL_UART_Transmit(htmc->huart, tx_buf, sizeof(tx_buf), HAL_MAX_DELAY);
}

HAL_StatusTypeDef TMC2209_ReadRegister(TMC2209_Handle_t* htmc, TMC2209_Register_t reg, uint32_t* value) {
    uint8_t tx_buf[4];
    uint8_t rx_buf[8];

    tx_buf[0] = TMC2209_UART_SYNC_BYTE;
    tx_buf[1] = htmc->slave_addr;
    tx_buf[2] = TMC2209_UART_READ | reg;
    tx_buf[3] = tmc_crc8(tx_buf, 3);

    if (HAL_UART_Transmit(htmc->huart, tx_buf, sizeof(tx_buf), HAL_MAX_DELAY) != HAL_OK) {
        return HAL_ERROR;
    }

    if (HAL_UART_Receive(htmc->huart, rx_buf, sizeof(rx_buf), 100) != HAL_OK) {
        return HAL_ERROR;
    }

    if (rx_buf[0] != TMC2209_UART_SYNC_BYTE ||
        rx_buf[1] != htmc->slave_addr ||
        (rx_buf[2] & 0x7F) != reg ||
        tmc_crc8(rx_buf, 7) != rx_buf[7]) {
        return HAL_ERROR;
    }

    *value = ((uint32_t)rx_buf[3] << 24) |
             ((uint32_t)rx_buf[4] << 16) |
             ((uint32_t)rx_buf[5] << 8)  |
             ((uint32_t)rx_buf[6]);

    return HAL_OK;
}

HAL_StatusTypeDef TMC2209_SetMotorCurrent(TMC2209_Handle_t* htmc, uint8_t run_current_percent, uint8_t hold_current_percent) {
    if (htmc == NULL || run_current_percent == 0 || run_current_percent > 100 || hold_current_percent > 100) {
        return HAL_ERROR;
    }

    uint8_t ihold = (uint8_t)((float)hold_current_percent / 100.0f * 31.0f);
    uint8_t irun = (uint8_t)((float)run_current_percent / 100.0f * 31.0f);
    uint8_t tpowerdown = 10;

    uint32_t value = ((uint32_t)tpowerdown << 24) | ((uint32_t)ihold << 16) | ((uint32_t)irun << 8) | (0x4 << 0);

    return TMC2209_WriteRegister(htmc, TMC2209_IHOLD_IRUN, value);
}

HAL_StatusTypeDef TMC2209_SetMicrosteps(TMC2209_Handle_t* htmc, uint16_t microsteps) {
    if (htmc == NULL) {
        return HAL_ERROR;
    }

    uint32_t mres_val = 0;
    switch (microsteps) {
        case 256: mres_val = 0; break;
        case 128: mres_val = 1; break;
        case 64:  mres_val = 2; break;
        case 32:  mres_val = 3; break;
        case 16:  mres_val = 4; break;
        case 8:   mres_val = 5; break;
        case 4:   mres_val = 6; break;
        case 2:   mres_val = 7; break;
        case 1:   mres_val = 8; break;
        default: return HAL_ERROR;
    }

    uint32_t chopconf_reg;
    if (TMC2209_ReadRegister(htmc, TMC2209_CHOPCONF, &chopconf_reg) != HAL_OK) {
        chopconf_reg = 0;
    }

    chopconf_reg &= ~((uint32_t)0xF << 24);
    chopconf_reg |= (mres_val << 24);

    return TMC2209_WriteRegister(htmc, TMC2209_CHOPCONF, chopconf_reg);
}

HAL_StatusTypeDef TMC2209_SetSpreadCycle(TMC2209_Handle_t* htmc, uint8_t enable) {
    (void)htmc;
    (void)enable;
    return HAL_ERROR;
}
```

#### 3.5.6. `motion_planner.h/.c` (Планировщик движения)
`motion_planner.h`:
```c
#ifndef MOTION_PLANNER_H
#define MOTION_PLANNER_H

#include <stdint.h>

#define MAX_STEPS_PER_SEC 20000

typedef struct {
    int32_t current_position;
    int32_t target_position;
    uint32_t current_speed_steps_per_sec;
    uint32_t max_speed_steps_per_sec;
    uint32_t acceleration_steps_per_sec2;
    uint8_t direction;
    int32_t steps_to_go;
    uint32_t step_pulse_period_us;
} MotorMotionState_t;

void MotionPlanner_InitMotorState(MotorMotionState_t* state, int32_t initial_pos);
int32_t MotionPlanner_CalculateNewTarget(MotorMotionState_t* state, int32_t target);
uint32_t MotionPlanner_GetNextPulsePeriod(MotorMotionState_t* state);
uint8_t MotionPlanner_IsMovementComplete(MotorMotionState_t* state);

#endif // MOTION_PLANNER_H
```
`motion_planner.c`:
```c
#include "motion_planner.h"
#include <math.h>
#include <stdlib.h>

void MotionPlanner_InitMotorState(MotorMotionState_t* state, int32_t initial_pos)
{
    if (state == NULL) return;

    state->current_position = initial_pos;
    state->target_position = initial_pos;
    state->current_speed_steps_per_sec = 0;
    state->max_speed_steps_per_sec = 2000;
    state->acceleration_steps_per_sec2 = 500;
    state->direction = 0;
    state->steps_to_go = 0;
    state->step_pulse_period_us = 0;
}

int32_t MotionPlanner_CalculateNewTarget(MotorMotionState_t* state, int32_t target)
{
    if (state == NULL) return 0;

    state->target_position = target;
    state->steps_to_go = abs(state->target_position - state->current_position);

    if (state->target_position > state->current_position) {
        state->direction = 1;
    } else {
        state->direction = 0;
    }

    return state->steps_to_go;
}

uint32_t MotionPlanner_GetNextPulsePeriod(MotorMotionState_t* state)
{
    if (state == NULL || state->steps_to_go == 0) {
        return 0;
    }

    if (state->steps_to_go > 0) {
        state->steps_to_go--;
        if (state->direction) {
            state->current_position++;
        } else {
            state->current_position--;
        }
    }

    return 1000000 / state->max_speed_steps_per_sec;
}

uint8_t MotionPlanner_IsMovementComplete(MotorMotionState_t* state)
{
    if (state == NULL) return 1;

    return (state->steps_to_go == 0);
}
```

#### 3.5.7. `motor_gpio.h/.c` (Абстракция GPIO для моторов)
`motor_gpio.h`:
```c
#ifndef MOTOR_GPIO_H
#define MOTOR_GPIO_H

#include "main.h"

void Motor_SetDirection(uint8_t motor_id, uint8_t direction);
void Motor_Enable(uint8_t motor_id);
void Motor_Disable(uint8_t motor_id);
void Motor_ToggleStepPin(uint8_t motor_id);

#endif // MOTOR_GPIO_H
```
`motor_gpio.c`:
```c
#include "motor_gpio.h"
#include "app_config.h"
#include "main.h"

const uint16_t STEP_PINS[MOTOR_COUNT] = {
    GPIO_PIN_0, GPIO_PIN_1, GPIO_PIN_4, GPIO_PIN_5,
    GPIO_PIN_6, GPIO_PIN_7, GPIO_PIN_8, GPIO_PIN_15
};

const uint16_t DIR_PINS[MOTOR_COUNT] = {
    GPIO_PIN_0, GPIO_PIN_1, GPIO_PIN_2, GPIO_PIN_3,
    GPIO_PIN_4, GPIO_PIN_5, GPIO_PIN_6, GPIO_PIN_7
};

const uint16_t EN_PINS[MOTOR_COUNT] = {
    GPIO_PIN_8, GPIO_PIN_9, GPIO_PIN_10, GPIO_PIN_11,
    GPIO_PIN_12, GPIO_PIN_13, GPIO_PIN_14, GPIO_PIN_15
};

void Motor_SetDirection(uint8_t motor_id, uint8_t direction)
{
    if (motor_id >= MOTOR_COUNT) return;

    if (direction == 1) {
        HAL_GPIO_WritePin(GPIOB, DIR_PINS[motor_id], GPIO_PIN_SET);
    } else {
        HAL_GPIO_WritePin(GPIOB, DIR_PINS[motor_id], GPIO_PIN_RESET);
    }
}

void Motor_Enable(uint8_t motor_id)
{
    if (motor_id >= MOTOR_COUNT) return;

    HAL_GPIO_WritePin(GPIOB, EN_PINS[motor_id], GPIO_PIN_RESET);
}

void Motor_Disable(uint8_t motor_id)
{
    if (motor_id >= MOTOR_COUNT) return;

    HAL_GPIO_WritePin(GPIOB, EN_PINS[motor_id], GPIO_PIN_SET);
}

void Motor_ToggleStepPin(uint8_t motor_id)
{
    if (motor_id >= MOTOR_COUNT) return;

    if ((GPIOA->ODR & STEP_PINS[motor_id]) != 0)
    {
        GPIOA->BSRR = (uint32_t)STEP_PINS[motor_id] << 16U;
    }
    else
    {
        GPIOA->BSRR = STEP_PINS[motor_id];
    }
}
```

## 4. Этап 3: Интеграция задач FreeRTOS и логики (Подготовка к Первому Тесту)

На этом этапе мы связали все задачи, настроили их взаимодействие через очереди и реализовали базовую логику для обеспечения первого движения мотора по CAN-команде.

### 4.1. Настройка очередей FreeRTOS в `main.c`
```c
// Core/Src/main.c

// Секция USER CODE BEGIN PV
/* USER CODE BEGIN PV */
osMessageQueueId_t can_rx_queueHandle;
osMessageQueueId_t can_tx_queueHandle;
osMessageQueueId_t parser_queueHandle;
osMessageQueueId_t motion_queueHandle;
osMessageQueueId_t tmc_manager_queueHandle;
/* USER CODE END PV */

// Секция USER CODE BEGIN 2
/* USER CODE BEGIN 2 */
// Создание очередей FreeRTOS
can_rx_queueHandle = osMessageQueueNew(CAN_RX_QUEUE_LEN, sizeof(CanRxFrame_t), NULL);
parser_queueHandle = osMessageQueueNew(PARSER_QUEUE_LEN, sizeof(CAN_Command_t), NULL);
motion_queueHandle = osMessageQueueNew(MOTION_QUEUE_LEN, sizeof(MotionCommand_t), NULL);
tmc_manager_queueHandle = osMessageQueueNew(TMC_MANAGER_QUEUE_LEN, sizeof(CAN_Command_t), NULL);
can_tx_queueHandle = osMessageQueueNew(CAN_TX_QUEUE_LEN, sizeof(CanTxFrame_t), NULL);

// Проверка успешности создания очередей
if (can_rx_queueHandle == NULL || parser_queueHandle == NULL || motion_queueHandle == NULL || tmc_manager_queueHandle == NULL || can_tx_queueHandle == NULL) {
    Error_Handler();
}
/* USER CODE END 2 */
```

### 4.2. Подключение задач к `main.c`
Конвенция: `StartTaskXyz` вызывает `app_start_task_xyz`.

```c
// Core/Src/main.c

// Секция USER CODE BEGIN Includes
/* USER CODE BEGIN Includes */
#include "app_queues.h"
#include "app_config.h"
#include "command_protocol.h"
#include "app_globals.h" // Для g_performer_id, motor_states, tmc_drivers
#include "tasks/task_can_handler.h"
#include "tasks/task_command_parser.h"
#include "tasks/task_motion_controller.h"
#include "tasks/task_tmc2209_manager.h"
/* USER CODE END Includes */

// Пример для TaskCANHandler (аналогично для всех 4 задач)
void StartTaskCANHandler(void *argument)
{
  /* USER CODE BEGIN StartTaskCANHandler */
  app_start_task_can_handler(argument);
  /* USER CODE END StartTaskCANHandler */
}
// ... и другие StartTaskXyz вызывают app_start_task_xyz
```

### 4.3. Реализация `Task_CAN_Handler`
`App/src/tasks/task_can_handler.c`:
```c
#include "task_can_handler.h"
#include "main.h"
#include "cmsis_os.h"

#include "app_queues.h"
#include "app_globals.h"
#include "command_protocol.h"
#include "app_config.h"

extern CAN_HandleTypeDef hcan; // Хэндл CAN-периферии из main.c

void app_start_task_can_handler(void *argument)
{
    (void)argument;

    CanRxFrame_t rx_frame;
    CAN_Command_t parsed_command;
    CanTxFrame_t tx_frame;

    uint32_t txMailbox;

    CAN_FilterTypeDef sFilterConfig;

    sFilterConfig.FilterBank = 0;
    sFilterConfig.FilterMode = CAN_FILTERMODE_IDMASK;
    sFilterConfig.FilterScale = CAN_FILTERSCALE_32BIT;
    sFilterConfig.FilterIdHigh = 0x0000;
    sFilterConfig.FilterIdLow = 0x0000;
    sFilterConfig.FilterMaskIdHigh = 0x0000;
    sFilterConfig.FilterMaskIdLow = 0x0000;
    sFilterConfig.FilterFIFOAssignment = CAN_RX_FIFO0;
    sFilterConfig.FilterActivation = ENABLE;
    sFilterConfig.SlaveStartFilterBank = 14;

    if (HAL_CAN_ConfigFilter(&hcan, &sFilterConfig) != HAL_OK) {
        Error_Handler();
    }

    if (HAL_CAN_Start(&hcan) != HAL_OK) {
        Error_Handler();
    }

    if (HAL_CAN_ActivateNotification(&hcan, CAN_IT_RX_FIFO0_MSG_PENDING) != HAL_OK) {
        Error_Handler();
    }

    for(;;)
    {
        if (osMessageQueueGet(can_rx_queueHandle, &rx_frame, NULL, osWaitForever) == osOK) {
            uint16_t std_id = rx_frame.header.StdId;
            uint8_t received_performer_id = (std_id >> 3) & 0xF;
            uint8_t received_motor_id = std_id & 0x7;

            // TODO: Проверить на соответствие CMD_PERFORMER_ID_SET для широковещания
            if (received_performer_id != g_performer_id && received_performer_id != 0xF) { // 0xF может быть Broadcast
                 continue;
            }

            parsed_command.motor_id = received_motor_id;
            parsed_command.command_id = (CommandID_t)rx_frame.data[0];

            parsed_command.payload = (int32_t)rx_frame.data[1] |
                                     ((int32_t)rx_frame.data[2] << 8) |
                                     ((int32_t)rx_frame.data[3] << 16) |
                                     ((int32_t)rx_frame.data[4] << 24);

            osMessageQueuePut(parser_queueHandle, &parsed_command, 0, 0);
        }

        if (osMessageQueueGet(can_tx_queueHandle, &tx_frame, NULL, 0) == osOK) {
            if (HAL_CAN_AddTxMessage(&hcan, &tx_frame.header, tx_frame.data, &txMailbox) != HAL_OK) {
                // Обработка ошибки отправки
            }
        }

        osDelay(1);
    }
}
```

### 4.4. Реализация `Task_Command_Parser`
`App/src/tasks/task_command_parser.c`:
```c
#include "task_command_parser.h"
#include "main.h"
#include "cmsis_os.h"

#include "command_protocol.h"
#include "app_queues.h"
#include "app_config.h"
#include "app_globals.h"

void app_start_task_command_parser(void *argument)
{
    (void)argument;

    CAN_Command_t received_command;
    MotionCommand_t motion_cmd;
    CanTxFrame_t tx_response_frame;

    if (g_performer_id == 0xFF) { // Если ID еще не установлен (например, первое включение)
        g_performer_id = 0;       // Для тестирования, примем, что наш ID = 0
    }

    for(;;)
    {
        if (osMessageQueueGet(parser_queueHandle, &received_command, NULL, osWaitForever) == osOK)
        {
            if (received_command.motor_id != 0xFF && (received_command.motor_id >> 3) != g_performer_id) {
                continue;
            }

            switch (received_command.command_id)
            {
                case CMD_MOVE_ABSOLUTE:
                case CMD_MOVE_RELATIVE:
                case CMD_SET_SPEED:
                case CMD_SET_ACCELERATION:
                case CMD_STOP:
                    motion_cmd.motor_id = received_command.motor_id & 0x7;
                    motion_cmd.steps = received_command.payload;
                    motion_cmd.speed_steps_per_sec = motor_states[motion_cmd.motor_id].max_speed_steps_per_sec; // Пример
                    motion_cmd.acceleration_steps_per_sec2 = motor_states[motion_cmd.motor_id].acceleration_steps_per_sec2; // Пример
                    osMessageQueuePut(motion_queueHandle, &motion_cmd, 0, 0);
                    break;

                case CMD_GET_STATUS:
                case CMD_SET_CURRENT:
                case CMD_ENABLE_MOTOR:
                    osMessageQueuePut(tmc_manager_queueHandle, &received_command, 0, 0);
                    break;

                case CMD_PERFORMER_ID_SET:
                    g_performer_id = (uint8_t)received_command.payload;
                    // TODO: Реализовать запись в Flash здесь
                    tx_response_frame.header.StdId = 0x200 | (g_performer_id << 3) | 0xFF;
                    tx_response_frame.header.IDE = CAN_ID_STD;
                    tx_response_frame.header.RTR = CAN_RTR_DATA;
                    tx_response_frame.header.DLC = 2;
                    tx_response_frame.data[0] = CMD_PERFORMER_ID_SET;
                    tx_response_frame.data[1] = g_performer_id;
                    osMessageQueuePut(can_tx_queueHandle, &tx_response_frame, 0, 0);
                    break;

                default:
                    // Неизвестная команда
                    break;
            }
        }
        osDelay(1);
    }
}
```

### 4.5. Реализация `Task_TMC_Manager`
`App/src/tasks/task_tmc2209_manager.c`:
```c
#include "task_tmc2209_manager.h"
#include "main.h"
#include "cmsis_os.h"

#include "tmc2209_driver.h"
#include "app_config.h"
#include "app_queues.h"
#include "app_globals.h"

extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;

void app_start_task_tmc2209_manager(void *argument)
{
    (void)argument;

    osDelay(100);

    for (uint8_t i = 0; i < MOTOR_COUNT; i++)
    {
        UART_HandleTypeDef* huart_ptr;
        if (i < 4) huart_ptr = &huart1;
        else huart_ptr = &huart2;
        
        uint8_t slave_addr = i % 4;

        TMC2209_Init(&tmc_drivers[i], huart_ptr, slave_addr);

        // Раскомментировано для первого теста
        TMC2209_SetMotorCurrent(&tmc_drivers[i], 70, 40); // 70% run, 40% hold
        TMC2209_SetMicrosteps(&tmc_drivers[i], 16); // 16 микрошагов
    }

    for(;;)
    {
        // Здесь будет обработка команд из tmc_manager_queue
        osDelay(1000);
    }
}
```

### 4.6. Реализация `Task_Motion_Controller` и его прерывания
`App/src/tasks/task_motion_controller.c`:
```c
#include "task_motion_controller.h"
#include "main.h"
#include "cmsis_os.h"

#include "app_queues.h"
#include "app_globals.h"
#include "app_config.h"
#include "motion_planner.h"

extern TIM_HandleTypeDef htim2;

void Motor_SetDirection(uint8_t motor_id, uint8_t direction);
void Motor_Enable(uint8_t motor_id);

void app_start_task_motion_controller(void *argument)
{
    (void)argument;

    MotionCommand_t received_motion_cmd;

    for(uint8_t i = 0; i < MOTOR_COUNT; i++) {
        MotionPlanner_InitMotorState(&motor_states[i], 0);
    }

    for(;;)
    {
        if (osMessageQueueGet(motion_queueHandle, &received_motion_cmd, NULL, osWaitForever) == osOK)
        {
            if (g_active_motor_id != -1) {
                continue;
            }

            uint8_t motor_id = received_motion_cmd.motor_id;

            if (motor_id >= MOTOR_COUNT) {
                continue;
            }

            int32_t target_pos = motor_states[motor_id].current_position + received_motion_cmd.steps;
            int32_t steps_to_go = MotionPlanner_CalculateNewTarget(&motor_states[motor_id], target_pos);

            if (steps_to_go == 0) {
                continue;
            }

            Motor_SetDirection(motor_id, motor_states[motor_id].direction);
            Motor_Enable(motor_id);

            g_active_motor_id = motor_id;

            uint32_t initial_pulse_period = 1000000 / motor_states[motor_id].max_speed_steps_per_sec;

            htim2.Instance->PSC = (SystemCoreClock / 1000000) - 1;
            htim2.Instance->ARR = initial_pulse_period - 1;
            htim2.Instance->CNT = 0;

            HAL_TIM_OC_Start_IT(&htim2, TIM_CHANNEL_1);

            while (g_active_motor_id != -1) {
                osDelay(5);
            }
        }
    }
}
```
`Core/Src/stm32f1xx_it.c` (Callback `HAL_TIM_OC_DelayElapsedCallback`):
```c
// ... Инклюды и extern-объявления сверху ...
#include "main.h"
#include "cmsis_os.h"
#include "app_queues.h"
#include "motor_gpio.h"
#include "motion_planner.h"
#include "app_globals.h" // Для g_active_motor_id, motor_states, MOTOR_COUNT

extern TIM_HandleTypeDef htim2;

// ...

void HAL_TIM_OC_DelayElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM2)
    {
        if (g_active_motor_id == -1) {
            HAL_TIM_OC_Stop_IT(htim, TIM_CHANNEL_1);
            return;
        }

        uint8_t motor_id = g_active_motor_id;

        GPIOA->BSRR = STEP_PINS[motor_id];

        for (volatile int i = 0; i < 10; i++); // Простая задержка для формирования импульса

        GPIOA->BSRR = (uint32_t)STEP_PINS[motor_id] << 16U;

        uint32_t next_pulse_period = MotionPlanner_GetNextPulsePeriod(&motor_states[motor_id]);

        if (motor_states[motor_id].steps_to_go == 0)
        {
            HAL_TIM_OC_Stop_IT(htim, TIM_CHANNEL_1);
            Motor_Disable(motor_id);
            g_active_motor_id = -1;
            // TODO: Отправить сообщение в Task_Motion_Controller о завершении движения (в будущем)
        }
        else
        {
            __HAL_TIM_SET_AUTORELOAD(htim, next_pulse_period - 1);
        }
    }
}
```

## 5. План Первого Теста и дальнейшие шаги

### 5.1. Необходимое оборудование и подключение
*   Плата "Исполнителя" с микроконтроллером STM32F103 и драйверами TMC2209.
*   Подключенные шаговые двигатели.
*   Источник питания для платы и моторов.
*   CAN-USB адаптер, подключенный к ПК и к шине CAN "Исполнителя".
*   Программа для отправки CAN-сообщений (например, Busmaster, CAN-Hacker GUI).

### 5.2. Пример тестовой CAN-команды
Отправьте одно CAN-сообщение с ПК:
*   **ID:** `0x100` (Адрес для `ID_Исполнителя = 0`, `ID_Мотора = 0`).
*   **DLC:** `5` байт.
*   **Data:** `02 E8 03 00 00`
    *   `02`: `CMD_MOVE_RELATIVE`
    *   `E8 03 00 00`: Payload `1000` (0x03E8) в little-endian формате.
    (Команда: "Мотор 0, сделать 1000 шагов вперед")

### 5.3. Ожидаемый результат
*   Мотор №0 должен провернуться на определенный угол.

### 5.4. Последующие улучшения и доработки
После успешного первого теста, план развития включает:
*   Реализацию "правильной" синхронизации задачи/прерывания через FreeRTOS семафоры.
*   Полную реализацию планировщика движения с трапецеидальным профилем (ускорение/замедление).
*   Расширение логики для одновременного управления несколькими моторами.
*   Реализацию механизма провизионинга `Performer_ID` (запись в Flash и протокол присвоения).
*   Добавление обработки ошибок и отправки ответных CAN-сообщений.

---
Нажмите Enter, чтобы сохранить отчет в файл.
