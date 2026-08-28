// C11 6.7.6.3p7: a parameter declared with array type is adjusted to a
// pointer to the element type, and a `const` written in front of that element
// type qualifies what the pointer points to, not the pointer. musl's
// `utimensat` takes `const struct timespec times[2]` and then assigns
// `times = 0` to signal "no times given", which was refused as "assignment
// operand is not a modifiable place" because the element's qualifier had been
// read as the parameter's own.
//
// The pointee's constness still has to hold, so the fixture writes through
// every non-const parameter and reads through the const ones, and checks the
// objects afterwards: moving the qualifier to the wrong place either rejects
// valid code or accepts a write that must not happen.

struct Times
{
    long seconds;
    long nanoseconds;
};

static struct Times stamps[2] = {{1, 2}, {3, 4}};

static long readable(const struct Times times[2])
{
    // The parameter is a modifiable pointer to constant elements.
    if (!times)
    {
        return -1;
    }
    if (times[0].seconds == 0 && times[1].seconds == 0)
    {
        times = 0;
    }
    if (!times)
    {
        return -2;
    }
    times += 1;
    return times[-1].seconds * 10 + times[0].seconds;
}

static long writable(struct Times times[2], long value)
{
    times[0].seconds = value;
    times = times + 1;
    times[0].seconds = value + 1;
    times -= 1;
    return times[0].seconds * 10 + times[1].seconds;
}

// The same shape with an unspecified bound, with a qualifier inside the
// brackets, and over a plain scalar element type.
static long unbounded(const long values[])
{
    const long *cursor = values;
    values = 0;
    return values ? -1 : cursor[0] + cursor[1];
}

static long bracket_qualified(long values[const 2])
{
    // `long values[const 2]` puts the qualifier on the pointer, so this one
    // is not assignable -- but its elements are.
    values[0] = 7;
    return values[0] + values[1];
}

static long counted(const char text[], unsigned long length)
{
    long total = 0;
    while (length)
    {
        total += text[0];
        text += 1;
        length -= 1;
    }
    return total;
}

int main(void)
{
    if (readable(stamps) != 13)
    {
        return 1;
    }
    if (readable(0) != -1)
    {
        return 2;
    }

    struct Times mutable_stamps[2] = {{0, 0}, {0, 0}};
    if (writable(mutable_stamps, 5) != 56 || mutable_stamps[0].seconds != 5 || mutable_stamps[1].seconds != 6)
    {
        return 3;
    }

    long values[2] = {20, 30};
    if (unbounded(values) != 50)
    {
        return 4;
    }
    if (bracket_qualified(values) != 37 || values[0] != 7 || values[1] != 30)
    {
        return 5;
    }

    if (counted("abc", 3) != 'a' + 'b' + 'c')
    {
        return 6;
    }

    // The const elements were not disturbed by the reads above.
    if (stamps[0].seconds != 1 || stamps[1].seconds != 3)
    {
        return 7;
    }

    return 0;
}
