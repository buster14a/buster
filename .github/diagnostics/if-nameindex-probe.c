#ifndef CASE
#define CASE 0
#endif
#if CASE & 1
#define factory item
#else
#define factory make_items
#endif
struct item { unsigned index; char *name; };
struct context { unsigned num, allocated, str_bytes; unsigned hash[64]; };
#if (CASE & 2) && !(CASE & 16)
struct item *factory(struct context *);
#endif
static unsigned long observed;
static void *allocate(unsigned long n) { observed = n; return (void *)0; }
static void escape(struct context *ctx) { ctx->num = 3; ctx->str_bytes = 5; }
#if CASE & 16
unsigned long factory(struct context *incoming) {
    struct context local, *ctx = &local;
    ctx->num = incoming->num;
    ctx->str_bytes = incoming->str_bytes;
    return sizeof(struct item[ctx->num+1]) + ctx->str_bytes;
}
#else
struct item *factory(struct context *incoming) {
    struct context local, *ctx = &local;
    struct item *result = (void *)0, *d;
    ctx->num = incoming->num;
    ctx->str_bytes = incoming->str_bytes;
#if CASE & 4
    escape(ctx);
#endif
#if CASE & 8
    if (ctx->num == 0) goto out;
#endif
    result = allocate(sizeof(struct item[ctx->num+1]) + ctx->str_bytes);
    d = result;
out:
    return d;
}
#endif
int main(void) {
    struct context ctx = { .num = 3, .str_bytes = 5 };
#if CASE & 16
    observed = factory(&ctx);
#else
    factory(&ctx);
#endif
    return observed != sizeof(struct item) * 4 + 5;
}
