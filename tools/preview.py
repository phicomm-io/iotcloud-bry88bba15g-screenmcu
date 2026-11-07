#!/usr/bin/env python3
"""Same LVGL renderer as MCU UI. Local-only interactive design preview."""
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
from urllib.parse import urlparse, parse_qs
import struct, subprocess, zlib, argparse
ROOT=Path(__file__).resolve().parents[1]
def png(page,demo,widget=4):
    raw=subprocess.check_output([str(ROOT/'build/screen_render'),str(page),str(int(demo)),str(widget)])
    header=b'P6\n480 854\n255\n'
    if not raw.startswith(header) or len(raw)!=len(header)+480*854*3: raise ValueError('Invalid render')
    rgb=raw[len(header):]
    def chunk(k,p):return struct.pack('>I',len(p))+k+p+struct.pack('>I',zlib.crc32(k+p)&0xffffffff)
    scan=b''.join(b'\0'+rgb[y*1440:(y+1)*1440] for y in range(854))
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',480,854,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(scan))+chunk(b'IEND',b'')
class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        url=urlparse(self.path)
        if url.path=='/screen.png':
            q=parse_qs(url.query)
            try: body=png(int(q.get('page',['0'])[0])%5,q.get('demo',['0'])[0]=='1',int(q.get('widget',['4'])[0])%5)
            except (ValueError,subprocess.CalledProcessError):self.send_error(400);return
            kind='image/png'
        elif url.path=='/':
            body=(ROOT/'simulator/index.html').read_bytes();kind='text/html; charset=utf-8'
        else:self.send_error(404);return
        self.send_response(200);self.send_header('Content-Type',kind);self.send_header('Cache-Control','no-store');self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--export',action='store_true');p.add_argument('--port',type=int,default=8874);a=p.parse_args()
    if a.export:
        (ROOT/'artifacts').mkdir(exist_ok=True)
        for demo in (False,True):
            for page in range(5):(ROOT/'artifacts'/f'{"demo" if demo else "offline"}-{page}.png').write_bytes(png(page,demo))
        for widget in range(5):
            (ROOT/'artifacts'/f'widget-{widget}.png').write_bytes(png(3,True,widget))
    else:HTTPServer(('127.0.0.1',a.port),Handler).serve_forever()
