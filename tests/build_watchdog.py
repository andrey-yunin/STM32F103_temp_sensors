#!/usr/bin/env python3
"""Build isolated R03 or production-check image from CubeIDE Debug makefiles."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--production', action='store_true')
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
source = root / 'Debug'
output = root / ('ProductionCheck' if args.production else 'WatchdogTest')
compiler = shutil.which('arm-none-eabi-gcc')
if compiler is None:
    candidates = sorted((Path.home() / 'st').glob(
        'stm32cubeide*/plugins/*gnu-tools*/tools/bin/arm-none-eabi-gcc'))
    if not candidates:
        raise SystemExit('arm-none-eabi-gcc not found; add STM32 toolchain to PATH')
    compiler = str(candidates[-1])
env = dict(os.environ)
env['PATH'] = str(Path(compiler).parent) + os.pathsep + env.get('PATH', '')
for path in source.rglob('*'):
    if path.is_file() and (path.suffix == '.mk' or path.name in ('makefile', 'objects.list')):
        dest = output / path.relative_to(source)
        dest.parent.mkdir(parents=True, exist_ok=True)
        content = path.read_text()
        if not args.production:
            content = content.replace('-DDEBUG', '-DDEBUG -DAPP_WATCHDOG_TEST_HOOKS=1')
        dest.write_text(content)
# Rebuild every object so flags from another configuration cannot leak in.
subprocess.run(['make', '-B', '-j4', 'all'], cwd=output, env=env, check=True)
elf = output / 'STM32F103_temp_sensors.elf'
for fmt, suffix in [('binary', '.bin'), ('ihex', '.hex')]:
    subprocess.run(['arm-none-eabi-objcopy', '-O', fmt, str(elf),
                    str(elf.with_suffix(suffix))], env=env, check=True)
print(f'Image: {elf}')
