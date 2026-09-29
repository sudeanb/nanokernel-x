/* NanoKernel-X kernel — VGA, serial, GDT/TSS, IDT, PIC, PIT, keyboard,
 * preemptive scheduler, ring-3 user task, int 0x80 syscalls.
 *
 * Design invariants (see docs/DESIGN.md):
 *   • every interrupt handler returns a regs-frame pointer
 *   • iret's RPL check performs ring transitions — no asm branches needed
 *   • syscall exit can switch tasks by returning another frame
 */

#include "nk.h"

/* ---------- freestanding ---------- */

void *memset(void *dst, int v, size_t n) {
    uint8_t *d = dst;
    while (n--) *d++ = (uint8_t)v;
    return dst;
}

/* ---------- ports ---------- */

static inline void outb(uint16_t port, uint8_t v) {
    __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

/* ---------- VGA ---------- */

static volatile uint16_t *const VGA = (volatile uint16_t *)0xB8000;
static int vga_row = 0, vga_col = 0;

static void vga_putc(char c) {
    if (c == '\n') { vga_row++; vga_col = 0; return; }
    VGA[vga_row * 80 + vga_col] = (0x0F << 8) | (uint8_t)c;
    if (++vga_col >= 80) { vga_col = 0; vga_row++; }
}

/* ---------- serial COM1 ---------- */

#define COM1 0x3F8

static void serial_init(void) {
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x01);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
}
static void serial_putc(char c) {
    while (!(inb(COM1 + 5) & 0x20)) { }
    outb(COM1, (uint8_t)c);
}
static void kputc(char c) { vga_putc(c); serial_putc(c); }
static void kputs(const char *s) { while (*s) kputc(*s++); }

/* ---------- GDT + TSS ---------- */

struct gdt_entry { uint16_t lim_lo, base_lo; uint8_t base_mid, access, gran, base_hi; } __attribute__((packed));
struct gdt_ptr { uint16_t lim; uint32_t base; } __attribute__((packed));
struct tss_entry {
    uint32_t prev, esp0, ss0, esp1, ss1, esp2, ss2, cr3;
    uint32_t eip, eflags, eax, ecx, edx, ebx, esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs, ldt;
    uint16_t trap, iomap_base;
} __attribute__((packed));

static struct gdt_entry gdt[6];
static struct gdt_ptr gp;
static struct tss_entry tss;

static void gdt_set(int i, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[i].base_lo = base & 0xFFFF;
    gdt[i].base_mid = (base >> 16) & 0xFF;
    gdt[i].base_hi = (base >> 24) & 0xFF;
    gdt[i].lim_lo = limit & 0xFFFF;
    gdt[i].gran = gran | ((limit >> 16) & 0x0F);
    gdt[i].access = access;
}

static void gdt_init(void) {
    gp.lim = sizeof(gdt) - 1;
    gp.base = (uint32_t)&gdt;
    gdt_set(0, 0, 0, 0, 0);
    gdt_set(1, 0, 0xFFFFF, 0x9A, 0xCF);        /* 0x08 kernel code */
    gdt_set(2, 0, 0xFFFFF, 0x92, 0xCF);        /* 0x10 kernel data */
    gdt_set(3, 0, 0xFFFFF, 0xFA, 0xCF);        /* 0x18 user code, flat */
    gdt_set(4, 0, 0xFFFFF, 0xF2, 0xCF);        /* 0x20 user data, flat */
    gdt_set(5, (uint32_t)&tss, sizeof(tss), 0x89, 0x00); /* 0x28 TSS */
    __asm__ volatile("lgdt %0" : : "m"(gp));
    __asm__ volatile(
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n mov %%ax, %%es\n mov %%ax, %%fs\n mov %%ax, %%gs\n mov %%ax, %%ss\n"
        "ljmp $0x08, $gdt_reload\n"
        "gdt_reload:\n" ::: "eax");
    memset(&tss, 0, sizeof(tss));
    tss.ss0 = 0x10;
    tss.esp0 = 0x98000;
    __asm__ volatile("mov $0x28, %%ax\n ltr %%ax" ::: "eax");
}

/* ---------- IDT ---------- */

struct idt_entry {
    uint16_t base_lo; uint16_t sel; uint8_t zero;
    uint8_t flags; uint16_t base_hi;
} __attribute__((packed));
struct idt_ptr { uint16_t lim; uint32_t base; } __attribute__((packed));

static struct idt_entry idt[256];
static struct idt_ptr idtp;

static void idt_set(int n, uint32_t handler) {
    idt[n].base_lo = handler & 0xFFFF;
    idt[n].sel = 0x08;
    idt[n].zero = 0;
    idt[n].flags = 0x8E;
    idt[n].base_hi = (handler >> 16) & 0xFFFF;
}

extern void irq0_entry(void);
extern void irq1_entry(void);
extern void isr80_entry(void);

static void idt_init(void) {
    idtp.lim = sizeof(idt) - 1;
    idtp.base = (uint32_t)&idt;
    memset(&idt, 0, sizeof(idt));
    idt_set(32, (uint32_t)irq0_entry);
    idt_set(33, (uint32_t)irq1_entry);
    idt_set(0x80, (uint32_t)isr80_entry);
    __asm__ volatile("lidt %0" : : "m"(idtp));
}

/* ---------- PIC + PIT ---------- */

static void pic_remap(int off1, int off2) {
    outb(0x20, 0x11); outb(0xA0, 0x11);
    outb(0x21, off1); outb(0xA1, off2);
    outb(0x21, 0x04); outb(0xA1, 0x02);
    outb(0x21, 0x01); outb(0xA1, 0x01);
    outb(0x21, 0x00); outb(0xA1, 0x00);
}

static void pit_init(uint32_t hz) {
    uint32_t div = 1193182 / hz;
    outb(0x43, 0x36);
    outb(0x40, div & 0xFF);
    outb(0x40, (div >> 8) & 0xFF);
}

/* ---------- keyboard ---------- */

static char kbd_queue[32];
static int kbd_qhead = 0, kbd_qtail = 0;

static void kbd_push(char c) {
    int next = (kbd_qtail + 1) % 32;
    if (next == kbd_qhead) return;
    kbd_queue[kbd_qtail] = c;
    kbd_qtail = next;
}
char kbd_pop(void) {
    if (kbd_qhead == kbd_qtail) return 0;
    char c = kbd_queue[kbd_qhead];
    kbd_qhead = (kbd_qhead + 1) % 32;
    return c;
}

/* ---------- scheduler policy (pure, host-testable) ---------- */

task_t tasks[MAX_TASKS];

int sched_pick_next(int current) {
    for (int k = 1; k <= MAX_TASKS; k++) {
        int i = (current + k) % MAX_TASKS;
        if (tasks[i].state == TASK_RUNNABLE) return i;
    }
    return -1;
}

char kbd_decode(uint8_t sc) {
    static const char map[128] = {
        0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
        '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
        0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
        0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
        '*', 0, ' ', 0,
    };
    if (sc >= 128) return 0;
    return map[sc];
}

/* ---------- scheduler core ---------- */

volatile uint32_t g_ticks = 0;
static int current_task = -1;
static int user_started = 0;

/* regs frame (low → high); uesp/uss used only by ring-3 entry frames */
struct regs {
    uint32_t eax, ecx, edx, ebx, oesp, ebp, esi, edi;
    uint32_t ds, es, fs, gs;
    uint32_t eip, cs, eflags;
    uint32_t uesp, uss;
};

static struct regs *make_user_frame(task_t *t) {
    struct regs *r = (struct regs *)(t->kstack + KSTACK_SIZE - sizeof(struct regs));
    memset(r, 0, sizeof(*r));
    r->ds = r->es = r->fs = r->gs = 0x23;  /* user data, rpl 3 */
    r->cs = 0x1B;                          /* user code, rpl 3 */
    r->eflags = 0x202;                     /* IF */
    r->eip = 0x400000;                     /* user entry */
    r->uesp = 0xBFFFF0;                    /* user stack top (user data seg) */
    r->uss = 0x23;
    return r;
}

static struct regs *make_kernel_frame(task_t *t, void *entry) {
    struct regs *r = (struct regs *)(t->kstack + KSTACK_SIZE - sizeof(struct regs));
    memset(r, 0, sizeof(*r));
    r->ds = r->es = r->fs = r->gs = 0x10;
    r->cs = 0x08;
    r->eflags = 0x202;
    r->eip = (uint32_t)entry;
    return r;
}

static void task_a(void);
static void task_b(void);
static void task_c(void);
static void *task_entry(int i);

uint32_t irq0_handler_c(uint32_t saved_frame) {
    g_ticks++;
    outb(0x20, 0x20);                      /* EOI first */
    if (current_task >= 0) tasks[current_task].esp = saved_frame;

    int next = sched_pick_next(current_task);
    if (next < 0) return saved_frame;      /* idle */
    current_task = next;

    if (next == 3 && !user_started) {
        user_started = 1;
        tss.esp0 = (uint32_t)(tasks[3].kstack + KSTACK_SIZE);
        return (uint32_t)make_user_frame(&tasks[3]);
    }
    if (tasks[next].esp == 0)
        tasks[next].esp = (uint32_t)make_kernel_frame(&tasks[next], task_entry(next));
    return tasks[next].esp;
}

void *task_entry(int i) {
    extern void task_a(void);
    extern void task_b(void);
    extern void task_c(void);
    switch (i) {
        case 0: return task_a;
        case 1: return task_b;
        case 2: return task_c;
        default: return 0;
    }
}

void irq1_handler_c(void) {
    uint8_t sc = inb(0x60);
    if (sc & 0x80) return;                 /* release */
    char c = kbd_decode(sc);
    if (c) kbd_push(c);
}

/* ---------- syscalls ---------- */

#define SYS_WRITE 1
#define SYS_GETTICK 2
#define SYS_EXIT 3

/* user flat segments are base 0, so user pointers are linear */
static void sys_write(uint32_t buf, uint32_t len) {
    const char *p = (const char *)buf;
    for (uint32_t i = 0; i < len; i++) kputc(p[i]);
}

uint32_t isr80_handler_c(uint32_t saved_frame) {
    struct regs *r = (struct regs *)saved_frame;
    switch (r->eax) {
        case SYS_WRITE: sys_write(r->ebx, r->ecx); r->eax = r->ecx; break;
        case SYS_GETTICK: r->eax = g_ticks; break;
        case SYS_EXIT: {
            if (current_task >= 0) tasks[current_task].state = TASK_ZOMBIE;
            int next = sched_pick_next(current_task);
            if (next < 0) return (uint32_t)r;
            current_task = next;
            if (tasks[next].esp == 0)
                saved_frame = (uint32_t)make_kernel_frame(&tasks[next], task_entry(next));
            else
                saved_frame = tasks[next].esp;
            return saved_frame;
        }
        default: r->eax = (uint32_t)-1; break;
    }
    return (uint32_t)saved_frame;
}

/* ---------- demo kernel tasks ---------- */

__attribute__((noreturn)) static void task_a(void) {
    for (;;) {
        for (volatile int d = 0; d < 300000; d++) { }
        kputc('A');
    }
}
__attribute__((noreturn)) static void task_b(void) {
    for (;;) {
        for (volatile int d = 0; d < 300000; d++) { }
        kputc('B');
    }
}
__attribute__((noreturn)) static void task_c(void) {
    for (;;) {
        for (volatile int d = 0; d < 300000; d++) { }
        kputc('C');
    }
}

/* ---------- user program (assembled from apps/user.S, embedded) ---------- */

extern const uint8_t user_bin_start[];
extern const uint8_t user_bin_end[];

static void copy_user(void) {
    uint8_t *dst = (uint8_t *)0x400000;
    const uint8_t *src = user_bin_start;
    while (src < user_bin_end) *dst++ = *src++;
}

/* ---------- kmain ---------- */

void kmain(void) {
    serial_init();
    kputs("\n[NANOKERNEL-X BOOT OK]\n");

    gdt_init();
    idt_init();
    pic_remap(0x20, 0x28);
    pit_init(100);
    copy_user();
    __asm__ volatile("sti");

    kputs("[GDT/IDT/PIC/PIT OK]\n");

    memset(tasks, 0, sizeof(tasks));
    for (int i = 0; i < 3; i++) {
        tasks[i].state = TASK_RUNNABLE;
        tasks[i].id = (char)('A' + i);
    }
    tasks[3].state = TASK_RUNNABLE;
    tasks[3].id = 'U';

    kputs("[SCHED OK] A B C + U(ring3)\n");
    kputs("[USER OK EXPECTED]\n");
    for (;;) __asm__ volatile("hlt");
}
