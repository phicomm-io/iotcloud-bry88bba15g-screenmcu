#!/usr/bin/env python3
import hashlib,json,sys
from pathlib import Path
root=Path(sys.argv[1]);lock=json.loads((Path(__file__).resolve().parents[1]/'platform/swm34s/sdk.lock.json').read_text())
for name,digest in lock['files'].items():
 if hashlib.sha256((root/name).read_bytes()).hexdigest()!=digest:raise SystemExit('SDK mismatch: '+name)
print('Official SDK input hashes verified')
