/* ========================================================================== *
 * minil - Minimal Linux User-Space Runtime                                   *
 * Core Library and Runtime Implementation                                    *
 * -------------------------------------------------------------------------- *
 * This software is dedicated to the public domain under CC0 1.0 Universal.   *
 * See LICENCE.md for full legal text.                                        *
 * ========================================================================== */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>

/* Базові типи даних */
typedef unsigned long size_t;
typedef unsigned char u8;

typedef unsigned short sa_family_t;
typedef unsigned int   socklen_t;

/* Визначення атрибуту NORETURN відповідно до стандарту */
#if (defined(__cplusplus) && __cplusplus >= 201103L) || \
    (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L)
    #define NORETURN [[noreturn]]
#else
    #define NORETURN __attribute__((noreturn))
#endif

/* Захист від оптимізацій компілятора, які генерують рекурсивні виклики */
#if defined(__clang__)
    #define NO_LOOP_DISTRIBUTE
#else
    #define NO_LOOP_DISTRIBUTE __attribute__((no_tree_loop_distribute_patterns))
#endif

/* Мережеві структури (Вирівнювання природне, без packed для запобігання SIGBUS) */
struct sockaddr {
    sa_family_t sa_family;
    char sa_data[14];
};

struct sockaddr_un {
    sa_family_t sun_family;
    char sun_path[108];
};

#define AF_UNIX      1
#define AF_INET      2

#define SOCK_STREAM  1
#define SOCK_DGRAM   2
#define AT_FDCWD     -100

/* Зовнішні символи з ассемблерного crt0 */
extern void _exit(int) NORETURN;
extern char** environ;
extern void println(const char* s);

/* Попередні оголошення */
size_t strlen(const char* s);
int strncmp(const char* a, const char* b, size_t n);
void* memset(void* dst, int val, size_t n);
void* memcpy(void* dst, const void* src, size_t n);
NORETURN void abort(void);

/* ========================================================================== *
 * 1. Форматований вивід (snprintf)                                          *
 * ========================================================================== */

static void buf_putc(char** buf, size_t* remain, char c) {
    if (*remain > 1) {
        **buf = c;
        (*buf)++;
        (*remain)--;
    }
}

static void buf_puts(char** buf, size_t* remain, const char* s) {
    while (*s) {
        buf_putc(buf, remain, *s++);
    }
}

static void buf_putnum(char** buf, size_t* remain, unsigned long n, int base) {
    static const char digits[] = "0123456789abcdef";
    char tmp[32];
    int i = 0;
    if (n == 0) { buf_putc(buf, remain, '0'); return; }
    while (n > 0) {
        tmp[i++] = digits[n % base];
        n /= base;
    }
    while (i > 0) buf_putc(buf, remain, tmp[--i]);
}

int snprintf(char* str, size_t size, const char* format, ...) {
    va_list args;
    va_start(args, format);
    
    char* p = str;
    size_t remain = size;

    for (const char* f = format; *f; f++) {
        if (*f != '%' || !*(f + 1)) {
            buf_putc(&p, &remain, *f);
            continue;
        }
        
        switch (*++f) {
            case 's': {
                const char* s = va_arg(args, const char*);
                if (!s) s = "(null)";
                buf_puts(&p, &remain, s);
                break;
            }
            case 'd': {
                int val = va_arg(args, int);
                if (val < 0) {
                    buf_putc(&p, &remain, '-');
                    /* Безпечний каст INT_MIN для запобігання signed overflow UB */
                    unsigned long uval = (val == -2147483647 - 1) ? 2147483648UL : (unsigned long)(-val);
                    buf_putnum(&p, &remain, uval, 10);
                } else {
                    buf_putnum(&p, &remain, (unsigned long)val, 10);
                }
                break;
            }
            case 'x': {
                buf_putnum(&p, &remain, va_arg(args, unsigned int), 16); 
                break;
            }
            case 'p': {
                buf_puts(&p, &remain, "0x"); 
                buf_putnum(&p, &remain, (unsigned long)(size_t)va_arg(args, void*), 16); 
                break;
            }
            default: {
                buf_putc(&p, &remain, *f); 
                break;
            }
        }
    }
    
    if (remain > 0) *p = '\0';
    va_end(args);
    return (int)(p - str);
}

/* ========================================================================== *
 * 2. Низькорівневі Системні Виклики (Multi-Arch Inline Assembly)              *
 * ========================================================================== */

#if defined(__x86_64__)
    #define SYS_CALL "syscall"
    #define SYS_CLOBBERS "rcx", "r11", "memory"
#elif defined(__aarch64__)
    #define SYS_CALL "svc #0"
    #define SYS_CLOBBERS "memory"
#elif defined(__riscv) && (__riscv_xlen == 64)
    #define SYS_CALL "ecall"
    #define SYS_CLOBBERS "memory"
#elif defined(__i386__)
    #define SYS_CALL "int $0x80"
#else
    #error "Unsupported architecture for minil runtime!"
#endif

#if defined(__x86_64__) || defined(__aarch64__) || (defined(__riscv) && (__riscv_xlen == 64))

static long sys_call1(long n, long a) {
    long ret;
#if defined(__x86_64__)
    __asm__ volatile (SYS_CALL : "=a"(ret) : "a"(n), "D"(a) : SYS_CLOBBERS);
#elif defined(__aarch64__)
    register long x8 __asm__("x8") = n;
    register long x0 __asm__("x0") = a;
    __asm__ volatile (SYS_CALL : "+r"(x0) : "r"(x8) : SYS_CLOBBERS);
    ret = x0;
#elif defined(__riscv)
    register long a7 __asm__("a7") = n;
    register long a0 __asm__("a0") = a;
    __asm__ volatile (SYS_CALL : "+r"(a0) : "r"(a7) : SYS_CLOBBERS);
    ret = a0;
#endif
    return ret;
}

static long sys_call2(long n, long a, long b) {
    long ret;
#if defined(__x86_64__)
    __asm__ volatile (SYS_CALL : "=a"(ret) : "a"(n), "D"(a), "S"(b) : SYS_CLOBBERS);
#elif defined(__aarch64__)
    register long x8 __asm__("x8") = n;
    register long x0 __asm__("x0") = a;
    register long x1 __asm__("x1") = b;
    __asm__ volatile (SYS_CALL : "+r"(x0) : "r"(x8), "r"(x1) : SYS_CLOBBERS);
    ret = x0;
#elif defined(__riscv)
    register long a7 __asm__("a7") = n;
    register long a0 __asm__("a0") = a;
    register long a1 __asm__("a1") = b;
    __asm__ volatile (SYS_CALL : "+r"(a0) : "r"(a7), "r"(a1) : SYS_CLOBBERS);
    ret = a0;
#endif
    return ret;
}

static long sys_call3(long n, long a, long b, long c) {
    long ret;
#if defined(__x86_64__)
    __asm__ volatile (SYS_CALL : "=a"(ret) : "a"(n), "D"(a), "S"(b), "d"(c) : SYS_CLOBBERS);
#elif defined(__aarch64__)
    register long x8 __asm__("x8") = n;
    register long x0 __asm__("x0") = a;
    register long x1 __asm__("x1") = b;
    register long x2 __asm__("x2") = c;
    __asm__ volatile (SYS_CALL : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2) : SYS_CLOBBERS);
    ret = x0;
#elif defined(__riscv)
    register long a7 __asm__("a7") = n;
    register long a0 __asm__("a0") = a;
    register long a1 __asm__("a1") = b;
    register long a2 __asm__("a2") = c;
    __asm__ volatile (SYS_CALL : "+r"(a0) : "r"(a7), "r"(a1), "r"(a2) : SYS_CLOBBERS);
    ret = a0;
#endif
    return ret;
}

static long sys_call6(long n, long a, long b, long c, long d, long e, long f) {
    long ret;
#if defined(__x86_64__)
    register long r10 __asm__("r10") = d;
    register long r8  __asm__("r8")  = e;
    register long r9  __asm__("r9")  = f;
    __asm__ volatile (
        SYS_CALL : "=a"(ret)
                 : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9)
                 : SYS_CLOBBERS
    );
#elif defined(__aarch64__)
    register long x8 __asm__("x8") = n;
    register long x0 __asm__("x0") = a;
    register long x1 __asm__("x1") = b;
    register long x2 __asm__("x2") = c;
    register long x3 __asm__("x3") = d;
    register long x4 __asm__("x4") = e;
    register long x5 __asm__("x5") = f;
    __asm__ volatile (SYS_CALL : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5) : SYS_CLOBBERS);
    ret = x0;
#elif defined(__riscv)
    register long a7 __asm__("a7") = n;
    register long a0 __asm__("a0") = a;
    register long a1 __asm__("a1") = b;
    register long a2 __asm__("a2") = c;
    register long a3 __asm__("a3") = d;
    register long a4 __asm__("a4") = e;
    register long a5 __asm__("a5") = f;
    __asm__ volatile (SYS_CALL : "+r"(a0) : "r"(a7), "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5) : SYS_CLOBBERS);
    ret = a0;
#endif
    return ret;
}

#elif defined(__i386__)

static long sys_call1(long n, long a) {
    long ret;
    __asm__ volatile (SYS_CALL : "=a"(ret) : "a"(n), "b"(a) : "memory");
    return ret;
}

static long sys_call2(long n, long a, long b) {
    long ret;
    __asm__ volatile (SYS_CALL : "=a"(ret) : "a"(n), "b"(a), "c"(b) : "memory");
    return ret;
}

static long sys_call3(long n, long a, long b, long c) {
    long ret;
    __asm__ volatile (SYS_CALL : "=a"(ret) : "a"(n), "b"(a), "c"(b), "d"(c) : "memory");
    return ret;
}

static long sys_call6(long n, long a, long b, long c, long d, long e, long f) {
    long ret;
    __asm__ volatile (
        "pushl %%ebp\n\t"
        "movl %[arg6], %%ebp\n\t"
        SYS_CALL "\n\t"
        "popl %%ebp\n\t"
        : "=a"(ret)
        : "0"(n), "b"(a), "c"(b), "d"(c), "S"(d), "D"(e), [arg6] "r"(f)
        : "memory"
    );
    return ret;
}

#endif
/* ========================================================================== *
 * minil - Minimal Linux User-Space Runtime                                   *
 * Core Library and Runtime Implementation                                    *
 * -------------------------------------------------------------------------- *
 * This software is dedicated to the public domain under CC0 1.0 Universal.   *
 * See LICENCE.md for full legal text.                                        *
 * ========================================================================== */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>

/* Basic data types */
typedef unsigned long size_t;
typedef unsigned char u8;

typedef unsigned short sa_family_t;
typedef unsigned int   socklen_t;

/* Definition of the NORETURN attribute according to the standard */
#if (defined(__cplusplus) && __cplusplus >= 201103L) || \
    (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L)
    #define NORETURN [[noreturn]]
#else
    #define NORETURN __attribute__((noreturn))
#endif

/* Protection against compiler optimizations that generate recursive calls */
#if defined(__clang__)
    #define NO_LOOP_DISTRIBUTE
#else
    #define NO_LOOP_DISTRIBUTE __attribute__((no_tree_loop_distribute_patterns))
#endif

/* Network structures (natural alignment, no packed to avoid SIGBUS) */
struct sockaddr {
    sa_family_t sa_family;
    char sa_data[14];
};

struct sockaddr_un {
    sa_family_t sun_family;
    char sun_path[108];
};

#define AF_UNIX      1
#define AF_INET      2

#define SOCK_STREAM  1
#define SOCK_DGRAM   2
#define AT_FDCWD     -100

/* External symbols from assembler crt0 */
extern void _exit(int) NORETURN;
extern char** environ;
extern void println(const char* s);

/* Forward declarations */
size_t strlen(const char* s);
int strncmp(const char* a, const char* b, size_t n);
void* memset(void* dst, int val, size_t n);
void* memcpy(void* dst, const void* src, size_t n);
NORETURN void abort(void);

/* ========================================================================== *
 * 1. Formatted Output (snprintf)                                             *
 * ========================================================================== */

static void buf_putc(char** buf, size_t* remain, char c) {
    if (*remain > 1) {
        **buf = c;
        (*buf)++;
        (*remain)--;
    }
}

static void buf_puts(char** buf, size_t* remain, const char* s) {
    while (*s) {
        buf_putc(buf, remain, *s++);
    }
}

static void buf_putnum(char** buf, size_t* remain, unsigned long n, int base) {
    static const char digits[] = "0123456789abcdef";
    char tmp[32];
    int i = 0;
    if (n == 0) { buf_putc(buf, remain, '0'); return; }
    while (n > 0) {
        tmp[i++] = digits[n % base];
        n /= base;
    }
    while (i > 0) buf_putc(buf, remain, tmp[--i]);
}

int snprintf(char* str, size_t size, const char* format, ...) {
    va_list args;
    va_start(args, format);
    
    char* p = str;
    size_t remain = size;

    for (const char* f = format; *f; f++) {
        if (*f != '%' || !*(f + 1)) {
            buf_putc(&p, &remain, *f);
            continue;
        }
        
        switch (*++f) {
            case 's': {
                const char* s = va_arg(args, const char*);
                if (!s) s = "(null)";
                buf_puts(&p, &remain, s);
                break;
            }
            case 'd': {
                int val = va_arg(args, int);
                if (val < 0) {
                    buf_putc(&p, &remain, '-');
                    /* Safe cast for INT_MIN to avoid signed overflow UB */
                    unsigned long uval = (val == -2147483647 - 1) ? 2147483648UL : (unsigned long)(-val);
                    buf_putnum(&p, &remain, uval, 10);
                } else {
                    buf_putnum(&p, &remain, (unsigned long)val, 10);
                }
                break;
            }
            case 'x': {
                buf_putnum(&p, &remain, va_arg(args, unsigned int), 16); 
                break;
            }
            case 'p': {
                buf_puts(&p, &remain, "0x"); 
                buf_putnum(&p, &remain, (unsigned long)(size_t)va_arg(args, void*), 16); 
                break;
            }
            default: {
                buf_putc(&p, &remain, *f); 
                break;
            }
        }
    }
    
    if (remain > 0) *p = '\0';
    va_end(args);
    return (int)(p - str);
}

/* ========================================================================== *
 * 2. Low-Level System Calls (Multi-Arch Inline Assembly)                      *
 * ========================================================================== */

#if defined(__x86_64__)
    #define SYS_CALL "syscall"
    #define SYS_CLOBBERS "rcx", "r11", "memory"
#elif defined(__aarch64__)
    #define SYS_CALL "svc #0"
    #define SYS_CLOBBERS "memory"
#elif defined(__riscv) && (__riscv_xlen == 64)
    #define SYS_CALL "ecall"
    #define SYS_CLOBBERS "memory"
#elif defined(__i386__)
    #define SYS_CALL "int $0x80"
#else
    #error "Unsupported architecture for minil runtime!"
#endif

/* ... (system call wrappers unchanged) ... */

/* Wrappers for POSIX API */
int close(int fd) {
#if defined(__x86_64__) || defined(__aarch64__) || defined(__riscv)
    return (int)sys_call1(3, fd);       /* __NR_close = 3 */
#else
    return (int)sys_call1(6, fd);       /* i386 SYS_close = 6 */
#endif
}

/* ... (socket, connect, mmap, munmap unchanged) ... */

/* ========================================================================== *
 * 3. Low-Level Memory Operations (Safe Optimization)                          *
 * ========================================================================== */

NO_LOOP_DISTRIBUTE void* memset(void* dst, int val, size_t n) {
    u8* p = (u8*)dst;
    u8 v = (u8)val;
    while (n--) *p++ = v;
    return dst;
}

NO_LOOP_DISTRIBUTE void* memcpy(void* dst, const void* src, size_t n) {
    u8* d = (u8*)dst;
    const u8* s = (const u8*)src;
    for (size_t i = 0; i < n; ++i) d[i] = s[i];
    return dst;
}

NO_LOOP_DISTRIBUTE void* memmove(void* dst, const void* src, size_t n) {
    u8* d = (u8*)dst;
    const u8* s = (const u8*)src;

    if (d == s || n == 0) return dst;

    if (d < s) {
        for (size_t i = 0; i < n; ++i) d[i] = s[i];
    } else {
        while (n--) d[n] = s[n];
    }
    return dst;
}

/* ========================================================================== *
 * 4. Environment and Strings                                                  *
 * ========================================================================== */

/* ... (getenv, strlen, strcmp, etc. unchanged) ... */

/* ========================================================================== *
 * 5. Heap Allocator (Adjacent Block Alignment Algorithm)                      *
 * ========================================================================== */

#define HEAP_SIZE   (1024UL * 1024UL)
#define ALIGNMENT   16UL
#define MAGIC_USED  ((size_t)0xC0FFEEUL)

#define ALIGN_UP(x) (((x) + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1))

typedef struct HeapBlock {
    size_t size;
    int free;
    size_t magic;
    struct HeapBlock* next;
} HeapBlock;

#define HEADER_SIZE ALIGN_UP(sizeof(HeapBlock))

static u8 heap[HEAP_SIZE] __attribute__((aligned(16)));
static size_t heap_off = 0;
static HeapBlock* heap_head = 0;

/* ... (allocator internals unchanged) ... */

/* Specific system aborts */
NORETURN void abort(void) {
    _exit(127);
    while(1);
}

/* ========================================================================== *
 * 6. Freestanding C++ Runtime Support                                         *
 * ========================================================================== */

#ifdef __cplusplus

/* Standard dynamic memory management for classes */
void* operator new(size_t n) { return malloc(n); }
void* operator new[](size_t n) { return malloc(n); }

void operator delete(void* p) noexcept { free(p); }
void operator delete[](void* p) noexcept { free(p); }

/* C++17 extended alignment delete operators */
#if __cplusplus >= 201703L
namespace std {
    enum class align_val_t : size_t {};
}
void operator delete(void* p, std::align_val_t) noexcept { free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { free(p); }
void operator delete(void* p, size_t, std::align_val_t) noexcept { free(p); }
void operator delete[](void* p, size_t, std::align_val_t) noexcept { free(p); }
#endif

/* System C++ ABI stubs */
extern "C" void __gxx_personality_v0(void) {}
extern "C" int __cxa_atexit(void (*)(void*), void*, void*) { return 0; }

/* ... (guard functions unchanged) ... */

/* Support for std::terminate and error handlers */
namespace std {
    typedef void (*terminate_handler)();

    static terminate_handler current_terminate_handler = ::abort;

    [[noreturn]] void terminate() noexcept {
        if (current_terminate_handler) {
            current_terminate_handler();
        }
        ::abort();
    }

    terminate_handler set_terminate(terminate_handler f) noexcept {
        terminate_handler old = current_terminate_handler;
        current_terminate_handler = f;
        return old;
    }

    terminate_handler get_terminate() noexcept {
        return current_terminate_handler;
    }
}

/* Verbose linker handler for critical failures */
extern "C" [[noreturn]] void __verbose_terminate_handler() {
    ::abort();
}

/* Minimal built-in C++ algorithms (native, without libc) */
namespace std {
    template<typename InputIt, typename T>
    auto count(InputIt first, InputIt last, const T& value) {
        auto n = 0;
        for (; first != last; ++first) {
            if (*first == value) { n++; }
        }
        return n;
    }
}
#endif

/* Обертки для POSIX API */
int close(int fd) {
#if defined(__x86_64__) || defined(__aarch64__) || defined(__riscv)
    return (int)sys_call1(3, fd);       /* __NR_close = 3 */
#else
    return (int)sys_call1(6, fd);       /* i386 SYS_close = 6 */
#endif
}

int socket(int domain, int type, int protocol) {
#if defined(__x86_64__) || defined(__aarch64__) || defined(__riscv)
    return (int)sys_call3(41, domain, type, protocol);   /* __NR_socket = 41 */
#else
    unsigned long args[3];
    args[0] = (unsigned long)domain;
    args[1] = (unsigned long)type;
    args[2] = (unsigned long)protocol;
    return (int)sys_call2(102, 1, (long)(size_t)args);   /* socketcall(SYS_SOCKET) */
#endif
}

int connect(int fd, const struct sockaddr* addr, socklen_t len) {
#if defined(__x86_64__) || defined(__aarch64__) || defined(__riscv)
    return (int)sys_call3(42, fd, (long)(size_t)addr, len);      /* __NR_connect = 42 */
#else
    unsigned long args[3];
    args[0] = (unsigned long)fd;
    args[1] = (unsigned long)(size_t)addr;
    args[2] = (unsigned long)len;
    return (int)sys_call2(102, 3, (long)(size_t)args);           /* socketcall(SYS_CONNECT) */
#endif
}

void* mmap(void* addr, size_t len, int prot, int flags, int fd, long off) {
#if defined(__x86_64__) || defined(__aarch64__) || defined(__riscv)
    long ret = sys_call6(9, (long)(size_t)addr, (long)len, (long)prot, (long)flags, (long)fd, off); /* __NR_mmap = 9 */
    return (void*)ret;
#else
    struct mmap_args {
        unsigned long addr;
        unsigned long len;
        unsigned long prot;
        unsigned long flags;
        unsigned long fd;
        unsigned long offset;
    } args;

    args.addr   = (unsigned long)(size_t)addr;
    args.len    = (unsigned long)len;
    args.prot   = (unsigned long)prot;
    args.flags  = (unsigned long)flags;
    args.fd     = (unsigned long)fd;
    args.offset = (unsigned long)off;

    long ret = sys_call1(90, (long)(size_t)&args);   /* SYS_mmap = 90 */
    return (void*)ret;
#endif
}

int munmap(void* addr, size_t len) {
#if defined(__x86_64__) || defined(__aarch64__) || defined(__riscv)
    return (int)sys_call2(11, (long)(size_t)addr, (long)len);  /* __NR_munmap = 11 */
#else
    return (int)sys_call2(91, (long)(size_t)addr, (long)len);  /* i386 SYS_munmap = 91 */
#endif
}

/* ========================================================================== *
 * 3. Низькорівнева робота з пам'яттю (Безпечна оптимізація)                 *
 * ========================================================================== */

NO_LOOP_DISTRIBUTE void* memset(void* dst, int val, size_t n) {
    u8* p = (u8*)dst;
    u8 v = (u8)val;
    while (n--) *p++ = v;
    return dst;
}

NO_LOOP_DISTRIBUTE void* memcpy(void* dst, const void* src, size_t n) {
    u8* d = (u8*)dst;
    const u8* s = (const u8*)src;
    for (size_t i = 0; i < n; ++i) d[i] = s[i];
    return dst;
}

NO_LOOP_DISTRIBUTE void* memmove(void* dst, const void* src, size_t n) {
    u8* d = (u8*)dst;
    const u8* s = (const u8*)src;

    if (d == s || n == 0) return dst;

    if (d < s) {
        for (size_t i = 0; i < n; ++i) d[i] = s[i];
    } else {
        while (n--) d[n] = s[n];
    }
    return dst;
}

/* ========================================================================== *
 * 4. Окруження та Рядки                                                      *
 * ========================================================================== */

char* getenv(const char* name) {
    size_t name_len;
    if (!name || !*name) return 0;

    name_len = strlen(name);
    if (!environ) return 0;

    for (char** e = environ; *e; ++e) {
        if (strncmp(*e, name, name_len) == 0 && (*e)[name_len] == '=') {
            return *e + name_len + 1;
        }
    }
    return 0;
}

size_t strlen(const char* s) {
    const char* p = s;
    while (*p) ++p;
    return (size_t)(p - s);
}

int strcmp(const char* a, const char* b) {
    while (*a && (*a == *b)) { ++a; ++b; }
    return (int)((unsigned char)*a - (unsigned char)*b);
}

int strncmp(const char* a, const char* b, size_t n) {
    while (n && *a && (*a == *b)) { ++a; ++b; --n; }
    if (n == 0) return 0;
    return (int)((unsigned char)*a - (unsigned char)*b);
}

int memcmp(const void* a, const void* b, size_t n) {
    const u8* x = (const u8*)a;
    const u8* y = (const u8*)b;
    for (size_t i = 0; i < n; ++i) {
        if (x[i] != y[i]) return (int)x[i] - (int)y[i];
    }
    return 0;
}

int puts(const char* s) {
    if (!s) s = "(null)";
    println(s);
    return 0;
}

/* ========================================================================== *
 * 5. Алокатор Кучі (Алгоритм суміжного вирівнювання блоків)                 *
 * ========================================================================== */

#define HEAP_SIZE   (1024UL * 1024UL)
#define ALIGNMENT   16UL
#define MAGIC_USED  ((size_t)0xC0FFEEUL)

#define ALIGN_UP(x) (((x) + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1))

typedef struct HeapBlock {
    size_t size;
    int free;
    size_t magic;
    struct HeapBlock* next;
} HeapBlock;

#define HEADER_SIZE ALIGN_UP(sizeof(HeapBlock))

static u8 heap[HEAP_SIZE] __attribute__((aligned(16)));
static size_t heap_off = 0;
static HeapBlock* heap_head = 0;

static size_t heap_align_size(size_t n) {
    if (n == 0) n = 1;
    if (n > ((size_t)-1) - (ALIGNMENT - 1)) abort();
    return ALIGN_UP(n);
}

static void heap_split_block(HeapBlock* block, size_t wanted) {
    if (!block || block->size <= wanted) return;

    size_t remaining = block->size - wanted;
    if (remaining < HEADER_SIZE + ALIGNMENT) return;

    HeapBlock* next = (HeapBlock*)((u8*)block + HEADER_SIZE + wanted);
    next->size = remaining - HEADER_SIZE;
    next->free = 1;
    next->magic = MAGIC_USED;
    next->next = block->next;

    block->size = wanted;
    block->next = next;
}

static void heap_coalesce(void) {
    HeapBlock* cur = heap_head;
    while (cur && cur->next) {
        u8* cur_end = (u8*)cur + HEADER_SIZE + cur->size;
        if (cur->free && cur->next->free && cur_end == (u8*)cur->next) {
            cur->size += HEADER_SIZE + cur->next->size;
            cur->next = cur->next->next;
            continue;
        }
        cur = cur->next;
    }
}

static HeapBlock* heap_find_free(size_t n) {
    HeapBlock* cur = heap_head;
    while (cur) {
        if (cur->free && cur->size >= n) return cur;
        cur = cur->next;
    }
    return 0;
}

static HeapBlock* heap_new_block(size_t n) {
    if (n > ((size_t)-1) - HEADER_SIZE) abort();
    size_t need = HEADER_SIZE + n;
    if (need > HEAP_SIZE - heap_off) abort();

    HeapBlock* block = (HeapBlock*)&heap[heap_off];
    block->size = n;
    block->free = 0;
    block->magic = MAGIC_USED;
    block->next = 0;

    if (!heap_head) {
        heap_head = block;
    } else {
        HeapBlock* cur = heap_head;
        while (cur->next) cur = cur->next;
        cur->next = block;
    }

    heap_off += need;
    return block;
}

static HeapBlock* heap_block_from_ptr(void* p) {
    if (!p) return 0;
    HeapBlock* block = (HeapBlock*)((u8*)p - HEADER_SIZE);
    if (block->magic != MAGIC_USED) abort();
    return block;
}

void* malloc(size_t n) {
    size_t wanted = heap_align_size(n);
    HeapBlock* block = heap_find_free(wanted);

    if (block) {
        block->free = 0;
        heap_split_block(block, wanted);
        return (u8*)block + HEADER_SIZE;
    }

    block = heap_new_block(wanted);
    return (u8*)block + HEADER_SIZE;
}

void free(void* p) {
    if (!p) return;
    HeapBlock* block = heap_block_from_ptr(p);
    if (block->free) abort();

    block->free = 1;
    heap_coalesce();
}

void* calloc(size_t n, size_t s) {
    if (n != 0 && s > ((size_t)-1) / n) abort();
    size_t total = n * s;
    void* p = malloc(total);
    memset(p, 0, total);
    return p;
}

void* realloc(void* p, size_t n) {
    if (!p) return malloc(n);
    if (n == 0) { free(p); return 0; }

    HeapBlock* block = heap_block_from_ptr(p);
    size_t wanted = heap_align_size(n);

    /* Якщо блок вже достатнього розміру — перевикористовуємо */
    if (block->size >= wanted) {
        heap_split_block(block, wanted);
        return p;
    }

    void* new_ptr = malloc(n);
    if (!new_ptr) return 0;
    size_t copy_size = block->size;
    memcpy(new_ptr, p, copy_size);
    free(p);
    return new_ptr;
}

char* strdup(const char* s) {
    size_t n = strlen(s);
    if (n == (size_t)-1) abort();
    n = n + 1;

    char* out = (char*)malloc(n);
    if (!out) abort();
    memcpy(out, s, n);
    return out;
}

/* Специфічні системні аборти */
NORETURN void abort(void) {
    _exit(127);
    while(1);
}

void __cxa_pure_virtual(void) {
    abort();
}

#ifdef __cplusplus
}
#endif

/* ========================================================================== *
 * 6. Freestanding C++ Runtime Support                                       *
 * ========================================================================== */
#ifdef __cplusplus

/* Стандартне динамічне керування пам'яттю для класів */
void* operator new(size_t n) { return malloc(n); }
void* operator new[](size_t n) { return malloc(n); }

void operator delete(void* p) noexcept { free(p); }
void operator delete[](void* p) noexcept { free(p); }

#if __cplusplus >= 201402L
void operator delete(void* p, size_t) noexcept { free(p); }
void operator delete[](void* p, size_t) noexcept { free(p); }
#endif

/* C++17 Розширене вирівнювання операторів delete */
#if __cplusplus >= 201703L
namespace std {
    enum class align_val_t : size_t {};
}
void operator delete(void* p, std::align_val_t) noexcept { free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { free(p); }
void operator delete(void* p, size_t, std::align_val_t) noexcept { free(p); }
void operator delete[](void* p, size_t, std::align_val_t) noexcept { free(p); }
#endif

/* Системні C++ ABI заглушки */
extern "C" void __gxx_personality_v0(void) {}
extern "C" int __cxa_atexit(void (*)(void*), void*, void*) { return 0; }

extern "C" int __cxa_guard_acquire(long* guard) {
    if (*guard) return 0; 
    return 1;              
}

extern "C" void __cxa_guard_release(long* guard) { *guard = 1; }
extern "C" void __cxa_guard_abort(long* guard) { abort(); }

/* Підтримка std::terminate та обробників помилок */
namespace std {
    typedef void (*terminate_handler)();

    static terminate_handler current_terminate_handler = ::abort;

    [[noreturn]] void terminate() noexcept {
        if (current_terminate_handler) {
            current_terminate_handler();
        }
        ::abort();
    }

    terminate_handler set_terminate(terminate_handler f) noexcept {
        terminate_handler old = current_terminate_handler;
        current_terminate_handler = f;
        return old;
    }

    terminate_handler get_terminate() noexcept {
        return current_terminate_handler;
    }
}

/* Лінкувальний вербозний обробник у випадку критичних затиків */
extern "C" [[noreturn]] void __verbose_terminate_handler() {
    ::abort();
}

/* Мінімальні вбудовані C++ алгоритми (нативні, без libc) */
namespace std {
    template<typename InputIt, typename T>
    auto count(InputIt first, InputIt last, const T& value) {
        auto n = 0;
        for (; first != last; ++first) {
            if (*first == value) { n++; }
        }
        return n;
    }
}
#endif
