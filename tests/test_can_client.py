import sys
import unittest
from pathlib import Path
from collections import deque
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "host"))
from can_client import DiagnosticClient, command, crc8


def message(**kwargs):
    return SimpleNamespace(is_remote_frame=False, is_error_frame=False, is_fd=False, **kwargs)


class FakeBus:
    def __init__(self, replies):
        self.replies = deque(message(arbitration_id=0x708, data=p, is_extended_id=False) for p in replies)
        self.sent = []

    def recv(self, timeout):
        if timeout == 0:
            return None
        if not self.replies:
            raise TimeoutError("No fake replies")
        return self.replies.popleft()

    def send(self, msg, timeout):
        self.sent.append(bytes(msg.data))


class Tests(unittest.TestCase):
    def client(self, replies):
        bus = FakeBus(replies)
        return bus, DiagnosticClient(bus, message, sleep=lambda _: None)

    def test_crc_and_command(self):
        self.assertEqual(crc8(b"123456789"), 0x4B)
        self.assertEqual(command(0, 450, True).hex(), "010001c201000034")
        for count in range(256):
            p = command(count, 800, False)
            self.assertEqual(p[7], crc8(p[:7]))
        with self.assertRaises(ValueError):
            command(256)

    def test_single(self):
        bus, client = self.client([b"\x02\x50\x03"])
        self.assertEqual(client.uds(b"\x10\x03"), b"\x50\x03")
        self.assertEqual(bus.sent[0], b"\x02\x10\x03".ljust(8, b"\0"))

    def test_large_response_blocks(self):
        data = bytes(range(196))
        frames = [bytes((0x10, len(data))) + data[:6]]
        pos, sn = 6, 1
        while pos < len(data):
            frames.append(bytes((0x20 | sn,)) + data[pos:pos + 7])
            pos += 7
            sn = (sn + 1) & 15
        bus, client = self.client(frames)
        self.assertEqual(client.uds(b"\x22\xF1\xA2"), data)
        self.assertEqual(sum(p[0] == 0x30 for p in bus.sent), 4)

    def test_bad_sequence(self):
        _, client = self.client([b"\x10\x08abcdef", b"\x22gh"])
        with self.assertRaises(ValueError):
            client.uds(b"\x22\xF1\x90")

    def test_bad_single(self):
        _, client = self.client([b"\x07x"])
        with self.assertRaises(ValueError):
            client.uds(b"\x22\xF1\x90")

    def test_request_multiframe(self):
        bus, client = self.client([b"\x30\x00\x00", b"\x03\x7F\x22\x13"])
        self.assertEqual(client.uds(bytes(range(30))), b"\x7F\x22\x13")
        self.assertEqual(bus.sent[0][:2], b"\x10\x1e")
        self.assertEqual([p[0] for p in bus.sent[1:]], [0x21, 0x22, 0x23, 0x24])

    def test_wait_limit(self):
        _, client = self.client([b"\x31\x00\x00"] * 4)
        with self.assertRaises(TimeoutError):
            client.uds(bytes(range(10)))

    def test_oversize(self):
        bus, client = self.client([b"\x11\x01abcdef"])
        with self.assertRaises(ValueError):
            client.uds(b"\x22\xF1\x90")
        self.assertEqual(bus.sent[-1][0], 0x32)


if __name__ == "__main__":
    unittest.main()
