/*
 * task_command_parser.c
 *
 *  Created on: Dec 8, 2025
 *      Author: andrey
 *
 *    Прикладной уровень. Отвечает за:
 *  - Приём ParsedCanCommand_t из parser_queue (от CAN Handler)
 *  - Валидацию параметров (sensor_id)
 *  - Маршрутизация Thermo-команд в thermo_queueHandle
 *  - Формирование универсальных сервисных ответов (ACK -> DATA -> DONE)
 *
 *
 */

#include "main.h"             // Для HAL-функций
#include "cmsis_os.h"         // Для osDelay, osMessageQueueXxx
#include "app_queues.h"       // Для хэндлов очередей
#include "app_config.h"       // Для ParsedCanCommand_t / ThermoCommand_t
#include "app_flash.h"
#include "can_protocol.h"
#include "task_dispatcher.h"
#include "task_can_handler.h"
#include "task_watchdog.h"
#include <string.h>

static void SendStatusMetric(uint16_t cmd_code, uint16_t metric_id, uint32_t value)
{
	uint8_t data[6];
	// Формат F007 GET_STATUS:
	// metric_id:uint16 LE + value:uint32 LE.
	data[0] = (uint8_t)(metric_id & 0xFF);
	data[1] = (uint8_t)((metric_id >> 8) & 0xFF);
	data[2] = (uint8_t)(value & 0xFF);
	data[3] = (uint8_t)((value >> 8) & 0xFF);
	data[4] = (uint8_t)((value >> 16) & 0xFF);
	data[5] = (uint8_t)((value >> 24) & 0xFF);

	CAN_SendData(cmd_code, data, 6);
}

static void EnqueueThermoCommand(const ParsedCanCommand_t *parsed)
{
	ThermoCommand_t thermo_cmd;
	memset(&thermo_cmd, 0, sizeof(thermo_cmd));

	// Dispatcher не работает с one-wire напрямую.
	// Он передает валидную прикладную команду задаче Temp Monitor,
	// которая является единственным владельцем DS18B20 bus операций.
	thermo_cmd.cmd_code = parsed->cmd_code;
	thermo_cmd.sensor_id = parsed->sensor_id;
	thermo_cmd.data_len = parsed->data_len;
	memcpy(thermo_cmd.data, parsed->data, sizeof(thermo_cmd.data));

	if (osMessageQueuePut(thermo_queueHandle, &thermo_cmd, 0, 0) != osOK) {
		CAN_Diagnostics_RecordAppQueueOverflow();
		CAN_SendNack(parsed->cmd_code, CAN_ERR_BUSY);
	}
}




void app_start_task_dispatcher(void *argument)
{
	ParsedCanCommand_t parsed;

	for (;;) {
		// Ожидаем команду из очереди (parser_queue), наполняемой в task_can_handler
	    /*
	     * Dispatcher жив и готов принимать валидные команды от CAN task.
	     */
		 AppWatchdog_Heartbeat(APP_WDG_CLIENT_DISPATCHER);

	    if (osMessageQueueGet(parser_queueHandle, &parsed, NULL,
			              APP_WATCHDOG_TASK_IDLE_TIMEOUT_MS) != osOK) {
		continue;
		}

	    /*
	     * Команда получена, Dispatcher реально продвинулся к прикладной обработке.
	     */
	    AppWatchdog_Heartbeat(APP_WDG_CLIENT_DISPATCHER);


	    // --- 1. Немедленное подтверждение получения команды (ACK) ---
	    CAN_SendAck(parsed.cmd_code);

	    // --- 2. Диспетчеризация по коду команды ---
	    switch (parsed.cmd_code) {

		case CAN_CMD_SENSOR_GET_TEMP: {
			if (parsed.sensor_id >= DS18B20_MAX_SENSORS) {
				CAN_SendNack(parsed.cmd_code, CAN_ERR_INVALID_SENSOR_ID);
				break;
			}

			EnqueueThermoCommand(&parsed);
			break;
			}

		case CAN_CMD_SENSOR_GET_ALL_TEMPS: {
			EnqueueThermoCommand(&parsed);
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

		case CAN_CMD_SRV_GET_UID: {
			uint8_t uid[12];
			uint8_t data[6];

			// F004 возвращает полный 96-bit UID STM32.
			// В один DATA frame помещается 6 байт полезной нагрузки,
			// поэтому UID передается двумя DATA кадрами по 6 байт.
			AppConfig_GetMCU_UID(uid);

			memcpy(data, &uid[0], 6);
			CAN_SendData(parsed.cmd_code, data, 6);

			memcpy(data, &uid[6], 6);
			CAN_SendData(parsed.cmd_code, data, 6);

			CAN_SendDone(parsed.cmd_code, 0);
			break;
			}

		case CAN_CMD_SRV_GET_STATUS: {
			CanDiagnostics_t diag;

			// Берем единый снимок счетчиков.
			// Все DATA кадры ниже относятся к одному состоянию диагностики.
			CAN_Diagnostics_GetSnapshot(&diag);

			SendStatusMetric(parsed.cmd_code, CAN_STATUS_RX_TOTAL, diag.rx_total);
			SendStatusMetric(parsed.cmd_code, CAN_STATUS_TX_TOTAL, diag.tx_total);
			SendStatusMetric(parsed.cmd_code, CAN_STATUS_RX_QUEUE_OVERFLOW, diag.rx_queue_overflow);
			SendStatusMetric(parsed.cmd_code, CAN_STATUS_TX_QUEUE_OVERFLOW, diag.tx_queue_overflow);
			SendStatusMetric(parsed.cmd_code, CAN_STATUS_DISPATCHER_OVERFLOW, diag.dispatcher_queue_overflow);
			SendStatusMetric(parsed.cmd_code, CAN_STATUS_DROP_NOT_EXT, diag.dropped_not_ext);
			SendStatusMetric(parsed.cmd_code, CAN_STATUS_DROP_WRONG_DST, diag.dropped_wrong_dst);
			SendStatusMetric(parsed.cmd_code, CAN_STATUS_DROP_WRONG_TYPE, diag.dropped_wrong_type);
			SendStatusMetric(parsed.cmd_code, CAN_STATUS_DROP_WRONG_DLC, diag.dropped_wrong_dlc);
			SendStatusMetric(parsed.cmd_code, CAN_STATUS_TX_MAILBOX_TIMEOUT, diag.tx_mailbox_timeout);
			SendStatusMetric(parsed.cmd_code, CAN_STATUS_TX_HAL_ERROR, diag.tx_hal_error);
			SendStatusMetric(parsed.cmd_code, CAN_STATUS_ERROR_CALLBACK, diag.can_error_callback_count);
			SendStatusMetric(parsed.cmd_code, CAN_STATUS_ERROR_WARNING, diag.error_warning_count);
			SendStatusMetric(parsed.cmd_code, CAN_STATUS_ERROR_PASSIVE, diag.error_passive_count);
			SendStatusMetric(parsed.cmd_code, CAN_STATUS_BUS_OFF, diag.bus_off_count);
			SendStatusMetric(parsed.cmd_code, CAN_STATUS_LAST_HAL_ERROR, diag.last_hal_error);
			SendStatusMetric(parsed.cmd_code, CAN_STATUS_LAST_ESR, diag.last_esr);
			SendStatusMetric(parsed.cmd_code, CAN_STATUS_APP_QUEUE_OVERFLOW, diag.app_queue_overflow);

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
			EnqueueThermoCommand(&parsed);
		    break;
		    }

		case CAN_CMD_SRV_GET_PHYS_ID: {
			if (parsed.sensor_id >= DS18B20_MAX_SENSORS) {
				CAN_SendNack(parsed.cmd_code, CAN_ERR_INVALID_SENSOR_ID);
				break;
				}

			EnqueueThermoCommand(&parsed);
			break;
			}

		case CAN_CMD_SRV_SET_CHANNEL_MAP: {
			if (parsed.sensor_id >= DS18B20_MAX_SENSORS) {
				CAN_SendNack(parsed.cmd_code, CAN_ERR_INVALID_SENSOR_ID);
				break;
				}

			EnqueueThermoCommand(&parsed);
			break;
		}

		case CAN_CMD_SRV_GET_CHANNEL_MAP: {
			if (parsed.sensor_id >= DS18B20_MAX_SENSORS) {
				CAN_SendNack(parsed.cmd_code, CAN_ERR_INVALID_SENSOR_ID);
				break;
				}

			EnqueueThermoCommand(&parsed);
			break;
			}

		case CAN_CMD_SRV_SET_CH_MAP_P2: {
			if (parsed.sensor_id >= DS18B20_MAX_SENSORS) {
				CAN_SendNack(parsed.cmd_code, CAN_ERR_INVALID_SENSOR_ID);
				break;
				}

			EnqueueThermoCommand(&parsed);
			break;
		}

		default:
			// Неизвестная команда (не реализована в данной прошивке)
			CAN_SendNack(parsed.cmd_code, CAN_ERR_UNKNOWN_CMD);
			break;
		}
	    }
}







