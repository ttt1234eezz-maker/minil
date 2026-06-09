#ifndef ARCH_H
#define ARCH_H

#if defined(__x86_64__)
    #define SYS_WRITE 1
    #define SYS_EXIT  60
    #define ASM_SYSCALL "syscall"
    #define ARG0 %rdi
    #define ARG1 %rsi
    #define ARG2 %rdx
#elif defined(__aarch64__)
    #define SYS_WRITE 64
    #define SYS_EXIT  93
    #define ASM_SYSCALL "svc #0"
    #define ARG0 x0
    #define ARG1 x1
#elif defined(__riscv)
    #define SYS_WRITE 64
    #define SYS_EXIT  93
    #define ASM_SYSCALL "ecall"
#endif

#endif
