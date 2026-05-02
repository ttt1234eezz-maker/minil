/* This software is dedicated to the public domain under CC0 1.0 Universal. */
/* See LICENCE.md for full legal text. */
#ifdef __cplusplus
extern "C" {
#endif

static uint8_t heap[1024 * 1024];
static size_t heap_off = 0;

void* malloc(size_t n)
{
    if (heap_off + n > sizeof(heap))
        return NULL;

    void* p = &heap[heap_off];
    heap_off += (n + 7) & ~7; // align
    return p;
}

void free(void* p) { (void)p; }

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

#ifdef __cplusplus
}
#endif
