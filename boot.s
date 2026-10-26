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
.long 0, 0, 0, 0, 0
.long 0          
.long 1024       
.long 768        
.long 32        

.section .data
.align 16
saved_magic: .long 0
saved_info:  .long 0

.section .bss
.align 4096

pml4_table: .skip 4096
pdp_table:  .skip 4096
pd_tables:  .skip 524288   # 128 * 4096 = 512KB for 128 PDs (128GB)

.align 4096
.global stack_bottom
stack_bottom:
.skip 262144
.global stack_top
stack_top:

.section .data
.align 16
gdt64:
    .quad 0x0000000000000000  # 0x00 null
    .quad 0x00209A0000000000  # 0x08 code64 (L=1)
    .quad 0x0000920000000000  # 0x10 data64
gdt64_ptr32:
    .word . - gdt64 - 1
    .long gdt64
gdt64_ptr64:
    .word . - gdt64 - 1
    .quad gdt64
idt64_ptr:
    .word 0
    .quad 0

.section .text
.global _start
.type _start, @function

_start:
    .code32
    mov %eax, %esi   # magic
    mov %ebx, %edx   # info ptr

    .extern _bss_start
    .extern _bss_end
    mov $_bss_start, %edi
    mov $_bss_end, %ecx
    sub %edi, %ecx
    xor %eax, %eax
    rep stosb

    mov %esi, saved_magic
    mov %edx, saved_info

    mov $pdp_table, %eax
    or $0x03, %eax
    mov %eax, pml4_table
    movl $0, pml4_table+4   # high 0

    # Fill PDP[0..127] -> PDs (each 1GB)
    mov $128, %ecx
    mov $pdp_table, %edi
    mov $pd_tables, %ebx
1:  mov %ebx, %eax
    or $0x03, %eax
    mov %eax, (%edi)        # low
    movl $0, 4(%edi)        # high zero (tables <4GB)
    add $4096, %ebx
    add $8, %edi
    dec %ecx
    jnz 1b

    mov $pd_tables, %edi    # dest pointer
    mov $128, %esi          # outer counter (128 PDs) — use esi so ecx free for inner
    xor %edx, %edx
    xor %eax, %eax
2:  mov $512, %ecx          # inner 512 per PD
3:  mov %eax, %ebx
    or $0x83, %ebx          # present|writable|huge
    mov %ebx, (%edi)        # low
    mov %edx, 4(%edi)       # high
    add $8, %edi
    add $0x200000, %eax
    adc $0, %edx
    dec %ecx
    jnz 3b
    dec %esi
    jnz 2b

    # Enable PAE
    mov %cr4, %eax
    or $0x20, %eax          # CR4.PAE
    mov %eax, %cr4

    # Load PML4 (must be before EFER.LME on VirtualBox)
    mov $pml4_table, %eax
    mov %eax, %cr3

    # Enable Long Mode (EFER.LME)
    mov $0xC0000080, %ecx
    rdmsr
    or $0x100, %eax
    wrmsr

    # Enable paging (PG+PE)
    mov %cr0, %eax
    or $0x80000000, %eax
    or $0x1, %eax
    mov %eax, %cr0

    lgdt gdt64_ptr32
    ljmp $0x08, $long_mode_entry

.code64
long_mode_entry:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss
    mov $stack_top, %rsp
    and $~15, %rsp
    mov saved_magic, %edi
    mov saved_info, %esi
    call kmain
    cli
hang64:
    hlt
    jmp hang64

.code64
.global gdt_flush
.type gdt_flush, @function
gdt_flush:
    lgdt (%rdi)
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss
    pushq $0x08
    lea .Lgdt_ret(%rip), %rax
    pushq %rax
    retfq
.Lgdt_ret:
    ret

.global idt_load
.type idt_load, @function
idt_load:
    lidt (%rdi)
    ret

.macro ISR_NOERRCODE num
  .global isr\num
  isr\num:
    pushq $0
    pushq $\num
    jmp isr_common_stub
.endm
.macro ISR_ERRCODE num
  .global isr\num
  isr\num:
    pushq $\num
    jmp isr_common_stub
.endm
.macro IRQ num, target
  .global irq\num
  irq\num:
    pushq $0
    pushq $\target
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
    pushq %rax
    pushq %rcx
    pushq %rdx
    pushq %rbx
    pushq %rbp
    pushq %rsi
    pushq %rdi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    mov %rsp, %rdi
    call isr_handler
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rdi
    popq %rsi
    popq %rbp
    popq %rbx
    popq %rdx
    popq %rcx
    popq %rax
    add $16, %rsp
    iretq

.extern irq_handler
irq_common_stub:
    pushq %rax
    pushq %rcx
    pushq %rdx
    pushq %rbx
    pushq %rbp
    pushq %rsi
    pushq %rdi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    mov %rsp, %rdi
    call irq_handler
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rdi
    popq %rsi
    popq %rbp
    popq %rbx
    popq %rdx
    popq %rcx
    popq %rax
    add $16, %rsp
    iretq
