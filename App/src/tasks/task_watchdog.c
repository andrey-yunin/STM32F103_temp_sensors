/*
 * task_watchdog.c
 *
 *  Created on: May 12, 2026
 *      Author: andrey
 */


#include "task_watchdog.h"
#include "app_safety.h"
#include "cmsis_os.h"
#include "main.h"

/*
 * Финальный вариант после включения IWDG в CubeMX.
 * Если IWDG еще не добавлен, эту строку пока не вставлять.
 */
extern IWDG_HandleTypeDef hiwdg;

static volatile uint32_t app_watchdog_heartbeats[APP_WDG_CLIENT_COUNT];

void AppWatchdog_Heartbeat(AppWatchdogClient_t client)
{
	if ((uint32_t)client < (uint32_t)APP_WDG_CLIENT_COUNT)
		{
		app_watchdog_heartbeats[client]++;
		}
}

static uint8_t AppWatchdog_AllClientsProgressed(uint32_t previous[APP_WDG_CLIENT_COUNT])
{
	uint32_t alive_mask = 0U;

    for (uint32_t i = 0U; i < (uint32_t)APP_WDG_CLIENT_COUNT; i++)
    {
    	uint32_t current = app_watchdog_heartbeats[i];

    	if (current != previous[i])
    		{
    		alive_mask |= (1UL << i);
    		}
    	previous[i] = current;
    	}
    return alive_mask == ((1UL << APP_WDG_CLIENT_COUNT) - 1UL);
}

void app_start_task_watchdog(void *argument)
{
	uint32_t previous[APP_WDG_CLIENT_COUNT] = {0};

	(void)argument;

	/*
     * IWDG refresh принадлежит только supervisor-задаче.
     */

	HAL_IWDG_Refresh(&hiwdg);

	for (;;)

		{
		osDelay(APP_WATCHDOG_SUPERVISOR_PERIOD_MS);

		if (AppWatchdog_AllClientsProgressed(previous))
		{
			HAL_IWDG_Refresh(&hiwdg);
			continue;
			}
		/*
         * Один из критических клиентов перестал продвигаться.
         * Для Thermo safe-state - отпустить/обезопасить 1-Wire домен.
         * DONE/NACK из этого path не отправляем.
         */
		AppSafety_EnterSafeState();

		for (;;)
			{
			osDelay(APP_WATCHDOG_SUPERVISOR_PERIOD_MS);
			}
		}
}



