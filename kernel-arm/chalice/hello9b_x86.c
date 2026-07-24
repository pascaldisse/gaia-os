// CHALICE C4-67 V3 probe -- REGISTER-indirect call through a real bound
// IAT slot (cross-image target: kernel32.dll's real GetStdHandle, the
// SAME slot hello9_x86.exe's own FF15 call already proves the floor binds
// correctly and durably, C4-65/66). Isolates the CALL-INSTRUCTION FORM
// (register-indirect, `mov reg,[slot]; call reg`) from hello9's own
// memory-indirect FF15 form, while holding the TARGET CLASS constant
// (cross-image) -- bisects "form" vs "target class" the other way from
// V2. `-lkernel32` pulls in the real Import Directory entry for
// GetStdHandle (zig's bundled mingw-w64 import lib, same convention
// mkpe32.sh/hello9_x86.c already uses); the naked `movl
// __imp__GetStdHandle@4, %reg` inline-asm reference (not a typed C
// extern) is deliberate -- a typed `extern void *x asm("__imp__...")`
// makes lld emit MinGW's runtime-pseudo-reloc auto-import indirection
// (an EXTRA load through a stub cell patched by a real Windows loader at
// process-load time, which this floor's own PeMapImage never performs),
// verified this atom via objdump -d (double `movl 0x402000,%eax; movl
// (%eax),%eax` for the typed-extern form vs a clean single `movl
// 0x402040,%eax` for the naked-asm form actually used here). Verified
// live at build time (objdump -d, this atom): `movl 0x402040(IAT slot,
// RVA matches Import Table's own addr field),%esi` then `pushl $-11;
// calll *%esi` -- a genuine `FF D6` register-indirect opcode, no FF15
// anywhere in this call site. Per task's own stop condition, GetStdHandle
// may legitimately execute (kernelbase chain already bound, C4-66) --
// only a clean exit(53) matters, not eax's returned handle value; the
// stdcall callee's own `ret 4` pops the pushed arg, stack stays balanced
// either way. Exits via hello8's proven direct-syscall path.
long PeMain(void) {
    void *target;
    long dummy;
    __asm__ volatile ("movl __imp__GetStdHandle@4, %0" : "=r"(target));
    __asm__ volatile (
        "pushl $-11\n\t"
        "calll *%1\n\t"
        "movl %%eax, %0\n\t"
        : "=r"(dummy)
        : "r"(target)
        : "eax", "ecx", "edx", "memory", "cc"
    );
    (void)dummy;
    return 53;
}
