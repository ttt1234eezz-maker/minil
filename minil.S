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
        leaq __init_array_start(%rip), %rdi
        leaq __init_array_end(%rip), %rsi
1:
        cmp %rdi, %rsi
        je 2f
        movq (%rdi), %rax
        addq $8, %rdi
        call *%rax
        jmp 1b
2:
        ret

#else
        /* FIX: save end pointer across calls — %esi/%edi are caller-saved
           in cdecl, so called constructors may clobber them.  Use the
           stack to preserve the end pointer around each indirect call. */
        pushl   %ebx
        movl    $__init_array_start, %ebx   /* cur  (callee-saved) */
        movl    $__init_array_end,   %edx   /* end (kept in %edx across loop) */
1:
        cmpl    %ebx, %edx
        je      2f
        movl    (%ebx), %eax
        addl    $4, %ebx
        call    *%eax
        jmp     1b
2:
        popl    %ebx
        ret

#endif

_start:

#if defined(__x86_64__)

/* ---------- x86-64 ---------- */

        xorq    %rbp, %rbp
        movq    %rsp, %r12

        movq    (%r12), %rdi            /* argc */
        leaq    8(%r12), %rsi           /* argv */
        leaq    8(%rsi,%rdi,8), %rdx    /* envp */

        movq    %rdx, environ(%rip)
        andq    $-16, %rsp
        subq    $8, %rsp

        call    call_init_array
        call    main

        movq    %rax, %rdi
        movq    $60, %rax               /* SYS_exit */
        syscall

#else   /* ---------- i386 ---------- */

        xorl    %ebp, %ebp

        movl    (%esp), %eax            /* argc */
        leal    4(%esp), %ecx           /* argv */
        leal    4(%ecx,%eax,4), %edx    /* envp */

        movl    %edx, environ

        /* FIX: call_init_array takes no arguments — do NOT push argc/argv/envp
           before it.  Push them only for main, after init_array completes. */
        call    call_init_array

        pushl   %edx                    /* envp */
        pushl   %ecx                    /* argv */
        pushl   %eax                    /* argc */
        call    main
        addl    $12, %esp

        movl    %eax, %ebx
        movl    $1, %eax                /* SYS_exit */
        int     $0x80

#endif


/* ====================== syscalls ====================== */

        .global write
write:
#if defined(__x86_64__)

        movq    $1, %rax                /* SYS_write */
        syscall

#else

        movl    4(%esp), %ebx           /* fd */
        movl    8(%esp), %ecx           /* buf */
        movl    12(%esp), %edx          /* len */
        movl    $4, %eax                /* SYS_write */
        int     $0x80

#endif
        ret


        .global read
read:
#if defined(__x86_64__)

        movq    $0, %rax                /* SYS_read */
        syscall

#else

        movl    4(%esp), %ebx
        movl    8(%esp), %ecx
        movl    12(%esp), %edx
        movl    $3, %eax
        int     $0x80

#endif
        ret


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


/* ====================== import (open) ====================== */

/*
 * int import(const char *pathname, int flags, int mode)
 *
 *   Opens a file and returns a file descriptor, or a negative errno
 *   on failure.  Mirrors the POSIX open(2) signature so existing call
 *   sites need no changes if you rename it later.
 *
 *   Typical flags (from <fcntl.h> / asm/fcntl.h):
 *     O_RDONLY = 0
 *     O_WRONLY = 1
 *     O_RDWR   = 2
 *     O_CREAT  = 0x40  (64)
 *     O_TRUNC  = 0x200 (512)
 *
 *   Example:
 *     int fd = import("data.bin", 0, 0);   // open for reading
 *     if (fd < 0) { /* handle error */ }
 */
        .global import
import:
#if defined(__x86_64__)
        /* rdi = pathname, rsi = flags, rdx = mode */
        movq    $2, %rax        /* SYS_open */
        syscall
        ret

#else
        /* 4(%esp) = pathname, 8(%esp) = flags, 12(%esp) = mode */
        movl    4(%esp),  %ebx  /* pathname */
        movl    8(%esp),  %ecx  /* flags    */
        movl    12(%esp), %edx  /* mode     */
        movl    $5, %eax        /* SYS_open */
        int     $0x80
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
