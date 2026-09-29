/* MBR bootloader - real mode, loads kernel sectors, enters protected mode.
 * Loaded at 0x7C00 by BIOS. Kernel (9 sectors, LBA 1..9) -> 0x8000. */

    .code16
    .text
    .globl _start
_start:
    cli
    xor %ax, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    mov $0x7C00, %sp
    sti
    cld

    /* int 13h AH=02: read AL=9 sectors CHS(0,0,2) -> ES:BX = 0000:8000 */
    mov $0x8000, %bx
    mov $0x02, %al
    mov $0x02, %cl
    mov $0x00, %dh
    mov $0x00, %ch
    int $0x13
    jc disk_error

    /* protected mode entry */
    cli
    lgdt gdt_desc
    mov %cr0, %eax
    or $0x01, %eax
    mov %eax, %cr0
    ljmp $0x08, $pm_continue

disk_error:
    mov $0x45, %al              /* 'E' distress mark via BIOS teletype */
    mov $0x0E, %ah
    mov $0x07, %bx
    int $0x10
1:  hlt
    jmp 1b

    .code32
pm_continue:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss
    mov $0x90000, %esp
    ljmp $0x08, $0x8000         /* kernel entry */

    .align 8
gdt:
    .quad 0x0000000000000000
    .quad 0x00CF9A000000FFFF
    .quad 0x00CF92000000FFFF
gdt_desc:
    .word gdt_desc - gdt - 1
    .long gdt

    /* pad to offset 510, then the boot signature AT 510/511 */
    . = _start + 510
    .byte 0x55, 0xAA
