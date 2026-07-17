// CHALICE C3b Atom 3B — fake-ntdll.dll: the dress-rehearsal ntdll.
// Exports the required name set (SLICE3-SPEC Architecture verdict 3) so
// nt_boot_process (zoneA/ntboot.c) has something real to wire the unixlib
// passthrough into and enter, before any stock Wine ntdll exists (Atom 3C).
// No DllMain: FakeNtdllEntry exists only so the linker has a valid
// AddressOfEntryPoint (mirrors minidll.c's MiniEntry convention; never
// actually invoked -- NtMapViewOfSection/nt_boot_process never call a DLL
// entry point, SLICE3-SPEC Atom 3A verdict).
// LLP64 note: aarch64-windows-gnu `unsigned long` = 32-bit. The unixlib
// dbg_write args struct below uses an explicit `unsigned long long len`
// (SCARS law) to match zoneA/ntboot.c's kernel-side (LP64, 8B `unsigned
// long`) struct byte-for-byte.
#include "ntids.h"

// ---- Nt* stub (ntstub.S), needed for the NtContinue leap out of
// LdrInitializeThunk. ----
extern long NtContinue(void *ContextRecord, unsigned char TestAlert);

// ---- data exports: kernel (ntboot.c's nt_boot_process) WRITES these two
// after mapping this DLL, before entering LdrInitializeThunk (Architecture
// verdict 3: "unix side looks up and writes *__wine_syscall_dispatcher /
// *__wine_unixlib_handle"). Zero-initialized (BSS) until then. ----
__declspec(dllexport) void *__wine_syscall_dispatcher;
__declspec(dllexport) void *__wine_unixlib_handle;
// LdrSystemDllInitBlock: data export only (Architecture verdict 3's
// "ret stubs / data export" split) -- dummy payload, nothing here reads it.
__declspec(dllexport) unsigned long long LdrSystemDllInitBlock;

typedef long (*nt_unix_dispatch_fn)(void *handle, unsigned int code, void *args);

// __wine_unix_call_dispatcher: forwards (handle, code, args) straight to
// whatever kernel wrote into __wine_syscall_dispatcher -- SLICE3-SPEC
// bullet 3's "Wine signature (handle, code, args) -> table[code](args)"
// is realized kernel-side (zoneA/ntboot.c's nt_unix_call_shim); this
// export is the PE-side forwarding thunk callers (LdrInitializeThunk,
// below) actually invoke.
__declspec(dllexport) long __wine_unix_call_dispatcher(void *handle, unsigned int code, void *args) {
    nt_unix_dispatch_fn fn = (nt_unix_dispatch_fn)__wine_syscall_dispatcher;
    if (!fn) return 0xC0000002L;   // STATUS_NOT_IMPLEMENTED -- not wired yet
    return fn(handle, code, args);
}

// Mirrors zoneA/ntboot.c's `struct nt_unix_dbg_write_args` byte-for-byte
// (pointer + unsigned-long-long len, both sides 8B fields, natural layout).
struct dbg_write_args { const char *str; unsigned long long len; };

// LdrInitializeThunk: entered by nt_boot_process (pe_enter1) with
// x0=&pushed_ctx, x18=teb (Architecture verdict 2). Real loader_init work
// is out of scope (Atom 3B is Phase-0 only) -- body proves the unixlib
// call path with one dbg_write, then unwinds via NtContinue(ctx, TRUE)
// straight to RtlUserThreadStart, per Architecture verdict 2's "PE side
// runs loader_init, then NtContinue(ctx, TRUE) unwinds to
// RtlUserThreadStart".
__declspec(dllexport) long LdrInitializeThunk(void *context) {
    static const char msg[] = "CHALICE: thunk enters\n";
    struct dbg_write_args args;
    args.str = msg;
    args.len = sizeof(msg) - 1;
    __wine_unix_call_dispatcher(__wine_unixlib_handle, NT_UNIXCALL_DBG_WRITE, &args);
    NtContinue(context, 1);
    return 0;   // unreachable -- NtContinue never returns
}

// RtlUserThreadStart: lands here via NtContinue's raw leap (Context->Pc),
// so its own x0/x1 registers ARE Context->X0/X1 (exe_entry, arg) --
// ordinary AAPCS64 two-param C entry, no special calling convention (same
// pattern hello3.c's Part2(cookie) already proves for a one-param landing
// site). "tail-call x0 as entry" (SLICE3-SPEC): just calls it.
__declspec(dllexport) long RtlUserThreadStart(long (*entry)(void), void *arg) {
    (void)arg;
    return entry();
}

// ---- ret-stub exports (Architecture verdict 3: "ret stubs"). Nothing in
// this dress rehearsal calls these; they exist purely so the required
// name set resolves in the export table. ----
__declspec(dllexport) void KiUserApcDispatcher(void) {}
__declspec(dllexport) void KiUserExceptionDispatcher(void) {}
__declspec(dllexport) void KiUserCallbackDispatcher(void) {}
__declspec(dllexport) void DbgUiRemoteBreakin(void) {}
__declspec(dllexport) void __wine_ctrl_routine(void) {}

long FakeNtdllEntry(void) { return 1; }
