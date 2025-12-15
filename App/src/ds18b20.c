/*
 * ds18b20.c
 *
 *  Created on: Dec 15, 2025
 *      Author: andrey
 */


#include "ds18b20.h"
#include "main.h" // Для HAL_GPIO_WritePin, HAL_GPIO_ReadPin, и макросов пинов
#include "app_config.h" // Для DS18B20_MAX_SENSORS


// --- Вспомогательные функции и команды для DS18B20 ---

#define DS18B20_CMD_SEARCHROM     0xF0
#define DS18B20_CMD_READROM       0x33
#define DS18B20_CMD_MATCHROM      0x55
#define DS18B20_CMD_SKIPROM       0xCC
#define DS18B20_CMD_CONVERTTEMP   0x44
#define DS18B20_CMD_READSCRATCH   0xBE


// --- Внешние переменные HAL ---
// Объявлены в main.c, здесь мы сообщаем компилятору, что будем их использовать.
extern TIM_HandleTypeDef htim3; // Хэндл таймера, который мы настроили в CubeMX для задержек.

// --- Глобальные переменные драйвера ---
static DS18B20_ROM_t ds18b20_rom_codes[DS18B20_MAX_SENSORS]; // Массив для хранения найденных ROM-кодов
static uint8_t ds18b20_sensor_count = 0;                     // Количество найденных датчиков

// --- Макросы для управления пином 1-Wire ---
// Убедитесь, что вы использовали 'ONE_WIRE_BUS' как User Label в CubeMX для вашего пина.
//#define ONE_WIRE_PORT       ONE_WIRE_BUS_GPIO_Port
//#define ONE_WIRE_PIN        ONE_WIRE_BUS_Pin

// Установка пина в состояние LOW
#define ONE_WIRE_LOW()      HAL_GPIO_WritePin(ONE_WIRE_BUS_GPIO_Port, ONE_WIRE_BUS_Pin, GPIO_PIN_RESET)

#define ONE_WIRE_HIGH()     HAL_GPIO_WritePin(ONE_WIRE_BUS_GPIO_Port, ONE_WIRE_BUS_Pin, GPIO_PIN_SET)

// Конфигурация пина как OUTPUT (Push-Pull для затягивания в HIGH, Open-Drain для LOW)
// Поскольку у нас Open-Drain, то для HIGH мы просто отпускаем пин, а подтягивающий резистор делает его HIGH.
// Для LOW мы активно затягиваем пин к земле.
// Open-Drain mode уже должен быть настроен в CubeMX для ONE_WIRE_BUS пина.
// Для установки HIGH мы просто отпускаем пин, а для LOW - активно тянем его к земле.

// Меняет направление пина на INPUT
void ONE_WIRE_INPUT()
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};
	GPIO_InitStruct.Pin = ONE_WIRE_BUS_Pin;
	GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
	GPIO_InitStruct.Pull = GPIO_PULLUP; // Для 1-Wire всегда нужен подтягивающий резистор
	HAL_GPIO_Init(ONE_WIRE_BUS_GPIO_Port, &GPIO_InitStruct);
	}

// Меняет направление пина на OUTPUT (Open-Drain, как настроено в CubeMX)
void ONE_WIRE_OUTPUT()
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = ONE_WIRE_BUS_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD; // Open Drain mode
    GPIO_InitStruct.Pull = GPIO_NOPULL;       // Подтяжка уже внешняя, или настроена как PULLUP в INPUT mode
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH; // Высокая скорость для 1-Wire
    HAL_GPIO_Init(ONE_WIRE_BUS_GPIO_Port, &GPIO_InitStruct);
    }



// --- Микросекундная задержка ---
// Используем TIM3 (или любой другой таймер, настроенный в CubeMX)
void delay_us(uint32_t us)
{
	__HAL_TIM_SET_COUNTER(&htim3, 0); // Обнуляем счетчик
	while(__HAL_TIM_GET_COUNTER(&htim3) < us); // Ждем нужного количества микросекунд
	}


// --- Низкоуровневые функции протокола 1-Wire ---

/**
  * @brief Выполняет сброс шины 1-Wire и получает ответ о присутствии.
  * @return true если датчик обнаружен, false если нет.
  */
bool OneWire_Reset()
{
	bool presence = false;
	ONE_WIRE_OUTPUT();   // Пин как выход
    ONE_WIRE_LOW();      // Тянем шину в LOW
    delay_us(480);       // Задержка 480 мкс (импульс сброса)
    ONE_WIRE_INPUT();    // Пин как вход (отпускаем шину)
    delay_us(70);        // Ждем 70 мкс (импульс присутствия от датчика)

    if (HAL_GPIO_ReadPin(ONE_WIRE_BUS_GPIO_Port, ONE_WIRE_BUS_Pin) == GPIO_PIN_RESET) {
    	presence = true; // Датчик ответил
    	}
    delay_us(410);       // Оставшаяся часть временного слота (recovery time)
    return presence;
    }

/**
 * @brief Записывает один бит в шину 1-Wire.
 * @param bit Бит для записи (0 или 1).
 */
void OneWire_WriteBit(bool bit)
{
	ONE_WIRE_OUTPUT();
	if (bit) {
		// Запись '1': Тянем в LOW, коротко, затем отпускаем.
		ONE_WIRE_LOW();
		delay_us(6);
		ONE_WIRE_INPUT(); // Отпускаем шину (подтягивается HIGH)
        delay_us(64);     // Оставшееся время слота
        }
	else {
		// Запись '0': Тянем в LOW, на весь слот.
		ONE_WIRE_LOW();
		delay_us(60);
		ONE_WIRE_INPUT(); // Отпускаем шину
            delay_us(10);     // Оставшееся время слота
            }
	}

/**
 * @brief Читает один бит из шины 1-Wire.
 * @return Прочитанный бит (true для '1', false для '0').
 */

bool OneWire_ReadBit()
{
	bool bit = false;
	ONE_WIRE_OUTPUT();
    ONE_WIRE_LOW();   // Тянем в LOW
    delay_us(6);      // Задержка 6 мкс
    ONE_WIRE_INPUT(); // Отпускаем шину
    delay_us(9);      // Ждем, пока датчик выставит бит

    if (HAL_GPIO_ReadPin(ONE_WIRE_BUS_GPIO_Port, ONE_WIRE_BUS_Pin) == GPIO_PIN_SET) {
    	bit = true;   // Прочитали '1'
    	}
    delay_us(55);     // Оставшаяся часть временного слота
    return bit;
    }

/**
 * @brief Записывает один байт в шину 1-Wire.
 * @param byte Байт для записи.
 */

void OneWire_WriteByte(uint8_t byte)
{
	for (uint8_t i = 0; i < 8; i++) {
		OneWire_WriteBit((byte >> i) & 0x01); // Записываем бит за битом, начиная с младшего
		}
	}

/**
  * @brief Читает один байт из шины 1-Wire.
  * @return Прочитанный байт.
  */
uint8_t OneWire_ReadByte()
{
	uint8_t byte = 0;
	for (uint8_t i = 0; i < 8; i++) {
		if (OneWire_ReadBit()) {
			byte |= (0x01 << i); // Читаем бит за битом, собираем байт
			}
		}
	return byte;
	}

/**
 * @brief Расчет CRC8 для проверки целостности данных от DS18B20
 */

uint8_t OneWire_CRC8(uint8_t* data, uint8_t len)
{
	uint8_t crc = 0;
	while (len--) {
		uint8_t inbyte = *data++;
		for (uint8_t i = 8; i; i--) {
			uint8_t mix = (crc ^ inbyte) & 0x01;
			crc >>= 1;
			if (mix) {
				crc ^= 0x8C;
				}
			inbyte >>= 1;
			}
		}
	return crc;
	}

// --- Реализация публичных функций драйвера ---

uint8_t DS18B20_Init()
{
	// Эта функция должна реализовывать алгоритм поиска ROM-кодов.
    // Для нашего первоначального теста мы можем ее упростить,
    // предполагая, что мы будем работать только с одним датчиком в режиме SKIP_ROM.
    // Полноценная реализация поиска ROM - более сложная задача.
    // Пока что просто проверим, что на шине есть хоть кто-то.
    if (OneWire_Reset()) {
    	ds18b20_sensor_count = 1; // Заглушка: говорим, что нашли 1 датчик
    	}
    else {
    	ds18b20_sensor_count = 0;
    	}
    return ds18b20_sensor_count;
    }
void DS18B20_StartAll()
{
	if (!OneWire_Reset()) {
		return; // Ошибка, нет датчиков
		}
	OneWire_WriteByte(DS18B20_CMD_SKIPROM); // Команда для всех устройств
	OneWire_WriteByte(DS18B20_CMD_CONVERTTEMP); // Начать измерение
	}
bool DS18B20_ReadTemperature(DS18B20_ROM_t* rom, float* out_temp)
{
	uint8_t scratchpad[9];
	if (!OneWire_Reset()) {
		return false; // Ошибка, нет датчиков
		}
	// Вместо сложного поиска ROM, для теста мы будем использовать команду SKIP_ROM.
	// Это сработает, если на шине только один датчик.
	// В будущем, когда у нас будет массив ROM-кодов, здесь будет OneWire_WriteByte(DS18B20_CMD_MATCHROM)
	// и отправка 8 байт rom->rom_code.
	OneWire_WriteByte(DS18B20_CMD_SKIPROM);
	OneWire_WriteByte(DS18B20_CMD_READSCRATCH); // Команда "прочитать блокнот"
	// Читаем 9 байт из "блокнота" датчика
	for (uint8_t i = 0; i < 9; i++) {
		scratchpad[i] = OneWire_ReadByte();
		}
	// Проверяем CRC
	if (OneWire_CRC8(scratchpad, 8) != scratchpad[8]) {
		return false; // Ошибка контрольной суммы
		}

	// Конвертируем сырые данные в температуру
	int16_t raw_temp = (int16_t)(scratchpad[1] << 8) | scratchpad[0];
	*out_temp = (float)raw_temp / 16.0f;
	return true;
	}
DS18B20_ROM_t* DS18B20_GetROM(uint8_t sensor_index)
{
	if (sensor_index < ds18b20_sensor_count) {
		return &ds18b20_rom_codes[sensor_index];
		}
	return NULL;
	}




