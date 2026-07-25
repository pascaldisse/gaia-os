#!/usr/bin/env python3
# CHALICE C4-68C4 -- COUNT-BASED late-arm (final watchpoint atom). Same
# rig-level QEMU gdbstub / lldb SBWatchpoint mechanism as C4-68C/C2/C3 (8-byte
# WRITE watchpoint on FEXCore's own CpuStateFrame::State.rip,
# CurrentFrame+0x18, default 0x270001158) -- NOT the guest DBGWCR/PSTATE.SS
# mechanism (C4-68/68B, proven undelivered under QEMU TCG).
#
# PRIOR TWO LATE-ARM ATTEMPTS, both 0 hits, MEASURED (not guessed):
#   c4-68c2: marker = last-shaped id=80 NtProtectVirtualMemory BaseAddress
#     print (ntfile.c C4-60) -- PROVEN TOO LATE: that line prints AFTER the
#     syscall completes, the zero-write follows within ~5 instructions, the
#     arm/interrupt/WatchAddress round-trip loses the race every time.
#   c4-68c3: marker = one-shot CHALICE C4-57 print ("binding boot exe IAT"),
#     line 5207/12389 (~42% of a full boot) -- race WON cleanly (watchpoint
#     armed, confirmed live) but the watched window opened was ~7100 lines
#     wide; QEMU TCG's own global per-block memory-write check only advanced
#     ~1400 of those lines in the full 600s WATCHED_TIMEOUT budget (measured
#     rate: ~0.43s/watched-line) -- reaching the fault from there would need
#     ~50min, impossible in any single run.
#
# THIS ATOM -- data-driven COUNT-based late-arm, no fixed marker string:
#   1. From a reference full V2 receipt (scratch/c4-68_v2_receipt_full.log,
#      full boot to fault, WITH the C4-64 per-syscall probe lines), count
#      total occurrences of the C4-64 probe print ("CHALICE C4-64: svc
#      id=") from boot to the fault line (elr=0x26D0C02B0) -- call it T.
#      Computed live from the reference file every run (never hardcoded),
#      T=2212 as of this atom's own reference receipt.
#   2. Drive the SAME boot at full speed (unwatched), tailing the live
#      serial transcript and counting the SAME probe print live; the moment
#      the live count reaches T-MARGIN, interrupt the vCPU and late-arm the
#      watchpoint, then resume. MARGIN=100 by default -- ~100 syscalls of
#      margin vs the single-digit-instruction latency the c4-68c2 race
#      lost by, so the watched slice should be only ~100-300 transcript
#      lines (~1-3 min at the c4-68c3-measured watched rate), not ~7100.
#   3. AUTO-RETRY (max 2 runs total, each under its own 900s TOTAL_TIMEOUT):
#      if attempt 1 ends with 0 hits AND the fault trampoline
#      (elr=0x26D0C02B0) appeared on the live serial transcript, that means
#      the boot reached the fault but the watchpoint was armed too LATE
#      (after the write) -- retry with a BIGGER margin (400, arms EARLIER).
#      If attempt 1 ends with 0 hits and NO fault appeared on serial, that
#      means the watched window opened too EARLY and the run hit
#      WATCHED_TIMEOUT mid-slice before reaching the fault -- retry with a
#      SMALLER margin (50, arms LATER/closer to the fault).
#   4. Hit handling unchanged from c4-68c2/c3: every watchpoint stop logs
#      pc+new_value, auto-continues; the first value==0 hit additionally
#      dumps full x0-x30/fp/lr/sp/pc/cpsr registers + a 256B disassembly
#      window around pc, and the loop keeps watching until the fault
#      trampoline appears on serial (early-stop, no need to burn the rest
#      of WATCHED_TIMEOUT). The last 10 hit values before any zero are also
#      recorded (the legit restore pattern breaking down is part of the
#      prize per this atom's own task).
#
# No hardcoded watch address blind either -- read live from this boot's own
# "CHALICE C4-68: watchpoint ARMED addr=0x..." print if it appears before
# our own late-arm point; falls back to the fixed 0x270001158 default (every
# c4-68* receipt on disk so far has shown this address, deterministic
# allocator, no ASLR) only if that print is somehow missed.
#
# No hardcoded port (IRON LAW) -- fresh free TCP port each attempt. Never
# /tmp -- all sockets/logs live under zoneA/scratch/. Vendored FEX/ tree
# read-only, not touched by this script (it only drives QEMU + reads serial
# + reads/writes guest memory via the gdb-remote protocol).
# C4-68C5 (this run): single attempt, no auto-retry, margin=25 (arm_target
# ~T-25=2187 vs c4-68c4's measured armed_at_count 2157/2169 of T=2212 --
# overshoot during interrupt lands ~2195-2200, ~12-17 watched syscalls to
# the fault at the measured deep-region rate of ~45-50s/syscall-line, i.e.
# roughly 10-15 watched minutes). watched_timeout=3600, total_timeout=5400
# (budget covers a much worse overshoot than measured). Receipts prefix
# c4-68c5_*. Launched detached (nohup/setsid) so it survives lane death.
import os, sys, re, socket, subprocess, threading, time, signal

ZONEA = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCRATCH = os.path.join(ZONEA, "scratch")
WATCH_ADDR_DEFAULT = int(sys.argv[1], 0) if len(sys.argv) > 1 else 0x270001158
WATCH_SIZE = 8

REFERENCE_RECEIPT = os.path.join(SCRATCH, "c4-68_v2_receipt_full.log")
C464_RE = re.compile(rb"CHALICE C4-64: svc id=")
# fault trampoline address, every c4-68* receipt on disk so far.
FAULT_RE = re.compile(rb"elr=0x0*26D0C02B0")
ARMED_RE = re.compile(rb"CHALICE C4-68: watchpoint ARMED addr=0x([0-9A-Fa-f]+)")

NON_WP_STOP_CAP = 50


def compute_T():
    """Count C4-64 probe occurrences from boot to the fault line in the
    reference full V2 receipt. Computed live every run -- never hardcoded."""
    with open(REFERENCE_RECEIPT, "rb") as f:
        data = f.read()
    total = 0
    for line in data.split(b"\n"):
        if FAULT_RE.search(line):
            break
        if C464_RE.search(line):
            total += 1
    return total


T = compute_T()
print(f"reference receipt {REFERENCE_RECEIPT}: T={T} (C4-64 probe occurrences boot->fault)")


def read_val(process, addr):
    import lldb
    e = lldb.SBError()
    data = process.ReadMemory(addr, 8, e)
    if not e.Success() or data is None:
        return None
    return int.from_bytes(data, "little")


def read_regs(thread):
    fr = thread.GetFrameAtIndex(0)
    regs = {}
    for reg_set in fr.GetRegisters():
        for r in reg_set:
            regs[r.GetName()] = r.GetValue()
    return fr.GetPC(), regs


def dump_disasm(process, pc, label):
    import lldb
    e = lldb.SBError()
    lo = max(0, pc - 64)
    data = process.ReadMemory(lo, 192, e)
    if not e.Success() or data is None:
        return f"disasm read failed at 0x{lo:x}: {e}"
    raw_path = os.path.join(SCRATCH, f"{label}_zerowrite_window.bin")
    with open(raw_path, "wb") as f:
        f.write(data)
    try:
        out = subprocess.check_output(
            ["aarch64-elf-objdump", "-D", "-b", "binary", "-m", "aarch64",
             "--adjust-vma=" + hex(lo), raw_path], stderr=subprocess.STDOUT
        ).decode(errors="replace")
    except Exception as ex:
        try:
            out = subprocess.check_output(
                ["llvm-objdump", "-D", "-b", "binary", "-m", "aarch64",
                 "--adjust-vma=" + hex(lo), raw_path], stderr=subprocess.STDOUT
            ).decode(errors="replace")
        except Exception as ex2:
            out = f"objdump failed: {ex} / {ex2}"
    return out


def run_attempt(attempt_no, margin, total_timeout=900, watched_timeout=600):
    boot_to_marker_timeout = max(60, total_timeout - watched_timeout - 60)
    label = "c4-68c5"
    serial_sock = os.path.join(SCRATCH, "c4-68c5-serial.sock")
    hits_log = os.path.join(SCRATCH, f"{label}_watch_hits.log")
    serial_log_full = os.path.join(SCRATCH, f"{label}_v2_watch_receipt_full.log")
    serial_log_short = os.path.join(SCRATCH, f"{label}_v2_watch_receipt.log")
    qemu_log = os.path.join(SCRATCH, f"{label}_qemu_launch.log")
    for p in (serial_sock, hits_log, serial_log_full, serial_log_short, qemu_log):
        if os.path.exists(p):
            os.remove(p)

    arm_target = max(1, T - margin)
    print(f"[attempt {attempt_no}] T={T} margin={margin} arm_target={arm_target} "
          f"total_timeout={total_timeout} watched_timeout={watched_timeout} "
          f"boot_to_marker_timeout={boot_to_marker_timeout}")

    _s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    _s.bind(("127.0.0.1", 0))
    GDB_PORT = _s.getsockname()[1]
    _s.close()

    env = dict(os.environ)
    env["GAIA_SERIAL"] = f"unix:{serial_sock},server=on,wait=off"
    env["GAIA_DISPLAY"] = "none"
    env["GAIA_GDB_PORT"] = str(GDB_PORT)

    print(f"[attempt {attempt_no}] launching build.sh: gdb tcp::{GDB_PORT}, serial {serial_sock}")
    qemu_log_f = open(qemu_log, "w")
    proc = subprocess.Popen(["./build.sh"], cwd=ZONEA, env=env,
                             stdout=qemu_log_f, stderr=subprocess.STDOUT)

    result = dict(hit_count=0, fault_seen=False, ok=False, last_values=[],
                  hits_log=hits_log, serial_log_full=serial_log_full,
                  serial_log_short=serial_log_short, qemu_log=qemu_log,
                  armed=False, armed_at_count=0, watch_addr=None)

    def cleanup_proc():
        try:
            proc.terminate()
            proc.wait(timeout=5)
        except Exception:
            try:
                proc.kill()
                proc.wait()
            except Exception:
                pass

    for _ in range(200):
        if os.path.exists(serial_sock):
            break
        time.sleep(0.1)
    else:
        print(f"[attempt {attempt_no}] FAIL: serial socket never appeared")
        cleanup_proc()
        return result

    lldb_pypath = subprocess.check_output(["lldb", "-P"]).decode().strip()
    if lldb_pypath not in sys.path:
        sys.path.insert(0, lldb_pypath)
    import lldb

    dbg = lldb.SBDebugger.Create()
    dbg.SetAsync(True)
    target = dbg.CreateTargetWithFileAndArch(None, "aarch64")
    listener = lldb.SBListener(f"c468c4-{attempt_no}")
    cerr = lldb.SBError()
    process = None
    for _ in range(100):
        process = target.ConnectRemote(listener, f"connect://127.0.0.1:{GDB_PORT}", "gdb-remote", cerr)
        if cerr.Success():
            break
        time.sleep(0.1)
    if not cerr.Success():
        print(f"[attempt {attempt_no}] FAIL: gdb-remote connect failed:", cerr)
        cleanup_proc()
        return result
    print(f"[attempt {attempt_no}] gdb-remote attached, CPU paused at reset -- "
          f"full-speed boot, counting C4-64 probe lines")

    stop_flag = threading.Event()
    transcript = bytearray()
    transcript_lock = threading.Lock()

    def serial_driver():
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.connect(serial_sock)
        s.settimeout(0.2)

        def drain(sec):
            end = time.time() + sec
            while time.time() < end and not stop_flag.is_set():
                try:
                    chunk = s.recv(65536)
                    if not chunk:
                        break
                    with transcript_lock:
                        transcript.extend(chunk)
                except socket.timeout:
                    pass
        drain(6)
        s.sendall(b"pe10 fex\r")
        drain(max(5, total_timeout - 10))
        s.close()

    th_serial = threading.Thread(target=serial_driver, daemon=True)
    th_serial.start()

    process.Continue()

    watch_addr = None
    marker_seen = False
    marker_deadline = time.time() + boot_to_marker_timeout
    last_count = 0
    while time.time() < marker_deadline:
        with transcript_lock:
            buf = bytes(transcript)
        if watch_addr is None:
            m = ARMED_RE.search(buf)
            if m:
                watch_addr = int(m.group(1), 16)
                print(f"[attempt {attempt_no}] live-read watchpoint addr from boot's own "
                      f"ARMED print: 0x{watch_addr:x}")
        cnt = buf.count(b"CHALICE C4-64: svc id=")
        last_count = cnt
        if cnt >= arm_target:
            marker_seen = True
            print(f"[attempt {attempt_no}] late-arm count reached: {cnt} >= target {arm_target} "
                  f"(T={T} margin={margin})")
            break
        if not th_serial.is_alive():
            break
        time.sleep(0.02)
    else:
        print(f"[attempt {attempt_no}] FAIL: count target not reached within boot_to_marker budget "
              f"(last count={last_count})")

    if not marker_seen:
        print(f"[attempt {attempt_no}] FAIL: late-arm count target not reached, last count={last_count}")
        stop_flag.set()
        with transcript_lock:
            txt = bytes(transcript)
        with open(serial_log_full, "w") as f:
            f.write(txt.decode(errors="replace"))
        result["fault_seen"] = FAULT_RE.search(txt) is not None
        process.Kill()
        cleanup_proc()
        return result

    if watch_addr is None:
        watch_addr = WATCH_ADDR_DEFAULT
        print(f"[attempt {attempt_no}] boot's own ARMED print not seen yet -- "
              f"using default watch addr 0x{watch_addr:x}")

    process.SendAsyncInterrupt()
    stopped = False
    ev0 = lldb.SBEvent()
    for _ in range(100):
        got = listener.WaitForEvent(1, ev0)
        if got and lldb.SBProcess.GetStateFromEvent(ev0) == lldb.eStateStopped:
            stopped = True
            break
    if not stopped:
        print(f"[attempt {attempt_no}] FAIL: process did not stop after interrupt, "
              f"state={process.GetState()}")
        cleanup_proc()
        return result
    print(f"[attempt {attempt_no}] vCPU interrupted at late-arm point (count={last_count})")

    werr = lldb.SBError()
    wp = target.WatchAddress(watch_addr, WATCH_SIZE, False, True, werr)
    if not werr.Success() or not wp or not wp.IsValid():
        print(f"[attempt {attempt_no}] FAIL: watchpoint arm failed:", werr)
        cleanup_proc()
        return result
    result["armed"] = True
    result["armed_at_count"] = last_count
    result["watch_addr"] = watch_addr
    print(f"[attempt {attempt_no}] write watchpoint LATE-ARMED at 0x{watch_addr:x} size={WATCH_SIZE} "
          f"count={last_count}/{T} margin={margin} -- resuming")

    hits_f = open(hits_log, "w")
    hits_f.write(f"# CHALICE C4-68C4 watch hits -- addr=0x{watch_addr:x} size={WATCH_SIZE} "
                 f"count-based late-arm T={T} margin={margin} arm_target={arm_target} "
                 f"armed_at_count={last_count}\n")
    hits_f.flush()

    watch_start = time.time()
    process.Continue()

    hit_count = 0
    last_values = []
    zero_seen = False
    fault_seen = False
    non_wp_stop_count = 0
    ev = lldb.SBEvent()
    while (time.time() - watch_start < watched_timeout
           and time.time() < marker_deadline + watched_timeout + 30):
        if zero_seen and not fault_seen:
            with transcript_lock:
                buf_tail = bytes(transcript[-4096:])
            if FAULT_RE.search(buf_tail):
                fault_seen = True
                hits_f.write("# fault trampoline elr=0x26D0C02B0 observed in serial -- "
                              "stopping watch loop\n")
                hits_f.flush()
                break
        got = listener.WaitForEvent(1, ev)
        if not got:
            if not th_serial.is_alive() and process.GetState() != lldb.eStateStopped:
                break
            continue
        st = lldb.SBProcess.GetStateFromEvent(ev)
        if st == lldb.eStateStopped:
            th = process.GetThreadAtIndex(0)
            reason = th.GetStopReason() if th else None
            if reason == lldb.eStopReasonWatchpoint:
                hit_count += 1
                pc, regs = read_regs(th)
                val = read_val(process, watch_addr)
                val_s = f"0x{val:016x}" if val is not None else "READ_FAIL"
                line = f"hit={hit_count} pc=0x{pc:016x} new_value={val_s}"
                hits_f.write(line + "\n")
                hits_f.flush()
                last_values.append(val)
                last_values = last_values[-10:]
                if val == 0 and not zero_seen:
                    zero_seen = True
                    hits_f.write(f"# ZERO WRITE at hit={hit_count} pc=0x{pc:016x}\n")
                    for k in ("x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9",
                              "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17",
                              "x18", "x19", "x20", "x21", "x22", "x23", "x24", "x25",
                              "x26", "x27", "x28", "fp", "lr", "sp", "pc", "cpsr"):
                        if k in regs:
                            hits_f.write(f"#   {k}={regs[k]}\n")
                    hits_f.write("# disasm window pc-64..pc+128:\n")
                    for dl in dump_disasm(process, pc, label).splitlines():
                        hits_f.write("#   " + dl + "\n")
                    hits_f.flush()
                    print(f"[attempt {attempt_no}] ZERO WRITE CAUGHT at hit={hit_count} "
                          f"pc=0x{pc:016x} -- continuing to observe fault/exit")
                process.Continue()
            else:
                non_wp_stop_count += 1
                pc, regs = read_regs(th) if th else (0, {})
                hits_f.write(f"# non-watchpoint stop #{non_wp_stop_count} reason={reason} pc=0x{pc:016x}\n")
                hits_f.flush()
                if non_wp_stop_count >= NON_WP_STOP_CAP:
                    hits_f.write(f"# BAIL: {NON_WP_STOP_CAP} non-watchpoint stops reached -- "
                                  "dumping state and stopping\n")
                    for k in ("x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9",
                              "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17",
                              "x18", "x19", "x20", "x21", "x22", "x23", "x24", "x25",
                              "x26", "x27", "x28", "fp", "lr", "sp", "pc", "cpsr"):
                        if k in regs:
                            hits_f.write(f"#   {k}={regs[k]}\n")
                    hits_f.flush()
                    break
                process.Continue()
        elif st in (lldb.eStateExited, lldb.eStateCrashed, lldb.eStateDetached):
            hits_f.write(f"# process state event: {st}\n")
            break

    stop_flag.set()
    th_serial.join(timeout=10)

    hits_f.write(f"# TOTAL HITS: {hit_count}\n")
    hits_f.write(f"# LAST 10 VALUES: {[hex(v) if v is not None else None for v in last_values]}\n")
    hits_f.close()

    with transcript_lock:
        txt = bytes(transcript)
    with open(serial_log_full, "w") as f:
        f.write(txt.decode(errors="replace"))
    with open(serial_log_short, "w") as f:
        f.write("\n".join(txt.decode(errors="replace").splitlines()[-80:]))

    process.Kill()
    cleanup_proc()
    if os.path.exists(serial_sock):
        try:
            os.remove(serial_sock)
        except Exception:
            pass

    fault_in_serial = FAULT_RE.search(txt) is not None
    print(f"[attempt {attempt_no}] total watchpoint hits: {hit_count} "
          f"fault_seen_in_serial={fault_in_serial}")

    result.update(hit_count=hit_count, fault_seen=fault_in_serial, ok=True,
                  last_values=last_values)
    return result


# ---------------- single-attempt driver (c4-68c5, no auto-retry) ----------------
MARGIN1 = int(os.environ.get("C468C5_MARGIN", "25"))
TOTAL_TIMEOUT1 = int(os.environ.get("C468C5_TOTAL_TIMEOUT", "5400"))
WATCHED_TIMEOUT1 = int(os.environ.get("C468C5_WATCHED_TIMEOUT", "3600"))
results = []
r1 = run_attempt(1, MARGIN1, total_timeout=TOTAL_TIMEOUT1, watched_timeout=WATCHED_TIMEOUT1)
results.append(r1)

print("=== SUMMARY ===")
for i, r in enumerate(results, 1):
    print(f"attempt {i}: armed={r['armed']} armed_at_count={r['armed_at_count']} "
          f"hit_count={r['hit_count']} fault_seen={r['fault_seen']} "
          f"last_values={[hex(v) if v is not None else None for v in r['last_values']]}")
    print(f"  hits_log={r['hits_log']}")
    print(f"  serial_log_full={r['serial_log_full']}")
    print(f"  qemu_log={r['qemu_log']}")

any_hits = any(r["hit_count"] > 0 for r in results)
print(f"FINAL: any_watchpoint_hits={any_hits}")
