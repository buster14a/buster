// A member whose type's alignment was lowered sits at an offset that is not a
// multiple of its own size, and the AArch64 encoder refused to address it: the
// scaled unsigned immediate form of a load or store addresses a multiple of the
// access size, and the encoder failed closed rather than taking the scratch-
// register path it already takes for an out-of-range offset. The row had
// already selected, so the whole enclosing function came back as an
// encode-stage fallback to the canonical emitter (issue #813) -- which is why
// the driver test pins the fallback count as well as running this: the answers
// are right either way, and only the count says the encoder carried them.
//
// The access itself is legal. AArch64 permits an unaligned load or store to
// normal memory; only the immediate form is scaled.

typedef int lowered __attribute__((aligned(2)));
typedef long long wide_lowered __attribute__((aligned(2)));
typedef short half_lowered __attribute__((aligned(1)));

struct four_at_two
{
    char tag;
    lowered value;
    char trailer;
};

struct eight_at_two
{
    char tag;
    wide_lowered value;
    char trailer;
};

struct two_at_one
{
    char tag;
    half_lowered value;
    char trailer;
};

// The same shapes reached through a pointer rather than off the frame, which
// takes the unscaled pointer rows instead and must agree.
static int read_through_pointer(struct four_at_two* record)
{
    return record->value == 0x5e6f7a8b && record->tag == 'z' && record->trailer == '!';
}

static struct four_at_two by_value(struct four_at_two record)
{
    record.value += 1;
    return record;
}

int main(void)
{
    struct four_at_two four;
    struct eight_at_two eight;
    struct two_at_one two;
    if (sizeof(struct four_at_two) != 8 || _Alignof(struct four_at_two) != 2)
    {
        return 1;
    }
    if (sizeof(struct eight_at_two) != 12 || _Alignof(struct eight_at_two) != 2)
    {
        return 2;
    }
    if (sizeof(struct two_at_one) != 4 || _Alignof(struct two_at_one) != 1)
    {
        return 3;
    }
    four.tag = 'z';
    four.value = 0x5e6f7a8b;
    four.trailer = '!';
    if (four.tag != 'z' || four.value != 0x5e6f7a8b || four.trailer != '!')
    {
        return 4;
    }
    eight.tag = 'y';
    eight.value = 0x0123456789abcdefLL;
    eight.trailer = '?';
    if (eight.tag != 'y' || eight.value != 0x0123456789abcdefLL || eight.trailer != '?')
    {
        return 5;
    }
    two.tag = 'x';
    two.value = -1234;
    two.trailer = '.';
    if (two.tag != 'x' || two.value != -1234 || two.trailer != '.')
    {
        return 6;
    }
    if (!read_through_pointer(&four))
    {
        return 7;
    }
    {
        struct four_at_two returned = by_value(four);
        if (returned.value != 0x5e6f7a8c || returned.tag != 'z' || returned.trailer != '!')
        {
            return 8;
        }
    }
    {
        // A whole-record copy and an array of them, which reach the frame at
        // offsets the element size does not divide either.
        struct four_at_two table[3];
        for (int index = 0; index < 3; index += 1)
        {
            table[index].tag = (char)('a' + index);
            table[index].value = 1000 + index;
            table[index].trailer = (char)('A' + index);
        }
        struct four_at_two copy = table[2];
        if (copy.tag != 'c' || copy.value != 1002 || copy.trailer != 'C')
        {
            return 9;
        }
        if (table[0].value != 1000 || table[1].value != 1001)
        {
            return 10;
        }
    }
    return 0;
}
