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
#include "app_globals.h"
#include "can_protocol.h"
#include <string.h>



void app_start_task_dispatcher(void *argument)
{
	 ParsedCanCommand_t parsed; // Буфер для принятой команды
	 for (;;) {
		 // Ожидаем команду из parser_queue (от Task_CAN_Handler)
		 if (osMessageQueueGet(parser_queueHandle, &parsed, NULL, osWaitForever) != osOK) {
			 continue;
			 }

		 // --- 1. Немедленное подтверждение (ACK) ---
		 CAN_SendAck(parsed.cmd_code);

	     // --- 2. Диспетчеризация команды ---
		 switch (parsed.cmd_code) {

		 	 case CAN_CMD_SENSOR_GET_TEMP: {
		 		 uint8_t sensor_index = parsed.sensor_id;
		 		 if (sensor_index < DS18B20_MAX_SENSORS) {
		 			 float raw_temp = g_latest_temperatures[sensor_index];
		 			 // Конвертация float -> int16_t (0.1°C resolution)
		 			 // Пример: 25.46 -> 255

		 			 int16_t tx_temp = (int16_t)(raw_temp * 10.0f);

		 			 // --- 3. Отправка данных (DATA) ---
		 			 uint8_t tx_data[2];
		 			 tx_data[0] = (uint8_t)(tx_temp & 0xFF);
		 			 tx_data[1] = (uint8_t)((tx_temp >> 8) & 0xFF);
		 			 CAN_SendData(parsed.cmd_code, tx_data, 2);

		 			 // --- 4. Завершение транзакции (DONE) ---
		 			 CAN_SendDone(parsed.cmd_code, sensor_index);
		 		 }
		 		 else {
		 			 // Ошибка: неверный индекс датчика
		 			 CAN_SendNack(parsed.cmd_code, CAN_ERR_INVALID_SENSOR_ID);
		 			 }
		 		 break;
		 		 }

		 	 case CAN_CMD_SENSOR_GET_ALL_TEMPS: {
		 		 // TODO: Реализовать циклическую отправку всех датчиков
		 		 CAN_SendDone(parsed.cmd_code, 0xFF);
		 		 break;
		 		 }

		 	 default:
		 		 // Неизвестная команда
		 		 CAN_SendNack(parsed.cmd_code, CAN_ERR_UNKNOWN_CMD);
		 		 break;
		 		 }
		 }
}










