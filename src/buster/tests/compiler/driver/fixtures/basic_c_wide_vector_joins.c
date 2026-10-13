// Whole vector images cross branches, loop backedges, calls and three-way
// parallel-copy cycles. Scalar lane expectations observe every byte's lane.
typedef unsigned int Words32 __attribute__((vector_size(32)));
typedef unsigned int Words64 __attribute__((vector_size(64)));
typedef double Doubles32 __attribute__((vector_size(32)));
typedef double Doubles64 __attribute__((vector_size(64)));
static volatile unsigned opaque = 1;
static volatile unsigned calls;

static __attribute__((noinline)) unsigned touch(unsigned value)
{
    calls += 1;
    return value * 3 + 7 + opaque;
}

#define DEFINE_JOIN(name, type, element, lanes) \
static __attribute__((noinline)) void name##_branch(type* output, type* input, unsigned choose) \
{ \
    type value; \
    if (choose) { value = input[0] + 11; } \
    else { value = input[1] + 23; } \
    *output = value; \
} \
static __attribute__((noinline)) void name##_ternary(type* output, type* input, unsigned choose) \
{ \
    *output = choose ? input[0] + 31 : input[1] + 47; \
} \
static __attribute__((noinline)) void name##_loop(type* output, type* input, unsigned rounds) \
{ \
    type value = input[0]; \
    for (unsigned index = 0; index < rounds; index += 1) { value += index; } \
    *output = value; \
} \
static __attribute__((noinline)) void name##_calls(type* output, type* input, unsigned rounds, unsigned choose) \
{ \
    type a = input[0], b = input[1], c = input[2]; \
    for (unsigned index = 0; index < rounds; index += 1) \
    { \
        type saved = a; a = b; b = c; c = saved; \
        a += touch(index); \
    } \
    *output = choose == 0 ? a : choose == 1 ? b : c; \
} \
static int name##_check(void) \
{ \
    int result = 0; \
    element input_lanes[3 * lanes] __attribute__((aligned(64))); \
    type* input = (type*)input_lanes; \
    struct { unsigned long long before; element scalar[lanes] __attribute__((aligned(64))); unsigned long long after; } output; \
    output.before = 0x123456789abcdef0ULL; output.after = 0xfedcba9876543210ULL; \
    for (unsigned value = 0; value < 3; value += 1) \
    { \
        for (unsigned lane = 0; lane < lanes; lane += 1) { input_lanes[value * lanes + lane] = (element)(value * 101 + lane * 7 + opaque); } \
    } \
    for (unsigned choose = 0; choose < 3; choose += 1) \
    { \
        name##_branch((type*)output.scalar, input, choose); \
        for (unsigned lane = 0; lane < lanes; lane += 1) \
        { \
            if (output.scalar[lane] != input_lanes[(choose ? 0 : 1) * lanes + lane] + (element)(choose ? 11 : 23)) { result |= 1; } \
        } \
        name##_ternary((type*)output.scalar, input, choose); \
        for (unsigned lane = 0; lane < lanes; lane += 1) \
        { \
            if (output.scalar[lane] != input_lanes[(choose ? 0 : 1) * lanes + lane] + (element)(choose ? 31 : 47)) { result |= 2; } \
        } \
        for (unsigned rounds = 0; rounds < 8; rounds += 1) \
        { \
            name##_loop((type*)output.scalar, input, rounds); \
            for (unsigned lane = 0; lane < lanes; lane += 1) \
            { \
                if (output.scalar[lane] != input_lanes[lane] + (element)(rounds * (rounds - 1) / 2)) { result |= 4; } \
            } \
            element expected[3][lanes]; \
            for (unsigned value = 0; value < 3; value += 1) \
            { \
                for (unsigned lane = 0; lane < lanes; lane += 1) { expected[value][lane] = input_lanes[value * lanes + lane]; } \
            } \
            for (unsigned index = 0; index < rounds; index += 1) \
            { \
                for (unsigned lane = 0; lane < lanes; lane += 1) \
                { \
                    element saved = expected[0][lane]; \
                    expected[0][lane] = expected[1][lane] + (element)(index * 3 + 8); \
                    expected[1][lane] = expected[2][lane]; expected[2][lane] = saved; \
                } \
            } \
            name##_calls((type*)output.scalar, input, rounds, choose); \
            for (unsigned lane = 0; lane < lanes; lane += 1) \
            { \
                if (output.scalar[lane] != expected[choose][lane]) { result |= 8; } \
            } \
            if (output.before != 0x123456789abcdef0ULL || output.after != 0xfedcba9876543210ULL) { result |= 16; } \
        } \
    } \
    return result; \
}

DEFINE_JOIN(words32, Words32, unsigned int, 8)
DEFINE_JOIN(words64, Words64, unsigned int, 16)
DEFINE_JOIN(doubles32, Doubles32, double, 4)
DEFINE_JOIN(doubles64, Doubles64, double, 8)

int main(void)
{
    int result = words32_check() | words64_check() | doubles32_check() | doubles64_check();
    return result | (calls != 336);
}
