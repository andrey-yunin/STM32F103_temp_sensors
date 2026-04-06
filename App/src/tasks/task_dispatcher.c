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
#include "app_flash.h"
#include "can_protocol.h"
#include "task_dispatcher.h"
#include "ds18b20.h"
#include <string.h>
#include "ds18b20.h"


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

	    	// ============================================================
	        // УНИВЕРСАЛЬНЫЕ СЕРВИСНЫЕ КОМАНДЫ (0xF0xx)
	        // ============================================================

	    	case CAN_CMD_SRV_GET_DEVICE_INFO: {
	    		uint8_t uid[12];
	    		uint8_t data[8];

	    		AppConfig_GetMCU_UID(uid);
	    		// Пакет 1: Метаданные + начало UID
	    	    data[0] = CAN_DEVICE_TYPE_THERMO;
	    	    data[1] = FW_REV_MAJOR;
	    	    data[2] = FW_REV_MINOR;
	    	    data[3] = DS18B20_MAX_SENSORS;
	    	    data[4] = uid[0];
	    	    data[5] = uid[1];
	    	    CAN_SendData(parsed.cmd_code, data, 6);

	    	    // Пакет 2: Середина UID
	    	    memcpy(data, &uid[2], 6);
	    	    CAN_SendData(parsed.cmd_code, data, 6);

	    	    // Пакет 3: Конец UID
	    	    memcpy(data, &uid[8], 4);
	    	    data[4] = 0; data[5] = 0;
	    	    CAN_SendData(parsed.cmd_code, data, 6);

	    	    CAN_SendDone(parsed.cmd_code, 0);
	    	    break;
	    	    }

	    	case CAN_CMD_SRV_REBOOT: {
	    		// Извлекаем Magic Key из параметров (байт 0-1 данных в ParsedCanCommand_t)
	    	    uint16_t key = (uint16_t)(parsed.data[0] | (parsed.data[1] << 8));
	    	    if (key == SRV_MAGIC_REBOOT) {
	    	    	CAN_SendDone(parsed.cmd_code, 0);
	    	    	osDelay(100); // Даем время на отправку CAN фрейма
	    	    	NVIC_SystemReset();
	    	    	}
	    	    else {
	    	    	CAN_SendNack(parsed.cmd_code, CAN_ERR_INVALID_KEY);
	    	    	}
	    	    break;
	    	    }

	    	case CAN_CMD_SRV_SET_NODE_ID: {
	    		// Изменение сетевого адреса (Node ID)
	    		// Ограничение: 0x02..0x7F, исключая адрес дирижера 0x10
	    		if (parsed.sensor_id >= 0x02 && parsed.sensor_id <= 0x7F && parsed.sensor_id != CAN_ADDR_CONDUCTOR) {
	    			AppConfig_SetPerformerID(parsed.sensor_id);
	    			CAN_SendDone(parsed.cmd_code, parsed.sensor_id);
	    			}
	    		else
	    			{
	    			CAN_SendNack(parsed.cmd_code, CAN_ERR_INVALID_PARAM);
	    			}
	    		break;
	    		}

	    	case CAN_CMD_SRV_FLASH_COMMIT: {
	    		if (AppConfig_Commit()) {
	    			CAN_SendDone(parsed.cmd_code, 0);
	    			}
	    		else {
	    			CAN_SendNack(parsed.cmd_code, CAN_ERR_FLASH_WRITE);
	    			}
	    		break;
	    		}

	    	case CAN_CMD_SRV_FACTORY_RESET: {
	    		// Полная очистка Flash (приведет к сбросу на 0x40 при перезагрузке)
	    		uint16_t key = (uint16_t)(parsed.data[0] | (parsed.data[1] << 8));
	    		if (key == SRV_MAGIC_FACTORY_RESET) {
	    			AppConfig_FactoryReset();
	    			CAN_SendDone(parsed.cmd_code, 0);
	    			osDelay(100);
	    			NVIC_SystemReset();
	    			}
	    		else {
	    			CAN_SendNack(parsed.cmd_code, CAN_ERR_INVALID_KEY);
	    			}
	    		break;
	    		}


	    	// ============================================================
	    	// СЕРВИСНЫЕ КОМАНДЫ ТЕРМОДАТЧИКОВ (0xF1xx)
	    	// ============================================================

	    	case CAN_CMD_SRV_SCAN_1WIRE: {
	    		// Принудительный запуск сканирования шины
	    	    uint8_t count = DS18B20_Init();
	    	    uint8_t data[1];
	    	    data[0] = count;
	    	    CAN_SendData(parsed.cmd_code, data, 1);

	    	    CAN_SendDone(parsed.cmd_code, count);
	    	    break;

	    	    }

	    	case CAN_CMD_SRV_GET_PHYS_ID: {
	    		// Запрос 64-битного ID датчика по его порядковому номеру на шине
	    		DS18B20_ROM_t* rom = DS18B20_GetROM(parsed.sensor_id);
	    		if (rom != NULL) {
	    			uint8_t data[9];
	    			data[0] = parsed.sensor_id;
	    			memcpy(&data[1], rom->rom_code, 8);
	    			// Отправляем частями (так как CAN_SendData берет до 6 байт payload)
	    			CAN_SendData(parsed.cmd_code, data, 6);      // Индекс + 5 байт ROM
	    			CAN_SendData(parsed.cmd_code, &data[6], 3);  // Оставшиеся 3 байта ROM
	    			CAN_SendDone(parsed.cmd_code, parsed.sensor_id);
	    			}
	    		else {
	    			CAN_SendNack(parsed.cmd_code, CAN_ERR_INVALID_SENSOR_ID);
	    			}
	    		break;
	    		}

	    	case CAN_CMD_SRV_SET_CHANNEL_MAP: {
	    		// Привязка ROM ID к логическому каналу (logical_id берем из sensor_id)
	    	    // Сами 8 байт ROM ID приходят в поле data
	    		if (parsed.sensor_id < DS18B20_MAX_SENSORS && parsed.data_len >= 8) {
	    			DS18B20_ROM_t new_rom;
	    			memcpy(new_rom.rom_code, parsed.data, 8);
	    			AppConfig_SetSensorROM(parsed.sensor_id, &new_rom);
	    			CAN_SendDone(parsed.cmd_code, parsed.sensor_id);
	    			}
	    		else {
	    			CAN_SendNack(parsed.cmd_code, CAN_ERR_INVALID_SENSOR_ID);
	    			}
	    		break;
	    	}

	    	default:
	    		// Неизвестная команда (не реализована в данной прошивке)
	    		CAN_SendNack(parsed.cmd_code, CAN_ERR_UNKNOWN_CMD);
	    		break;
	    	}
	    }
}










