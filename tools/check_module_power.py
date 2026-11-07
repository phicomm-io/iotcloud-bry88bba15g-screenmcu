#!/usr/bin/env python3
"""Execute the linked ARM OTA interlock and fault recovery against GPIO MMIO.

This checks the real application callbacks and module policy. GPIO readback is
modeled as a register; it does not model supply voltage or module electronics.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE
from unicorn.arm_const import (UC_CPU_ARM_CORTEX_M4, UC_ARM_REG_SP,
                              UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_R1)

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--elf', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[1]
prefix = str(Path.home()/'.cache/phicomm/toolchains/'
             'arm-gnu-toolchain-13.3.rel1-x86_64-arm-none-eabi/bin/arm-none-eabi-')
symbols = {}
for line in subprocess.check_output([prefix+'nm', str(a.elf)], text=True).splitlines():
    parts = line.split()
    if len(parts) == 3:
        symbols[parts[2]] = int(parts[0], 16)
def sym(name):
    return symbols[name] if name in symbols else next(
        v for k, v in symbols.items() if k.startswith(name+'.'))
fields = ['write', 'read', 'context', 'initialized', 'inhibited', 'duration']
with tempfile.TemporaryDirectory(prefix='bry88-module-layout-') as tmp:
    tmp = Path(tmp)
    source = '#include <stddef.h>\n#include "runtime.h"\nconst unsigned layout[]={'
    source += ','.join('offsetof(screen_runtime,module.'+f+')' for f in fields)+'};\n'
    (tmp/'layout.c').write_text(source)
    subprocess.run([prefix+'gcc', '-mcpu=cortex-m33', '-mthumb', '-mfloat-abi=soft',
                    '-I'+str(root/'include'), '-I'+str(root/'protocol'), '-c',
                    str(tmp/'layout.c'), '-o', str(tmp/'layout.o')], check=True)
    subprocess.run([prefix+'objcopy', '-O', 'binary', '-j', '.rodata',
                    str(tmp/'layout.o'), str(tmp/'layout.bin')], check=True)
    offsets = dict(zip(fields, struct.unpack('<6I', (tmp/'layout.bin').read_bytes())))
u = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
u.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M4)
for address, size in [(0,0x80000), (0x20000000,0x10000),
                      (0x80000000,0x800000), (0x40000000,0x100000), (0xe000e000,0x2000)]:
    u.mem_map(address, size)
image = a.elf.with_suffix('.bin').read_bytes()
u.mem_write(0x14000, image)
def put(address, value): u.mem_write(address, struct.pack('<I', value))
def get(address): return int.from_bytes(u.mem_read(address,4),'little')
def call(name, r0=0, r1=0):
    u.reg_write(UC_ARM_REG_SP, 0x20010000)
    u.reg_write(UC_ARM_REG_LR, 0x7af01)
    u.reg_write(UC_ARM_REG_R0, r0)
    u.reg_write(UC_ARM_REG_R1, r1)
    u.emu_start(sym(name)|1, 0x7af00, count=100000)
runtime = sym('runtime')
module = runtime + offsets['context']  # context is the first module field
put(runtime+offsets['write'], sym('board_module_write')|1)
put(runtime+offsets['read'], sym('board_module_read')|1)
u.mem_write(runtime+offsets['initialized'], b'\x01')
u.mem_write(sym('normal_ready'), b'\x01')
pin = 0x40041870  # GPIOD.DATAPIN12
put(pin, 1)
call('maintenance_activity', 0, 1)
assert get(pin) == 0 and not get(runtime+offsets['duration'])
assert u.mem_read(runtime+offsets['inhibited'],1) == b'\x01'
call('screen_module_restore', module)  # Same path used by link reset.
call('screen_module_tick', module, 120000)
assert get(pin) == 0
call('maintenance_activity', 0, 0)
assert get(pin) == 1 and u.mem_read(runtime+offsets['inhibited'],1) == b'\x00'
reached = []
def pump(cpu, address, size, context):
    reached.append('pump'); cpu.emu_stop()
u.hook_add(UC_HOOK_CODE, pump, begin=sym('pump'), end=sym('pump'))
# Make all SDRAM unavailable: recovery must use its direct GPIO and SRAM path.
u.mem_write(sym('lvgl_tick_active'),b'\x01')
u.mem_unmap(0x80000000, 0x800000)
call('board_recovery')
assert reached and get(pin) == 0 and u.mem_read(sym('normal_ready'),1) == b'\x00'
assert u.mem_read(sym('lvgl_tick_active'),1)==b'\x00'
call('SysTick_Handler')  # Must not call into LVGL's unmapped SDRAM in recovery.
a.output.write_text(json.dumps({'passed':True, 'image_sha256':hashlib.sha256(image).hexdigest(),
    'cases':['OTA low', 'link reset cannot release', 'lease expiry cannot release',
             'OTA exit high', 'fault recovery low with SDRAM unmapped and UART pump reached',
             'recovery disables the LVGL ISR tick before SysTick runs without SDRAM'],
    'limit':__doc__}, indent=2)+'\n')
print('PASS actual ARM module interlock, OTA exit and fault recovery without SDRAM')
