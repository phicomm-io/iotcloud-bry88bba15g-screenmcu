#!/usr/bin/env python3
"""Execute normal entry and a retained-SRAM reset in the actual ARM image.

Clock ROM, NOR flag reads and application MD5 helpers are modeled; no external
SDRAM is mapped. Stop at the first SDRAM-controller write and inspect recovery
words, then restart the same image retaining SRAM. This verifies reset policy,
not physical reset wiring or the business application after SDRAM startup.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

from unicorn import (Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS,
                     UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE)
from unicorn.arm_const import (UC_CPU_ARM_CORTEX_M4, UC_ARM_REG_PC,
                              UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_R1,
                              UC_ARM_REG_R2, UC_ARM_REG_R3)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--elf", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--expect", choices=("fixed", "legacy"), default="fixed")
    p.add_argument("--nm", default=str(Path.home() / ".cache/phicomm/toolchains/"
                   "arm-gnu-toolchain-13.3.rel1-x86_64-arm-none-eabi/bin/arm-none-eabi-nm"))
    args = p.parse_args()
    symbols = {}
    for line in subprocess.check_output([args.nm, str(args.elf)], text=True).splitlines():
        fields = line.split()
        if len(fields) == 3:
            symbols[fields[2]] = int(fields[0], 16)
    image = args.elf.with_suffix(".bin").read_bytes()
    u = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    u.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M4)
    for address, size in [(0, 0x80000), (0x20000000, 0x10000),
                          (0x40000000, 0x100000), (0xe000e000, 0x2000),
                          (0x11000000, 0x1000), (0x88000000, 0x1000)]:
        u.mem_map(address, size)
    u.mem_write(0x14000, image)

    def put(address, value):
        u.mem_write(address, struct.pack("<I", value))

    def get(address):
        return int.from_bytes(u.mem_read(address, 4), "little")

    def reset_registers():
        put(0x40000000, 0x3001)
        put(0x400aa04c, 1)
        put(0x400a0808, 0)
        put(0x400a0800, 0)
        put(0x40000024, 1)  # POR set, WDT clear: not a watchdog reset.
        put(0x400000d4, 0x13579bdf)
        put(0x400000d8, 0x10203040)

    reset_registers()
    wire = bytearray()
    phase, count, reached = "notice", 0, False
    mocks = {symbols[n] for n in ("screen_md5_init", "screen_md5_update",
                                 "screen_md5_final")}

    def read(cpu, access, address, size, value, context):
        if address == 0x400000d0:
            put(address, get(address) | 0x100)

    def write(cpu, access, address, size, value, context):
        nonlocal reached
        if address == 0x40042000:
            wire.append(value & 255)
        if phase == "normal" and 0x88000000 <= address < 0x88001000:
            reached = True
            cpu.emu_stop()

    def code(cpu, address, size, context):
        nonlocal count
        count += 1
        if address in mocks:
            if address == symbols["screen_md5_final"]:
                cpu.mem_write(cpu.reg_read(UC_ARM_REG_R1), bytes(16))
            cpu.reg_write(UC_ARM_REG_PC, cpu.reg_read(UC_ARM_REG_LR))
        elif address == symbols.get("board_nor_read"):
            assert cpu.reg_read(UC_ARM_REG_R1) == 0x7f0000
            assert cpu.reg_read(UC_ARM_REG_R3) == 1
            cpu.mem_write(cpu.reg_read(UC_ARM_REG_R2), b"\xff")
            cpu.reg_write(UC_ARM_REG_R0, 1)
            cpu.reg_write(UC_ARM_REG_PC, cpu.reg_read(UC_ARM_REG_LR))
        elif 0x11000000 <= address < 0x11001000:
            cpu.reg_write(UC_ARM_REG_PC, cpu.reg_read(UC_ARM_REG_LR))
        if count % 10000 == 0 and get(0xe000e010) & 1:
            put(symbols["board_ms"], get(symbols["board_ms"]) + 1)
        if phase == "notice" and wire.endswith(b"\r\n"):
            cpu.emu_stop()

    u.hook_add(UC_HOOK_MEM_READ, read)
    u.hook_add(UC_HOOK_MEM_WRITE, write)
    u.hook_add(UC_HOOK_CODE, code)
    u.emu_start(symbols["Reset_Handler"] | 1, 0x7f000, count=4000000)
    first = bytes(wire).decode().strip().split()
    assert first[:2] == ["screen_recovery", "3"] and int(first[4]) == 1, first
    phase = "normal"
    put(symbols["board_ms"], 31000)
    u.emu_start(u.reg_read(UC_ARM_REG_PC) | 1, 0x7f000, count=4000000)
    assert reached, "Did not reach normal startup after the maintenance window"
    words = [get(symbols["fault_latch"]), get(symbols["fault_latch_check"])]
    phase = "notice"
    wire.clear()
    reset_registers()
    u.emu_start(symbols["Reset_Handler"] | 1, 0x7f000, count=4000000)
    second = bytes(wire).decode().strip().split()
    assert second[:2] == ["screen_recovery", "3"], second
    mode = int(second[4])
    expected_words = [0, 0] if args.expect == "fixed" else [0x52454356, 0xadbabca9]
    expected_mode = 1 if args.expect == "fixed" else 3
    ok = words == expected_words and mode == expected_mode
    report = {"result": "PASS" if ok else "FAIL", "expected": args.expect,
              "image_sha256": hashlib.sha256(image).hexdigest(),
              "normal_entry_recovery_words": [hex(w) for w in words],
              "retained_sram_reset_mode": mode, "reset_status": 1,
              "method": __doc__}
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report))
    raise SystemExit(0 if ok else 1)


if __name__ == "__main__":
    main()
