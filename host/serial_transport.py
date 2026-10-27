"""Windows COM transport using the Win32 API. No third-party Python packages."""
import ctypes as C
from ctypes import wintypes as W

import queue
import re
import threading


class DCB(C.Structure):
    _fields_ = [("length", W.DWORD), ("baud", W.DWORD), ("flags", W.DWORD),
                ("reserved", W.WORD), ("xonlim", W.WORD), ("xofflim", W.WORD),
                ("bytesize", C.c_ubyte), ("parity", C.c_ubyte), ("stopbits", C.c_ubyte),
                ("xonchar", C.c_char), ("xoffchar", C.c_char), ("errorchar", C.c_char),
                ("eofchar", C.c_char), ("eventchar", C.c_char), ("reserved1", W.WORD)]


class Timeouts(C.Structure):
    _fields_ = [("interval", W.DWORD), ("read_mult", W.DWORD), ("read_const", W.DWORD),
                ("write_mult", W.DWORD), ("write_const", W.DWORD)]


class Serial:
    def __init__(self, port):
        if not re.fullmatch(r"COM\d+", port.upper()):
            raise ValueError("串口名格式为 COM3 或 COM12")
        self.api = C.WinDLL("kernel32", use_last_error=True)
        signatures = {
            "CreateFileW": ([W.LPCWSTR, W.DWORD, W.DWORD, W.LPVOID, W.DWORD, W.DWORD, W.HANDLE], W.HANDLE),
            "GetCommState": ([W.HANDLE, C.POINTER(DCB)], W.BOOL),
            "SetCommState": ([W.HANDLE, C.POINTER(DCB)], W.BOOL),
            "SetCommTimeouts": ([W.HANDLE, C.POINTER(Timeouts)], W.BOOL),
            "ReadFile": ([W.HANDLE, W.LPVOID, W.DWORD, C.POINTER(W.DWORD), W.LPVOID], W.BOOL),
            "WriteFile": ([W.HANDLE, W.LPCVOID, W.DWORD, C.POINTER(W.DWORD), W.LPVOID], W.BOOL),
            "CloseHandle": ([W.HANDLE], W.BOOL),
        }
        for name, (args, result) in signatures.items():
            fn = getattr(self.api, name)
            fn.argtypes, fn.restype = args, result
        self.handle = self.api.CreateFileW("\\\\.\\" + port.upper(), 0xC0000000, 0, None, 3, 0, None)
        if self.handle == W.HANDLE(-1).value:
            raise C.WinError(C.get_last_error())
        try:
            dcb = DCB()
            dcb.length = C.sizeof(DCB)
            self._check(self.api.GetCommState(self.handle, C.byref(dcb)))
            dcb.baud, dcb.flags, dcb.bytesize, dcb.parity, dcb.stopbits = 115200, 1, 8, 0, 0
            self._check(self.api.SetCommState(self.handle, C.byref(dcb)))
            timeout = Timeouts(20, 0, 80, 0, 500)
            self._check(self.api.SetCommTimeouts(self.handle, C.byref(timeout)))
        except Exception:
            self.api.CloseHandle(self.handle)
            raise
        self.lines = queue.Queue(maxsize=1000)
        self.stop = threading.Event()
        self.thread = threading.Thread(target=self._read, daemon=True)
        self.thread.start()

    @staticmethod
    def _check(result):
        if not result:
            raise C.WinError(C.get_last_error())

    def _read(self):
        pending = b""
        while not self.stop.is_set():
            buf, count = C.create_string_buffer(512), W.DWORD()
            if not self.api.ReadFile(self.handle, buf, 512, C.byref(count), None):
                try:
                    self.lines.put_nowait({"error": str(C.WinError(C.get_last_error()))})
                except queue.Full:
                    pass
                return
            pending += buf.raw[:count.value]
            while b"\n" in pending:
                line, pending = pending.split(b"\n", 1)
                try:
                    self.lines.put_nowait(line.decode("ascii").rstrip("\r"))
                except (ValueError, UnicodeError, queue.Full):
                    pass
            if len(pending) > 8192:
                pending = b""

    def send(self, command):
        data, count = (command + "\n").encode("ascii"), W.DWORD()
        self._check(self.api.WriteFile(self.handle, data, len(data), C.byref(count), None))
        if count.value != len(data):
            raise RuntimeError("串口写入超时")

    def close(self):
        self.stop.set()
        self.thread.join(timeout=1)
        self.api.CloseHandle(self.handle)
