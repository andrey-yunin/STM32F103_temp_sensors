/*
 * app_queues.h
 *
 *  Created on: Dec 9, 2025
 *      Author: andrey
 */

#ifndef APP_QUEUES_H_
#define APP_QUEUES_H_

#include "cmsis_os.h" // Для osMessageQueueId_t

// Глобальные хэндлы для всех очередей FreeRTOS
extern osMessageQueueId_t can_rx_queueHandle;      // Для приема сырых CAN-фреймов (ISR -> CAN Handler)
extern osMessageQueueId_t can_tx_queueHandle;      // Для отправки CAN-сообщений (любая задача -> CAN Handler)
extern osMessageQueueId_t dispatcher_queueHandle;      // Для передачи CAN-фреймов (CAN Handler -> Command Parser)


#endif /* APP_QUEUES_H_ */
