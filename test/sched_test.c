/* Host-testable unit tests for the pure kernel modules.
 * Runs on the dev machine AND in CI (before the QEMU boot test). */

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "nk.h"

extern int sched_pick_next(int current);
extern char kbd_decode(uint8_t sc);

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else printf("ok:   %s\n", msg); \
} while (0)

int main(void) {
    /* --- scheduler: round-robin skips non-runnable --- */
    memset(tasks, 0, sizeof(tasks));
    tasks[0].state = TASK_RUNNABLE;
    tasks[1].state = TASK_EMPTY;
    tasks[2].state = TASK_RUNNABLE;
    tasks[3].state = TASK_EMPTY;

    int n1 = sched_pick_next(-1);
    CHECK(n1 == 0, "first pick finds task 0");
    int n2 = sched_pick_next(n1);
    CHECK(n2 == 2, "round-robin skips empty task 1");
    int n3 = sched_pick_next(n2);
    CHECK(n3 == 0, "wraps around to task 0");

    /* --- scheduler: no runnable tasks -> -1 --- */
    memset(tasks, 0, sizeof(tasks));
    tasks[1].state = TASK_ZOMBIE;
    CHECK(sched_pick_next(-1) == -1, "no runnable task yields -1");

    /* --- scheduler: zombie tasks are skipped --- */
    memset(tasks, 0, sizeof(tasks));
    tasks[0].state = TASK_ZOMBIE;
    tasks[1].state = TASK_RUNNABLE;
    tasks[2].state = TASK_ZOMBIE;
    tasks[3].state = TASK_RUNNABLE;
    int n4 = sched_pick_next(-1);
    int n5 = sched_pick_next(n4);
    CHECK(n4 == 1 && n5 == 3, "zombies skipped in rotation");

    /* --- keyboard decode: set-1 scancodes --- */
    CHECK(kbd_decode(0x1E) == 'a', "0x1E -> 'a'");
    CHECK(kbd_decode(0x2C) == 'z', "0x2C -> 'z'");
    CHECK(kbd_decode(0x39) == ' ', "0x39 -> space");
    CHECK(kbd_decode(0x0B) == '0', "0x0B -> '0'");
    CHECK(kbd_decode(0x01) == 27, "0x01 -> escape");
    CHECK(kbd_decode(0x80 + 0x1E) == 0, "break codes ignored");
    CHECK(kbd_decode(0xE0) == 0, "extended prefix ignored");

    if (failures) { printf("\n%d failure(s)\n", failures); return 1; }
    printf("\nall host tests passed\n");
    return 0;
}
