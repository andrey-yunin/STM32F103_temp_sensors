/*
 * task_temp_monitor.c
 *
 *  Created on: Dec 15, 2025
 *      Author: andrey
 */


#include "task_temp_monitor.h"
#include "cmsis_os.h"
#include "app_globals.h"
#include "ds18b20.h"

/**
 *
 * @brief Задача мониторинга температуры.
 * Реализует промышленный цикл: Broadcast Start -> RTOS Wait -> Match ROM Read.
 */
void app_start_task_temp_monitor(void *argument)
{
	// 1. Инициализация при старте: Поиск всех датчиков на шине (Search ROM)
    uint8_t found_sensors = DS18B20_Init();
    float temp_val = 0.0f;

     // Инициализируем массив температур значениями "Ошибка" (-999.0),
     // пока не получены первые реальные данные.
     for (uint8_t i = 0; i < DS18B20_MAX_SENSORS; i++) {
    	 g_latest_temperatures[i] = -999.0f;
    	 }

     for(;;)
    	 {
    	 if (found_sensors > 0)
    		 {
    		 // 2. ЗАПУСК: Даем команду ВСЕМ датчикам начать измерение одновременно.
    		 // Это экономит 750мс * (N-1) времени по сравнению с поочередным запуском.
    		 DS18B20_StartAll();

    		 // 3. ОЖИДАНИЕ: Даем датчикам время на 12-битную конвертацию (макс 750мс).
    		 // Используем osDelay, чтобы не блокировать процессор для задач CAN и Dispatcher.
    		 osDelay(800);

    		 // 4. ЧТЕНИЕ: Обращаемся к каждому найденному датчику по его уникальному ID.
    		 for (uint8_t i = 0; i < found_sensors; i++)
    			 {
    			 DS18B20_ROM_t* rom = DS18B20_GetROM(i);
    			 // Адресное чтение (Match ROM) с проверкой CRC внутри драйвера
    			 if (DS18B20_ReadTemperature(rom, &temp_val))
    				 {
    				 // Успех: сохраняем в глобальный массив для передачи по CAN
    				 g_latest_temperatures[i] = temp_val;
    				 }
    			 else
    				 {
    				 // Ошибка (например, помеха на линии): помечаем канал как невалидный
    				 g_latest_temperatures[i] = -999.0f;
    				 }
    			 }
    		 }
    	 else
    		 {
    		 // Если датчики не найдены, пытаемся переинициализировать шину раз в 5 секунд
    		 osDelay(5000);
    		 found_sensors = DS18B20_Init();
    		 }
    	 // Пауза между полными циклами опроса всей системы (например, 2 секунды)
    	 osDelay(2000);
    	 }
}

