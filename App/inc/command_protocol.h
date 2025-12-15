/*
 * command_protocol.h
 *
 *  Created on: Dec 9, 2025
 *      Author: andrey
 */

#ifndef COMMAND_PROTOCOL_H_
#define COMMAND_PROTOCOL_H_

#include <stdint.h>

// ID команд, которые "дирижер" может отправлять "исполнителю насосов и клапанов"
typedef enum {
	CMD_MOVE_ABSOLUTE       = 0x01, // Движение в абсолютную позицию
    CMD_MOVE_RELATIVE       = 0x02, // Движение на заданное количество шагов
    CMD_SET_SPEED           = 0x03, // Установить максимальную скорость
    CMD_SET_ACCELERATION    = 0x04, // Установить ускорение
    CMD_STOP                = 0x05, // Остановить движение
    CMD_GET_STATUS          = 0x06, // Запросить статус мотора
    CMD_SET_CURRENT         = 0x07, // Установить рабочий ток
    CMD_ENABLE_MOTOR        = 0x08, // Включить/выключить драйвер
    CMD_PERFORMER_ID_SET    = 0x09, // Команда для установки ID исполнителя
	CMD_SET_PUMP_STATE      = 0x10, // Установить состояние насоса (вкл/выкл)
	CMD_SET_VALVE_STATE     = 0x11, // Установить состояние клапана (откр/закр)
	CMD_GET_TEMPERATURE     = 0x12, // Запросить температуру с датчика
    } CommandID_t;


// Структура CAN-сообщения для парсинга
// Это поможет нам легко разбирать входящие CAN-пакеты
typedef struct {
	uint8_t     motor_id;   // ID мотора (0-7). Для команд Performer_ID_SET может быть 0xFF.
    CommandID_t command_id; // ID команды
    int32_t     payload;    // 32-битное значение (позиция, скорость, ID исполнителя и т.д.)
    } CAN_Command_t;






#endif /* COMMAND_PROTOCOL_H_ */
