/*
 * motion_planner.h
 *
 *  Created on: Dec 8, 2025
 *      Author: andrey
 */

#ifndef MOTION_PLANNER_H_
#define MOTION_PLANNER_H_

#include <stdint.h>


// Максимальное количество шагов в секунду для базовых расчетов
#define MAX_STEPS_PER_SEC 20000 // Пример: 20 кГц, может быть скорректировано

// Структура для хранения параметров движения
typedef struct {
	int32_t current_position;       // Текущая абсолютная позиция мотора
    int32_t target_position;        // Целевая абсолютная позиция
    uint32_t current_speed_steps_per_sec; // Текущая скорость (шагов/сек)
    uint32_t max_speed_steps_per_sec;     // Максимальная желаемая скорость
    uint32_t acceleration_steps_per_sec2; // Ускорение (шагов/сек^2)
    uint8_t direction;              // Направление движения (0 - CW, 1 - CCW)
    int32_t steps_to_go;            // Сколько шагов осталось сделать
    uint32_t step_pulse_period_us;  // Период STEP-импульса в микросекундах (для таймера)
    } MotorMotionState_t;


// Прототипы функций планировщика движения
void MotionPlanner_InitMotorState(MotorMotionState_t* state, int32_t initial_pos);
int32_t MotionPlanner_CalculateNewTarget(MotorMotionState_t* state, int32_t target);
uint32_t MotionPlanner_GetNextPulsePeriod(MotorMotionState_t* state);
uint8_t MotionPlanner_IsMovementComplete(MotorMotionState_t* state);


#endif /* MOTION_PLANNER_H_ */
