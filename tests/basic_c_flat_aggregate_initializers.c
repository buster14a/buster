struct Pair
{
    int first;
    int second;
};

struct NamedOuter
{
    struct Pair pair;
    int tail;
};

struct AnonymousOuter
{
    struct
    {
        int first;
        volatile int second;
    };
    int tail;
};

struct ArrayOuter
{
    int rows[2][2];
    int tail;
};

struct UnionOuter
{
    union
    {
        struct
        {
            int first;
            int second;
        };
        long alternative;
    };
    int tail;
};

static int order;

static int ordered_value(int expected, int value)
{
    int result = value;
    if (order != expected)
    {
        result = -1000;
    }
    order += 1;
    return result;
}

static struct Pair pair_value(int first, int second)
{
    struct Pair result = {first, second};
    return result;
}

static int named_local(void)
{
    order = 0;
    struct NamedOuter value = {
        ordered_value(0, 11),
        ordered_value(1, 13),
        ordered_value(2, 17),
    };
    return value.pair.first != 11 || value.pair.second != 13 || value.tail != 17 || order != 3;
}

static int named_literal(void)
{
    struct NamedOuter value = (struct NamedOuter){19, 23, 29};
    return value.pair.first != 19 || value.pair.second != 23 || value.tail != 29;
}

static int anonymous_local(void)
{
    struct AnonymousOuter value = {31, 37, 41};
    return value.first != 31 || value.second != 37 || value.tail != 41;
}

static int array_local(void)
{
    struct ArrayOuter value = {43, 47, 53, 59, 61};
    return value.rows[0][0] != 43 || value.rows[0][1] != 47 || value.rows[1][0] != 53 || value.rows[1][1] != 59 || value.tail != 61;
}

static int union_local(void)
{
    struct UnionOuter value = {67, 71, 73};
    return value.first != 67 || value.second != 71 || value.tail != 73;
}

static int aggregate_expression(void)
{
    struct NamedOuter direct = {pair_value(79, 83), 89};
    struct NamedOuter parenthesized = {(pair_value(97, 101)), 103};
    return direct.pair.first != 79 || direct.pair.second != 83 || direct.tail != 89 || parenthesized.pair.first != 97 ||
           parenthesized.pair.second != 101 || parenthesized.tail != 103;
}

int main(void)
{
    return named_local() || named_literal() || anonymous_local() || array_local() || union_local() || aggregate_expression();
}
