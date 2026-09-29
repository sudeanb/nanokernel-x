/* Scheduler policy — pure, host-testable. Round-robin over the task table
 * starting after `current`; empty/zombie tasks are skipped. */

#include "nk.h"

task_t tasks[MAX_TASKS];

int sched_pick_next(int current) {
    for (int k = 1; k <= MAX_TASKS; k++) {
        int i = (current + k) % MAX_TASKS;
        if (tasks[i].state == TASK_RUNNABLE) return i;
    }
    return -1;
}

/* keyboard: PS/2 set-1 make codes → ASCII (lowercase). Extended codes
 * (0xE0 prefixes) are handled by the IRQ and never reach this table. */
char kbd_decode(uint8_t sc) {
    static const char map[128] = {
        0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
        '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
        0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
        0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
        '*', 0, ' ', 0,
    };
    if (sc >= 128) return 0;
    return map[sc];
}
