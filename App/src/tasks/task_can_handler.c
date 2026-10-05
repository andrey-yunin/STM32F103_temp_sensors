/*
 * task_can_handler.c
 *
 *  Created on: Dec 8, 2025
 *      Author: andrey *
 *
 *   Транспортный уровень CAN. Отвечает за:
 *   - Приём CAN-фреймов, аппаратную фильтрацию, распаковку в ParsedCanCommand_t
 *   - Отправку CAN-фреймов из can_tx_queue в CAN-периферию
 *   - Event-driven обработку через osThreadFlags (FLAG_CAN_RX, FLAG_CAN_TX)
 */

#include "task_can_handler.h"
#include "main.h"           // Для HAL-функций, CAN_HandleTypeDef, UART_HandleTypeDef
#include "cmsis_os.h"       // Для osDelay, osMessageQueueXxx
#include "app_queues.h"     // Для хэндлов очередей
#include "app_config.h"     // Для CanRxFrame_t, CanTxFrame_t, CAN_DATA_MAX_LEN
#include "can_protocol.h"
#include "app_flash.h"
#include "task_watchdog.h"
#include <stdbool.h>
#include <string.h>

// --- Внешние хэндлы HAL ---
extern CAN_HandleTypeDef hcan; // Хэндл CAN-периферии из main.c
extern osThreadId_t task_can_handleHandle;

static volatile CanDiagnostics_t g_can_diag;

// --- Аппаратная фильтрация CAN ---

/*
 * Общий механизм HC: bank0 принимает broadcast COMMAND, bank1 — direct
 * COMMAND на текущий NodeID. Приоритет и источник не входят в маску.
 * Программная проверка принятых кадров сохраняется в CAN-задаче.
 * Фильтр задаётся в координатах bxCAN: Extended ID сдвигается на 3 бита.
 */
#define CAN_FILTER_MSGTYPE_DST_MASK (0x03FFUL << 19)
#define CAN_FILTER_IDE              (1UL << 2)

/* Настраивает один банк; ошибка HAL переводит плату в аварийный путь. */
static void CAN_ConfigureFilterBank(uint8_t bank, uint8_t destination) {
	CAN_FilterTypeDef filter_config;
	uint32_t filter_id;
	uint32_t filter_mask;
	uint32_t filter_reg;

	memset(&filter_config, 0, sizeof(filter_config));

	/*
	 * The filter accepts only COMMAND frames addressed to the selected
	 * destination. The source address remains unfiltered at hardware level.
	 */
	filter_id = CAN_BUILD_ID(0U, CAN_MSG_TYPE_COMMAND, destination, 0U);

	/*
	 * Both identifier and mask use bxCAN filter-register coordinates.
	 * The Extended CAN ID is shifted by three bits before being written.
	 */
	filter_reg = (filter_id << 3) | CAN_FILTER_IDE;
	filter_mask = CAN_FILTER_MSGTYPE_DST_MASK | CAN_FILTER_IDE;

	filter_config.FilterBank = bank;
	filter_config.FilterMode = CAN_FILTERMODE_IDMASK;
	filter_config.FilterScale = CAN_FILTERSCALE_32BIT;
	filter_config.FilterIdHigh = (uint16_t) (filter_reg >> 16);
	filter_config.FilterIdLow = (uint16_t) (filter_reg & 0xFFFFU);
	filter_config.FilterMaskIdHigh = (uint16_t) (filter_mask >> 16);
	filter_config.FilterMaskIdLow = (uint16_t) (filter_mask & 0xFFFFU);
	filter_config.FilterFIFOAssignment = CAN_RX_FIFO0;
	filter_config.FilterActivation = ENABLE;
	filter_config.SlaveStartFilterBank = 14U;

	if (HAL_CAN_ConfigFilter(&hcan, &filter_config) != HAL_OK) {
		Error_Handler();
	}
}

/* Обновляет только direct-банк при F005; broadcast-банк сохраняется. */
void CAN_UpdateDirectFilter(uint8_t destination) {
	CAN_ConfigureFilterBank(1U, destination);
}

// --- Программная проверка входящей команды ---

/*
 * Общий профиль HC: CAN-задача проверяет кадр до разбора payload и передачи
 * диспетчеру. Первое нарушение завершает проверку без ACK/NACK; только
 * IDE/DLC/type/dst увеличивают стандартные DROP-счётчики F007.
 * RTR и source отбрасываются без новых метрик. Broadcast проходит те же
 * проверки, что и direct; параметры команды проверяются прикладным слоем.
 */
static bool CAN_IsAcceptedCommand(const CanRxFrame_t *rx_frame) {
	uint8_t destination;
	uint8_t source;
	uint8_t node_id;

	if (rx_frame == NULL) {
		return false;
	}

	if (rx_frame->header.IDE != CAN_ID_EXT) {
		g_can_diag.dropped_not_ext++;
		return false;
	}

	if (rx_frame->header.RTR != CAN_RTR_DATA) {
		return false;
	}

	if (rx_frame->header.DLC != 8U) {
		g_can_diag.dropped_wrong_dlc++;
		return false;
	}

	if (CAN_GET_MSG_TYPE(rx_frame->header.ExtId) != CAN_MSG_TYPE_COMMAND) {
		g_can_diag.dropped_wrong_type++;
		return false;
	}

	destination = CAN_GET_DST_ADDR(rx_frame->header.ExtId);
	source = CAN_GET_SRC_ADDR(rx_frame->header.ExtId);
	node_id = (uint8_t) AppConfig_GetPerformerID();

	if (source != CAN_ADDR_CONDUCTOR) {
		return false;
	}

	if ((destination != node_id) && (destination != CAN_ADDR_BROADCAST)) {
		g_can_diag.dropped_wrong_dst++;
		return false;
	}

	return true;
}

// --- Снимок CAN-диагностики ---
// CAN-модуль копирует счётчики для F007 под запретом IRQ.
// После копирования восстанавливает исходный PRIMASK,
// сохраняя запрет, если его ранее установил вызывающий код.

void CAN_Diagnostics_GetSnapshot(CanDiagnostics_t *out) {
	uint32_t primask;

	if (out == NULL) {
		return;
	}

	primask = __get_PRIMASK();
	__disable_irq();

	memcpy(out, (const void*) &g_can_diag, sizeof(CanDiagnostics_t));

	__set_PRIMASK(primask);
}

void CAN_Diagnostics_RecordRxQueueOverflow(void) {
	g_can_diag.rx_queue_overflow++;
}

void CAN_Diagnostics_RecordAppQueueOverflow(void) {
	g_can_diag.app_queue_overflow++;
}

void CAN_Diagnostics_RecordCanError(uint32_t hal_error, uint32_t esr) {
	g_can_diag.can_error_callback_count++;
	g_can_diag.last_hal_error = hal_error;
	g_can_diag.last_esr = esr;

	if ((esr & CAN_ESR_EWGF) != 0U) {
		g_can_diag.error_warning_count++;
	}

	if ((esr & CAN_ESR_EPVF) != 0U) {
		g_can_diag.error_passive_count++;
	}

	if ((esr & CAN_ESR_BOFF) != 0U) {
		g_can_diag.bus_off_count++;
	}
}

static void CAN_QueueTxFrame(CanTxFrame_t *tx) {
	// Единая точка постановки исходящих CAN-кадров в очередь.
	// tx_total здесь не увеличиваем: физическая отправка выполняется ниже
	// через HAL_CAN_AddTxMessage(), как в образцах Motion/Fluidics.
	if (osMessageQueuePut(can_tx_queueHandle, tx, 0, 0) == osOK) {
		osThreadFlagsSet(task_can_handleHandle, FLAG_CAN_TX);
	} else {
		g_can_diag.tx_queue_overflow++;
	}
}

// ============================================================
// Вспомогательные функции (Response Helpers)
// ============================================================

void CAN_SendAck(uint16_t cmd_code) {
	CanTxFrame_t tx;
	tx.header.ExtId = CAN_BUILD_ID(CAN_PRIORITY_NORMAL, CAN_MSG_TYPE_ACK,
			CAN_ADDR_CONDUCTOR, AppConfig_GetPerformerID());
	tx.header.IDE = CAN_ID_EXT;
	tx.header.RTR = CAN_RTR_DATA;
	tx.header.DLC = 8; // Unified DLC=8
	tx.data[0] = (uint8_t) (cmd_code & 0xFF);
	tx.data[1] = (uint8_t) ((cmd_code >> 8) & 0xFF);
	for (uint8_t i = 2; i < 8; i++)
		tx.data[i] = 0x00;

	CAN_QueueTxFrame(&tx);

}

void CAN_SendNack(uint16_t cmd_code, uint16_t error_code) {
	CanTxFrame_t tx;
	tx.header.ExtId = CAN_BUILD_ID(CAN_PRIORITY_NORMAL, CAN_MSG_TYPE_NACK,
			CAN_ADDR_CONDUCTOR, AppConfig_GetPerformerID());
	tx.header.IDE = CAN_ID_EXT;
	tx.header.RTR = CAN_RTR_DATA;
	tx.header.DLC = 8; // Unified DLC=8
	tx.data[0] = (uint8_t) (cmd_code & 0xFF);
	tx.data[1] = (uint8_t) ((cmd_code >> 8) & 0xFF);
	tx.data[2] = (uint8_t) (error_code & 0xFF);
	tx.data[3] = (uint8_t) ((error_code >> 8) & 0xFF);
	for (uint8_t i = 4; i < 8; i++)
		tx.data[i] = 0x00;

	CAN_QueueTxFrame(&tx);

}

void CAN_SendDone(uint16_t cmd_code, uint8_t sensor_id) {
	CanTxFrame_t tx;
	tx.header.ExtId = CAN_BUILD_ID(CAN_PRIORITY_NORMAL,
			CAN_MSG_TYPE_DATA_DONE_LOG, CAN_ADDR_CONDUCTOR,
			AppConfig_GetPerformerID());
	tx.header.IDE = CAN_ID_EXT;
	tx.header.RTR = CAN_RTR_DATA;
	tx.header.DLC = 8; // Unified DLC=8
	tx.data[0] = CAN_SUB_TYPE_DONE;
	tx.data[1] = (uint8_t) (cmd_code & 0xFF);
	tx.data[2] = (uint8_t) ((cmd_code >> 8) & 0xFF);
	tx.data[3] = sensor_id;
	for (uint8_t i = 4; i < 8; i++)
		tx.data[i] = 0x00;

	CAN_QueueTxFrame(&tx);

}

// --- Постановка DATA в очередь передачи ---

/*
 * Общий механизм : формирует один DATA-кадр с явным sequence_info.
 * Payload копируется сразу, поэтому вызывающий код может использовать
 * локальный буфер. Неиспользованные байты заполнены нулями.
 * cmd_code не включается в DATA: команду определяет контекст транзакции.
 * Постановка в очередь сама по себе не подтверждает доставку получателю.
 */
void CAN_SendData(uint16_t cmd_code, uint8_t sequence_info, const uint8_t *data,
		uint8_t len) {
	CanTxFrame_t tx_frame;
	uint8_t copy_len;

	(void) cmd_code;

	memset(&tx_frame, 0, sizeof(tx_frame));

	copy_len = len;
	if (copy_len > CAN_DATA_PAYLOAD_MAX) {
		copy_len = CAN_DATA_PAYLOAD_MAX;
	}

	tx_frame.header.ExtId = CAN_BUILD_ID(CAN_PRIORITY_NORMAL,
			CAN_MSG_TYPE_DATA_DONE_LOG, CAN_ADDR_CONDUCTOR,
			AppConfig_GetPerformerID());

	tx_frame.header.IDE = CAN_ID_EXT;
	tx_frame.header.RTR = CAN_RTR_DATA;
	tx_frame.header.DLC = CAN_FRAME_DLC;

	tx_frame.data[0] = CAN_SUB_TYPE_DATA;
	tx_frame.data[1] = sequence_info;

	if ((data != NULL) && (copy_len > 0U)) {
		memcpy(&tx_frame.data[2], data, copy_len);
	}

	CAN_QueueTxFrame(&tx_frame);
}

// ============================================================
// Основная задача (Main Task Loop)
// ============================================================

void app_start_task_can_handler(void *argument) {

	CanRxFrame_t rx_frame;
	CanTxFrame_t tx_frame;
	uint32_t txMailbox;

	// --- Два банка: broadcast и текущий адрес из RAM-конфигурации ---
	/* Фильтры устанавливаются до запуска CAN и разрешения RX notifications. */
	CAN_ConfigureFilterBank(0U, CAN_ADDR_BROADCAST);
	CAN_ConfigureFilterBank(1U, (uint8_t) AppConfig_GetPerformerID());

	if (HAL_CAN_Start(&hcan) != HAL_OK)
		Error_Handler();
	if (HAL_CAN_ActivateNotification(&hcan,
	CAN_IT_RX_FIFO0_MSG_PENDING |
	CAN_IT_RX_FIFO0_FULL |
	CAN_IT_RX_FIFO0_OVERRUN |
	CAN_IT_ERROR_WARNING |
	CAN_IT_ERROR_PASSIVE |
	CAN_IT_BUSOFF |
	CAN_IT_LAST_ERROR_CODE |
	CAN_IT_ERROR) != HAL_OK)
		Error_Handler();

	// --- Интервал ожидания CAN-событий ---
	// CMSIS принимает время ожидания в тиках RTOS.
	// При текущей частоте 1000 Гц интервал 500 мс равен 500 тикам.
	const uint32_t idle_wait_ticks = (APP_WATCHDOG_TASK_IDLE_TIMEOUT_MS
			* osKernelGetTickFreq()) / 1000U;

	for (;;) {
		// --- Ожидание события или штатного таймаута ---
		// CAN-задача подтверждает прогресс только после штатного
		// возврата из ожидания. Ошибка API heartbeat не формирует.
		uint32_t flags = osThreadFlagsWait(
		FLAG_CAN_RX | FLAG_CAN_TX,
		osFlagsWaitAny, idle_wait_ticks);

		// Отсутствие трафика допустимо: задача работает и завершила ожидание.
		if (flags == osFlagsErrorTimeout) {
			AppWatchdog_Heartbeat(APP_WDG_CLIENT_CAN);
			continue;
		}

		// Прочие ошибки не подтверждают штатный прогресс задачи.
		if ((flags & osFlagsError) != 0U) {
			continue;
		}

		// --- Подтверждение обработки события ---
		// Если последующая обработка RX/TX зависнет, новых отметок не будет.
		AppWatchdog_Heartbeat(APP_WDG_CLIENT_CAN);

		// --- Обработка приема (RX) ---
		if (flags & FLAG_CAN_RX) {
			while (osMessageQueueGet(can_rx_queueHandle, &rx_frame, NULL, 0)
					== osOK) {
				// До чтения payload применяем общий программный профиль HC.
				if (!CAN_IsAcceptedCommand(&rx_frame)) {
					continue;
				}

				ParsedCanCommand_t parsed;
				memset(&parsed, 0, sizeof(parsed));

				parsed.cmd_code = (uint16_t) rx_frame.data[0]
						| ((uint16_t) rx_frame.data[1] << 8);

				parsed.sensor_id = rx_frame.data[2];
				parsed.data_len = 5U;

				for (uint8_t i = 0U; i < parsed.data_len; i++) {
					parsed.data[i] = rx_frame.data[3U + i];
				}

				if (osMessageQueuePut(parser_queueHandle, &parsed, 0, 0)
						== osOK) {
					g_can_diag.rx_total++;
				} else {
					g_can_diag.dispatcher_queue_overflow++;
				}
			}
		}

		// --- Обработка передачи (TX) ---
		if (flags & FLAG_CAN_TX) {
			while (osMessageQueueGet(can_tx_queueHandle, &tx_frame, NULL, 0)
					== osOK) {
				uint32_t tick_start = HAL_GetTick();
				while ((HAL_CAN_GetTxMailboxesFreeLevel(&hcan) == 0U)
						&& ((HAL_GetTick() - tick_start) < 10U)) {
					osDelay(1);
				}

				if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) > 0U) {
					if (HAL_CAN_AddTxMessage(&hcan, &tx_frame.header,
							tx_frame.data, &txMailbox) == HAL_OK) {
						g_can_diag.tx_total++;
					} else {
						g_can_diag.tx_hal_error++;
						g_can_diag.last_hal_error = HAL_CAN_GetError(&hcan);
						g_can_diag.last_esr = hcan.Instance->ESR;
					}
				} else {
					g_can_diag.tx_mailbox_timeout++;
				}
			}
		}
	}
}

