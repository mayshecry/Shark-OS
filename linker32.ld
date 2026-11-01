OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)
ENTRY(_start)

SECTIONS
{
    . = 1M;
    _kernel_start = .;

    .text : ALIGN(4K)
    {
        KEEP(*(.multiboot))
        *(.text)
        *(.text.*)
    }

    .rodata : ALIGN(4K)
    {
        *(.rodata)
        *(.rodata.*)
    }

    .data : ALIGN(4K)
    {
        *(.data)
        *(.data.*)
    }

    .bss : ALIGN(4K)
    {
        _bss_start = .;
        *(COMMON)
        *(.bss)
        *(.bss.*)
        _bss_end = .;
    }

    _kernel_end = .;

    /DISCARD/ :
    {
        *(.note.gnu.property)
        *(.comment)
        *(.eh_frame)
        *(.eh_frame.*)
    }
}
