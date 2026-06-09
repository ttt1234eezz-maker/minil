/* This software is dedicated to the public domain under CC0 1.0 Universal. */
/* See LICENCE.md for full legal text. */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>

typedef unsigned long size_t;
typedef unsigned char u8;

typedef unsigned short sa_family_t;
typedef unsigned int   socklen_t;

#if (defined(__cplusplus) && __cplusplus >= 201103L) || \
    (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L)
    #define NORETURN [[noreturn]]
#else
    /* fallback */
    #define NORETURN __attribute__((noreturn))
#endif

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

extern void _exit(int);
extern char** environ;

/* Forward Declarations to prevent compilation errors */
size_t strlen(const char* s);
int strncmp(const char* a, const char* b, size_t n);

/* --------------------------------------------------
 * Buffer helpers for snprintf
 * -------------------------------------------------- */
static void buf_putc(char** buf, size_t* remain, char c) {
    if (*remain > 1) {
        **buf = c;
        (*buf)++;
        (*remain)--;
    }
}

/* FIXED: Added missing buf_puts function */
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

/* --------------------------------------------------
 * snprintf()
 * -------------------------------------------------- */
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
                /* Optimized: utilizing the shared buf_puts */
                buf_puts(&p, &remain, s);
                break;
            }
            case 'd': {
                /* FIXED: Correct handling of negative numbers */
                int val = va_arg(args, int);
                if (val < 0) {
                    buf_putc(&p, &remain, '-');
                    buf_putnum(&p, &remain, (unsigned long)(-val), 10);
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
                buf_putnum(&p, &remain, (unsigned long)va_arg(args, void*), 16); 
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

/* --------------------------------------------------
 * abort()
 * -------------------------------------------------- */
NORETURN void abort(void)
{
    _exit(127);
    while(1);
}
void __cxa_pure_virtual(void)
{
    abort();
}

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311
#define ckd_add(result, a, b) __builtin_add_overflow((a), (b), (result))
#define ckd_sub(result, a, b) __builtin_sub_overflow((a), (b), (result))
#define ckd_mul(result, a, b) __builtin_mul_overflow((a), (b), (result))
#endif 

/* --------------------------------------------------
 * Raw Linux syscall helpers with Multi-Arch Support
 * -------------------------------------------------- */

#if defined(__x86_64__)
    #define SYS_CALL "syscall"
    #define SYS_CLOBBERS "rcx", "r11", "memory"
#elif defined(__aarch64__)
    #define SYS_CALL "svc #0"
    #define SYS_CLOBBERS "memory"
#elif defined(__riscv) && (__riscv_xlen == 64)
    #define SYS_CALL "ecall"
    #define SYS_CLOBBERS "memory"
#endif

/* 64-bit architectures (x86_64, ARM64, RISC-V 64) share similar direct register passing */
#if defined(__x86_64__) || defined(__aarch64__) || (defined(__riscv) && (__riscv_xlen == 64))

static long sys_call1(long n, long a)
{
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

static long sys_call2(long n, long a, long b)
{
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

static long sys_call3(long n, long a, long b, long c)
{
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

static long sys_call6(long n, long a, long b, long c, long d, long e, long f)
{
    long ret;
#if defined(__x86_64__)
    register long r10 __asm__("r10") = d;
    register long r8  __asm__("r8")  = e;
    register long r9  __asm__("r9")  = f;
    __asm__ volatile (
        SYS_CALL
        : "=a"(ret)
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

#else /* i386 (Legacy fallback using int $0x80) */

#define SYS_CALL "int $0x80"

static long sys_call1(long n, long a)
{
    long ret;
    __asm__ volatile (SYS_CALL : "=a"(ret) : "a"(n), "b"(a) : "memory");
    return ret;
}

static long sys_call2(long n, long a, long b)
{
    long ret;
    __asm__ volatile (SYS_CALL : "=a"(ret) : "a"(n), "b"(a), "c"(b) : "memory");
    return ret;
}

static long sys_call3(long n, long a, long b, long c)
{
    long ret;
    __asm__ volatile (SYS_CALL : "=a"(ret) : "a"(n), "b"(a), "c"(b), "d"(c) : "memory");
    return ret;
}

static long sys_call6(long n, long a, long b, long c, long d, long e, long f)
{
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

/* --------------------------------------------------
 * System Calls (Cross-platform wrapper using standard POSIX NR)
 * Note: Table IDs may vary for legacy architectures (like i386),
 * but match for modern 64-bit platforms (x86_64, aarch64, riscv64).
 * -------------------------------------------------- */
int close(int fd)
{
#if defined(__x86_64__) || defined(__aarch64__) || defined(__riscv)
    return (int)sys_call1(3, fd);       /* __NR_close = 3 */
#else
    return (int)sys_call1(6, fd);       /* i386 SYS_close = 6 */
#endif
}

int socket(int domain, int type, int protocol)
{
#if defined(__x86_64__) || defined(__aarch64__) || defined(__riscv)
    return (int)sys_call3(41, domain, type, protocol);   /* __NR_socket = 41 */
#else
    unsigned long args[3];
    args[0] = (unsigned long)domain;
    args[1] = (unsigned long)type;
    args[2] = (unsigned long)protocol;
    return (int)sys_call2(102, 1, (long)args);           /* socketcall(SYS_SOCKET) */
#endif
}

int connect(int fd, const struct sockaddr* addr, socklen_t len)
{
#if defined(__x86_64__) || defined(__aarch64__) || defined(__riscv)
    return (int)sys_call3(42, fd, (long)addr, len);      /* __NR_connect = 42 */
#else
    unsigned long args[3];
    args[0] = (unsigned long)fd;
    args[1] = (unsigned long)addr;
    args[2] = (unsigned long)len;
    return (int)sys_call2(102, 3, (long)args);           /* socketcall(SYS_CONNECT) */
#endif
}

void* mmap(void* addr, size_t len, int prot, int flags, int fd, long off)
{
#if defined(__x86_64__) || defined(__aarch64__) || defined(__riscv)
    long ret = sys_call6(9, (long)addr, (long)len, (long)prot, (long)flags, (long)fd, off); /* __NR_mmap = 9 */
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

    args.addr   = (unsigned long)addr;
    args.len    = (unsigned long)len;
    args.prot   = (unsigned long)prot;
    args.flags  = (unsigned long)flags;
    args.fd     = (unsigned long)fd;
    args.offset = (unsigned long)off;

    long ret = sys_call1(90, (long)&args);   /* SYS_mmap = 90 */
    return (void*)ret;
#endif
}

int munmap(void* addr, size_t len)
{
#if defined(__x86_64__) || defined(__aarch64__) || defined(__riscv)
    return (int)sys_call2(11, (long)addr, (long)len);  /* __NR_munmap = 11 */
#else
    return (int)sys_call2(91, (long)addr, (long)len);  /* i386 SYS_munmap = 91 */
#endif
}

/* --------------------------------------------------
 * Memory Operations
 * -------------------------------------------------- */
void* memset(void* dst, int val, size_t n)
{
    u8* p = (u8*)dst;
    u8 v = (u8)val;
    while (n--) *p++ = v;
    return dst;
}

void* memcpy(void* dst, const void* src, size_t n)
{
    u8* d = (u8*)dst;
    const u8* s = (const u8*)src;
    for (size_t i = 0; i < n; ++i) d[i] = s[i];
    return dst;
}

void* memmove(void* dst, const void* src, size_t n)
{
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

/* --------------------------------------------------
 * getenv()
 * -------------------------------------------------- */
char* getenv(const char* name)
{
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

/* --------------------------------------------------
 * Mini Heap Allocator
 * -------------------------------------------------- */
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

static size_t heap_align_size(size_t n)
{
    if (n == 0) n = 1;
    if (n > ((size_t)-1) - (ALIGNMENT - 1)) abort();
    return ALIGN_UP(n);
}

static void heap_split_block(HeapBlock* block, size_t wanted)
{
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

static void heap_coalesce(void)
{
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

static HeapBlock* heap_find_free(size_t n)
{
    HeapBlock* cur = heap_head;
    while (cur) {
        if (cur->free && cur->size >= n) return cur;
        cur = cur->next;
    }
    return 0;
}

static HeapBlock* heap_new_block(size_t n)
{
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

static HeapBlock* heap_block_from_ptr(void* p)
{
    if (!p) return 0;
    HeapBlock* block = (HeapBlock*)((u8*)p - HEADER_SIZE);
    if (block->magic != MAGIC_USED) abort();
    return block;
}

void* malloc(size_t n)
{
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

void free(void* p)
{
    if (!p) return;
    HeapBlock* block = heap_block_from_ptr(p);
    if (block->free) abort();

    block->free = 1;
    heap_coalesce();
}

void* calloc(size_t n, size_t s)
{
    if (n != 0 && s > ((size_t)-1) / n) abort();
    size_t total = n * s;
    void* p = malloc(total);
    memset(p, 0, total);
    return p;
}

void* realloc(void* p, size_t n)
{
    if (!p) return malloc(n);
    if (n == 0) { free(p); return 0; }

    HeapBlock* block = heap_block_from_ptr(p);
    size_t wanted = heap_align_size(n);

    if (block->size >= wanted) {
        heap_split_block(block, wanted);
        return p;
    }

    if (block->next && block->next->free) {
        u8* block_end = (u8*)block + HEADER_SIZE + block->size;
        if (block_end == (u8*)block->next) {
            size_t combined = block->size + HEADER_SIZE + block->next->size;
            if (combined >= wanted) {
                block->size = combined;
                block->next = block->next->next;
                heap_split_block(block, wanted);
                return p;
            }
        }
    }

    void* new_ptr = malloc(n);
    size_t copy_size = (block->size > n) ? n : block->size;
    memcpy(new_ptr, p, copy_size);
    free(p);
    return new_ptr;
}

/* --------------------------------------------------
 * String Functions
 * -------------------------------------------------- */
size_t strlen(const char* s)
{
    const char* p = s;
    while (*p) ++p;
    return (size_t)(p - s);
}

int strcmp(const char* a, const char* b)
{
    while (*a && (*a == *b)) { ++a; ++b; }
    return (int)((unsigned char)*a - (unsigned char)*b);
}

int strncmp(const char* a, const char* b, size_t n)
{
    while (n && *a && (*a == *b)) { ++a; ++b; --n; }
    if (n == 0) return 0;
    return (int)((unsigned char)*a - (unsigned char)*b);
}

int memcmp(const void* a, const void* b, size_t n)
{
    const u8* x = (const u8*)a;
    const u8* y = (const u8*)b;
    for (size_t i = 0; i < n; ++i) {
        if (x[i] != y[i]) return (int)x[i] - (int)y[i];
    }
    return 0;
}

extern void println(const char* s);

int puts(const char* s)
{
    if (!s) s = "(null)";
    println(s);
    return 0;
}

char* strdup(const char* s)
{
    size_t n = strlen(s);
    if (n == (size_t)-1) abort();
    n = n + 1;

    char* out = (char*)malloc(n);
    if (!out) abort();
    memcpy(out, s, n);
    return out;
}

#ifdef __cplusplus
}
#endif

/* --------------------------------------------------
 * C++ memory management new/delete operators
 * -------------------------------------------------- */
#ifdef __cplusplus

void* operator new(size_t n) { return malloc(n); }
void* operator new[](size_t n) { return malloc(n); }

void operator delete(void* p) noexcept { free(p); }
void operator delete[](void* p) noexcept { free(p); }

#if __cplusplus >= 201402L
void operator delete(void* p, size_t) noexcept { free(p); }
void operator delete[](void* p, size_t) noexcept { free(p); }
#endif

extern "C" void __gxx_personality_v0(void) {}
extern "C" int __cxa_atexit(void (*f)(void*), void* p, void* d) { return 0; }

extern "C" int __cxa_guard_acquire(long* guard) {
    if (*guard) return 0; 
    return 1;              
}

extern "C" void __cxa_guard_release(long* guard) { *guard = 1; }
extern "C" void __cxa_guard_abort(long* guard) { abort(); }

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
