/*
 * task_command_parser.c
 *
 *  Created on: Dec 8, 2025
 *      Author: andrey
 *
 *    Прикладной уровень. Отвечает за:
 *  - Приём ParsedCanCommand_t из parser_queue (от CAN Handler)
 *  - Валидацию параметров (sensor_id)
 *  - Формирование ответов по жизненному циклу (ACK -> DATA -> DONE)
 *
 *
 */

#include "main.h"             // Для HAL-функций
#include "cmsis_os.h"         // Для osDelay, osMessageQueueXxx
#include "app_queues.h"       // Для хэндлов очередей
#include "app_config.h"       // Для MotionCommand_t
#include "can_protocol.h"
#include "task_dispatcher.h"
#include <string.h>



void app_start_task_dispatcher(void *argument)
{
	ParsedCanCommand_t parsed;

	for (;;) {
		// Ожидаем команду из очереди (parser_queue), наполняемой в task_can_handler
	    if (osMessageQueueGet(parser_queueHandle, &parsed, NULL, osWaitForever) != osOK) {
	    	continue;
	    	}

	    // --- 1. Немедленное подтверждение получения команды (ACK) ---
	    CAN_SendAck(parsed.cmd_code);

	    // --- 2. Диспетчеризация по коду команды ---
	    switch (parsed.cmd_code) {

	    	case CAN_CMD_SENSOR_GET_TEMP: {
	    		uint8_t idx = parsed.sensor_id;
	    		if (idx < DS18B20_MAX_SENSORS) {
	    			// Читаем значение из защищенного хранилища монитора
	    			float raw_t = TempMonitor_GetTemperature(idx);
	    			if (raw_t > -100.0f) { // Датчик в сети и данные валидны
	    				// Конвертация: float -> int16_t (0.1°C precision)
	    				int16_t tx_val = (int16_t)(raw_t * 10.0f);
	    				uint8_t data[2];
	    				data[0] = (uint8_t)(tx_val & 0xFF);
	    				data[1] = (uint8_t)((tx_val >> 8) & 0xFF);

	    				// --- 3. Отправка полезных данных (DATA) ---
	    				CAN_SendData(parsed.cmd_code, data, 2);

	    				// --- 4. Завершение успешной транзакции (DONE) ---
	    				CAN_SendDone(parsed.cmd_code, idx);
	    				}
	    			else {
	    				// Датчик не ответил в цикле мониторинга
	                    CAN_SendNack(parsed.cmd_code, CAN_ERR_SENSOR_FAILURE);
	                    }
	    			}
	    		else {
	    			// Запрошен индекс за пределами массива (например, 25)
	    			CAN_SendNack(parsed.cmd_code, CAN_ERR_INVALID_SENSOR_ID);
	    			}
	    		break;
	    		}

	    	case CAN_CMD_SENSOR_GET_ALL_TEMPS: {
	    		// Пакетная отправка данных по всем активным датчикам
	    		for (uint8_t i = 0; i < DS18B20_MAX_SENSORS; i++) {
	    			float t = TempMonitor_GetTemperature(i);
	    			if (t > -100.0f) {
	    				int16_t tx_v = (int16_t)(t * 10.0f);
	    				uint8_t d[3];
	    				d[0] = i; // Префикс данных: индекс датчика
	    				d[1] = (uint8_t)(tx_v & 0xFF);
	    				d[2] = (uint8_t)((tx_v >> 8) & 0xFF);
	    				CAN_SendData(parsed.cmd_code, d, 3);
	    				}
	    			}
	    		CAN_SendDone(parsed.cmd_code, 0xFF); // 0xFF - маркер "Все датчики"
	    		break;
	    		}

	    	default:
	    		// Неизвестная команда (не реализована в данной прошивке)
	    		CAN_SendNack(parsed.cmd_code, CAN_ERR_UNKNOWN_CMD);
	    		break;
	    	}
	    }
}










