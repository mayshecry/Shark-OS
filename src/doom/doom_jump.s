.text
.global doom_setjmp
.type doom_setjmp,@function
doom_setjmp:
    mov %rbx, 0(%rdi)
    mov %rbp, 8(%rdi)
    mov %r12, 16(%rdi)
    mov %r13, 24(%rdi)
    mov %r14, 32(%rdi)
    mov %r15, 40(%rdi)
    lea 8(%rsp), %rax
    mov %rax, 48(%rdi)
    mov (%rsp), %rax
    mov %rax, 56(%rdi)
    xor %eax, %eax
    ret
.size doom_setjmp, .-doom_setjmp
.global doom_longjmp
.type doom_longjmp,@function
doom_longjmp:
    mov %rdi, %r11
    mov %esi, %eax
    test %eax, %eax
    jne 1f
    mov $1, %eax
1:
    mov 0(%r11), %rbx
    mov 8(%r11), %rbp
    mov 16(%r11), %r12
    mov 24(%r11), %r13
    mov 32(%r11), %r14
    mov 40(%r11), %r15
    mov 48(%r11), %rsp
    mov 56(%r11), %rdx
    jmp *%rdx
.size doom_longjmp, .-doom_longjmp
.section .note.GNU-stack,"",@progbits
