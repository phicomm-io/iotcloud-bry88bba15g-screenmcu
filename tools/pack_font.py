#!/usr/bin/env python3
"""Pack uncompressed lv_font_conv 1.5.3 C output into checked IOTFNT02.

No re-rasterization: glyph metrics and 2bpp coverage are retained exactly.
"""
from pathlib import Path
import re,struct,zlib,argparse

def pack(source):
 s=source.read_text();bitmap=s.split('glyph_bitmap[] = {',1)[1].split('};',1)[0]
 codes=[int(cp,16) for cp in re.findall(r'/\* U\+([0-9A-Fa-f]+) ',bitmap)]
 raw=bytes(int(h,16) for h in re.findall(r'0x([0-9a-fA-F]+)',re.sub(r'/\*.*?\*/','',bitmap,flags=re.S)))
 desc=s.split('glyph_dsc[] = {',1)[1].split('};',1)[0]
 fields=('bitmap_index','adv_w','box_w','box_h','ofs_x','ofs_y')
 ds=[tuple(int(re.search(r'\.'+f+r' = (-?\d+)',line)[1]) for f in fields) for line in desc.splitlines() if '.bitmap_index' in line][1:]
 assert len(ds)==len(codes) and codes==sorted(set(codes)) and len(codes)<=8192
 assert re.search(r'\.bitmap_format = 0',s) and re.search(r'\.bpp = 2',s)
 pixels=bytearray();entries=bytearray();start=32+24*len(codes)
 for cp,(at,advance,w,h,x,y) in zip(codes,ds):
  n=(w*h*2+7)//8;assert at+n<=len(raw)
  entries.extend(struct.pack('<IIHHHHhhI',cp,start+len(pixels),n,(advance+8)//16,w,h,x,y,0));pixels.extend(raw[at:at+n])
 payload=entries+pixels;header=bytearray(32);header[:8]=b'IOTFNT02'
 struct.pack_into('<IIHH',header,8,len(payload)+32,zlib.crc32(payload),len(codes),24)
 header[20]=36;header[21]=2;header[22]=2;header[23]=32
 struct.pack_into('<I',header,28,zlib.crc32(header[:28]))
 assert len(header+payload)<=0x1fe000
 return bytes(header+payload)
if __name__=='__main__':
 a=argparse.ArgumentParser();a.add_argument('source',type=Path);a.add_argument('output',type=Path);o=a.parse_args();data=pack(o.source);o.output.parent.mkdir(parents=True,exist_ok=True);o.output.write_bytes(data);print(f'{o.output}: {len(data)} bytes, {struct.unpack_from("<H",data,16)[0]} glyphs, native 32px 2bpp')
