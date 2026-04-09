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

// --- Внешние хэндлы HAL ---
extern CAN_HandleTypeDef hcan; // Хэндл CAN-периферии из main.c
extern osThreadId_t task_can_handleHandle;


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

    osMessageQueuePut(can_tx_queueHandle, &tx, 0, 0);
    osThreadFlagsSet(task_can_handleHandle, FLAG_CAN_TX);
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

    osMessageQueuePut(can_tx_queueHandle, &tx, 0, 0);
    osThreadFlagsSet(task_can_handleHandle, FLAG_CAN_TX);
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

    osMessageQueuePut(can_tx_queueHandle, &tx, 0, 0);
    osThreadFlagsSet(task_can_handleHandle, FLAG_CAN_TX);
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

     osMessageQueuePut(can_tx_queueHandle, &tx, 0, 0);
     osThreadFlagsSet(task_can_handleHandle, FLAG_CAN_TX);
}




// ============================================================
// Основная задача (Main Task Loop)
// ============================================================

void app_start_task_can_handler(void *argument) {
	CanRxFrame_t rx_frame;
    CanTxFrame_t tx_frame;
    uint32_t txMailbox;

    // --- Настройка CAN-фильтра (bxCAN Hardware Filter) ---
    // В экосистеме DDS-240 используем открытый фильтр (Mask = 0).
    // Это позволяет принимать широковещательные команды (DstAddr = 0x00).
    // Фильтрация по NodeID выполняется программно в цикле RX.
    CAN_FilterTypeDef sFilterConfig;

    sFilterConfig.FilterBank = 0;
    sFilterConfig.FilterMode = CAN_FILTERMODE_IDMASK;
    sFilterConfig.FilterScale = CAN_FILTERSCALE_32BIT;
    sFilterConfig.FilterIdHigh = 0x0000;
    sFilterConfig.FilterIdLow = 0x0000;
    sFilterConfig.FilterMaskIdHigh = 0x0000;
    sFilterConfig.FilterMaskIdLow = 0x0000;
    sFilterConfig.FilterFIFOAssignment = CAN_RX_FIFO0;
    sFilterConfig.FilterActivation = ENABLE;
    sFilterConfig.SlaveStartFilterBank = 14;

    if (HAL_CAN_ConfigFilter(&hcan, &sFilterConfig) != HAL_OK) Error_Handler();
    if (HAL_CAN_Start(&hcan) != HAL_OK) Error_Handler();
    if (HAL_CAN_ActivateNotification(&hcan, CAN_IT_RX_FIFO0_MSG_PENDING) != HAL_OK) Error_Handler();

    for (;;) {
    	// Ожидаем прерывание (RX) или запрос на отправку (TX)
        uint32_t flags = osThreadFlagsWait(FLAG_CAN_RX | FLAG_CAN_TX, osFlagsWaitAny, osWaitForever);

        // --- Обработка приема (RX) ---
        if (flags & FLAG_CAN_RX) {
        	while (osMessageQueueGet(can_rx_queueHandle, &rx_frame, NULL, 0) == osOK) {
        		if (rx_frame.header.IDE != CAN_ID_EXT) continue;

        		uint32_t can_id = rx_frame.header.ExtId;
        		uint8_t dst_addr = CAN_GET_DST_ADDR(can_id);
        		uint8_t my_id = (uint8_t)AppConfig_GetPerformerID();

                // Программная фильтрация: Наш ID или Broadcast (0x00)
                if (dst_addr != my_id && dst_addr != CAN_ADDR_BROADCAST) continue;

                if (CAN_GET_MSG_TYPE(can_id) != CAN_MSG_TYPE_COMMAND) continue;
                
                // --- Directive 2.0: Strict DLC check ---
                if (rx_frame.header.DLC != 8) {
                    CAN_SendNack(0x0000, CAN_ERR_INVALID_PARAM); // Опционально: сообщаем об ошибке формата
                    continue;
                }

                ParsedCanCommand_t parsed;
                parsed.cmd_code = (uint16_t)(rx_frame.data[0] | ((uint16_t)rx_frame.data[1] << 8));
                parsed.sensor_id = rx_frame.data[2];
                parsed.data_len = 5; // Всегда 5 байт (3 заголовок + 5 данные = 8 DLC)

                for (uint8_t i = 0; i < 5; i++) {
                	parsed.data[i] = rx_frame.data[3 + i];
                	}

                osMessageQueuePut(parser_queueHandle, &parsed, 0, 0);
                }
        	}

        // --- Обработка передачи (TX) ---
        if (flags & FLAG_CAN_TX) {
        	while (osMessageQueueGet(can_tx_queueHandle, &tx_frame, NULL, 0) == osOK) {
        		// Mailbox Guard: Ожидаем свободный почтовый ящик перед отправкой
        		uint32_t start_tick = osKernelGetTickCount();
        		while (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) == 0) {
        			if ((osKernelGetTickCount() - start_tick) > 10) break; // Таймаут 10мс
        			osDelay(1);
        		}
        		HAL_CAN_AddTxMessage(&hcan, &tx_frame.header, tx_frame.data, &txMailbox);
        		}
        }
    }
}






