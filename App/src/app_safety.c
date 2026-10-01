/*
 * app_safety.c
 *
 *  Created on: May 12, 2026
 *      Author: andrey
 */

#include "app_safety.h"
#include "ds18b20.h"
#include "main.h"

// --- Постоянный запрет до перезапуска MCU ---

/*
 * Владелец признака — модуль safety.
 * Программного снятия запрета нет: false устанавливается
 * только при начальной инициализации после reset.
 */
static volatile bool app_safety_reset_pending = false;

// --- Отпускание линии 1-Wire ---

/*
 * Thermo не управляет силовыми выходами.
 * Здесь только отпускаем линию; RTOS и CAN не используются.
 * Терминальный аварийный путь рассматривается отдельно в T12.
 */
void AppSafety_EnterSafeState(void) {
	DS18B20_BusRelease();
}

// --- Чтение признака подготовки к reset ---

/*
 * Чтение признака не изменяет состояние платы.
 * Защита последующего аппаратного действия — обязанность драйвера.
 */
bool AppSafety_IsResetPending(void) {
	return app_safety_reset_pending;
}

// --- Запрет работы и отпускание шины ---

/*
 * Установка запрета и отпускание линии выполняются вместе,
 * без переключения задач между этими действиями.
 * Внутри нет ожиданий, операций Flash или передачи CAN.
 */
void AppSafety_PrepareReset(void) {
	const uint32_t saved_primask = __get_PRIMASK();
	__disable_irq();

	app_safety_reset_pending = true;
	AppSafety_EnterSafeState();
	__DSB();

	__set_PRIMASK(saved_primask);
}

