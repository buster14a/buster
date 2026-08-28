// `E1[E2]` is defined as `*((E1)+(E2))` (C11 6.5.2.1p2), and `+` commutes, so
// the subscripted operand may be written on either side of the brackets.
// musl's `getenv` and `unsetenv` both test `l[*e] == '='` with the length on
// the left, which the lowering rejected with "could not form an indexed
// place" because it only ever read the base from the left operand.
//
// Every check below is written both ways round and compared against the same
// answer, so a lowering that quietly indexed the wrong operand -- reading
// `e[l]`'s element as if it were `l[e]`'s -- fails here rather than producing
// a plausible byte.

static const char letters[] = "abcdef";

static int calls;

static int counted(int value)
{
    calls += 1;
    return value;
}

struct Pair
{
    int first;
    int second;
};

static struct Pair pairs[3] = {{1, 2}, {3, 4}, {5, 6}};

int main(void)
{
    const char *cursor = letters;

    // Pointer on the right, index on the left.
    unsigned long index = 2;
    if (index[cursor] != 'c' || index[cursor] != cursor[index])
    {
        return 1;
    }

    // The same over an array rather than a pointer: the array decays either
    // way round.
    if (3[letters] != 'd' || 3[letters] != letters[3])
    {
        return 2;
    }

    // A signed index, and a negative one applied to an interior pointer.
    const char *middle = letters + 3;
    int offset = -1;
    if (offset[middle] != 'c' || offset[middle] != middle[offset])
    {
        return 3;
    }

    // The reversed form is a place: it assigns, takes an address, and
    // compound-assigns like the ordinary one.
    char buffer[4] = {0, 0, 0, 0};
    1[buffer] = 'x';
    2[buffer] = 'y';
    2[buffer] += 1;
    if (buffer[1] != 'x' || buffer[2] != 'z' || &1[buffer] != buffer + 1)
    {
        return 4;
    }

    // Increment through the reversed form, prefix and postfix. The index is a
    // variable here because an increment whose subscript operand is spelled as
    // a literal -- `0[buffer]++` -- is still refused by the postfix scan,
    // which is a separate gap from the one this fixture covers.
    unsigned long zero = 0;
    zero[buffer] = 5;
    if (zero[buffer]++ != 5 || buffer[0] != 6 || ++zero[buffer] != 7 || buffer[0] != 7)
    {
        return 5;
    }

    // An aggregate element selected through the reversed form.
    if (1[pairs].second != 4 || 1[pairs].second != pairs[1].second)
    {
        return 6;
    }
    2[pairs].first = 9;
    if (pairs[2].first != 9)
    {
        return 7;
    }

    // Each operand is evaluated exactly once whichever side it is written on.
    calls = 0;
    if (counted(1)[letters] != 'b' || calls != 1)
    {
        return 8;
    }
    calls = 0;
    if (letters[counted(1)] != 'b' || calls != 1)
    {
        return 9;
    }

    // Two-dimensional: `1[grid]` is the row, and the column may be reversed
    // independently of it.
    static int grid[2][3] = {{10, 11, 12}, {20, 21, 22}};
    if (1[grid][2] != 22 || 2[1[grid]] != 22 || 2[grid[1]] != 22)
    {
        return 10;
    }

    // sizeof over the reversed form measures the element, not the pointer.
    if (sizeof(0[letters]) != sizeof(char) || sizeof(0[pairs]) != sizeof(struct Pair))
    {
        return 11;
    }

    // A _Bool index converts like any other integer.
    _Bool selector = 1;
    if (selector[letters] != 'b' || selector[letters] != letters[selector])
    {
        return 12;
    }

    return 0;
}
