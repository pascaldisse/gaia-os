#!/usr/bin/env python3
"""CHALICE C4-20: raw-RSP execution-truth trace for `pe10 fex`.

Starts the normal Zone A boot rig through build.sh, never a naked QEMU.  The
GDB socket is opt-in (GAIA_GDB_SOCKET) and only this harness enables it.
Addresses are derived from this boot's serial module-base receipts; only the
JIT HLT has a historical fallback because it is arena-generated.
"""
import argparse
import os
import re
import signal
import socket
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ZONEA = os.path.join(ROOT, "kernel-arm", "zoneA")
LLVM_OBJDUMP = "/opt/homebrew/opt/llvm@20/bin/llvm-objdump"
NTDLL = os.path.join(ROOT, "kernel-arm", "chalice", "stock", "ntdll.dll")
DEFAULT_HLT = 0x2681B02F0
OFFSETS = {"dispatcher": 0x66C40, "wow64_prepare": 0x1DA18, "fex_reset": 0x13423C}


def fail(message):
    print(f"TRACE FAIL: {message}", file=sys.stderr)
    raise RuntimeError(message)


class RSP:
    def __init__(self, path):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.settimeout(10)
        self.sock.connect(path)

    @staticmethod
    def packet(payload):
        raw = payload.encode("ascii")
        return b"$" + raw + b"#" + f"{sum(raw) & 0xff:02x}".encode("ascii")

    def byte(self):
        got = self.sock.recv(1)
        if not got:
            fail("gdb socket closed")
        return got

    def reply(self):
        while True:
            lead = self.byte()
            if lead in b"+-":
                continue
            if lead != b"$":
                continue
            body = bytearray()
            while True:
                c = self.byte()
                if c == b"#":
                    break
                body += c
            checksum = self.sock.recv(2)
            if len(checksum) != 2:
                fail("truncated RSP checksum")
            self.sock.sendall(b"+")
            text = body.decode("ascii", "replace")
            # QEMU may emit GDB console packets; consume them, they are not
            # command replies or stops.
            if text.startswith("O"):
                continue
            return text

    def command(self, payload):
        self.sock.sendall(self.packet(payload))
        # reply() consumes either an ACK followed by a packet or a direct
        # packet.  QEMU can select either form before qSupported completes.
        return self.reply()

    def continue_(self):
        self.sock.sendall(self.packet("c"))

    def interrupt(self):
        self.sock.sendall(b"\x03")

    def stop(self):
        while True:
            result = self.reply()
            if result.startswith(("S", "T", "W", "X")):
                return result

    def reg(self, number):
        text = self.command(f"p{number:x}")
        if text.startswith("E") or len(text) != 16:
            fail(f"register {number} reply={text!r}")
        return int.from_bytes(bytes.fromhex(text), "little")

    def memory(self, address, length):
        text = self.command(f"m{address:x},{length:x}")
        if text.startswith("E"):
            fail(f"memory {address:#x} reply={text!r}")
        return bytes.fromhex(text)

    def breakpoint(self, address):
        reply = self.command(f"Z0,{address:x},4")
        if reply != "OK":
            fail(f"Z0 {address:#x}: {reply!r}")

    def close(self):
        self.sock.close()


def wait_path(path, seconds=20):
    end = time.time() + seconds
    while time.time() < end:
        if os.path.exists(path):
            return
        time.sleep(.05)
    fail(f"socket never appeared: {path}")


def connect_serial(path):
    end = time.time() + 20
    while time.time() < end:
        try:
            s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            s.settimeout(.2)
            s.connect(path)
            return s
        except OSError:
            time.sleep(.05)
    fail(f"serial socket never accepted: {path}")


def drain(serial, transcript, seconds):
    end = time.time() + seconds
    while time.time() < end:
        try:
            data = serial.recv(65536)
            if not data:
                break
            transcript.extend(data)
        except socket.timeout:
            pass


def wait_for(serial, transcript, predicate, seconds):
    end = time.time() + seconds
    while time.time() < end:
        drain(serial, transcript, .2)
        if predicate(transcript.decode("utf-8", "replace")):
            return
    fail("serial receipt timed out")


def exported_rva(name):
    output = subprocess.check_output([LLVM_OBJDUMP, "-p", NTDLL], text=True)
    match = re.search(r"^\s*\d+\s+0x([0-9a-fA-F]+)\s+" + re.escape(name) + r"\s*$",
                      output, re.M)
    if not match:
        fail(f"export not found: {name}")
    return int(match.group(1), 16)


def latest_hex(text, pattern, label):
    found = re.findall(pattern, text, re.I | re.M)
    if not found:
        fail(f"missing {label} module-base receipt")
    return int(found[-1], 16)


def module_bases(text):
    # nt_boot_process emits the first form; the WOW64 probe emits the other
    # two, each from actual mapping rather than a baked address.
    return {
        "ntdll": latest_hex(text, r"CHALICE PROBE: ntdll_base=0x([0-9a-f]+)", "ntdll"),
        "wow64": latest_hex(text, r"CHALICE: wow64\.dll mapped base=0x([0-9a-f]+)", "wow64"),
        "fex": latest_hex(text, r"CHALICE: CPU backend mapped name=libwow64fex\.dll\s*\r?\n\s*base=0x([0-9a-f]+)", "FEX"),
    }


def pc_from_stop(rsp, stop):
    match = re.search(r"(?:^|;)pc:([0-9a-fA-F]+)", stop)
    if match:
        return int.from_bytes(bytes.fromhex(match.group(1)), "little")
    return rsp.reg(32)  # AArch64 g-packet register order: x0..x30, sp, pc.


def ranges(before, after):
    changed = [i for i, (a, b) in enumerate(zip(before, after)) if a != b]
    if not changed:
        return "none"
    pieces = []
    first = last = changed[0]
    for offset in changed[1:]:
        if offset == last + 1:
            last = offset
            continue
        pieces.append(f"0x{first:x}-0x{last:x}")
        first = last = offset
    pieces.append(f"0x{first:x}-0x{last:x}")
    return ",".join(pieces)


def terminate(proc):
    if not proc:
        return
    proc.send_signal(signal.SIGTERM)
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--gdb-socket", default="/tmp/gaia-pe10-trace-gdb.sock")
    parser.add_argument("--serial-socket", default="/tmp/gaia-pe10-trace-serial.sock")
    parser.add_argument("--hlt", type=lambda x: int(x, 0), default=None,
                        help="JIT HLT address; default learns this boot's exception ELR, then historical fallback")
    parser.add_argument("--hits", type=int, default=12)
    parser.add_argument("--timeout", type=float, default=25)
    args = parser.parse_args()

    for path in (args.gdb_socket, args.serial_socket):
        try:
            os.unlink(path)
        except FileNotFoundError:
            pass

    env = dict(os.environ)
    env.update({"GAIA_GDB_SOCKET": args.gdb_socket,
                "GAIA_SERIAL": f"unix:{args.serial_socket},server=on,wait=off",
                "GAIA_DISPLAY": "none"})
    transcript = bytearray()
    proc = serial = rsp = None
    lines = []
    try:
        print("TRACE start build rig")
        proc = subprocess.Popen(["./build.sh"], cwd=ZONEA, env=env,
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        wait_path(args.gdb_socket)
        print("TRACE gdb socket ready")
        rsp = RSP(args.gdb_socket)
        print(f"RSP qSupported={rsp.command('qSupported:multiprocess+')}" )
        print(f"RSP initial-stop={rsp.command('?')}")
        serial = connect_serial(args.serial_socket)
        rsp.continue_()
        drain(serial, transcript, 6)
        serial.sendall(b"pe10 fex\r")
        wait_for(serial, transcript,
                 lambda t: "CHALICE EXCEPTION:" in t and "CPU backend mapped name=libwow64fex.dll" in t,
                 args.timeout)
        # Freeze the known delivery loop, then derive every image-relative
        # address from this boot's serial receipts before setting breakpoints.
        rsp.interrupt()
        frozen = rsp.stop()
        print(f"RSP frozen-stop={frozen} pc=0x{pc_from_stop(rsp, frozen):x}")
        text = transcript.decode("utf-8", "replace")
        bases = module_bases(text)
        hlt = args.hlt
        if hlt is None:
            hit = re.findall(r"CHALICE EXCEPTION: code=0x[0-9A-F]+ elr=0x([0-9A-F]+)", text)
            hlt = int(hit[-1], 16) if hit else DEFAULT_HLT
        restore_rva = exported_rva("RtlRestoreContext")
        points = {
            "dispatcher": bases["ntdll"] + OFFSETS["dispatcher"],
            "wow64_prepare": bases["wow64"] + OFFSETS["wow64_prepare"],
            "fex_reset": bases["fex"] + OFFSETS["fex_reset"],
            "hlt": hlt,
            "RtlRestoreContext": bases["ntdll"] + restore_rva,
        }
        print("TRACE BASES " + " ".join(f"{k}=0x{v:x}" for k, v in bases.items()))
        print("TRACE BREAKPOINTS " + " ".join(f"{k}=0x{v:x}" for k, v in points.items()))
        for address in points.values():
            rsp.breakpoint(address)

        delivery_context = None
        restore_context = None
        for _ in range(args.hits):
            rsp.continue_()
            stop = rsp.stop()
            pc = pc_from_stop(rsp, stop)
            names = [name for name, address in points.items() if address == pc]
            name = "+".join(names) if names else "other"
            line = f"TRACE HIT {name} pc=0x{pc:x} stop={stop}"
            lines.append(line)
            print(line)
            if "dispatcher" in names:
                sp = rsp.reg(31)
                delivery_context = rsp.memory(sp, 0x390)
                print(f"TRACE CONTEXT before-delivery addr=0x{sp:x} bytes=0x{len(delivery_context):x}")
            if "RtlRestoreContext" in names:
                x0 = rsp.reg(0)
                restore_context = rsp.memory(x0, 0x390)
                print(f"TRACE CONTEXT at-restore addr=0x{x0:x} bytes=0x{len(restore_context):x}")
                if delivery_context is not None:
                    print("TRACE CONTEXT DIFF delivery-vs-restore=" +
                          ranges(delivery_context, restore_context))
        drain(serial, transcript, 1)
    finally:
        if serial:
            serial.close()
        if rsp:
            rsp.close()
        terminate(proc)
        for path in (args.gdb_socket, args.serial_socket):
            try:
                os.unlink(path)
            except FileNotFoundError:
                pass
        log = "/tmp/gaia-pe10-trace.log"
        with open(log, "wb") as out:
            out.write(transcript)
        print(f"TRACE serial-log={log} bytes={len(transcript)}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as exc:
        print(f"TRACE FAIL: {exc}", file=sys.stderr)
        sys.exit(1)
