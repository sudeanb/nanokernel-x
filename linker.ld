/* NanoKernel-X kernel link script — ELF for multiboot-less -kernel boot,
 * plus .user section for the embedded ring-3 program. */

ENTRY(kernel_entry)

SECTIONS
{
    . = 0x8000;

    .text : ALIGN(4)
    {
        kernel/entry.o(kernel_entry)
        *(.text .text.*)
    }

    .rodata : ALIGN(4)
    {
        *(.user)
        *(.rodata .rodata.*)
    }

    .data : ALIGN(4)
    {
        *(.data .data.*)
    }

    .bss : ALIGN(4)
    {
        bss_start = .;
        *(.bss .bss.* COMMON)
        bss_end = .;
    }

    kernel_end = .;
}
