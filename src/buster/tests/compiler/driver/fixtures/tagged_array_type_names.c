// Independent expected-value fixture for the #1234 dependency of #3444.
#ifndef BUSTER_TAG_ARRAY_TYPE_NAME_FIXTURE
#define BUSTER_TAG_ARRAY_TYPE_NAME_FIXTURE(...) __VA_ARGS__
#endif
BUSTER_TAG_ARRAY_TYPE_NAME_FIXTURE(
struct item { unsigned index; char *name; };
union cell { unsigned value; void *pointer; };
enum state { STATE = 7 };
struct context { unsigned num, str_bytes; };
static __SIZE_TYPE__ observed;
static void *allocate(__SIZE_TYPE__ size)
{
    observed = size;
    return (void *)0;
}
struct item *item(struct context *ctx)
{
    __SIZE_TYPE__ fixed = sizeof(struct item[2]);
    __SIZE_TYPE__ nested = sizeof(struct item[ctx->num][2]);
    return allocate(sizeof(struct item[ctx->num++ + 1]) + ctx->str_bytes + fixed + nested);
}
static __SIZE_TYPE__ cell(unsigned count)
{
    return sizeof(union cell[count]);
}
static __SIZE_TYPE__ state(unsigned count)
{
    return sizeof(enum state[count]);
}
int main(void)
{
    struct context ctx = { .num = 3, .str_bytes = 5 };
    item(&ctx);
    return observed != sizeof(struct item) * 12 + 5 || ctx.num != 4 ||
           cell(3) != sizeof(union cell) * 3 || state(4) != sizeof(enum state) * 4;
}
)
