#!/usr/bin/env python3
# CHALICE C4-2 -- pe9 gate, cloned from pe8_gate.py (scratch/ is
# gitignored/session-local per HANDOFF convention). Boots gaia-os.img with
# serial routed to a unix socket (never QEMU on stdio), drives the shell
# over that socket, greps the transcript for the required-green markers:
# pe8's full required-green list (no regression) PLUS pe9's own two
# image-map proof lines, for BOTH CPU backends (default wowbox64.dll and
# `pe9 fex` -> libwow64fex.dll -- IRON law: backend is a parameter, prove
# both binds work at the mount seam).
import os, socket, subprocess, sys, time, signal

ZONEA = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOCK = "/tmp/gaia-pe9-gate.sock"

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
# CHALICE C4-10: pe9/pe9-fex waits bumped 4/4 -> 45/45 -- the RESERVE/COMMIT
# arena's per-window commit-on-first-touch cost (same root cause documented
# in backtoback_gate.py this leg) makes pe9's ~250-call AVM sequence take
# anywhere from ~8s to ~40s wall-clock depending on host load (live-measured
# both ends this leg, isolated probe outside this gate); 45s covers the
# observed worst case with margin. Harness timing-budget fix, not a
# functional regression -- pe9 itself completes correctly every time given
# the headroom.
for cmd, wait in [("pe", 3), ("pe4", 3), ("pe5", 3), ("pe6", 6), ("pe7", 8), ("pe8", 8),
                   ("pe9", 45), ("pe9 fex", 45), ("fb", 3)]:
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
out_path = "/tmp/gaia-pe9-gate.log"
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
    # ---- pe8/hello7 surface-walk proof lines (no regression) ----
    "CHALICE: hello7 surface walk complete",
    "pe8 ret=78",
    # ---- pe9/C4-2 -- WOW64 image trio maps, default backend (wowbox64) ----
    "CHALICE: wow64.dll mapped",
    "CHALICE: CPU backend mapped name=wowbox64.dll",
    # ---- pe9/C4-4 (first slice) -- BTCpu export resolution, default backend ----
    "CHALICE: BTCpu export scan name=wowbox64.dll",
    "CHALICE: BTCpu wired",
    # ---- pe9/C4-4 (this slice) -- two-root FS live self-test: real
    # NtOpenFile dirname-redirect + raw Machine-field read (NtReadFile,
    # format-invariant -- NtQuerySection/pe_query_image_info can't parse
    # 32-bit PE yet, C4-5 territory). All 4 must show the CORRECT arch
    # resolved, not just "open succeeded" (default/non-wow64 %system32%
    # stays 0xAA64 unchanged; wow64-active %system32% redirects to 0x14C;
    # sysnative always escapes to 0xAA64 even wow64-active; explicit
    # syswow64 always resolves 0x14C).
    "CHALICE: fstest native-default wow64active=0 path=\\??\\C:\\windows\\system32\\kernel32.dll open_rc=0 machine=0x000000000000AA64",
    "CHALICE: fstest wow64-redirect wow64active=1 path=\\??\\C:\\windows\\system32\\kernel32.dll open_rc=0 machine=0x000000000000014C",
    "CHALICE: fstest sysnative-escape wow64active=1 path=\\??\\C:\\windows\\sysnative\\ntdll.dll open_rc=0 machine=0x000000000000AA64",
    "CHALICE: fstest explicit-syswow64 wow64active=0 path=\\??\\C:\\windows\\syswow64\\kernelbase.dll open_rc=0 machine=0x000000000000014C",
    # ---- pe9/C4-5 -- PE32 (i386) section-path live proof: real
    # NtOpenFile->NtCreateSection(SEC_IMAGE)->NtQuerySection->
    # NtMapViewOfSection chain over a genuine i386 blob (syswow64/ntdll.dll
    # AND hello8_x86.exe), format-CORRECT now via pemod.c's is_pe32 path
    # (not the raw-byte-read fstest workaround above) -- both must resolve
    # Machine=0x14C and map successfully (rc=0 at every stage).
    "CHALICE: pe32sec syswow64-ntdll path=\\??\\C:\\windows\\syswow64\\ntdll.dll open_rc=0 create_rc=0 query_rc=0 Machine=0x000000000000014C",
    "CHALICE: pe32sec hello8-x86-exe path=\\??\\C:\\windows\\hello8_x86.exe open_rc=0 create_rc=0 query_rc=0 Machine=0x000000000000014C",
    # ---- pe9/C4-3 -- TEB32/PEB32 floor (structures built+wired, no guest
    # code runs yet) -- must appear before this invocation's own "pe9 ret=0".
    "CHALICE: WOW64 teb32/peb32 built",
    "CHALICE: WOW64 crossproclist addr=",
    "pe9 ret=0",
]
missing = [r for r in required if r not in txt]

# `pe9 fex` run: the fex-backend proof line must appear SOMEWHERE after the
# first "pe9 ret=0" (i.e. in the second invocation's own output) -- checked
# by simple substring presence (both backends are named distinctly, no
# ambiguity even scanning the whole transcript).
fex_line = "CHALICE: CPU backend mapped name=libwow64fex.dll"
if fex_line not in txt:
    missing.append(fex_line)

# CHALICE C4-4 (first slice): fex-backend BTCpu export-scan proof line,
# same "appears somewhere after the first pe9 ret=0" substring check.
fex_btcpu_line = "CHALICE: BTCpu export scan name=libwow64fex.dll"
if fex_btcpu_line not in txt:
    missing.append(fex_btcpu_line)

print("---- required-green ----")
for r in required:
    print(("OK  " if r in txt else "MISS") + " " + r)
print(("OK  " if fex_line in txt else "MISS") + " " + fex_line)
print(("OK  " if fex_btcpu_line in txt else "MISS") + " " + fex_btcpu_line)
print("---- pe9-specific / fault lines ----")
for line in txt.splitlines():
    if "pe9" in line or "wow64" in line.lower() or "CPU backend" in line or \
       "BTCpu" in line or "FAULT" in line or "NtRaiseException" in line or "boot:" in line:
        print(line)

if missing:
    print(f"GATE FAIL: missing {len(missing)} required lines")
    sys.exit(1)
print("GATE PASS (required-green, no regression, both CPU backends)")
