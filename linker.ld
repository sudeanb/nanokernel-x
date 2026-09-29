/* NanoKernel-X kernel link script — ELF for multiboot-less -kernel boot,
 * plus .user section for the embedded ring-3 program. */

ENTRY(kernel_entry)

SECTIONS
{
    /* Ubuntu's ld injects a .note.gnu.build-id by default; at a fixed
     * 0x8000 load address that note would sit BEFORE the entry point and
     * the MBR would jump into data. */
    /DISCARD/ : { *(.note) *(.note.*) *(.comment) }

    . = 0x8000;

    .text : ALIGN(4)
    {
        /* entry.o is first on the linker command line, so kernel_entry
         * lands at 0x8000 naturally */
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
