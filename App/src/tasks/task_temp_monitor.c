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
#define TEMP_MONITOR_SAMPLE_PERIOD_MS      3000U
#define TEMP_MONITOR_NO_PENDING_SENSOR     0xFFU

// --- Инкапсулированные данные (скрыты внутри модуля) ---
// --- Последний успешный результат каждого канала ---
// Модель HC адаптирована к восьми независимым каналам Thermo.
// Время относится к началу преобразования; ошибка попытки хранится отдельно.
#define TEMP_MONITOR_MAX_AGE_MS 9000U

typedef enum {
	TEMP_SAMPLE_EMPTY = 0,
	TEMP_SAMPLE_VALID,
	TEMP_SAMPLE_STALE,
	TEMP_SAMPLE_ERROR
} TempSampleStatus_t;

typedef struct {
	float temperature;
	uint32_t sample_started_ms;
	TempSampleStatus_t status;
	uint16_t last_error;
} TempSample_t;

static TempSample_t s_samples[DS18B20_MAX_SENSORS];

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

// --- Публикация успешного измерения ---
// Владелец записи — задача мониторинга. Mutex защищает весь результат.
// Ошибку предыдущей попытки снимаем, возраст считаем от запуска conversion.
static void TempMonitor_SetTemperature(uint8_t index, float value,
		uint32_t sample_started_ms) {
	if (index >= DS18B20_MAX_SENSORS || tempMutex == NULL) {
		return;
	}
	if (osMutexAcquire(tempMutex, osWaitForever) != osOK) {
		return;
	}

	s_samples[index].temperature = value;
	s_samples[index].sample_started_ms = sample_started_ms;
	s_samples[index].status =
			((uint32_t) (HAL_GetTick() - sample_started_ms)
					>= TEMP_MONITOR_MAX_AGE_MS) ?
					TEMP_SAMPLE_STALE : TEMP_SAMPLE_VALID;
	s_samples[index].last_error = CAN_ERR_NONE;

	if (osMutexRelease(tempMutex) != osOK) {
		Error_Handler();
	}
}

// --- Ошибка обновления по модели HC ---
// Сохраняем успешную температуру и её исходное время.
// До первого успешного результата фиксируем состояние ERROR.
static void TempMonitor_SetSampleError(uint8_t index, uint16_t sample_error) {
	if (index >= DS18B20_MAX_SENSORS || tempMutex == NULL) {
		return;
	}
	if (osMutexAcquire(tempMutex, osWaitForever) != osOK) {
		return;
	}

	s_samples[index].last_error = TempMonitor_NormalizeSampleError(
			sample_error);
	if (s_samples[index].status == TEMP_SAMPLE_EMPTY) {
		s_samples[index].status = TEMP_SAMPLE_ERROR;
	}

	if (osMutexRelease(tempMutex) != osOK) {
		Error_Handler();
	}
}

// --- Ошибка общего преобразования ---
// Попытка не дала новых данных для всех каналов; прежние значения сохраняются.
static void TempMonitor_RecordAllSampleErrors(uint16_t sample_error) {
	for (uint8_t i = 0; i < DS18B20_MAX_SENSORS; i++) {
		TempMonitor_SetSampleError(i, sample_error);
	}
}

// --- Очистка записи при смене привязки ---
// Старый результат не должен перейти к новому датчику.
// При отказе очистки нельзя продолжать работу с изменённой ROM-map.
static void TempMonitor_ClearSample(uint8_t index) {
	if (index >= DS18B20_MAX_SENSORS || tempMutex == NULL) {
		Error_Handler();
		return;
	}
	if (osMutexAcquire(tempMutex, osWaitForever) != osOK) {
		Error_Handler();
		return;
	}

	s_samples[index] = (TempSample_t ) { 0 };

	if (osMutexRelease(tempMutex) != osOK) {
		Error_Handler();
	}
}

// --- Чтение пригодного результата ---
// Как в HC, свежая успешная запись доступна даже после ошибки обновления.
// STALE сохраняется до нового измерения; ошибка не обновляет timestamp.
// -999 остаётся только значением отсутствия данных для публичного float API.
static bool TempMonitor_GetSample(uint8_t index, float *out_value,
		uint16_t *out_error) {
	bool valid = false;
	float value = -999.0f;
	uint16_t sample_error = CAN_ERR_SENSOR_FAILURE;

	if (index < DS18B20_MAX_SENSORS && tempMutex != NULL) {
		if (osMutexAcquire(tempMutex, 10) == osOK) {
			TempSample_t *sample = &s_samples[index];

			if (sample->status
					== TEMP_SAMPLE_VALID&& (uint32_t)(HAL_GetTick() - sample->sample_started_ms)
					>= TEMP_MONITOR_MAX_AGE_MS) {
				sample->status = TEMP_SAMPLE_STALE;
			}

			valid = (sample->status == TEMP_SAMPLE_VALID);
			if (valid) {
				value = sample->temperature;
				sample_error = CAN_ERR_NONE;
			} else {
				sample_error = TempMonitor_NormalizeSampleError(
						sample->last_error);
			}
			osMutexRelease(tempMutex);
		}
	}

	if (out_value != NULL) {
		*out_value = value;
	}
	if (out_error != NULL) {
		*out_error = sample_error;
	}
	return valid;
}

// --- Ожидание готовности датчиков с учётом отмены ---

// --- Состояние фонового измерения ---
/*
 * Контекст принадлежит задаче мониторинга. START выполняется при переходе
 * IDLE -> WAIT; READ обрабатывает один канал за вызов. Размер кеша и карты
 * остаётся равным возможностям универсальной платы: восемь каналов.
 */
typedef enum {
	TEMP_CYCLE_IDLE = 0, TEMP_CYCLE_WAIT, TEMP_CYCLE_READ
} TempCycleState_t;

typedef struct {
	TempCycleState_t state;
	bool attempted;
	uint32_t started_ms;
	uint32_t polled_ms;
	uint8_t read_index;
} TempCycle_t;

// --- Один шаг измерительного тракта ---
/*
 * Между вызовами обслуживается одна команда.
 * Во время WAIT другой обмен 1-Wire запрещён; во время READ ROM-map
 * неизменна. Отложенная сервисная команда запрещает начало нового цикла.
 * Отмена при reset не регистрируется как ошибка измерения.
 */
static void TempMonitor_ServiceMeasurement(TempCycle_t *cycle,
bool allow_start) {
	if (AppSafety_IsResetPending()) {
		cycle->state = TEMP_CYCLE_IDLE;
		TempMonitor_ClearPendingMap();
		return;
	}

	uint32_t now_ms = HAL_GetTick();

	switch (cycle->state) {
	case TEMP_CYCLE_IDLE: {
		if (!allow_start
				|| (cycle->attempted
						&& (uint32_t) (now_ms - cycle->started_ms)
								< TEMP_MONITOR_SAMPLE_PERIOD_MS)) {
			return;
		}

		cycle->attempted = true;
		cycle->started_ms = now_ms;

		bool started = DS18B20_StartAll();

		if (AppSafety_IsResetPending()) {
			return;
		}

		if (!started) {
			TempMonitor_RecordAllSampleErrors(CAN_ERR_THERMO_COMM);
			DS18B20_BusRelease();
			return;
		}

		cycle->polled_ms = HAL_GetTick();
		cycle->state = TEMP_CYCLE_WAIT;
		return;
	}

	case TEMP_CYCLE_WAIT: {
		uint32_t elapsed_ms = now_ms - cycle->started_ms;

		if ((uint32_t) (now_ms - cycle->polled_ms)
				< TEMP_MONITOR_CONVERSION_POLL_MS
				&& elapsed_ms < TEMP_MONITOR_CONVERSION_TIMEOUT_MS) {
			return;
		}

		cycle->polled_ms = now_ms;

		bool ready = DS18B20_IsConversionComplete();

		if (AppSafety_IsResetPending()) {
			cycle->state = TEMP_CYCLE_IDLE;
			return;
		}

		if (ready) {
			cycle->read_index = 0U;
			cycle->state = TEMP_CYCLE_READ;
		} else if ((uint32_t) (HAL_GetTick() - cycle->started_ms)
				>= TEMP_MONITOR_CONVERSION_TIMEOUT_MS) {
			TempMonitor_RecordAllSampleErrors(
			CAN_ERR_THERMO_CONVERSION_TIMEOUT);
			DS18B20_BusRelease();
			cycle->state = TEMP_CYCLE_IDLE;
		}

		return;
	}

	case TEMP_CYCLE_READ: {
		DS18B20_ROM_t rom;
		float temperature = 0.0f;
		uint8_t index = cycle->read_index;

		AppConfig_GetSensorROM(index, &rom);

		if (AppSafety_IsResetPending()) {
			cycle->state = TEMP_CYCLE_IDLE;
			return;
		}

		if (DS18B20_IsValidROM(&rom)) {
			DS18B20_ReadResult_t result = DS18B20_ReadTemperature(&rom,
					&temperature);

			if (result == DS18B20_READ_CANCELLED
					|| AppSafety_IsResetPending()) {
				cycle->state = TEMP_CYCLE_IDLE;
				return;
			}

			if (result == DS18B20_READ_OK) {
				TempMonitor_SetTemperature(index, temperature,
						cycle->started_ms);
			} else {
				TempMonitor_SetSampleError(index, CAN_ERR_THERMO_COMM);
			}
		} else {
			TempMonitor_ClearSample(index);
		}

		cycle->read_index++;

		if (cycle->read_index >= DS18B20_MAX_SENSORS) {
			cycle->state = TEMP_CYCLE_IDLE;
		}

		return;
	}
	}
}

// --- Ожидание до следующего шага ---
/*
 * WAIT просыпается к проверке готовности или timeout, READ не ждёт.
 * IDLE сохраняет период ограничивает ожидание окном watchdog.
 */
static uint32_t TempMonitor_GetWaitMs(const TempCycle_t *cycle) {
	if (AppSafety_IsResetPending()) {
		return APP_WATCHDOG_TASK_IDLE_TIMEOUT_MS;
	}

	uint32_t now_ms = HAL_GetTick();
	uint32_t elapsed_ms = now_ms - cycle->started_ms;

	if (cycle->state == TEMP_CYCLE_READ) {
		return 0U;
	}

	if (cycle->state == TEMP_CYCLE_WAIT) {
		uint32_t poll_elapsed_ms = now_ms - cycle->polled_ms;

		if (elapsed_ms >= TEMP_MONITOR_CONVERSION_TIMEOUT_MS
				|| poll_elapsed_ms >= TEMP_MONITOR_CONVERSION_POLL_MS) {
			return 0U;
		}

		uint32_t wait_ms =
		TEMP_MONITOR_CONVERSION_POLL_MS - poll_elapsed_ms;
		uint32_t remaining_ms =
		TEMP_MONITOR_CONVERSION_TIMEOUT_MS - elapsed_ms;

		return wait_ms < remaining_ms ? wait_ms : remaining_ms;
	}

	if (!cycle->attempted || elapsed_ms >= TEMP_MONITOR_SAMPLE_PERIOD_MS) {
		return 0U;
	}

	uint32_t wait_ms = TEMP_MONITOR_SAMPLE_PERIOD_MS - elapsed_ms;

	return wait_ms < APP_WATCHDOG_TASK_IDLE_TIMEOUT_MS ?
			wait_ms : APP_WATCHDOG_TASK_IDLE_TIMEOUT_MS;
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

	bool valid = TempMonitor_GetSample(sensor_id, &raw_t, &sample_error);

	if (valid) {
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

		bool valid = TempMonitor_GetSample(i, &t, &sample_error);

		if (valid) {
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

		// После применения карты прежнее измерение канала больше не пригодно.
		TempMonitor_ClearSample(cmd->sensor_id);

		CAN_SendDone(cmd->cmd_code, cmd->sensor_id);
		break;

	}

	default:
		CAN_SendNack(cmd->cmd_code, CAN_ERR_UNKNOWN_CMD);
		break;

	}
}

// --- Ожидание следующей попытки измерения ---

// --- Сервисные команды между измерительными циклами ---
/*
 * Scan использует общую шину, F103/F105 меняют привязку. Сохраняем FIFO:
 * одна такая команда ждёт локально, следующие остаются в штатной очереди.
 * GET за отложенной сервисной командой тоже ждёт; отдельной очереди нет.
 */
static bool TempMonitor_CommandNeedsIdle(uint16_t cmd_code) {
	return cmd_code == CAN_CMD_SRV_SCAN_1WIRE
			|| cmd_code == CAN_CMD_SRV_SET_CHANNEL_MAP
			|| cmd_code == CAN_CMD_SRV_SET_CH_MAP_P2;
}

static uint32_t TempMonitor_MsToTicks(uint32_t timeout_ms) {
	return (timeout_ms * osKernelGetTickFreq() + 999U) / 1000U;
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

void app_start_task_temp_monitor(void *argument) {
	(void) argument;

	tempMutex = osMutexNew(&tempMutex_attr);

	if (tempMutex == NULL) {
		Error_Handler();
		return;
	}

	memset(s_samples, 0, sizeof(s_samples));

	TempCycle_t cycle = { 0 };
	ThermoCommand_t deferred_command;
	bool deferred = false;

	for (;;) {
		AppWatchdog_Heartbeat(APP_WDG_CLIENT_TEMP_MONITOR);

		TempMonitor_ServiceMeasurement(&cycle, !deferred);

		if (deferred) {
			if (cycle.state == TEMP_CYCLE_IDLE || AppSafety_IsResetPending()) {
				TempMonitor_ProcessCommand(&deferred_command);
				deferred = false;
			} else {
				uint32_t wait_ms = TempMonitor_GetWaitMs(&cycle);

				if (wait_ms != 0U
						&& osDelay(TempMonitor_MsToTicks(wait_ms)) != osOK) {
					Error_Handler();
					return;
				}
			}
		} else {
			ThermoCommand_t command;
			uint32_t wait_ms = TempMonitor_GetWaitMs(&cycle);

			if (osMessageQueueGet(thermo_queueHandle, &command, NULL,
					TempMonitor_MsToTicks(wait_ms)) == osOK) {
				if (!AppSafety_IsResetPending()
						&& cycle.state != TEMP_CYCLE_IDLE
						&& TempMonitor_CommandNeedsIdle(command.cmd_code)) {
					deferred_command = command;
					deferred = true;
				} else {
					TempMonitor_ProcessCommand(&command);
				}
			}
		}

		AppWatchdog_Heartbeat(APP_WDG_CLIENT_TEMP_MONITOR);
	}
}

