/*
 * task_command_parser.c
 *
 *  Created on: Dec 8, 2025
 *      Author: andrey
 */

#include "task_dispatcher.h"
#include "main.h"             // Для HAL-функций
#include "cmsis_os.h"         // Для osDelay, osMessageQueueXxx
#include "command_protocol.h" // Для CAN_Command_t, CommandID_t
#include "app_queues.h"       // Для хэндлов очередей
#include "app_config.h"       // Для MotionCommand_t
#include "app_globals.h"
#include <stdbool.h>




void app_start_task_dispatcher(void *argument)
{
	CAN_Command_t received_command; // Буфер для принятой команды от CAN_Handler
	CanTxFrame_t tx_response_frame; // Буфер для отправки ответов

    // --- Логика определения ID исполнителя (пока заглушка) ---
	// В будущем здесь будет чтение ID из Flash.
	// Пока для тестирования, примем, что наш ID = 0, если он еще не установлен
	if (g_performer_id == 0xFF) {
		g_performer_id = 2;
		}


	// Бесконечный цикл задачи
	for(;;)
		{
		 // 1. Ждем команду из очереди parser_queue (от Task_CAN_Handler)
		 if (osMessageQueueGet(dispatcher_queueHandle, &received_command, NULL, osWaitForever) == osOK){

		        // --- 3. Диспетчеризация команды ---
		 	    switch (received_command.command_id){
		 	        // >>> НАЧАЛО ДОБАВЛЕННОЙ КОМАНДЫ ДЛЯ ДАТЧИКОВ ТЕМПЕРАТУРЫ <<<
		 	        case CMD_GET_TEMPERATURE:
		 	        	{
		 	        		// Payload: Index of the sensor (0 to DS18B20_MAX_SENSORS - 1)
		 	        		uint8_t sensor_index = (uint8_t)received_command.payload; // Используем payload для индекса датчика
		 	        		float temperature = 0.0f;
		 	        		//bool success = false; пока не используется
		 	        		if (sensor_index < DS18B20_MAX_SENSORS) {
		 	        			temperature = g_latest_temperatures[sensor_index];
		 	        			//success = true; пока не используется
		 	        			}
		 	        		// --- Отправляем ACK (подтверждение) с температурой ---
		 	        		// StdId ответа: 0x200 | ID исполнителя | индекс датчика
		 	        		tx_response_frame.header.StdId = 0x200 | (g_performer_id << 4) | sensor_index;
		 	        		tx_response_frame.header.IDE = CAN_ID_STD;
		 	                tx_response_frame.header.RTR = CAN_RTR_DATA;
		 	                tx_response_frame.header.DLC = 5; // Команда + 4 байта температуры (float)

		 	                tx_response_frame.data[0] = CMD_GET_TEMPERATURE; // Подтверждаем команду

		 	                // Преобразуем float в 4 байта для отправки.

		 	                uint32_t temp_as_uint32 = *((uint32_t*)&temperature);
		 	                tx_response_frame.data[1] = (uint8_t)(temp_as_uint32);
		 	                tx_response_frame.data[2] = (uint8_t)(temp_as_uint32 >> 8);
		 	                tx_response_frame.data[3] = (uint8_t)(temp_as_uint32 >> 16);
		 	                tx_response_frame.data[4] = (uint8_t)(temp_as_uint32 >> 24);

		 	                osMessageQueuePut(can_tx_queueHandle, &tx_response_frame, 0, 0);

		 	                }
		 	        	break;

		 	        	// <<< КОНЕЦ ДОБАВЛЕННЫХ КОМАНД >>>

		 	         // --- Специальные команды ---
		 	         case CMD_PERFORMER_ID_SET:
		 	         // Команда установки ID исполнителя (для провизионинга)
		 	         // payload содержит новый ID
		 	         g_performer_id = (uint8_t)received_command.payload;
		 	         // TODO: Реализовать запись в Flash здесь
		 	         // TODO: Отправить подтверждение через can_tx_queue

		 	         // Пример ответа:
		 	         tx_response_frame.header.StdId = 0x200 | (g_performer_id << 3) | 0xFF; // Ответ от исполнителя
		 	         tx_response_frame.header.IDE = CAN_ID_STD;
		 	         tx_response_frame.header.RTR = CAN_RTR_DATA;
		 	         tx_response_frame.header.DLC = 2;
		 	         tx_response_frame.data[0] = CMD_PERFORMER_ID_SET;
		 	         tx_response_frame.data[1] = g_performer_id; // Подтверждаем установленный ID
		 	         osMessageQueuePut(can_tx_queueHandle, &tx_response_frame, 0, 0);
		 	         break;

		 	         default:
		 	        	 // Неизвестная команда
		 	             // TODO: Отправить ошибку через can_tx_queue
		 	         break;
		 	            }
		      }
	    }
}

