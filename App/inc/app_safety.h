/*
 * app_safety.h
 *
 *  Created on: May 12, 2026
 *      Author: andrey
 */

#ifndef APP_SAFETY_H_
#define APP_SAFETY_H_

#include <stdbool.h>

// --- Безопасное состояние Thermo ---

/*
 * Отпускает линию 1-Wire без обращения к RTOS и CAN.
 * Сам по себе этот вызов не запрещает следующие операции.
 */
void AppSafety_EnterSafeState(void);

// --- Подготовка к сервисному перезапуску ---

/*
 * Устанавливает постоянный до reset запрет работы с шиной
 * и отпускает линию 1-Wire.
 * Dispatcher вызывает функцию после проверки сервисного ключа.
 */
void AppSafety_PrepareReset(void);

/*
 * Возвращает признак подготовки к перезапуску.
 * Проверку и последующее включение LOW драйвер обязан
 * выполнять как одну защищённую операцию.
 */
bool AppSafety_IsResetPending(void);

#endif /* APP_SAFETY_H_ */
