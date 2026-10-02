/*
 * app_flash.c
 *
 *  Created on: Apr 1, 2026
 *      Author: andrey
 */

#include "app_flash.h"
#include "app_safety.h"
#include "main.h"
#include "cmsis_os.h"  // Для osMutex
#include <string.h>
#include <stdbool.h>
#include <stddef.h>

#include "can_protocol.h"

// --- Инкапсулированные данные (скрыты внутри модуля) ---
static AppConfig_t g_app_config;
static osMutexId_t configMutex = NULL;

// Атрибуты мьютекса (стандарт FreeRTOS CMSIS_V2)
const osMutexAttr_t configMutex_attr = { "configMutex",                  // name
		osMutexRecursive | osMutexPrioInherit,  // attr_bits
		NULL,                                   // cb_mem
		0U                                      // cb_size
		};

// --- Внутренние вспомогательные функции ---

/**
 * @brief Чтение 96-битного уникального идентификатора чипа (MCU UID).
 * @param out_uid Указатель на массив размером 12 байт.
 */
void AppConfig_GetMCU_UID(uint8_t *out_uid) {
	if (out_uid == NULL)
		return;
	// Адрес UID для STM32F103 (согласно Reference Manual)
	uint8_t *uid_base = (uint8_t*) 0x1FFFF7E8;
	memcpy(out_uid, uid_base, 12);
}

// --- Проверка целостности конфигурации ---

/*
 * CRC16 охватывает конфигурацию до поля checksum.
 * Функция только читает данные и допускает указатель на Flash.
 * Алгоритм и область расчёта сохраняются для совместимости записей.
 */
static uint16_t CalculateChecksum(const AppConfig_t *cfg) {
	uint16_t crc = 0xFFFFU;
	const uint8_t *bytes = (const uint8_t*) cfg;
	const size_t length = offsetof(AppConfig_t, checksum);

	for (size_t index = 0U; index < length; index++) {
		crc ^= bytes[index];

		for (uint8_t bit = 0U; bit < 8U; bit++) {
			if ((crc & 0x0001U) != 0U) {
				crc = (uint16_t) ((crc >> 1U) ^ 0xA001U);
			} else {
				crc >>= 1U;
			}
		}
	}

	return crc;
}

// --- Загрузка конфигурации из Flash ---

/*
 * При старте принимаем конфигурацию только при корректных magic, CRC
 * и принадлежности NodeID адресной группе Thermo.
 * Валидная запись загружается целиком вместе с маппингом датчиков.
 * При отказе формируем заводские настройки только в RAM.
 * Запись во Flash выполняется отдельно через F003.
 */
void AppConfig_Init(void) {
	const AppConfig_t *flash_cfg;

	/* Сохраняем существующий порядок создания mutex. */
	if (configMutex == NULL) {
		configMutex = osMutexNew(&configMutex_attr);
	}

	/*
	 * Как в HC: без mutex конфигурации штатная работа недопустима.
	 * При отказе создания переходим в существующий аварийный путь.
	 */
	if (configMutex == NULL) {
		Error_Handler();
		return;
	}

	flash_cfg = (const AppConfig_t*) APP_CONFIG_FLASH_ADDR;

	if ((flash_cfg->magic == APP_CONFIG_MAGIC)
			&& (flash_cfg->checksum == CalculateChecksum(flash_cfg))
			&& (flash_cfg->performer_id >= CAN_ADDR_THERMO_BOARD_BASE)
			&& (flash_cfg->performer_id <= CAN_ADDR_THERMO_BOARD_LAST)) {
		memcpy(&g_app_config, flash_cfg, sizeof(g_app_config));
	} else {
		/*
		 * Конфигурация не принята.
		 * Заводской адрес и пустой маппинг создаются в RAM;
		 * исходная страница Flash остаётся нетронутой.
		 */
		memset(&g_app_config, 0, sizeof(g_app_config));
		g_app_config.magic = APP_CONFIG_MAGIC;
		g_app_config.performer_id = CAN_ADDR_THERMO_BOARD_BASE;

		for (uint8_t i = 0U; i < DS18B20_MAX_SENSORS; i++) {
			memset(g_app_config.sensors[i].rom_code, 0xFF,
					sizeof(g_app_config.sensors[i].rom_code));
		}

		g_app_config.checksum = CalculateChecksum(&g_app_config);
	}
}

// --- Стирание страницы конфигурации ---

/*
 * Механизм HC: сериализуем доступ к конфигурации через configMutex.
 * Стираем только выделенную страницу, проверяем результаты HAL.
 * После попытки закрываем доступ к Flash и освобождаем mutex.
 * Ошибка не гарантирует сохранность прежней записи: стирание могло
 * уже состояться. RAM-конфигурация здесь не изменяется.
 */
bool AppConfig_FactoryReset(void) {
	FLASH_EraseInitTypeDef erase_config = { 0 };
	uint32_t page_error = 0U;
	HAL_StatusTypeDef status;

	if (configMutex == NULL) {
		return false;
	}

	if (osMutexAcquire(configMutex, osWaitForever) != osOK) {
		return false;
	}

	erase_config.TypeErase = FLASH_TYPEERASE_PAGES;
	erase_config.PageAddress = APP_CONFIG_FLASH_ADDR;
	erase_config.NbPages = 1U;

	/* Стирание разрешено только после успешной разблокировки. */
	status = HAL_FLASH_Unlock();

	if (status == HAL_OK) {
		status = HAL_FLASHEx_Erase(&erase_config, &page_error);
	}

	/* Проверяем блокировку и после неудачной попытки операции. */
	if (HAL_FLASH_Lock() != HAL_OK) {
		status = HAL_ERROR;
	}

	if (osMutexRelease(configMutex) != osOK) {
		Error_Handler();
		return false;
	}

	return status == HAL_OK;
}

void AppConfig_GetSensorROM(uint8_t index, DS18B20_ROM_t *out_rom) {
	if (index >= DS18B20_MAX_SENSORS || out_rom == NULL)
		return;
	if (osMutexAcquire(configMutex, osWaitForever) == osOK) {
		memcpy(out_rom, &g_app_config.sensors[index], sizeof(DS18B20_ROM_t));
		osMutexRelease(configMutex);
	}
}

// --- Применение команды изменения привязки ---
/*
 * Mutex сериализует доступ к конфигурации. После ожидания проверка reset
 * и применение восьми байтов защищены вместе: уже начатая F105
 * не должна менять привязку после PrepareReset. Flash здесь не меняется.
 */
bool AppConfig_SetSensorROM(uint8_t index, const DS18B20_ROM_t *in_rom) {
	if (index >= DS18B20_MAX_SENSORS || in_rom == NULL || configMutex == NULL)
		return false;
	if (osMutexAcquire(configMutex, osWaitForever) != osOK)
		return false;

	const uint32_t saved_primask = __get_PRIMASK();
	__disable_irq();
	const bool applied = !AppSafety_IsResetPending();
	if (applied) {
		memcpy(&g_app_config.sensors[index], in_rom, sizeof(DS18B20_ROM_t));
	}
	__DSB();
	__set_PRIMASK(saved_primask);
	if (osMutexRelease(configMutex) != osOK) {
		Error_Handler();
		return false;
	}
	return applied;
}

uint32_t AppConfig_GetPerformerID(void) {
	uint32_t id = 0x40;
	if (osMutexAcquire(configMutex, osWaitForever) == osOK) {
		id = g_app_config.performer_id;
		osMutexRelease(configMutex);
	}
	return id;
}

/**
 * @brief Безопасная запись CAN ID платы (в RAM).
 */
void AppConfig_SetPerformerID(uint32_t id) {
	if (osMutexAcquire(configMutex, osWaitForever) == osOK) {
		g_app_config.performer_id = (uint8_t) id;
		osMutexRelease(configMutex);
	}
}

/**
 * @brief Сохранение всех изменений из RAM во Flash (Атомарная транзакция).
 */
bool AppConfig_Commit(void) {
	bool success = false;
	HAL_StatusTypeDef status = HAL_ERROR; // По умолчанию - ошибка
	uint32_t PageError = 0;
	FLASH_EraseInitTypeDef EraseInitStruct;

	// 1. ЗАЩИТА: Захватываем мьютекс.
	// Пока мы пишем во Flash, никто (даже задача мониторинга) не должен менять g_app_config.
	if (osMutexAcquire(configMutex, osWaitForever) == osOK) {

		// 2. ОБНОВЛЕНИЕ: Считаем актуальную контрольную сумму
		// текущих данных в RAM перед тем, как отправить их в память.
		g_app_config.checksum = CalculateChecksum(&g_app_config);

		// 3. РАЗБЛОКИРОВКА: В STM32 Flash-память защищена от случайной записи.
		// Чтобы её поменять, нужно вызвать специальную функцию разблокировки.
		HAL_FLASH_Unlock();

		// 4. ОЧИСТКА: Во Flash-памяти бит можно сменить с 1 на 0, но нельзя с 0 на 1.
		// Чтобы записать новые данные, нужно сначала "обнулить" (стереть) всю страницу.
		// После стирания все ячейки станут 0xFF (все биты 1).
		EraseInitStruct.TypeErase = FLASH_TYPEERASE_PAGES;
		EraseInitStruct.PageAddress = APP_CONFIG_FLASH_ADDR;
		EraseInitStruct.NbPages = 1;

		// Выполняем стирание и сохраняем статус
		status = HAL_FLASHEx_Erase(&EraseInitStruct, &PageError);

		// 5. Выполняем стирание. Если оно не удалось (status != HAL_OK) — мы не пишем данные.
		if (status == HAL_OK) {

			// 6. ЗАПИСЬ: Данные пишутся по 32-битным словам (4 байта за раз).
			// Мы берем указатель на нашу структуру и проходим её от начала до конца.
			uint32_t *pData = (uint32_t*) &g_app_config;
			uint32_t addr = APP_CONFIG_FLASH_ADDR;
			for (uint32_t i = 0; i < sizeof(AppConfig_t); i += 4) {
				status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr,
						*pData);
				if (status != HAL_OK)
					break;
				addr += 4;
				pData++;
			}
		}

		// 7. БЛОКИРОВКА: Закрываем доступ к Flash от случайных изменений.
		HAL_FLASH_Lock();

		// 8. ЗАВЕРШЕНИЕ: Сообщаем об успехе (status == HAL_OK).
		success = (status == HAL_OK);
		osMutexRelease(configMutex);
	}
	return success;
}
