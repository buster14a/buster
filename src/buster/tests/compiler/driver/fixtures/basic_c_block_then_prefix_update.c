// A statement that begins with a prefix `++` or `--` right after a block's
// closing brace is a prefix update of the following statement, not a postfix
// update of the block. A compound literal `(type){...}` stays an operand.

static long long counter;
static unsigned char small;

static long long after_block(long long x)
{
    counter = x;
    {
        counter += 10;
    }
    --counter;
    {
        long long local = 3;
        counter += local;
    }
    ++counter;
    return counter;
}

static long long after_control(long long x)
{
    counter = x;
    if (x > 0)
    {
        counter += 1;
    }
    --counter;
    for (long long i = 0; i < 2; i += 1)
    {
    }
    ++counter;
    while (0)
    {
    }
    --counter;
    return counter;
}

static long long member_update(void)
{
    struct pair
    {
        unsigned char a;
        unsigned char b;
    } p = {1, 2};
    {
        p.a = 9;
    }
    --p.a;
    {
        p.b = 0;
    }
    --p.b;
    return (long long)p.a * 1000 + p.b;
}

static long long literal_update(void)
{
    long long r = ((long long){5})++;
    return r;
}

int main(void)
{
    small = 255;
    {
        small = 255;
    }
    ++small;
    if (small != 0)
    {
        return 1;
    }
    if (after_block(5) != 5 + 10 - 1 + 3 + 1)
    {
        return 2;
    }
    if (after_control(4) != 4 + 1 - 1 + 1 - 1)
    {
        return 3;
    }
    if (member_update() != 8 * 1000 + 255)
    {
        return 4;
    }
    if (literal_update() != 5)
    {
        return 5;
    }
    return 0;
}
