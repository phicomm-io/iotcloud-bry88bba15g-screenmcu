#!/usr/bin/env python3
"""Execute the linked ARM control-pointer guard with injected corruptions.

Uses the same compiler to extract structure offsets, then runs the actual guard
with mapped internal SRAM and SDRAM. Checks return on valid state, preservation
of damaged data, and recovery entry before any invalid callback can execute.
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
                              UC_ARM_REG_PC, UC_ARM_REG_LR, UC_ARM_REG_R0)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--elf', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--prefix', default=str(Path.home()/'.cache/phicomm/toolchains/'
        'arm-gnu-toolchain-13.3.rel1-x86_64-arm-none-eabi/bin/arm-none-eabi-'))
    a = p.parse_args()
    root = Path(__file__).resolve().parents[1]
    symbols = {}
    for line in subprocess.check_output([a.prefix+'nm',str(a.elf)],text=True).splitlines():
        parts = line.split()
        if len(parts) == 3:
            symbols[parts[2]] = int(parts[0],16)
    fields = [
        ('rtc','screen_rtc','read','board_rtc_read',5),
        ('rtc','screen_rtc','write','board_rtc_write',6),
        ('rtc','screen_rtc','context',None,7),
        ('runtime','screen_runtime','send','board_send',8),
        ('runtime','screen_runtime','factory','factory_line',9),
        ('runtime','screen_runtime','context','@factory',10),
        ('runtime','screen_runtime','calibration_event','factory_calibration',11),
        ('factory','factory_owner','runtime','@runtime',12),
        ('factory','factory_owner','maintenance','@maintenance',13),
        ('factory','factory_owner','power','@power',14),
        ('runtime','screen_runtime','module.write','board_module_write',15),
        ('runtime','screen_runtime','module.read','board_module_read',16),
        ('runtime','screen_runtime','module.context',None,17)]
    with tempfile.TemporaryDirectory(prefix='bry88-control-layout-') as td:
        td = Path(td)
        source = '#include <stddef.h>\n#include "rtc.h"\n#include "factory.h"\n'
        source += 'const unsigned layout[] = {' + ','.join(
            'offsetof('+typ+','+field+')' for _,typ,field,_,_ in fields) + '};\n'
        (td/'layout.c').write_text(source)
        subprocess.run([a.prefix+'gcc','-mcpu=cortex-m33','-mthumb','-mfloat-abi=soft',
            '-Os','-I'+str(root/'include'),'-I'+str(root/'protocol'),'-c',
            str(td/'layout.c'),'-o',str(td/'layout.o')],check=True)
        subprocess.run([a.prefix+'objcopy','-O','binary','-j','.rodata',
            str(td/'layout.o'),str(td/'layout.bin')],check=True)
        offsets = struct.unpack('<'+str(len(fields))+'I',(td/'layout.bin').read_bytes())

    def sym(name):
        return symbols[name] if name in symbols else next(
            value for key,value in symbols.items() if key.startswith(name+'.lto'))

    pointers = []
    for (obj,_,field,target,error),offset in zip(fields,offsets):
        expected = 0 if target is None else sym(target[1:]) if target.startswith('@') else sym(target)|1
        pointers.append((obj+'.'+field,sym(obj)+offset,expected,error))
    image = a.elf.with_suffix('.bin').read_bytes()
    cases = []
    for damaged in [None,*range(len(fields)),'panel']:
        u = Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
        u.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M4)
        for addr,n in [(0,0x80000),(0x20000000,0x10000),
                       (0x80000000,0x800000),(0x40000000,0x100000),(0xe000e000,0x2000)]:
            u.mem_map(addr,n)
        u.mem_write(0x14000,image)
        def put(addr,value): u.mem_write(addr,struct.pack('<I',value))
        def get(addr): return int.from_bytes(u.mem_read(addr,4),'little')
        for _,addr,value,_ in pointers: put(addr,value)
        u.mem_write(sym('normal_ready'),b'\x01')
        put(sym('fault_record'),9)
        index = 0 if damaged == 'panel' else damaged
        if index is not None: put(pointers[index][1],0xff79)
        if damaged == 'panel':
            u.mem_write(sym('panel_ready'),b'\x01')
            put(0x400461a4,6000)
            put(0x40002014,1<<13)
        reached = False
        def code(cpu,addr,n,ctx):
            nonlocal reached
            reached = True
            cpu.emu_stop()
        u.hook_add(UC_HOOK_CODE,code,begin=sym('board_recovery'),end=sym('board_recovery'))
        u.reg_write(UC_ARM_REG_SP,0x20010000)
        u.reg_write(UC_ARM_REG_LR,0x7af01)
        u.emu_start(sym('control_check')|1,0x7af00,count=20000)
        if index is None:
            assert not reached and u.reg_read(UC_ARM_REG_PC) == 0x7af00
            assert get(sym('board_sdram_error')) == 0
        else:
            assert reached
            assert get(sym('board_sdram_error')) == pointers[index][3]
            assert get(sym('recovery_reason')) == 6
            assert get(pointers[index][1]) == 0xff79
            assert get(sym('control_actual')) == 0xff79
            assert get(sym('control_expected')) == pointers[index][2]
            assert get(sym('control_address')) == pointers[index][1]
        assert get(sym('fault_record')) == 9
        if damaged == 'panel':
            assert get(0x400461a4) == 0 and not get(0x40002014) & (1<<13)
        cases.append({'case':'valid' if index is None else
                      'panel-active' if damaged == 'panel' else pointers[index][0],
                      'recovery_entered':reached,'error':get(sym('board_sdram_error')),'result':'PASS'})
    sensor_cases = []
    for damaged_step in [None, *range(1, 20)]:
        u = Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
        u.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M4)
        for addr,n in [(0,0x80000),(0x20000000,0x10000),
                       (0x80000000,0x800000),(0x40000000,0x100000),(0xe000e000,0x2000)]:
            u.mem_map(addr,n)
        u.mem_write(0x14000,image)
        for _,addr,value,_ in pointers: put(addr,value)
        put(sym('fault_record'),7)
        put(sym('SystemCoreClock'),120000000)
        put(sym('CyclesPerUs'),120)
        steps, reached = [], False
        def checkpoint(cpu,addr,n,ctx):
            step = cpu.reg_read(UC_ARM_REG_R0)
            steps.append(step)
            if step == damaged_step:
                put(pointers[5][1],0xff79)
        u.hook_add(UC_HOOK_CODE,checkpoint,
                   begin=sym('board_sensor_checkpoint'),end=sym('board_sensor_checkpoint'))
        u.hook_add(UC_HOOK_CODE,code,begin=sym('board_recovery'),end=sym('board_recovery'))
        u.reg_write(UC_ARM_REG_SP,0x20010000)
        u.reg_write(UC_ARM_REG_LR,0x7af01)
        u.emu_start(sym('board_sensor_init')|1,0x7af00,count=500000)
        assert steps == list(range(1,(damaged_step or 19)+1)), (damaged_step,steps)
        assert reached == (damaged_step is not None)
        assert get(sym('sensor_step')) == (damaged_step or 19)
        assert get(sym('fault_record')) == 7
        if damaged_step is not None:
            assert get(sym('board_sdram_error')) == 10
            assert get(sym('control_actual')) == 0xff79
            assert get(sym('control_expected')) == pointers[5][2]
            assert get(sym('control_address')) == pointers[5][1]
        else:
            assert u.reg_read(UC_ARM_REG_PC) == 0x7af00
            assert all(get(addr) == value for _,addr,value,_ in pointers)
            assert get(0x40041870) == 0, 'Sensor init enabled AM1008L before panel readiness'
        sensor_cases.append({'injected_step':damaged_step,'steps':steps,
                             'recovery_entered':reached,'result':'PASS'})
    report = {'method':__doc__,'result':'PASS','image_sha256':hashlib.sha256(image).hexdigest(),
              'cases':cases,'sensor_initialization_cases':sensor_cases,
              'sensor_limit':'Actual linked GPIO/UART/ADC code with modeled MMIO; no electrical fault model'}
    a.output.write_text(json.dumps(report,indent=2)+'\n')
    print('PASS actual ARM control guard: valid state, thirteen corruptions, panel shutdown; '
          'real sensor initialization and corruption at all 19 checkpoints')


if __name__ == '__main__':
    main()
