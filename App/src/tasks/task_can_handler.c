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
#include <string.h>

// --- Внешние хэндлы HAL ---
extern CAN_HandleTypeDef hcan; // Хэндл CAN-периферии из main.c
extern osThreadId_t task_can_handleHandle;

static volatile CanDiagnostics_t g_can_diag;

void CAN_Diagnostics_GetSnapshot(CanDiagnostics_t *out)
{
	if (out == NULL) {
		return;
		}

	// Снимок нужен для F007 GET_STATUS.
  	// Копируем атомарно, потому что часть счетчиков позже будет обновляться
  	// из CAN callback/ISR path.
  	__disable_irq();
  	memcpy(out, (const void *)&g_can_diag, sizeof(CanDiagnostics_t));
  	__enable_irq();
}

void CAN_Diagnostics_RecordRxQueueOverflow(void)
{
	g_can_diag.rx_queue_overflow++;
}

void CAN_Diagnostics_RecordAppQueueOverflow(void)
{
	g_can_diag.app_queue_overflow++;
}

void CAN_Diagnostics_RecordCanError(uint32_t hal_error, uint32_t esr)
{
	g_can_diag.can_error_callback_count++;
    g_can_diag.last_hal_error = hal_error;
    g_can_diag.last_esr = esr;

    if ((esr & CAN_ESR_EWGF) != 0U)
    	{
    	g_can_diag.error_warning_count++;
    	}

    if ((esr & CAN_ESR_EPVF) != 0U)
    	{
    	g_can_diag.error_passive_count++;
    	}

    if ((esr & CAN_ESR_BOFF) != 0U)
    	{
    	g_can_diag.bus_off_count++;
    	}
}


static void CAN_QueueTxFrame(CanTxFrame_t *tx)
{
	// Единая точка постановки исходящих CAN-кадров в очередь.
	// tx_total здесь не увеличиваем: физическая отправка выполняется ниже
	// через HAL_CAN_AddTxMessage(), как в образцах Motion/Fluidics.
	if (osMessageQueuePut(can_tx_queueHandle, tx, 0, 0) == osOK)
	{
		osThreadFlagsSet(task_can_handleHandle, FLAG_CAN_TX);
	}
	else
	{
		g_can_diag.tx_queue_overflow++;
	}
}


// ============================================================
// Вспомогательные функции (Response Helpers)
// ============================================================

void CAN_SendAck(uint16_t cmd_code) {
	CanTxFrame_t tx;
    tx.header.ExtId = CAN_BUILD_ID(CAN_PRIORITY_NORMAL, CAN_MSG_TYPE_ACK, CAN_ADDR_CONDUCTOR, AppConfig_GetPerformerID());
    tx.header.IDE = CAN_ID_EXT;
    tx.header.RTR = CAN_RTR_DATA;
    tx.header.DLC = 8; // Unified DLC=8
    tx.data[0] = (uint8_t)(cmd_code & 0xFF);
    tx.data[1] = (uint8_t)((cmd_code >> 8) & 0xFF);
    for(uint8_t i = 2; i < 8; i++) tx.data[i] = 0x00;

    CAN_QueueTxFrame(&tx);

}

void CAN_SendNack(uint16_t cmd_code, uint16_t error_code) {
	CanTxFrame_t tx;
	tx.header.ExtId = CAN_BUILD_ID(CAN_PRIORITY_NORMAL, CAN_MSG_TYPE_NACK, CAN_ADDR_CONDUCTOR, AppConfig_GetPerformerID());
    tx.header.IDE = CAN_ID_EXT;
    tx.header.RTR = CAN_RTR_DATA;
    tx.header.DLC = 8; // Unified DLC=8
    tx.data[0] = (uint8_t)(cmd_code & 0xFF);
    tx.data[1] = (uint8_t)((cmd_code >> 8) & 0xFF);
    tx.data[2] = (uint8_t)(error_code & 0xFF);
    tx.data[3] = (uint8_t)((error_code >> 8) & 0xFF);
    for(uint8_t i = 4; i < 8; i++) tx.data[i] = 0x00;

    CAN_QueueTxFrame(&tx);

}

void CAN_SendDone(uint16_t cmd_code, uint8_t sensor_id) {
	CanTxFrame_t tx;
    tx.header.ExtId = CAN_BUILD_ID(CAN_PRIORITY_NORMAL, CAN_MSG_TYPE_DATA_DONE_LOG, CAN_ADDR_CONDUCTOR, AppConfig_GetPerformerID());
    tx.header.IDE = CAN_ID_EXT;
    tx.header.RTR = CAN_RTR_DATA;
    tx.header.DLC = 8; // Unified DLC=8
    tx.data[0] = CAN_SUB_TYPE_DONE;
    tx.data[1] = (uint8_t)(cmd_code & 0xFF);
    tx.data[2] = (uint8_t)((cmd_code >> 8) & 0xFF);
    tx.data[3] = sensor_id;
    for(uint8_t i = 4; i < 8; i++) tx.data[i] = 0x00;

    CAN_QueueTxFrame(&tx);

}

void CAN_SendData(uint16_t cmd_code, uint8_t *data, uint8_t len) {
	CanTxFrame_t tx;
    tx.header.ExtId = CAN_BUILD_ID(CAN_PRIORITY_NORMAL, CAN_MSG_TYPE_DATA_DONE_LOG, CAN_ADDR_CONDUCTOR, AppConfig_GetPerformerID());
    tx.header.IDE = CAN_ID_EXT;
    tx.header.RTR = CAN_RTR_DATA;
    tx.header.DLC = 8; // Unified DLC=8
    tx.data[0] = CAN_SUB_TYPE_DATA;
    tx.data[1] = 0x80; // Sequence Info: EOT=1, Seq=0 (для одиночных пакетов)

    for(uint8_t i = 0; i < 6; i++) {
        if (i < len) tx.data[2 + i] = data[i];
        else         tx.data[2 + i] = 0x00;
    	}

    CAN_QueueTxFrame(&tx);

}




// ============================================================
// Основная задача (Main Task Loop)
// ============================================================

void app_start_task_can_handler(void *argument) {

	CanRxFrame_t rx_frame;
    CanTxFrame_t tx_frame;
    uint32_t txMailbox;

    // --- Настройка CAN-фильтра (bxCAN Hardware Filter) ---
    // В экосистеме DDS-240 принимаем 29-bit Extended ID.
    // Фильтрация по NodeID и типу сообщения выполняется программно ниже.
    CAN_FilterTypeDef sFilterConfig;

    sFilterConfig.FilterBank = 0;
    sFilterConfig.FilterMode = CAN_FILTERMODE_IDMASK;
    sFilterConfig.FilterScale = CAN_FILTERSCALE_32BIT;
    sFilterConfig.FilterIdHigh = 0x0000;
    sFilterConfig.FilterIdLow = 0x0000 | (1 << 2); // IDE=1
    sFilterConfig.FilterMaskIdHigh = 0x0000;
    sFilterConfig.FilterMaskIdLow = 0x0000 | (1 << 2);
    sFilterConfig.FilterFIFOAssignment = CAN_RX_FIFO0;
    sFilterConfig.FilterActivation = ENABLE;
    sFilterConfig.SlaveStartFilterBank = 14;

    if (HAL_CAN_ConfigFilter(&hcan, &sFilterConfig) != HAL_OK) Error_Handler();
    if (HAL_CAN_Start(&hcan) != HAL_OK) Error_Handler();
    if (HAL_CAN_ActivateNotification(&hcan,
    		CAN_IT_RX_FIFO0_MSG_PENDING |
			CAN_IT_RX_FIFO0_FULL |
			CAN_IT_RX_FIFO0_OVERRUN |
			CAN_IT_ERROR_WARNING |
			CAN_IT_ERROR_PASSIVE |
			CAN_IT_BUSOFF |
			CAN_IT_LAST_ERROR_CODE |
			CAN_IT_ERROR) != HAL_OK) Error_Handler();

    for (;;) {
    	// Ожидаем прерывание (RX) или запрос на отправку (TX)
        uint32_t flags = osThreadFlagsWait(FLAG_CAN_RX | FLAG_CAN_TX, osFlagsWaitAny, osWaitForever);

        if ((flags & osFlagsError) != 0U) {
        	continue;
        }

        // --- Обработка приема (RX) ---
        if (flags & FLAG_CAN_RX) {
        	while (osMessageQueueGet(can_rx_queueHandle, &rx_frame, NULL, 0) == osOK) {
        		// Транспорт принимает только 29-bit Extended ID.
        		// Некорректный транспортный формат не получает NACK.
        		if (rx_frame.header.IDE != CAN_ID_EXT)
        		{
        			g_can_diag.dropped_not_ext++;
        			continue;
        		}

        		uint32_t can_id = rx_frame.header.ExtId;
        		uint8_t dst_addr = CAN_GET_DST_ADDR(can_id);
        		uint8_t my_id = (uint8_t)AppConfig_GetPerformerID();

                // Программная фильтрация: наш NodeID или Broadcast (0x00).
                if (dst_addr != my_id && dst_addr != CAN_ADDR_BROADCAST)
                {
                	g_can_diag.dropped_wrong_dst++;
                	continue;
                }

                if (CAN_GET_MSG_TYPE(can_id) != CAN_MSG_TYPE_COMMAND)
                {
                	g_can_diag.dropped_wrong_type++;
                	continue;
                }
                
                // Directive 2.0: Conductor <-> Executor использует строгий DLC=8.
                if (rx_frame.header.DLC != 8U) {
                    g_can_diag.dropped_wrong_dlc++;
                    continue;
                }

                ParsedCanCommand_t parsed;
                memset(&parsed, 0, sizeof(parsed));

                parsed.cmd_code = (uint16_t)rx_frame.data[0] |
                                    ((uint16_t)rx_frame.data[1] << 8);
                parsed.sensor_id = rx_frame.data[2];
                parsed.data_len = 5U;

                for (uint8_t i = 0U; i < parsed.data_len; i++)
                	{
                	parsed.data[i] = rx_frame.data[3U + i];
                	}

                if (osMessageQueuePut(parser_queueHandle, &parsed, 0, 0) == osOK)
                	{
                	g_can_diag.rx_total++;
                	}
                else
                	{
                	g_can_diag.dispatcher_queue_overflow++;
                	}
        	}
        }


        // --- Обработка передачи (TX) ---
        if (flags & FLAG_CAN_TX)
        	{
        	while (osMessageQueueGet(can_tx_queueHandle, &tx_frame, NULL, 0) == osOK)
        	{
        	uint32_t tick_start = HAL_GetTick();

        	while ((HAL_CAN_GetTxMailboxesFreeLevel(&hcan) == 0U) &&
        			((HAL_GetTick() - tick_start) < 10U))
        		{
        		osDelay(1);
        		}

        	if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) > 0U)
        		{
        		if (HAL_CAN_AddTxMessage(&hcan,
        				&tx_frame.header,
						tx_frame.data,
						&txMailbox) == HAL_OK)
        			{
        			g_can_diag.tx_total++;
        			}
        		else
        			{
        			g_can_diag.tx_hal_error++;
        	        g_can_diag.last_hal_error = HAL_CAN_GetError(&hcan);
        	        g_can_diag.last_esr = hcan.Instance->ESR;
        	        }
        		}
        	else
        	{
        		g_can_diag.tx_mailbox_timeout++;
        		}
        	}
        	}
    }
}




