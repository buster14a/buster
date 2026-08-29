// Assigning an aggregate across a `volatile` qualifier (C11 6.5.16.1p2, and
// the lvalue conversion of 6.3.2.1p2).  `volatile T` and `T` are two IR types
// -- the qualified one is a copy the frontend builds so a place, a pointee or
// a member can carry the qualifier -- but they are one representation, and a
// value crossing between them is not a conversion.  The value conversion the
// frontend runs before a store only spanned the scalar kinds, so a struct
// crossing the qualifier had nothing to reach for and the assignment was
// refused outright.
//
// libc-test's `functional/setjmp` is written that way:
//
//     volatile sigset_t oldset;
//     sigset_t set2;
//     ...
//     oldset = set2;
//
// and `sigset_t` is a struct wrapping an array of longs, so every one of its
// assignments failed to compile.
//
// What is pinned here is both directions of the qualifier, every place an
// aggregate value comes from -- a load, an initializer, a compound literal, a
// call return -- and the members, elements and pointees that carry the
// qualifier on their own.  The volatile scalars beside them are the shapes
// that already worked and have to keep working: their stores now cross the
// same qualifier rather than converting through it.

typedef struct
{
    unsigned long bits[8];
} Set;

typedef struct
{
    int first;
    long second;
    char text[3];
} Record;

typedef union
{
    int integer;
    unsigned char bytes[4];
} Alternative;

struct Mixed
{
    volatile Record record;
    volatile int counter;
    Record plain;
};

static volatile Set signal_mask;
static volatile Record file_scope_record;
static Record source_record = {1, 2, {3, 4, 5}};

static Set make_set(unsigned long seed)
{
    Set result;
    for (unsigned long index = 0; index < sizeof result.bits / sizeof result.bits[0]; index += 1)
    {
        result.bits[index] = seed + index;
    }

    return result;
}

static int set_equals(Set left, Set right)
{
    int result = 1;
    for (unsigned long index = 0; index < sizeof left.bits / sizeof left.bits[0]; index += 1)
    {
        if (left.bits[index] != right.bits[index])
        {
            result = 0;
        }
    }

    return result;
}

// Out of line and taking the unqualified type, so a volatile argument is a
// real copy made at the call and not something folded at the call site.
static Record scale(Record value, int factor)
{
    Record result = {value.first * factor, value.second * factor, {value.text[0], value.text[1], value.text[2]}};

    return result;
}

int main(void)
{
    // The reported shape: a plain aggregate into a volatile object.
    volatile Set destination;
    Set source = make_set(100);
    destination = source;
    Set read_back;
    // ... and the other direction, which is the same refusal mirrored.
    read_back = destination;
    if (!set_equals(read_back, source))
    {
        return 1;
    }

    // Both operands qualified, and a file-scope destination rather than a
    // local one: the place comes from a global instead of a frame slot.
    signal_mask = source;
    volatile Set qualified_both;
    qualified_both = signal_mask;
    Set from_qualified_both;
    from_qualified_both = qualified_both;
    if (!set_equals(from_qualified_both, source))
    {
        return 2;
    }

    // An initializer builds the aggregate value at the declared type, so the
    // value is the qualified one here and the plain one on the next line.
    volatile Record braced = {6, 7, {8, 9, 10}};
    Record from_braced = braced;
    if (from_braced.first != 6 || from_braced.second != 7 || from_braced.text[2] != 10)
    {
        return 3;
    }

    volatile Record zeroed = {0};
    Record from_zeroed = zeroed;
    if (from_zeroed.first != 0 || from_zeroed.second != 0 || from_zeroed.text[0] != 0)
    {
        return 4;
    }

    // A compound literal, spelled with and without the qualifier on the type
    // name: the value it produces carries whichever type was written.
    volatile Record from_plain_literal = (Record){11, 12, {13, 14, 15}};
    Record from_qualified_literal = (volatile Record){16, 17, {18, 19, 20}};
    volatile Record qualified_literal_into_volatile = (volatile Record){21, 22, {23, 24, 25}};
    if (from_plain_literal.first != 11 || from_qualified_literal.first != 16 || qualified_literal_into_volatile.first != 21)
    {
        return 5;
    }

    // A call boundary in both directions: a volatile argument converted to the
    // parameter's plain type, and a plain return value stored into a volatile
    // object.
    volatile Record argument = {2, 3, {4, 5, 6}};
    volatile Record returned = scale(argument, 3);
    Record plain_returned = scale(returned, 2);
    if (returned.first != 6 || returned.second != 9 || plain_returned.first != 12 || plain_returned.second != 18)
    {
        return 6;
    }

    // A member of a plain struct carrying the qualifier itself, and a plain
    // member beside it.
    struct Mixed mixed;
    mixed.record = source_record;
    mixed.counter = 26;
    mixed.plain = mixed.record;
    Record from_member = mixed.record;
    if (from_member.first != 1 || mixed.plain.second != 2 || mixed.counter != 26)
    {
        return 7;
    }

    // An element of a volatile array, which carries the qualifier through the
    // element type rather than through the object.
    volatile Record records[2];
    records[0] = source_record;
    records[1] = scale(records[0], 4);
    Record first_element = records[0];
    Record second_element = records[1];
    if (first_element.first != 1 || second_element.first != 4 || second_element.second != 8)
    {
        return 8;
    }

    // Through a pointer, where the qualifier is on the pointee: the place the
    // dereference hands back is the qualified one at both ends.
    volatile Record* pointer = &file_scope_record;
    *pointer = source_record;
    Record through_pointer = *pointer;
    volatile Record volatile_through_pointer = *pointer;
    if (through_pointer.first != 1 || volatile_through_pointer.second != 2 || file_scope_record.text[1] != 4)
    {
        return 9;
    }

    // A union, whose assignment copies the whole object rather than a member.
    volatile Alternative alternative;
    Alternative plain_alternative;
    plain_alternative.integer = 0;
    plain_alternative.bytes[1] = 27;
    alternative = plain_alternative;
    Alternative copied_alternative = alternative;
    if (copied_alternative.bytes[1] != 27)
    {
        return 10;
    }

    // A read-only qualified source, which is the const-volatile pair a
    // memory-mapped input is declared with.
    const volatile Record constant = {28, 29, {30, 31, 32}};
    Record from_constant = constant;
    volatile Record volatile_from_constant = constant;
    if (from_constant.first != 28 || volatile_from_constant.second != 29)
    {
        return 11;
    }

    // The scalars that always worked: their values now cross the qualifier
    // instead of converting through it, and the arithmetic on them is
    // unchanged.
    volatile int scalar = 33;
    scalar += 4;
    scalar *= 2;
    scalar++;
    --scalar;
    int scalar_read = scalar;
    volatile double real = 1.5;
    real += 0.25;
    volatile char narrow = 34;
    narrow = (char)(narrow + 1);
    volatile int* volatile scalar_pointer = &scalar;
    *scalar_pointer += 1;
    if (scalar_read != 74 || scalar != 75 || real != 1.75 || narrow != 35)
    {
        return 12;
    }

    // A converting assignment into a volatile object still converts: the
    // qualifier is not what decides whether a conversion is needed.
    volatile long widened = scalar;
    volatile int narrowed = (int)real;
    if (widened != 75 || narrowed != 1)
    {
        return 13;
    }

    return 0;
}
