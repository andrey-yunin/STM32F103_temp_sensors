#!/usr/bin/env python3
"""Exercise the actual Commit function with injected HAL return statuses.

Checks control flow only, not physical Flash or RTOS behavior.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'App/src/app_flash.c').read_text()
start = source.index('bool AppConfig_Commit(void) {')
body_start = source.index('{', start)
depth = 1
end = body_start + 1
while depth:
    depth += (source[end] == '{') - (source[end] == '}')
    end += 1
function = source[start:end]

stub = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef struct { uint32_t TypeErase, PageAddress, NbPages; } FLASH_EraseInitTypeDef;
typedef struct { uint32_t words[18]; uint16_t reserved, checksum; } AppConfig_t;
_Static_assert(sizeof(AppConfig_t) == 76, "Thermo layout size");
static AppConfig_t g_app_config;
static int configMutex = 1;
#define osOK 0
#define osWaitForever UINT32_MAX
#define APP_CONFIG_FLASH_ADDR 0x0800FC00U
#define FLASH_TYPEERASE_PAGES 0U
#define FLASH_TYPEPROGRAM_WORD 2U
static int lock_status, unlock_status, erase_status, program_status;
static int acquire_status, fail_word;
static int acquires, releases, unlocks, erases, programs, locks, checksums;
static int osMutexAcquire(int mutex, uint32_t timeout) {
    assert(mutex == 1 && timeout == osWaitForever); ++acquires;
    return acquire_status;
}
static int osMutexRelease(int mutex) { assert(mutex == 1); ++releases; return osOK; }
static uint16_t CalculateChecksum(const AppConfig_t *cfg) {
    assert(cfg == &g_app_config); ++checksums; return 0x1234;
}
static HAL_StatusTypeDef HAL_FLASH_Unlock(void) { ++unlocks; return unlock_status; }
static HAL_StatusTypeDef HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef *cfg, uint32_t *error) {
    assert(unlock_status == HAL_OK);
    assert(cfg->TypeErase == FLASH_TYPEERASE_PAGES);
    assert(cfg->PageAddress == APP_CONFIG_FLASH_ADDR && cfg->NbPages == 1);
    assert(error != 0); ++erases; return erase_status;
}
static HAL_StatusTypeDef HAL_FLASH_Program(uint32_t type, uint32_t addr, uint64_t value) {
    assert(unlock_status == HAL_OK && erase_status == HAL_OK);
    assert(type == FLASH_TYPEPROGRAM_WORD);
    assert(addr == APP_CONFIG_FLASH_ADDR + 4U * programs);
    uint32_t expected;
    memcpy(&expected, (const uint8_t *)&g_app_config + 4U * programs, 4);
    assert(value == expected);
    ++programs;
    return programs == fail_word ? program_status : HAL_OK;
}
static HAL_StatusTypeDef HAL_FLASH_Lock(void) { ++locks; return lock_status; }
'''
checks = r'''
int main(void) {
    for (unlock_status=0; unlock_status<4; ++unlock_status)
    for (erase_status=0; erase_status<4; ++erase_status)
    for (program_status=0; program_status<4; ++program_status)
    for (lock_status=0; lock_status<4; ++lock_status)
    for (fail_word=1; fail_word<=19; ++fail_word) {
        acquires=releases=unlocks=erases=programs=locks=checksums=0;
        acquire_status=osOK;
        memset(&g_app_config, 0x5a, sizeof(g_app_config));
        bool result=AppConfig_Commit();
        assert(result == (!unlock_status && !erase_status && !program_status && !lock_status));
        assert(acquires==1 && releases==1 && unlocks==1 && locks==1 && checksums==1);
        assert(erases == (unlock_status==HAL_OK));
        int expected_programs=0;
        if (!unlock_status && !erase_status)
            expected_programs=program_status ? fail_word : 19;
        assert(programs==expected_programs);
        assert(g_app_config.checksum==0x1234);
    }
    acquires=releases=unlocks=erases=programs=locks=checksums=0;
    acquire_status=1;
    assert(!AppConfig_Commit());
    assert(acquires==1 && !releases && !unlocks && !erases && !programs && !locks && !checksums);
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='thermo-commit-') as directory:
    tmp = Path(directory)
    (tmp / 'check.c').write_text(stub + function + checks)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    str(tmp / 'check.c'), '-o', str(tmp / 'check')], check=True)
    subprocess.run([str(tmp / 'check')], check=True)
print('PASS: 4864 HAL-status/word-position combinations and mutex acquisition failure')
