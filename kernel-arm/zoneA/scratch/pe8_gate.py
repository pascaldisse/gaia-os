#!/usr/bin/env python3
# CHALICE C3c-2r -- pe8 gate, cloned from pe7_gate.py (scratch/ is
# gitignored/session-local per HANDOFF convention). Boots gaia-os.img with
# serial routed to a unix socket (never QEMU on stdio, per SCARS
# discipline), drives the shell over that socket, greps the transcript for
# the required-green markers (pe1-pe7 unchanged, no regression) PLUS every
# hello7/pe8 proof line, then kills qemu cleanly.
import os, socket, subprocess, sys, time, signal

ZONEA = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOCK = "/tmp/gaia-pe8-gate.sock"

if os.path.exists(SOCK):
    os.remove(SOCK)

env = dict(os.environ)
env["GAIA_SERIAL"] = f"unix:{SOCK},server=on,wait=off"
env["GAIA_DISPLAY"] = "none"

proc = subprocess.Popen(["./build.sh"], cwd=ZONEA, env=env,
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

s = None
for _ in range(200):
    if os.path.exists(SOCK):
        try:
            s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            s.connect(SOCK)
            break
        except OSError:
            s = None
    time.sleep(0.1)

if not s:
    print("GATE FAIL: never connected to serial socket")
    proc.terminate()
    sys.exit(1)

s.settimeout(0.5)
transcript = b""

def drain(seconds):
    global transcript
    end = time.time() + seconds
    while time.time() < end:
        try:
            chunk = s.recv(65536)
            if not chunk:
                break
            transcript += chunk
        except socket.timeout:
            pass

def send(line):
    s.sendall((line + "\r").encode())

drain(6)
for cmd, wait in [("pe", 3), ("pe4", 3), ("pe5", 3), ("pe6", 6), ("pe7", 8), ("pe8", 8), ("fb", 3)]:
    send(cmd)
    drain(wait)

s.close()
proc.send_signal(signal.SIGTERM)
try:
    proc.wait(timeout=5)
except subprocess.TimeoutExpired:
    proc.kill()
    proc.wait()

txt = transcript.decode(errors="replace")
out_path = "/tmp/gaia-pe8-gate.log"
with open(out_path, "w") as f:
    f.write(txt)
print(f"transcript saved: {out_path} ({len(txt)} bytes)")

required = [
    # ---- pe1-pe5 required-green (no regression, HANDOFF) ----
    "pe1 ret=42",
    "CHALICE: the cup fills",
    "pe2 ret=7",
    "CHALICE: the cup speaks",
    "CHALICE: continue lands",
    "CHALICE: NT10 floor",
    "pe3 ret=99",
    "CHALICE: the cup links",
    "pe4 ret=60",
    "CHALICE: thunk enters",
    "CHALICE: the chain speaks",
    "pe5 ret=99",
    "CHALICE: stock ntdll mapped",
    "CHALICE: stock thunk enters",
    # ---- pe7 milestone (no regression) ----
    "CHALICE: kernel32 speaks",
    "pe7 ret=77",
    # ---- pe8/hello7 surface-walk proof lines ----
    "CHALICE: GetCommandLineA -> \"hello7.exe\"",
    "CHALICE: GetModuleHandleA(NULL) ->",
    "CHALICE: GetModuleHandleA(kernel32.dll) ->",
    "CHALICE: GetProcAddress(WriteFile) ->",
    "CHALICE: WriteFile-via-pointer speaks",
    "CHALICE: GetProcessHeap ->",
    "CHALICE: HeapAlloc(64) ->",
    "CHALICE: heap payload readback -> \"chalice-heap-payload\"",
    "CHALICE: HeapFree ->",
    "CHALICE: GetCurrentProcessId ->",
    "CHALICE: SetLastError(0x1234)/GetLastError ->",
    "CHALICE: hello7 surface walk complete",
    "pe8 ret=78",
]
missing = [r for r in required if r not in txt]
print("---- required-green ----")
for r in required:
    print(("OK  " if r in txt else "MISS") + " " + r)
print("---- pe8-specific / fault lines ----")
for line in txt.splitlines():
    if "pe8" in line or "hello7" in line or "GetCommandLineA" in line or "GetModuleHandleA" in line or \
       "GetProcAddress" in line or "GetProcessHeap" in line or "HeapAlloc" in line or "HeapFree" in line or \
       "GetCurrentProcessId" in line or "GetLastError" in line or "SetLastError" in line or \
       "FAULT" in line or "NtRaiseException" in line or "svc id=" in line:
        print(line)

if missing:
    print(f"GATE FAIL: missing {len(missing)} required lines")
    sys.exit(1)
print("GATE PASS (required-green)")
