#!/usr/bin/env python3
"""Execute the linked LCD setup and IRQ with modeled MMIO, including 24-bit wrap.

Panel command delays and PWM calls are skipped. This checks the application and
SDK register writes, IRQ arithmetic and SRAM placement; scan cadence is measured
separately on the physical device.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE
from unicorn.arm_const import (UC_CPU_ARM_CORTEX_M4, UC_ARM_REG_SP,
                              UC_ARM_REG_PC, UC_ARM_REG_LR, UC_ARM_REG_R0)

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--elf', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
prefix = str(Path.home()/'.cache/phicomm/toolchains/'
             'arm-gnu-toolchain-13.3.rel1-x86_64-arm-none-eabi/bin/arm-none-eabi-')
symbols = {}
for line in subprocess.check_output([prefix+'nm', str(a.elf)], text=True).splitlines():
    parts = line.split()
    if len(parts) == 3:
        symbols[parts[2]] = int(parts[0],16)
u = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
u.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M4)
for address,size in [(0,0x80000),(0x20000000,0x10000),(0x80000000,0x800000),
                     (0x40000000,0x100000),(0xe000e000,0x2000)]:
    u.mem_map(address,size)
image = a.elf.with_suffix('.bin').read_bytes()
u.mem_write(0x14000,image)
def put(addr,v): u.mem_write(addr,struct.pack('<I',v))
def get(addr): return int.from_bytes(u.mem_read(addr,4),'little')
def call(name,r0=0):
    u.reg_write(UC_ARM_REG_SP,0x20010000)
    u.reg_write(UC_ARM_REG_LR,0x7af01)
    u.reg_write(UC_ARM_REG_R0,r0)
    u.emu_start(symbols[name]|1,0x7af00,count=2000000)
    assert u.reg_read(UC_ARM_REG_PC)==0x7af00,name
skip = {symbols[n] for n in ('PORT_Init','board_delay','PWM_Init','PWM_Start','panel') if n in symbols}
def code(cpu,addr,size,ctx):
    if addr in skip: cpu.reg_write(UC_ARM_REG_PC,cpu.reg_read(UC_ARM_REG_LR))
u.hook_add(UC_HOOK_CODE,code)
put(symbols['CyclesPerUs'],150)
put(symbols['SystemCoreClock'],150000000)
call('board_display')
assert get(0x40002014)&(1<<13), 'continuous LCD scanning was disabled'
assert get(0x40002004)==1, 'frame-end IRQ missing'
assert get(0x40046900)==0xffffff and get(0x40046930)==149
assert not get(0x40046910), 'microsecond timer must not generate an IRQ'
# The frame ISR must be independent of the external memory and its LVGL heap.
u.mem_unmap(0x80000000,0x800000)
for name in ('lcd_frame_count','lcd_period_count','lcd_last_us','lcd_period_min',
             'lcd_period_max','lcd_period_total','timer_high','timer_last'):
    assert 0x20000000<=symbols[name]<0x2000bfc0,name
times=[0xfff000,0xfff000+20000,0xfff000+40020]
for now in times:
    put(0x40046904,(~now)&0xffffff)
    put(0x40002000,1)
    call('LCD_Handler')
assert get(symbols['lcd_frame_count'])==3
assert get(symbols['lcd_period_count'])==2
assert get(symbols['lcd_period_min'])==20000
assert get(symbols['lcd_period_max'])==20020
assert get(symbols['lcd_period_total'])==40020
put(0x40002000,0);call('LCD_Handler')
assert get(symbols['lcd_frame_count'])==3, 'spurious IRQ changed counters'
call('board_display_diag',0x20005000)
assert get(0x20005000+64)==3 and get(0x20005000+68)==20010
assert get(0x20005000+96)==150000000 and get(0x20005000+100)==6
assert get(0x20005000+104)==560 and get(0x20005000+108)==894, 'sync widths counted twice'
a.output.write_text(json.dumps({'passed':True,'sha256':hashlib.sha256(image).hexdigest(),
    'cases':['continuous LCD scan preserved','1 MHz / 24-bit timer with no IRQ',
             'frame IRQ runs without SDRAM','24-bit counter wrap extends correctly',
             'spurious IRQ ignored','actual controller totals include sync within the back porch'],
    'limit':__doc__},indent=2)+'\n')
print('PASS actual ARM LCD continuous scan, 1 MHz timer, IRQ SRAM isolation and counter wrap')
