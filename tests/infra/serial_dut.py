"""SerialDut — the hardware twin of EmuDut.

One pyserial port behind the pytest-embedded Dut surface (expect / expect_exact
/ pexpect_proc.before / write) that infra.expect_helper drives. A background
thread tees raw bytes to `logpath` and into an in-memory buffer; expect()
advances a forward-only consume cursor so `pexpect_proc.before` reflects only
bytes since the last match.

Unlike EmuDut there is no subprocess and no env_logger filtering — a real serial
line carries only firmware output. `reset()` pulses the board's auto-reset lines
on our own handle, so a test captures the full boot log without an esptool
re-open racing the port.
"""
import logging
import re
import threading
import time

import serial

from infra.emu_dut import ExpectMixin, _Pexpect
from infra.signatures import SETTLE_FATAL as _FATAL

_log = logging.getLogger("eh")  # streamed live by pytest log_cli


class SerialDut(ExpectMixin):
    def __init__(self, name, port, baud, logpath):
        self.name = name
        self._buf = ''
        self._cursor = 0
        self._lock = threading.Lock()
        self._fh = open(logpath, 'w')
        self.pexpect_proc = _Pexpect(self)
        self._alive = True
        _log.info("[serial] open %s %s @%d", name, port, baud)
        # rfc2217://host:port (remote serial) opens the same way as a /dev path.
        if '://' in str(port):
            self._ser = serial.serial_for_url(str(port), baudrate=baud, timeout=0.1)
        else:
            self._ser = serial.Serial(port, baud, timeout=0.1)
        # Idle the auto-reset lines (DTR→IO0 high, RTS→EN high) so merely opening
        # the port doesn't strand the chip in the ROM loader.
        self._set_lines(dtr=False, rts=False)
        threading.Thread(target=self._pump, daemon=True).start()

    def _set_lines(self, dtr, rts):
        try:
            self._ser.dtr = dtr
            self._ser.rts = rts
        except (OSError, ValueError):
            pass

    def _pump(self):
        while self._alive:
            try:
                data = self._ser.read(4096)
            except (OSError, serial.SerialException):
                break
            if not data:
                continue
            text = data.decode('utf-8', errors='replace')
            self._fh.write(text)
            self._fh.flush()
            with self._lock:
                self._buf += text

    def _unconsumed(self):
        with self._lock:
            return self._buf[self._cursor:]

    def write(self, s):
        _log.info("[cmd %s] %s", self.name, s.rstrip('\n'))
        self._ser.write((s if s.endswith('\n') else s + '\n').encode())

    def reset(self):
        """Classic ESP auto-reset (two-transistor): pulse EN low via RTS while
        IO0/DTR stays high, so the chip reboots into the app (not the ROM
        loader)."""
        self._set_lines(dtr=False, rts=True)   # EN low → hold in reset
        time.sleep(0.1)
        self._set_lines(dtr=False, rts=False)  # EN high → boot

    def fatal(self):
        """First late-crash signature in the full log, or None (post-success
        settle check)."""
        with self._lock:
            text = self._buf
        for p in _FATAL:
            if p in text:
                return p
        return None

    def stop(self):
        self._alive = False
        try:
            self._ser.close()
        except (OSError, serial.SerialException):
            pass
        self._fh.close()
