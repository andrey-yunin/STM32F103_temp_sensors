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


typedef enum {
	DS18B20_READ_OK = 0,
	DS18B20_READ_COMM_ERROR
} DS18B20_ReadResult_t;


/**
 * @brief Инициализирует драйвер DS18B20.
 *        Находит все датчики на шине и сохраняет их ROM-коды.
 * @return Количество найденных датчиков.
 */
uint8_t DS18B20_Init();


/**
 * @brief Запускает измерение температуры на ВСЕХ датчиках на шине.
 *        Это широковещательная команда SKIP ROM + CONVERT T.
 * @return true, если шина ответила presence pulse и команда CONVERT T отправлена.
 *         false означает bus/presence failure, а не conversion timeout.
 */
bool DS18B20_StartAll(void);


/**
 * @brief Читает температуру с конкретного DS18B20.
 *
 * COMM_ERROR означает low-level проблему 1-Wire/DS18B20:
 * нет presence pulse, не читается scratchpad или не сходится CRC.
 * Этот результат исполнитель переводит в CAN_ERR_THERMO_COMM.
 */
DS18B20_ReadResult_t DS18B20_ReadTemperature(const DS18B20_ROM_t* rom,
                                             float* out_temp);


/**
 * @brief Предоставляет доступ к ROM-кодам найденных датчиков.
 * @param sensor_index Индекс датчика (от 0 до найденного количества - 1).
 * @return Указатель на структуру с ROM-кодом или NULL, если индекс некорректен.
 */
DS18B20_ROM_t* DS18B20_GetROM(uint8_t sensor_index);


/**
 * @brief Проверяет, что ROM принадлежит DS18B20 и имеет корректный CRC.
 */
bool DS18B20_IsValidROM(const DS18B20_ROM_t* rom);


/**
 * @brief Отпускает 1-Wire шину в idle/high-Z состояние.
 *
 * Для Thermo safe-state это базовое безопасное действие:
 * исполнитель не имеет силовых выходов, поэтому аварийный выход
 * означает "не держать 1-Wire bus в LOW".
 */
void DS18B20_BusRelease(void);


/**
 * @brief Проверяет завершение broadcast conversion после DS18B20_StartAll().
 *
 * DS18B20 в штатном питании возвращает 0 в read slot, пока conversion идет,
 * и 1, когда conversion завершена. На общей 1-Wire шине 0 от любого датчика
 * означает, что общий broadcast conversion еще не завершен.
 *
 * Важно: вызывать только до следующего RESET или другой 1-Wire транзакции.
 */
bool DS18B20_IsConversionComplete(void);



#endif /* DS18B20_H_ */
