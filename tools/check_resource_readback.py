#!/usr/bin/env python3
"""Run the linked ARM NOR driver against an SFC/GPIO NOR model.

Checks the actual aligned AHB stores, register commands, resource/OTA boundaries,
readback and explicit diagnostic modes. Physical timing still requires a device.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
from unicorn import (Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS,
                     UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE)
from unicorn.arm_const import (UC_CPU_ARM_CORTEX_M4, UC_ARM_REG_SP, UC_ARM_REG_PC,
                              UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_R1,
                              UC_ARM_REG_R2, UC_ARM_REG_R3)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--nm', default=str(Path.home() / '.cache/phicomm/toolchains/'
                        'arm-gnu-toolchain-13.3.rel1-x86_64-arm-none-eabi/bin/arm-none-eabi-nm'))
    args = parser.parse_args()
    symbols = {}
    for line in subprocess.check_output([args.nm, str(args.elf)], text=True).splitlines():
        fields = line.split()
        if len(fields) == 3:
            symbols[fields[2]] = int(fields[0], 16)
    image = args.elf.with_suffix('.bin').read_bytes()
    cpu = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    cpu.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M4)
    for address, size in ((0, 0x80000), (0x20000000, 0x10000),
                          (0x40000000, 0x100000), (0x70000000, 0x1000000),
                          (0xe000e000, 0x2000)):
        cpu.mem_map(address, size)
    cpu.mem_write(0x14000, image)
    nor = bytearray(b'\xff') * 0x1000000
    sfc = 0x4004a800
    state = dict(flags=0, sr1=0, sr2=4, go=0, ticks=0, fault=False, stuck=False)
    state.update(boot=False, boot_stop=0, fast=False, drop=False)
    counts = dict(sfc_quad_read=0, sfc_single_read=0, sfc_quad_write=0,
                  sfc_single_write=0, sfc_erase=0, gpio_read=0,
                  gpio_write=0, gpio_erase=0, gpio_spi=0)
    wire = []
    gpio_bit = 0

    def get32(a):
        return int.from_bytes(cpu.mem_read(a, 4), 'little')

    def put32(a, value):
        cpu.mem_write(a, int(value).to_bytes(4, 'little'))

    def finish(result=1):
        cpu.reg_write(UC_ARM_REG_R0, result)
        cpu.reg_write(UC_ARM_REG_PC, cpu.reg_read(UC_ARM_REG_LR))

    def erase(at, cmd, bus):
        size = {0x20: 4096, 0x52: 32768, 0xd8: 65536}[cmd]
        assert at % size == 0
        nor[at:at+size] = b'\xff' * size
        counts[bus + '_erase'] += 1

    def register_command():
        cfg, cmd = get32(sfc), get32(sfc+0x24)
        kind = cfg & 15
        if cmd == 0x9f:
            assert kind == 2
            put32(sfc+0x1c, int.from_bytes(bytes.fromhex('684018'), 'little'))
        elif cmd in (5, 0x35):
            assert kind == 1
            put32(sfc+0x1c, state['sr1' if cmd == 5 else 'sr2'])
        elif cmd == 1:
            assert kind == 6 and cfg & 0x1000
            data = get32(sfc+0x1c)
            state['sr1'], state['sr2'] = data & 255, (data >> 8) & 255
            if state['fast']: state['flags'] |= 2
        else:
            assert kind == 7 and cfg & 0x1000
            erase(get32(sfc+0x18), cmd, 'sfc')
        state['go'] = 1 if state['stuck'] else 0

    def memory(uc, access, address, size, value, context):
        nonlocal gpio_bit
        if access == 16:  # UC_MEM_READ
            if address == sfc+0x14:
                put32(address, state['go'])
            elif address == sfc+0xc:
                put32(address, state['flags'])
            elif address == symbols['board_ms']:
                state['ticks'] += 1
                put32(address, state['ticks'])
            elif 0x70000000 <= address < 0x71000000:
                assert size == 4 and address % 4 == 0
                quad = (get32(sfc) >> 10) & 3 == 2
                assert (get32(sfc+0x20) >> 24) == (0xeb if quad else 3)
                assert not quad or state['sr2'] & 2
                counts['sfc_quad_read' if quad else 'sfc_single_read'] += 1
                at = address-0x70000000
                uc.mem_write(address, bytes(nor[at:at+size]))
                if state['fault']:
                    state['flags'] |= 2
        else:
            if address == sfc+0xc:
                state['flags'] &= ~value
            elif address == sfc+0x14 and value == 1:
                register_command()
            elif 0x70000000 <= address < 0x71000000:
                assert size == 4 and address % 4 == 0 and get32(sfc) & 0x1000
                quad = bool(get32(sfc) & 0x200)
                assert ((get32(sfc+0x20) >> 8) & 255) == (0x32 if quad else 2)
                assert not quad or state['sr2'] & 2
                counts['sfc_quad_write' if quad else 'sfc_single_write'] += 1
                at = address-0x70000000
                data = value.to_bytes(size, 'little')
                if not state['drop']:
                    nor[at:at+size] = bytes(a & b for a, b in zip(nor[at:at+size], data))
                if state['fast'] or state['drop']: state['flags'] |= 2
            elif address == 0x40041854 and value == 1 and wire and wire[0] == 3:
                at = int.from_bytes(bytes(wire[1:4]), 'big')+gpio_bit//8
                put32(0x4004185c, (nor[at] >> (7-gpio_bit % 8)) & 1)
                gpio_bit += 1
                counts['gpio_read'] += 1

    def code(uc, address, size, context):
        nonlocal gpio_bit
        r0 = uc.reg_read(UC_ARM_REG_R0)
        if state['boot']:
            if address == state['boot_stop']:
                uc.emu_stop()
                return
            if address == symbols['board_early']:
                finish()
                return
            if address == symbols['nor_sfc_config']:
                assert r0 == 1
                state['boot_stop'] = uc.reg_read(UC_ARM_REG_LR) & ~1
        if address == symbols['cs']:
            if not r0:
                wire.clear()
                gpio_bit = 0
            elif wire:
                if wire[0] == 2:
                    at = int.from_bytes(bytes(wire[1:4]), 'big')
                    data = bytes(wire[4:])
                    assert at//256 == (at+len(data)-1)//256
                    nor[at:at+len(data)] = bytes(a & b for a, b in zip(nor[at:at+len(data)], data))
                    counts['gpio_write'] += 1
                elif wire[0] in (0x20, 0x52, 0xd8):
                    erase(int.from_bytes(bytes(wire[1:4]), 'big'), wire[0], 'gpio')
            finish()
        elif address == symbols['spi']:
            wire.append(r0 & 255)
            counts['gpio_spi'] += 1
            result = 0
            if wire[0] == 0x9f and len(wire) > 1:
                result = [0x68, 0x40, 0x18][len(wire)-2]
            elif wire[0] in (5, 0x35) and len(wire) == 2:
                result = state['sr1' if wire[0] == 5 else 'sr2']
            finish(result)

    cpu.hook_add(UC_HOOK_CODE, code)
    cpu.hook_add(UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, memory)
    buffer = 0x20004000

    def call(name, *args):
        for reg, value in ((UC_ARM_REG_SP, 0x20010000), (UC_ARM_REG_LR, 0x7af01)):
            cpu.reg_write(reg, value)
        for reg, value in zip((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3), args):
            cpu.reg_write(reg, value)
        cpu.emu_start(symbols[name] | 1, 0x7af00, count=3000000)
        assert cpu.reg_read(UC_ARM_REG_PC) == 0x7af00, name
        return cpu.reg_read(UC_ARM_REG_R0)

    def read(at, length):
        assert call('board_nor_read', 0, at, buffer, length)
        return bytes(cpu.mem_read(buffer, length))

    def write(name, at, data):
        cpu.mem_write(buffer, data)
        assert call(name, 0, at, buffer, len(data))

    def mode():
        return cpu.mem_read(symbols['nor_read_mode'], 1)[0]

    def boot():
        # Execute the real reset entry through its NOR setup. board_early is
        # skipped because PLL/UART/LCD clocks are outside this NOR model.
        state.update(boot=True, boot_stop=0)
        cpu.reg_write(UC_ARM_REG_SP, 0x20010000)
        cpu.emu_start(symbols['screen_start'] | 1, 0x7af00, count=3000000)
        assert state['boot_stop'] and cpu.reg_read(UC_ARM_REG_PC) == state['boot_stop']
        state['boot'] = False

    boot()
    assert mode() == 3
    assert call('board_jedec') == 0x684018
    state['fast'] = True
    payload = hashlib.shake_256(b'BRY88 resource readback regression').digest(204960)
    base = 0xa42000
    for offset in range(0, len(payload), 512):
        part = payload[offset:offset+512]
        write('board_resource_write', base+offset, part)
        assert read(base+offset, len(part)) == part
    assert state['sr1'] == 0 and state['sr2'] == 6  # QE preserves other bits.
    assert counts['gpio_spi'] == 0
    nor[0xb00000:0xb00010] = b'abc'+b'\xff'*11+b'yz'
    write('board_resource_write', 0xb00003, b'font header')
    assert read(0xb00000, 16) == b'abcfont headeryz'
    assert call('board_resource_erase', 0, base, 4096)
    assert read(base+512, 512) == b'\xff'*512
    assert call('board_nor_erase', 0, 0x780000, 0x68000)
    staged = hashlib.shake_256(b'OTA staged image').digest(0x67000)
    digest = hashlib.md5()
    for at in range(0, len(staged), 1024):
        write('board_nor_write', 0x780000+at, staged[at:at+1024])
        digest.update(read(0x780000+at, 1024))
    assert digest.digest() == hashlib.md5(staged).digest()
    assert call('board_flag_erase', 0, 0x7f0000, 65536)
    nor[0x7f0001:0x7f0004] = b'xyz'
    write('board_flag_write', 0x7f0000, b'\xa1')
    assert read(0x7f0000, 4) == b'\xa1xyz'
    state['drop'] = True
    cpu.mem_write(buffer, b'\x00')
    assert not call('board_resource_write', 0, base, buffer, 1)
    state['drop'] = False
    for at, length in ((0x1000, 1), (0xa7ffff, 2), (0xafffff, 2), (0xefffff, 2)):
        assert not call('board_resource_write', 0, at, buffer, length)
    assert not call('board_nor_erase', 0, 0xffff8000, 0x8000)
    state['fault'] = True
    assert not call('board_nor_read', 0, base, buffer, 4)
    state['fault'] = False
    assert mode() == 3 and counts['gpio_spi'] == 0
    state['stuck'] = True
    assert not call('board_nor_read', 0, base, buffer, 4)
    state['stuck'] = False
    state['go'] = 0
    assert mode() == 3 and counts['gpio_spi'] == 0
    for bus in (2, 1):
        assert call('board_nor_mode', 0, bus) and mode() == bus
        assert call('board_resource_erase', 0, base, 4096)
        write('board_resource_write', base+253, b'cross page boundary')
        assert read(base+253, 19) == b'cross page boundary'
    assert not call('board_nor_mode', 0, 4) and mode() == 1
    boot()
    assert mode() == 3
    cases = ['401 wallpaper chunks on SFC quad with no GPIO transactions',
             'SFC register JEDEC byte order', 'QE setup preserves protection/status bits',
             'fast WIP completion requires exact readback', 'failed programs are rejected',
             'unaligned writes preserve adjacent bytes',
             'resource 4KiB and OTA 32KiB erases', 'complete staged OTA MD5 readback',
             '64KiB flag erase and single-byte flag programming', 'resource/OTA range rejection',
             'SFC errors and timeouts do not downgrade', 'explicit single-SFC/GPIO selection',
             'diagnostic write across page boundary', 'reset restores quad SFC']
    report = dict(result='PASS', image_sha256=hashlib.sha256(image).hexdigest(),
                  cases=cases, bus_calls=counts, limit=__doc__)
    args.output.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
