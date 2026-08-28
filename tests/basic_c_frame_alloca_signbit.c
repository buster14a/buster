// Three GNU builtins QuickJS needs and Buster did not have.  It measures its
// own recursion depth with `__builtin_frame_address(0)`, allocates argument
// vectors with `alloca` (which glibc defines as `__builtin_alloca`), and
// orders -0.0 against +0.0 in its typed-array sort with `signbit`.
extern void *memset(void *destination, int value, unsigned long count);

static unsigned long stack_mark(void)
{
    return (unsigned long)__builtin_frame_address(0);
}

static unsigned long deeper(int depth)
{
    return depth ? deeper(depth - 1) : stack_mark();
}

static int sum_alloca(int count)
{
    int *values = (int *)__builtin_alloca((unsigned long)count * sizeof(int));
    int total = 0;
    memset(values, 0, (unsigned long)count * sizeof(int));
    for (int index = 0; index < count; index += 1)
    {
        values[index] = index + 1;
    }
    for (int index = 0; index < count; index += 1)
    {
        total += values[index];
    }
    return total;
}

static int compare_doubles(double left, double right)
{
    if (left != left) return right != right ? 0 : 1;
    if (right != right) return -1;
    if (left < right) return -1;
    if (left > right) return 1;
    if (left != 0) return 0;
    if (__builtin_signbit(left)) return __builtin_signbit(right) ? 0 : -1;
    return __builtin_signbit(right) ? 1 : 0;
}

int main(void)
{
    // The stack grows down on every target this fixture runs on, so a deeper
    // frame's address is below a shallower one's.
    unsigned long outer = stack_mark();
    unsigned long inner = deeper(8);
    if (inner >= outer) return 1;
    if (sum_alloca(4) != 10) return 2;
    if (sum_alloca(100) != 5050) return 3;
    // Repeated allocation inside one frame keeps every block live.
    int *first = (int *)__builtin_alloca(64);
    int *second = (int *)__builtin_alloca(64);
    first[0] = 1;
    second[0] = 2;
    if (first == second || first[0] != 1 || second[0] != 2) return 4;
    double negative_zero = -0.0;
    double positive_zero = 0.0;
    if (!__builtin_signbit(negative_zero)) return 5;
    if (__builtin_signbit(positive_zero)) return 6;
    if (__builtin_signbit(-1.5) != 1) return 7;
    if (__builtin_signbit(1.5) != 0) return 8;
    if (__builtin_signbitf(-2.5f) != 1) return 9;
    if (compare_doubles(negative_zero, positive_zero) != -1) return 10;
    if (compare_doubles(positive_zero, negative_zero) != 1) return 11;
    if (compare_doubles(negative_zero, negative_zero) != 0) return 12;
    return 0;
}
