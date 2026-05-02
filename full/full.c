/* This software is dedicated to the public domain under CC0 1.0 Universal. */
/* See LICENCE.md for full legal text. */
#ifdef __cplusplus
extern "C" {
#endif

extern void _exit(int);

typedef unsigned long  size_t;
typedef unsigned char  u8;

/* --------------------------------------------------
 * abort()
 * Required by C and C++
 * -------------------------------------------------- */
void abort(void)
{
    _exit(127);
}

void __cxa_pure_virtual(void)
{
    abort();
}

static u8     heap[1024 * 1024];
static size_t heap_off = 0;

void* malloc(size_t n)
{
    if (heap_off + n > sizeof(heap))
        abort();

    void* p = &heap[heap_off];
    heap_off += (n + 7) & ~7; // align
    return p;
}

void free(void* p) { (void)p; }


/* --------------------------------------------------
 * memset (minimal, required)
 * -------------------------------------------------- */
void* memset(void* dst, int val, size_t n)
{
    u8* p = (u8*)dst;
    u8  v = (u8)val;

    while (n--)
        *p++ = v;

    return dst;
}


void* calloc(size_t n, size_t s)
{
    size_t total = n * s;
    void* p = malloc(total);
    if (!p)
        abort();

    memset(p, 0, total);
    return p;
}

void* memcpy(void* dst, const void* src, size_t n)
{
    uint8_t* d = dst;
    const uint8_t* s = src;
    for (size_t i = 0; i < n; ++i)
        d[i] = s[i];
    return dst;
}

void* calloc(size_t n, size_t s)
{
    size_t total = n * s;
    void* p = malloc(total);
    memset(p, 0, total);
    return p;
}


#ifdef __cplusplus
}
#endif


#ifdef __cplusplus
void* operator new(size_t n)            { return malloc(n); }
void* operator new[](size_t n)           { return malloc(n); }
void  operator delete(void* p) noexcept  { free(p); }
void  operator delete[](void* p) noexcept{ free(p); }
#endif
