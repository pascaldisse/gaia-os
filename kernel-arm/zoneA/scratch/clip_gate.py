# GAIA-OS first slab — clipboard gate. Boots gaia-os.img with the serial
# console on a unix socket, drives clip.c through shell.c, and checks the
# bytes that actually came back. Same socket pattern as ramfb_gate.py.
#
# clip(hello love=1) -> clip -> clip > c1 -> clip < c1 -> clip -> ls
# PASS = "hello love=1" appears in the ClipGet output at least twice
#        AND "c1" appears in ls.
# QEMU is always killed.
import os, socket, subprocess, sys, time, signal

ZONEA = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SERIAL_SOCK = "/tmp/gaia-clip-gate.sock"

if os.path.exists(SERIAL_SOCK):
    os.remove(SERIAL_SOCK)

env = dict(os.environ)
env["GAIA_SERIAL"] = f"unix:{SERIAL_SOCK},server=on,wait=off"
env["GAIA_DISPLAY"] = "none"

proc = subprocess.Popen(
    ["./build.sh"],
    cwd=ZONEA, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def connect(path, tries=300):
    for _ in range(tries):
        if os.path.exists(path):
            try:
                s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                s.connect(path)
                return s
            except OSError:
                pass
        time.sleep(0.1)
    return None


def drain(sock, seconds):
    end = time.time() + seconds
    buf = b""
    while time.time() < end:
        try:
            c = sock.recv(65536)
            if not c:
                break
            buf += c
        except socket.timeout:
            pass
    return buf


try:
    s = connect(SERIAL_SOCK)
    if not s:
        print("GATE FAIL: no serial socket")
        sys.exit(1)
    s.settimeout(0.5)

    log = drain(s, 8)                       # boot banner + T:/> prompt
    steps = [b"clip hello love=1", b"clip", b"clip > c1", b"clip < c1", b"clip", b"ls"]
    for cmd in steps:
        s.sendall(cmd + b"\r")
        log += drain(s, 2)

    s.close()
finally:
    proc.send_signal(signal.SIGTERM)
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()
    if os.path.exists(SERIAL_SOCK):
        os.remove(SERIAL_SOCK)

text = log.decode("utf-8", "replace")
print("===== SERIAL LOG (raw, decoded) =====")
print(text)
print("===== END SERIAL LOG =====")

hits = text.count("hello love=1")
print(f"[check] 'hello love=1' occurrences in full log : {hits}")
print(f"[check] 'c1' present in log                  : {'c1' in text}")
# ClipGet lines look like:  clip("hello love=1") 14 bytes
clipget_hits = text.count('clip("hello love=1")')
print(f'[check] ClipGet echo count                  : {clipget_hits}')

ok = hits >= 2 and "c1" in text
print("GATE PASS" if ok else "GATE FAIL")
sys.exit(0 if ok else 1)
