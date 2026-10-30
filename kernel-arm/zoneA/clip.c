// GAIA-OS Zone A — the clipboard. First slab of GAIA-OS proper.
//
// macOS equivalent: `pbcopy` (stdin -> pasteboard) / `pbpaste` (pasteboard ->
// stdout). Here the pasteboard is one static buffer in ring 0 — no process
// boundary, no IPC, no syscall, no handle. That absence IS the design: a
// freestanding single-address-space kernel cannot fail to copy because there
// is nothing to copy across. Every task (cooperative, same MMU, same heap)
// reads the same bytes by taking the same pointer.
//
//	ClipSet(s)    == pbcopy                 (replace contents)
//	ClipGet()     == pbpaste                (NUL-terminated contents)
//	ClipAppend(s) == pbpaste | pbcopy       (append; the seed for `clip >>`)
//	ClipLen()     == length of pbpaste      (no trailing newline, unlike mac)
//
// Static storage, not MAlloc: the clipboard must outlive any heap churn and
// stay valid across task switches with no lifetime bookkeeping.
#include "kernel.h"

#define CLIP_MAX 4096          // compile-time ceiling; truncate, never grow

static char clip_buf[CLIP_MAX + 1];    // +1 keeps the NUL reachable at CLIP_MAX
static u64  clip_len;                  // bytes used, NUL excluded

static u64 slen(const char *s) {
    u64 n = 0;
    while (s && s[n]) n++;
    return n;
}

// pbcopy: replace the pasteboard. Returns the number of bytes actually stored.
u64 ClipSet(const char *s) {
    if (!s) { clip_len = 0; clip_buf[0] = 0; return 0; }
    u64 n = slen(s);
    if (n > CLIP_MAX) n = CLIP_MAX;
    for (u64 i = 0; i < n; i++) clip_buf[i] = s[i];
    clip_buf[n] = 0;
    clip_len = n;
    return n;
}

// pbpaste: the contents, NUL terminated, valid until the next ClipSet/Append.
const char *ClipGet(void) {
    clip_buf[clip_len] = 0;            // paranoia: len and NUL stay in sync
    return clip_buf;
}

u64 ClipLen(void) {
    return clip_len;
}

// Append to the pasteboard (the `clip >>` seed). Returns bytes written, 0 if
// the buffer is already full or s is empty.
int ClipAppend(const char *s) {
    if (!s) return 0;
    u64 n = slen(s);
    if (!n || clip_len >= CLIP_MAX) return 0;
    if (n > CLIP_MAX - clip_len) n = CLIP_MAX - clip_len;
    for (u64 i = 0; i < n; i++) clip_buf[clip_len + i] = s[i];
    clip_len += n;
    clip_buf[clip_len] = 0;
    return (int)n;
}
