/*
 * dds240_global_config.h
 *
 * Canonical DDS-240 ecosystem constants.
 *
 * This file is the common reference for the Conductor and all STM32 Executor
 * boards. Board-local protocol headers may keep domain aliases, but new shared
 * values must be synchronized from this file first.
 *
 * Consolidated from:
 * - STM32F103_step_motors_refactored/App/inc/can_protocol.h
 * - STM32F103_pumps_valves/App/inc/can_protocol.h
 * - STM32F103_temp_sensors/App/inc/can_protocol.h
 */

#ifndef DDS240_GLOBAL_CONFIG_H_
#define DDS240_GLOBAL_CONFIG_H_

#include <stdint.h>

/* -------------------------------------------------------------------------- */
/* Ecosystem version                                                          */
/* -------------------------------------------------------------------------- */

#define DDS240_ECOSYSTEM_STANDARD_MAJOR             1U
#define DDS240_ECOSYSTEM_STANDARD_MINOR             4U

/* -------------------------------------------------------------------------- */
/* CAN executor transport profile                                             */
/* -------------------------------------------------------------------------- */

#define DDS240_CAN_BITRATE                          1000000UL
#define DDS240_CAN_CLASSIC_MTU                      8U
#define DDS240_CAN_EXECUTOR_DLC                     8U
#define DDS240_CAN_EXTENDED_ID_REQUIRED             1U

/*
 * Host -> Conductor may keep command-specific dynamic DLC.
 * Conductor <-> Executor is strict DLC=8 with zero padding.
 */
#define DDS240_HOST_TO_CONDUCTOR_DYNAMIC_DLC_ALLOWED 1U
#define DDS240_CONDUCTOR_TO_EXECUTOR_STRICT_DLC      1U

/*
 * STM32 bxCAN executors must enable TransmitFifoPriority to preserve
 * ACK -> DATA... -> DONE ordering for multi-frame responses.
 */
#define DDS240_BXCAN_MODE_NORMAL_REQUIRED           1U
#define DDS240_BXCAN_TIME_TRIGGERED_MODE_ENABLE     0U
#define DDS240_BXCAN_AUTO_BUS_OFF_ENABLE            0U
#define DDS240_BXCAN_AUTO_WAKE_UP_ENABLE            0U
#define DDS240_BXCAN_AUTO_RETRANSMISSION_ENABLE     0U
#define DDS240_BXCAN_RECEIVE_FIFO_LOCKED_ENABLE     0U
#define DDS240_BXCAN_TRANSMIT_FIFO_PRIORITY_ENABLE  1U

#define DDS240_TX_MAILBOX_WAIT_TIMEOUT_MS           10U
#define DDS240_CONDUCTOR_ACK_TIMEOUT_MS             50U

/*
 * Fast DONE timeout is only for commands whose contract is expected to finish
 * immediately after acceptance, for example short service/state transitions.
 * It must not be used for finite actuator commands such as Fluidics
 * PUMP_RUN_DURATION or Motion ROTATE. For finite commands the Conductor must
 * compute operation timeout from the commanded physical duration plus margin.
 */
#define DDS240_CONDUCTOR_FAST_DONE_TIMEOUT_MS       100U

/* STM32F103 CAN reference profile used by the verified Fluidics board. */
#define DDS240_STM32F103_CAN_APB1_HZ                32000000UL
#define DDS240_STM32F103_CAN_PRESCALER              2U
#define DDS240_STM32F103_CAN_BS1_TQ                 11U
#define DDS240_STM32F103_CAN_BS2_TQ                 4U
#define DDS240_STM32F103_CAN_SJW_TQ                 1U
#define DDS240_STM32F103_CAN_SAMPLE_POINT_PERMILLE  750U

/* -------------------------------------------------------------------------- */
/* 29-bit Extended CAN ID layout                                              */
/* -------------------------------------------------------------------------- */

#define DDS240_CAN_PRIORITY_HIGH                    0U
#define DDS240_CAN_PRIORITY_NORMAL                  1U

#define DDS240_CAN_MSG_TYPE_COMMAND                 0U
#define DDS240_CAN_MSG_TYPE_ACK                     1U
#define DDS240_CAN_MSG_TYPE_NACK                    2U
#define DDS240_CAN_MSG_TYPE_DATA_DONE_LOG           3U

#define DDS240_CAN_SUB_TYPE_DONE                    0x01U
#define DDS240_CAN_SUB_TYPE_DATA                    0x02U
#define DDS240_CAN_SUB_TYPE_LOG                     0x03U

#define DDS240_CAN_ADDR_BROADCAST                   0x00U
#define DDS240_CAN_ADDR_HOST                        0x01U
#define DDS240_CAN_ADDR_CONDUCTOR                   0x10U
#define DDS240_CAN_ADDR_MOTION_DEFAULT              0x20U
#define DDS240_CAN_ADDR_FLUIDICS_DEFAULT            0x30U
#define DDS240_CAN_ADDR_THERMO_DEFAULT              0x40U

/* Board-local naming aliases used by existing executors. */
#define DDS240_CAN_ADDR_MOTOR_BOARD                 DDS240_CAN_ADDR_MOTION_DEFAULT
#define DDS240_CAN_ADDR_PUMP_BOARD                  DDS240_CAN_ADDR_FLUIDICS_DEFAULT
#define DDS240_CAN_ADDR_THERMO_BOARD                DDS240_CAN_ADDR_THERMO_DEFAULT

#define DDS240_CAN_DYNAMIC_NODE_ID_MIN              0x02U
#define DDS240_CAN_DYNAMIC_NODE_ID_MAX              0x7FU

#define DDS240_CAN_BUILD_ID(priority, msg_type, dst_addr, src_addr) \
	((uint32_t)((((uint32_t)(priority) & 0x07UL) << 26) | \
	            (((uint32_t)(msg_type) & 0x03UL) << 24) | \
	            (((uint32_t)(dst_addr) & 0xFFUL) << 16) | \
	            (((uint32_t)(src_addr) & 0xFFUL) << 8)))

#define DDS240_CAN_GET_PRIORITY(id)                 ((uint8_t)(((id) >> 26) & 0x07U))
#define DDS240_CAN_GET_MSG_TYPE(id)                 ((uint8_t)(((id) >> 24) & 0x03U))
#define DDS240_CAN_GET_DST_ADDR(id)                 ((uint8_t)(((id) >> 16) & 0xFFU))
#define DDS240_CAN_GET_SRC_ADDR(id)                 ((uint8_t)(((id) >> 8)  & 0xFFU))

/* -------------------------------------------------------------------------- */
/* Device type IDs returned by GET_DEVICE_INFO                                */
/* -------------------------------------------------------------------------- */

#define DDS240_DEVICE_TYPE_MOTION                   0x01U
#define DDS240_DEVICE_TYPE_THERMO                   0x02U
#define DDS240_DEVICE_TYPE_FLUIDICS                 0x03U

#define DDS240_MOTION_CHANNELS_DEFAULT              8U
#define DDS240_THERMO_CHANNELS_DEFAULT              8U
#define DDS240_FLUIDICS_PUMPS_DEFAULT               13U
#define DDS240_FLUIDICS_VALVES_DEFAULT              3U
#define DDS240_FLUIDICS_CHANNELS_DEFAULT            16U

/* -------------------------------------------------------------------------- */
/* Common service commands                                                    */
/* -------------------------------------------------------------------------- */

#define DDS240_CMD_SRV_GET_DEVICE_INFO              0xF001U
#define DDS240_CMD_SRV_REBOOT                       0xF002U
#define DDS240_CMD_SRV_FLASH_COMMIT                 0xF003U
#define DDS240_CMD_SRV_GET_UID                      0xF004U
#define DDS240_CMD_SRV_SET_NODE_ID                  0xF005U
#define DDS240_CMD_SRV_FACTORY_RESET                0xF006U
#define DDS240_CMD_SRV_GET_STATUS                   0xF007U

#define DDS240_SRV_MAGIC_REBOOT                     0x55AAU
#define DDS240_SRV_MAGIC_FACTORY_RESET              0xDEADU

/* -------------------------------------------------------------------------- */
/* Common GET_STATUS metric IDs                                               */
/* -------------------------------------------------------------------------- */

#define DDS240_STATUS_RX_TOTAL                      0x0001U
#define DDS240_STATUS_TX_TOTAL                      0x0002U
#define DDS240_STATUS_RX_QUEUE_OVERFLOW             0x0003U
#define DDS240_STATUS_TX_QUEUE_OVERFLOW             0x0004U
#define DDS240_STATUS_DISPATCHER_OVERFLOW           0x0005U
#define DDS240_STATUS_DROP_NOT_EXT                  0x0006U
#define DDS240_STATUS_DROP_WRONG_DST                0x0007U
#define DDS240_STATUS_DROP_WRONG_TYPE               0x0008U
#define DDS240_STATUS_DROP_WRONG_DLC                0x0009U
#define DDS240_STATUS_TX_MAILBOX_TIMEOUT            0x000AU
#define DDS240_STATUS_TX_HAL_ERROR                  0x000BU
#define DDS240_STATUS_ERROR_CALLBACK                0x000CU
#define DDS240_STATUS_ERROR_WARNING                 0x000DU
#define DDS240_STATUS_ERROR_PASSIVE                 0x000EU
#define DDS240_STATUS_BUS_OFF                       0x000FU
#define DDS240_STATUS_LAST_HAL_ERROR                0x0010U
#define DDS240_STATUS_LAST_ESR                      0x0011U
#define DDS240_STATUS_APP_QUEUE_OVERFLOW            0x0012U

#define DDS240_STATUS_DOMAIN_METRIC_BASE            0x1000U

/* -------------------------------------------------------------------------- */
/* NACK error registry                                                        */
/* -------------------------------------------------------------------------- */

/*
 * NACK error payload is interpreted in transaction context:
 * source NodeID + command code + error code.
 *
 * The aliases below intentionally preserve the names used by the existing
 * executors. Equal numeric values may have different domain names when the
 * source executor/command defines that meaning.
 */

#define DDS240_ERR_NONE                             0x0000U
#define DDS240_ERR_UNKNOWN_CMD                      0x0001U
#define DDS240_ERR_INVALID_DEVICE_ID                0x0002U
#define DDS240_ERR_DEVICE_BUSY                      0x0003U
#define DDS240_ERR_INVALID_KEY                      0x0004U
#define DDS240_ERR_FLASH_WRITE                      0x0005U
#define DDS240_ERR_INVALID_PARAM                    0x0006U

/* Motion Executor NACK aliases. */
#define DDS240_ERR_MOTION_INVALID_MOTOR_ID          0x0002U
#define DDS240_ERR_MOTION_MOTOR_BUSY                0x0003U

/* Fluidics Executor NACK aliases. */
#define DDS240_ERR_FLUIDICS_INVALID_DEVICE_ID       0x0002U
#define DDS240_ERR_FLUIDICS_DEVICE_BUSY             0x0003U

/* Thermo Executor NACK aliases. */
#define DDS240_ERR_THERMO_INVALID_SENSOR_ID         0x0002U
#define DDS240_ERR_THERMO_SENSOR_FAILURE            0x0003U
#define DDS240_ERR_THERMO_BUSY                      0x0007U

/* -------------------------------------------------------------------------- */
/* Motion domain command IDs and defaults                                     */
/* -------------------------------------------------------------------------- */

#define DDS240_CMD_MOTION_MOTOR_ROTATE              0x0101U
#define DDS240_CMD_MOTION_MOTOR_HOME                0x0102U
#define DDS240_CMD_MOTION_MOTOR_START_CONTINUOUS    0x0103U
#define DDS240_CMD_MOTION_MOTOR_STOP                0x0104U

#define DDS240_MOTION_MOTORS_DEFAULT                8U

/* -------------------------------------------------------------------------- */
/* Fluidics domain command IDs and defaults                                   */
/* -------------------------------------------------------------------------- */

#define DDS240_CMD_FLUIDICS_PUMP_RUN_DURATION       0x0201U
#define DDS240_CMD_FLUIDICS_PUMP_START              0x0202U
#define DDS240_CMD_FLUIDICS_PUMP_STOP               0x0203U
#define DDS240_CMD_FLUIDICS_VALVE_OPEN              0x0204U
#define DDS240_CMD_FLUIDICS_VALVE_CLOSE             0x0205U

#define DDS240_FLUIDICS_DEFAULT_PUMP_TIMEOUT_MS     60000UL
#define DDS240_FLUIDICS_DEFAULT_VALVE_TIMEOUT_MS    300000UL

/*
 * Fluidics timing parameter is uint32 little-endian in payload bytes 3..6.
 * For PUMP_RUN_DURATION it is duration_ms and 0 is invalid. DONE is sent only
 * after the pump has been switched off at the end of duration_ms.
 * For PUMP_START/VALVE_OPEN it is a safety timeout; 0 means domain default.
 * Conductor recipe dosing must use one PUMP_RUN_DURATION step rather than
 * PUMP_START -> WAIT_MS -> PUMP_STOP. During a finite pump run, the Conductor
 * owns a per-channel resource lock; a repeated command for the same channel is
 * expected to return DEVICE_BUSY and is not a successful recipe step.
 */

/* -------------------------------------------------------------------------- */
/* Thermo domain command IDs and defaults                                     */
/* -------------------------------------------------------------------------- */

#define DDS240_CMD_SENSOR_GET_ALL_TEMPS             0x9010U
#define DDS240_CMD_SENSOR_GET_TEMP                  0x9011U

#define DDS240_CMD_THERMO_SRV_SCAN_1WIRE            0xF101U
#define DDS240_CMD_THERMO_SRV_GET_PHYS_ID           0xF102U
#define DDS240_CMD_THERMO_SRV_SET_CHANNEL_MAP_P1    0xF103U
#define DDS240_CMD_THERMO_SRV_GET_CHANNEL_MAP       0xF104U
#define DDS240_CMD_THERMO_SRV_SET_CHANNEL_MAP_P2    0xF105U

/*
 * Thermo SET_CHANNEL_MAP uses two strict-DLC executor frames:
 * F103 carries ROM bytes 0..3, F105 carries ROM bytes 4..7 and finalizes the
 * mapping update in RAM. FLASH_COMMIT persists the map.
 */

/* -------------------------------------------------------------------------- */
/* Watchdog / safe-state reference profile                                    */
/* -------------------------------------------------------------------------- */

#define DDS240_IWDG_REQUIRED_FOR_STM32_EXECUTORS    1U
#define DDS240_IWDG_REFRESH_BY_SUPERVISOR_ONLY      1U
#define DDS240_WATCHDOG_TASK_IDLE_TIMEOUT_MS        500U
#define DDS240_WATCHDOG_SUPERVISOR_PERIOD_MS        1000U

/* Verified STM32F103 Fluidics IWDG profile. Actual timeout depends on LSI. */
#define DDS240_STM32F103_IWDG_PRESCALER             256U
#define DDS240_STM32F103_IWDG_RELOAD                624U

/*
 * If ACK was sent but DONE is absent before operation timeout, the Conductor
 * must not treat the command as successful. Required action: mark operation as
 * failed/unknown, perform recovery/discovery, verify NodeID/device_type/UID,
 * then resume the recipe only by explicit recovery policy.
 */
#define DDS240_CONDUCTOR_ACK_WITHOUT_DONE_IS_FAULT  1U

/* -------------------------------------------------------------------------- */
/* RTOS resource reference profile                                            */
/* -------------------------------------------------------------------------- */

#define DDS240_RTX_OBJECT_CREATION_MUST_BE_CHECKED  1U

/*
 * Historical Fluidics STM32F103 heap reference was 8192 bytes after the 4096-byte
 * resource fault. The current 2026-05-05 Fluidics baseline with IWDG/watchdog
 * task and finite PUMP_RUN_DURATION uses 10240 bytes.
 */
#define DDS240_FLUIDICS_F103_MIN_HEAP_SIZE_BYTES    10240U

#endif /* DDS240_GLOBAL_CONFIG_H_ */
