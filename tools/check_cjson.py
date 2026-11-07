#!/usr/bin/env python3
import hashlib,json,sys
from pathlib import Path
root=Path(__file__).resolve().parents[1]
lock=json.loads((root/'platform/cjson.lock.json').read_text())
for name,sha in lock['files'].items():
    assert hashlib.sha256((Path(sys.argv[1])/name).read_bytes()).hexdigest()==sha,name
