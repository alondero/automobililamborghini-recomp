#!/usr/bin/env python3
"""
ares_debug_client.py — GDB Remote Protocol client for ares DebugServer.

ares implements a GDB-compatible DebugServer over TCP (default port 9123).
This module provides a Python wrapper for the GDB remote protocol commands
needed to read RDP registers and RDRAM for instrumentation.

Protocol basics (GDB Remote Protocol):
  $mADDR,LEN#CHECKSUM   — read memory (hex)
  $MADDR,LEN,DATA#CS    — write memory (hex)
  $g#...                — read general registers
  $c#                   — continue (blocks until halt in stop-mode)
  $s#                   — step
  $?#                   — query current signal/PC
  $vCtrlC#..            — Ctrl+C to interrupt target
  $+                     — ACK
  $-                     — NAK

ares-specific register addresses (verified via RCP scan 2026-04-21):
  VI register base = 0xA4400000  (NOT 0xA43C0000 — ares VI space starts at 0xA4400000)
  VI_CONTROL      = 0xA4400000  (returns 0x301F)
  VI_DRAM_ADDR    = 0xA4400004
  VI_WIDTH        = 0xA4400008
  VI_V_INTR       = 0xA440000C
  VI_CURRENT      = 0xA4400010  (live vcounter: 0x1B4 = 436 scanlines — WORKING)
  VI_TIMING       = 0xA4400014
  VI_V_SYNC       = 0xA4400018
  Note: DPC/RDP registers at 0xA3C0 are NOT accessible via ares GDB (mapped differently)
"""

import socket
import time


class AresDebugClient:
    def __init__(self, host="127.0.0.1", port=9123, timeout=30.0):
        self.host = host
        self.port = port
        self.timeout = timeout
        self.sock = None
        self._connected = False

    def connect(self):
        """Connect to ares DebugServer TCP port."""
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.sock.settimeout(self.timeout)
        self.sock.connect((self.host, self.port))
        self._connected = True
        # TCPText requires first byte to be + (security handshake)
        self.sock.sendall(b"+")
        return self

    def disconnect(self):
        """Disconnect from DebugServer."""
        if self.sock:
            try:
                self.sock.close()
            except Exception:
                pass
            self.sock = None
            self._connected = False

    def _checksum(self, data: str) -> int:
        """Compute GDB packet checksum (mod 256)."""
        return sum(ord(c) for c in data) & 0xFF

    def _send_packet(self, packet: str):
        """Send a GDB packet, including checksum and terminator."""
        checksum = self._checksum(packet)
        full = f"${packet}#{checksum:02x}"
        self.sock.sendall(full.encode("ascii"))

    def _recv_packet(self) -> str:
        """Receive a GDB packet, stripping $ and #CHECKSUM."""
        buf = b""
        # Discard leading noise (+ from ACKs) until we hit $
        while True:
            c = self.sock.recv(1)
            if not c:
                raise EOFError("Connection closed")
            if c == b"$":
                break
        # Collect payload until #
        while True:
            c = self.sock.recv(1)
            if c == b"#":
                break
            buf += c
        checksum = self.sock.recv(2).decode("ascii")
        # ACK after full packet received
        self.sock.sendall(b"+")
        return buf.decode("ascii")

    def _recv_hex_packet(self) -> bytes:
        """Receive a GDB packet and decode hex data."""
        payload = self._recv_packet()
        # GDB error responses are exactly "Exx" where xx is a 2-digit error code.
        # Memory data can legitimately start with hex 'E' (e.g. byte 0xED), so
        # only treat 3-character "Exx" responses as errors.
        if len(payload) == 3 and payload[0] == "E":
            try:
                int(payload[1:], 16)
                raise RuntimeError(f"GDB error: {payload}")
            except ValueError:
                pass  # not an error code, fall through to hex parse
        if payload.startswith("S") or payload.startswith("T"):
            # Target halted with signal (e.g. "S05", "T05") — return zeros
            return bytes(4)
        if len(payload) % 2 != 0:
            raise RuntimeError(f"Uneven hex payload: {payload}")
        return bytes.fromhex(payload)

    def _drain(self, timeout=0.5):
        """Drain any pending response with short timeout."""
        old_timeout = self.sock.gettimeout()
        self.sock.settimeout(timeout)
        try:
            while True:
                self._recv_packet()
        except (socket.timeout, EOFError):
            pass
        finally:
            self.sock.settimeout(old_timeout)

    def read_mem(self, addr: int, size: int) -> bytes:
        """Read 'size' bytes from memory address.

        GDB protocol: `m<addr>,<size>` where BOTH addr and size are hex
        (no 0x prefix). Sending size in decimal caused ares to over-read,
        e.g. size=256 (decimal) was parsed as 0x256 = 598 bytes."""
        packet = f"m{addr:x},{size:x}"
        self._send_packet(packet)
        return self._recv_hex_packet()

    def read_mem_batch(self, addr: int, count: int) -> bytes:
        """Read count bytes starting at addr in one GDB packet (one round-trip)."""
        packet = f"m{addr:x},{count:x}"
        self._send_packet(packet)
        return self._recv_hex_packet()

    def read32(self, addr: int) -> int:
        """Read a 32-bit value from address (big-endian, matching MIPS/GDB protocol)."""
        data = self.read_mem(addr, 4)
        return int.from_bytes(data, "big")

    def write_mem(self, addr: int, data: bytes):
        """Write bytes to memory address."""
        hex_data = data.hex()
        packet = f"M{addr:x},{len(data)}:{hex_data}"
        self._send_packet(packet)
        self._recv_packet()  # Expect "OK"

    def halt(self):
        """Send vCtrlC to interrupt the target."""
        cs = sum(ord(c) for c in "vCtrlC") & 0xFF
        pkt = f"$vCtrlC#{cs:02x}".encode()
        self.sock.sendall(pkt)
        try:
            self._recv_packet()
        except Exception:
            pass

    def continue_(self, wait=True):
        """Continue emulation. If wait=True, blocks until target halts."""
        self._send_packet("c")
        if wait:
            resp = self._recv_packet()
            return resp
        return None

    def step(self):
        """Step one instruction."""
        self._send_packet("s")
        return self._recv_packet()

    def get_pc(self) -> int:
        """Query current PC via $? packet."""
        self._send_packet("?")
        resp = self._recv_packet()
        if resp.startswith("T"):
            parts = resp.split(";")
            for p in parts:
                if p.startswith("0f"):
                    return int(p[2:], 16)
        return 0

    def read_rdp_state(self) -> dict:
        """Read VI registers via ares GDB.

        VI register base in ares is 0xA4400000 (not 0xA43C0000 as on real N64 hardware).
        VI_CURRENT at 0xA4400010 returns live vcounter (dynamically updated).
        DPC/RDP registers at 0xA3C0 are not accessible via ares GDB.
        """
        return {
            "vi_control":    self.read32(0xA4400000),
            "vi_dram_addr": self.read32(0xA4400004),
            "vi_width":      self.read32(0xA4400008),
            "vi_v_intr":     self.read32(0xA440000C),
            "vi_current":    self.read32(0xA4400010),  # Live vcounter
            "vi_timing":     self.read32(0xA4400014),
            "vi_v_sync":    self.read32(0xA4400018),
        }

    def read_ram_word(self, addr: int) -> int:
        """Read a 32-bit word from RDRAM via GDB normalized address."""
        data = self.read_mem(addr, 4)
        return int.from_bytes(data, "little")

    def read_ram_block(self, addr: int, size: int) -> bytes:
        """Read a block of RDRAM memory via GDB normalized address."""
        return self.read_mem(addr, size)

    def get_stop_reason(self) -> str:
        """Query why target is halted."""
        self._send_packet("?")
        return self._recv_packet()

    # ------------------------------------------------------------------
    # Extended capabilities — verified working on ares-v147 (2026-05-18).
    # See .claude/skills/ares-debugger/SKILL.md for recipes.
    # ------------------------------------------------------------------

    def enable_no_ack_mode(self) -> bool:
        """Drop the +/- ACK round-trip. Roughly halves bytes per exchange.
        Returns True if accepted. Once enabled, do not change it back."""
        self._send_packet("QStartNoAckMode")
        return self._recv_packet().strip() == "OK"

    def read_register(self, idx: int) -> int:
        """Read a single register by GDB index via the 'p' packet.
        Returns the value as 64-bit unsigned. Caller masks to 32-bit if needed.

        Register indices are not formally documented by ares; empirically on
        ares-v147 mips64 stub:
          0..31  GPRs ($zero..$ra), $ra is index 31
          37     plausible PC (verify with a known breakpoint — see read_pc)
        See read_kseg0_registers() to enumerate code-pointer-like values."""
        self._send_packet(f"p{idx:x}")
        resp = self._recv_packet()
        if not resp or resp.startswith("E"):
            return 0
        return int(resp[:16], 16)

    def read_kseg0_registers(self) -> dict:
        """Return {reg_index: 32-bit value} for every register whose value
        looks like a KSEG0 address (0x80000000..0x80800000). Useful at a
        breakpoint/watchpoint hit for finding PC, $ra, and pointer args
        without knowing the exact register layout."""
        out = {}
        for i in range(72):
            v = self.read_register(i) & 0xFFFFFFFF
            if 0x80000000 <= v <= 0x80800000:
                out[i] = v
        return out

    def read_pc(self) -> int:
        """Best-effort PC read. ares mips64 stub places PC at index 37 in the
        most common layout, but verify by setting a breakpoint at a known
        address and confirming the value matches before trusting this in
        automation."""
        return self.read_register(37) & 0xFFFFFFFF

    def set_breakpoint(self, addr: int, hardware: bool = False) -> bool:
        """Set a code breakpoint at addr. hardware=False uses Z0 (software,
        replaces opcode with trap), hardware=True uses Z1 (hwbreak — ares
        advertises hwbreak- but accepts Z1 OK; behavior unverified)."""
        kind = "Z1" if hardware else "Z0"
        self._send_packet(f"{kind},{addr:x},4")
        return self._recv_packet().strip() == "OK"

    def clear_breakpoint(self, addr: int, hardware: bool = False) -> bool:
        kind = "z1" if hardware else "z0"
        self._send_packet(f"{kind},{addr:x},4")
        return self._recv_packet().strip() == "OK"

    def set_watchpoint(self, addr: int, kind: str = "write", size: int = 4) -> bool:
        """Set a memory watchpoint. kind in {'write','read','access'}.
        Halts the CPU on the next matching access — game keeps running until
        then. Pair with continue_until_halt(). Use clear_watchpoint() before
        stepping or you will keep re-triggering the same store."""
        z = {"write": "Z2", "read": "Z3", "access": "Z4"}[kind]
        self._send_packet(f"{z},{addr:x},{size}")
        return self._recv_packet().strip() == "OK"

    def clear_watchpoint(self, addr: int, kind: str = "write", size: int = 4) -> bool:
        z = {"write": "z2", "read": "z3", "access": "z4"}[kind]
        self._send_packet(f"{z},{addr:x},{size}")
        return self._recv_packet().strip() == "OK"

    def continue_until_halt(self, timeout: float = 10.0) -> str:
        """Continue execution; block until the CPU halts (bp/wp/ctrl-C).
        Returns the raw stop reason packet, e.g. 'T05watch:800a39cc;' or 'S05'.
        Raises socket.timeout if no halt within the window — caller should
        then send halt() to force a stop."""
        old = self.sock.gettimeout()
        self.sock.settimeout(timeout)
        try:
            self._send_packet("c")
            return self._recv_packet()
        finally:
            self.sock.settimeout(old)
            self._drain(0.2)  # consume any stragglers

    def parse_stop_reason(self, resp: str) -> dict:
        """Parse a stop response. Returns {signal, kind, addr?}.
        Examples:
          'S05'                     -> {signal:5, kind:'signal'}
          'T05watch:800a39cc;'      -> {signal:5, kind:'watch',  addr:0x800a39cc}
          'T05swbreak:80003040;'    -> {signal:5, kind:'swbreak',addr:0x80003040}
        """
        if not resp:
            return {"signal": 0, "kind": "none"}
        sig = int(resp[1:3], 16) if len(resp) >= 3 else 0
        if resp[0] == "S":
            return {"signal": sig, "kind": "signal"}
        info = {"signal": sig, "kind": "stop"}
        for part in resp[3:].rstrip(";").split(";"):
            if ":" in part:
                k, v = part.split(":", 1)
                if k in ("watch", "rwatch", "awatch", "swbreak", "hwbreak"):
                    info["kind"] = k
                    try:
                        info["addr"] = int(v, 16)
                    except ValueError:
                        info["raw"] = v
        return info

    def dump_rdram(self, start: int, size: int, chunk: int = 2048) -> bytes:
        """Read a contiguous RDRAM region by chunking. ares advertises
        PacketSize=4096; 2KB chunks (4096 hex chars) are safe and fast.
        ~30 KB/s typical — full 8MB dump takes ~4 minutes; prefer
        targeted regions (slot DLs, framebuffers, descriptor tables)."""
        out = bytearray()
        addr = start
        remaining = size
        while remaining > 0:
            n = min(chunk, remaining)
            out += self.read_mem(addr, n)
            addr += n
            remaining -= n
        return bytes(out)