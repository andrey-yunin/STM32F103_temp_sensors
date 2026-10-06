/*
 * task_watchdog.c
 *
 *  Created on: May 12, 2026
 *      Author: andrey
 */

#include "task_watchdog.h"
#include "cmsis_os.h"
#include "main.h"

/*
 * Финальный вариант после включения IWDG в CubeMX.
 * Если IWDG еще не добавлен, эту строку пока не вставлять.
 */
extern IWDG_HandleTypeDef hiwdg;

static volatile uint32_t app_watchdog_heartbeats[APP_WDG_CLIENT_COUNT];

#if APP_WATCHDOG_TEST_HOOKS
static volatile uint32_t app_watchdog_test_mode;
volatile uint32_t app_watchdog_test_reset_csr;
volatile uint32_t app_watchdog_test_entered;
volatile uint32_t app_watchdog_test_failed_mask;

void AppWatchdog_TestArm(uint8_t mode) {
	app_watchdog_test_mode = mode;
}

static void AppWatchdog_TestCheckpoint(AppWatchdogClient_t client) {
	uint32_t mode = app_watchdog_test_mode;
	if ((mode >= 1U && mode <= 3U && (uint32_t) client == mode - 1U)
			|| (mode == 4U && client == APP_WDG_CLIENT_CAN)) {
		app_watchdog_test_entered = mode;
		for (;;) {
			if (mode != 4U) {
				/* Block only this client; supervisor and IRQs remain alive. */
				if (osDelay(1U) != osOK) {
					Error_Handler();
				}
			} else {
				/* No RTOS calls, yielding, IRQ masking or IWDG refresh. */
				__NOP();
			}
		}
	}
}
#endif

void AppWatchdog_Heartbeat(AppWatchdogClient_t client) {
#if APP_WATCHDOG_TEST_HOOKS
	AppWatchdog_TestCheckpoint(client);
#endif
	if ((uint32_t) client < (uint32_t) APP_WDG_CLIENT_COUNT) {
		app_watchdog_heartbeats[client]++;
	}
}

static uint8_t AppWatchdog_AllClientsProgressed(
		uint32_t previous[APP_WDG_CLIENT_COUNT]) {
	uint32_t alive_mask = 0U;

	for (uint32_t i = 0U; i < (uint32_t) APP_WDG_CLIENT_COUNT; i++) {
		uint32_t current = app_watchdog_heartbeats[i];

		if (current != previous[i]) {
			alive_mask |= (1UL << i);
		}
		previous[i] = current;
	}
#if APP_WATCHDOG_TEST_HOOKS
	app_watchdog_test_failed_mask =
			((1UL << APP_WDG_CLIENT_COUNT) - 1UL) & ~alive_mask;
#endif
	return alive_mask == ((1UL << APP_WDG_CLIENT_COUNT) - 1UL);
}

// --- Контроль прогресса задач ---
// Supervisor обслуживает IWDG только при продвижении всех клиентов.
// При отказе передаёт управление терминальному Error_Handler.
void app_start_task_watchdog(void *argument) {
	uint32_t previous[APP_WDG_CLIENT_COUNT] = { 0 };

	(void) argument;

#if APP_WATCHDOG_TEST_HOOKS
	/* Capture once per boot, then clear sticky flags for the next reset. */
	app_watchdog_test_reset_csr = RCC->CSR;
	__HAL_RCC_CLEAR_RESET_FLAGS();
#endif

	// Первое окно наблюдения начинается после начального refresh.
	HAL_IWDG_Refresh(&hiwdg);

	for (;;) {
		osDelay(APP_WATCHDOG_SUPERVISOR_PERIOD_MS);

		if (AppWatchdog_AllClientsProgressed(previous)) {
			HAL_IWDG_Refresh(&hiwdg);
			continue;
		}

		// --- Терминальный переход при отказе клиента ---
		// Запрет IRQ, отпускание 1-Wire и ожидание reset.
		// Штатная работа и обслуживание IWDG прекращаются.
		Error_Handler();
	}
}
