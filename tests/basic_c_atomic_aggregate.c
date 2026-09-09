/* `_Atomic` over a struct or union: the object is padded up to the next power
   of two (#731) so that one integer access of that width covers it, and the
   load and the store are that access (#762).  Every expected byte here is
   baked in rather than computed, so the fixture pins the *representation* and
   not merely self-consistency: it was read out of Clang, which zeroes an
   atomic aggregate's padding through the temporary it copies the value into.

   `signed char` is spelled out because plain `char` is unsigned on AArch64,
   which would make the baked answers disagree with themselves across targets.

   Widths one through eight run everywhere. Sixteen uses CMPXCHG16B on x86-64
   with cx16 and exclusive-pair loops on AArch64. The wide fixture is enabled
   for both targets and is exercised under every allocator. */

typedef struct
{
    signed char a, b, c;
} three;
typedef _Atomic three atomic_three;

typedef struct
{
    signed char a;
} one;

typedef struct
{
    short a;
} two;

typedef struct
{
    int a;
    short b;
} six;

typedef struct
{
    long long a;
} eight;
typedef _Atomic six atomic_six;

typedef union
{
    int word;
    signed char bytes[3];
} narrow_union;

static atomic_three global_three;
static _Atomic one global_one;
static _Atomic two global_two;
static _Atomic six global_six;
static _Atomic eight global_eight;
static _Atomic narrow_union global_union;
static volatile _Atomic six global_volatile_six;

typedef struct
{
    atomic_three member;
    int tail;
} holder;

static holder global_holder;

/* The fourth spelling of the type, `_Atomic` written in front of the tag
   keyword (#761).  It needs a tag -- every shape above is a typedef of an
   anonymous aggregate, which no leading spelling can name -- and it belongs
   with the accesses rather than only with the sizes, because the access is
   what the dropped qualifier cost: a type that never became atomic is stored
   with an ordinary three-byte aggregate copy, which leaves the fourth byte
   holding whatever was there instead of the zero the atomic store writes. */
struct tagged_three
{
    signed char a, b, c;
};

static _Atomic struct tagged_three global_leading;

static int bytes_are(const void* object, const unsigned char* expected, unsigned count)
{
    const unsigned char* actual = (const unsigned char*)object;
    int equal = 1;
    for (unsigned index = 0; index < count; index += 1)
    {
        if (actual[index] != expected[index])
        {
            equal = 0;
        }
    }
    return equal;
}

static int global_round_trip(void)
{
    unsigned char three_bytes[4] = {1, 2, 3, 0};
    unsigned char one_bytes[1] = {0x7f};
    unsigned char two_bytes[2] = {0x34, 0x12};
    unsigned char six_bytes[8] = {0x44, 0x33, 0x22, 0x11, 0x66, 0x55, 0, 0};
    unsigned char eight_bytes[8] = {0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11};

    three value_three = {1, 2, 3};
    global_three = value_three;
    three read_three = global_three;

    one value_one = {0x7f};
    global_one = value_one;
    one read_one = global_one;

    two value_two = {0x1234};
    global_two = value_two;
    two read_two = global_two;

    six value_six = {0x11223344, 0x5566};
    global_six = value_six;
    six read_six = global_six;

    eight value_eight = {0x1122334455667788LL};
    global_eight = value_eight;
    eight read_eight = global_eight;

    return bytes_are(&global_three, three_bytes, sizeof(three_bytes)) && read_three.a == 1 && read_three.b == 2 && read_three.c == 3 &&
           bytes_are(&global_one, one_bytes, sizeof(one_bytes)) && read_one.a == 0x7f &&
           bytes_are(&global_two, two_bytes, sizeof(two_bytes)) && read_two.a == 0x1234 &&
           bytes_are(&global_six, six_bytes, sizeof(six_bytes)) && read_six.a == 0x11223344 && read_six.b == 0x5566 &&
           bytes_are(&global_eight, eight_bytes, sizeof(eight_bytes)) && read_eight.a == 0x1122334455667788LL;
}

static int local_round_trip(void)
{
    unsigned char expected[4] = {4, 5, 6, 0};
    three value = {4, 5, 6};
    atomic_three local = value;
    three read = local;
    local = read;
    return bytes_are(&local, expected, sizeof(expected)) && read.a == 4 && read.b == 5 && read.c == 6;
}

static int narrow_compare_exchange_round_trip(void)
{
    three initial = {1, 2, 3};
    three desired = {4, 5, 6};
    three replacement = {7, 8, 9};
    three expected = initial;
    __c11_atomic_store(&global_three, initial, __ATOMIC_SEQ_CST);
    int changed = __c11_atomic_compare_exchange_strong(
        &global_three, &expected, desired, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    three stale = initial;
    int stale_changed = __c11_atomic_compare_exchange_strong(
        &global_three, &stale, replacement, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    three previous = __c11_atomic_exchange(&global_three, replacement, __ATOMIC_ACQ_REL);
    unsigned char bytes[4] = {7, 8, 9, 0};
    return changed && !stale_changed && stale.a == 4 && stale.b == 5 && stale.c == 6 &&
           previous.a == 4 && previous.b == 5 && previous.c == 6 && bytes_are(&global_three, bytes, sizeof(bytes));
}

static int qualified_and_union_exchange_round_trip(void)
{
    six initial = {0x11223344, 0x5566};
    six desired = {0x22334455, 0x6677};
    six expected = initial;
    __c11_atomic_store(&global_volatile_six, initial, __ATOMIC_SEQ_CST);
    int changed = __c11_atomic_compare_exchange_strong(
        &global_volatile_six, &expected, desired, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    six previous = __c11_atomic_exchange(&global_volatile_six, initial, __ATOMIC_SEQ_CST);
    six loaded = __c11_atomic_load(&global_volatile_six, __ATOMIC_ACQUIRE);

    narrow_union union_initial;
    narrow_union union_desired;
    union_initial.word = 0x11223344;
    union_desired.word = 0x556677;
    narrow_union union_expected = union_initial;
    __c11_atomic_store(&global_union, union_initial, __ATOMIC_SEQ_CST);
    int union_changed = __c11_atomic_compare_exchange_strong(
        &global_union, &union_expected, union_desired, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    narrow_union union_previous = __c11_atomic_exchange(&global_union, union_initial, __ATOMIC_ACQ_REL);
    narrow_union union_loaded = __c11_atomic_load(&global_union, __ATOMIC_RELAXED);
    return changed && previous.a == desired.a && previous.b == desired.b && loaded.a == initial.a && loaded.b == initial.b &&
           union_changed && union_previous.word == union_desired.word && union_loaded.word == union_initial.word;
}

static int pointer_round_trip(void)
{
    unsigned char expected[4] = {7, 8, 9, 0};
    atomic_three storage;
    atomic_three* place = &storage;
    three value = {7, 8, 9};
    *place = value;
    three read = *place;
    return bytes_are(place, expected, sizeof(expected)) && read.a == 7 && read.b == 8 && read.c == 9;
}

static int member_round_trip(void)
{
    unsigned char expected[4] = {10, 11, 12, 0};
    three value = {10, 11, 12};
    global_holder.member = value;
    global_holder.tail = 0x5a5a5a;
    three read = global_holder.member;
    return bytes_are(&global_holder.member, expected, sizeof(expected)) && read.a == 10 && read.b == 11 && read.c == 12 &&
           global_holder.tail == 0x5a5a5a;
}

static int union_round_trip(void)
{
    unsigned char expected[4] = {0x11, 0x22, 0x33, 0};
    narrow_union value;
    value.word = 0;
    value.bytes[0] = 0x11;
    value.bytes[1] = 0x22;
    value.bytes[2] = 0x33;
    global_union = value;
    narrow_union read = global_union;
    return bytes_are(&global_union, expected, sizeof(expected)) && read.bytes[0] == 0x11 && read.bytes[1] == 0x22 && read.bytes[2] == 0x33;
}

static int leading_round_trip(void)
{
    unsigned char expected[4] = {13, 14, 15, 0};
    struct tagged_three value = {13, 14, 15};
    /* The padding byte is dirtied first, so a store that writes only the three
       value bytes fails here rather than passing on a zero that was already
       there. */
    ((unsigned char*)&global_leading)[3] = 0x5a;
    global_leading = value;
    struct tagged_three read = global_leading;
    return bytes_are(&global_leading, expected, sizeof(expected)) && read.a == 13 && read.b == 14 && read.c == 15 && sizeof(global_leading) == 4 &&
           _Alignof(_Atomic struct tagged_three) == 4 && sizeof(struct tagged_three) == 3;
}

static int sizes_are_promoted(void)
{
    return sizeof(atomic_three) == 4 && _Alignof(atomic_three) == 4 && sizeof(global_one) == 1 && sizeof(global_two) == 2 &&
           sizeof(global_six) == 8 && _Alignof(atomic_six) == 8 && sizeof(global_eight) == 8 && sizeof(global_union) == 4 && sizeof(three) == 3;
}

/* Passing and returning one (#786).  The type an argument is classified from
   is the promoted one, so what travels is four bytes where the record is three
   and the padding byte travels with it: the value reaches the call through an
   object of the atomic type, which is where its zero comes from and is where
   Clang's comes from too.  Nothing reads a member out of the atomic object
   itself -- an atomic aggregate is copied whole and read through the copy,
   which is also what Clang's -Watomic-access asks for.

   All four spellings that build the type appear here, in both positions,
   because what failed was the position rather than any one spelling: the
   typedef, `_Atomic` in front of the tag keyword, after it, and the
   `_Atomic ( T )` operator. */
static int takes_alias(atomic_three parameter)
{
    unsigned char expected[4] = {16, 17, 18, 0};
    three value = parameter;
    return bytes_are(&parameter, expected, sizeof(expected)) && value.a == 16 && value.b == 17 && value.c == 18 && sizeof(parameter) == 4;
}

static atomic_three gives_alias(three value)
{
    atomic_three result = value;
    return result;
}

static int takes_leading(_Atomic struct tagged_three parameter)
{
    struct tagged_three value = parameter;
    return ((const unsigned char*)&parameter)[3] == 0 && value.a == 19 && value.b == 20 && value.c == 21;
}

static _Atomic struct tagged_three gives_leading(struct tagged_three value)
{
    _Atomic struct tagged_three result = value;
    return result;
}

static int takes_trailing(struct tagged_three _Atomic parameter)
{
    struct tagged_three value = parameter;
    return ((const unsigned char*)&parameter)[3] == 0 && value.a == 19 && value.b == 20 && value.c == 21;
}

static struct tagged_three _Atomic gives_trailing(struct tagged_three value)
{
    struct tagged_three _Atomic result = value;
    return result;
}

static int takes_operator(_Atomic(struct tagged_three) parameter)
{
    struct tagged_three value = parameter;
    return ((const unsigned char*)&parameter)[3] == 0 && value.a == 19 && value.b == 20 && value.c == 21;
}

static _Atomic(struct tagged_three) gives_operator(struct tagged_three value)
{
    _Atomic(struct tagged_three) result = value;
    return result;
}

static int takes_union(_Atomic narrow_union parameter)
{
    unsigned char expected[4] = {0x11, 0x22, 0x33, 0};
    narrow_union value = parameter;
    return bytes_are(&parameter, expected, sizeof(expected)) && value.bytes[0] == 0x11 && value.bytes[1] == 0x22 && value.bytes[2] == 0x33;
}

static _Atomic narrow_union gives_union(narrow_union value)
{
    _Atomic narrow_union result = value;
    return result;
}

/* The promotion adds nothing to this one -- `six` is already eight bytes -- so
   the round trip runs over a pair of types that are the same size and still
   not the same type. */
static int takes_six(atomic_six parameter)
{
    six value = parameter;
    return value.a == 0x11223344 && value.b == 0x5566 && sizeof(parameter) == 8;
}

static atomic_six gives_six(six value)
{
    atomic_six result = value;
    return result;
}

static int argument_round_trip(void)
{
    unsigned char returned_bytes[4] = {16, 17, 18, 0};
    three value_three = {16, 17, 18};
    struct tagged_three value_tagged = {19, 20, 21};
    narrow_union value_union;
    six value_six = {0x11223344, 0x5566};

    /* The destination's padding byte is dirtied first, so a returned value
       that left it alone fails here rather than passing on a zero that was
       already in the object. */
    atomic_three returned;
    ((unsigned char*)&returned)[3] = 0x5a;
    returned = gives_alias(value_three);

    value_union.word = 0;
    value_union.bytes[0] = 0x11;
    value_union.bytes[1] = 0x22;
    value_union.bytes[2] = 0x33;

    return takes_alias(gives_alias(value_three)) && bytes_are(&returned, returned_bytes, sizeof(returned_bytes)) && takes_alias(returned) &&
           takes_leading(gives_leading(value_tagged)) && takes_trailing(gives_trailing(value_tagged)) &&
           takes_operator(gives_operator(value_tagged)) && takes_union(gives_union(value_union)) && takes_six(gives_six(value_six));
}

/* x86-64 lowers these through the CMPXCHG16B pair, AArch64 through
   LDXP/STXP exclusive-pair loops; both make the sixteen-byte shapes and
   the promoted padding of `nine` observable. */
#if defined(__x86_64__) || defined(__aarch64__)
typedef struct
{
    long long a, b;
} sixteen;

typedef struct
{
    int a, b, c, d;
} four_ints;

typedef struct
{
    void* pointer;
    unsigned long long counter;
} pointer_counter;

typedef struct
{
    long long a;
    signed char b;
} nine;

typedef _Atomic sixteen atomic_sixteen;
typedef _Atomic four_ints atomic_four_ints;
typedef _Atomic pointer_counter atomic_pointer_counter;
typedef _Atomic nine atomic_nine;

static atomic_sixteen global_sixteen;
static atomic_four_ints global_four_ints;
static atomic_pointer_counter global_pointer_counter;
static atomic_nine global_nine;
static int pointer_anchor_a;
static int pointer_anchor_b;

static int wide_round_trip(void)
{
    unsigned char nine_bytes[16] = {0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x5a, 0, 0, 0, 0, 0, 0, 0};
    sixteen value_sixteen = {0x1122334455667788LL, 0x0102030405060708LL};
    global_sixteen = value_sixteen;
    sixteen read_sixteen = global_sixteen;

    nine value_nine = {0x1122334455667788LL, 0x5a};
    global_nine = value_nine;
    nine read_nine = global_nine;

    return read_sixteen.a == 0x1122334455667788LL && read_sixteen.b == 0x0102030405060708LL && sizeof(global_sixteen) == 16 &&
           bytes_are(&global_nine, nine_bytes, sizeof(nine_bytes)) && read_nine.a == 0x1122334455667788LL && read_nine.b == 0x5a &&
           sizeof(global_nine) == 16 && _Alignof(atomic_nine) == 16;
}

static int wide_compare_exchange_round_trip(void)
{
    sixteen initial = {1, 2};
    sixteen desired = {3, 4};
    sixteen replacement = {5, 6};
    sixteen expected = initial;
    __c11_atomic_store(&global_sixteen, initial, __ATOMIC_SEQ_CST);
    int strong_succeeded = __c11_atomic_compare_exchange_strong(
        &global_sixteen, &expected, desired, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    sixteen stale = initial;
    int stale_succeeded = __c11_atomic_compare_exchange_strong(
        &global_sixteen, &stale, replacement, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    sixteen loaded = __c11_atomic_load(&global_sixteen, __ATOMIC_ACQUIRE);

    four_ints four_initial = {7, 8, 9, 10};
    four_ints four_desired = {11, 12, 13, 14};
    four_ints four_expected = four_initial;
    __c11_atomic_store(&global_four_ints, four_initial, __ATOMIC_RELEASE);
    int weak_succeeded = 0;
    for (unsigned attempt = 0; attempt < 64 && !weak_succeeded; attempt += 1)
    {
        four_expected = four_initial;
        weak_succeeded = __c11_atomic_compare_exchange_weak(
            &global_four_ints, &four_expected, four_desired, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    }
    four_ints four_loaded = __c11_atomic_load(&global_four_ints, __ATOMIC_RELAXED);

    pointer_counter pointer_initial = {&pointer_anchor_a, 15};
    pointer_counter pointer_desired = {&pointer_anchor_b, 16};
    __c11_atomic_store(&global_pointer_counter, pointer_initial, __ATOMIC_SEQ_CST);
    pointer_counter pointer_previous = __c11_atomic_exchange(
        &global_pointer_counter, pointer_desired, __ATOMIC_ACQ_REL);
    pointer_counter pointer_loaded = __c11_atomic_load(&global_pointer_counter, __ATOMIC_ACQUIRE);

    return strong_succeeded && !stale_succeeded && stale.a == desired.a && stale.b == desired.b &&
           loaded.a == desired.a && loaded.b == desired.b && weak_succeeded &&
           four_loaded.a == four_desired.a && four_loaded.b == four_desired.b &&
           four_loaded.c == four_desired.c && four_loaded.d == four_desired.d &&
           pointer_previous.pointer == pointer_initial.pointer && pointer_previous.counter == pointer_initial.counter &&
           pointer_loaded.pointer == pointer_desired.pointer && pointer_loaded.counter == pointer_desired.counter &&
           sizeof(atomic_sixteen) == 16 && _Alignof(atomic_sixteen) == 16 &&
           sizeof(atomic_four_ints) == 16 && _Alignof(atomic_four_ints) == 16 &&
           sizeof(atomic_pointer_counter) == 16 && _Alignof(atomic_pointer_counter) == 16;
}

/* Sixteen bytes ride two eightbytes rather than one, and the seven bytes the
   promotion added to `nine` are part of what is passed. */
static int takes_nine(atomic_nine parameter)
{
    unsigned char expected[16] = {0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x5a, 0, 0, 0, 0, 0, 0, 0};
    nine value = parameter;
    return bytes_are(&parameter, expected, sizeof(expected)) && value.a == 0x1122334455667788LL && value.b == 0x5a && sizeof(parameter) == 16;
}

static atomic_nine gives_nine(nine value)
{
    atomic_nine result = value;
    return result;
}

static int wide_argument_round_trip(void)
{
    nine value_nine = {0x1122334455667788LL, 0x5a};
    return takes_nine(gives_nine(value_nine));
}
#else
static int wide_round_trip(void)
{
    return 1;
}

static int wide_argument_round_trip(void)
{
    return 1;
}

static int wide_compare_exchange_round_trip(void)
{
    return 1;
}
#endif

int main(void)
{
    return !(sizes_are_promoted() && global_round_trip() && local_round_trip() && pointer_round_trip() && member_round_trip() && union_round_trip() &&
             leading_round_trip() && argument_round_trip() && wide_round_trip() && wide_argument_round_trip() &&
                       wide_compare_exchange_round_trip() && narrow_compare_exchange_round_trip() && qualified_and_union_exchange_round_trip());
}
