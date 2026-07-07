


.set ALIGN,    1<<0
.set MEMINFO,  1<<1
.set GRAPHICS, 1<<2
.set FLAGS,    ALIGN | MEMINFO | GRAPHICS
.set MAGIC,    0x1BADB002
.set CHECKSUM, -(MAGIC + FLAGS)

.section .multiboot
.align 4
.long MAGIC
.long FLAGS
.long CHECKSUM

# Multiboot address fields (not used)
.long 0, 0, 0, 0, 0

# Graphics fields
.long 0          # mode_type = 0 (linear framebuffer)
.long 1024       # width
.long 768        # height
.long 32         # depth (32-bit color)

.section .bss
.align 4096
.align 16
stack_bottom:
.skip 8192
.global stack_top
stack_top:

.section .data
.align 4
gdt64:
    .quad 0x0000000000000000
    .quad 0x00cf9a000000ffff
    .quad 0x00cf92000000ffff
gdt64_ptr32:
    .word . - gdt64 - 1
    .long gdt64

.section .text
.global _start
.type _start, @function

_start:
    .code32
    mov %eax, %esi
    mov %ebx, %edx

    .extern _bss_start
    .extern _bss_end
    mov $_bss_start, %edi
    mov $_bss_end, %ecx
    sub %edi, %ecx
    xor %eax, %eax
    rep stosb

    mov $stack_top, %esp
    push %edx
    push %esi

    lgdt gdt64_ptr32
    ljmp $0x08, $flush_segments

flush_segments:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss
    call kmain
    hlt

.global gdt_flush
gdt_flush:
    mov 4(%esp), %eax
    lgdt (%eax)
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss
    ljmp $0x08, $.f
.f:
    ret

.global idt_load
idt_load:
    mov 4(%esp), %eax
    lidt (%eax)
    ret

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
ISR_ERRCODE   8
ISR_NOERRCODE 9
ISR_ERRCODE   10
ISR_ERRCODE   11
ISR_ERRCODE   12
ISR_ERRCODE   13
ISR_ERRCODE   14
ISR_NOERRCODE 15
ISR_NOERRCODE 16
ISR_ERRCODE   17
ISR_NOERRCODE 18
ISR_NOERRCODE 19
ISR_NOERRCODE 20
ISR_NOERRCODE 21
ISR_NOERRCODE 22
ISR_NOERRCODE 23
ISR_NOERRCODE 24
ISR_NOERRCODE 25
ISR_NOERRCODE 26
ISR_NOERRCODE 27
ISR_NOERRCODE 28
ISR_NOERRCODE 29
ISR_ERRCODE   30
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
    pusha
    mov %esp, %eax
    push %eax
    call isr_handler
    add $4, %esp
    popa
    add $8, %esp
    iret

.extern irq_handler
irq_common_stub:
    pusha
    mov %esp, %eax
    push %eax
    call irq_handler
    add $4, %esp
    popa
    add $8, %esp
    iret