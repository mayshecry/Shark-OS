.set ALIGN, 1<<0

.set MEMINFO, 1<<1

.set GRAPHICS, 1<<2

.set FLAGS, ALIGN | MEMINFO | GRAPHICS

.set MAGIC, 0x1BADB002

.set CHECKSUM, -(MAGIC + FLAGS)



.section .multiboot

.align 4

.long MAGIC

.long FLAGS

.long CHECKSUM

.long 0, 0, 0, 0, 0

.long 0

.long 1024

.long 768

.long 32



.section .data

.align 4

saved_magic: .long 0

saved_info: .long 0



.section .bss

.align 16

.global stack_bottom

stack_bottom:

.skip 262144

.global stack_top

stack_top:



.section .data

.align 8

gdt32:

.quad 0

.quad 0x00CF9A000000FFFF

.quad 0x00CF92000000FFFF

gdt32_end:

gdt32_ptr:

.word gdt32_end-gdt32-1

.long gdt32



.section .text

.global _start

.type _start, @function

_start:

    .code32

    cli

    mov %eax, %esi

    mov %ebx, %edi

    lgdt gdt32_ptr

    ljmp $0x08, $boot32_cs

boot32_cs:

    mov $0x10, %ax

    mov %ax, %ds

    mov %ax, %es

    mov %ax, %fs

    mov %ax, %gs

    mov %ax, %ss

    mov %esi, saved_magic

    mov %edi, saved_info

    cld

    mov $_bss_start, %edi

    mov $_bss_end, %ecx

    sub %edi, %ecx

    xor %eax, %eax

    rep stosb

    mov $stack_top, %esp

    and $~15, %esp

    push saved_info

    push saved_magic

    call kmain

boot32_hang:

    cli

    hlt

    jmp boot32_hang



.macro ISR_NOERRCODE num

.global isr\num

isr\num:

    pushl $0

    pushl $\num

    jmp isr_common_stub

.endm



.macro ISR_ERRCODE num

.global isr\num

isr\num:

    pushl $\num

    jmp isr_common_stub

.endm



.macro IRQ num, target

.global irq\num

irq\num:

    pushl $0

    pushl $\target

    jmp irq_common_stub

.endm



ISR_NOERRCODE 0

ISR_NOERRCODE 1

ISR_NOERRCODE 2

ISR_NOERRCODE 3

ISR_NOERRCODE 4

ISR_NOERRCODE 5

ISR_NOERRCODE 6

ISR_NOERRCODE 7

ISR_ERRCODE 8

ISR_NOERRCODE 9

ISR_ERRCODE 10

ISR_ERRCODE 11

ISR_ERRCODE 12

ISR_ERRCODE 13

ISR_ERRCODE 14

ISR_NOERRCODE 15

ISR_NOERRCODE 16

ISR_ERRCODE 17

ISR_NOERRCODE 18

ISR_NOERRCODE 19

ISR_NOERRCODE 20

ISR_ERRCODE 21

ISR_NOERRCODE 22

ISR_NOERRCODE 23

ISR_NOERRCODE 24

ISR_NOERRCODE 25

ISR_NOERRCODE 26

ISR_NOERRCODE 27

ISR_NOERRCODE 28

ISR_ERRCODE 29

ISR_ERRCODE 30

ISR_NOERRCODE 31

ISR_NOERRCODE 128

IRQ 0, 32

IRQ 1, 33

IRQ 2, 34

IRQ 3, 35

IRQ 4, 36

IRQ 5, 37

IRQ 6, 38

IRQ 7, 39

IRQ 8, 40

IRQ 9, 41

IRQ 10, 42

IRQ 11, 43

IRQ 12, 44

IRQ 13, 45

IRQ 14, 46

IRQ 15, 47



.extern isr_handler

isr_common_stub:

    cld

    pusha

    pushl %esp

    call isr_handler

    addl $4, %esp

    popa

    addl $8, %esp

    iret



.extern irq_handler

irq_common_stub:

    cld

    pusha

    pushl %esp

    call irq_handler

    addl $4, %esp

    popa

    addl $8, %esp

    iret



.global idt_load

.type idt_load, @function

idt_load:

    movl 4(%esp), %eax

    lidt (%eax)

    ret

