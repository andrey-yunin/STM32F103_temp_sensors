/*
 * task_command_parser.h
 *
 *  Created on: Dec 8, 2025
 *      Author: andrey
 */

#ifndef TASK_DISPATCHER_H_
#define TASK_DISPATCHER_H_

#include <stdint.h> // Для void* argument

// Функция, которая будет точкой входа для задачи FreeRTOS.

void app_start_task_dispatcher(void *argument);

#endif /* TASK_DISPATCHER_H_ */
