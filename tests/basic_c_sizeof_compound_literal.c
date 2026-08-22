// `sizeof (T){...}` is sizeof applied to a compound literal, not `sizeof (T)`
// with a stray brace after it: the parenthesis is the literal's type, so the
// operand extends through the braced initializer and any postfix suffix on it.
// Three bugs met in this shape, and once each was out of the way the next one
// answered with a number rather than a diagnostic, so the fixture runs:
//
//   - The expression core stopped at the type parenthesis and failed on the
//     trailing braces ("could not lower logical expression core").
//   - Every other sizeof operand scan stopped there too. In an array bound
//     that left `{1, 2, 3}` in the retokenized integer expression, the bound
//     never folded, and `sizeof pad` then answered the expression prediction's
//     int guess of 4 -- 4 whatever the real size was.
//   - The file-scope declarator suffix scan stopped at the first `=`, `,` or
//     `;` anywhere rather than the first one at top level, so a bound holding
//     a braced group ended mid-bracket and the whole declaration was dropped
//     ("use of undeclared identifier" at the first use).
//
// Every size below is what clang reports for the same translation unit. The
// checks that only read a size would still pass on a compiler that laid the
// object out too small, so the file-scope objects are written through their
// last byte as well, with neighbours on both sides to catch the overlap.

struct Pair
{
    int a;
    int b;
};

// A member wider than the first forces padding the literal must carry too.
struct Wide
{
    int a;
    long b;
};

typedef struct
{
    double d;
    char c;
} Padded;

// The bound must survive the declarator scan in both spellings.
static char unparenthesized[sizeof (int[3]){1, 2, 3}];
static char parenthesized[sizeof((int[3]){1, 2, 3})];
static char aggregate_literal[sizeof (struct Pair){1, 2}];
static char typedef_literal[sizeof (Padded){1, 2}];

// One declaration, two declarators: the comma inside the first bound's
// initializer is not the comma that separates them.
static char shared_first[sizeof (int[3]){1, 2, 3}], shared_second[sizeof (struct Pair){1, 2}];

// The operand ends at the initializer, so the rest of the bound still counts.
static char trailing_term[sizeof (int[3]){1, 2, 3} + 1];

// A compound literal is a postfix expression: these size the element and the
// member, not the whole literal.
static char subscripted[sizeof (char[3]){1}[0] + 4];
static char member_selected[sizeof (struct Pair){1, 2}.a + 4];

// _Alignof over the same shape is the literal type's alignment.
static char alignment_bound[_Alignof (double[3]){1}];

// Neighbours for the overlap checks below.
static char before_guard = 11;
static char written_through[sizeof (int[3]){1, 2, 3}];
static char after_guard = 22;

struct Holder
{
    char first[sizeof (Padded){1, 2}];
    char second[sizeof (int[3]){1, 2, 3}];
};

enum
{
    ENUM_UNPARENTHESIZED = (int)sizeof (int[5]){0},
    ENUM_PARENTHESIZED = (int)sizeof((int[5]){0}),
    ENUM_AGGREGATE = (int)sizeof (struct Pair){1, 2},
    ENUM_NEXT = ENUM_AGGREGATE + 1,
};

_Static_assert(sizeof (int[4]){0} == 16, "array compound literal bound folds");
_Static_assert(sizeof (struct Pair){1, 2} == 8, "aggregate compound literal folds");
_Static_assert(_Alignof (double[2]){0} == 8, "alignof of a compound literal folds");

static int calls;

static int counted(void)
{
    calls += 1;
    return 5;
}

int main(void)
{
    if (sizeof unparenthesized != 12 || sizeof parenthesized != 12)
    {
        return 1;
    }
    if (sizeof aggregate_literal != 8 || sizeof typedef_literal != 16)
    {
        return 2;
    }
    if (sizeof shared_first != 12 || sizeof shared_second != 8)
    {
        return 3;
    }
    if (sizeof trailing_term != 13)
    {
        return 4;
    }
    if (sizeof subscripted != 5 || sizeof member_selected != 8)
    {
        return 5;
    }
    if (sizeof alignment_bound != 8)
    {
        return 6;
    }
    // Function scope resolves the bound through the expression lowering rather
    // than the file-scope bound evaluator, so it is checked separately. A
    // literal-sized bound is a constant, not a VLA.
    char local_unparenthesized[sizeof (int[3]){1, 2, 3}];
    char local_parenthesized[sizeof((struct Pair){1, 2})];
    char local_trailing[sizeof (int[3]){1, 2, 3} + 1];
    local_unparenthesized[0] = 1;
    local_parenthesized[0] = 0;
    local_trailing[0] = 0;
    if (sizeof local_unparenthesized != 12 || sizeof local_parenthesized != 8 || sizeof local_trailing != 13 ||
        local_unparenthesized[0] != 1)
    {
        return 7;
    }
    // Expression position, including the shapes that consume postfix suffixes.
    if ((int)sizeof (int[3]){1, 2, 3} != 3 * (int)sizeof(int) || (int)sizeof((int[3]){1, 2, 3}) != 12)
    {
        return 8;
    }
    // Struct, scalar, and char-array literal types. The struct sizes are read
    // through sizeof of the type so padding is not restated here.
    if (sizeof (struct Pair){1, 2} != sizeof(struct Pair) || sizeof (struct Wide){1, 2} != sizeof(struct Wide))
    {
        return 9;
    }
    if (sizeof (int){7} != sizeof(int) || sizeof (char[2]){1} != 2)
    {
        return 10;
    }
    if ((int)sizeof (char[3]){1}[0] != 1 || (int)sizeof (struct Pair){1, 2}.a != 4)
    {
        return 11;
    }
    if ((int)_Alignof (double[3]){1} != 8)
    {
        return 12;
    }
    // The operand stops at the initializer and not before or after it: an
    // unbounded array literal takes its length from the initializer, and a
    // term after the literal still applies.
    if ((int)sizeof (int[]){1, 2, 3} != 12 || (int)sizeof (int[3]){1, 2, 3} + 1 != 13)
    {
        return 13;
    }
    // A sizeof operand is unevaluated, and widening the operand to cover the
    // initializer must not start evaluating it.
    calls = 0;
    if (sizeof (int[3]){counted(), 2, 3} != 3 * sizeof(int) || calls != 0)
    {
        return 14;
    }
    calls = 0;
    char unevaluated[sizeof (int[2]){counted(), counted()}];
    unevaluated[0] = 0;
    if (sizeof unevaluated != 8 || calls != 0)
    {
        return 15;
    }
    // Over-skip check: a call after the operand in the same expression runs,
    // both on its own and folded into surrounding arithmetic.
    calls = 0;
    if ((int)sizeof (int[3]){1, 2, 3} + counted() != 12 + 5 || calls != 1)
    {
        return 16;
    }
    calls = 0;
    if (3 + (int)sizeof (int[2]){1, 2} + counted() != 3 + 8 + 5 || calls != 1)
    {
        return 17;
    }
    struct Holder holder;
    holder.first[0] = 0;
    if (sizeof holder.first != 16 || sizeof holder.second != 12)
    {
        return 18;
    }
    if (ENUM_UNPARENTHESIZED != 20 || ENUM_PARENTHESIZED != 20 || ENUM_AGGREGATE != 8 || ENUM_NEXT != 9)
    {
        return 19;
    }
    // The object really is that long, and writing its last byte leaves the
    // neighbours alone: a bound folded to the int guess would give it 4.
    written_through[11] = 33;
    if (written_through[11] != 33 || before_guard != 11 || after_guard != 22)
    {
        return 20;
    }
    for (int index = 0; index < 12; index += 1)
    {
        written_through[index] = (char)index;
    }
    if (before_guard != 11 || after_guard != 22 || written_through[0] != 0 || written_through[11] != 11)
    {
        return 21;
    }
    return 0;
}
