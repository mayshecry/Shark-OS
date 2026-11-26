ENTRY(_start)

SECTIONS
{

	. = DEFINED(shark_load_base) ? shark_load_base : 1M;
	_kernel_start = .;

	.text : ALIGN(4K)
	{
		KEEP(*(.multiboot))
		*(.text .text.*)
	}

	.rodata : ALIGN(4K)
	{
		*(.rodata .rodata.*)
        _kernel_ro_end = .;
	}

	.data : ALIGN(4K)
	{
		*(.data .data.*)
	}

	.bss : ALIGN(4K)
	{
		_bss_start = .;
		*(COMMON)
		*(.bss .bss.*)
		_bss_end = .;
	}
	_kernel_end = .;


	/DISCARD/ :
	{
		*(.note.gnu.property)
		*(.comment)
	}
}
