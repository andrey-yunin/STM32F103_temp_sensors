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
#include "app_safety.h"
#include "main.h"
#include <string.h>
#include <stdbool.h>

#define TEMP_MONITOR_CONVERSION_TIMEOUT_MS 800U
#define TEMP_MONITOR_CONVERSION_POLL_MS    10U
#define TEMP_MONITOR_IDLE_DELAY_MS         2000U
#define TEMP_MONITOR_NO_PENDING_SENSOR     0xFFU

// --- Инкапсулированные данные (скрыты внутри модуля) ---
static float s_latest_temperatures[DS18B20_MAX_SENSORS];
static uint16_t s_latest_sample_errors[DS18B20_MAX_SENSORS];
static uint8_t s_rom_id_buffer[8];
static uint8_t s_rom_map_pending_sensor_id = TEMP_MONITOR_NO_PENDING_SENSOR;
static bool s_rom_map_pending = false;

static osMutexId_t tempMutex = NULL;

const osMutexAttr_t tempMutex_attr = { "tempMutex",
osMutexPrioInherit,
NULL, 0U };

static void TempMonitor_ClearPendingMap(void) {
	memset(s_rom_id_buffer, 0xFF, sizeof(s_rom_id_buffer));
	s_rom_map_pending_sensor_id = TEMP_MONITOR_NO_PENDING_SENSOR;
	s_rom_map_pending = false;
}

// --- Отмена доменной команды при сервисном reset ---
/*
 * По модели HC отклоняем работу существующим DEVICE_BUSY.
 * Буфер привязки очищает только его владелец — задача мониторинга.
 * Вызов после обмена/ожидания отличает отмену от неисправности датчика.
 */
static bool TempMonitor_RejectDuringReset(uint16_t cmd_code) {
	if (!AppSafety_IsResetPending()) {
		return false;
	}
	TempMonitor_ClearPendingMap();
	CAN_SendNack(cmd_code, CAN_ERR_DEVICE_BUSY);
	return true;
}

static bool TempMonitor_IsEmptyROM(const DS18B20_ROM_t *rom) {
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

static bool TempMonitor_IsActiveMappedChannel(uint8_t sensor_id) {
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

static uint16_t TempMonitor_NormalizeSampleError(uint16_t sample_error) {
	switch (sample_error) {
	case CAN_ERR_THERMO_COMM:
	case CAN_ERR_THERMO_CONVERSION_TIMEOUT:
		return sample_error;

	default:
		return CAN_ERR_SENSOR_FAILURE;
	}
}

static uint16_t TempMonitor_SelectAggregateSampleError(uint16_t current_error,
		uint16_t candidate_error) {
	uint16_t normalized_candidate = TempMonitor_NormalizeSampleError(
			candidate_error);

	if (current_error == CAN_ERR_THERMO_CONVERSION_TIMEOUT
			|| normalized_candidate == CAN_ERR_THERMO_CONVERSION_TIMEOUT) {
		return CAN_ERR_THERMO_CONVERSION_TIMEOUT;
	}

	if (current_error == CAN_ERR_THERMO_COMM
			|| normalized_candidate == CAN_ERR_THERMO_COMM) {
		return CAN_ERR_THERMO_COMM;
	}

	return CAN_ERR_SENSOR_FAILURE;
}

// --- Сохранение результата измерения ---

/*
 * Задача мониторинга обновляет значение и его ошибку вместе.
 * Mutex защищает данные от конкурентного чтения.
 * Отмену измерения обрабатывает вызывающий код.
 */
static void TempMonitor_SetSample(uint8_t index, float value,
		uint16_t sample_error) {
	if (index >= DS18B20_MAX_SENSORS || tempMutex == NULL) {
		return;
	}

	if (osMutexAcquire(tempMutex, osWaitForever) != osOK) {
		return;
	}

	s_latest_temperatures[index] = value;
	s_latest_sample_errors[index] = sample_error;

	if (osMutexRelease(tempMutex) != osOK) {
		Error_Handler();
	}
}

static void TempMonitor_GetSample(uint8_t index, float *out_value,
		uint16_t *out_error) {
	float value = -999.0f;
	uint16_t sample_error = CAN_ERR_SENSOR_FAILURE;

	if (index < DS18B20_MAX_SENSORS && tempMutex != NULL) {
		if (osMutexAcquire(tempMutex, 10) == osOK) {
			value = s_latest_temperatures[index];
			sample_error = s_latest_sample_errors[index];
			osMutexRelease(tempMutex);
		}
	}

	if (out_value != NULL) {
		*out_value = value;
	}

	if (out_error != NULL) {
		*out_error = TempMonitor_NormalizeSampleError(sample_error);
	}
}

static void TempMonitor_SetTemperature(uint8_t index, float value) {
	TempMonitor_SetSample(index, value, CAN_ERR_NONE);
}

static void TempMonitor_SetSampleError(uint8_t index, uint16_t sample_error) {
	TempMonitor_SetSample(index, -999.0f,
			TempMonitor_NormalizeSampleError(sample_error));
}

static void TempMonitor_InvalidateAllTemperatures(uint16_t sample_error) {
	for (uint8_t i = 0; i < DS18B20_MAX_SENSORS; i++) {
		TempMonitor_SetSampleError(i, sample_error);
	}
}

// --- Ожидание готовности датчиков с учётом отмены ---

/*
 * Задача мониторинга опрашивает завершение conversion.
 * При reset прекращает ожидание без дальнейшего опроса шины.
 * Текущее RTOS-ожидание завершается штатно.
 *
 * false означает отсутствие подтверждённой готовности.
 * Вызывающий код обязан отличать отмену от timeout по reset_pending.
 * Существующий способ отсчёта времени здесь сохраняется.
 */
static bool TempMonitor_WaitConversionComplete(uint32_t timeout_ms) {
	uint32_t elapsed_ms = 0U;

	while (elapsed_ms < timeout_ms) {
		if (AppSafety_IsResetPending()) {
			return false;
		}

		if (DS18B20_IsConversionComplete()) {
			return !AppSafety_IsResetPending();
		}

		if (AppSafety_IsResetPending()) {
			return false;
		}

		uint32_t wait_ms = TEMP_MONITOR_CONVERSION_POLL_MS;
		uint32_t remaining_ms = timeout_ms - elapsed_ms;

		if (remaining_ms < wait_ms) {
			wait_ms = remaining_ms;
		}

		AppWatchdog_Heartbeat(APP_WDG_CLIENT_TEMP_MONITOR);

		if (osDelay(wait_ms) != osOK) {
			Error_Handler();
			return false;
		}

		elapsed_ms += wait_ms;
	}

	if (AppSafety_IsResetPending()) {
		return false;
	}

	return DS18B20_IsConversionComplete();
}

static void TempMonitor_SendTemperature(uint16_t cmd_code, uint8_t sensor_id) {
	if (!TempMonitor_IsActiveMappedChannel(sensor_id)) {
		/*
		 * Канал существует как индекс, но не привязан к валидному DS18B20 ROM.
		 * Executor сообщает low-level факт SENSOR_NOT_FOUND; Host-смысл
		 * выбирает Дирижер в direct-route mapper.
		 */
		CAN_SendNack(cmd_code, CAN_ERR_THERMO_SENSOR_NOT_FOUND);
		return;
	}

	float raw_t = -999.0f;
	uint16_t sample_error = CAN_ERR_SENSOR_FAILURE;

	TempMonitor_GetSample(sensor_id, &raw_t, &sample_error);

	if (raw_t > -100.0f) {
		// Формат температуры: int16, десятые доли градуса Celsius, little-endian.
		int16_t tx_val = (int16_t) (raw_t * 10.0f);
		uint8_t data[2];

		data[0] = (uint8_t) (tx_val & 0xFF);
		data[1] = (uint8_t) ((tx_val >> 8) & 0xFF);

		/* Одна температура — самостоятельный DATA; канал указан в запросе/DONE. */
		CAN_SendData(cmd_code, CAN_DATA_SEQ_EOT_MASK, data, sizeof(data));
		CAN_SendDone(cmd_code, sensor_id);
	}

	else {
		// Канал active/mapped, но валидного измерения сейчас нет.
		CAN_SendNack(cmd_code, sample_error);
	}

}

static void TempMonitor_SendAllTemperatures(uint16_t cmd_code) {
	uint8_t active_count = 0U;
	uint8_t valid_count = 0U;
	uint16_t aggregate_error = CAN_ERR_SENSOR_FAILURE;

	for (uint8_t i = 0; i < DS18B20_MAX_SENSORS; i++) {
		if (!TempMonitor_IsActiveMappedChannel(i)) {
			continue;
		}

		active_count++;

		float t = -999.0f;
		uint16_t sample_error = CAN_ERR_SENSOR_FAILURE;

		TempMonitor_GetSample(i, &t, &sample_error);

		if (t > -100.0f) {
			int16_t tx_v = (int16_t) (t * 10.0f);
			uint8_t data[3];

			data[0] = i;
			data[1] = (uint8_t) (tx_v & 0xFF);
			data[2] = (uint8_t) ((tx_v >> 8) & 0xFF);

			/*
			 * Одна запись содержит канал и его температуру.
			 * Каждая запись завершена; весь набор завершается существующим DONE.
			 */
			CAN_SendData(cmd_code, CAN_DATA_SEQ_EOT_MASK, data, sizeof(data));
			valid_count++;
		} else {
			aggregate_error = TempMonitor_SelectAggregateSampleError(
					aggregate_error, sample_error);
		}
	}

	/*
	 * DONE по GET_ALL означает: команда обработана, и передан
	 * хотя бы один валидный результат по active/mapped каналам.
	 */
	if (active_count == 0U) {
		CAN_SendNack(cmd_code, CAN_ERR_THERMO_SENSOR_NOT_FOUND);
	} else if (valid_count == 0U) {
		CAN_SendNack(cmd_code, aggregate_error);
	}

	else {
		CAN_SendDone(cmd_code, 0xFF);
	}
}

// --- F102: компактный идентификатор обнаруженного датчика ---

/*
 * Домен Thermo читает ROM из списка последнего обнаружения.
 * После проверки family и исходного CRC передаёт все шесть байтов serial.
 * Физический индекс определяется запросом и повторяется в DONE.
 * Новое сканирование и температурное преобразование здесь не запускаются.
 */
static void TempMonitor_SendPhysId(uint16_t cmd_code, uint8_t sensor_id) {
	const DS18B20_ROM_t *rom = DS18B20_GetROM(sensor_id);
	if (TempMonitor_RejectDuringReset(cmd_code)) {
		return;
	}

	if (rom == NULL) {
		CAN_SendNack(cmd_code, CAN_ERR_INVALID_SENSOR_ID);
		return;
	}

	if (!DS18B20_IsValidROM(rom)) {
		CAN_SendNack(cmd_code, CAN_ERR_THERMO_SENSOR_FAILURE);
		return;
	}

	CAN_SendData(cmd_code, CAN_DATA_SEQ_EOT_MASK, &rom->rom_code[1], 6U);
	CAN_SendDone(cmd_code, sensor_id);
}

// --- F104: компактный идентификатор привязанного датчика ---

/*
 * Домен читает сохранённую привязку логического канала.
 * Полный ROM остаётся в конфигурации, по CAN передаются ROM[1..6].
 * Пустая привязка FF..FF даёт E401 без DATA/DONE.
 * Непустой, но некорректный ROM даёт E400: это не пустой канал.
 * Ответ подтверждает содержимое маппинга, а не присутствие датчика на шине.
 */
static void TempMonitor_SendChannelMap(uint16_t cmd_code, uint8_t sensor_id) {
	DS18B20_ROM_t mapped_rom = { 0 };

	AppConfig_GetSensorROM(sensor_id, &mapped_rom);

	if (TempMonitor_IsEmptyROM(&mapped_rom)) {
		CAN_SendNack(cmd_code, CAN_ERR_THERMO_SENSOR_NOT_FOUND);
		return;
	}

	if (!DS18B20_IsValidROM(&mapped_rom)) {
		CAN_SendNack(cmd_code, CAN_ERR_THERMO_SENSOR_FAILURE);
		return;
	}

	CAN_SendData(cmd_code, CAN_DATA_SEQ_EOT_MASK, &mapped_rom.rom_code[1], 6U);
	CAN_SendDone(cmd_code, sensor_id);
}

static void TempMonitor_ProcessCommand(const ThermoCommand_t *cmd) {
	if (TempMonitor_RejectDuringReset(cmd->cmd_code)) {
		return;
	}
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
		if (TempMonitor_RejectDuringReset(cmd->cmd_code)) {
			break;
		}
		uint8_t data[1];
		data[0] = count;

		/* Количество обнаруженных датчиков помещается в один DATA. */
		CAN_SendData(cmd->cmd_code, CAN_DATA_SEQ_EOT_MASK, data, sizeof(data));
		/* DONE завершает сканирование и подтверждает количество датчиков. */
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

		/* При отмене локальная заготовка удаляется, F105 её не применит. */
		if (TempMonitor_RejectDuringReset(cmd->cmd_code)) {
			break;
		}

		CAN_SendDone(cmd->cmd_code, cmd->sensor_id);
		break;

	case CAN_CMD_SRV_GET_CHANNEL_MAP: {
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

		if (cmd->data_len < 4U || !s_rom_map_pending
				|| s_rom_map_pending_sensor_id != cmd->sensor_id) {
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
		if (!TempMonitor_IsEmptyROM(&new_rom)
				&& !DS18B20_IsValidROM(&new_rom)) {
			TempMonitor_ClearPendingMap();
			CAN_SendNack(cmd->cmd_code, CAN_ERR_INVALID_PARAM);
			break;
		}

		bool applied = AppConfig_SetSensorROM(cmd->sensor_id, &new_rom);
		TempMonitor_ClearPendingMap();
		if (!applied) {
			if (!TempMonitor_RejectDuringReset(cmd->cmd_code)) {
				/* Валидная привязка не применена из-за отказа доступа к config. */
				Error_Handler();
			}
			break;
		}

		CAN_SendDone(cmd->cmd_code, cmd->sensor_id);
		break;
	}

	default:
		CAN_SendNack(cmd->cmd_code, CAN_ERR_UNKNOWN_CMD);
		break;

	}
}

static bool TempMonitor_ProcessPendingCommands(uint32_t timeout_ms) {
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

static void TempMonitor_ProcessPendingCommandsWithHeartbeat(
		uint32_t total_timeout_ms) {
	uint32_t elapsed_ms = 0U;

	while (elapsed_ms < total_timeout_ms) {
		uint32_t wait_ms = APP_WATCHDOG_TASK_IDLE_TIMEOUT_MS;
		uint32_t remaining_ms = total_timeout_ms - elapsed_ms;

		if (remaining_ms < wait_ms) {
			wait_ms = remaining_ms;
		}
		AppWatchdog_Heartbeat(APP_WDG_CLIENT_TEMP_MONITOR);

		/*
		 * Если команда пришла, сохраняем старое поведение:
		 * обрабатываем ее и сразу выходим в новый цикл измерения.
		 */
		if (TempMonitor_ProcessPendingCommands(wait_ms)) {
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

	TempMonitor_GetSample(index, &val, NULL);
	return val;
}

/**
 *
 * @brief Задача мониторинга температуры.
 * Реализует промышленный цикл: Broadcast Start -> RTOS Wait -> Match ROM Read.
 */
void app_start_task_temp_monitor(void *argument) {
	// 1. Инициализация мьютекса защиты данных
	tempMutex = osMutexNew(&tempMutex_attr);

	/*
	 * Mutex защищает согласованное чтение значения и ошибки измерения.
	 * При отказе создания мониторинг не запускается.
	 */
	if (tempMutex == NULL) {
		Error_Handler();
		return;
	}

	// 2. Инициализация массива начальными значениями "Ошибки"
	for (uint8_t i = 0; i < DS18B20_MAX_SENSORS; i++) {
		s_latest_temperatures[i] = -999.0f;
		s_latest_sample_errors[i] = CAN_ERR_SENSOR_FAILURE;
	}

	// Буферы для работы в цикле
	DS18B20_ROM_t target_rom;
	float current_temp = 0.0f;

	for (;;) {
		AppWatchdog_Heartbeat(APP_WDG_CLIENT_TEMP_MONITOR);

		/*
		 * Подготовка reset прекращает измерения, но не работу RTOS-задачи.
		 * Как в HC, очередь обслуживается с отказом DEVICE_BUSY.
		 * Штатное ожидание сохраняет CPU для CAN и dispatcher.
		 */
		if (AppSafety_IsResetPending()) {
			TempMonitor_ClearPendingMap();
			TempMonitor_ProcessPendingCommands(
					APP_WATCHDOG_TASK_IDLE_TIMEOUT_MS);
			continue;
		}

		// Сначала обслуживаем команды, которые уже пришли от Dispatcher.
		TempMonitor_ProcessPendingCommands(0);

		AppWatchdog_Heartbeat(APP_WDG_CLIENT_TEMP_MONITOR);

		// 3. Широковещательный запуск conversion на всех датчиках.
		bool conversion_started = DS18B20_StartAll();
		bool conversion_ready = false;

		if (conversion_started) {
			conversion_ready = TempMonitor_WaitConversionComplete(
			TEMP_MONITOR_CONVERSION_TIMEOUT_MS);
		}

		/* Отменённая операция не регистрируется как COMM или timeout. */
		if (AppSafety_IsResetPending()) {
			continue;
		}

		if (!conversion_ready) {
			/*
			 * Timeout фиксируем только если CONVERT T был реально отправлен.
			 * Если не было presence pulse при старте, это bus/presence failure,
			 * а не conversion timeout; это low-level COMM.
			 */
			uint16_t sample_error = conversion_started ?
			CAN_ERR_THERMO_CONVERSION_TIMEOUT :
															CAN_ERR_THERMO_COMM;

			TempMonitor_InvalidateAllTemperatures(sample_error);
			DS18B20_BusRelease();

			AppWatchdog_Heartbeat(APP_WDG_CLIENT_TEMP_MONITOR);
			TempMonitor_ProcessPendingCommandsWithHeartbeat(
			TEMP_MONITOR_IDLE_DELAY_MS);
			AppWatchdog_Heartbeat(APP_WDG_CLIENT_TEMP_MONITOR);
			continue;
		}

		AppWatchdog_Heartbeat(APP_WDG_CLIENT_TEMP_MONITOR);

		// 4. ОПРОС ПО ТАБЛИЦЕ МАППИНГА (из Flash)
		for (uint8_t i = 0; i < DS18B20_MAX_SENSORS; i++) {
			// Читаем ROM ID для канала 'i' из защищенной конфигурации
			AppConfig_GetSensorROM(i, &target_rom);

			// Читаем только валидно привязанный DS18B20 ROM: family code + CRC.
			if (DS18B20_IsValidROM(&target_rom)) {
				DS18B20_ReadResult_t read_result = DS18B20_ReadTemperature(
						&target_rom, &current_temp);

				/*
				 * Подготовка reset не является отказом датчика.
				 * Прекращаем обход каналов без записи ошибки.
				 */
				if (read_result == DS18B20_READ_CANCELLED
						|| AppSafety_IsResetPending()) {
					break;
				}

				if (read_result == DS18B20_READ_OK) {
					TempMonitor_SetTemperature(i, current_temp);
				} else {
					// Ошибка чтения DS18B20: no presence / read failure / CRC mismatch.
					TempMonitor_SetSampleError(i, CAN_ERR_THERMO_COMM);
				}
			} else {
				// Канал не настроен (пусто во Flash)
				TempMonitor_SetSampleError(i, CAN_ERR_SENSOR_FAILURE);
			}

			AppWatchdog_Heartbeat(APP_WDG_CLIENT_TEMP_MONITOR);
		}

		/*
		 * В idle-окне ждем прикладную команду.
		 * Thermo idle остается 2000 ms, но ожидание разбито на watchdog-safe интервалы.
		 * Если команда пришла, helper обработает ее и сразу вернет задачу в новый цикл измерения.
		 */

		TempMonitor_ProcessPendingCommandsWithHeartbeat(
		TEMP_MONITOR_IDLE_DELAY_MS);

		AppWatchdog_Heartbeat(APP_WDG_CLIENT_TEMP_MONITOR);
	}
}
