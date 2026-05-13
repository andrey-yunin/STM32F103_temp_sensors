/*
 * task_watchdog.h
 *
 *  Created on: May 12, 2026
 *      Author: andrey
 */

#ifndef TASK_WATCHDOG_H_
#define TASK_WATCHDOG_H_

#include <stdint.h>

/*
 * Общий watchdog timeout для задач, которые ждут очередь/event.
 * Не заменяет доменные timing-константы Thermo.
 */

#define APP_WATCHDOG_TASK_IDLE_TIMEOUT_MS  500U

/*
 * Период supervisor-задачи, как в Motion/Fluidics.
 */
#define APP_WATCHDOG_SUPERVISOR_PERIOD_MS  1000U

typedef enum {
	APP_WDG_CLIENT_CAN = 0,
    APP_WDG_CLIENT_DISPATCHER,
    APP_WDG_CLIENT_TEMP_MONITOR,
    APP_WDG_CLIENT_COUNT
} AppWatchdogClient_t;

void AppWatchdog_Heartbeat(AppWatchdogClient_t client);
void app_start_task_watchdog(void *argument);

#endif /* TASK_WATCHDOG_H_ */

