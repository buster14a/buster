// C11 6.10.3.3: empty operands remain placemarkers until all token pastes
// finish. An earlier token must never become an empty parameter's operand.
#define CAT(left, right) left ## right
#define CAT3(first, second, third) first ## second ## third
#define CAT4(first, second, third, fourth) first ## second ## third ## fourth
#define PREFIX(left, right) 1 + left ## right
#define SUFFIX(left, right) left ## right + 2
#define SURROUND(left, right) 1 left ## right + 2
#define EMPTY
#define EMPTYvalue 37
#define RESCAN_VALUE 41

// A comma pasted against a named empty parameter is preserved even in a
// variadic macro. Only the marked GNU `, ## __VA_ARGS__` idiom deletes it.
#define NAMED_COMMA(name, ...) {0, ## name 2}
#define ORDINARY_COMMA(name) {0, ## name 3}
#define CALL(function, first, ...) function(first, ## __VA_ARGS__)
#define FORWARD(function, first, ...) CALL(function, first, ## __VA_ARGS__)

CAT(,)
CAT3(,,)
CAT4(,,,)

static int one_argument(int value)
{
    return value;
}

static int two_arguments(int first, int second)
{
    return first * 10 + second;
}

int CAT(,main)(void)
{
    int result = 0;
    int named_comma[] = NAMED_COMMA(, unused);
    int ordinary_comma[] = ORDINARY_COMMA();

    result |= CAT(,7) != 7;
    result |= CAT(7,) != 7;
    result |= CAT(1,2) != 12;
    result |= PREFIX(,2) != 3;
    result |= SUFFIX(1,) != 3;
    result |= SURROUND(,) != 3;

    result |= CAT3(,,7) != 7;
    result |= CAT3(,7,) != 7;
    result |= CAT3(7,,) != 7;
    result |= CAT3(,1,2) != 12;
    result |= CAT3(1,,2) != 12;
    result |= CAT3(1,2,) != 12;
    result |= CAT3(1,2,3) != 123;
    result |= CAT4(,,,7) != 7;
    result |= CAT4(1,,,2) != 12;
    result |= CAT4(,1,,2) != 12;

    // Only the boundary tokens of nonempty arguments participate in pasting.
    result |= CAT(,1 + 2) != 3;
    result |= CAT(1 + 2,) != 3;
    result |= CAT3(1 + 2,,3 + 4) != 28;
    result |= CAT(EMPTY,value) != 37;
    result |= CAT(,RESCAN_VALUE) != 41;
    result |= (1 CAT(,EMPTY) + 2) != 3;

    result |= sizeof(named_comma) / sizeof(named_comma[0]) != 2;
    result |= named_comma[1] != 2;
    result |= sizeof(ordinary_comma) / sizeof(ordinary_comma[0]) != 2;
    result |= ordinary_comma[1] != 3;
    result |= CALL(one_argument, 3) != 3;
    result |= CALL(two_arguments, 3, 4) != 34;
    result |= FORWARD(one_argument, 5) != 5;
    result |= FORWARD(two_arguments, 5, 6) != 56;

    return result;
}
