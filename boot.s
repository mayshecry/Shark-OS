/*
 * SharkOS Boot Assembly
 * ---------------------
 * This 32-bit Multiboot kernel boots on BOTH 32-bit (i686) and 64-bit (x86_64) CPUs.
 * GRUB loads the kernel in 32-bit protected mode regardless of the host CPU's native mode.
 * On x86_64 processors, GRUB transitions from long mode to 32-bit protected mode before
 * jumping to _start, so no 64-bit specific code is needed here.
 */

/* Declare constants for the multiboot header. */
.set ALIGN,    1<<0             /* align loaded modules on page boundaries */
.set MEMINFO,  1<<1             /* provide memory map */
.set GRAPHICS, 1<<2             /* request graphics mode */
.set FLAGS,    ALIGN | MEMINFO | GRAPHICS
.set MAGIC,    0x1BADB002       /* 'magic number' lets bootloader find the header */
.set CHECKSUM, -(MAGIC + FLAGS) /* checksum of above, to prove we are multiboot */

.section .multiboot
.align 4
.long MAGIC
.long FLAGS
.long CHECKSUM
/* Multiboot header address fields (required as padding to reach offset 32 for graphics fields) */
.long 0, 0, 0, 0, 0
/* Graphics mode fields */
.long 0    /* 0 = Linear Graphics Mode */
.long 1920 /* Width */
.long 1080 /* Height */
.long 32   /* Depth (Bits per pixel) */

.section .bss
.align 4096
.align 16
stack_bottom:
.skip 16384 # 16 KiB
.global stack_top
stack_top:

.section .data
.align 4
gdt64:
    .quad 0x0000000000000000         # Null
    .quad 0x00cf9a000000ffff         # Code
    .quad 0x00cf92000000ffff         # Data
gdt64_ptr32:
    .word . - gdt64 - 1
    .long gdt64

.section .text
.global _start
.type _start, @function

_start:
    .code32
    /* Save Multiboot magic and info before we clobber them */
    mov %eax, %esi
    mov %ebx, %edx

    /* Zero out BSS */
    .extern _bss_start
    .extern _bss_end
    mov $_bss_start, %edi
    mov $_bss_end, %ecx
    sub %edi, %ecx
    xor %eax, %eax
    rep stosb

    /* Now that BSS is clear, setup stack and push arguments */
    mov $stack_top, %esp
    push %edx            /* Arg 2: Multiboot Info Pointer */
    push %esi            /* Arg 1: Multiboot Magic */

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

ISR_NOERRCODE 128 /* System Call Interrupt 0x80 */

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
