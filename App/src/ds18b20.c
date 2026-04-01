/*
 * ds18b20.c
 *
 *  Created on: Dec 15, 2025
 *      Author: andrey
 */


#include "ds18b20.h"
#include "main.h"
#include "app_config.h"


// --- Команды DS18B20 ---
#define DS18B20_CMD_SEARCHROM     0xF0
#define DS18B20_CMD_MATCHROM      0x55
#define DS18B20_CMD_SKIPROM       0xCC
#define DS18B20_CMD_CONVERTTEMP   0x44
#define DS18B20_CMD_READSCRATCH   0xBE

extern TIM_HandleTypeDef htim3;

// --- Глобальные переменные драйвера ---
static DS18B20_ROM_t ds18b20_rom_codes[DS18B20_MAX_SENSORS];
static uint8_t ds18b20_sensor_count = 0;

// Переменные для алгоритма Search ROM
static uint8_t ROM_NO[8];
static uint8_t LastDiscrepancy;
static uint8_t LastDeviceFlag;

// --- Макросы прямого доступа к регистрам (Direct Register Access) ---
// Для STM32F103: BRR - сброс в 0 (LOW), BSRR - установка в 1 (Hi-Z в Open-Drain)
#define ONE_WIRE_LOW()      (ONE_WIRE_BUS_GPIO_Port->BRR = ONE_WIRE_BUS_Pin)
#define ONE_WIRE_HIGH()     (ONE_WIRE_BUS_GPIO_Port->BSRR = ONE_WIRE_BUS_Pin)
#define ONE_WIRE_READ()     ((ONE_WIRE_BUS_GPIO_Port->IDR & ONE_WIRE_BUS_Pin) != 0)

// Микросекундная задержка (TIM3 настроен на 1 МГц)
void delay_us(uint32_t us) {
	__HAL_TIM_SET_COUNTER(&htim3, 0);
	while(__HAL_TIM_GET_COUNTER(&htim3) < us);
	}

// --- Низкоуровневый 1-Wire уровень (Advanced Timing) ---

bool OneWire_Reset() {
	bool presence = false;
	ONE_WIRE_LOW();
	delay_us(480);
    ONE_WIRE_HIGH(); // Отпускаем шину
    delay_us(70);    // Ждем импульс присутствия
    if (!ONE_WIRE_READ()) presence = true;
    delay_us(410);   // Время восстановления
    return presence;
}

void OneWire_WriteBit(bool bit) {
	ONE_WIRE_LOW();
	if (bit) {
		delay_us(6);
		ONE_WIRE_HIGH();
		delay_us(64);
		}
	else {
		delay_us(60);
		ONE_WIRE_HIGH();
		delay_us(10);
		}
}

bool OneWire_ReadBit() {
	bool bit = false;
	ONE_WIRE_LOW();
	delay_us(6);
	ONE_WIRE_HIGH();
	delay_us(9); // Точка стробирования (Sample time)
	if (ONE_WIRE_READ()) bit = true;
	delay_us(55);
	return bit;
}

void OneWire_WriteByte(uint8_t byte) {
	for (uint8_t i = 0; i < 8; i++) OneWire_WriteBit((byte >> i) & 0x01);
}

uint8_t OneWire_ReadByte() {
	uint8_t byte = 0;
	for (uint8_t i = 0; i < 8; i++) if (OneWire_ReadBit()) byte |= (0x01 << i);
	return byte;
}

uint8_t OneWire_CRC8(uint8_t* data, uint8_t len) {
	uint8_t crc = 0;
	while (len--) {
		uint8_t inbyte = *data++;
		for (uint8_t i = 8; i; i--) {
			uint8_t mix = (crc ^ inbyte) & 0x01;
			crc >>= 1;
			if (mix) crc ^= 0x8C;
			inbyte >>= 1;
			}
		}
	return crc;
}

// --- Алгоритм Search ROM (Maxim Integrated) ---

bool OneWire_Search(uint8_t *newAddr) {
	uint8_t id_bit_number = 1, last_zero = 0, rom_byte_number = 0, rom_byte_mask = 1;
	bool id_bit, cmp_id_bit, search_direction, search_result = false;

	if (!LastDeviceFlag) {
		if (!OneWire_Reset()) {
			LastDiscrepancy = 0; LastDeviceFlag = false; return false;
			}
		OneWire_WriteByte(DS18B20_CMD_SEARCHROM);

		while (rom_byte_number < 8) {
			id_bit = OneWire_ReadBit();
			cmp_id_bit = OneWire_ReadBit();
			if (id_bit && cmp_id_bit) break; // Ошибка: никто не ответил
			else {
				if (id_bit != cmp_id_bit) search_direction = id_bit;
				else {
					if (id_bit_number < LastDiscrepancy)
						search_direction = ((ROM_NO[rom_byte_number] & rom_byte_mask) > 0);
					else
						search_direction = (id_bit_number == LastDiscrepancy);
					if (search_direction == 0) last_zero = id_bit_number;
					}
				if (search_direction == 1) ROM_NO[rom_byte_number] |= rom_byte_mask;
				else ROM_NO[rom_byte_number] &= ~rom_byte_mask;
				OneWire_WriteBit(search_direction);
				id_bit_number++; rom_byte_mask <<= 1;
				if (rom_byte_mask == 0) { rom_byte_number++; rom_byte_mask = 1; }
				}
			}
		if (!(id_bit_number < 65)) {
			LastDiscrepancy = last_zero;
			if (LastDiscrepancy == 0) LastDeviceFlag = true;
			search_result = true;
			}
		}
	if (!search_result || !ROM_NO[0]) {
		LastDiscrepancy = 0; LastDeviceFlag = false; search_result = false;
		}
	else {
		for (int i = 0; i < 8; i++) newAddr[i] = ROM_NO[i];
		}
	return search_result;
	}

// --- Публичные функции драйвера ---
uint8_t DS18B20_Init() {
	ds18b20_sensor_count = 0;
	LastDiscrepancy = 0;
	LastDeviceFlag = false;
	while (OneWire_Search(ds18b20_rom_codes[ds18b20_sensor_count].rom_code)) {
		if (OneWire_CRC8(ds18b20_rom_codes[ds18b20_sensor_count].rom_code, 7) == ds18b20_rom_codes[ds18b20_sensor_count].rom_code[7]) {
			ds18b20_sensor_count++;
			}
		if (ds18b20_sensor_count >= DS18B20_MAX_SENSORS) break;
		}
	return ds18b20_sensor_count;
	}

void DS18B20_StartAll() {
	if (OneWire_Reset()) {
		OneWire_WriteByte(DS18B20_CMD_SKIPROM);
		OneWire_WriteByte(DS18B20_CMD_CONVERTTEMP);
		}
	}

bool DS18B20_ReadTemperature(DS18B20_ROM_t* rom, float* out_temp) {
	uint8_t scratchpad[9];
	if (!OneWire_Reset()) return false;
	OneWire_WriteByte(DS18B20_CMD_MATCHROM);
	for (uint8_t i = 0; i < 8; i++) OneWire_WriteByte(rom->rom_code[i]);
	OneWire_WriteByte(DS18B20_CMD_READSCRATCH);
	for (uint8_t i = 0; i < 9; i++) scratchpad[i] = OneWire_ReadByte();
	if (OneWire_CRC8(scratchpad, 8) != scratchpad[8]) return false;
	int16_t raw_temp = (int16_t)(scratchpad[1] << 8) | scratchpad[0];
	*out_temp = (float)raw_temp * 0.0625f; // Разрешение 12 бит (1/16)
	return true;
	}

DS18B20_ROM_t* DS18B20_GetROM(uint8_t sensor_index) {
	if (sensor_index < ds18b20_sensor_count) return &ds18b20_rom_codes[sensor_index];
	return NULL;
	}





