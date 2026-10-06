#!/usr/bin/env python3
"""Host checks of actual watchdog source; not a hardware/scheduler test."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='thermo-wdg-') as directory:
    tmp = Path(directory)
    (tmp / 'cmsis_os.h').write_text('''
#include <stdint.h>
#define osOK 0
int osDelay(uint32_t ticks);
''')
    (tmp / 'main.h').write_text('''
#include <stdint.h>
typedef int IWDG_HandleTypeDef;
typedef struct { uint32_t CSR; } TestRcc;
extern TestRcc test_rcc;
#define RCC (&test_rcc)
#define __HAL_RCC_CLEAR_RESET_FLAGS() (test_rcc.CSR = 0)
void test_nop(void);
#define __NOP() test_nop()
void Error_Handler(void);
int HAL_IWDG_Refresh(IWDG_HandleTypeDef *handle);
''')
    (tmp / 'check.c').write_text('''
#include <assert.h>
#include <setjmp.h>
#include <string.h>
#include "task_watchdog.c"
IWDG_HandleTypeDef hiwdg;
TestRcc test_rcc;
static jmp_buf escape;
static int missing, refreshes, periods;
void test_nop(void) { longjmp(escape, 2); }
void Error_Handler(void) { longjmp(escape, 3); }
int HAL_IWDG_Refresh(IWDG_HandleTypeDef *h) { (void)h; ++refreshes; return 0; }
int osDelay(uint32_t ticks) {
    if (ticks == 1U) longjmp(escape, 1);
    assert(ticks == APP_WATCHDOG_SUPERVISOR_PERIOD_MS);
    for (int i = 0; i < APP_WDG_CLIENT_COUNT; ++i)
        if (i != missing) AppWatchdog_Heartbeat((AppWatchdogClient_t)i);
    if (++periods == 3 && missing == -1) longjmp(escape, 4);
    return osOK;
}
int main(void) {
    for (int mode = 1; mode <= 4; ++mode) {
        memset((void *)app_watchdog_heartbeats, 0, sizeof(app_watchdog_heartbeats));
        AppWatchdog_TestArm(mode);
        int target = mode == 4 ? 0 : mode - 1;
        for (int i = 0; i < APP_WDG_CLIENT_COUNT; ++i)
            if (i != target) AppWatchdog_Heartbeat((AppWatchdogClient_t)i);
        int result = setjmp(escape);
        if (!result) AppWatchdog_Heartbeat((AppWatchdogClient_t)target);
        assert(result == (mode == 4 ? 2 : 1));
        assert(app_watchdog_test_entered == (uint32_t)mode);
        assert(app_watchdog_heartbeats[target] == 0);
    }
    AppWatchdog_TestArm(0);
    for (missing = -1; missing < APP_WDG_CLIENT_COUNT; ++missing) {
        memset((void *)app_watchdog_heartbeats, 0, sizeof(app_watchdog_heartbeats));
        refreshes = periods = 0;
        test_rcc.CSR = 0x20000000U;
        int result = setjmp(escape);
        if (!result) app_start_task_watchdog(0);
        assert(app_watchdog_test_reset_csr == 0x20000000U);
        assert(test_rcc.CSR == 0);
        if (missing == -1) {
            assert(result == 4 && refreshes == 3);
            assert(app_watchdog_test_failed_mask == 0);
        } else {
            assert(result == 3 && refreshes == 1);
            assert(app_watchdog_test_failed_mask == (1U << missing));
        }
    }
    return 0;
}
''')
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-DAPP_WATCHDOG_TEST_HOOKS=1', '-I' + str(tmp),
                    '-I' + str(root / 'App/inc/tasks'),
                    '-I' + str(root / 'App/src/tasks'), str(tmp / 'check.c'),
                    '-o', str(tmp / 'check')], check=True)
    subprocess.run([str(tmp / 'check')], check=True)
print('PASS: four hook modes, healthy supervisor, each missing client, reset flags')
