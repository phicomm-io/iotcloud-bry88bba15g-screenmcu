#!/usr/bin/env python3
"""Regenerate checked-in UI subsets; requires lv_font_conv 1.5.3 via npm cache."""
from pathlib import Path
import hashlib
import json
import subprocess
import tempfile
import argparse
from pack_font import pack

root = Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser()
parser.add_argument('--internal-only',action='store_true',help='Update fallback UI glyphs without changing the installed external bank')
args=parser.parse_args()
font = Path('/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf')
clock = Path('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf')
# External text currently supports this UI/demo subset plus printable ASCII.
extra = '室外温度提示保持通风，留意室外空气变化分时数据数据分布'
chars = ''.join(sorted({c for p in (root/'src').glob('*.c') for c in p.read_text() + extra if ord(c) > 255}))
common = ''.join(bytes([a,b]).decode('gb2312',errors='ignore') for a in range(0xb0,0xf8) for b in range(0xa1,0xff))
(root/'src/fonts/ui-characters.txt').write_text(chars+'\n')
for name, size in ([] if args.internal_only else [('metric_font',64),('clock_font',144),('clock72',72)]):
    subprocess.run(['npx','--offline','--yes','lv_font_conv@1.5.3','--font',str(clock),
                    '--range','0x20,0x25,0x2b-0x3a,0x4d,0x6b,0xb0','--size',str(size),
                    '--bpp','1','--format','lvgl','--lv-font-name',name,
                    '--output',str(root/f'src/fonts/{name}.c')],check=True)
# Only common UI and weather text remain in internal Flash. Extended Chinese
# uses a native 32px grayscale bank, validated and cached before rendering.
chars += '晴多云阴雾霾小中大暴阵雷雨夹雪冻冰沙尘浮扬风微东北南西白天夜间未同步故障维护升级恢复电池充电连接网络资源缺失'
subprocess.run(['npx','--offline','--yes','lv_font_conv@1.5.3','--font',str(clock),
                '--range','0x20-0x7e,0xb0,0xb5,0xb3,0xb7','--font',str(font),
                '--size','16','--bpp','1','--format','lvgl','--symbols',chars,
                '--lv-font-name','zh16','--output',str(root/'src/fonts/zh16.c')],check=True)
if not args.internal_only:
 with tempfile.TemporaryDirectory(prefix='bry88-font-') as tmp:
    full=Path(tmp)/'zh32_full.c'
    subprocess.run(['npx','--offline','--yes','lv_font_conv@1.5.3','--font',str(clock),
                    '--range','0x20-0x7e,0xb0,0xb5,0xb3,0xb7','--font',str(font),
                    '--size','32','--bpp','2','--no-compress','--format','lvgl','--symbols',chars+common,
                    '--lv-font-name','zh32_full','--output',str(full)],check=True)
    bank=root/'resources/zh32.iotfnt';bank.parent.mkdir(exist_ok=True);bank.write_bytes(pack(full))
lock = json.loads((root/'dependencies.lock.json').read_text())
lock['font_generator'] = {'package':'lv_font_conv', 'version':'1.5.3',
                         'sources':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (font, clock)}}
lock['external_font'] = {'file':'resources/zh32.iotfnt','sha256':hashlib.sha256((root/'resources/zh32.iotfnt').read_bytes()).hexdigest(),'size':32,'bpp':2}
lock['fonts'] = {str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted((root/'src/fonts').glob('*.c'))}
(root/'dependencies.lock.json').write_text(json.dumps(lock, indent=2)+'\n')
