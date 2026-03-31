/*
 * app_config.h
 *
 *  Created on: Dec 9, 2025
 *      Author: andrey
 */

#ifndef APP_CONFIG_H_
#define APP_CONFIG_H_

#include"main.h"


/* --- RTOS Thread Flags (Event-Driven CAN) --- */
#define FLAG_CAN_RX             0x01
#define FLAG_CAN_TX             0x02


// =============================================================================
//                             ОБЩИЕ НАСТРОЙКИ ПРИЛОЖЕНИЯ
// =============================================================================

#define CAN_DATA_MAX_LEN            8 // Максимальная длина поля данных CAN-фрейма (в байтах)

#define DS18B20_MAX_SENSORS         8 // Количество датчиков DS18B20 на шине


// =============================================================================
//                             НАСТРОЙКИ ОЧЕРЕДЕЙ FREERTOS
// =============================================================================


/* --- CAN Network Addresses (DDS-240 Standard) --- */
#define CAN_ADDR_CONDUCTOR      0x10  // Адрес Дирижера (Материнская плата)
#define CAN_ADDR_THERMO_BOARD   0x40  // Адрес этого модуля (Исполнитель)


// -- ДЛИНА ОЧЕРЕДЕЙ (количество элементов) --

// Очередь для приема сырых CAN-фреймов
#define CAN_RX_QUEUE_LEN            10

// Очередь для отправки CAN-фреймов
#define CAN_TX_QUEUE_LEN            10

// Очередь для передачи команд парсеру
#define DISPATCHER_QUEUE_LEN        10


// Структура для хранения полного Rx CAN-фрейма (header + data)
typedef struct {
	CAN_RxHeaderTypeDef header;
    uint8_t data[CAN_DATA_MAX_LEN];
    } CanRxFrame_t;

    // Структура для хранения полного Tx CAN-фрейма (header + data)
typedef struct {
	CAN_TxHeaderTypeDef header;
	uint8_t data[CAN_DATA_MAX_LEN];
    } CanTxFrame_t;


#endif // APP_CONFIG_H




