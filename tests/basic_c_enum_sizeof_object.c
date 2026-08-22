// An enum-constant initializer may apply sizeof to an object, not only to a
// type name: the array-length idiom `enum { N = sizeof(t) / sizeof(t[0]) }`
// is the common spelling. The enum evaluator resolves these without the
// type-parse machine (c_parse_sizeof_operand_expression_layout in
// c_parse.c); when it could not, the whole enum type failed silently and
// every one of its enumerators became "use of undeclared identifier" at its
// first function-body use, while file-scope uses still folded. Inferred-
// length arrays are the hard case: their element count normally appears in
// a whole-file pass that has not run yet when the enum needs it.
typedef struct Item
{
    int first;
    short second;
} Item;

static Item table[] = {{1, 2}, {3, 4}, {5, 6}};
static int scalars[] = {10, 20, 30, 40};
static char message[] = "sized";
static double grid[][2] = {{1.0, 2.0}, {3.0, 4.0}, {5.0, 6.0}};
static int explicit_table[3];
static int* pointer = scalars;

enum
{
    TABLE_LENGTH = sizeof(table) / sizeof(table[0]),
    SCALAR_BYTES = sizeof(scalars),
    MESSAGE_BYTES = sizeof(message),
    GRID_ROWS = sizeof(grid) / sizeof(grid[0]),
    EXPLICIT_LENGTH = sizeof(explicit_table) / sizeof(explicit_table[0]),
    POINTEE_BYTES = sizeof(*pointer),
    MEMBER_BYTES = sizeof(table[1].second),
    PARENTHESIZED = sizeof((table)) / sizeof((table[0])),
};

// The same enumerators must also work as array bounds, the shape that first
// exposed the failure.
static unsigned char dependent[TABLE_LENGTH * 2];

// A member of the enum still being declared, referenced by a later sizeof.
enum
{
    SELF_BASE = 7,
    SELF_SIZE = sizeof(SELF_BASE),
};

// Array type names inside the enum evaluator, whose bounds spell an earlier
// enumerator of the same enum, a nested sizeof, or a named struct tag. These
// resolve through the machineless base type inside c_parse_type_layout's
// bound walk, because the enum evaluator runs inside the type-parse machine
// and cannot reenter it.
typedef unsigned short BoundElement;
struct BoundTag
{
    long long wide[3];
};

enum
{
    BOUND_SELF = 5,
    BOUND_BY_SELF = sizeof(int[BOUND_SELF]),
    BOUND_BY_SIZEOF = sizeof(char[sizeof(BoundElement)]),
    BOUND_BY_TAG = sizeof(char[sizeof(struct BoundTag)]),
    BOUND_PLAIN = sizeof(char[3]),
};

int main(void)
{
    int result = 0;
    if (TABLE_LENGTH != 3)
    {
        result = 1;
    }
    else if (SCALAR_BYTES != 4 * sizeof(int))
    {
        result = 2;
    }
    else if (MESSAGE_BYTES != 6)
    {
        result = 3;
    }
    else if (GRID_ROWS != 3)
    {
        result = 4;
    }
    else if (EXPLICIT_LENGTH != 3 || POINTEE_BYTES != sizeof(int))
    {
        result = 5;
    }
    else if (MEMBER_BYTES != sizeof(short) || PARENTHESIZED != 3)
    {
        result = 6;
    }
    else if (sizeof(dependent) != 6)
    {
        result = 7;
    }
    else if (SELF_BASE != 7 || SELF_SIZE != sizeof(int))
    {
        result = 8;
    }
    else if (BOUND_BY_SELF != 20 || BOUND_BY_SIZEOF != 2)
    {
        result = 9;
    }
    else if (BOUND_BY_TAG != 24 || BOUND_PLAIN != 3)
    {
        result = 10;
    }
    return result;
}
