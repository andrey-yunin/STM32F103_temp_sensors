/*
 * app_flash.h
 *
 *  Created on: Apr 1, 2026
 *      Author: andrey
 */

#ifndef APP_FLASH_H_
#define APP_FLASH_H_

#include <stdint.h>
#include <stdbool.h>
#include "ds18b20.h"
#include "app_config.h"

// Адрес последней страницы Flash (Page 63 для 64KB STM32F103)
#define APP_CONFIG_FLASH_ADDR    0x0800FC00
#define APP_CONFIG_MAGIC         0x55AAEEFF // Ключ валидности данных


/**
 * @brief Структура конфигурации устройства, хранимая во Flash.
 * Размер должен быть кратен 4 байтам.
 */
typedef struct {
	uint32_t magic;                             // Метка инициализации памяти
    DS18B20_ROM_t sensors[DS18B20_MAX_SENSORS]; // Таблица маппинга (8 x 8 байт)
    uint32_t performer_id;                      // Настраиваемый CAN ID платы
    uint16_t checksum;                          // Контрольная сумма структуры
} AppConfig_t;



/**
 * @brief Инициализирует конфигурацию и создает Mutex.
 */
void AppConfig_Init(void);

/**
 * @brief Безопасное чтение ROM ID для логического канала.
 */
void AppConfig_GetSensorROM(uint8_t index, DS18B20_ROM_t *out_rom);

/**
 * @brief Безопасная запись ROM ID для логического канала в RAM.
 */
void AppConfig_SetSensorROM(uint8_t index, DS18B20_ROM_t *in_rom);

/**
 * @brief Чтение текущего CAN ID платы.
 */
uint32_t AppConfig_GetPerformerID(void);

/**
 * @brief Сохранение всех изменений во Flash.
 */
bool AppConfig_Commit(void);

#endif /* APP_FLASH_H_ */
