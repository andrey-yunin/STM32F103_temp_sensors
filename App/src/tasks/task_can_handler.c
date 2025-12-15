/*
 * task_can_handler.c
 *
 *  Created on: Dec 8, 2025
 *      Author: andrey
 */


#include "task_can_handler.h"
#include "main.h"           // Для HAL-функций, CAN_HandleTypeDef, UART_HandleTypeDef
#include "cmsis_os.h"       // Для osDelay, osMessageQueueXxx
#include "app_queues.h"     // Для хэндлов очередей
#include "command_protocol.h" // Для CAN_Command_t
#include "app_config.h"     // Для CanRxFrame_t, CanTxFrame_t, CAN_DATA_MAX_LEN

// --- Внешние хэндлы HAL ---
extern CAN_HandleTypeDef hcan; // Хэндл CAN-периферии из main.c

// --- Глобальная переменная для хранения ID исполнителя (пока заглушка) ---
extern uint8_t g_performer_id;


void app_start_task_can_handler(void *argument)
{
	CanRxFrame_t rx_frame;      // Буфер для входящего CAN-фрейма
	CAN_Command_t parsed_command; // Буфер для распарсенной команды
	CanTxFrame_t tx_frame;    // Буфер для исходящего CAN-фрейма

	uint32_t txMailbox; // Для HAL_CAN_AddTxMessage

	// --- Настройка CAN-фильтров ---
	// Это очень важный шаг, чтобы CAN-контроллер принимал только нужные сообщения.
	// Пока что настроим простой фильтр, который принимает ВСЁ,
	// но в будущем здесь будет логика для фильтрации по Performer_ID.
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

	if (HAL_CAN_ConfigFilter(&hcan, &sFilterConfig) != HAL_OK) {
		Error_Handler(); // Проблема с настройкой фильтра
		}

	// --- Запуск CAN-контроллера ---
	if (HAL_CAN_Start(&hcan) != HAL_OK) {
		Error_Handler(); // Проблема с запуском CAN
		}

	// --- Активация CAN RX прерываний ---
	// Это позволит прерыванию CAN_RX_FIFO0_MSG_PENDING_IT срабатывать
	// и передавать данные в can_rx_queue
	if (HAL_CAN_ActivateNotification(&hcan, CAN_IT_RX_FIFO0_MSG_PENDING) != HAL_OK) {
		Error_Handler();
		}
	// Бесконечный цикл задачи
	for(;;)
		{
		// --- 1. Обработка входящих сообщений (RX) ---
		// Ждем полный CAN-фрейм от прерывания CAN_RxFifo0MsgPendingCallback
		if (osMessageQueueGet(can_rx_queueHandle, &rx_frame, NULL, osWaitForever) == osOK) {
		// --- Парсинг StdId для получения Performer_ID и Motor_ID ---
		uint16_t std_id = rx_frame.header.StdId;

		// --- Парсинг StdId в соответствии с CAN_PROTOCOL.md ---
		// Мы стандартизируем структуру ID для ясности и расширяемости.
		// Согласно протоколу:
		// - Биты 7-4: ID исполнителя (Performer ID), 4 бита, диапазон 0-15.
		// - Биты 3-0: ID мотора (Motor ID), 4 бита, диапазон 0-15.
		// received_performer_id: Чтобы получить биты 7-4, мы сдвигаем StdId вправо на 4 позиции.
		// Затем применяем маску 0x0F (0b00001111), чтобы получить только эти 4 бита.

		uint8_t received_performer_id = (std_id >> 4) & 0x0F;

		// received_motor_id: Чтобы получить биты 3-0, нам не нужно сдвигать StdId.
		// Просто применяем маску 0x0F (0b00001111), чтобы получить младшие 4 бита.
		uint8_t received_motor_id     = std_id & 0x0F;


		// --- Фильтрация по Performer ID ---
		// Если ID исполнителя в сообщении не наш, игнорируем его
		if (received_performer_id != g_performer_id) {
		// Если g_performer_id == 0xFF (не настроен) или received_performer_id == 0x0F (broadcast)
		// то все равно обрабатываем. Пока что просто игнорируем
		continue;
		}
		// --- Парсинг поля данных в CAN_Command_t ---
		parsed_command.motor_id = received_motor_id; // motor_id из StdId
		parsed_command.command_id = (CommandID_t)rx_frame.data[0]; // CommandID_t из первого байта данных
		// Payload - это int32_t, занимает 4 байта, начиная со второго (index 1)
		parsed_command.payload = (int32_t)rx_frame.data[1] |
				                 ((int32_t)rx_frame.data[2] << 8) |
				                 ((int32_t)rx_frame.data[3] << 16) |
		                         ((int32_t)rx_frame.data[4] << 24);

		 // --- Отправка распарсенной команды в parser_queue ---
		 osMessageQueuePut(dispatcher_queueHandle, &parsed_command, 0, 0);
		 }

		 // --- 2. Обработка исходящих сообщений (TX) ---
		 // Ждем CanTxFrame_t от других задач
		 if (osMessageQueueGet(can_tx_queueHandle, &tx_frame, NULL, 0) == osOK)
		 {
			 // 0 - не блокировать, если очередь пуста
		     // Добавляем сообщение в аппаратную очередь CAN на передачу
			 if (HAL_CAN_AddTxMessage(&hcan, &tx_frame.header, tx_frame.data, &txMailbox) != HAL_OK) {
				 // Ошибка при отправке сообщения. Можно добавить счетчик ошибок или повтор
				 }
			 }
		 osDelay(1); // Небольшая задержка, чтобы избежать "голодания" других задач

		 }
}
