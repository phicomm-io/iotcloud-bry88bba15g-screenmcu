#!/usr/bin/env python3
"""Execute the reviewed board loader's scatter initialization against an ELF.

Requires Unicorn. This checks actual loader CPU writes and linker placement;
physical SRAM retention and the remaining loader path need a live reset test.
The private boot backup is an input and is never bundled with this tool.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_MEM_WRITE
from unicorn.arm_const import UC_CPU_ARM_CORTEX_M4, UC_ARM_REG_SP, UC_ARM_REG_PC, UC_ARM_REG_LR

BOOT_SHA256 = "ccb9d414186c5ea47823badf5083f7ab5b1b61b02c6a8d0510af6ee31bebdf09"


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--boot", type=Path, required=True)
    p.add_argument("--elf", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--nm", default=str(Path.home() / ".cache/phicomm/toolchains/"
                   "arm-gnu-toolchain-13.3.rel1-x86_64-arm-none-eabi/bin/arm-none-eabi-nm"))
    args = p.parse_args()
    boot = args.boot.read_bytes()
    assert len(boot) == 0x14000 and hashlib.sha256(boot).hexdigest() == BOOT_SHA256
    symbols = {}
    for line in subprocess.check_output([args.nm, str(args.elf)], text=True).splitlines():
        fields = line.split()
        if len(fields) == 3:
            symbols[fields[2]] = int(fields[0], 16)
    start, end = symbols["__retained_start__"], symbols["__retained_end__"]
    assert start in (0x2000bfc0, 0x2000bff0) and start < end <= 0x2000c000
    assert symbols["__iram_end__"] <= start
    for name in ("fault_latch", "fault_latch_check"):
        assert start <= symbols[name] < end
    u = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    u.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M4)
    u.mem_map(0, 0x20000)
    u.mem_write(0, boot)
    u.mem_map(0x20000000, 0x10000)
    u.mem_write(0x20000000, b"\xa5" * 0x10000)
    u.reg_write(UC_ARM_REG_SP, struct.unpack_from("<I", boot)[0])
    seed = bytes(range(end - start))
    u.mem_write(start, seed)
    u.mem_write(symbols["fault_latch"], struct.pack("<I", 0x52454356))
    u.mem_write(symbols["fault_latch_check"], struct.pack("<I", ~0x52454356 & 0xffffffff))
    retained = bytes(u.mem_read(start, end - start))
    # The first live image placed its latch here; reproduce its erasure.
    u.mem_write(0x200001f0, struct.pack("<I", 0x52454356))
    writes = []
    def written(cpu, access, address, size, value, context):
        writes.append((address, size))
    u.hook_add(UC_HOOK_MEM_WRITE, written)
    u.emu_start(0x6f5, 0x1b0, count=1000000)
    assert u.reg_read(UC_ARM_REG_PC) == 0x1b0
    assert bytes(u.mem_read(0x200001f0, 4)) == b"\0" * 4
    assert bytes(u.mem_read(start, end - start)) == retained
    assert writes and all(0x20000000 <= a and a + n <= 0x20006230 for a, n in writes)
    fault = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    fault.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M4)
    fault.mem_map(0, 0x80000)
    fault.mem_write(0x14000, args.elf.with_suffix(".bin").read_bytes())
    fault.mem_map(0x20000000, 0x10000)
    fault.mem_map(0xe000e000, 0x2000)
    # Fault recovery must work even when the interrupted stack is unusable.
    fault.reg_write(UC_ARM_REG_SP, 0xdeadbee0)
    expected_fault = (0x101, 0x40000000, 0x81234567, 0x82345678)
    for at, value in zip((0xe000ed28, 0xe000ed2c, 0xe000ed34, 0xe000ed38), expected_fault):
        fault.mem_write(at, struct.pack('<I', value))
    reset = []
    def reset_write(cpu, access, address, size, value, context):
        if address == 0xe000ed0c:
            reset.append(value)
            cpu.emu_stop()
    fault.hook_add(UC_HOOK_MEM_WRITE, reset_write)
    fault.emu_start(symbols["board_fault_reset"] | 1, 0x7f000, count=100)
    assert reset == [0x05fa0004]
    assert struct.unpack("<I", fault.mem_read(symbols["fault_latch"], 4))[0] == 0x52454356
    assert struct.unpack("<I", fault.mem_read(symbols["fault_latch_check"], 4))[0] == ~0x52454356 & 0xffffffff
    if 'fault_record' in symbols:
        captured = struct.unpack('<8I', fault.mem_read(symbols['fault_record'], 32))
        assert captured[2:6] == expected_fault
        assert captured[7] == 0xdeadbee0
    frame_cases = []
    if symbols.get('nonce_fallback', 0) >= symbols.get('fault_record', 0) + 44:
        for name, sp, lr, cfsr, xpsr, valid in [
                ('basic', 0x2000fd00, 0xffffffb8, 1, 0x01000000, True),
                ('extended', 0x2000fc00, 0xffffffe9, 1, 0x01000000, True),
                ('invalid-sp', 0xdeadbee0, 0xfffffff9, 1, 0x01000000, False),
                ('partial-frame', 0x2000fff0, 0xfffffff9, 1, 0x01000000, False),
                ('stacking-fault', 0x2000fd00, 0xfffffff9, 0x1001, 0x01000000, False),
                ('psp-not-owned', 0x2000fd00, 0xfffffffd, 1, 0x01000000, False),
                ('not-thumb', 0x2000fd00, 0xfffffff9, 1, 0, False)]:
            fault.mem_write(symbols['fault_record'] + 32, b'\xa5' * 12)
            fault.mem_write(0xe000ed28, struct.pack('<I', cfsr))
            if 0x2000c000 <= sp <= 0x2000ff80:
                core = sp + (0 if lr & 16 else 72)
                fault.mem_write(core + 20, struct.pack('<III', 0x16001, 0x194bc, xpsr))
            fault.reg_write(UC_ARM_REG_SP, sp)
            fault.reg_write(UC_ARM_REG_LR, lr)
            reset.clear()
            fault.emu_start(symbols['board_fault_reset'] | 1, 0x7f000, count=200)
            assert reset == [0x05fa0004], name
            fields = struct.unpack('<3I', fault.mem_read(symbols['fault_record'] + 32, 12))
            assert fields == ((0x194bc, 0x16001, xpsr) if valid else (0, 0, 0)), (name, fields)
            frame_cases.append({'case': name, 'frame_captured': valid, 'result': 'pass'})
    report = {"result": "PASS", "boot_sha256": BOOT_SHA256,
              "elf_sha256": hashlib.sha256(args.elf.read_bytes()).hexdigest(),
              "executed": "actual factory scatter loader 0x6f4 -> 0x1b0",
              "loader_write_range": [hex(min(a for a, n in writes)),
                                     hex(max(a + n for a, n in writes))],
              "retained_range": [hex(start), hex(end)],
              "stack_bottom": "0x2000c000", "old_latch_erased": True,
              "new_latch_preserved": True, "fault_reset_without_stack": True,
              "internal_frame_cases": frame_cases,
              "limit": "CPU scatter initialization only; verify real reset and recovery separately"}
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print("PASS actual loader erases old low latch and preserves reserved recovery words")


if __name__ == "__main__":
    main()
