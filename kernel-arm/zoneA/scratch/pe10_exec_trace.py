#!/usr/bin/env python3
# CHALICE C4-28 -- QEMU '-d exec' fallback trace for pe10 fex (Z-packets
# proven dead for breakpoint use on this stub per C4-20/21). Passes '-d exec
# -D <logfile>' straight through build.sh's trailing-arg passthrough to
# qemu-system-aarch64. Logs every ARM64 translated-block entry PC executed
# by the guest (kernel + ntdll + wow64 + FEX JIT are all ARM64 code on this
# floor -- x86 is emulated INSIDE the ARM64 guest by FEX, not by QEMU).
# Isolated probe, never the canonical gate path -- SIGTERMs QEMU regardless
# of outcome.
import os, socket, subprocess, sys, time, signal

ZONEA = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOCK = "/tmp/gaia-pe10-exectrace.sock"
EXECLOG = "/tmp/gaia-pe10-exec.log"

for p in (SOCK, EXECLOG):
    if os.path.exists(p):
        os.remove(p)

env = dict(os.environ)
env["GAIA_SERIAL"] = f"unix:{SOCK},server=on,wait=off"
env["GAIA_DISPLAY"] = "none"

proc = subprocess.Popen(["./build.sh", "-d", "exec", "-D", EXECLOG], cwd=ZONEA, env=env,
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
    print("PROBE FAIL: never connected to serial socket")
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
send("pe10 fex")
drain(8)   # RAISE#1 fires within ~1-2s of issuing pe10 fex per prior receipts

s.close()
proc.send_signal(signal.SIGTERM)
try:
    proc.wait(timeout=5)
except subprocess.TimeoutExpired:
    proc.kill()
    proc.wait()

txt = transcript.decode(errors="replace")
out_path = "/tmp/gaia-pe10-exectrace.log"
with open(out_path, "w") as f:
    f.write(txt)
print(f"serial transcript saved: {out_path} ({len(txt)} bytes)")
print(f"exec trace saved: {EXECLOG} ({os.path.getsize(EXECLOG) if os.path.exists(EXECLOG) else 0} bytes)")
print("---- serial tail (last 40 lines) ----")
for line in txt.splitlines()[-40:]:
    print(line)
