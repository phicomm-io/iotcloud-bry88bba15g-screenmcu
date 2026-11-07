#!/usr/bin/env python3
"""Derive board constants from the reviewed factory app; no binary embedded."""
import argparse,hashlib,json,re
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('analysis',type=Path);a=p.parse_args();r=a.analysis
out=Path(__file__).resolve().parents[1]/'platform/swm34s'
regs={};ports=[];words=[]
def value(x):
 x=x.strip();return int(x[1:],0) if x.startswith('#') else regs.get(x)
for name in ['lcd-sdram-pin-init-complete.txt','panel-init-complete.txt']:
 regs={}
 for line in (r/'evidence/mcu'/name).read_text().splitlines():
  m=re.match(r'([0-9a-f]+):\s+(\S+)\s+([^;]+)',line)
  if not m:continue
  addr=int(m[1],16);op=m[2].split('.')[0];args=[v.strip() for v in m[3].strip().split(',')]
  if op in ('mov','movs','movw') and len(args)==2:regs[args[0]]=value(args[1])
  elif op=='movt' and len(args)==2:regs[args[0]]=(regs.get(args[0],0) or 0)&65535|int(args[1][1:],0)<<16
  elif op in ('add','adds','sub','subs') and len(args) in (2,3):
   x=value(args[-2]) if len(args)==3 else regs.get(args[0]);y=value(args[-1]);regs[args[0]]=None if x is None or y is None else x+y if op.startswith('add') else x-y
  elif op=='bl':
   target=int(args[0][1:],0)
   if name.startswith('lcd') and target==0x1a488:ports.append([regs.get('r'+str(i)) for i in range(4)])
   if name.startswith('panel') and target==0x4bc6c:words.append((regs['r0']<<8)|regs['r1'])
# First 20 are LCD, remaining 39 SDRAM. Reject unknown tracking.
assert len(ports)==59 and len(words)==207 and all(None not in v for v in ports) and None not in words,(len(ports),len(words))
assert all(0<=v<=0x1ff for v in words)
contract=json.loads((r/'evidence/mcu/board-contract.json').read_text())
assert words==[(w['args'][0]<<8)|w['args'][1] for w in contract['panel_words']]
assert words[:6]==[0xff,0x177,0x101,0x100,0x100,0x113]
text='/* Derived factory board constants; see board-evidence.json. */\n'
text+='static const struct {uint32_t port;uint8_t pin,function,input;} board_mux[] = {\n'
text+=''.join('{0x%08xu,%d,%d,%d},\n'%tuple(v) for v in ports)+'};\n'
text+='static const uint16_t panel_words[] = {\n'+','.join('0x%03x'%v for v in words)+'\n};\n'
(out/'board_tables.h').write_text(text)
binary=r/'downloads/SENSOR3_MCU-SV.04.00.04.00.bin'
(out/'board-evidence.json').write_text(json.dumps({'factory_app_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'port_calls':len(ports),'lcd_calls':20,'sdram_calls':39,'panel_words':len(words),'source_ranges':['0x4a11c..0x4a496','0x4b1cc..0x4bc66'],'warning':'Bootloader and live version SV.04.00.01.00 not recovered'},indent=2)+'\n')
print('Recovered',len(ports),'mux calls and',len(words),'panel words')
