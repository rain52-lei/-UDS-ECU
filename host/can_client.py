"""ThermoGuard SDK: Classic CAN command and bounded ISO-TP diagnostic client."""
import argparse
import time


def crc8(data):
    crc = 0xFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = ((crc << 1) ^ (0x1D if crc & 0x80 else 0)) & 0xFF
    return crc ^ 0xFF


def command(counter, target=450, enable=False):
    if not 0 <= counter <= 255 or not 200 <= target <= 800:
        raise ValueError("counter=0..255; target=200..800 dC")
    data = bytes((1, counter, target >> 8, target & 255, int(bool(enable)), 0, 0))
    return data + bytes((crc8(data),))


class DiagnosticClient:
    def __init__(self, bus, message_factory, clock=time.monotonic, sleep=time.sleep):
        self.bus, self.message, self.clock, self.sleep = bus, message_factory, clock, sleep

    def send(self, ident, data):
        self.bus.send(self.message(arbitration_id=ident, data=bytes(data).ljust(8, b"\0"),
                                   is_extended_id=False), timeout=0.2)

    def receive(self, timeout=2.0):
        deadline = self.clock() + timeout
        while self.clock() < deadline:
            msg = self.bus.recv(timeout=max(0.0, deadline - self.clock()))
            if msg is not None and msg.arbitration_id == 0x708 and not any((
                    msg.is_extended_id, msg.is_remote_frame, msg.is_error_frame,
                    getattr(msg, "is_fd", False))):
                data = bytes(msg.data)
                if 1 <= len(data) <= 8:
                    return data
        raise TimeoutError("CAN ID 0x708 response timeout")

    def flow_control(self):
        waits = 0
        while True:
            frame = self.receive(1.0)
            if len(frame) < 3 or frame[0] >> 4 != 3:
                raise ValueError("Expected flow control")
            status = frame[0] & 15
            if status == 1:
                waits += 1
                if waits > 3:
                    raise TimeoutError("Too many flow-control WAIT frames")
                continue
            if status != 0:
                raise ValueError("Flow-control overflow or invalid status")
            st = frame[2]
            if st <= 0x7F:
                delay = st / 1000.0
            elif 0xF1 <= st <= 0xF9:
                delay = (st - 0xF0) / 10000.0
            else:
                raise ValueError("Reserved STmin")
            return frame[1], delay

    def uds(self, payload):
        payload = bytes(payload)
        if not 1 <= len(payload) <= 256:
            raise ValueError("ISO-TP payload must contain 1..256 bytes")
        # 有界清理旧响应，避免上一次会话残留被误认为本次回复。
        for _ in range(256):
            if self.bus.recv(timeout=0) is None:
                break
        if len(payload) <= 7:
            self.send(0x700, bytes((len(payload),)) + payload)
        else:
            self.send(0x700, bytes((0x10 | len(payload) >> 8, len(payload) & 255)) + payload[:6])
            pos, sn = 6, 1
            while pos < len(payload):
                block, delay = self.flow_control()
                sent = 0
                while pos < len(payload) and (block == 0 or sent < block):
                    self.sleep(delay + 0.001)
                    part = payload[pos:pos + 7]
                    self.send(0x700, bytes((0x20 | sn,)) + part)
                    pos += len(part)
                    sn = (sn + 1) & 15
                    sent += 1
        first = self.receive()
        if first[0] >> 4 == 0:
            size = first[0] & 15
            if not 1 <= size <= 7 or len(first) < size + 1:
                raise ValueError("Invalid single frame")
            return first[1:size + 1]
        if len(first) != 8 or first[0] >> 4 != 1:
            raise ValueError("Expected first frame")
        size = ((first[0] & 15) << 8) | first[1]
        if not 8 <= size <= 256:
            self.send(0x700, b"\x32\x00\x00")
            raise ValueError("Response exceeds buffer")
        data, sn, received = bytearray(first[2:]), 1, 0
        self.send(0x700, b"\x30\x08\x05")
        while len(data) < size:
            frame = self.receive(1.0)
            count = min(7, size - len(data))
            if len(frame) < count + 1 or frame[0] != (0x20 | sn):
                raise ValueError("Invalid consecutive-frame length or sequence")
            data.extend(frame[1:count + 1])
            sn = (sn + 1) & 15
            received += 1
            if received == 8 and len(data) < size:
                self.send(0x700, b"\x30\x08\x05")
                received = 0
        return bytes(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--interface", required=True)
    parser.add_argument("--channel", required=True)
    parser.add_argument("--uds", help="例如 '22 F1 90'")
    parser.add_argument("--heartbeat", type=float, help="按100ms周期发送控制命令的持续秒数")
    parser.add_argument("--target", type=int, default=450)
    parser.add_argument("--enable", action="store_true")
    parser.add_argument("--remote", action="store_true", help="先通过UDS请求进入远程模式，需要ECU已停机")
    args = parser.parse_args()
    if args.heartbeat is not None and args.heartbeat <= 0:
        parser.error("heartbeat must be positive")
    try:
        import can
    except ImportError:
        parser.error("USB-CAN功能需要python-can及适配器驱动，见host/requirements-can.txt")
    channel = int(args.channel) if args.channel.isdigit() else args.channel
    with can.Bus(interface=args.interface, channel=channel, bitrate=500000, ignore_config=True) as bus:
        client = DiagnosticClient(bus, can.Message)
        if args.remote:
            for request, prefix in ((b"\x10\x03", b"\x50\x03"),
                                    (b"\x2E\xF1\xA4\x01", b"\x6E\xF1\xA4")):
                reply = client.uds(request)
                if not reply.startswith(prefix):
                    raise RuntimeError("Remote-mode request rejected: " + reply.hex(" "))
        if args.uds:
            print(client.uds(bytes.fromhex(args.uds)).hex(" ").upper())
        if args.heartbeat:
            deadline, counter = time.monotonic() + args.heartbeat, 0
            try:
                while time.monotonic() < deadline:
                    client.send(0x201, command(counter, args.target, args.enable))
                    counter = (counter + 1) & 255
                    time.sleep(0.1)
            finally:
                client.send(0x201, command(counter, args.target, False))


if __name__ == "__main__":
    main()
