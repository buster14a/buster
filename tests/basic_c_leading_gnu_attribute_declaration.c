// A GNU attribute list *leading* a block-scope declaration (#1685):
//
//     __attribute__((unused)) unsigned long long r;
//
// The parser bound the declaration, but the lowering body walker classified
// the statement from its first token, which is `__attribute__`, and lowered
// it as an expression -- "could not lower unbound identifier
// '__attribute__'". The walker already stepped over a C23 `[[...]]` sequence
// in that position; the GNU spelling reaches it through Buster's own
// BUSTER_UNUSED_DECL in the tests-enabled unity build.
//
// `aligned` is here because stepping over the list in the walker is only
// correct while the parser still reads what it says off the declaration.

typedef unsigned long long u64;

struct pair
{
    int first;
    int second;
};

static int fall(int value)
{
    int result = 0;
    switch (value)
    {
    case 0:
        result += 1;
        __attribute__((fallthrough));
    case 1:
        result += 2;
        break;
    default:
        break;
    }
    return result;
}

int main(void)
{
    __attribute__((unused)) unsigned long long unassigned;
    __attribute__((unused)) u64 named = 7;
    __attribute__((unused)) static int kept = 3;
    __attribute__((unused)) const int first = 2, second = 5;
    __attribute__((unused)) struct pair pair = {11, 13};
    __attribute__((unused)) __attribute__((unused)) int twice = 4;
    __attribute((unused)) int short_spelling = 1;
    __attribute__((aligned(64))) char aligned_first;
    __attribute__((aligned(64))) char aligned_second;
    int loop_total = 0;
    for (__attribute__((unused)) int index = 0; index < 3; index += 1)
    {
        loop_total += index;
    }
    if (named + (u64)kept != 10 || first + second != 7 || pair.first + pair.second != 24)
    {
        return 1;
    }
    if (twice != 4 || short_spelling != 1 || loop_total != 3)
    {
        return 2;
    }
    if (((unsigned long long)&aligned_first % 64) != 0 || ((unsigned long long)&aligned_second % 64) != 0)
    {
        return 3;
    }
    if (fall(0) != 3 || fall(1) != 2)
    {
        return 4;
    }
    return 0;
}
