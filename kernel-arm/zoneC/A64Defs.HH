// A64Defs.HH — aarch64 register-role constants for the HolyC compiler port.
// Per BACKA64-DESIGN.md section 2. This header REPLACES the x86 REG_*
// definitions from the kernel headers: in the hosted/ARM build it must be
// included INSTEAD of (never after) the x86 definitions, since HolyC does
// not permit silent #define redefinition.
//
// The arch-neutral optimizer passes (OptPass3/4/5/6), CInit, PrsLib, OptLib
// and PrsExp reference these names symbolically; only the values change.
//
// Mapping (design section 2):
//   accumulator / result ("RAX role")  -> x0
//   frame pointer        ("RBP role")  -> x29 (AAPCS64 FP)
//   stack pointer        ("RSP role")  -> SP, register number 31
//   OptPass3 allocatable pool          -> x19..x28 (callee-saved, 10 regs)
//   x18 is platform-reserved and must NEVER appear in generated code.

#define REG_RAX		0	// x0: primary accumulator; F64 results as raw bits
#define REG_RBP		29	// x29: frame pointer
#define REG_RSP		31	// SP (immediate-form addressing only; reg-form 31 = XZR)
#define REG_REGS_NUM	32	// aarch64 GPR index space (x0..x30 + SP)

// OptPass3 pool bounds. The x86 code marks non-allocatable registers by
// explicit REG_* checks; the ARM build's pool is the contiguous callee-saved
// range below. C4 audit item: verify OptPass3's mask logic honors these
// (it may need a small patch where it hardcodes x86 non-pool registers).
#define REG_A64_POOL_LO	19	// x19: first OptPass3-allocatable register
#define REG_A64_POOL_HI	28	// x28: last OptPass3-allocatable register
