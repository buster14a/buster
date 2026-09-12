#include "basic_c_overaligned_stack_shapes.h"

// Each call shape has its own function. Strict scalar runs cover the
// alignment paths independently of unsupported narrow vector signatures.
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

static struct stack_result_pair call_pair_indirect(struct stack_result_pair (*function)(struct stack_aligned64), struct stack_aligned64 value)
{
    return function(value);
}

static int call_results(void)
{
    struct stack_aligned64 value64 = {{1, 2, 3, 4, 5, 6, 7, 8}};
    struct stack_aligned32 value32 = {{1, 2, 3, 4}};
    struct stack_result_pair result = call_pair_indirect(stack_return_pair, value64);
    int failures = result.low != 9 || result.high != 9 || stack_return_float(value32) != 3.75;
    // Each alloca rounds to sixteen on x86-64. The intervening aligned call
    // must restore that exact RSP; compare integer addresses, not unrelated
    // C object pointers. The live byte also checks the earlier allocation.
    volatile unsigned char* before = __builtin_alloca(1);
    *before = 37;
    result = call_pair_indirect(stack_return_pair, value64);
    volatile unsigned char* after = __builtin_alloca(1);
    *after = 19;
    failures += (unsigned long long)before - (unsigned long long)after != 16;
    failures += result.low != 9 || result.high != 9 || *before != 37 || *after != 19;
    return failures;
}

#if !defined(OVERALIGNED_STACK_SCALAR_ONLY)
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

#endif

int main(void)
{
    int failures = (call32() != 0) | ((call64() != 0) << 1) | ((call_after_scalars() != 0) << 2) |
                   ((call_variadic() != 0) << 3) | ((call_results() != 0) << 7);
#if !defined(OVERALIGNED_STACK_SCALAR_ONLY)
    failures |= ((call_vector32() != 0) << 4) | ((call_vector64() != 0) << 5);
#endif
#if !defined(OVERALIGNED_STACK_OBSERVE)
    failures |= (call_variadic_tail() != 0) << 6;
#endif
    return failures;
}
