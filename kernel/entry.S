/* Multiboot header — QEMU -kernel loads the ELF directly, bypassing
 * the MBR for the CI boot test. Must be within the first 8 KiB, 4-aligned. */
    .section .mbh, "a"
    .align 4
    .long 0x1BADB002                /* magic */
    .long 0x00000003                /* flags: page-align modules + mem info */
    .long 0xE4524FFB                /* checksum = -(magic+flags) */

/* NanoKernel-X interrupt entry stubs.
 *
 * Contract: every C handler returns the pointer to a "regs frame" to
 * resume. A frame is (low → high):
 *   eax ecx edx ebx oesp ebp esi edi | ds es fs gs | eip cs eflags | uesp uss
 * The iret at the end pops eip/cs/eflags (+ uesp/uss when the frame's CS
 * carries RPL 3 — that is how the ring-3 user task is first dispatched).
 */

    .text

/* ---- IRQ0: PIT preemption ---- */
    .globl irq0_entry
    .extern irq0_handler_c
irq0_entry:
    pusha
    push %ds
    push %es
    push %fs
    push %gs
    mov %esp, %eax
    push %eax
    call irq0_handler_c     /* eax := resume frame */
    mov %eax, %esp
    pop %gs
    pop %fs
    pop %es
    pop %ds
    popa
    iret

/* ---- IRQ1: keyboard ---- */
    .globl irq1_entry
    .extern irq1_handler_c
irq1_entry:
    pusha
    push %ds
    push %es
    push %fs
    push %gs
    call irq1_handler_c
    pop %gs
    pop %fs
    pop %es
    pop %ds
    popa
    iret

/* ---- int 0x80: syscall (from ring 3) ---- */
    .globl isr80_entry
    .extern isr80_handler_c
isr80_entry:
    pusha
    push %ds
    push %es
    push %fs
    push %gs
    mov %esp, %eax
    push %eax
    call isr80_handler_c    /* eax := resume frame (may switch tasks) */
    mov %eax, %esp
    pop %gs
    pop %fs
    pop %es
    pop %ds
    popa
    iret
