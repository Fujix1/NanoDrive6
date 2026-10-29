"""Native regression tests for the shared NDSIF protocol core (Windows/MSVC)."""
import binascii
import ctypes
import os
from pathlib import Path
import struct
import subprocess
import unittest

root = Path(__file__).resolve().parents[2]
out = root / '.pio' / 'ndsif_core_test'
out.mkdir(parents=True, exist_ok=True)
compiler = Path('C:/Program Files (x86)/Microsoft Visual Studio/2019/BuildTools/VC/Tools/MSVC/14.29.30133/bin/Hostx64/x64/cl.exe')
sdk = 'C:/Program Files (x86)/Windows Kits/10/Include/10.0.26100.0/ucrt'
includes = ['/I' + str(root / 'lib/ndsif'), '/I' + str(compiler.parents[3] / 'include'), '/I' + sdk]
cmd = [str(compiler), '/nologo', '/std:c++14', '/Od', '/GS-', '/Zl', '/LD', '/DNDEBUG'] + includes + [
    str(root / 'lib/ndsif/ndsif.cpp'), str(root / 'tools/ndsif_core_test/bridge.cpp'),
    '/Fo' + str(out) + '/', '/link', '/NOENTRY', '/NODEFAULTLIB', '/OUT:' + str(out / 'core.dll'), '/IMPLIB:' + str(out / 'core.lib')]
env = os.environ.copy()
env['PATH'] = str(compiler.parent) + os.pathsep + env['PATH']
subprocess.run(cmd, cwd=root, env=env, check=True)
dll = ctypes.CDLL(str(out / 'core.dll'))
dll.feed.argtypes = [ctypes.c_char_p, ctypes.c_uint]


def encode(raw):
    out = bytearray([0]); pos = 0; code = 1
    for value in raw:
        if value == 0:
            out[pos] = code; pos = len(out); out.append(0); code = 1
        else:
            out.append(value); code += 1
            if code == 255:
                out[pos] = code; pos = len(out); out.append(0); code = 1
    out[pos] = code
    return b'\0' + out + b'\0'


def send(op, payload=b'', kind=0):
    raw = b'ND' + bytes([1, kind, op]) + struct.pack('<HH', 123, len(payload)) + payload
    packet = encode(raw + struct.pack('<H', binascii.crc_hqx(raw, 0xffff)))
    dll.feed(packet, len(packet))
    output = ctypes.create_string_buffer(512)
    n = dll.output(output)
    data = output.raw[:n]
    if not data: return None, data
    frames = [f for f in data.split(b'\0') if f]
    assert len(frames) == 1
    encoded = frames[0]; decoded = bytearray(); pos = 0
    while pos < len(encoded):
        code = encoded[pos]; pos += 1
        decoded.extend(encoded[pos:pos + code - 1]); pos += code - 1
        if code != 255 and pos < len(encoded): decoded.append(0)
    assert decoded[:5] == b'ND\1\2' + bytes([op])
    assert binascii.crc_hqx(decoded[:-2], 0xffff) == struct.unpack('<H', decoded[-2:])[0]
    return bytes(decoded[9:-2]), data


class SharedCoreTests(unittest.TestCase):
    def test_nd6_byte_bursts_and_extension(self):
        dll.init(6)
        self.assertIsNone(send(0x50, bytes([0, 0, 0x90, 0x1f, 0x9f]))[0])
        self.assertEqual([dll.metric(i) for i in range(3)], [3, 0x90, 0x9f])
        self.assertIsNone(send(0x30, bytes([1, 0, 0xbf]))[0])
        self.assertEqual([dll.metric(i) for i in range(3)], [1, 0xbf, 0xbf])
        self.assertEqual(send(0x18, kind=1)[0], b'\0\1')
        caps, raw = send(5, b'\0\0', 1)
        self.assertEqual(caps[9] & 2, 2)
        self.assertEqual(raw[-2:], b'\0\0')
        self.assertEqual(len(raw) % 64, 1)

    def test_nd8_legacy_register_and_adpcm(self):
        dll.init(8)
        info, _ = send(2, kind=1)
        self.assertIn(b'NanoDrive 8', info)
        self.assertIsNone(send(0x54, b'\0\0\x20\xc7\x08\x78')[0])
        self.assertEqual([dll.metric(i) for i in range(3)], [2, 8, 0x78])
        self.assertIsNone(send(0x10, b'\xb7\0\x12')[0])
        self.assertEqual(send(0x13, b'\xb7\0', 1)[0], b'\0\xa5')
        self.assertEqual(dll.metric(3), 2)
        self.assertEqual(send(0x18, kind=1)[0], b'\1')
        self.assertEqual(send(5, b'\0\0', 1)[0][9] & 2, 0)

if __name__ == '__main__': unittest.main(verbosity=2)
