/*
 * task_temp_monitor.h
 *
 *  Created on: Dec 15, 2025
 *      Author: andrey
 */

#ifndef TASK_TEMP_MONITOR_H_
#define TASK_TEMP_MONITOR_H_

#include <stdint.h> // Для void* argument


void app_start_task_temp_monitor(void *argument);

/**
 * @brief Безопасное чтение температуры для указанного канала.
 */
float TempMonitor_GetTemperature(uint8_t index);


#endif /* TASK_TEMP_MONITOR_H_ */
