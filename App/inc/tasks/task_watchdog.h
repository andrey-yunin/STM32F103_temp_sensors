/*
 * task_watchdog.h
 *
 *  Created on: May 12, 2026
 *      Author: andrey
 */

#ifndef TASK_WATCHDOG_H_
#define TASK_WATCHDOG_H_

#include <stdint.h>

/* Enabled only by the separate watchdog bench build. */
#ifndef APP_WATCHDOG_TEST_HOOKS
#define APP_WATCHDOG_TEST_HOOKS 0
#endif

#if APP_WATCHDOG_TEST_HOOKS
#define APP_WDG_TEST_INFO_COMMAND  0xF0FDU
#define APP_WDG_TEST_FAULT_COMMAND 0xF0FEU
#define APP_WDG_TEST_KEY           0xBEEFU
/* Modes: 1 CAN wait, 2 Dispatcher wait, 3 Temp wait, 4 CAN busy-loop. */
void AppWatchdog_TestArm(uint8_t mode);
extern volatile uint32_t app_watchdog_test_reset_csr;
extern volatile uint32_t app_watchdog_test_entered;
extern volatile uint32_t app_watchdog_test_failed_mask;
#endif

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
