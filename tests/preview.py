#!/usr/bin/env python3
"""Real local HTTP requests and a decoded PNG dimension/CRC check."""
import importlib.util
from http.server import HTTPServer
from pathlib import Path
import struct
import threading
import unittest
from urllib.error import HTTPError
from urllib.request import urlopen
import zlib

root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('preview', root/'tools/preview.py')
preview=importlib.util.module_from_spec(spec)
spec.loader.exec_module(preview)

class PreviewTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server=HTTPServer(('127.0.0.1',0),preview.Handler)
        cls.thread=threading.Thread(target=cls.server.serve_forever,daemon=True)
        cls.thread.start()
        cls.url=f'http://127.0.0.1:{cls.server.server_port}'
    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown();cls.thread.join();cls.server.server_close()
    def get(self,path):
        with urlopen(self.url+path,timeout=10) as response:
            return response.read()
    def test_all_renderings(self):
        self.assertIn(b'id="screen"',self.get('/'))
        for demo in (0,1):
            for page in range(5):
                png=self.get(f'/screen.png?page={page}&demo={demo}')
                self.assertEqual(png[:8],b'\x89PNG\r\n\x1a\n')
                pos=8;image=b''
                while pos<len(png):
                    length=struct.unpack('>I',png[pos:pos+4])[0]
                    kind=png[pos+4:pos+8];data=png[pos+8:pos+8+length]
                    crc=struct.unpack('>I',png[pos+8+length:pos+12+length])[0]
                    self.assertEqual(crc,zlib.crc32(kind+data)&0xffffffff)
                    if kind==b'IHDR':self.assertEqual(struct.unpack('>II',data[:8]),(480,854))
                    if kind==b'IDAT':image+=data
                    pos+=12+length
                self.assertEqual(len(zlib.decompress(image)),854*(480*3+1))
        for widget in range(5):self.assertTrue(self.get(f'/screen.png?page=3&demo=1&widget={widget}'))
    def test_errors(self):
        for path,code in (('/missing',404),('/screen.png?page=no',400)):
            with self.assertRaises(HTTPError) as error:self.get(path)
            self.assertEqual(error.exception.code,code)

if __name__=='__main__':unittest.main()
