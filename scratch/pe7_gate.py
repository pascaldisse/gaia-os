#!/usr/bin/env python3
# CHALICE C3c Atom 3C-2j scratch harness: boot QEMU headless, drive `pe7`
# (hello6.exe's real KERNEL32.dll import over stock ntdll.dll), capture the
# full svc dispatch trace. Diagnostic frontier only -- NOT added to
# chalice_gate.py's required-pass list this atom. Modeled directly on
# scratch/chalice_gate.py's own pe6 drive.
import os, socket, subprocess, sys, time

REPO  = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ZONEA = os.path.join(REPO, "kernel-arm", "zoneA")
SCR   = os.path.join(REPO, "scratch")
SER   = os.path.join(SCR, "pe7_serial.sock")

try: os.remove(SER)
except FileNotFoundError: pass

env = dict(os.environ, GAIA_SERIAL=f"unix:{SER},server,nowait", GAIA_DISPLAY="none")
qemu = subprocess.Popen(["bash", "build.sh"], cwd=ZONEA, env=env,
                         stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)

def connect(path, timeout=15):
    end = time.time() + timeout
    while time.time() < end:
        try:
            s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            s.connect(path); s.setblocking(False); return s
        except OSError:
            time.sleep(0.1)
    raise SystemExit(f"could not connect {path}")

def drain(s, secs):
    buf = b""; end = time.time() + secs
    while time.time() < end:
        try: buf += s.recv(4096)
        except BlockingIOError: time.sleep(0.05)
    return buf

full = b""
try:
    ser = connect(SER)
    full += drain(ser, 5.0)
    ser.send(b"pe7\n")
    full += drain(ser, 25.0)
finally:
    qemu.terminate()
    try: qemu.wait(timeout=5)
    except Exception: qemu.kill()

text = full.decode("latin1")
sys.stdout.write("---- FULL SERIAL LOG (pe7) ----\n")
sys.stdout.write(text)
