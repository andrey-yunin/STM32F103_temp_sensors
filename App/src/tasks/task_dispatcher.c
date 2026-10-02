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
#include "app_safety.h"
#include "can_protocol.h"
#include "task_dispatcher.h"
#include "task_can_handler.h"
#include "task_watchdog.h"
#include <string.h>

// --- Проверка нового адреса платы ---

/*
 * Dispatcher допускает F005 только внутри адресной группы Thermo.
 * Функция не изменяет конфигурацию и не проверяет занятость адреса
 * другой платой: уникальность обеспечивается при настройке системы.
 */
static bool Dispatcher_IsValidNodeId(uint8_t node_id) {
	return (node_id >= CAN_ADDR_THERMO_BOARD_BASE)
			&& (node_id <= CAN_ADDR_THERMO_BOARD_LAST);
}

// --- Отправка одной метрики F007 ---

/*
 * Dispatcher формирует самостоятельную запись metric_id + value.
 * Каждая метрика помещается в один DATA с sequence_info=0x80.
 * Завершение всего ответа DONE выполняет обработчик F007.
 */
static void SendStatusMetric(uint16_t cmd_code, uint16_t metric_id,
		uint32_t value) {
	uint8_t data[CAN_DATA_PAYLOAD_MAX];

	data[0] = (uint8_t) (metric_id & 0xFFU);
	data[1] = (uint8_t) ((metric_id >> 8U) & 0xFFU);
	data[2] = (uint8_t) (value & 0xFFU);
	data[3] = (uint8_t) ((value >> 8U) & 0xFFU);
	data[4] = (uint8_t) ((value >> 16U) & 0xFFU);
	data[5] = (uint8_t) ((value >> 24U) & 0xFFU);

	CAN_SendData(cmd_code, CAN_DATA_SEQ_EOT_MASK, data, sizeof(data));
}

static void EnqueueThermoCommand(const ParsedCanCommand_t *parsed) {
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

void app_start_task_dispatcher(void *argument) {
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

			// --- F001: информация об устройстве ---

			/*
			 * Dispatcher передаёт 16 байт: метаданные и полный UID MCU.
			 * Фрагментация по HC: 00, 01, 82, затем DONE.
			 * Compact ID датчиков DS18B20 к UID микроконтроллера не относится.
			 */
		case CAN_CMD_SRV_GET_DEVICE_INFO: {
			uint8_t uid[12];
			uint8_t data[CAN_DATA_PAYLOAD_MAX];

			AppConfig_GetMCU_UID(uid);

			data[0] = CAN_DEVICE_TYPE_THERMO;
			data[1] = FW_REV_MAJOR;
			data[2] = FW_REV_MINOR;
			data[3] = DS18B20_MAX_SENSORS;
			data[4] = uid[0];
			data[5] = uid[1];

			CAN_SendData(parsed.cmd_code, 0x00U, data, sizeof(data));

			memcpy(data, &uid[2], sizeof(data));
			CAN_SendData(parsed.cmd_code, 0x01U, data, sizeof(data));

			memset(data, 0, sizeof(data));
			memcpy(data, &uid[8], 4U);
			CAN_SendData(parsed.cmd_code,
					(uint8_t) (CAN_DATA_SEQ_EOT_MASK | 0x02U), data,
					sizeof(data));

			CAN_SendDone(parsed.cmd_code, 0U);
			break;
		}

			// --- F004: полный UID микроконтроллера ---

			/*
			 * 12 байт UID передаются двумя фрагментами по шесть байт.
			 * Последовательность по HC: 00, 81, затем DONE.
			 */
		case CAN_CMD_SRV_GET_UID: {
			uint8_t uid[12];

			AppConfig_GetMCU_UID(uid);

			CAN_SendData(parsed.cmd_code, 0x00U, &uid[0], CAN_DATA_PAYLOAD_MAX);
			CAN_SendData(parsed.cmd_code,
					(uint8_t) (CAN_DATA_SEQ_EOT_MASK | 0x01U), &uid[6],
					CAN_DATA_PAYLOAD_MAX);

			CAN_SendDone(parsed.cmd_code, 0U);
			break;
		}

		case CAN_CMD_SRV_GET_STATUS: {
			CanDiagnostics_t diag;

			// Берем единый снимок счетчиков.
			// Все DATA кадры ниже относятся к одному состоянию диагностики.
			CAN_Diagnostics_GetSnapshot(&diag);

			SendStatusMetric(parsed.cmd_code, CAN_STATUS_RX_TOTAL,
					diag.rx_total);
			SendStatusMetric(parsed.cmd_code, CAN_STATUS_TX_TOTAL,
					diag.tx_total);
			SendStatusMetric(parsed.cmd_code, CAN_STATUS_RX_QUEUE_OVERFLOW,
					diag.rx_queue_overflow);
			SendStatusMetric(parsed.cmd_code, CAN_STATUS_TX_QUEUE_OVERFLOW,
					diag.tx_queue_overflow);
			SendStatusMetric(parsed.cmd_code, CAN_STATUS_DISPATCHER_OVERFLOW,
					diag.dispatcher_queue_overflow);
			SendStatusMetric(parsed.cmd_code, CAN_STATUS_DROP_NOT_EXT,
					diag.dropped_not_ext);
			SendStatusMetric(parsed.cmd_code, CAN_STATUS_DROP_WRONG_DST,
					diag.dropped_wrong_dst);
			SendStatusMetric(parsed.cmd_code, CAN_STATUS_DROP_WRONG_TYPE,
					diag.dropped_wrong_type);
			SendStatusMetric(parsed.cmd_code, CAN_STATUS_DROP_WRONG_DLC,
					diag.dropped_wrong_dlc);
			SendStatusMetric(parsed.cmd_code, CAN_STATUS_TX_MAILBOX_TIMEOUT,
					diag.tx_mailbox_timeout);
			SendStatusMetric(parsed.cmd_code, CAN_STATUS_TX_HAL_ERROR,
					diag.tx_hal_error);
			SendStatusMetric(parsed.cmd_code, CAN_STATUS_ERROR_CALLBACK,
					diag.can_error_callback_count);
			SendStatusMetric(parsed.cmd_code, CAN_STATUS_ERROR_WARNING,
					diag.error_warning_count);
			SendStatusMetric(parsed.cmd_code, CAN_STATUS_ERROR_PASSIVE,
					diag.error_passive_count);
			SendStatusMetric(parsed.cmd_code, CAN_STATUS_BUS_OFF,
					diag.bus_off_count);
			SendStatusMetric(parsed.cmd_code, CAN_STATUS_LAST_HAL_ERROR,
					diag.last_hal_error);
			SendStatusMetric(parsed.cmd_code, CAN_STATUS_LAST_ESR,
					diag.last_esr);
			SendStatusMetric(parsed.cmd_code, CAN_STATUS_APP_QUEUE_OVERFLOW,
					diag.app_queue_overflow);

			CAN_SendDone(parsed.cmd_code, 0);
			break;
		}

		case CAN_CMD_SRV_REBOOT: {
			if (parsed.data_len < 2U) {
				CAN_SendNack(parsed.cmd_code, CAN_ERR_INVALID_KEY);
				break;
			}
			// Извлекаем Magic Key из параметров (байт 0-1 данных в ParsedCanCommand_t)
			uint16_t key = (uint16_t) (parsed.data[0] | (parsed.data[1] << 8));
			if (key == SRV_MAGIC_REBOOT) {
				/* По HC: запрет домена до ответа, CAN/RTOS продолжают работу. */
				AppSafety_PrepareReset();
				CAN_SendDone(parsed.cmd_code, 0);
				/* Окно отправки, не подтверждение доставки. */
				if (osDelay(100U) != osOK) {
					Error_Handler();
				}
				NVIC_SystemReset();
			} else {
				CAN_SendNack(parsed.cmd_code, CAN_ERR_INVALID_KEY);
			}
			break;
		}

			// --- F005: изменение CAN-адреса платы Thermo ---

			/*
			 * Проверка выполняется до изменения RAM и direct-фильтра.
			 * Допустимый адрес применяется сразу; сохранение во Flash
			 * выполняется отдельной командой F003.
			 */
		case CAN_CMD_SRV_SET_NODE_ID: {
			if (!Dispatcher_IsValidNodeId(parsed.sensor_id)) {
				CAN_SendNack(parsed.cmd_code, CAN_ERR_INVALID_PARAM);
				break;
			}

			AppConfig_SetPerformerID(parsed.sensor_id);

			/*
			 * Перестраиваем bank1 до DONE.
			 * Broadcast-фильтр bank0 сохраняется.
			 */
			CAN_UpdateDirectFilter(parsed.sensor_id);

			CAN_SendDone(parsed.cmd_code, parsed.sensor_id);
			break;
		}

		case CAN_CMD_SRV_FLASH_COMMIT: {
			if (AppConfig_Commit()) {
				CAN_SendDone(parsed.cmd_code, 0);
			} else {
				CAN_SendNack(parsed.cmd_code, CAN_ERR_FLASH_WRITE);
			}
			break;
		}

			// --- F006: сброс сохранённой конфигурации ---

			/*
			 * Dispatcher проверяет ключ до обращения к Flash.
			 * Результат стирания определяет DONE или NACK.
			 * После попытки выполняется reset, как в HC.
			 * Запрет домена устанавливается до стирания и остаётся до reset.
			 */
		case CAN_CMD_SRV_FACTORY_RESET: {
			if (parsed.data_len < 2U) {
				CAN_SendNack(parsed.cmd_code, CAN_ERR_INVALID_KEY);
				break;
			}

			uint16_t key = (uint16_t) ((uint16_t) parsed.data[0]
					| ((uint16_t) parsed.data[1] << 8U));

			if (key != SRV_MAGIC_FACTORY_RESET) {
				CAN_SendNack(parsed.cmd_code, CAN_ERR_INVALID_KEY);
				break;
			}

			AppSafety_PrepareReset();
			if (AppConfig_FactoryReset()) {
				CAN_SendDone(parsed.cmd_code, 0U);
			} else {
				CAN_SendNack(parsed.cmd_code, CAN_ERR_FLASH_WRITE);
			}

			/*
			 * Сохраняем задержку перед reset.
			 * Она даёт транспорту время, но не подтверждает доставку ответа.
			 */
			if (osDelay(100U) != osOK) {
				Error_Handler();
			}

			NVIC_SystemReset();
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
