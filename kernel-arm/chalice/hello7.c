// CHALICE C3c Atom 3C-2r — hello7.exe: widen the proven Win32 surface past
// hello6's GetStdHandle/WriteFile/ExitProcess trio. Same real-import-table
// gate shape (KERNEL32.dll import via zig's bundled mingw-w64 import def,
// mkpe.sh: `-lkernel32`, no ntstub.S) -- but this exe pulls MORE kernel32
// exports: GetCommandLineA, GetModuleHandleA (NULL + "kernel32.dll"),
// GetProcAddress (+ an indirect call through the resolved pointer),
// GetProcessHeap/HeapAlloc/HeapFree (write-then-read a live heap block),
// GetCurrentProcessId, GetLastError/SetLastError roundtrip. Each API
// prints one short proof line via WriteFile to GetStdHandle(-11) so a
// serial-log gate (pe8_gate.py) can grep for every one individually.
//
// No aggregate initializers (SCARS law, hello6.c header note, unchanged
// here): every local is a scalar decl + explicit assignment loop, the only
// array literals are `static const char foo[] = "..."` (plain char-array
// string literals, same idiom hello6.c already uses, not the aggregate-
// struct/int-array kind that pulls in a memcpy-from-compound-literal CRT
// fallback at -O0).

typedef unsigned long       DWORD;    // LLP64: 32-bit on this target
typedef unsigned long long  ULONGLONG; // LLP64: pointer-target integer, 64-bit
typedef void                *HANDLE;
typedef const void           *LPCVOID;
typedef void                 *LPVOID;
typedef int                  BOOL;

#define STD_OUTPUT_HANDLE   ((DWORD)-11)
#define HEAP_ZERO_MEMORY    0x00000008UL

__declspec(dllimport) HANDLE GetStdHandle(DWORD nStdHandle);
__declspec(dllimport) BOOL   WriteFile(HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite,
                                        DWORD *lpNumberOfBytesWritten, void *lpOverlapped);
__declspec(dllimport) void   ExitProcess(unsigned int uExitCode);
__declspec(dllimport) char  *GetCommandLineA(void);
__declspec(dllimport) HANDLE GetModuleHandleA(const char *lpModuleName);
__declspec(dllimport) void  *GetProcAddress(HANDLE hModule, const char *lpProcName);
__declspec(dllimport) HANDLE GetProcessHeap(void);
__declspec(dllimport) LPVOID HeapAlloc(HANDLE hHeap, DWORD dwFlags, ULONGLONG dwBytes);
__declspec(dllimport) BOOL   HeapFree(HANDLE hHeap, DWORD dwFlags, LPVOID lpMem);
__declspec(dllimport) DWORD  GetCurrentProcessId(void);
__declspec(dllimport) DWORD  GetLastError(void);
__declspec(dllimport) void   SetLastError(DWORD dwErrCode);

static HANDLE g_out;

static unsigned long strlen_local(const char *s) {
    unsigned long n = 0;
    while (s[n]) n++;
    return n;
}

static void say(const char *s) {
    DWORD written = 0;
    WriteFile(g_out, s, (DWORD)strlen_local(s), &written, 0);
}

static void say_hex(ULONGLONG v) {
    char buf[19];
    const char *hexd = "0123456789ABCDEF";
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 0; i < 16; i++) buf[2 + i] = hexd[(v >> ((15 - i) * 4)) & 0xFULL];
    buf[18] = 0;
    say(buf);
}

static void say_dec(unsigned long v) {
    char b[21];
    int i = 20;
    b[i] = 0;
    do { b[--i] = '0' + (char)(v % 10); v /= 10; } while (v);
    say(&b[i]);
}

long PeMain(void) {
    g_out = GetStdHandle(STD_OUTPUT_HANDLE);

    // 1. GetCommandLineA
    char *cmdline = GetCommandLineA();
    say("CHALICE: GetCommandLineA -> \"");
    say(cmdline ? cmdline : "(null)");
    say("\"\n");

    // 2. GetModuleHandleA(NULL) -- own image base
    HANDLE hexe = GetModuleHandleA((const char *)0);
    say("CHALICE: GetModuleHandleA(NULL) -> ");
    say_hex((ULONGLONG)(unsigned long long)hexe);
    say("\n");

    // 3. GetModuleHandleA("kernel32.dll")
    HANDLE hk32 = GetModuleHandleA("kernel32.dll");
    say("CHALICE: GetModuleHandleA(kernel32.dll) -> ");
    say_hex((ULONGLONG)(unsigned long long)hk32);
    say("\n");

    // 4. GetProcAddress(kernel32, "WriteFile") + call through the pointer
    void *pWriteFile = GetProcAddress(hk32, "WriteFile");
    say("CHALICE: GetProcAddress(WriteFile) -> ");
    say_hex((ULONGLONG)(unsigned long long)pWriteFile);
    say("\n");
    if (pWriteFile) {
        typedef BOOL (*WriteFileFn)(HANDLE, LPCVOID, DWORD, DWORD *, void *);
        WriteFileFn wf = (WriteFileFn)pWriteFile;
        static const char msg2[] = "CHALICE: WriteFile-via-pointer speaks\n";
        DWORD written2 = 0;
        wf(g_out, msg2, sizeof(msg2) - 1, &written2, 0);
    }

    // 5. GetProcessHeap + HeapAlloc (write into it) + readback + HeapFree
    HANDLE heap = GetProcessHeap();
    say("CHALICE: GetProcessHeap -> ");
    say_hex((ULONGLONG)(unsigned long long)heap);
    say("\n");

    LPVOID block = HeapAlloc(heap, HEAP_ZERO_MEMORY, 64);
    say("CHALICE: HeapAlloc(64) -> ");
    say_hex((ULONGLONG)(unsigned long long)block);
    say("\n");

    if (block) {
        static const char payload[] = "chalice-heap-payload";
        char *p = (char *)block;
        unsigned long i = 0;
        while (payload[i]) { p[i] = payload[i]; i++; }
        p[i] = 0;
        say("CHALICE: heap payload readback -> \"");
        say(p);
        say("\"\n");

        BOOL freed = HeapFree(heap, 0, block);
        say("CHALICE: HeapFree -> ");
        say_dec((unsigned long)freed);
        say("\n");
    }

    // 6. GetCurrentProcessId
    DWORD pid = GetCurrentProcessId();
    say("CHALICE: GetCurrentProcessId -> ");
    say_dec((unsigned long)pid);
    say("\n");

    // 7. GetLastError/SetLastError roundtrip
    SetLastError(0x1234);
    DWORD err = GetLastError();
    say("CHALICE: SetLastError(0x1234)/GetLastError -> ");
    say_hex((ULONGLONG)err);
    say("\n");

    say("CHALICE: hello7 surface walk complete\n");
    ExitProcess(78);
    return -1;   // unreachable
}
