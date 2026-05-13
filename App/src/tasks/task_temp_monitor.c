/*
 * task_temp_monitor.c
 *
 *  Created on: Dec 15, 2025
 *      Author: andrey
 */


#include "task_temp_monitor.h"
#include "cmsis_os.h"
#include "app_queues.h"
#include "app_flash.h"
#include "can_protocol.h"
#include "task_watchdog.h"
#include "ds18b20.h"
#include <string.h>
#include <stdbool.h>


#define TEMP_MONITOR_CONVERSION_DELAY_MS   800U
#define TEMP_MONITOR_IDLE_DELAY_MS         2000U
#define TEMP_MONITOR_NO_PENDING_SENSOR     0xFFU

// --- Инкапсулированные данные (скрыты внутри модуля) ---
static float s_latest_temperatures[DS18B20_MAX_SENSORS];
static uint8_t s_rom_id_buffer[8];
static uint8_t s_rom_map_pending_sensor_id = TEMP_MONITOR_NO_PENDING_SENSOR;
static bool s_rom_map_pending = false;
static osMutexId_t tempMutex = NULL;

const osMutexAttr_t tempMutex_attr = {
		"tempMutex",
		osMutexPrioInherit,
		NULL,
			0U
			};

static void TempMonitor_ClearPendingMap(void)
{
	memset(s_rom_id_buffer, 0xFF, sizeof(s_rom_id_buffer));
	s_rom_map_pending_sensor_id = TEMP_MONITOR_NO_PENDING_SENSOR;
	s_rom_map_pending = false;
}

static bool TempMonitor_IsEmptyROM(const DS18B20_ROM_t* rom)
{
	if (rom == NULL) {
		return false;
		}

	for (uint8_t i = 0; i < sizeof(rom->rom_code); i++) {
		if (rom->rom_code[i] != 0xFFU) {
			return false;
			}
		}
	return true;
}

static bool TempMonitor_IsActiveMappedChannel(uint8_t sensor_id)
{
	DS18B20_ROM_t mapped_rom;

	if (sensor_id >= DS18B20_MAX_SENSORS) {
		return false;
	}

	/*
	 * Active/mapped канал - это логический канал, в котором сохранен
	 * валидный ROM DS18B20: family code 0x28 + корректный CRC8.
	 *
	 * Пустой канал FF..FF автоматически не проходит DS18B20_IsValidROM().
	 */
	AppConfig_GetSensorROM(sensor_id, &mapped_rom);
	return DS18B20_IsValidROM(&mapped_rom);
}


static void TempMonitor_SetTemperature(uint8_t index, float value)
{
	if (index < DS18B20_MAX_SENSORS && tempMutex != NULL) {
		if (osMutexAcquire(tempMutex, osWaitForever) == osOK) {
			s_latest_temperatures[index] = value;
			osMutexRelease(tempMutex);
			}
		}
}

static void TempMonitor_SendTemperature(uint16_t cmd_code, uint8_t sensor_id)
{
	if (!TempMonitor_IsActiveMappedChannel(sensor_id)) {
		/*
		 * Канал существует как индекс, но не является привязанным
		 * температурным каналом. Это low-level SENSOR_FAILURE,
		 * а не Host-level TEMP_DATA.status.
		 */
		CAN_SendNack(cmd_code, CAN_ERR_SENSOR_FAILURE);
		return;
	}

	float raw_t = TempMonitor_GetTemperature(sensor_id);

	if (raw_t > -100.0f) {
		// Формат температуры: int16, десятые доли градуса Celsius, little-endian.
		int16_t tx_val = (int16_t)(raw_t * 10.0f);
		uint8_t data[2];

		data[0] = (uint8_t)(tx_val & 0xFF);
		data[1] = (uint8_t)((tx_val >> 8) & 0xFF);

		CAN_SendData(cmd_code, data, sizeof(data));
		CAN_SendDone(cmd_code, sensor_id);
	}
	else {
		// Канал active/mapped, но валидного измерения сейчас нет.
		CAN_SendNack(cmd_code, CAN_ERR_SENSOR_FAILURE);
	}
}


static void TempMonitor_SendAllTemperatures(uint16_t cmd_code)
{
	uint8_t active_count = 0U;
	uint8_t valid_count = 0U;

	for (uint8_t i = 0; i < DS18B20_MAX_SENSORS; i++) {
		if (!TempMonitor_IsActiveMappedChannel(i)) {
			continue;
		}

		active_count++;

		float t = TempMonitor_GetTemperature(i);

		if (t > -100.0f) {
			int16_t tx_v = (int16_t)(t * 10.0f);
			uint8_t data[3];

			data[0] = i;
			data[1] = (uint8_t)(tx_v & 0xFF);
			data[2] = (uint8_t)((tx_v >> 8) & 0xFF);

			CAN_SendData(cmd_code, data, sizeof(data));
			valid_count++;
		}
	}

	/*
	 * DONE по GET_ALL означает: команда обработана, и передан
	 * хотя бы один валидный результат по active/mapped каналам.
	 *
	 * Отсутствующие active/mapped каналы Дирижер переведет
	 * в Host TEMP_DATA.status = 3.
	 */
	if (active_count > 0U && valid_count > 0U) {
		CAN_SendDone(cmd_code, 0xFF);
	}
	else {
		CAN_SendNack(cmd_code, CAN_ERR_SENSOR_FAILURE);
	}
}


static void TempMonitor_SendPhysId(uint16_t cmd_code, uint8_t sensor_id)
{
	DS18B20_ROM_t* rom = DS18B20_GetROM(sensor_id);

	if (rom != NULL) {
		uint8_t data[9];
		data[0] = sensor_id;
		memcpy(&data[1], rom->rom_code, sizeof(rom->rom_code));

		// DATA frame несет максимум 6 байт полезной нагрузки,
		// поэтому 64-bit ROM ID передается двумя CAN DATA кадрами.
		CAN_SendData(cmd_code, data, 6);
		CAN_SendData(cmd_code, &data[6], 3);
		CAN_SendDone(cmd_code, sensor_id);
		}
	else {
		CAN_SendNack(cmd_code, CAN_ERR_INVALID_SENSOR_ID);
		}
}

static void TempMonitor_SendChannelMap(uint16_t cmd_code, uint8_t sensor_id)
{
	DS18B20_ROM_t mapped_rom;
	uint8_t data[9];

	AppConfig_GetSensorROM(sensor_id, &mapped_rom);

	data[0] = sensor_id;
	memcpy(&data[1], mapped_rom.rom_code, sizeof(mapped_rom.rom_code));

	CAN_SendData(cmd_code, data, 6);
	CAN_SendData(cmd_code, &data[6], 3);
	CAN_SendDone(cmd_code, sensor_id);
}

static void TempMonitor_ProcessCommand(const ThermoCommand_t *cmd)
{
	switch (cmd->cmd_code) {
	case CAN_CMD_SENSOR_GET_TEMP:
		if (cmd->sensor_id >= DS18B20_MAX_SENSORS) {
			CAN_SendNack(cmd->cmd_code, CAN_ERR_INVALID_SENSOR_ID);
			break;
			}
		TempMonitor_SendTemperature(cmd->cmd_code, cmd->sensor_id);
		break;

	case CAN_CMD_SENSOR_GET_ALL_TEMPS:
		TempMonitor_SendAllTemperatures(cmd->cmd_code);
		break;

	case CAN_CMD_SRV_SCAN_1WIRE: {
		uint8_t count = DS18B20_Init();
		uint8_t data[1];
		data[0] = count;
		CAN_SendData(cmd->cmd_code, data, sizeof(data));
		CAN_SendDone(cmd->cmd_code, count);
		break;
		}

	case CAN_CMD_SRV_GET_PHYS_ID:
		if (cmd->sensor_id >= DS18B20_MAX_SENSORS) {
			CAN_SendNack(cmd->cmd_code, CAN_ERR_INVALID_SENSOR_ID);
			break;
			}
		TempMonitor_SendPhysId(cmd->cmd_code, cmd->sensor_id);
		break;

	case CAN_CMD_SRV_SET_CHANNEL_MAP:
		if (cmd->sensor_id >= DS18B20_MAX_SENSORS) {
			CAN_SendNack(cmd->cmd_code, CAN_ERR_INVALID_SENSOR_ID);
			break;
			}

		if (cmd->data_len < 4U) {
			CAN_SendNack(cmd->cmd_code, CAN_ERR_INVALID_PARAM);
			break;
			}

		// Phase 1: сохраняем первые 4 байта ROM как незавершенную транзакцию.
		// data[4] в текущем DLC=8 остается резервным байтом и не входит в ROM.
		memcpy(&s_rom_id_buffer[0], cmd->data, 4);
		s_rom_map_pending_sensor_id = cmd->sensor_id;
		s_rom_map_pending = true;

		CAN_SendDone(cmd->cmd_code, cmd->sensor_id);
		break;

	case CAN_CMD_SRV_GET_CHANNEL_MAP:{
		if (cmd->sensor_id >= DS18B20_MAX_SENSORS) {
			CAN_SendNack(cmd->cmd_code, CAN_ERR_INVALID_SENSOR_ID);
			break;
		}

		// Возвращает сохраненный ROM-код логического канала.
		TempMonitor_SendChannelMap(cmd->cmd_code, cmd->sensor_id);
		break;
	}



	case CAN_CMD_SRV_SET_CH_MAP_P2: {
		DS18B20_ROM_t new_rom;

		if (cmd->sensor_id >= DS18B20_MAX_SENSORS) {
			CAN_SendNack(cmd->cmd_code, CAN_ERR_INVALID_SENSOR_ID);
			break;
		}

		if (cmd->data_len < 4U ||
		    !s_rom_map_pending ||
		    s_rom_map_pending_sensor_id != cmd->sensor_id) {
			TempMonitor_ClearPendingMap();
			CAN_SendNack(cmd->cmd_code, CAN_ERR_INVALID_PARAM);
			break;
		}

		// Phase 2: принимаем последние 4 байта только для того же sensor_id.
		memcpy(&s_rom_id_buffer[4], cmd->data, 4);
		memcpy(new_rom.rom_code, s_rom_id_buffer, sizeof(new_rom.rom_code));

		// Допускаем два состояния:
		 // 1. валидный DS18B20 ROM;
		 // 2. 0xFF..0xFF как пустой, очищенный канал.
		 if (!TempMonitor_IsEmptyROM(&new_rom) && !DS18B20_IsValidROM(&new_rom)) {
			 TempMonitor_ClearPendingMap();
			 CAN_SendNack(cmd->cmd_code, CAN_ERR_INVALID_PARAM);
			 break;
			 }


		AppConfig_SetSensorROM(cmd->sensor_id, &new_rom);
		TempMonitor_ClearPendingMap();

		CAN_SendDone(cmd->cmd_code, cmd->sensor_id);
		break;
		}

	default:
		CAN_SendNack(cmd->cmd_code, CAN_ERR_UNKNOWN_CMD);
		break;

	}
}

static bool TempMonitor_ProcessPendingCommands(uint32_t timeout_ms)
{
	ThermoCommand_t cmd;
    bool processed = false;

    if (osMessageQueueGet(thermo_queueHandle, &cmd, NULL, timeout_ms) == osOK) {
      processed = true;
      TempMonitor_ProcessCommand(&cmd);

      /*
       * После пробуждения очищаем очередь без ожидания, чтобы серия команд
       * не застревала за очередным циклом измерения.
       */
      while (osMessageQueueGet(thermo_queueHandle, &cmd, NULL, 0) == osOK) {
        processed = true;
        TempMonitor_ProcessCommand(&cmd);
      }
    }

    return processed;
}


static void TempMonitor_ProcessPendingCommandsWithHeartbeat(uint32_t total_timeout_ms)
{
	uint32_t elapsed_ms = 0U;

    while (elapsed_ms < total_timeout_ms)
    {
	uint32_t wait_ms = APP_WATCHDOG_TASK_IDLE_TIMEOUT_MS;
	uint32_t remaining_ms = total_timeout_ms - elapsed_ms;

	if (remaining_ms < wait_ms)
		{
		wait_ms = remaining_ms;
		}
	AppWatchdog_Heartbeat(APP_WDG_CLIENT_TEMP_MONITOR);

	/*
	 * Если команда пришла, сохраняем старое поведение:
	 * обрабатываем ее и сразу выходим в новый цикл измерения.
         */
	if (TempMonitor_ProcessPendingCommands(wait_ms))
		{
		AppWatchdog_Heartbeat(APP_WDG_CLIENT_TEMP_MONITOR);
		return;
		}

	AppWatchdog_Heartbeat(APP_WDG_CLIENT_TEMP_MONITOR);
	elapsed_ms += wait_ms;
	}
}



// --- Публичный API доступа к данным ---
/**
 * @brief Безопасное чтение температуры из другого потока (например, из Dispatcher).
 */
float TempMonitor_GetTemperature(uint8_t index) {
float val = -999.0f;
if (index < DS18B20_MAX_SENSORS && tempMutex != NULL) {
	if (osMutexAcquire(tempMutex, 10) == osOK) { // Ждем максимум 10мс
		val = s_latest_temperatures[index];
		osMutexRelease(tempMutex);
		}
	}
return val;
}

/**
 *
 * @brief Задача мониторинга температуры.
 * Реализует промышленный цикл: Broadcast Start -> RTOS Wait -> Match ROM Read.
 */
void app_start_task_temp_monitor(void *argument)
{
	// 1. Инициализация мьютекса защиты данных
	tempMutex = osMutexNew(&tempMutex_attr);

	// 2. Инициализация массива начальными значениями "Ошибки"
	for (uint8_t i = 0; i < DS18B20_MAX_SENSORS; i++) {
		s_latest_temperatures[i] = -999.0f;
		}

	// Буферы для работы в цикле
	DS18B20_ROM_t target_rom;
	float current_temp = 0.0f;

	for(;;) {
		AppWatchdog_Heartbeat(APP_WDG_CLIENT_TEMP_MONITOR);

		// Сначала обслуживаем команды, которые уже пришли от Dispatcher.
		TempMonitor_ProcessPendingCommands(0);

		AppWatchdog_Heartbeat(APP_WDG_CLIENT_TEMP_MONITOR);

		// 3. ШИРОКОВЕЩАТЕЛЬНЫЙ ЗАПУСК: Все датчики на шине начинают мерить температуру.
	    DS18B20_StartAll();

	    // 4. ОЖИДАНИЕ: Даем датчикам время на замер (750мс).
	    osDelay(TEMP_MONITOR_CONVERSION_DELAY_MS);

	    AppWatchdog_Heartbeat(APP_WDG_CLIENT_TEMP_MONITOR);

	    // 5. ОПРОС ПО ТАБЛИЦЕ МАППИНГА (из Flash)
	    for (uint8_t i = 0; i < DS18B20_MAX_SENSORS; i++) {
		// Читаем ROM ID для канала 'i' из защищенной конфигурации
		AppConfig_GetSensorROM(i, &target_rom);

		// Читаем только валидно привязанный DS18B20 ROM: family code + CRC.
		if (DS18B20_IsValidROM(&target_rom)) {
				// Адресное чтение конкретного датчика
				if (DS18B20_ReadTemperature(&target_rom, &current_temp)) {
					TempMonitor_SetTemperature(i, current_temp);
					}
				else {
					// Ошибка чтения (датчик пропал или помеха)
					TempMonitor_SetTemperature(i, -999.0f);
					}
				}
			else {
				// Канал не настроен (пусто во Flash)
				TempMonitor_SetTemperature(i, -999.0f);
				}

		AppWatchdog_Heartbeat(APP_WDG_CLIENT_TEMP_MONITOR);
		}

	    /*
	     * В idle-окне ждем прикладную команду.
	     * Thermo idle остается 2000 ms, но ожидание разбито на watchdog-safe интервалы.
	     * Если команда пришла, helper обработает ее и сразу вернет задачу в новый цикл измерения.
	     */

	    TempMonitor_ProcessPendingCommandsWithHeartbeat(TEMP_MONITOR_IDLE_DELAY_MS);

	    AppWatchdog_Heartbeat(APP_WDG_CLIENT_TEMP_MONITOR);
	    }
}
