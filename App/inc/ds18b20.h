/*
 * ds18b20.h
 *
 *  Created on: Dec 15, 2025
 *      Author: andrey
 */

#ifndef DS18B20_H_
#define DS18B20_H_

#include <stdint.h>
#include <stdbool.h>
#include "app_config.h"


// Структура для хранения уникального 64-битного ROM-кода датчика
typedef struct {
	uint8_t rom_code[8];
	} DS18B20_ROM_t;

/**
 * @brief Инициализирует драйвер DS18B20.
 *        Находит все датчики на шине и сохраняет их ROM-коды.
 * @return Количество найденных датчиков.
   19  */
uint8_t DS18B20_Init();

/**
 * @brief Запускает измерение температуры на ВСЕХ датчиках на шине.
 *        Это широковещательная команда, не требует адресации.
 */
void DS18B20_StartAll();

/**
 * @brief Читает температуру с конкретного датчика.
 * @param rom Указатель на структуру с ROM-кодом датчика.
 * @param out_temp Указатель на float, куда будет записана температура.
 * @return true в случае успеха (и корректной CRC), false в случае ошибки.
 */
bool DS18B20_ReadTemperature(DS18B20_ROM_t* rom, float* out_temp);

/**
 * @brief Предоставляет доступ к ROM-кодам найденных датчиков.
 * @param sensor_index Индекс датчика (от 0 до найденного количества - 1).
 * @return Указатель на структуру с ROM-кодом или NULL, если индекс некорректен.
 */
DS18B20_ROM_t* DS18B20_GetROM(uint8_t sensor_index);


#endif /* DS18B20_H_ */
