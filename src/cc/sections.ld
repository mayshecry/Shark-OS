SECTIONS {
 .text : { *(.text .text.*) }
 .rodata : { *(.rodata .rodata.*) }
 .data : { data_begin = .; *(.data .data.*) data_end = .; }
 .bss : { zero_begin = .; *(.bss .bss.* COMMON) zero_end = .; }
 /DISCARD/ : { *(.eh_frame .note.GNU-stack .comment) }
}
