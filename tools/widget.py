#!/usr/bin/env python3
"""Encode the bounded screen widget MessagePack contract without dependencies.

This outputs a UART development frame, not an unauthenticated network API.
Use only after a successful screen_caps exchange with the same session.
"""
import argparse
import base64
import json
import math
import struct
import zlib


def pack(value):
    if isinstance(value, int):
        if not 0 <= value <= 0xffffffff:
            raise ValueError('unsigned integer out of range')
        if value < 128:
            return bytes([value])
        return b'\xce' + struct.pack('>I', value)
    if isinstance(value, float):
        if not math.isfinite(value) or abs(value) > 1000000:
            raise ValueError('nonfinite or excessive reading')
        return b'\xca' + struct.pack('>f', value)
    if isinstance(value, str):
        data = value.encode('utf-8')
        if len(data) > 255 or any(ord(c) < 32 or ord(c) == 127 for c in value):
            raise ValueError('invalid string')
        return (bytes([0xa0 + len(data)]) if len(data) < 32 else bytes([0xd9, len(data)])) + data
    if isinstance(value, list):
        if len(value) > 16:
            raise ValueError('array too large')
        return (bytes([0x90 + len(value)]) if len(value) < 16 else b'\xdc\x00\x10') + b''.join(map(pack, value))
    raise TypeError(type(value))


def encode(slot, kind, source, ttl, title, unit='', text='', values=()):
    if slot not in range(2) or kind not in range(5) or source not in range(3) or not 1 <= ttl <= 86400:
        raise ValueError('invalid widget metadata')
    if not title or len(title.encode()) > 48 or len(unit.encode()) > 16 or len(text.encode()) > 96:
        raise ValueError('string exceeds widget limit')
    if (kind == 0 and len(values) != 1) or (kind == 1 and values) or (kind >= 2 and not 2 <= len(values) <= 16):
        raise ValueError('invalid number of values')
    if kind == 4 and (len(values) > 4 or any(v < 0 for v in values) or sum(values) <= 0):
        raise ValueError('invalid pie weights')
    return pack([1, slot, kind, source, ttl, title, unit, text, [float(v) for v in values]])


def frame(payload, session, sequence):
    if not 0 < session <= 0xffffffff or not 0 < sequence <= 0xffffffff or not 1 <= len(payload) <= 768:
        raise ValueError('invalid frame metadata')
    return f'screen_ext 1 {session:08x} {sequence:08x} {zlib.crc32(payload):08x} '.encode() + base64.b64encode(payload) + b'\r\n'


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('widget', help='JSON file containing encode() keyword arguments')
    parser.add_argument('--session', type=lambda x: int(x, 0), required=True)
    parser.add_argument('--sequence', type=int, required=True)
    args = parser.parse_args()
    with open(args.widget, encoding='utf-8') as f:
        payload = encode(**json.load(f))
    import sys
    sys.stdout.buffer.write(frame(payload, args.session, args.sequence))
