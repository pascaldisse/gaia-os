// CHALICE C4-5 done-line guest -- hello9_x86.exe: the x86-door WriteFile
// marker. hello8_x86.c proved the toolchain emits a genuine i386 (Machine
// 0x14C) PE32; THIS file is the actual C4-5 milestone guest: a freestanding
// PE32 whose Import Directory genuinely names KERNEL32.dll + 2 functions
// (GetStdHandle, WriteFile), resolved from zig's bundled mingw-w64 kernel32
// import lib (mkpe32.sh: `-lkernel32`) -- same real-import-table pattern as
// hello6.c on the aarch64 side, but i386/stdcall. No CRT, custom entry
// PeMain, returns 9 after printing the marker.
//
// LLP64/PE32 scar law (hello3.c/hello6.c lineage): PE-side `unsigned long`
// is 32-bit on BOTH windows-gnu targets -- on i386 every pointer is 32-bit
// too, so DWORD == unsigned long == 32 bits here, and the WriteFile
// prototype widths below are exact. i386 kernel32 exports are __stdcall
// (callee-pops, decorated _WriteFile@20/_GetStdHandle@4 in the import lib);
// the explicit __stdcall below is what makes the call sites match the real
// kernel32 ABI. __declspec(dllimport) is what makes lld emit the genuine
// Import Directory entry (hello6.c's own receipt) -- verify with:
//   llvm-objdump -p hello9_x86.exe   (look for "DLL Name: kernel32.dll")
//
// No aggregate initializers (SCARS law, hello6.c: `= {...}` at -O0 on a
// freestanding target lowers to a memcpy libcall this link doesn't have);
// the static string below is a plain .rdata init, same as hello6's msg.

typedef unsigned long       DWORD;    // 32-bit on this i386 PE32 target
typedef void                *HANDLE;
typedef const void           *LPCVOID;
typedef int                  BOOL;

#define STD_OUTPUT_HANDLE ((DWORD)-11)

__declspec(dllimport) HANDLE __stdcall GetStdHandle(DWORD nStdHandle);
__declspec(dllimport) BOOL   __stdcall WriteFile(HANDLE hFile, LPCVOID lpBuffer,
                                                 DWORD nNumberOfBytesToWrite,
                                                 DWORD *lpNumberOfBytesWritten,
                                                 void *lpOverlapped);

long PeMain(void) {
    static const char msg[] = "hello9: the door opens\n";
    DWORD written = 0;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), msg, sizeof(msg) - 1, &written, 0);
    return 9;
}
