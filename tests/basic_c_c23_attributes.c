// Every position the C23 grammar lets an attribute specifier stand in, in one
// translation unit.  Buster acts on [[noreturn]] alone -- basic_c_c23_noreturn.c
// is where that is proven -- so what this fixture asserts is that the rest are
// parsed and discarded without disturbing the declaration, the type, the
// layout or the control flow they decorate.  It is a runtime fixture because
// the failure mode being guarded is silent: before this was implemented an
// attributed declaration did not register at all, and the error surfaced later
// as an unrelated "undeclared identifier".
//
// The frontend has no parse tree -- every consumer re-walks token ranges -- so
// an attribute sequence has to be invisible to each scan that classifies a
// token by its neighbours.  The cases below are grouped by which scan they
// exercise, because that is what a regression here would name.

// Declaration specifiers, at file scope, in each of the three spellings a
// sequence can take: one list, two adjacent lists, and two names in one list.
[[maybe_unused]] static int one_list = 1;
[[maybe_unused]] [[deprecated]] static int two_lists = 2;
[[maybe_unused, deprecated]] static int two_names = 4;

// The scoped form.  The C lexer has no '::' token, so this arrives as two
// colons inside the list and is skipped as part of the balanced sequence
// rather than being recognized as a scope operator.
[[gnu::unused]] static int scoped = 8;

// The argument form is a balanced token sequence, so it may carry parentheses,
// commas and brackets of its own.  The bracket is the interesting one: the
// scan closes on the ']]' that is not nested, not on the first one it meets.
[[deprecated("superseded, see one_list")]] static int with_arguments = 16;
[[maybe_unused]] static int with_bracketed_argument[2] = {32, 64};

// After the declarator rather than before the specifiers.
static int trailing [[maybe_unused]] = 128;

// Aggregates: on the tag, on a member, and on an enumerator.  The enumerator
// case is its own scan -- the enum body is split on top-level commas and each
// enumerator read as `name = value` -- so an attribute between the name and
// the '=' has to be stepped over there specifically.
struct [[deprecated]] Tagged
{
    int first [[maybe_unused]];
    int second;
};

union [[maybe_unused]] Choice
{
    int as_integer;
    unsigned as_unsigned;
};

enum [[deprecated]] Counted
{
    counted_first [[deprecated]] = 7,
    counted_second,
    counted_third [[maybe_unused]] = 20,
    counted_fourth,
};

// Parameters, in both the position before the specifiers and the position
// after the declarator.
static int parameters([[maybe_unused]] int before, int after [[maybe_unused]])
{
    return before + after;
}

// A typedef declarator.
typedef int attributed_integer [[maybe_unused]];

// An attribute declaration: a sequence and a semicolon, declaring nothing.
[[maybe_unused]];

static int statements(int value)
{
    // Block-scope declaration.  This is the shape that reported
    // "undeclared identifier 'maybe_unused'" before the statement walkers
    // learned to step over the sequence.
    [[maybe_unused]] int local = value;

    // A label may carry one.  This is the only question in the frontend that
    // is asked from the far side of the sequence: a label is proven by what
    // stands before it, and the ']' the sequence ends with is not a statement
    // boundary, so the proof has to peel the sequence off backwards.
    [[maybe_unused]] plain_label: local += 1;

    // The same peel, with a control statement's substatement position behind
    // it: the test that finds the ')' has to run on the token the sequence
    // starts at, not on the label.
    if (value) [[maybe_unused]] guarded_label: local += 2;

    // A jump statement and a primary block may each carry one.
    if (value > 1000) [[unlikely]] { local += 4; }

    // The for-initializer is a declaration in its own right.
    for ([[maybe_unused]] int index = 0; index < 2; index += 1)
    {
        local += 8;
    }

    switch (value)
    {
        case 0:
            local += 16;
            [[fallthrough]];
        case 1:
            local += 32;
            break;
        default:
            local += 64;
            break;
    }
    return local;
}

int main(void)
{
    struct Tagged tagged = {1, 2};
    union Choice choice = {3};
    attributed_integer typed = 4;

    if (one_list + two_lists + two_names + scoped + with_arguments != 31)
    {
        return 1;
    }
    if (with_bracketed_argument[0] + with_bracketed_argument[1] + trailing != 224)
    {
        return 2;
    }
    if (tagged.first + tagged.second + choice.as_integer + typed != 10)
    {
        return 3;
    }
    // The enumerators must have taken the values their initializers give and
    // the implicit successors of those, exactly as they would without the
    // attributes standing between each name and its '='.
    if (counted_first != 7 || counted_second != 8 || counted_third != 20 || counted_fourth != 21)
    {
        return 4;
    }
    if (parameters(5, 6) != 11)
    {
        return 5;
    }
    // local starts at the argument.  0: +1 plain, the guarded label is not
    // taken, +8 twice from the loop, then +16 and +32 through the
    // fallthrough.
    if (statements(0) != 65)
    {
        return 6;
    }
    // 1: +1, +2 guarded, +8 twice, +32 from case 1 alone.
    if (statements(1) != 52)
    {
        return 7;
    }
    // 2: +1, +2, +8 twice, +64 from default.
    if (statements(2) != 85)
    {
        return 8;
    }
    // 1001 additionally takes the attributed block, which is what brings the
    // running total to 1024 before the default arm adds its 64.
    if (statements(1001) != 1088)
    {
        return 9;
    }
    return 0;
}
