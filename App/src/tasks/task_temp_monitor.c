/*
 * task_temp_monitor.c
 *
 *  Created on: Dec 15, 2025
 *      Author: andrey
 */


#include "task_temp_monitor.h"
#include "cmsis_os.h"
#include "app_flash.h"
#include "ds18b20.h"
#include <string.h>

// --- Инкапсулированные данные (скрыты внутри модуля) ---
static float s_latest_temperatures[DS18B20_MAX_SENSORS];
static osMutexId_t tempMutex = NULL;

const osMutexAttr_t tempMutex_attr = {
		"tempMutex",
		osMutexPrioInherit,
		NULL,
		0U
		};

// --- Публичный API доступа к данным ---
/**
 * @brief Безопасное чтение температуры из другого потока (например, из Dispatcher).
 */
float TempMonitor_GetTemperature(uint8_t index) {
float val = -999.0f;
if (index < DS18B20_MAX_SENSORS && tempMutex != NULL) {
	if (osMutexAcquire(tempMutex, 10) == osOK) { // Ждем максимум 10мс
		val = s_latest_temperatures[index];
		osMutexRelease(tempMutex);
		}
	}
return val;
}

/**
 *
 * @brief Задача мониторинга температуры.
 * Реализует промышленный цикл: Broadcast Start -> RTOS Wait -> Match ROM Read.
 */
void app_start_task_temp_monitor(void *argument)
{
	// 1. Инициализация мьютекса защиты данных
	tempMutex = osMutexNew(&tempMutex_attr);

	// 2. Инициализация массива начальными значениями "Ошибки"
	for (uint8_t i = 0; i < DS18B20_MAX_SENSORS; i++) {
		s_latest_temperatures[i] = -999.0f;
		}

	// Буферы для работы в цикле
	DS18B20_ROM_t target_rom;
	float current_temp = 0.0f;

	for(;;) {
		// 3. ШИРОКОВЕЩАТЕЛЬНЫЙ ЗАПУСК: Все датчики на шине начинают мерить температуру.
	    DS18B20_StartAll();

	    // 4. ОЖИДАНИЕ: Даем датчикам время на замер (750мс).
	    osDelay(800);

	    // 5. ОПРОС ПО ТАБЛИЦЕ МАППИНГА (из Flash)
	    for (uint8_t i = 0; i < DS18B20_MAX_SENSORS; i++) {
	    	// Читаем ROM ID для канала 'i' из защищенной конфигурации
	    	AppConfig_GetSensorROM(i, &target_rom);

	    	// Проверяем, привязан ли датчик (первый байт DS18B20 всегда 0x28)
	    	if (target_rom.rom_code[0] == 0x28) {
	    		// Адресное чтение конкретного датчика
	    		if (DS18B20_ReadTemperature(&target_rom, &current_temp)) {
	    			// Успех: сохраняем результат под защитой мьютекса
	    			if (osMutexAcquire(tempMutex, osWaitForever) == osOK) {
	    				s_latest_temperatures[i] = current_temp;
	    				osMutexRelease(tempMutex);
	    				}
	    			}
	    		else {
	    			// Ошибка чтения (датчик пропал или помеха)
	    			s_latest_temperatures[i] = -999.0f;
	    			}
	    		}
	    	else {
	    		// Канал не настроен (пусто во Flash)
	    		s_latest_temperatures[i] = -999.0f;
	    		}
	    	}

	    // Пауза между полными циклами опроса всей системы (например, 2 секунды)
    	 osDelay(2000);
    	 }
}

