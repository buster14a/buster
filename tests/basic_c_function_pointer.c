#include <stddef.h>

extern void *malloc(size_t size);
extern void free(void *pointer);

typedef void *(*Allocator)(size_t);

static Allocator allocator = malloc;

int main(void)
{
    void *pointer = allocator(32);
    if (!pointer)
    {
        return 1;
    }
    free(pointer);
    return 0;
}
