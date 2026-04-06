/*
 * can_protocol.h
 *
 *  Created on: Mar 31, 2026
 *      Author: andrey
 */

#ifndef CAN_PROTOCOL_H_
#define CAN_PROTOCOL_H_

/*
* Протокол CAN-обмена между дирижером и исполнителем термодатчиков.
* Адаптация констант из can_packer.h дирижера (DDS-240 Standard)
* под STM32F103 HAL CAN (bxCAN, 29-bit Extended ID).
*/


#include <stdint.h>
#include <stdbool.h>

// ============================================================
// Приоритеты (биты 28-26 CAN ID)
// ============================================================
#define CAN_PRIORITY_HIGH       0   // Команды от дирижера
#define CAN_PRIORITY_NORMAL     1   // Ответы от исполнителей

// ============================================================
// Типы сообщений (биты 25-24 CAN ID)
// ============================================================
#define CAN_MSG_TYPE_COMMAND        0   // Команда (Conductor -> Executor)
#define CAN_MSG_TYPE_ACK            1   // Подтверждение (Executor -> Conductor)
#define CAN_MSG_TYPE_NACK           2   // Ошибка (Executor -> Conductor)
#define CAN_MSG_TYPE_DATA_DONE_LOG  3   // DATA/DONE/LOG (Executor -> Conductor)

// ============================================================
// Подтипы для MSG_TYPE_DATA_DONE_LOG (первый байт payload)
// ============================================================
#define CAN_SUB_TYPE_DONE       0x01
#define CAN_SUB_TYPE_DATA       0x02
#define CAN_SUB_TYPE_LOG        0x03

// ============================================================
// Адреса узлов (биты 23-16 dst, 15-8 src)
// ============================================================
#define CAN_ADDR_BROADCAST      0x00
#define CAN_ADDR_HOST           0x01
#define CAN_ADDR_CONDUCTOR      0x10
#define CAN_ADDR_THERMO_BOARD   0x40  // Наш адрес (Плата термодатчиков)

// ============================================================
// Коды команд сенсоров (байты 0-1 payload, Little-Endian)
// ============================================================
#define CAN_CMD_SENSOR_GET_TEMP      0x9011 // Запрос конкретного датчика
#define CAN_CMD_SENSOR_GET_ALL_TEMPS  0x9010 // Запрос всех датчиков

// ============================================================
// Макрос построения 29-bit Extended CAN ID
// ============================================================
#define CAN_BUILD_ID(priority, msg_type, dst_addr, src_addr) \
     ((uint32_t)(((priority) & 0x07) << 26) | \
                 (((msg_type) & 0x03) << 24) | \
                 (((dst_addr) & 0xFF) << 16) | \
                 (((src_addr) & 0xFF) << 8))

// ============================================================
// Макросы извлечения полей из 29-bit CAN ID
// ============================================================
#define CAN_GET_PRIORITY(id)    ((uint8_t)(((id) >> 26) & 0x07))
#define CAN_GET_MSG_TYPE(id)    ((uint8_t)(((id) >> 24) & 0x03))
#define CAN_GET_DST_ADDR(id)    ((uint8_t)(((id) >> 16) & 0xFF))
#define CAN_GET_SRC_ADDR(id)    ((uint8_t)(((id) >> 8)  & 0xFF))

// ============================================================
// Коды ошибок для NACK-ответов (DDS-240 Standard)
// ============================================================
#define CAN_ERR_NONE                0x0000
#define CAN_ERR_UNKNOWN_CMD         0x0001
#define CAN_ERR_INVALID_SENSOR_ID   0x0002
#define CAN_ERR_SENSOR_FAILURE      0x0003
#define CAN_ERR_INVALID_KEY         0x0004 // Ошибка защитного ключа (Magic Key)
#define CAN_ERR_FLASH_WRITE         0x0005 // Ошибка записи во внутреннюю память
#define CAN_ERR_INVALID_PARAM       0x0006 // Некорректный параметр команды
#define CAN_ERR_BUSY                0x0007 // Устройство занято выполнением другой задачи

// ============================================================
// Внутренние структуры для передачи в Dispatcher
// ============================================================

typedef struct {
     uint16_t cmd_code;   // Код команды (Little-Endian)
     uint8_t  sensor_id;  // ID сенсора (параметр)
     uint8_t  data[8];    // Дополнительные данные (если есть)
     uint8_t  data_len;
} ParsedCanCommand_t;

// ============================================================
// Прототипы функций отправки ответов (будут реализованы в task_can_handler)
// ============================================================
void CAN_SendAck(uint16_t cmd_code);
void CAN_SendNack(uint16_t cmd_code, uint16_t error_code);
void CAN_SendDone(uint16_t cmd_code, uint8_t sensor_id);
void CAN_SendData(uint16_t cmd_code, uint8_t *data, uint8_t len);


// ============================================================
// Типы устройств (Device Type IDs)
// ============================================================
#define CAN_DEVICE_TYPE_THERMO      0x40
#define CAN_DEVICE_TYPE_MOTION      0x20
#define CAN_DEVICE_TYPE_PUMP        0x30

// ============================================================
// Универсальные сервисные команды (0xF0xx)
// ============================================================
#define CAN_CMD_SRV_GET_DEVICE_INFO  0xF001
#define CAN_CMD_SRV_REBOOT           0xF002
#define CAN_CMD_SRV_FLASH_COMMIT     0xF003
#define CAN_CMD_SRV_FACTORY_RESET    0xF006 // Синхронизировано с экосистемой
#define CAN_CMD_SRV_SET_NODE_ID      0xF005

// ============================================================
// Сервисные команды термодатчиков (0xF1xx)
// ============================================================
#define CAN_CMD_SRV_SCAN_1WIRE       0xF101
#define CAN_CMD_SRV_GET_PHYS_ID      0xF102
#define CAN_CMD_SRV_SET_CHANNEL_MAP  0xF103
#define CAN_CMD_SRV_GET_CHANNEL_MAP  0xF104

// ============================================================
// Защитные ключи (Magic Keys)
// ============================================================
#define SRV_MAGIC_REBOOT             0xDEAD
#define SRV_MAGIC_FACTORY_RESET      0xFACE

// ============================================================
// Версия прошивки (Firmware Version)
// ============================================================
#define FW_REV_MAJOR                 1
#define FW_REV_MINOR                 0


#endif /* CAN_PROTOCOL_H_ */
