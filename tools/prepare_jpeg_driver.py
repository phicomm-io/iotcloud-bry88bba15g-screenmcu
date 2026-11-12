#!/usr/bin/env python3
"""Apply the two reviewed bounds/shift fixes to the pinned vendor driver.

The upstream SDK remains immutable. The AC register bank holds 164 symbols,
but jfif_info_t has 162; fill the last two register bytes with zero. Empty DC
entries must not shift a signed integer by 32. The strict parser rejects all
unsupported sampling and table selectors before this driver is called.
"""
import pathlib
import re
import sys

source = pathlib.Path(sys.argv[1]).read_text()
for index in (2, 3):
    old = f"jfif_info->HTable[i].AC.codeVal[j+{index}]"
    assert source.count(old) == 1
    source = source.replace(old, f"(j+{index} < 162 ? jfif_info->HTable[i].AC.codeVal[j+{index}] : 0)")
old = "jfif_info->HTable[i].DC.codeWord[j+1] << (32 - jfif_info->HTable[i].DC.codeLen[j+1])"
assert source.count(old) == 1
source = source.replace(old, "(jfif_info->HTable[i].DC.codeLen[j+1] ? (uint32_t)jfif_info->HTable[i].DC.codeWord[j+1] << (32 - jfif_info->HTable[i].DC.codeLen[j+1]) : 0u)")
source = re.sub(r"(jfif_info->HTable\[i\]\.(?:DC|AC)\.code(?:Val|Word)\[[^\]]+\])(?=\s*<<)", r"(uint32_t)\1", source)
source = source.replace("(j+3 < 162 ? jfif_info->HTable[i].AC.codeVal[j+3] : 0) <<", "(uint32_t)(j+3 < 162 ? jfif_info->HTable[i].AC.codeVal[j+3] : 0) <<")
pathlib.Path(sys.argv[2]).write_text(source)
