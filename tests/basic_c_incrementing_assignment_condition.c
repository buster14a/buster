// `while ((*d++ = *s++));` is how musl writes `wcscpy`, and the same shape
// drives `wcsftime`'s output cursor and `wcsxfrm`'s tail. The condition-leaf
// lowering splits the controlling expression at its assignment and resolves
// the left operand through a direct place path that handles identifier and
// member places only; a left operand that also increments something has an
// update of its own to emit first, so it was refused with "unsupported C
// function-body statement or expression near 'while'". A parenthesized left
// operand already had an escape to the full expression machine; the
// incrementing one now takes it too.
//
// The condition is where the bug lived, so every check reads both the copied
// bytes and where the cursors ended up: a lowering that dropped the
// increment, or applied it twice, still produces a program.

static char destination[8];
static const char source[] = "abc";

static char *copy_string(char *d, const char *s)
{
    char *start = d;
    while ((*d++ = *s++))
        ;
    return start;
}

static int copied_count(const char *s)
{
    static char sink[8];
    char *d = sink;
    int count = 0;
    while ((*d++ = *s++))
    {
        count += 1;
    }
    return count;
}

int main(void)
{
    if (copy_string(destination, source) != destination)
    {
        return 1;
    }
    if (destination[0] != 'a' || destination[1] != 'b' || destination[2] != 'c' || destination[3] != 0)
    {
        return 2;
    }
    if (copied_count(source) != 3)
    {
        return 3;
    }

    // The cursors advance exactly once per iteration, and the loop stops on
    // the terminator it just stored.
    char buffer[8];
    char *cursor = buffer;
    const char *text = "xy";
    const char *reader = text;
    int iterations = 0;
    while ((*cursor++ = *reader++))
    {
        iterations += 1;
    }
    if (iterations != 2 || cursor != buffer + 3 || reader != text + 3)
    {
        return 4;
    }
    if (buffer[0] != 'x' || buffer[1] != 'y' || buffer[2] != 0)
    {
        return 5;
    }

    // `if` takes the same path as `while`, and a decrementing left operand
    // behaves the same way.
    char pair[2] = {0, 0};
    char *tail = pair + 1;
    if (*tail-- = 7)
    {
        if (pair[1] != 7 || tail != pair)
        {
            return 6;
        }
    }
    else
    {
        return 7;
    }

    // A false condition still performs the store and the increment.
    char zeroed[2] = {9, 9};
    char *writer = zeroed;
    if (*writer++ = 0)
    {
        return 8;
    }
    if (zeroed[0] != 0 || zeroed[1] != 9 || writer != zeroed + 1)
    {
        return 9;
    }

    // A compound assignment through an incrementing place, which travels the
    // same escape.
    char totals[2] = {1, 2};
    char *accumulator = totals;
    if ((*accumulator++ += 4) != 5 || totals[0] != 5 || accumulator != totals + 1)
    {
        return 10;
    }

    // The index form, which already worked, still answers the same way.
    char indexed[2] = {0, 0};
    int position = 0;
    if (indexed[position++] = 3)
    {
        if (indexed[0] != 3 || position != 1)
        {
            return 11;
        }
    }
    else
    {
        return 12;
    }

    return 0;
}
