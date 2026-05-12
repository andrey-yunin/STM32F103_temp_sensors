/*
 * app_safety.c
 *
 *  Created on: May 12, 2026
 *      Author: andrey
 */

#include "app_safety.h"
#include "ds18b20.h"

void AppSafety_EnterSafeState(void)
{
	/*
  	 * Thermo Executor не управляет силовыми нагрузками.
  	 *
  	 * Safe-state для этой платы:
  	 * - отпустить 1-Wire bus;
  	 * - считать текущую DS18B20 транзакцию прерванной;
  	 * - не отправлять штатный DONE из аварийного path.
  	 */
	DS18B20_BusRelease();
}



