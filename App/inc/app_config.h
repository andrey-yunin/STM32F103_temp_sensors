/*
 * app_config.h
 *
 *  Created on: Dec 9, 2025
 *      Author: andrey
 */

#ifndef APP_CONFIG_H_
#define APP_CONFIG_H_

#include"main.h"

// =============================================================================
//                             ОБЩИЕ НАСТРОЙКИ ПРИЛОЖЕНИЯ
// =============================================================================

#define CAN_DATA_MAX_LEN            8 // Максимальная длина поля данных CAN-фрейма (в байтах)



#define DS18B20_MAX_SENSORS         8 // Количество датчиков DS18B20 на шине


// =============================================================================
//                             НАСТРОЙКИ ОЧЕРЕДЕЙ FREERTOS
// =============================================================================

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




