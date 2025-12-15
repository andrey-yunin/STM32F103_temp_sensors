/*
 * app_globals.h
 *
 *  Created on: Dec 10, 2025
 *      Author: andrey
 */

#ifndef APP_GLOBALS_H_
#define APP_GLOBALS_H_

#include <stdint.h>
#include "app_config.h"     // Для MOTOR_COUNT


// Определение и инициализация глобальной переменной
extern uint8_t g_performer_id; // 0xFF означает, что ID еще не установлен

// Глобальный массив для хранения последних усредненных температур
extern float g_latest_temperatures[DS18B20_MAX_SENSORS];


#endif /* APP_GLOBALS_H_ */
