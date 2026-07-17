// CHALICE C3c Atom 3C-2j — hello6.exe: the real-import-table gate. Unlike
// hello3/4/5 (TEB-dispatch stubs, zero PE imports), this exe's Import
// Directory genuinely names KERNEL32.dll + 3 functions (GetStdHandle,
// WriteFile, ExitProcess), pulled from zig's bundled mingw-w64 kernel32
// import def (mkpe.sh: `-lkernel32`), NOT our own libgaia.a/gaia.def shim.
// Booted over STOCK Wine ntdll (pe7, nt_boot_process("hello6.exe",
// "ntdll.dll", 1), same as pe6), this forces the real chain: ntdll's own
// process-init import walk (LdrpLoadDll-class machinery, the same
// mechanism that already resolves kernel32.dll's OWN dependency set,
// C3c-2i) must now also resolve hello6.exe's import descriptor for
// KERNEL32.dll and its IAT before RtlUserThreadStart ever reaches PeMain
// below -- exercising the loader's import-address-table (IAT) patching
// path for the first time in this codebase, plus the real kernel32 code
// path for GetStdHandle/WriteFile/ExitProcess: GetStdHandle reads
// ProcessParameters->StandardOutput (set to NT_STDOUT_HANDLE=0x14,
// ntboot.c) and WriteFile on that handle must resolve through kernel32's
// own (kernelbase-forwarded) implementation down into our NtWriteFile
// service handler (ntsvc.c's 0x14 stdout special case).
//
// No aggregate initializers (SCARS law: at -O0 on this freestanding
// target, `= {...}` struct/array literals lower to a memcpy-from-compound-
// literal that pulls the mingw CRT's memcpy fallback into the link, ~145KB
// bloat measured empirically on hello4 -- this file has none to begin
// with, only scalar decls, so no risk here, noted for anyone extending
// it). __declspec(dllimport) on each prototype below is what makes lld
// emit a genuine Import Directory entry against `-lkernel32`'s import lib
// (as opposed to ntstub.S's TEB-slot indirect-call convention used by
// every earlier hello*.exe) -- verify with:
//   llvm-objdump -p hello6.exe   (look for "DLL Name: KERNEL32.dll")

typedef unsigned long       DWORD;    // LLP64: 32-bit on this target
typedef void                *HANDLE;
typedef const void           *LPCVOID;
typedef int                  BOOL;

#define STD_OUTPUT_HANDLE ((DWORD)-11)

__declspec(dllimport) HANDLE GetStdHandle(DWORD nStdHandle);
__declspec(dllimport) BOOL   WriteFile(HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite,
                                        DWORD *lpNumberOfBytesWritten, void *lpOverlapped);
__declspec(dllimport) void   ExitProcess(unsigned int uExitCode);

long PeMain(void) {
    HANDLE hstdout = GetStdHandle(STD_OUTPUT_HANDLE);

    static const char msg[] = "CHALICE: kernel32 speaks\n";
    DWORD written = 0;
    WriteFile(hstdout, msg, sizeof(msg) - 1, &written, 0);

    ExitProcess(77);
    return -1;   // unreachable
}
