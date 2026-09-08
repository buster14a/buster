#include "basic_c_overaligned_stack_shapes.h"

// Each call shape has its own function. A shape which already falls back to
// canonical codegen must not hide machine selection of the first-slot case.
#define CHECK_WITH_STACK_SHIFT(expression) \
    int failures = (expression); \
    volatile unsigned char* padding = __builtin_alloca(1); \
    *padding = 37; \
    failures += (expression); \
    failures += *padding != 37; \
    return failures

static int call32(void)
{
    struct stack_aligned32 value32 = {{1, 2, 3, 4}};
    CHECK_WITH_STACK_SHIFT(stack_check32(value32));
}

static int call64(void)
{
    struct stack_aligned64 value64 = {{1, 2, 3, 4, 5, 6, 7, 8}};
    CHECK_WITH_STACK_SHIFT(stack_check64(value64));
}

static int call_after_scalars(void)
{
    struct stack_aligned64 value64 = {{1, 2, 3, 4, 5, 6, 7, 8}};
    CHECK_WITH_STACK_SHIFT(stack_check64_after_scalars(1, 2, 3, 4, 5, 6, 7, value64));
}

static int call_variadic(void)
{
    struct stack_aligned64 value64 = {{1, 2, 3, 4, 5, 6, 7, 8}};
    CHECK_WITH_STACK_SHIFT(stack_check_variadic(value64, 11, 97ULL));
}

#if !defined(OVERALIGNED_STACK_OBSERVE)
// Exercise the outgoing variadic tail against the independent host callee;
// the reverse direction has no supported Buster aggregate va_arg consumer.
static int call_variadic_tail(void)
{
    struct stack_aligned64 value64 = {{1, 2, 3, 4, 5, 6, 7, 8}};
    CHECK_WITH_STACK_SHIFT(stack_check_variadic_tail(11, value64));
}
#endif

static int call_vector32(void)
{
    stack_vector32 vector32;
    for (int index = 0; index < 32; index += 1)
    {
        vector32[index] = (unsigned char)(index + 1);
    }
    CHECK_WITH_STACK_SHIFT(stack_check_vector32(1, 2, 3, 4, 5, 6, 7, 8, vector32));
}

static int call_vector64(void)
{
    stack_vector64 vector64;
    for (int index = 0; index < 64; index += 1)
    {
        vector64[index] = (unsigned char)(index + 1);
    }
    CHECK_WITH_STACK_SHIFT(stack_check_vector64(1, 2, 3, 4, 5, 6, 7, 8, vector64));
}

int main(void)
{
    int failures = (call32() != 0) | ((call64() != 0) << 1) | ((call_after_scalars() != 0) << 2) |
                   ((call_variadic() != 0) << 3) | ((call_vector32() != 0) << 4) | ((call_vector64() != 0) << 5);
#if !defined(OVERALIGNED_STACK_OBSERVE)
    failures |= (call_variadic_tail() != 0) << 6;
#endif
    return failures;
}
