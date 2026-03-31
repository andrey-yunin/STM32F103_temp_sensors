/*
 * app_queues.h
 *
 *  Created on: Dec 9, 2025
 *      Author: andrey
 */

#ifndef APP_QUEUES_H_
#define APP_QUEUES_H_

#include "cmsis_os.h"
#include "app_config.h"
#include "can_protocol.h"

// --- Хэндлы очередей (будут созданы в main.c) ---

// Очередь для приема сырых CAN-фреймов (CanRxFrame_t)
extern osMessageQueueId_t can_rx_queueHandle;

// Очередь для отправки сырых CAN-фреймов (CanTxFrame_t)
extern osMessageQueueId_t can_tx_queueHandle;

// Очередь для передачи распарсенных команд (ParsedCanCommand_t)
extern osMessageQueueId_t parser_queueHandle;


#endif /* APP_QUEUES_H_ */
