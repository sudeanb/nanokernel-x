/* nanokernel-x — shared declarations. */

#ifndef NK_H
#define NK_H

#include <stdint.h>
#include <stddef.h>

#define MAX_TASKS 4
#define KSTACK_SIZE 4096
#define TAPE_N 30000

typedef enum { TASK_EMPTY = 0, TASK_RUNNABLE, TASK_ZOMBIE } task_state_t;

typedef struct {
    uint32_t esp;              /* saved kernel stack pointer (regs frame) */
    task_state_t state;
    uint8_t kstack[KSTACK_SIZE];
    char id;                   /* debug letter */
} task_t;

extern task_t tasks[MAX_TASKS];

/* scheduler (pure, host-testable): returns index of next runnable task
 * after `current` in round-robin order, or -1 when none is runnable. */
int sched_pick_next(int current);

/* keyboard: PS/2 set-1 scancode → ASCII (press events only), 0 if none */
char kbd_decode(uint8_t scancode);

/* scheduler tick hook, called from the IRQ0 path each timer interrupt */
void sched_tick(void);

/* syscall dispatch (int 0x80): eax=nr, ebx=arg0, ecx=arg1, edx=arg2 */
void syscall_dispatch(uint32_t nr, uint32_t a, uint32_t b, uint32_t c);

extern volatile uint32_t g_ticks;

#endif
