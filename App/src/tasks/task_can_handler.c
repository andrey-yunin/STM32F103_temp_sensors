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

// --- Внешние хэндлы HAL ---
extern CAN_HandleTypeDef hcan; // Хэндл CAN-периферии из main.c
extern osThreadId_t task_can_handleHandle;


// ============================================================
// Вспомогательные функции (Response Helpers)
// ============================================================

void CAN_SendAck(uint16_t cmd_code) {
	CanTxFrame_t tx;
    tx.header.ExtId = CAN_BUILD_ID(CAN_PRIORITY_NORMAL, CAN_MSG_TYPE_ACK, CAN_ADDR_CONDUCTOR, CAN_ADDR_THERMO_BOARD);
    tx.header.IDE = CAN_ID_EXT;
    tx.header.RTR = CAN_RTR_DATA;
    tx.header.DLC = 2;
    tx.data[0] = (uint8_t)(cmd_code & 0xFF);
    tx.data[1] = (uint8_t)((cmd_code >> 8) & 0xFF);

    osMessageQueuePut(can_tx_queueHandle, &tx, 0, 0);
    osThreadFlagsSet(task_can_handleHandle, FLAG_CAN_TX);
}

void CAN_SendNack(uint16_t cmd_code, uint16_t error_code) {
	CanTxFrame_t tx;
	tx.header.ExtId = CAN_BUILD_ID(CAN_PRIORITY_NORMAL, CAN_MSG_TYPE_NACK, CAN_ADDR_CONDUCTOR, CAN_ADDR_THERMO_BOARD);
    tx.header.IDE = CAN_ID_EXT;
    tx.header.RTR = CAN_RTR_DATA;
    tx.header.DLC = 4;
    tx.data[0] = (uint8_t)(cmd_code & 0xFF);
    tx.data[1] = (uint8_t)((cmd_code >> 8) & 0xFF);
    tx.data[2] = (uint8_t)(error_code & 0xFF);
    tx.data[3] = (uint8_t)((error_code >> 8) & 0xFF);

    osMessageQueuePut(can_tx_queueHandle, &tx, 0, 0);
    osThreadFlagsSet(task_can_handleHandle, FLAG_CAN_TX);
}

void CAN_SendDone(uint16_t cmd_code, uint8_t sensor_id) {
	CanTxFrame_t tx;
    tx.header.ExtId = CAN_BUILD_ID(CAN_PRIORITY_NORMAL, CAN_MSG_TYPE_DATA_DONE_LOG, CAN_ADDR_CONDUCTOR, CAN_ADDR_THERMO_BOARD);
    tx.header.IDE = CAN_ID_EXT;
    tx.header.RTR = CAN_RTR_DATA;
    tx.header.DLC = 4;
    tx.data[0] = CAN_SUB_TYPE_DONE;
    tx.data[1] = (uint8_t)(cmd_code & 0xFF);
    tx.data[2] = (uint8_t)((cmd_code >> 8) & 0xFF);
    tx.data[3] = sensor_id;

    osMessageQueuePut(can_tx_queueHandle, &tx, 0, 0);
    osThreadFlagsSet(task_can_handleHandle, FLAG_CAN_TX);
}

void CAN_SendData(uint16_t cmd_code, uint8_t *data, uint8_t len) {
	CanTxFrame_t tx;
    tx.header.ExtId = CAN_BUILD_ID(CAN_PRIORITY_NORMAL, CAN_MSG_TYPE_DATA_DONE_LOG, CAN_ADDR_CONDUCTOR, CAN_ADDR_THERMO_BOARD);
    tx.header.IDE = CAN_ID_EXT;
    tx.header.RTR = CAN_RTR_DATA;
    tx.header.DLC = (len > 6) ? 8 : (len + 2);
    tx.data[0] = CAN_SUB_TYPE_DATA;
    tx.data[1] = 0x80; // Sequence Info: EOT=1, Seq=0 (для одиночных пакетов)

    for(uint8_t i = 0; i < len && i < 6; i++) {
    	tx.data[2 + i] = data[i];
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
    // Фильтруем по DstAddr = CAN_ADDR_THERMO_BOARD (0x40)
    CAN_FilterTypeDef sFilterConfig;
    uint32_t filter_id   = ((uint32_t)CAN_ADDR_THERMO_BOARD << 16) << 3 | CAN_ID_EXT;
    uint32_t filter_mask = ((uint32_t)0xFF << 16) << 3 | CAN_ID_EXT;

    sFilterConfig.FilterBank = 0;
    sFilterConfig.FilterMode = CAN_FILTERMODE_IDMASK;
    sFilterConfig.FilterScale = CAN_FILTERSCALE_32BIT;
    sFilterConfig.FilterIdHigh = (uint16_t)(filter_id >> 16);
    sFilterConfig.FilterIdLow = (uint16_t)(filter_id & 0xFFFF);
    sFilterConfig.FilterMaskIdHigh = (uint16_t)(filter_mask >> 16);
    sFilterConfig.FilterMaskIdLow = (uint16_t)(filter_mask & 0xFFFF);
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
                if (CAN_GET_MSG_TYPE(can_id) != CAN_MSG_TYPE_COMMAND) continue;
                if (rx_frame.header.DLC < 3) continue;

                ParsedCanCommand_t parsed;
                parsed.cmd_code = (uint16_t)(rx_frame.data[0] | ((uint16_t)rx_frame.data[1] << 8));
                parsed.sensor_id = rx_frame.data[2];
                parsed.data_len = (rx_frame.header.DLC > 3) ? (rx_frame.header.DLC - 3) : 0;

                for (uint8_t i = 0; i < parsed.data_len && i < 5; i++) {
                	parsed.data[i] = rx_frame.data[3 + i];
                	}

                osMessageQueuePut(parser_queueHandle, &parsed, 0, 0);
                }
        	}

        // --- Обработка передачи (TX) ---
        if (flags & FLAG_CAN_TX) {
        	while (osMessageQueueGet(can_tx_queueHandle, &tx_frame, NULL, 0) == osOK) {
        		HAL_CAN_AddTxMessage(&hcan, &tx_frame.header, tx_frame.data, &txMailbox);
        		}
        }
    }
}






