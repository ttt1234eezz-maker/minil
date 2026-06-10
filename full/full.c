/* ========================================================================== *
 * minil - Minimal Linux User-Space Runtime                                   *
 * This file - Full Runtime Implementation                                                *
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

// Sized delete for C++14
#if defined(__cplusplus) && __cplusplus = 201402L
void operator delete(void* p, std::size_t) noexcept {
    ::free(p);
}
void operator delete[](void* p, std::size_t) noexcept {
    ::free(p);
}
#endif


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
