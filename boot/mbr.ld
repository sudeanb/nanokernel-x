/* MBR link script — labels are 0x7C00-based, signature forced at 510. */

SECTIONS
{
    . = 0x7C00;
    .text : { *(.text) *(.rodata) }
    . = 0x7C00 + 510;
    .sig : { SHORT(0xAA55) }
}
