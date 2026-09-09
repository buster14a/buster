struct Padded
{
    unsigned int : 2;
    unsigned int first : 3;
    unsigned int : 0;
    unsigned int second : 4;
    unsigned int : 5;
    int tail;
};

union Choice
{
    unsigned int : 3;
    unsigned int first : 4;
    int other;
};

struct Nested
{
    unsigned int : 1;
    struct
    {
        unsigned int : 2;
        unsigned int value : 3;
    };
    int tail;
};

struct Elided
{
    struct
    {
        unsigned int : 0;
        unsigned int : 4;
        volatile unsigned int value : 3;
    };
};

struct NestedUnion
{
    union
    {
        unsigned int : 2;
        struct
        {
            unsigned int : 3;
            unsigned int value : 3;
        };
        int alternative;
    };
    int tail;
};

static int value(int input)
{
    return input;
}

int main(void)
{
    int result = 0;
    struct Padded positional = {value(5), value(9), value(37)};
    struct Padded designated = {.first = value(2), value(6), value(41)};
    struct Padded literal = (struct Padded){value(3), value(10), value(43)};
    struct Padded designated_literal = (struct Padded){.second = value(12), .tail = value(47), .first = value(4)};
    result |= positional.first != 5 || positional.second != 9 || positional.tail != 37;
    result |= designated.first != 2 || designated.second != 6 || designated.tail != 41;
    result |= literal.first != 3 || literal.second != 10 || literal.tail != 43;
    result |= designated_literal.first != 4 || designated_literal.second != 12 || designated_literal.tail != 47;

    union Choice first = {value(11)};
    union Choice other = {.other = value(29)};
    union Choice choice_literal = (union Choice){value(13)};
    result |= first.first != 11 || other.other != 29 || choice_literal.first != 13;

    struct Nested nested = {{value(6)}, value(23)};
    struct Nested promoted = {.value = value(3), .tail = value(19)};
    struct Nested nested_literal = (struct Nested){{value(7)}, value(31)};
    struct Nested promoted_literal = (struct Nested){.value = value(4), .tail = value(17)};
    result |= nested.value != 6 || nested.tail != 23;
    result |= promoted.value != 3 || promoted.tail != 19;
    result |= nested_literal.value != 7 || nested_literal.tail != 31;
    result |= promoted_literal.value != 4 || promoted_literal.tail != 17;

    struct Elided elided[1] = {{value(5)}};
    struct Elided* elided_literal = (struct Elided[]){{value(6)}};
    result |= elided[0].value != 5 || elided_literal[0].value != 6;

    struct NestedUnion anonymous_union = {{{value(2)}}, value(53)};
    struct NestedUnion union_literal = (struct NestedUnion){{{value(3)}}, value(59)};
    result |= anonymous_union.value != 2 || anonymous_union.tail != 53;
    result |= union_literal.value != 3 || union_literal.tail != 59;
    return result;
}
