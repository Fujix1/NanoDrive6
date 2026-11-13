"""Native Windows tests for the live DAC scheduler and shared NDSIF features."""
import binascii
import _ctypes
import ctypes
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(r'C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Tools\MSVC\14.29.30133\bin\Hostx64\x64\cl.exe')
SDK = r'C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\ucrt'

def ordered_record(wait, page, reg, value):
    if page == 0 and reg == 0x2a:
        return bytes((wait, value)) if wait <= 0xfd else b'\xfe' + struct.pack('<IB', wait, value)
    return b'\xff' + struct.pack('<IBBB', wait, page, reg, value)

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

def decode(data):
    out = bytearray(); i = 0
    while i < len(data):
        code = data[i]; i += 1
        out.extend(data[i:i + code - 1]); i += code - 1
        if code < 255 and i < len(data): out.append(0)
    return bytes(out)

class LiveNativeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='nd6_live_test_')
        target = Path(cls.temp.name)
        includes = ['/I' + str(ROOT / 'lib/ndsif'), '/I' + str(COMPILER.parents[3] / 'include'), '/I' + SDK]
        cmd = [str(COMPILER), '/nologo', '/std:c++14', '/Od', '/GS-', '/Zl', '/LD', '/DNDEBUG'] + includes + [
            str(ROOT / 'lib/ndsif/ndsif.cpp'), str(ROOT / 'tools/live_dac_test/live_dac_native.cpp'), '/Fo' + str(target) + '/',
            '/link', '/NOENTRY', '/NODEFAULTLIB', '/OUT:' + str(target / 'live.dll'), '/IMPLIB:' + str(target / 'live.lib')]
        env = os.environ.copy(); env['PATH'] = str(COMPILER.parent) + os.pathsep + env['PATH']
        subprocess.run(cmd, cwd=target, env=env, check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        cls.dll = ctypes.CDLL(str(target / 'live.dll'))
        cls.dll.live_push.argtypes = [ctypes.c_char_p, ctypes.c_uint]
        cls.dll.live_next.argtypes = [ctypes.c_uint64, ctypes.POINTER(ctypes.c_uint8)]
        cls.dll.live_written.argtypes = [ctypes.c_uint64]
        cls.dll.live_written_generation.argtypes = [ctypes.c_uint64, ctypes.c_uint]
        cls.dll.live_diagnostics.argtypes = [ctypes.c_void_p]
        cls.dll.ordered_append.argtypes = [ctypes.c_char_p, ctypes.c_uint]
        cls.dll.ordered_next.argtypes = [ctypes.c_uint64, ctypes.c_void_p]
        cls.dll.ordered_diagnostics.argtypes = [ctypes.c_void_p]
        cls.dll.ordered_status.argtypes = [ctypes.c_void_p]
        cls.dll.protocol_feed.argtypes = [ctypes.c_char_p, ctypes.c_uint]

    def setUp(self):
        self.dll.live_reset()
        self.dll.ordered_reset()

    def push(self, *records):
        data = b''.join(struct.pack('<IB', wait, value) for wait, value in records)
        return self.dll.live_push(data, len(data))

    def next(self, now):
        out = ctypes.c_uint8()
        return out.value if self.dll.live_next(now, ctypes.byref(out)) else None

    def test_wait_zero_late_empty_resume_and_repeated_values(self):
        self.assertTrue(self.push((0, 0x80), (4, 0x84), (0, 0x84)))
        self.assertEqual(self.next(1000), 0x80); self.dll.live_written(1000)
        self.assertIsNone(self.next(1089))
        self.assertEqual(self.next(1090), 0x84); self.dll.live_written(1090)
        self.assertEqual(self.next(1090), 0x84); self.dll.live_written(1090)
        self.assertIsNone(self.next(5000000))
        self.assertTrue(self.dll.live_enabled())
        self.assertTrue(self.push((4, 0x77)))
        self.assertEqual(self.next(5000000), 0x77)

    def test_ordered_timeline_and_backpressure(self):
        records = [(0, 0, 0x28, 0xf0), (0, 1, 0xb4, 0xc0),
                   (2, 0, 0x2a, 0x80), (3, 0, 0x2a, 0x84),
                   (2, 0, 0x2b, 0), (2, 0, 0x28, 0)]
        frame = b'\x03' + struct.pack('<I', 0) + b''.join(ordered_record(*r) for r in records)
        self.assertEqual(self.dll.ordered_append(frame, len(frame)), 0)
        out = ctypes.create_string_buffer(3)
        def next_event(now):
            return tuple(out.raw) if self.dll.ordered_next(now, out) else None
        self.assertEqual(next_event(1000), (0, 0x28, 0xf0))
        self.assertEqual(next_event(1000), (1, 0xb4, 0xc0))
        self.assertIsNone(next_event(1044))
        self.assertEqual(next_event(1045), (0, 0x2a, 0x80))
        self.assertIsNone(next_event(1112))
        self.assertEqual(next_event(1113), (0, 0x2a, 0x84))
        self.assertEqual(next_event(1158), (0, 0x2b, 0))
        self.assertEqual(next_event(1204), (0, 0x28, 0))
        self.assertEqual(self.dll.ordered_append(frame, len(frame)), 3)
        self.dll.ordered_reset()
        for seq in range(5):
            data = b'\x03' + struct.pack('<I', seq) + b''.join(ordered_record(0, 0, 0x2a, i) for i in range(100))
            self.assertEqual(self.dll.ordered_append(data, len(data)), 0)
        data = b'\x03' + struct.pack('<I', 5) + b''.join(ordered_record(0, 0, 0x2a, i) for i in range(12))
        self.assertEqual(self.dll.ordered_append(data, len(data)), 0)
        data = b'\x03' + struct.pack('<I', 6) + b''.join(ordered_record(0, 1, 0xb4, i) for i in range(13))
        self.assertEqual(self.dll.ordered_append(data, len(data)), 1)
        for _ in range(13): self.assertIsNotNone(next_event(2000))
        self.assertEqual(self.dll.ordered_append(data, len(data)), 0)

    def test_ordered_psg_is_interleaved_and_invalid_page_is_atomic(self):
        header = b'\x03' + struct.pack('<I', 0)
        records = [(0, 0, 0x28, 1), (2, 2, 0, 0x80), (3, 0, 0x2a, 0x88)]
        data = header + b''.join(ordered_record(*r) for r in records)
        self.assertEqual(self.dll.ordered_append(data, len(data)), 0)
        out = ctypes.create_string_buffer(3)
        for now, expected in [(1000, (0, 0x28, 1)), (1045, (2, 0, 0x80)), (1113, (0, 0x2a, 0x88))]:
            self.assertTrue(self.dll.ordered_next(now, out))
            self.assertEqual(tuple(out.raw), expected)
        bad = b'\x03' + struct.pack('<I', 1) + ordered_record(0, 2, 1, 0)
        self.assertEqual(self.dll.ordered_append(bad, len(bad)), 2)

    def test_ordered_corrects_small_lateness_and_rebases_after_empty(self):
        records = [(0, 0, 0x2a, 1), (3, 0, 0x28, 2), (3, 0, 0x2a, 3)]
        data = b'\x03' + struct.pack('<I', 0) + b''.join(ordered_record(*r) for r in records)
        self.assertEqual(self.dll.ordered_append(data, len(data)), 0)
        out = ctypes.create_string_buffer(3)
        self.assertTrue(self.dll.ordered_next(1000, out))
        self.assertFalse(self.dll.ordered_next(1067, out))
        self.assertTrue(self.dll.ordered_next(1071, out))  # 3 us late
        self.assertFalse(self.dll.ordered_next(1135, out))
        self.assertTrue(self.dll.ordered_next(1136, out))  # source timeline, not 1139
        data = b'\x03' + struct.pack('<I', 1) + ordered_record(3, 0, 0x28, 4)
        self.assertEqual(self.dll.ordered_append(data, len(data)), 0)
        self.assertTrue(self.dll.ordered_next(5000, out))  # long empty: immediate, no stale replay
        self.assertEqual(tuple(out.raw), (0, 0x28, 4))
        diag = ctypes.create_string_buffer(53)
        self.assertEqual(self.dll.ordered_diagnostics(diag), 53)
        version, queued, peak, accepted, executed, full, late, rebased, maxlate, first, last, ticks = struct.unpack('<BHH6I3Q', diag.raw)
        self.assertEqual((version, queued, peak, accepted, executed, full, late, rebased), (3, 0, 3, 4, 4, 0, 0, 1))
        self.assertEqual((first, last, ticks), (1000, 5000, 9))

    def test_compact_validation_is_atomic_and_forward_gap_recovers(self):
        header = lambda seq: b'\x03' + struct.pack('<I', seq)
        bad = header(0) + ordered_record(0, 0, 0x2a, 0x80) + b'\xfe\x01'
        self.assertEqual(self.dll.ordered_append(bad, len(bad)), 2)
        out = ctypes.create_string_buffer(3)
        self.assertFalse(self.dll.ordered_next(1000, out))
        first = header(0) + ordered_record(0, 0, 0x2a, 0x80)
        self.assertEqual(self.dll.ordered_append(first, len(first)), 0)
        later = header(2) + ordered_record(254, 0, 0x2a, 0x81) + ordered_record(3, 1, 0xb4, 0xc0)
        self.assertEqual(self.dll.ordered_append(later, len(later)), 0)
        stale = header(1) + ordered_record(0, 0, 0x2a, 0x82)
        self.assertEqual(self.dll.ordered_append(stale, len(stale)), 3)
        diag = ctypes.create_string_buffer(17)
        self.assertEqual(self.dll.ordered_status(diag), 17)
        self.assertEqual(struct.unpack('<BIIHHI', diag.raw), (3, 3, 3, 3, 509, 1))
        self.assertTrue(self.dll.ordered_next(1000, out)); self.assertEqual(tuple(out.raw), (0, 0x2a, 0x80))
        self.assertFalse(self.dll.ordered_next(6758, out))
        self.assertTrue(self.dll.ordered_next(6759, out)); self.assertEqual(tuple(out.raw), (0, 0x2a, 0x81))
        self.assertTrue(self.dll.ordered_next(6828, out)); self.assertEqual(tuple(out.raw), (1, 0xb4, 0xc0))

    def test_first_received_value_ignores_missing_predecessor(self):
        self.assertTrue(self.push((500, 0x66)))
        self.assertEqual(self.next(12345), 0x66)

    def test_write_processing_time_is_inside_the_sample_interval(self):
        # The caller reports the start time even if the chip operation and
        # display update complete 10 us later. 4 ticks alternate between
        # 90 and 91 us after fractional tick conversion.
        self.assertTrue(self.push(*([(0, 0x80)] + [(4, 0x81)] * 49)))
        starts = []
        now = 1000
        while len(starts) < 50:
            value = self.next(now)
            if value is None:
                now += 1
                continue
            starts.append(now)
            self.dll.live_written(now)
            now += 10  # Simulated chip I/O plus visual bookkeeping.
        self.assertEqual(starts[-1] - starts[0], (49 * 4 * 1000000) // 44100)

    def test_capacity_rejects_entire_burst_without_stopping(self):
        for _ in range(10): self.assertTrue(self.push(*[(0, 1)] * 50))
        self.assertTrue(self.push(*[(0, 2)] * 12))
        self.assertFalse(self.push((0, 3)))
        self.assertTrue(self.dll.live_enabled())
        for _ in range(500):
            self.assertEqual(self.next(0), 1); self.dll.live_written(0)
        for _ in range(12):
            self.assertEqual(self.next(0), 2); self.dll.live_written(0)
        self.assertIsNone(self.next(0))
        self.assertTrue(self.push((0, 4)))
        self.assertEqual(self.next(0), 4)

    def test_diagnostics_count_full_and_late_resume_without_fault(self):
        self.assertTrue(self.push((0, 0x80)))
        self.assertEqual(self.next(1000), 0x80); self.dll.live_written(1000)
        self.assertTrue(self.push((1, 0x81)))
        self.assertEqual(self.next(2000), 0x81); self.dll.live_written(2000)
        for _ in range(10): self.assertTrue(self.push(*[(0, 1)] * 50))
        self.assertTrue(self.push(*[(0, 2)] * 12))
        self.assertFalse(self.push((0, 3), (0, 4)))
        out = ctypes.create_string_buffer(29)
        self.assertEqual(self.dll.live_diagnostics(out), 29)
        version, queued, high, accepted, emitted, frames, dropped, late, max_us = struct.unpack('<BHH6I', out.raw)
        self.assertEqual((version, queued, high, accepted, emitted, frames, dropped, late, max_us),
                         (1, 512, 512, 514, 2, 1, 2, 1, 978))
        self.assertTrue(self.dll.live_enabled())
        self.dll.live_reset()
        self.dll.live_diagnostics(out)
        self.assertEqual(struct.unpack('<BHH6I', out.raw), (1, 0, 0, 0, 0, 0, 0, 0, 0))

    def test_late_completion_from_reset_session_does_not_change_new_timing(self):
        self.assertTrue(self.push((0, 1)))
        self.assertEqual(self.next(0), 1)
        generation = self.dll.live_generation()
        self.dll.live_reset()
        self.assertTrue(self.push((100, 2)))
        self.dll.live_written_generation(0, generation)
        self.assertEqual(self.next(10), 2)

    def test_core_control_feature_is_opt_in_and_reset_by_configure(self):
        def capability(features):
            self.dll.protocol_init(features)
            payload = struct.pack('<H', 0)
            raw = struct.pack('<2sBBBHH', b'ND', 1, 1, 5, 1, len(payload)) + payload
            raw += struct.pack('<H', binascii.crc_hqx(raw, 0xffff))
            request = encode(raw); self.dll.protocol_feed(request, len(request))
            out = ctypes.create_string_buffer(512)
            size = self.dll.protocol_output(out)
            response = decode(out.raw[:size].strip(b'\0'))
            return struct.unpack_from('<H', response, 9 + 9)[0]
        self.assertEqual(capability(4), 6)
        self.assertEqual(capability(0), 2)

    def test_wire_ordered_capabilities_and_events(self):
        self.dll.protocol_init(8)
        def send(command, payload, kind=1):
            raw = struct.pack('<2sBBBHH', b'ND', 1, kind, command, 9, len(payload)) + payload
            raw += struct.pack('<H', binascii.crc_hqx(raw, 0xffff))
            packet = encode(raw); self.dll.protocol_feed(packet, len(packet))
            out = ctypes.create_string_buffer(512); size = self.dll.protocol_output(out)
            return decode(out.raw[:size].strip(b'\0'))[9:-2] if size else None
        self.assertEqual(send(0x21, b''), struct.pack('<BBBIHB', 0, 3, 5, 44100, 512, 100))
        payload = b'\x03' + struct.pack('<I', 0) + ordered_record(0, 1, 0xb4, 0xc0)
        self.assertIsNone(send(0x22, payload, 0))
        self.assertEqual(send(0x23, b''), struct.pack('<BBIIHHI', 0, 3, 1, 1, 1, 511, 0))
        self.assertIsNone(send(0x22, payload, 0))  # replay is rejected
        self.assertEqual(send(0x23, b''), struct.pack('<BBIIHHI', 0, 3, 1, 1, 1, 511, 0))
        diag = send(0x20, b'N6D3')
        self.assertEqual((len(diag), diag[0], diag[1]), (54, 0, 3))
        out = ctypes.create_string_buffer(3)
        self.assertTrue(self.dll.ordered_next(0, out))
        self.assertEqual(tuple(out.raw), (1, 0xb4, 0xc0))

    def test_wire_capabilities_and_live_data(self):
        self.dll.protocol_init(4)
        def send(command, payload, kind):
            raw = struct.pack('<2sBBBHH', b'ND', 1, kind, command, 9, len(payload)) + payload
            raw += struct.pack('<H', binascii.crc_hqx(raw, 0xffff))
            packet = encode(raw); self.dll.protocol_feed(packet, len(packet))
            out = ctypes.create_string_buffer(512); size = self.dll.protocol_output(out)
            return decode(out.raw[:size].strip(b'\0')) if size else None
        response = send(0x1e, b'\0\0', 1)
        self.assertEqual(response[9:-2], struct.pack('<BBBIIBHB', 0, 2, 1, 44100, 512, 50, 0xffff, 1) + b'\x52\0')
        self.assertIsNone(send(0x1f, b'\x02\x52\0' + struct.pack('<IB', 0, 0x80), 0))
        self.assertEqual(self.next(1000), 0x80); self.dll.live_written(1000)
        self.assertIsNone(send(0x1f, b'\x02\x52\0' + struct.pack('<IB', 4, 0x84), 0))
        self.assertIsNone(self.next(1089)); self.assertEqual(self.next(1090), 0x84)
        self.dll.live_written(1090)
        diag = send(0x20, b'N6D1', 1)
        self.assertEqual(len(diag[9:-2]), 30)
        self.assertEqual(diag[9:11], b'\0\x01')
        self.assertEqual(send(0x20, b'bad!', 1)[9:-2], b'\x01')
        self.assertIsNone(send(0x1f, b'\x02\x52\x01' + struct.pack('<IB', 0, 0x55), 0))
        self.assertIsNone(self.next(5000))

    @classmethod
    def tearDownClass(cls):
        _ctypes.FreeLibrary(cls.dll._handle)
        cls.temp.cleanup()

if __name__ == '__main__':
    unittest.main()
