/* This software is dedicated to the public domain under CC0 1.0 Universal. */
/* See LICENCE.md for full legal text. */

#include "arch.h"

        .global _start
        .global environ
        .extern main

/* ====================== bss ====================== */
        .section .bss
#if defined(__x86_64__) || defined(__aarch64__) || defined(__riscv)
        .balign 8
environ:
        .quad 0
#else
        .balign 4
environ:
        .long 0
#endif

/* ====================== text ====================== */
        .section .text

.extern __init_array_start
.extern __init_array_end

call_init_array:
#if defined(__x86_64__)
        pushq   %r12
        pushq   %r13
        subq    $8, %rsp

        leaq    __init_array_start(%rip), %r12
        leaq    __init_array_end(%rip),   %r13
1:
        cmpq    %r13, %r12
        je      2f
        movq    (%r12), %rax
        addq    $8, %r12
        call    *%rax
        jmp     1b
2:
        addq    $8, %rsp
        popq    %r13
        popq    %r12
        ret
#elif defined(__i386__)
        pushl   %ebx
        pushl   %esi

        movl    $__init_array_start, %ebx
        movl    $__init_array_end,   %esi
1:
        cmpl    %esi, %ebx
        je      2f
        movl    (%ebx), %eax
        addl    $4, %ebx
        call    *%eax
        jmp     1b
2:
        popl    %esi
        popl    %ebx
        ret
#else
        /* TODO: Реалізувати call_init_array для AArch64 та RISC-V */
        ret
#endif

_start:
#if defined(__x86_64__)
        cld
        xorq    %rbp, %rbp

        movq    %rsp, %r12              /* original stack */
        movq    (%r12), %r13            /* argc */
        leaq    8(%r12), %r14           /* argv */
        leaq    8(%r14,%r13,8), %r15    /* envp */

        movq    %r15, environ(%rip)

        andq    $-16, %rsp              /* correct alignment before call */

        call    call_init_array

        movq    %r13, ARG0              /* argc */
        movq    %r14, ARG1              /* argv */
        movq    %r15, ARG2              /* envp */
        call    main

        movq    %rax, ARG0              /* Передаємо код повернення в exit */
        movq    $SYS_EXIT, SYSCALL_REG
        ASM_SYSCALL

#elif defined(__i386__)
        cld
        xorl    %ebp, %ebp

        movl    (%esp), %eax            /* argc */
        leal    4(%esp), %ecx           /* argv */
        leal    4(%ecx,%eax,4), %edx    /* envp */

        movl    %edx, environ

        pushl   %eax                    /* save argc */
        pushl   %ecx                    /* save argv */
        pushl   %edx                    /* save envp */

        call    call_init_array

        popl    %edx                    /* envp */
        popl    %ecx                    /* argv */
        popl    %eax                    /* argc */

        pushl   %edx
        pushl   %ecx
        pushl   %eax
        call    main
        addl    $12, %esp

        movl    %eax, ARG0              /* Передаємо код повернення в exit */
        movl    $SYS_EXIT, SYSCALL_REG
        ASM_SYSCALL
#else
        /* TODO: Реалізувати _start для AArch64 та RISC-V */
#endif

/* ====================== syscalls ====================== */

.global write
write:
#if defined(__i386__)
        pushl   %ebx
        movl    8(%esp), ARG0
        movl    12(%esp), ARG1
        movl    16(%esp), ARG2
        movl    $SYS_WRITE, SYSCALL_REG
        ASM_SYSCALL
        popl    %ebx
#elif defined(__x86_64__)
        movq    $SYS_WRITE, SYSCALL_REG
        ASM_SYSCALL
#elif defined(__aarch64__)
        mov     SYSCALL_REG, SYS_WRITE
        ASM_SYSCALL
#elif defined(__riscv)
        li      SYSCALL_REG, SYS_WRITE
        ASM_SYSCALL
#endif
        ret

.global read
read:
#if defined(__i386__)
        pushl   %ebx
        movl    8(%esp), ARG0
        movl    12(%esp), ARG1
        movl    16(%esp), ARG2
        movl    $SYS_READ, SYSCALL_REG
        ASM_SYSCALL
        popl    %ebx
#elif defined(__x86_64__)
        movq    $SYS_READ, SYSCALL_REG
        ASM_SYSCALL
#elif defined(__aarch64__)
        mov     SYSCALL_REG, SYS_READ
        ASM_SYSCALL
#elif defined(__riscv)
        li      SYSCALL_REG, SYS_READ
        ASM_SYSCALL
#endif
        ret

.global _exit
_exit:
#if defined(__i386__)
        movl    4(%esp), ARG0
        movl    $SYS_EXIT, SYSCALL_REG
        ASM_SYSCALL
#elif defined(__x86_64__)
        movq    $SYS_EXIT, SYSCALL_REG
        ASM_SYSCALL
#elif defined(__aarch64__)
        mov     SYSCALL_REG, SYS_EXIT
        ASM_SYSCALL
#elif defined(__riscv)
        li      SYSCALL_REG, SYS_EXIT
        ASM_SYSCALL
#endif
        /* Fallthrough in case of syscall failure */
        ret

/*
 * println(const char *s)
 * Writes s followed by a newline to stdout.
 */
        .global println
println:
#if defined(__x86_64__)
        xorq    %rcx, %rcx
.Llen:
        cmpb    $0, (%rdi,%rcx)
        je      .Lwrite
        incq    %rcx
        jmp     .Llen
.Lwrite:
        movq    %rcx, ARG2              /* len */
        movq    %rdi, ARG1              /* buf */
        movq    $1, ARG0                /* fd = stdout */
        movq    $SYS_WRITE, SYSCALL_REG
        ASM_SYSCALL

        /* newline */
        movq    $1, ARG0
        lea     nl(%rip), ARG1
        movq    $1, ARG2
        movq    $SYS_WRITE, SYSCALL_REG
        ASM_SYSCALL
        ret
#elif defined(__i386__)
        pushl   %ebx
        pushl   %esi
        movl    12(%esp), %esi          /* s */
        xorl    %ecx, %ecx
.Llen32:
        cmpb    $0, (%esi,%ecx)
        je      .Lwrite32
        incl    %ecx
        jmp     .Llen32
.Lwrite32:
        movl    %ecx, ARG2              /* len */
        movl    %esi, ARG1              /* buf */
        movl    $1, ARG0                /* fd */
        movl    $SYS_WRITE, SYSCALL_REG
        ASM_SYSCALL

        movl    $1, ARG0
        leal    nl, ARG1
        movl    $1, ARG2
        movl    $SYS_WRITE, SYSCALL_REG
        ASM_SYSCALL

        popl    %esi
        popl    %ebx
        ret
#endif

/*
 * print(const char *s)
 * Writes to stdout.
 */
.global print
print:
#if defined(__x86_64__)
        xorq    %rcx, %rcx
.LlenP:
        cmpb    $0, (%rdi,%rcx)
        je      .LwriteP
        incq    %rcx
        jmp     .LlenP
.LwriteP:
        movq    %rcx, ARG2              /* len */
        movq    %rdi, ARG1              /* buf */
        movq    $1, ARG0                /* fd = stdout */
        movq    $SYS_WRITE, SYSCALL_REG
        ASM_SYSCALL
        ret
#elif defined(__i386__)
        pushl   %ebx
        pushl   %esi
        movl    12(%esp), %esi          /* s */
        xorl    %ecx, %ecx
.LlenP32:
        cmpb    $0, (%esi,%ecx)
        je      .LwriteP32
        incl    %ecx
        jmp     .LlenP32
.LwriteP32:
        movl    %ecx, ARG2              /* len */
        movl    %esi, ARG1              /* buf */
        movl    $1, ARG0                /* fd */
        movl    $SYS_WRITE, SYSCALL_REG
        ASM_SYSCALL
        popl    %esi
        popl    %ebx
        ret
#endif

/* ============ math ============== */

.global math_abs
math_abs:
#if defined(__x86_64__)
    movl    %edi, %eax
    testl   %eax, %eax
    jge     1f
    negl    %eax
#elif defined(__i386__)
    movl    4(%esp), %eax
    testl   %eax, %eax
    jge     1f
    negl    %eax
#endif
1:
    ret

.global math_max
math_max:
#if defined(__x86_64__)
    movl    %edi, %eax
    cmpl    %esi, %eax
    jge     1f
    movl    %esi, %eax
#elif defined(__i386__)
    movl    4(%esp), %eax
    cmpl    8(%esp), %eax
    jge     1f
    movl    8(%esp), %eax
#endif
1:
    ret

.global math_min
math_min:
#if defined(__x86_64__)
    movl    %edi, %eax
    cmpl    %esi, %eax
    jle     1f
    movl    %esi, %eax
#elif defined(__i386__)
    movl    4(%esp), %eax
    cmpl    8(%esp), %eax
    jle     1f
    movl    8(%esp), %eax
#endif
1:
    ret

/* ================================== */
.section .rodata
nl:
        .byte 10

.section .note.GNU-stack,"",@progbits
