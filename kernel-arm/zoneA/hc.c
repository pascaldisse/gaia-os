// GAIA-OS Zone C seed — SeedC: HolyC-subset JIT compiler, aarch64, in-kernel.
// The Temple's soul: type HolyC at the prompt, it becomes machine code, it runs.
// Subset: I64 everywhere (pointers are I64), functions, if/else/while/for,
// return, globals, locals, full C operator set, "fmt",args; print statements,
// recursion, calls into kernel builtins (Print, MAlloc, Spawn, ...).
// Full ZealC front end + BackA64 replaces this at M4; this is the living seed.
#include "kernel.h"

typedef long long i64;

// ---------------------------------------------------------------- utils ----
static int hstreq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}
static void hscpy(char *d, const char *s, int max) {
    int i = 0;
    while (s[i] && i < max - 1) { d[i] = s[i]; i++; }
    d[i] = 0;
}
static void pdec(i64 v) {
    char b[22]; int i = 21; b[21] = 0;
    u64 u = v < 0 ? (u64)(-v) : (u64)v;
    do { b[--i] = '0' + u % 10; u /= 10; } while (u);
    if (v < 0) b[--i] = '-';
    puts(&b[i]);
}

// ---------------------------------------------------------- emit buffers ----
typedef struct { u32 *buf; int pos, max; } Ectx;
static Ectx ec_main, ec_fn, *E;

static int errf;
static int line_no;
static void herr(const char *m, const char *detail) {
    if (errf) return;
    errf = 1;
    puts("hc: line "); pdec(line_no); puts(": "); puts(m);
    if (detail) { puts(" '"); puts(detail); puts("'"); }
    puts("\n");
}

static void emit(u32 w) {
    if (E->pos >= E->max) { herr("code buffer full", 0); return; }
    E->buf[E->pos++] = w;
}
static u32 *here(void) { return &E->buf[E->pos]; }

// ------------------------------------------------------- a64 encoders ----
static void e_movz(int rd, u32 v, int hw) { emit(0xD2800000 | (u32)hw << 21 | (v & 0xFFFF) << 5 | rd); }
static void e_movk(int rd, u32 v, int hw) { emit(0xF2800000 | (u32)hw << 21 | (v & 0xFFFF) << 5 | rd); }
static void e_imm64(int rd, u64 v) {                       // minimal length
    e_movz(rd, v & 0xFFFF, 0);
    if ((v >> 16) & 0xFFFF) e_movk(rd, (v >> 16) & 0xFFFF, 1);
    if ((v >> 32) & 0xFFFF) e_movk(rd, (v >> 32) & 0xFFFF, 2);
    if ((v >> 48) & 0xFFFF) e_movk(rd, (v >> 48) & 0xFFFF, 3);
}
static void e_addr64(int rd, u64 v) {                      // fixed 4 (patchable)
    e_movz(rd, v & 0xFFFF, 0);         e_movk(rd, (v >> 16) & 0xFFFF, 1);
    e_movk(rd, (v >> 32) & 0xFFFF, 2); e_movk(rd, (v >> 48) & 0xFFFF, 3);
}
static void e_push(int rt) { emit(0xF8000C00 | 0x1F0u << 12 | 31u << 5 | rt); } // str rt,[sp,#-16]!
static void e_pop(int rt)  { emit(0xF8400400 | 16u << 12 | 31u << 5 | rt); }    // ldr rt,[sp],#16
static void e_addr_(int rd, int rn, int rm) { emit(0x8B000000 | rm << 16 | rn << 5 | rd); }
static void e_subr(int rd, int rn, int rm)  { emit(0xCB000000 | rm << 16 | rn << 5 | rd); }
static void e_mul(int rd, int rn, int rm)   { emit(0x9B007C00 | rm << 16 | rn << 5 | rd); }
static void e_sdiv(int rd, int rn, int rm)  { emit(0x9AC00C00 | rm << 16 | rn << 5 | rd); }
static void e_msub(int rd, int rn, int rm, int ra) { emit(0x9B008000 | rm << 16 | ra << 10 | rn << 5 | rd); }
static void e_and(int rd, int rn, int rm)   { emit(0x8A000000 | rm << 16 | rn << 5 | rd); }
static void e_orr(int rd, int rn, int rm)   { emit(0xAA000000 | rm << 16 | rn << 5 | rd); }
static void e_eor(int rd, int rn, int rm)   { emit(0xCA000000 | rm << 16 | rn << 5 | rd); }
static void e_lsl(int rd, int rn, int rm)   { emit(0x9AC02000 | rm << 16 | rn << 5 | rd); }
__attribute__((unused)) static void e_lsr(int rd, int rn, int rm)   { emit(0x9AC02400 | rm << 16 | rn << 5 | rd); }
static void e_asr(int rd, int rn, int rm)   { emit(0x9AC02800 | rm << 16 | rn << 5 | rd); }
static void e_mvn(int rd, int rm)  { emit(0xAA200000 | rm << 16 | 31u << 5 | rd); }  // orn rd,xzr,rm
static void e_neg(int rd, int rm)  { emit(0xCB000000 | rm << 16 | 31u << 5 | rd); }
static void e_cmp(int rn, int rm)  { emit(0xEB00001F | rm << 16 | rn << 5); }
static void e_cmp0(int rn)         { emit(0xF100001F | rn << 5); }
static void e_cset(int rd, int cond) { emit(0x9A9F07E0 | (u32)(cond ^ 1) << 12 | rd); }
static void e_ldr(int rt, int rn, int off) { emit(0xF9400000 | (u32)(off / 8) << 10 | rn << 5 | rt); }
static void e_str(int rt, int rn, int off) { emit(0xF9000000 | (u32)(off / 8) << 10 | rn << 5 | rt); }
static void e_addi(int rd, int rn, int imm) { emit(0x91000000 | (u32)imm << 10 | rn << 5 | rd); }
static void e_subi(int rd, int rn, int imm) { emit(0xD1000000 | (u32)imm << 10 | rn << 5 | rd); }
static void e_blr(int rn) { emit(0xD63F0000 | rn << 5); }
static void e_ret(void)   { emit(0xD65F03C0); }

static u32 *e_b(void)      { u32 *p = here(); emit(0x14000000); return p; }
static u32 *e_cbz0(void)   { u32 *p = here(); emit(0xB4000000); return p; }  // cbz x0
static void patch_b(u32 *p, u32 *target) {
    i64 off = target - p;
    if ((*p & 0xFC000000u) == 0x14000000u)
        *p = 0x14000000u | ((u32)off & 0x3FFFFFF);
    else
        *p = (*p & 0xFF00001Fu) | (((u32)off & 0x7FFFF) << 5);
}

// cond codes: eq=0 ne=1 ge=10 lt=11 gt=12 le=13
#define CC_EQ 0
#define CC_NE 1
#define CC_GE 10
#define CC_LT 11
#define CC_GT 12
#define CC_LE 13

static void e_prologue(void) {
    emit(0xA9BF7BFD);            // stp x29,x30,[sp,#-16]!
    e_subi(31, 31, 512);         // sub sp,sp,#512
    emit(0x910003FD);            // mov x29,sp
}
static void e_epilogue(void) {
    emit(0x910003BF);            // mov sp,x29
    e_addi(31, 31, 512);
    emit(0xA8C17BFD);            // ldp x29,x30,[sp],#16
    e_ret();
}

// --------------------------------------------------------------- lexer ----
enum { T_EOF, T_NUM, T_STR, T_ID, T_PUNCT };
enum { P_EQ = 256, P_NE, P_LE, P_GE, P_SHL, P_SHR, P_LAND, P_LOR,
       P_INC, P_DEC, P_ADDA, P_SUBA };

static const char *src;
static int tok, tpunct;
static i64 tval;
static char tid[64];
static char tstr[512];
static int tslen;

static int esc(int c) {
    switch (c) {
    case 'n': return '\n'; case 't': return '\t'; case 'r': return '\r';
    case '0': return 0;    case '\\': return '\\';
    case '"': return '"';  case '\'': return '\'';
    default: return c;
    }
}

static void next(void) {
    if (errf) { tok = T_EOF; return; }
    for (;;) {
        while (*src == ' ' || *src == '\t' || *src == '\n' || *src == '\r') {
            if (*src == '\n') line_no++;
            src++;
        }
        if (src[0] == '/' && src[1] == '/') { while (*src && *src != '\n') src++; continue; }
        if (src[0] == '/' && src[1] == '*') {
            src += 2;
            while (*src && !(src[0] == '*' && src[1] == '/')) { if (*src == '\n') line_no++; src++; }
            if (*src) src += 2;
            continue;
        }
        break;
    }
    if (!*src) { tok = T_EOF; return; }
    char c = *src;
    if (c >= '0' && c <= '9') {
        tok = T_NUM; tval = 0;
        if (c == '0' && (src[1] == 'x' || src[1] == 'X')) {
            src += 2;
            for (;;) {
                char h = *src;
                if (h >= '0' && h <= '9') tval = tval * 16 + (h - '0');
                else if (h >= 'a' && h <= 'f') tval = tval * 16 + (h - 'a' + 10);
                else if (h >= 'A' && h <= 'F') tval = tval * 16 + (h - 'A' + 10);
                else break;
                src++;
            }
        } else {
            while (*src >= '0' && *src <= '9') tval = tval * 10 + (*src++ - '0');
        }
        return;
    }
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
        int i = 0;
        while ((*src >= 'a' && *src <= 'z') || (*src >= 'A' && *src <= 'Z') ||
               (*src >= '0' && *src <= '9') || *src == '_') {
            if (i < 63) tid[i++] = *src;
            src++;
        }
        tid[i] = 0; tok = T_ID;
        return;
    }
    if (c == '"') {
        src++; tslen = 0;
        while (*src && *src != '"') {
            char x = *src++;
            if (x == '\\' && *src) x = (char)esc(*src++);
            if (tslen < 511) tstr[tslen++] = x;
        }
        if (*src == '"') src++; else herr("unterminated string", 0);
        tstr[tslen] = 0; tok = T_STR;
        return;
    }
    if (c == '\'') {
        src++;
        int x = *src ? *src++ : 0;
        if (x == '\\' && *src) x = esc(*src++);
        if (*src == '\'') src++;
        tok = T_NUM; tval = x;
        return;
    }
    tok = T_PUNCT; src++;
    switch (c) {
    case '=': if (*src == '=') { src++; tpunct = P_EQ; } else tpunct = '='; return;
    case '!': if (*src == '=') { src++; tpunct = P_NE; } else tpunct = '!'; return;
    case '<': if (*src == '=') { src++; tpunct = P_LE; }
              else if (*src == '<') { src++; tpunct = P_SHL; } else tpunct = '<'; return;
    case '>': if (*src == '=') { src++; tpunct = P_GE; }
              else if (*src == '>') { src++; tpunct = P_SHR; } else tpunct = '>'; return;
    case '&': if (*src == '&') { src++; tpunct = P_LAND; } else tpunct = '&'; return;
    case '|': if (*src == '|') { src++; tpunct = P_LOR; } else tpunct = '|'; return;
    case '+': if (*src == '+') { src++; tpunct = P_INC; }
              else if (*src == '=') { src++; tpunct = P_ADDA; } else tpunct = '+'; return;
    case '-': if (*src == '-') { src++; tpunct = P_DEC; }
              else if (*src == '=') { src++; tpunct = P_SUBA; } else tpunct = '-'; return;
    default: tpunct = c; return;
    }
}

typedef struct { const char *s; int line, tok, tpunct; i64 tval; char tid[64]; } LexSave;
static void lsave(LexSave *x) {
    x->s = src; x->line = line_no; x->tok = tok; x->tpunct = tpunct; x->tval = tval;
    hscpy(x->tid, tid, 64);
}
static void lrestore(LexSave *x) {
    src = x->s; line_no = x->line; tok = x->tok; tpunct = x->tpunct; tval = x->tval;
    hscpy(tid, x->tid, 64);
}

static int is_punct(int p) { return tok == T_PUNCT && tpunct == p; }
static void expect(int p, const char *what) {
    if (is_punct(p)) next();
    else herr("expected", what);
}
static int is_type_kw(void) {
    return tok == T_ID && (hstreq(tid, "I64") || hstreq(tid, "U64") ||
                           hstreq(tid, "U8") || hstreq(tid, "U0"));
}

// ------------------------------------------------------------- symbols ----
#define K_GLOB 1
#define K_FN   2
typedef struct { char name[32]; u64 addr; int kind; } Sym;
static Sym syms[192]; static int nsyms;
typedef struct { char name[32]; int off; } Loc;
static Loc locals[48]; static int nloc;
typedef struct { u32 *site; int sym, rd; } Patch;
static Patch patches[256]; static int npatch;

static unsigned char *data; static int dpos;
#define DATA_MAX (16 * 1024)

static int sym_find(const char *n) {
    for (int i = 0; i < nsyms; i++) if (hstreq(syms[i].name, n)) return i;
    return -1;
}
static int sym_add(const char *n, u64 addr, int kind) {
    if (nsyms >= 192) { herr("too many symbols", n); return 0; }
    hscpy(syms[nsyms].name, n, 32);
    syms[nsyms].addr = addr; syms[nsyms].kind = kind;
    return nsyms++;
}
static int loc_find(const char *n) {
    for (int i = 0; i < nloc; i++) if (hstreq(locals[i].name, n)) return i;
    return -1;
}
static void add_patch(u32 *site, int sym, int rd) {
    if (npatch >= 256) { herr("too many fwd refs", 0); return; }
    patches[npatch].site = site; patches[npatch].sym = sym; patches[npatch].rd = rd;
    npatch++;
}
static void patch_addr(u32 *site, int rd, u64 v) {
    site[0] = 0xD2800000 | ((u32)(v & 0xFFFF)) << 5 | rd;
    site[1] = 0xF2800000 | 1u << 21 | ((u32)((v >> 16) & 0xFFFF)) << 5 | rd;
    site[2] = 0xF2800000 | 2u << 21 | ((u32)((v >> 32) & 0xFFFF)) << 5 | rd;
    site[3] = 0xF2800000 | 3u << 21 | ((u32)((v >> 48) & 0xFFFF)) << 5 | rd;
}

// emit "load address of symbol s into rd" — patchable if not yet defined
static void e_symaddr(int rd, int si) {
    u32 *site = here();
    e_addr64(rd, syms[si].addr);
    if (syms[si].kind == K_FN && syms[si].addr == 0) add_patch(site, si, rd);
}

static u64 mkstr(void) {
    if (dpos + tslen + 1 > DATA_MAX) { herr("data buffer full", 0); return 0; }
    u64 a = (u64)&data[dpos];
    for (int i = 0; i <= tslen; i++) data[dpos + i] = (unsigned char)tstr[i];
    dpos += tslen + 1;
    return a;
}

// ------------------------------------------------------------ builtins ----
static void HcPrint(const char *f, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5) {
    u64 args[5] = { a1, a2, a3, a4, a5 };
    int ai = 0;
    for (; *f; f++) {
        if (*f == '%' && f[1]) {
            f++;
            if (*f == '%') { putc('%'); continue; }
            u64 v = ai < 5 ? args[ai++] : 0;
            if (*f == 'd') pdec((i64)v);
            else if (*f == 'x') puthex(v);
            else if (*f == 's') puts((const char *)v);
            else if (*f == 'c') putc((char)v);
            else { putc('%'); putc(*f); ai--; }
        } else putc(*f);
    }
}
static u64 HcTicks(void)  { return ticks; }
static u64 HcPeekB(u64 a) { return *(volatile unsigned char *)a; }
static void HcPokeB(u64 a, u64 v) { *(volatile unsigned char *)a = (unsigned char)v; }
static u64 HcPeekQ(u64 a) { return *(volatile u64 *)a; }
static void HcPokeQ(u64 a, u64 v) { *(volatile u64 *)a = v; }

static void add_builtin(const char *n, void *fn) { sym_add(n, (u64)fn, K_FN); }
static void builtins_init(void) {
    add_builtin("Print",   HcPrint);
    add_builtin("PutChar", putc);
    add_builtin("MAlloc",  MAlloc);
    add_builtin("Free",    Free);
    add_builtin("MemFree", MemFreeBytes);
    add_builtin("Ticks",   HcTicks);
    add_builtin("Yield",   Yield);
    add_builtin("Spawn",   Spawn);
    add_builtin("PeekB",   HcPeekB);
    add_builtin("PokeB",   HcPokeB);
    add_builtin("PeekQ",   HcPeekQ);
    add_builtin("PokeQ",   HcPokeQ);
    add_builtin("FsRead",  FsRead);
    add_builtin("FsWrite", FsWrite);
}

// -------------------------------------------------------------- parser ----
static int in_fn;
static void assign(void);
#define expr assign

static void var_load(const char *name) {
    int li = in_fn ? loc_find(name) : -1;
    if (li >= 0) { e_ldr(0, 29, locals[li].off); return; }
    int si = sym_find(name);
    if (si < 0) { herr("undefined", name); return; }
    if (syms[si].kind == K_GLOB) { e_imm64(9, syms[si].addr); e_ldr(0, 9, 0); }
    else e_symaddr(0, si);                       // function name as value
}

// store x0 into var; op: '=' plain, P_ADDA, P_SUBA
static void var_store(const char *name, int op) {
    int li = in_fn ? loc_find(name) : -1;
    if (li >= 0) {
        if (op != '=') {
            e_ldr(1, 29, locals[li].off);
            if (op == P_ADDA) e_addr_(0, 1, 0); else e_subr(0, 1, 0);
        }
        e_str(0, 29, locals[li].off);
        return;
    }
    int si = sym_find(name);
    if (si < 0 || syms[si].kind != K_GLOB) { herr("undefined var", name); return; }
    e_imm64(9, syms[si].addr);
    if (op != '=') {
        e_ldr(1, 9, 0);
        if (op == P_ADDA) e_addr_(0, 1, 0); else e_subr(0, 1, 0);
    }
    e_str(0, 9, 0);
}

static void incdec(const char *name, int op) {   // ++/--, result = new value
    var_load(name);
    if (op == P_INC) e_addi(0, 0, 1); else e_subi(0, 0, 1);
    var_store(name, '=');
}

static void primary(void) {
    if (errf) return;
    if (tok == T_NUM) { e_imm64(0, (u64)tval); next(); return; }
    if (tok == T_STR) { u64 a = mkstr(); e_imm64(0, a); next(); return; }
    if (is_punct('(')) { next(); expr(); expect(')', ")"); return; }
    if (tok == T_ID) {
        char name[64]; hscpy(name, tid, 64);
        next();
        if (is_punct('(')) {                      // call
            next();
            int nargs = 0;
            while (!is_punct(')') && tok != T_EOF) {
                expr(); e_push(0); nargs++;
                if (nargs > 8) { herr("too many args", name); return; }
                if (is_punct(',')) next(); else break;
            }
            expect(')', ")");
            for (int i = nargs - 1; i >= 0; i--) e_pop(i);
            int si = sym_find(name);
            if (si < 0) si = sym_add(name, 0, K_FN);   // forward ref
            if (syms[si].kind == K_GLOB) {             // call through var
                e_imm64(9, syms[si].addr); e_ldr(9, 9, 0);
            } else e_symaddr(9, si);
            e_blr(9);
            return;
        }
        if (is_punct(P_INC) || is_punct(P_DEC)) { int op = tpunct; next(); incdec(name, op); return; }
        var_load(name);
        return;
    }
    herr("unexpected token", 0);
    next();
}

static void unary(void) {
    if (errf) return;
    if (is_punct('-')) { next(); unary(); e_neg(0, 0); return; }
    if (is_punct('!')) { next(); unary(); e_cmp0(0); e_cset(0, CC_EQ); return; }
    if (is_punct('~')) { next(); unary(); e_mvn(0, 0); return; }
    if (is_punct('*')) { next(); unary(); e_ldr(0, 0, 0); return; }
    if (is_punct('&')) {
        next();
        if (tok != T_ID) { herr("& needs a name", 0); return; }
        char name[64]; hscpy(name, tid, 64); next();
        int li = in_fn ? loc_find(name) : -1;
        if (li >= 0) { e_addi(0, 29, locals[li].off); return; }
        int si = sym_find(name);
        if (si < 0) { herr("undefined", name); return; }
        if (syms[si].kind == K_GLOB) e_imm64(0, syms[si].addr);
        else e_symaddr(0, si);
        return;
    }
    if (is_punct(P_INC) || is_punct(P_DEC)) {
        int op = tpunct; next();
        if (tok != T_ID) { herr("++ needs a name", 0); return; }
        char name[64]; hscpy(name, tid, 64); next();
        incdec(name, op);
        return;
    }
    primary();
}

static int prec(int p) {
    switch (p) {
    case P_LOR: return 1;  case P_LAND: return 2;
    case '|': return 3;    case '^': return 4;   case '&': return 5;
    case P_EQ: case P_NE: return 6;
    case '<': case '>': case P_LE: case P_GE: return 7;
    case P_SHL: case P_SHR: return 8;
    case '+': case '-': return 9;
    case '*': case '/': case '%': return 10;
    default: return 0;
    }
}

static void binexpr(int minp) {
    unary();
    for (;;) {
        if (errf || tok != T_PUNCT) return;
        int pr = prec(tpunct);
        if (!pr || pr < minp) return;
        int op = tpunct;
        next();
        e_push(0);
        binexpr(pr + 1);
        e_pop(1);                                  // x1 = lhs, x0 = rhs
        switch (op) {
        case '+': e_addr_(0, 1, 0); break;
        case '-': e_subr(0, 1, 0); break;
        case '*': e_mul(0, 1, 0); break;
        case '/': e_sdiv(0, 1, 0); break;
        case '%': e_sdiv(10, 1, 0); e_msub(0, 10, 0, 1); break;
        case '&': e_and(0, 1, 0); break;
        case '|': e_orr(0, 1, 0); break;
        case '^': e_eor(0, 1, 0); break;
        case P_SHL: e_lsl(0, 1, 0); break;
        case P_SHR: e_asr(0, 1, 0); break;
        case P_EQ: e_cmp(1, 0); e_cset(0, CC_EQ); break;
        case P_NE: e_cmp(1, 0); e_cset(0, CC_NE); break;
        case '<':  e_cmp(1, 0); e_cset(0, CC_LT); break;
        case '>':  e_cmp(1, 0); e_cset(0, CC_GT); break;
        case P_LE: e_cmp(1, 0); e_cset(0, CC_LE); break;
        case P_GE: e_cmp(1, 0); e_cset(0, CC_GE); break;
        case P_LAND: e_cmp0(1); e_cset(1, CC_NE); e_cmp0(0); e_cset(0, CC_NE); e_and(0, 1, 0); break;
        case P_LOR:  e_cmp0(1); e_cset(1, CC_NE); e_cmp0(0); e_cset(0, CC_NE); e_orr(0, 1, 0); break;
        }
    }
}

static void assign(void) {
    if (errf) return;
    if (tok == T_ID && !is_type_kw()) {
        char name[64]; hscpy(name, tid, 64);
        LexSave sv; lsave(&sv);
        next();
        if (tok == T_PUNCT && (tpunct == '=' || tpunct == P_ADDA || tpunct == P_SUBA)) {
            int op = tpunct;
            next();
            assign();                              // right-assoc
            var_store(name, op);
            return;
        }
        lrestore(&sv);
    }
    binexpr(1);
}

// ---------------------------------------------------------- statements ----
static void stmt(void);

static void decl_stmt(void) {                      // "I64 x, y=5;" — local or global
    while (is_punct('*')) next();                  // pointer stars: all I64
    for (;;) {
        if (tok != T_ID) { herr("expected name in decl", 0); return; }
        char name[64]; hscpy(name, tid, 64);
        next();
        if (in_fn) {
            if (nloc >= 48) { herr("too many locals", name); return; }
            hscpy(locals[nloc].name, name, 32);
            locals[nloc].off = nloc * 8;
            nloc++;
        } else if (sym_find(name) < 0) {
            if (dpos + 8 > DATA_MAX) { herr("data full", 0); return; }
            dpos = (dpos + 7) & ~7;
            for (int i = 0; i < 8; i++) data[dpos + i] = 0;
            sym_add(name, (u64)&data[dpos], K_GLOB);
            dpos += 8;
        }
        if (is_punct('=')) { next(); assign(); var_store(name, '='); }
        if (is_punct(',')) { next(); continue; }
        break;
    }
    expect(';', ";");
}

static void stmt(void) {
    if (errf || tok == T_EOF) return;
    if (is_punct(';')) { next(); return; }
    if (is_punct('{')) {
        next();
        while (!is_punct('}') && tok != T_EOF && !errf) stmt();
        expect('}', "}");
        return;
    }
    if (tok == T_STR) {                            // "fmt", args;  — Temple print
        u64 a = mkstr();
        next();
        e_imm64(0, a); e_push(0);
        int n = 1;
        while (is_punct(',')) {
            next(); assign(); e_push(0);
            if (++n > 6) { herr("too many print args", 0); return; }
        }
        for (int i = n - 1; i >= 0; i--) e_pop(i);
        e_imm64(9, (u64)HcPrint); e_blr(9);
        expect(';', ";");
        return;
    }
    if (is_type_kw()) { next(); decl_stmt(); return; }
    if (tok == T_ID && hstreq(tid, "if")) {
        next(); expect('(', "("); expr(); expect(')', ")");
        u32 *j = e_cbz0();
        stmt();
        if (tok == T_ID && hstreq(tid, "else")) {
            u32 *j2 = e_b();
            patch_b(j, here());
            next(); stmt();
            patch_b(j2, here());
        } else patch_b(j, here());
        return;
    }
    if (tok == T_ID && hstreq(tid, "while")) {
        next();
        u32 *top = here();
        expect('(', "("); expr(); expect(')', ")");
        u32 *j = e_cbz0();
        stmt();
        patch_b(e_b(), top);
        patch_b(j, here());
        return;
    }
    if (tok == T_ID && hstreq(tid, "for")) {
        next(); expect('(', "(");
        if (!is_punct(';')) assign();
        expect(';', ";");
        u32 *top = here();
        u32 *j_end = 0;
        if (!is_punct(';')) { expr(); j_end = e_cbz0(); }
        expect(';', ";");
        u32 *j_body = e_b();
        u32 *inc_top = here();
        if (!is_punct(')')) assign();
        patch_b(e_b(), top);
        expect(')', ")");
        patch_b(j_body, here());
        stmt();
        patch_b(e_b(), inc_top);
        if (j_end) patch_b(j_end, here());
        return;
    }
    if (tok == T_ID && hstreq(tid, "return")) {
        next();
        if (!is_punct(';')) expr();
        e_epilogue();
        expect(';', ";");
        return;
    }
    assign();
    expect(';', ";");
}

static void funcdef(const char *name) {            // at '(' after "I64 Name"
    int si = sym_find(name);
    if (si < 0) si = sym_add(name, 0, K_FN);
    else if (syms[si].kind == K_FN && syms[si].addr) { herr("redefined", name); return; }
    Ectx *saved = E;
    E = &ec_fn;
    in_fn = 1; nloc = 0;
    syms[si].kind = K_FN;
    syms[si].addr = (u64)here();
    next();                                        // eat '('
    int nargs = 0;
    while (!is_punct(')') && tok != T_EOF && !errf) {
        if (is_type_kw()) next(); else { herr("expected arg type", name); break; }
        while (is_punct('*')) next();
        if (tok != T_ID) { herr("expected arg name", name); break; }
        hscpy(locals[nloc].name, tid, 32);
        locals[nloc].off = nloc * 8;
        nloc++; nargs++;
        next();
        if (is_punct(',')) next();
    }
    expect(')', ")");
    e_prologue();
    for (int i = 0; i < nargs && i < 8; i++) e_str(i, 29, i * 8);
    expect('{', "{");
    while (!is_punct('}') && tok != T_EOF && !errf) stmt();
    expect('}', "}");
    e_epilogue();                                  // fallthrough return
    in_fn = 0;
    E = saved;
}

static void toplevel(void) {
    if (errf || tok == T_EOF) return;
    if (is_type_kw()) {
        next();
        while (is_punct('*')) next();
        if (tok != T_ID) { herr("expected name", 0); return; }
        char name[64]; hscpy(name, tid, 64);
        LexSave sv; lsave(&sv);
        next();
        if (is_punct('(')) { funcdef(name); return; }
        lrestore(&sv);
        decl_stmt();
        return;
    }
    stmt();
}

// ---------------------------------------------------------------- run ----
static void iflush(void *base, int bytes) {
    u64 a = (u64)base & ~63ULL, end = (u64)base + (u64)bytes;
    for (u64 p = a; p < end; p += 64) asm volatile("dc cvau, %0" :: "r"(p));
    asm volatile("dsb ish");
    for (u64 p = a; p < end; p += 64) asm volatile("ic ivau, %0" :: "r"(p));
    asm volatile("dsb ish; isb");
}

#define CODE_MAIN_MAX (8 * 1024)                   // u32 slots
#define CODE_FN_MAX   (8 * 1024)

void HcRun(const char *s) {
    // fresh world per run; buffers stay allocated (spawned HolyC tasks live on)
    ec_main.buf = MAlloc(CODE_MAIN_MAX * 4); ec_main.pos = 0; ec_main.max = CODE_MAIN_MAX;
    ec_fn.buf   = MAlloc(CODE_FN_MAX * 4);   ec_fn.pos = 0;   ec_fn.max = CODE_FN_MAX;
    data = MAlloc(DATA_MAX); dpos = 0;
    if (!ec_main.buf || !ec_fn.buf || !data) { puts("hc: out of memory\n"); return; }
    nsyms = 0; nloc = 0; npatch = 0; errf = 0; in_fn = 0; line_no = 1;
    builtins_init();
    src = s;
    E = &ec_main;
    e_prologue();
    next();
    while (tok != T_EOF && !errf) toplevel();
    E = &ec_main;
    e_epilogue();
    for (int i = 0; i < npatch && !errf; i++) {
        if (syms[patches[i].sym].addr == 0) herr("undefined function", syms[patches[i].sym].name);
        else patch_addr(patches[i].site, patches[i].rd, syms[patches[i].sym].addr);
    }
    if (errf) return;
    iflush(ec_main.buf, ec_main.pos * 4);
    iflush(ec_fn.buf, ec_fn.pos * 4);
    ((void (*)(void))ec_main.buf)();
}
