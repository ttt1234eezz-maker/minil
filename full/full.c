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
