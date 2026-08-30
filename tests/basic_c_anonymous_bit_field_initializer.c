// C11 6.7.9p9: unnamed members of a structure object take no part in
// initialization. A positional brace list therefore skips an anonymous
// bit-field and lands on the next named member; assigning to the anonymous one
// instead gave every member at or after it its right-hand neighbour's value and
// left the last one zero (issue #818). The static spelling was already right,
// so each shape here is built at runtime -- through a variable initializer
// where a constant would be folded into a static image and never reach the
// store path that was wrong.

static int runtime_two = 2;
static int runtime_three = 3;

struct Separated
{
    unsigned a : 3;
    unsigned : 5;
    unsigned b : 7;
};

// A zero-width separator is unnamed too, and moves the next member to its
// declared type's boundary without taking an initializer of its own.
struct ZeroWidth
{
    unsigned a : 3;
    unsigned : 0;
    unsigned b : 5;
};

// The anonymous member sits between two ordinary members rather than between
// two bit-fields, which is where the shifted value used to be visible without
// any bit arithmetic.
struct Mixed
{
    int first;
    unsigned : 4;
    int second;
    int third;
};

struct Leading
{
    unsigned : 6;
    unsigned value : 5;
};

// A designated initializer resolves its member by name and never walked the
// positional cursor, so it is the control that says the skip did not move it.
static int designated_is_unchanged(void)
{
    struct Separated designated = {.b = 3};
    return designated.a == 0 && designated.b == 3;
}

int main(void)
{
    struct Separated separated = {1, runtime_two};
    struct ZeroWidth zero_width = {1, runtime_two};
    struct Mixed mixed = {7, runtime_two, runtime_three};
    struct Leading leading = {runtime_two};
    struct Separated zeroed = {0};
    int result = 0;
    if (separated.a != 1 || separated.b != 2)
    {
        result = 1;
    }
    else if (zero_width.a != 1 || zero_width.b != 2)
    {
        result = 2;
    }
    else if (mixed.first != 7 || mixed.second != 2 || mixed.third != 3)
    {
        result = 3;
    }
    else if (leading.value != 2)
    {
        result = 4;
    }
    else if (zeroed.a != 0 || zeroed.b != 0)
    {
        result = 5;
    }
    else if (!designated_is_unchanged())
    {
        result = 6;
    }
    return result;
}
