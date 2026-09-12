#include "basic_c_overaligned_stack_shapes.h"

// Only the host callee inspects the incoming argument's address. The reverse
// mixed link checks value integrity: the canonical Buster callee copies the
// parameter into a local slot, whose alignment is a separate existing defect.
#if defined(OVERALIGNED_STACK_OBSERVE)
#define STACK_PARAMETER_MISALIGNED(value, alignment) stack_observe_alignment(&(value), alignment)
#else
#define STACK_PARAMETER_MISALIGNED(value, alignment) 0
#endif

int stack_check32(struct stack_aligned32 value)
{
    int failures = STACK_PARAMETER_MISALIGNED(value, 32);
    for (int index = 0; index < 4; index += 1)
    {
        failures += value.values[index] != (unsigned long long)(index + 1);
    }
    return failures;
}

int stack_check64(struct stack_aligned64 value)
{
    int failures = STACK_PARAMETER_MISALIGNED(value, 64);
    for (int index = 0; index < 8; index += 1)
    {
        failures += value.values[index] != (unsigned long long)(index + 1);
    }
    return failures;
}

int stack_check64_after_scalars(int a, int b, int c, int d, int e, int f, int g, struct stack_aligned64 value)
{
    int failures = STACK_PARAMETER_MISALIGNED(value, 64);
    failures += a != 1 || b != 2 || c != 3 || d != 4 || e != 5 || f != 6 || g != 7;
    for (int index = 0; index < 8; index += 1)
    {
        failures += value.values[index] != (unsigned long long)(index + 1);
    }
    return failures;
}

int stack_check_variadic(struct stack_aligned64 value, int marker, ...)
{
    int failures = STACK_PARAMETER_MISALIGNED(value, 64);
    failures += marker != 11;
    for (int index = 0; index < 8; index += 1)
    {
        failures += value.values[index] != (unsigned long long)(index + 1);
    }
    return failures;
}

#if defined(OVERALIGNED_STACK_OBSERVE)
// The host's va_arg independently rounds its overflow cursor to the ABI
// alignment. Misaligning the outgoing tail therefore corrupts the values.
// Buster's unsupported over-aligned aggregate va_arg is a separate concern.
int stack_check_variadic_tail(int marker, ...)
{
    __builtin_va_list arguments;
    __builtin_va_start(arguments, marker);
    struct stack_aligned64 value = __builtin_va_arg(arguments, struct stack_aligned64);
    __builtin_va_end(arguments);
    int failures = marker != 11;
    for (int index = 0; index < 8; index += 1)
    {
        failures += value.values[index] != (unsigned long long)(index + 1);
    }
    return failures;
}
#endif

struct stack_result_pair stack_return_pair(struct stack_aligned64 value)
{
    struct stack_result_pair result = {value.values[0] + value.values[7], value.values[1] + value.values[6]};
    return result;
}

double stack_return_float(struct stack_aligned32 value)
{
    return value.values[2] * 1.25;
}

#if !defined(OVERALIGNED_STACK_SCALAR_ONLY)
// Exhaust the eight vector argument registers before each wide vector so
// both host compiler feature sets must pass it in the stack argument area.
int stack_check_vector32(double a, double b, double c, double d, double e, double f, double g, double h, stack_vector32 value)
{
    int failures = a != 1 || b != 2 || c != 3 || d != 4 || e != 5 || f != 6 || g != 7 || h != 8;
    for (int index = 0; index < 32; index += 1)
    {
        failures += value[index] != (unsigned char)(index + 1);
    }
    return failures;
}

int stack_check_vector64(double a, double b, double c, double d, double e, double f, double g, double h, stack_vector64 value)
{
    int failures = a != 1 || b != 2 || c != 3 || d != 4 || e != 5 || f != 6 || g != 7 || h != 8;
    for (int index = 0; index < 64; index += 1)
    {
        failures += value[index] != (unsigned char)(index + 1);
    }
    return failures;
}

#endif
