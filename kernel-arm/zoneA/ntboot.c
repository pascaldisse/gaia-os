// CHALICE C3b Atom 3B — boot protocol (fake-ntdll dress rehearsal).
// nt_boot_process: Phase-0 emulation per SLICE3-SPEC. Maps ntdll+exe blobs
// via the 3A machinery (PeMapImage/nt_territory_alloc/nt_image_register,
// ntfile.c), wires the unixlib passthrough into the mapped ntdll's data
// exports, builds a 64K thread stack + extended TEB/PEB (graduates
// ntsvc.c's nt_build_teb, patches in the extra fields Atom 3B needs),
// pushes a synthetic CONTEXT (Architecture verdict 2 exact shape) and
// enters ntdll's LdrInitializeThunk via pe_enter1 (ntdisp.S). The booted
// chain's own NtTerminateProcess unwinds back here through the same
// nt_setjmp/nt_longjmp channel PeRun's use_nt=1 path already uses (proven
// slice-2/3A) -- no new unwind mechanism needed.
// LLP64 note: this file is aarch64-none-elf (LP64, kernel side) -- every
// `unsigned long` here is 8 bytes. PE-side payloads (chalice/fake-ntdll.c,
// chalice/hello5.c) are aarch64-windows-gnu (LLP64) and must use explicit
// `unsigned long long` for any pointer-target integer, per the established
// SCARS law.
#include "kernel.h"
#include "pemod.h"
#include "ntsvc.h"
#include "ntfile.h"
#include "ntboot.h"
#include "../chalice/ntids.h"

typedef unsigned char  u8;
typedef unsigned long  u64b;   // aarch64-none-elf LP64: `unsigned long` = 8B
typedef unsigned int   u32b;
typedef unsigned short u16b;

static void wr16(u8 *p, u16b v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }
static void wr32(u8 *p, u32b v) { for (int i = 0; i < 4; i++) p[i] = (u8)(v >> (8 * i)); }
static void wr64(u8 *p, u64b v) { for (int i = 0; i < 8; i++) p[i] = (u8)(v >> (8 * i)); }
static u64b rd64(const u8 *p) { u64b v = 0; for (int i = 0; i < 8; i++) v |= (u64b)p[i] << (8 * i); return v; }

static u64b strlen_local(const char *s) { u64b n = 0; while (s[n]) n++; return n; }

static void putdec_signed(long v) {
    if (v < 0) { putc('-'); v = -v; }
    u64b u = (u64b)v;
    char b[21]; int i = 20; b[i] = 0;
    do { b[--i] = '0' + (char)(u % 10); u /= 10; } while (u);
    puts(&b[i]);
}

static long boot_err(const char *msg) {
    puts("CHALICE: boot: "); puts(msg); puts("\n");
    return -1;
}

// CHALICE C3c Atom 3C-1: was manifest-only (nt_blob_manifest[] direct
// walk); now routes through ntfile.c's nt_blob_lookup_or_fs so stock
// ntdll.dll (too big to embed, lives on the RedSea-spirit disk -- fs.c/
// mkdisk.py) resolves the same way NtOpenFile's loader-path already does.
// Single mechanism, no parallel lookup (SLICE3-SPEC 3A precedent).
static int boot_blob_lookup(const char *name, const unsigned char **out_bytes, u64b *out_len) {
    const char *unused_name;
    return nt_blob_lookup_or_fs(name, out_bytes, out_len, &unused_name);
}

// =====================================================================
// unixlib passthrough (SLICE3-SPEC Atom 3B / Architecture verdict 3).
// nt_unix_call_shim IS what kernel writes into ntdll's *__wine_syscall_
// dispatcher; nt_unix_call_funcs (this array's address) IS what kernel
// writes into ntdll's *__wine_unixlib_handle. fake-ntdll.c's exported
// __wine_unix_call_dispatcher(handle, code, args) forwards straight to
// nt_unix_call_shim(handle, code, args) via that first write, closing the
// loop -- no TEB-slot indirection needed here (unlike the regular Nt*
// dispatch), this is a direct cross-toolchain C function-pointer call
// (same trick slice-1's IAT resolution already proved works: basic
// integer/pointer AAPCS is compatible between aarch64-windows-gnu callers
// and aarch64-none-elf kernel callees for the first 8 register args).
// =====================================================================
typedef long (*NtUnixCallFn)(void *args);

static long nt_unix_stub(void *args) { (void)args; return STATUS_NOT_IMPLEMENTED; }

// Mirrors Wine's real dbg_write_params shape: {const char *str; size_t len;}.
// PE side (fake-ntdll.c) declares the matching struct with an explicit
// `unsigned long long len` (LLP64 SCARS law) -- both sides 8B, natural
// layout matches without needing raw-offset paranoia (precedent:
// hello3.c/ntsvc.c's shared IO_STATUS_BLOCK struct-not-offsets approach).
struct nt_unix_dbg_write_args { const char *str; u64b len; };

static long nt_unix_dbg_write(void *args) {
    struct nt_unix_dbg_write_args *a = (struct nt_unix_dbg_write_args *)args;
    if (!a || !a->str) return STATUS_INVALID_PARAMETER;
    for (u64b i = 0; i < a->len; i++) putc(a->str[i]);
    return STATUS_SUCCESS;
}

// KUSER_SHARED_DATA.InterruptTime: kernel "IS the unix side" (HANDOFF) --
// write it fresh right here rather than a periodic-update task (nothing
// else in this dress rehearsal needs a live clock). Derived from the
// existing `ticks` heartbeat (task.c), scaled toward 100ns units (NT
// FILETIME/interrupt-time convention) -- not calibrated against real wall
// time (no RTC read here), just monotonic and nonzero, matching "KUSER-
// derived" without claiming clock-accurate semantics.
static long nt_unix_system_time_precise(void *args) {
    if (!args) return STATUS_INVALID_PARAMETER;
    u64b v = (u64b)ticks * 2500000ULL;   // ~ticks (250ms/tick, task.c) -> 100ns units
    u8 *k = (u8 *)(u64b)NT_KUSER_SHARED_DATA_ADDR;
    wr32(k + NT_KUSER_INTERRUPTTIME_OFF, (u32b)v);
    wr32(k + NT_KUSER_INTERRUPTTIME_OFF + 4, (u32b)(v >> 32));
    *(u64b *)args = rd64(k + NT_KUSER_INTERRUPTTIME_OFF);
    return STATUS_SUCCESS;
}

static NtUnixCallFn nt_unix_call_funcs[NT_UNIXCALL_COUNT];
static int nt_unix_call_funcs_inited;

static void nt_unix_call_funcs_ensure_init(void) {
    if (nt_unix_call_funcs_inited) return;
    nt_unix_call_funcs_inited = 1;
    for (int i = 0; i < NT_UNIXCALL_COUNT; i++) nt_unix_call_funcs[i] = nt_unix_stub;
    nt_unix_call_funcs[NT_UNIXCALL_DBG_WRITE]           = nt_unix_dbg_write;
    nt_unix_call_funcs[NT_UNIXCALL_SYSTEM_TIME_PRECISE] = nt_unix_system_time_precise;
}

// Wine signature (handle, code, args) -> table[code](args). `handle` here
// IS the table base pointer (single unixlib in this kernel, so "handle" ==
// &nt_unix_call_funcs, written verbatim into *__wine_unixlib_handle at
// wire time below) -- matches SLICE3-SPEC bullet 3 literally.
static long nt_unix_call_shim(void *handle, unsigned int code, void *args) {
    NtUnixCallFn *table = (NtUnixCallFn *)handle;
    if (!table || code >= NT_UNIXCALL_COUNT) return STATUS_NOT_IMPLEMENTED;
    NtUnixCallFn fn = table[code];
    if (!fn) return STATUS_NOT_IMPLEMENTED;
    return fn(args);
}

// =====================================================================
// TEB.NtTib / RTL_USER_PROCESS_PARAMETERS extra offsets (Atom 3B growth
// over ntsvc.c's nt_build_teb). NT_TIB64 (winternl.h): ExceptionList@0,
// StackBase@8, StackLimit@0x10, ..., Self@0x30 (ntsvc.c's existing
// TEB_NTTIB_SELF_OFF receipt cross-checks this layout). RTL_USER_PROCESS_
// PARAMETERS (rtltypes.h, well-known public offsets, StandardOutput@0x28
// already receipted in ntsvc.c): StandardInput@0x20, StandardError@0x30,
// CurrentDirectory(CURDIR: UNICODE_STRING+Handle)@0x38..0x50, DllPath
// UNICODE_STRING@0x50, ImagePathName UNICODE_STRING@0x60, CommandLine
// UNICODE_STRING@0x70, Environment ptr@0x80 -- fits PARAMS_SIZE 0x90
// (ntsvc.c, bumped this atom). UNICODE_STRING: Length(u16)@0,
// MaximumLength(u16)@2, Buffer(ptr,8-aligned)@8 (matches ntfile.c's
// US_LENGTH_OFF/US_BUFFER_OFF receipt).
// =====================================================================
#define TEB_NTTIB_STACKBASE_OFF    0x08
#define TEB_NTTIB_STACKLIMIT_OFF   0x10
#define TEB_PEB_OFF                0x60   // ntsvc.c receipt, duplicated (file-local const convention)
#define PEB_PROCESSPARAMETERS_OFF  0x20   // ntsvc.c receipt, duplicated
// CHALICE C3c-2h: PEB.ApiSetMap. Donor receipt (wine/include/winternl.h,
// reactos/sdk/include/wine/winternl.h -- both agree): field comment
// "ApiSetMap; /* 038/068 */" -- the TWO numbers are the 32-bit/64-bit
// build offsets of the SAME field, not two different fields. This kernel
// is LP64 (aarch64-none-elf) building an LP64 (aarch64-windows-gnu) PEB,
// i.e. the 64-bit branch -> 0x68, NOT 0x38 (that's the x86/WoW64 offset --
// verified against the donor before use, not assumed from any well-known-
// public-offset table). Cross-check: ntsvc.c's own PEB_PROCESSPARAMETERS_OFF
// (0x20/RTL_USER_PROCESS_PARAMETERS* @ "010/020") and its
// TlsExpansionCounter C_ASSERT anchor ("03c/070" -> 0x70) both follow the
// exact same 32-bit/64-bit dual-offset comment convention in the same
// donor header, one field after ApiSetMap -- consistent.
#define PEB_APISETMAP_OFF          0x68
#define PARAMS_STANDARDINPUT_OFF   0x20
#define PARAMS_STANDARDERROR_OFF   0x30
#define PARAMS_IMAGEPATHNAME_OFF   0x60
#define PARAMS_COMMANDLINE_OFF     0x70
#define US_LENGTH_OFF              0x00
#define US_MAXLENGTH_OFF           0x02
#define US_BUFFER_OFF              0x08

// ---- ARM64 CONTEXT offsets (ke.h _CONTEXT, LP64/_STRUCT64 -- full receipt
// lives in ntdisp.S's NtContinue comment; duplicated here, file-local
// const convention, same as every other struct-offset table in this
// codebase). X[i] @ 0x08 + i*8 (X0..X28,Fp=X29,Lr=X30); Sp@0x100; Pc@0x108;
// total size 0x390.
#define CTX_SIZE     0x390
#define CTX_X0_OFF   0x08
#define CTX_X1_OFF   0x10
#define CTX_X18_OFF  0x98
#define CTX_SP_OFF   0x100
#define CTX_PC_OFF   0x108

static void widen_ascii(const char *s, u16b *out, u64b n) {
    for (u64b i = 0; i < n; i++) out[i] = (u16b)(unsigned char)s[i];
}

// CHALICE C3c-2h: PEB.ApiSetMap wiring -- replicates wine's own unix-side
// load_apiset_dll (wine/dlls/ntdll/unix/loader.c:1726, donor receipt in
// HANDOFF) on our boot path. ntdll's import-name resolution
// (build_import_name, loader.c:756) reads PEB.ApiSetMap on EVERY api-ms-
// win-*/api-set import; left null, every such import resolves to
// STATUS_DLL_NOT_FOUND before any file is even opened -- confirmed live
// this session: kernel32.dll's own NtOpenFile fires, but no dependency's
// ever does, and ntdll cleanly reports "could not load kernel32.dll,
// status c0000135" and exits. Gated to stock=1 only by the caller (Atom
// 3B's fake-ntdll.c dress rehearsal never reads this PEB field).
static long nt_wire_apiset(u8 *peb) {
    const unsigned char *schema_bytes; u64b schema_len;
    if (!boot_blob_lookup("apisetschema.dll", &schema_bytes, &schema_len))
        return boot_err("apisetschema.dll blob not found");

    // .apiset's raw on-disk range -- apisetschema.dll is a data-only DLL
    // wine maps SEC_COMMIT flat (not SEC_IMAGE), so PointerToRawData is
    // already the right offset into the very same file-bytes buffer
    // boot_blob_lookup just returned (pe_find_raw_section receipt,
    // pemod.c/h) -- no PeMapImage, no separate copy or allocator call.
    unsigned long sec_off = 0, sec_size = 0;
    long rc = pe_find_raw_section(schema_bytes, (unsigned long)schema_len, ".apiset", &sec_off, &sec_size);
    if (rc != 0) return boot_err("apisetschema.dll: .apiset section not found");
    if (sec_size < 28) return boot_err("apisetschema.dll: .apiset section too small for API_SET_NAMESPACE header");

    // schema_bytes itself is a STABLE, persistent address: for the FS-
    // fallback path (the one stock=1 always takes -- apisetschema.dll is
    // never in the embedded manifest) it's a DLL-territory bump allocation
    // (ntfile.c's nt_blob_lookup_fs), never freed/overwritten for the
    // kernel's lifetime -- exactly the guarantee PEB.ApiSetMap needs
    // (read on every import for the process's whole life). Point straight
    // at the section's bytes inside it; no extra copy required.
    const u8 *map = schema_bytes + sec_off;
    u32b version = (u32b)map[0] | ((u32b)map[1] << 8) | ((u32b)map[2] << 16) | ((u32b)map[3] << 24);
    u32b mapsize  = (u32b)map[4] | ((u32b)map[5] << 8) | ((u32b)map[6] << 16) | ((u32b)map[7] << 24);
    // API_SET_NAMESPACE (winternl.h): Version@0, Size@4 (donor validation
    // this atom replicates: "map->Version==6 && map->Size<=VirtualSize").
    if (version != 6) return boot_err("apisetschema.dll: .apiset Version != 6");
    if ((u64b)mapsize > sec_size) return boot_err("apisetschema.dll: .apiset Size exceeds section VirtualSize");

    wr64(peb + PEB_APISETMAP_OFF, (u64b)map);
    puts("CHALICE: ApiSetMap wired addr="); puthex((u64b)map);
    puts(" size="); putdec_signed((long)mapsize); puts("\n");
    return 0;
}

#define NT_BOOT_STACK_SIZE 0x10000UL

long nt_boot_process(const char *exe_name, const char *ntdll_name, int stock, const char *label) {
    nt_blob_ensure_init();

    const unsigned char *ntdll_bytes, *exe_bytes;
    u64b ntdll_len, exe_len;
    if (!boot_blob_lookup(ntdll_name, &ntdll_bytes, &ntdll_len)) return boot_err("ntdll blob not found");
    if (!boot_blob_lookup(exe_name, &exe_bytes, &exe_len))       return boot_err("exe blob not found");

    // ---- Phase 0: map ntdll + exe via the 3A machinery (do_imports=0,
    // Architecture verdict 4: PE-side loader owns its own imports; there
    // is no Ldr walk here to resolve them against anyway). ----
    u64b ntdll_base = 0, ntdll_size = 0, ntdll_entry = 0;
    if (PeMapImage(ntdll_bytes, ntdll_len, "pe5-ntdll", nt_territory_alloc, 0, 1,
                    &ntdll_base, &ntdll_size, &ntdll_entry) != 0)
        return boot_err("ntdll map failed");
    nt_image_register(ntdll_name, ntdll_base, ntdll_size, ntdll_entry, 1);

    u64b exe_base = 0, exe_size = 0, exe_entry = 0;
    if (PeMapImage(exe_bytes, exe_len, "pe5-exe", nt_territory_alloc, 0, 1,
                    &exe_base, &exe_size, &exe_entry) != 0)
        return boot_err("exe map failed");
    nt_image_register(exe_name, exe_base, exe_size, exe_entry, 0);

    // ---- required ntdll exports (Architecture verdict 3 required-name
    // set; kernel only needs to LOOK UP the four it directly consumes --
    // the rest (KiUser*, DbgUiRemoteBreakin, __wine_ctrl_routine,
    // __wine_unix_call_dispatcher, LdrSystemDllInitBlock) just need to
    // EXIST in the mapped image so PE-side code that references them
    // resolves; nothing kernel-side calls them by address). Missing any
    // of the four -> clear serial error line, abort boot cleanly. ----
    u64b thunk_addr = 0, rtl_addr = 0, sdisp_addr = 0, uhandle_addr = 0, ucdisp_addr = 0;
    if (pe_find_named_export(ntdll_base, ntdll_size, "LdrInitializeThunk", &thunk_addr) != 0)
        return boot_err("ntdll missing export LdrInitializeThunk");
    if (pe_find_named_export(ntdll_base, ntdll_size, "RtlUserThreadStart", &rtl_addr) != 0)
        return boot_err("ntdll missing export RtlUserThreadStart");
    if (pe_find_named_export(ntdll_base, ntdll_size, "__wine_syscall_dispatcher", &sdisp_addr) != 0)
        return boot_err("ntdll missing export __wine_syscall_dispatcher");
    if (pe_find_named_export(ntdll_base, ntdll_size, "__wine_unixlib_handle", &uhandle_addr) != 0)
        return boot_err("ntdll missing export __wine_unixlib_handle");
    // CHALICE C3c-2k p2 diagnostic: cross-check our OWN kernel-side export
    // lookup against the exact export name the guest's find_named_export
    // (wine/dlls/ntdll/loader.c) is observed failing on ("ntdll.dll.
    // __wine_dbg_header", stub_entry_point abort). If this succeeds, the
    // export directory itself is intact in our mapped ntdll -- the guest-
    // side failure is a Wine loader-state bug, not a mapping/memory bug
    // on our end (disambiguates two very different next-gap fixes).
    if (stock) {
        u64b dbgh_addr = 0;
        long dbgh_rc = pe_find_named_export(ntdll_base, ntdll_size, "__wine_dbg_header", &dbgh_addr);
        puts("CHALICE: kernel-side __wine_dbg_header lookup rc="); putdec_signed(dbgh_rc);
        puts(" addr="); puthex(dbgh_addr); puts("\n");
    }
    // CHALICE C3c-2f p2: __wine_unix_call_dispatcher is a THIRD writable
    // data slot in STOCK ntdll (not a code export there -- confirmed via
    // `llvm-objdump -p stock/ntdll.dll`: its RVA 0x151750 sits directly
    // adjacent to __wine_syscall_dispatcher@0x151758 and
    // __wine_unixlib_handle@0x151760, a 3-pointer unixlib-wiring struct,
    // not 3 unrelated symbols) -- ONLY looked up/written when stock=1.
    // fake-ntdll.c (stock=0) exports this SAME NAME as a real CODE
    // function (its own hand-rolled forwarder, fake-ntdll.c:37) -- writing
    // through that address would overwrite live machine code, so the
    // lookup+write below is gated behind `stock` entirely, never touching
    // the stock=0 (pe5) path.
    if (stock && pe_find_named_export(ntdll_base, ntdll_size, "__wine_unix_call_dispatcher", &ucdisp_addr) != 0)
        return boot_err("ntdll missing export __wine_unix_call_dispatcher");

    // ---- unixlib wiring: pointer WRITES into the mapped ntdll's data
    // exports (Architecture verdict 3), no syscall. ----
    // CHALICE C3c-2d fix: *__wine_syscall_dispatcher is the REAL NT-service
    // dispatch slot (every Zw* stub's literal-pool target, verified against
    // stock/ntdll.dll -- ZwAllocateVirtualMemory's own literal resolves to
    // this exact export RVA) -- it must hold our x8-id table dispatcher
    // (nt_dispatch, ntdisp.S), NOT nt_unix_call_shim (a DIFFERENT donor
    // mechanism, (handle,code,args)-shaped, meant for *__wine_unixlib_handle
    // consumers only). Pre-fix this slot always got nt_unix_call_shim,
    // which happens to run harmlessly (STATUS_NOT_IMPLEMENTED, its own
    // ArgumentTable-less bounds check on a garbage "code") then `ret`s
    // straight back into the stub's own trailing `ret` -- the actual cause
    // of the ntdll.dll+0x64E24 self-loop (unrelated to the syscall-number/
    // x9 fix above, which never got a chance to run at all). fake-ntdll.c's
    // OWN hand-rolled __wine_unix_call_dispatcher (pe5/stock=0 dress
    // rehearsal only) still deliberately reads *__wine_syscall_dispatcher
    // as the unix-call shim by its own design (fake-ntdll.c comment), so
    // stock=0 keeps the old wiring unchanged -- only stock=1 (real Wine
    // ntdll, pe6) gets the fix.
    //
    // CHALICE C3c-2f p2: the C3c-2d fix above wired *__wine_syscall_dispatcher
    // correctly but left nt_unix_call_shim (built exactly for this
    // (handle,code,args) shape) with nowhere to plug in on the stock path
    // -- STOCK ntdll's own __wine_dbg_output (its always-on debug-channel
    // trace helper, called from inside RtlCreateAtomTable during heap init,
    // unconditional on any WINEDEBUG env var) calls straight through
    // *__wine_unix_call_dispatcher with x0=*__wine_unixlib_handle (our
    // nt_unix_call_funcs table, verified in the fault dump: x0=0x4021EA08
    // matches exactly), x1=code=2 (NT_UNIXCALL_DBG_WRITE), x2=argsptr --
    // this THIRD slot was never written, so it stayed zero (kernel BSS/
    // fresh page) and `blr x9` (x9=0) branched straight to address 0.
    // Fix: also write nt_unix_call_shim into *__wine_unix_call_dispatcher
    // when stock=1.
    nt_unix_call_funcs_ensure_init();
    wr64((u8 *)(u64b)sdisp_addr, stock ? (u64b)nt_dispatch : (u64b)nt_unix_call_shim);
    wr64((u8 *)(u64b)uhandle_addr, (u64b)nt_unix_call_funcs);
    if (stock) wr64((u8 *)(u64b)ucdisp_addr, (u64b)nt_unix_call_shim);
    if (stock) puts("CHALICE: stock ntdll mapped\n");

    // ---- 64K thread stack, DLL territory (same bump allocator as the DLL
    // images themselves -- SLICE3-SPEC: "alloc 64K thread stack in DLL
    // territory"). ----
    u64b stack_base = (u64b)nt_territory_alloc(NT_BOOT_STACK_SIZE);
    if (!stack_base) return boot_err("thread stack alloc failed");
    u64b stack_top = stack_base + NT_BOOT_STACK_SIZE;

    // ---- TEB/PEB floor: graduate ntsvc.c's nt_build_teb (today's
    // behavior: 4 trapdoors + PEB.ImageBaseAddress=image_base +
    // ProcessParameters.StandardOutput), then patch in Atom 3B's extras. ----
    u8 *teb = (u8 *)nt_build_teb((void *)exe_base, (void *)exe_entry, (unsigned long)exe_size);
    if (!teb) return boot_err("nt_build_teb failed");

    wr64(teb + TEB_NTTIB_STACKBASE_OFF, stack_top);     // StackBase = highest addr
    wr64(teb + TEB_NTTIB_STACKLIMIT_OFF, stack_base);   // StackLimit = lowest addr

    // ---- CHALICE C3c-2l Phase A: nt_build_teb only ever registers ONE
    // self-entry (the exe) in PEB.Ldr -- ntdll itself (mapped directly by
    // this file's own C code above, bypassing the guest's own
    // NtCreateSection/NtMapViewOfSection dependency-load path kernel32/
    // kernelbase go through) never gets a module-list entry. Receipt dump
    // BEFORE the fix, then Phase B: register it, dump again after. ----
    if (stock) {
        puts("CHALICE: ldr state before ntdll self-registration:\n");
        nt_ldr_dump(teb);
        u64b ntdll_namelen = strlen_local(ntdll_name);
        u16b *ntdll_wname = (u16b *)MAlloc((ntdll_namelen + 1) * 2);
        if (!ntdll_wname) return boot_err("ntdll ldr name alloc failed");
        widen_ascii(ntdll_name, ntdll_wname, ntdll_namelen);
        nt_ldr_add_module(teb, (void *)ntdll_base, (void *)ntdll_entry, ntdll_size,
                          ntdll_wname, ntdll_namelen);
        puts("CHALICE: ldr state after ntdll self-registration:\n");
        nt_ldr_dump(teb);
    }

    u8 *peb    = (u8 *)(u64b)rd64(teb + TEB_PEB_OFF);
    u8 *params = (u8 *)(u64b)rd64(peb + PEB_PROCESSPARAMETERS_OFF);
    wr64(params + PARAMS_STANDARDINPUT_OFF, NT_STDOUT_HANDLE);
    wr64(params + PARAMS_STANDARDERROR_OFF, NT_STDOUT_HANDLE);

    u64b namelen = strlen_local(exe_name);
    u16b *wname = (u16b *)MAlloc((namelen + 1) * 2);
    if (!wname) return boot_err("command line alloc failed");
    widen_ascii(exe_name, wname, namelen);
    wr16(params + PARAMS_IMAGEPATHNAME_OFF + US_LENGTH_OFF, (u16b)(namelen * 2));
    wr16(params + PARAMS_IMAGEPATHNAME_OFF + US_MAXLENGTH_OFF, (u16b)(namelen * 2));
    wr64(params + PARAMS_IMAGEPATHNAME_OFF + US_BUFFER_OFF, (u64b)wname);
    wr16(params + PARAMS_COMMANDLINE_OFF + US_LENGTH_OFF, (u16b)(namelen * 2));
    wr16(params + PARAMS_COMMANDLINE_OFF + US_MAXLENGTH_OFF, (u16b)(namelen * 2));
    wr64(params + PARAMS_COMMANDLINE_OFF + US_BUFFER_OFF, (u64b)wname);

    // ---- CHALICE C3c-2h: PEB.ApiSetMap, stock=1 only (Atom 3B's
    // fake-ntdll.c dress rehearsal never reads this field). Must land
    // before the thunk is entered -- ntdll's import-name resolution reads
    // it starting with kernel32.dll's own import walk, immediately after
    // "stock thunk enters". ----
    if (stock && nt_wire_apiset(peb) != 0) return -1;

    // ---- synthetic CONTEXT, pushed onto the new stack (16-aligned).
    // Architecture verdict 2 exact shape: X0=exe_entry, X1=arg(0), X18=teb,
    // Sp=stack_top, Pc=RtlUserThreadStart. ----
    u64b ctx_addr = (stack_top - CTX_SIZE) & ~0xFULL;
    u8 *ctx = (u8 *)ctx_addr;
    for (u64b i = 0; i < CTX_SIZE; i++) ctx[i] = 0;
    wr64(ctx + CTX_X0_OFF, exe_entry);
    wr64(ctx + CTX_X1_OFF, 0);
    wr64(ctx + CTX_X18_OFF, (u64b)teb);
    wr64(ctx + CTX_SP_OFF, stack_top);
    wr64(ctx + CTX_PC_OFF, rtl_addr);

    // ---- enter at LdrInitializeThunk (x0=&pushed_ctx, x18=teb). Runs on
    // the KERNEL's own C stack (pe_enter1, mirrors PeRun's pe_enter) until
    // LdrInitializeThunk itself calls NtContinue, which switches to
    // Context->Sp (the new 64K stack) and leaps to Context->Pc
    // (RtlUserThreadStart) -- never returns here on the success path.
    // NtTerminateProcess (called from deep inside the booted chain) longjmps
    // straight back to the nt_setjmp point below, same channel PeRun's
    // use_nt=1 path already proves (slice-2/3A). ----
    // ---- CHALICE C3c-2m: PROBE -- simulate the guest's OWN base-
    // discovery path (donor: wine/dlls/ntdll/loader.c build_ntdll_module,
    // quoted in HANDOFF) before entering the thunk. Wine's ntdll learns
    // ITS OWN module base by calling NtQueryVirtualMemory on an address it
    // already knows is inside itself (LdrInitializeThunk) and reading
    // meminfo.AllocationBase back -- every later find_named_export (incl.
    // KERNEL32.dll's import of ntdll.dll.__wine_dbg_header) keys off that
    // value. Reuse our OWN NtQueryVirtualMemory (ntfile.c) on thunk_addr,
    // the exact address the real loader queries, to see EXACTLY what the
    // guest sees -- no guessing, same tool, same input. ----
    if (stock) {
        unsigned char mbi[0x30];   // real Win64 sizeof(MEMORY_BASIC_INFORMATION), C3c-2m fix
        for (int i = 0; i < 0x30; i++) mbi[i] = 0;
        long qrc = NtQueryVirtualMemory(0, (void *)(u64b)thunk_addr, 0, mbi, sizeof(mbi), 0);
        u64b guest_base = rd64(mbi + 0x08 /* MBI_ALLOCATIONBASE_OFF */);
        int match = (guest_base == ntdll_base);
        u64b probe_addr = 0;
        long probe_rc = pe_find_named_export(guest_base, ntdll_size, "__wine_dbg_header", &probe_addr);
        puts("CHALICE PROBE: thunk="); puthex(thunk_addr);
        puts(" qrc="); putdec_signed(qrc);
        puts(" allocbase="); puthex(guest_base);
        puts(" ntdll_base="); puthex(ntdll_base);
        puts(" match="); putdec_signed(match);
        puts(" find_named_export_rc="); putdec_signed(probe_rc);
        puts(" addr="); puthex(probe_addr);
        puts("\n");
    }

    static unsigned long ntjmp[14];
    nt_exit_jmpbuf = ntjmp;
    long ret;
    if (nt_setjmp(ntjmp) == 0) {
        if (stock) puts("CHALICE: stock thunk enters\n");
        ret = pe_enter1((long (*)(void *))(u64b)thunk_addr, teb, (void *)ctx_addr);
    } else {
        ret = nt_exit_code;
    }
    nt_exit_jmpbuf = 0;

    puts(label); puts(" ret="); putdec_signed(ret); puts("\n");
    return ret;
}
