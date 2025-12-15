/*
 * pumps_valves_gpio.c
 *
 *  Created on: Dec 12, 2025
 *      Author: andrey
 */


#include "pumps_valves_gpio.h"
#include "main.h" // Для HAL_GPIO_WritePin и макросов пинов (например, PUMP1_GPIO_Port)
#include "app_config.h"

// --- Локальные массивы для хранения информации о пинах ---
// Эти массивы связывают ID насоса/клапана с соответствующим GPIO портом и пином.

// Массивы для насосов:
// Массив портов:
static GPIO_TypeDef* const pump_ports[NUM_PUMPS] = {PUMP_1_GPIO_Port, PUMP_2_GPIO_Port, PUMP_3_GPIO_Port, PUMP_4_GPIO_Port,
		PUMP_5_GPIO_Port, PUMP_6_GPIO_Port, PUMP_7_GPIO_Port, PUMP_8_GPIO_Port, PUMP_9_GPIO_Port, PUMP_10_GPIO_Port,
		PUMP_11_GPIO_Port, PUMP_12_GPIO_Port, PUMP_13_GPIO_Port};

// Массив пинов:
static uint16_t const pump_pins[NUM_PUMPS] = {PUMP_1_Pin, PUMP_2_Pin, PUMP_3_Pin, PUMP_4_Pin, PUMP_5_Pin, PUMP_6_Pin,
		PUMP_7_Pin, PUMP_8_Pin, PUMP_9_Pin, PUMP_10_Pin, PUMP_11_Pin, PUMP_12_Pin, PUMP_13_Pin};

// Массивы для клапанов:
// Массив портов:
static GPIO_TypeDef* const valve_ports[NUM_VALVES] = {VALVE_1_GPIO_Port, VALVE_2_GPIO_Port, VALVE_3_GPIO_Port};

// Массив пинов:
static uint16_t const valve_pins[NUM_VALVES] = {VALVE_1_Pin, VALVE_2_Pin, VALVE_3_Pin};


void PumpsValves_SetPumpState(PumpID_t pump_id, bool is_on)
{
	if (pump_id < NUM_PUMPS) {
		HAL_GPIO_WritePin(pump_ports[pump_id], pump_pins[pump_id], (is_on ? GPIO_PIN_SET : GPIO_PIN_RESET));
		}
	}

void PumpsValves_SetValveState(ValveID_t valve_id, bool is_open)
{
	if (valve_id < NUM_VALVES) {
		HAL_GPIO_WritePin(valve_ports[valve_id], valve_pins[valve_id], (is_open ? GPIO_PIN_SET : GPIO_PIN_RESET));
		}
	}






