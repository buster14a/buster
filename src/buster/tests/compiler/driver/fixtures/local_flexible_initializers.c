// Independent expected-value reductions for #3444; no musl source is imported.
// Compile this file directly for the reference control, or stringify the same
// input inside the registered compiler-driver regression.
#ifndef BUSTER_FLEXIBLE_INITIALIZER_FIXTURE
#define BUSTER_FLEXIBLE_INITIALIZER_FIXTURE(...) __VA_ARGS__
#endif
BUSTER_FLEXIBLE_INITIALIZER_FIXTURE(
struct match
{
    struct match *next;
    char name[];
};
struct dso
{
    void *base;
    long values[3];
    int marker;
    char buf[];
};
static int glob_shape(void)
{
    struct match head = { .next = (void *)0 }, *tail = &head;
    return tail->next != (void *)0;
}
static int dynlink_shape(void)
{
    struct dso *p, temp_dso = {0};
    p = &temp_dso;
    return p->base != (void *)0 || p->values[0] != 0 ||
           p->values[1] != 0 || p->values[2] != 0 || p->marker != 0;
}
static int calls;
static int value(void)
{
    calls += 1;
    return 37;
}
extern void *malloc(__SIZE_TYPE__);
extern void free(void *);
static int copy_shape(void)
{
    unsigned char *backing = malloc(128);
    int failed = 1;
    if (backing)
    {
        struct dso object = { .values = {11, 0, 23}, .marker = value() };
        struct dso *copy = (void *)backing;
        unsigned long index;
        for (index = 0; index < 128; index += 1)
        {
            backing[index] = 90;
        }
        *copy = object;
        failed = calls != 1 || copy->base != (void *)0 ||
                 copy->values[0] != 11 || copy->values[1] != 0 ||
                 copy->values[2] != 23 || copy->marker != 37;
        for (index = sizeof object; index < 128; index += 1)
        {
            failed |= backing[index] != 90;
        }
        free(backing);
    }
    return failed;
}
int main(void)
{
    return glob_shape() | (dynlink_shape() << 1) | (copy_shape() << 2);
}
)
