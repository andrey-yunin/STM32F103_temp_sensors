# STM32F103 Temperature Sensors Executor (DDS-240 Standard)

## Accepted Thermo firmware — 2026-10-08

T01–T15 unification and the agreed firmware acceptance tests are complete.
Accepted image: ordinary Debug, APP_WATCHDOG_TEST_HOOKS=0; ELF SHA256
`2c22a025b98d32b3a3a95150f8be1579aae5e2ecafb490201fc55e7c25d006e2`.
Flash commit now propagates Unlock/Lock failures, skips erase/programming
when Unlock fails, and preserves the configuration layout and CRC.

Existing validation includes Flash fault injection and real commit/reset,
watchdog and 1-Wire safe-state captures, idle, actual processor HardFault,
working-load measurement scheduling and final production smoke. Three
60-second repeatability runs returned all four channels in 90/90 GET_ALL
responses without E402, timeouts or partial replies. Earlier temperature
observations remain historical with unknown cause; this does not claim
that every internal sensor refresh succeeded. No additional runs assigned.

Release/T16 and queue optimization remain deferred outside this acceptance.
This is board firmware acceptance, not whole-analyzer certification.
See the [shared acceptance report, §§33.98–33.100](../DDS-240_readme/DDS-240_eko_system/Thermo/TEMP_SENSORS_EXECUTOR_REPORT.md),
[unification matrix](../DDS-240_readme/DDS-240_eko_system/Thermo/THERMO_HC_UNIFICATION_MATRIX.md)
and the committed `tests/acceptance_20261008.json` (path from project root).
All checkpoints below are historical; their instructions do not reopen tests.

## Historical checkpoint — 2026-10-05

Session finished. Resume from the [Thermo handoff](../DDS-240_readme/DDS-240_eko_system/Thermo/NEXT_SESSION_PROMPT.md).
T01–T13 are BUILD; hardware acceptance remains open. T13 preserves successful
samples across errors, applies the agreed 9000 ms age limit and clears a
channel sample after ROM-map application (D05).
Next: finish agreeing T14 against HC scheduling before providing manual edits.
The proposed 3000 ms start-to-start period is not implemented or finally agreed.
T15/T16 remain open. Last incremental Debug build: text=40052, data=100,
bss=14264; working-source host model passed. No commit, flashing or bench work
this session. Firmware changes remain uncommitted. Entries below are history.

## Historical checkpoint — 2026-10-02

Resume from the [Thermo handoff](../DDS-240_readme/DDS-240_eko_system/Thermo/NEXT_SESSION_PROMPT.md).
T07/T08 are BUILD; hardware acceptance remains open. T09 scope is discussed,
but its code is unchanged: preserve PRIMASK in CAN_Diagnostics_GetSnapshot.
Define each change's boundaries from the HC reference and Thermo requirements
before proposing code. Session finished; entries below are historical.

## Historical checkpoint — 2026-10-01

This checkpoint supersedes the historical entry below. Resume from the
[Thermo handoff](../DDS-240_readme/DDS-240_eko_system/Thermo/NEXT_SESSION_PROMPT.md).
T01–T06 have build confirmation. T07 is partial: safety API and protected
1-Wire LOW operations are implemented; the user confirmed a clean build.
PrepareReset is not yet called by F002/F006. Next: high-level driver
cancellation, monitor/map protection, then dispatcher integration.
Finish Thermo unification before testing; Studio/Conductor integration follows.
Firmware changes are entered manually by the user. Hardware acceptance remains
open; the earlier flashing confirmation covers T01, not subsequent changes.

## Current work — 2026-09-30: Thermo unification, T01 implemented

Start with the [Thermo entry point and working rules](../DDS-240_readme/DDS-240_eko_system/Thermo/NEXT_SESSION_PROMPT.md)
and the [source audit matrix](../DDS-240_readme/DDS-240_eko_system/Thermo/THERMO_HC_UNIFICATION_MATRIX.md).
T01 is implemented and directly compared with HC: two hardware CAN banks
(COMMAND/broadcast and COMMAND/runtime NodeID), updated before DONE in F005.
Full Debug rebuild and host filter model passed. Flashing and hardware
acceptance remain pending; resume here next session. Other changes require
separate discussion. See the Thermo report §33.14 for evidence.

The common-layer reference is HC, limited to verified blocks. Preserve the
Thermo DS18B20 domain. Historical descriptions below are not acceptance claims:
there are four RTOS tasks, and
Release source/include configuration requires correction (T16).

---

![STM32](https://img.shields.io/badge/MCU-STM32F103-blue.svg)
![FreeRTOS](https://img.shields.io/badge/RTOS-FreeRTOS%20(CMSIS--V2)-green.svg)
![Protocol](https://img.shields.io/badge/Protocol-DDS--240%20(CAN%202.0B)-orange.svg)
![Sensors](https://img.shields.io/badge/Sensors-8x%20DS18B20-red.svg)

Industrial-grade execution module for high-precision temperature monitoring in biochemical analyzers. This firmware manages up to 8 DS18B20 sensors on a shared 1-Wire bus, providing data via a transactional CAN protocol (DDS-240).

## 🚀 Key Features

- **Industrial 1-Wire Driver**: Direct Register Access (DRA) using `BSRR/BRR` registers for precise bit-banging timings on STM32F103.
- **Auto-Discovery**: Full implementation of the Maxim Integrated Search ROM algorithm for automatic detection of all sensors on the bus.
- **DDS-240 CAN Protocol**: 29-bit Extended ID communication with full transaction lifecycle management (COMMAND -> ACK -> DATA -> DONE).
- **Advanced Service Layer (0xFxxx)**: Remote management capabilities, including "Warm Finger" sensor mapping and identification without reflashing.
- **Identity Management**: Unique identification using 96-bit factory-programmed MCU UID, default NodeID `0x40`, and Thermo DeviceType `0x02`.
- **NACK Namespace**: Common executor codes are shared across boards; Thermo `SENSOR_FAILURE` is the domain code `0xE400`.
- **Fail-Safe Storage**: Internal Flash-based persistent storage for sensor mapping with CRC16 and MagicKey validation.
- **Thread-Safe Architecture**: Fully asynchronous task-based design using FreeRTOS Mutexes, Semaphores, and Message Queues.

## 🏗 System Architecture

The firmware is structured into three specialized RTOS tasks to ensure high availability and responsiveness:

1.  **CAN Handler Task**: Manages low-level bxCAN hardware, interrupt-driven RX/TX buffering, and hardware-level destination address filtering.
2.  **Dispatcher Task**: Implements the application-level command parser and handles the Command/Response state machine (Service & Operational commands).
3.  **Temperature Monitor Task**: Executes the industrial polling cycle (Broadcast Start -> RTOS Wait -> Match ROM Sequential Read) for all mapped sensors.

## 📡 Service & Identity

Each board is uniquely identified and managed within the analyzer ecosystem:
- **NodeID**: `0x40` (Thermo Board).
- **Device Type**: `0x02`.
- **Unique ID**: 96-bit MCU UID used for physical instance identification.
- **Service API**:
    - `0xF001`: Request Device Info (Type, FW Version, 96-bit UID).
    - `0xF002`: Remote Reboot with 0xDEAD Magic Key protection.
    - `0xF101`: Trigger 1-Wire Bus Scan and discovery.
    - `0xF103`: Map physical ROM ID to a logical channel (e.g., "Incubator", "Reagents").

## 🛠 Hardware Requirements

- **MCU**: STM32F103C8T6.
- **CAN Transceiver**: TJA1050 / SN65HVD230.
- **Sensors**: DS18B20 (up to 8 units) on a single bus.
- **Timer Configuration**: `TIM3` configured for 1µs resolution for precise 1-Wire delays.

## 📂 Project Structure

- `App/src/tasks/`: Implementation of the RTOS tasks and application logic.
- `App/src/ds18b20.c`: Optimized 1-Wire driver with full Search ROM support.
- `App/src/app_flash.c`: Reliable persistent configuration management.
- `readme/`: Detailed specifications of the CAN protocol and architectural concepts.

## 📖 Documentation

The project keeps only a local pointer in `readme/README.md`; the authoritative
documents live in the shared workspace documentation tree:

- [Local documentation pointer](readme/README.md)
- [Thermo executor report](../DDS-240_readme/DDS-240_eko_system/Thermo/TEMP_SENSORS_EXECUTOR_REPORT.md)
- [Ecosystem standard](../DDS-240_readme/DDS-240_eko_system/DDS-240_ECOSYSTEM_STANDARD.md)
- [Low-level command set](../DDS-240_readme/DDS-240_eko_system/CAN_Protocol/5_Low_Level_Commands.md)

---
*Developed as part of the SmartHeater & Analyzer ecosystem. Advanced Level Engineering.*
