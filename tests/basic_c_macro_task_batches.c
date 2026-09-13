// Macro task storage: nested argument rescans, growth, empty replacement
// lists, re-enable lookahead, paste, stringify and variadics. No extensions.
#define TASK_ID(x) x
#define TASK_FN(x) ((x) + 1)
#define TASK_ALIAS TASK_FN
#define TASK_EMPTY()
#define TASK_VALUE 1
#define TASK_TWO(x) ((x) + (x))
#define TASK_FOUR(x) TASK_TWO(TASK_TWO(x))
#define TASK_EIGHT(x) TASK_FOUR(TASK_TWO(x))
#define TASK_16(x) TASK_EIGHT(TASK_TWO(x))
#define TASK_32(x) TASK_16(TASK_TWO(x))
#define TASK_64(x) TASK_32(TASK_TWO(x))
#define TASK_128(x) TASK_64(TASK_TWO(x))
#define TASK_256(x) TASK_128(TASK_TWO(x))
#define TASK_CAT(a, b) a ## b
#define TASK_STR(x) #x
#define TASK_VALUES(first, ...) first, __VA_ARGS__
#define task_self task_self
#define task_cycle_a task_cycle_b
#define task_cycle_b task_cycle_a

#if TASK_ID(TASK_VALUE) != 1
#error conditional expansion changed
#endif

enum { task_sum = TASK_256(TASK_VALUE) };

static int same_text(char const* left, char const* right)
{
    unsigned index = 0;
    while (left[index] && left[index] == right[index])
    {
        index += 1;
    }
    return left[index] == right[index];
}

int main(void)
{
    int result = 0;
    int task_self = 19;
    int task_cycle_a = 23;
    int values[] = {TASK_VALUES(3, TASK_FN(3), TASK_ID(TASK_VALUE))};
    result |= task_sum != 256;
    result |= TASK_ID(TASK_FN)(7) != 8;
    result |= TASK_ALIAS(11) != 12;
    result |= TASK_ID(TASK_ALIAS)(13) != 14;
    result |= TASK_FN(TASK_FN(9)) != 11;
    result |= TASK_TWO(task_self) != 38;
    result |= TASK_TWO(task_cycle_a) != 46;
    result |= TASK_CAT(,12) != 12;
    result |= TASK_CAT(TASK_,VALUE) != 1;
    result |= TASK_EMPTY() TASK_CAT(,) 7 != 7;
    result |= !same_text(TASK_STR(task_self /* gap */ + TASK_VALUE), "task_self + TASK_VALUE");
    result |= sizeof(values) / sizeof(values[0]) != 3;
    result |= values[0] != 3 || values[1] != 4 || values[2] != 1;
    return result;
}
