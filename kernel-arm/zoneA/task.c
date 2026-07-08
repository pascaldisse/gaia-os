// GAIA-OS Zone A — cooperative round-robin tasks, the Temple way.
// Successor to ZealOS KTask.ZC + Sched.ZC. Yield() is canon: Terry's
// scheduler was cooperative; preemption is Babylon.
#include "kernel.h"

void swtch(u64 *old_sp_save, u64 new_sp);

#define TASK_STACK (64 * 1024)
#define MAX_NAME 32

typedef struct CTask {
    u64 sp;
    struct CTask *next;          // circular run queue
    const char *name;
    u64 stack_base;
} CTask;

static CTask boot_task;          // kmain's identity
static CTask *cur;

void TaskInit(void) {
    boot_task.name = "Adam";     // the first task is always Adam
    boot_task.next = &boot_task;
    cur = &boot_task;
}

void task_trampoline(void);

void Spawn(void (*fn)(void), const char *name) {
    CTask *t = MAlloc(sizeof(CTask));
    t->name = name;
    t->stack_base = (u64)MAlloc(TASK_STACK);
    u64 sp = (t->stack_base + TASK_STACK) & ~15ULL;

    // fabricate a swtch frame: x19 = entry fn, x30 = trampoline
    sp -= 96;
    u64 *f = (u64 *)sp;
    f[0] = (u64)fn;              // x19
    for (int i = 1; i < 10; i++) f[i] = 0;
    f[10] = 0;                   // x29
    f[11] = (u64)task_trampoline; // x30: swtch returns into trampoline
    t->sp = sp;

    t->next = cur->next;         // insert after current
    cur->next = t;
}

void task_exit(void) {
    // task returned: remove self from ring, switch away forever
    CTask *self = cur;
    CTask *p = cur;
    while (p->next != self) p = p->next;
    p->next = self->next;
    puts("[task exit: "); puts(self->name); puts("]\n");
    cur = self->next;
    u64 dummy;
    swtch(&dummy, cur->sp);      // never returns
}

void Yield(void) {
    CTask *prev = cur;
    cur = cur->next;
    if (cur == prev) return;     // alone in the ring
    swtch(&prev->sp, cur->sp);
}
