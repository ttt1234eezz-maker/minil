/* This software is dedicated to the public domain under CC0 1.0 Universal. */
/* See LICENCE.md for full legal text. */

        .global _start
        .global environ
        .extern main

/* ====================== bss ====================== */
        .section .bss
#if defined(__x86_64__)
environ:
        .quad 0
#else
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

#else
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
#endif


_start:
        cld
#if defined(__x86_64__)

/* ---------- x86-64 ---------- */


        xorq    %rbp, %rbp

        movq    %rsp, %r12              /* original stack */
        movq    (%r12), %r13            /* argc */
        leaq    8(%r12), %r14           /* argv */
        leaq    8(%r14,%r13,8), %r15    /* envp */

        movq    %r15, environ(%rip)

        andq    $-16, %rsp              /* correct alignment before call */

        call    call_init_array

        movq    %r13, %rdi              /* argc */
        movq    %r14, %rsi              /* argv */
        movq    %r15, %rdx              /* envp */
        call    main

        movq    %rax, %rdi
        movq    $60, %rax
        syscall

#else   /* ---------- i386 ---------- */


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

        movl    %eax, %ebx
        movl    $1, %eax
        int     $0x80

#endif


/* ====================== syscalls ====================== */


.global write
write:
#if defined(__x86_64__)
        movq    $1, %rax
        syscall
        ret
#else
        pushl   %ebx

        movl    8(%esp), %ebx           /* fd */
        movl    12(%esp), %ecx          /* buf */
        movl    16(%esp), %edx          /* len */
        movl    $4, %eax                /* SYS_write */
        int     $0x80

        popl    %ebx
        ret
#endif


.global read
read:
#if defined(__x86_64__)
        movq    $0, %rax
        syscall
        ret
#else
        pushl   %ebx

        movl    8(%esp), %ebx
        movl    12(%esp), %ecx
        movl    16(%esp), %edx
        movl    $3, %eax
        int     $0x80

        popl    %ebx
        ret
#endif


        /*
         * println(const char *s)
         *   Writes s followed by a newline to stdout.
         */
        .global println
println:
#if defined(__x86_64__)
        /* rdi = pointer to string */

        /* strlen */
        xorq    %rcx, %rcx
.Llen:
        cmpb    $0, (%rdi,%rcx)
        je      .Lwrite
        incq    %rcx
        jmp     .Llen

.Lwrite:
        movq    %rcx, %rdx        /* len */
        movq    %rdi, %rsi        /* buf */
        movq    $1, %rdi          /* fd = stdout */
        movq    $1, %rax          /* SYS_write */
        syscall

        /* newline */
        movq    $1, %rdi
        lea     nl(%rip), %rsi
        movq    $1, %rdx
        movq    $1, %rax
        syscall
        ret

#else
        /* FIX: the two register saves shift %esp by 8 bytes before the
           return address, so the first argument is at 12(%esp), not 4(%esp).
           Previous code used 4(%esp) which read the wrong stack slot. */
        pushl   %ebx
        pushl   %esi

        movl    12(%esp), %esi    /* s  (was 4(%esp) — wrong after 2 pushes) */
        xorl    %ecx, %ecx
.Llen32:
        cmpb    $0, (%esi,%ecx)
        je      .Lwrite32
        incl    %ecx
        jmp     .Llen32

.Lwrite32:
        movl    %ecx, %edx        /* len */
        movl    %esi, %ecx        /* buf */
        movl    $1, %ebx          /* fd */
        movl    $4, %eax          /* SYS_write */
        int     $0x80

        movl    $1, %ebx
        leal    nl, %ecx
        movl    $1, %edx
        movl    $4, %eax
        int     $0x80

        popl    %esi
        popl    %ebx
        ret
#endif


        .global _exit
_exit:
#if defined(__x86_64__)
        /* rdi already contains status */
        movq    $60, %rax               /* SYS_exit */
        syscall
#else
        movl    4(%esp), %ebx
        movl    $1, %eax
        int     $0x80
#endif



/* ============ math ============== */

.global math_abs
math_abs:
#if defined(__x86_64__)
    movl    %edi, %eax
    testl   %eax, %eax
    jge     1f
    negl    %eax
#else
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
#else
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
    movl    %edi, %eax      /* eax = a */
    cmpl    %esi, %eax      /* compare b with a */
    jle     1f              /* if a <= b, keep a */
    movl    %esi, %eax      /* else eax = b */
1:
    ret
#else
    movl    4(%esp), %eax   /* eax = a */
    cmpl    8(%esp), %eax   /* compare b with a */
    jle     1f
    movl    8(%esp), %eax
1:
    ret
#endif

.section .rodata
nl:
        .byte 10

.section .note.GNU-stack,"",@progbits
