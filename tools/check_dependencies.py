#!/usr/bin/env python3
"""Verify the exact shared LVGL checkout and checked-in generated font bytes."""
from pathlib import Path
import hashlib
import json
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
lock = json.loads((root/'dependencies.lock.json').read_text())
lvgl = Path(sys.argv[1]).expanduser() if len(sys.argv) > 1 else Path.home()/'.cache/phicomm/sources/lvgl-8.3.11'
actual = subprocess.check_output(['git', '-C', str(lvgl), 'rev-parse', 'HEAD'], text=True).strip()
if actual != lock['lvgl']['commit']:
    raise SystemExit(f'LVGL commit mismatch: {actual}')
subprocess.run(['git', '-C', str(lvgl), 'diff', '--quiet', 'HEAD', '--'], check=True)
for path, sha in lock['fonts'].items():
    if hashlib.sha256((root/path).read_bytes()).hexdigest() != sha:
        raise SystemExit(f'Generated font changed: {path}; regenerate and update lock')
print('LVGL and generated fonts verified')

if 'external_font' in lock:
    bank=lock['external_font']
    if hashlib.sha256((root/bank['file']).read_bytes()).hexdigest()!=bank['sha256']:
        raise SystemExit('External font does not match its source lock')
