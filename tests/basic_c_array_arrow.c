#include <stddef.h>

typedef struct CJsonHooks
{
    void *(*allocate)(size_t);
} CJsonHooks;

typedef struct PrintBuffer
{
    unsigned char *buffer;
    size_t length;
} PrintBuffer;

static unsigned char storage;

static void *allocate(size_t size)
{
    return size == 7 ? &storage : NULL;
}

static int consume(unsigned char *buffer)
{
    return buffer == &storage;
}

static int make_buffer(const CJsonHooks *hooks)
{
    PrintBuffer buffer[1] = {{0}};
    buffer->buffer = (unsigned char *)hooks->allocate(7);
    buffer->length = 7;
    return consume(buffer->buffer) && buffer->length == 7;
}

int main(void)
{
    CJsonHooks hooks = {allocate};
    return make_buffer(&hooks) ? 0 : 1;
}
