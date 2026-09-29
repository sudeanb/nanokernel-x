/* NanoKernel-X user program — runs at ring 3, entry 0x400000 (flat).
 * Prints "USER OK" twice via int 0x80 syscalls, then exits to the kernel. */

    .text
    .globl _start
_start:
    mov $msg, %ebx          /* linear pointer: user offset == linear addr */
    mov $9, %ecx            /* len */
    mov $1, %eax            /* SYS_WRITE */
    int $0x80

    mov $2000000, %eax      /* delay so the kernel keeps scheduling A/B/C */
1:  dec %eax
    jnz 1b

    mov $msg2, %ebx
    mov $9, %ecx
    mov $1, %eax
    int $0x80

    mov $0, %ebx
    mov $3, %eax            /* SYS_EXIT */
    int $0x80

msg:    .ascii "USER OK\n"
msg2:   .ascii "USER OK\n"
