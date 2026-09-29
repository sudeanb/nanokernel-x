/* NanoKernel-X user program link script — link base 0x400000.
 * The flat binary is copied to linear 0x400000 by the kernel, and the
 * user segment descriptors are flat (base 0), so user offsets ARE the
 * linear addresses. */

ENTRY(_start)

SECTIONS
{
    . = 0x400000;

    .text : { *(.text .text.*) }
    .data : { *(.data .data.*) }
    .bss  : { *(.bss .bss.* COMMON) }
}
