/*
 * app_flash.c
 *
 *  Created on: Apr 1, 2026
 *      Author: andrey
 */

#include "app_flash.h"
#include "main.h"
#include "cmsis_os.h"  // Для osMutex
#include <string.h>
#include <stdbool.h>

// --- Инкапсулированные данные (скрыты внутри модуля) ---
static AppConfig_t g_app_config;
static osMutexId_t configMutex = NULL;

// Атрибуты мьютекса (стандарт FreeRTOS CMSIS_V2)
const osMutexAttr_t configMutex_attr = {
		"configMutex",                          // name
		osMutexRecursive | osMutexPrioInherit,  // attr_bits
		NULL,                                   // cb_mem
		0U                                      // cb_size
};

// --- Внутренние вспомогательные функции ---


/**
 * @brief Чтение 96-битного уникального идентификатора чипа (MCU UID).
 * @param out_uid Указатель на массив размером 12 байт.
 */
void AppConfig_GetMCU_UID(uint8_t* out_uid) {
	if (out_uid == NULL) return;
	// Адрес UID для STM32F103 (согласно Reference Manual)
	uint8_t* uid_base = (uint8_t*)0x1FFFF7E8;
	memcpy(out_uid, uid_base, 12);
}


/**
 * @brief Расчет контрольной суммы (Checksum).
 */
static uint16_t CalculateChecksum(AppConfig_t* cfg) {
	uint16_t checksum = 0;
	uint8_t* p = (uint8_t*)cfg;
	for (uint32_t i = 0; i < sizeof(AppConfig_t) - sizeof(cfg->checksum); i++) {
		checksum += p[i];
		}
	return checksum;
}


// --- Публичный API с защитой Mutex ---

void AppConfig_Init(void) {
	// 1. Создание мьютекса для защиты доступа
	if (configMutex == NULL) {
		configMutex = osMutexNew(&configMutex_attr);
		}

	// 2. Чтение данных из Flash
    AppConfig_t* flash_cfg = (AppConfig_t*)APP_CONFIG_FLASH_ADDR;

    // 3. Валидация
    if (flash_cfg->magic == APP_CONFIG_MAGIC &&
    		flash_cfg->checksum == CalculateChecksum(flash_cfg))
    	{
    	memcpy(&g_app_config, flash_cfg, sizeof(AppConfig_t));
    	}
    else
    	{
    	// "Заводские настройки"
    	memset(&g_app_config, 0, sizeof(AppConfig_t));
    	g_app_config.magic = APP_CONFIG_MAGIC;
    	g_app_config.performer_id = 0x40;
    	for (int i = 0; i < DS18B20_MAX_SENSORS; i++) {
    		memset(g_app_config.sensors[i].rom_code, 0xFF, 8);
    		}
    	g_app_config.checksum = CalculateChecksum(&g_app_config);
    	}
    }

void AppConfig_GetSensorROM(uint8_t index, DS18B20_ROM_t *out_rom) {
	if (index >= DS18B20_MAX_SENSORS || out_rom == NULL) return;
	if (osMutexAcquire(configMutex, osWaitForever) == osOK) {
		memcpy(out_rom, &g_app_config.sensors[index], sizeof(DS18B20_ROM_t));
		osMutexRelease(configMutex);
		}
	}

void AppConfig_SetSensorROM(uint8_t index, DS18B20_ROM_t *in_rom) {
	if (index >= DS18B20_MAX_SENSORS || in_rom == NULL) return;
	if (osMutexAcquire(configMutex, osWaitForever) == osOK) {
		memcpy(&g_app_config.sensors[index], in_rom, sizeof(DS18B20_ROM_t));
		osMutexRelease(configMutex);
		}
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
		g_app_config.performer_id = id;
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
		EraseInitStruct.TypeErase   = FLASH_TYPEERASE_PAGES;
		EraseInitStruct.PageAddress = APP_CONFIG_FLASH_ADDR;
		EraseInitStruct.NbPages     = 1;

		// Выполняем стирание и сохраняем статус
		status = HAL_FLASHEx_Erase(&EraseInitStruct, &PageError);


		// 5. Выполняем стирание. Если оно не удалось (status != HAL_OK) — мы не пишем данные.
		if (status == HAL_OK) {

			// 6. ЗАПИСЬ: Данные пишутся по 32-битным словам (4 байта за раз).
			// Мы берем указатель на нашу структуру и проходим её от начала до конца.
			uint32_t* pData = (uint32_t*)&g_app_config;
			uint32_t addr = APP_CONFIG_FLASH_ADDR;
			for (uint32_t i = 0; i < sizeof(AppConfig_t); i += 4) {
				status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, *pData);
				if (status != HAL_OK) break;
				addr += 4; pData++;
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



