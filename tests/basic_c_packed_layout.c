// `__attribute__((packed))` and `__attribute__((aligned(N)))` decide object
// representation, so ignoring them is an ABI divergence rather than a missing
// optimization.  Every number below is what the platform C compiler computes
// for the same declaration; the cross-linked pair in
// basic_c_packed_layout_caller.c / _callee.c is what pins them to the
// platform's rather than to Buster's own opinion.
//
// The shapes are the four ways the two attributes reach a layout: on the
// aggregate before the tag, on the aggregate after the closing brace, on one
// member alone, and on an object declarator.  `#pragma pack` is the same
// question asked by a directive and shares the ceiling this implements.
// A member's own list has three positions of its own -- before the shared
// specifiers, after a plain declarator, and after a parenthesized one -- and
// a declarator that is not the first of its list has a fourth.  A bit-field
// declarator has a fifth, after the width, which is also the only one it has:
// the reference compilers reject a list written before the colon.  Which
// declarator of a list the attribute sits on is a separate question from
// where in that declarator it is written, and it decides which members move.
// An object declarator reaches the last two positions as well, at file scope
// and at block scope, each parsed by code of its own, and which declarator of
// a list carries the attribute decides which objects move there too.  A
// typedef declarator reaches them too, and there the request belongs to the
// type the name declares rather than to any object: it replaces the natural
// alignment instead of raising it, which makes it the one place `aligned`
// lowers one without `packed`.

// Packed before the tag, which is how QuickJS spells its unaligned readers.
struct __attribute__((packed)) leading
{
    char byte;
    int value;
};

// Packed after the body, which is how musl's <sys/epoll.h> spells it.
struct trailing
{
    char byte;
    int value;
} __attribute__((packed));

// One packed member.  Its own alignment drops to a byte, which also stops it
// from raising the aggregate's.
struct member_packed
{
    char byte;
    int value __attribute__((packed));
};

// Packing lowers the aggregate's alignment and `aligned` raises it; written
// together the explicit request wins and the size rounds up to it.
struct __attribute__((packed, aligned(8))) packed_then_aligned
{
    char byte;
    int value;
};

// A union packs its members without changing its size, which is still the
// widest member.
union __attribute__((packed)) packed_union
{
    char byte;
    int value;
};

// A packed union sizes to the bits its widest member occupies rather than to
// that member's declared type, so `int value : 5` makes this one byte -- which
// is what Clang and GCC do and what a size taken from the declared type got
// wrong (#706).  The unpacked spelling below is the control: there the
// rounding to the alignment its declared type asks for gives the four bytes
// back, so one uniform rule answers both.
union __attribute__((packed)) packed_bit_union
{
    char lead;
    int value : 5;
};

// The same shape a byte wider, so the size is not only the one-byte case: a
// twelve-bit field occupies two bytes and is read through the two-byte unit
// the aggregate has room for.
union __attribute__((packed)) packed_bit_union_wide
{
    char lead;
    unsigned int value : 12;
};

// A member wider than the union's size, which pins that the unit a union
// bit-field is read through is chosen from the aggregate the same way a
// struct's is: four bytes hold the thirty-two-bit field and the byte member
// both.
union __attribute__((packed)) packed_bit_union_mixed
{
    char lead;
    int narrow : 5;
    long long wide : 32;
};

union unpacked_bit_union
{
    char lead;
    int value : 5;
};

// The union's size is where the member after it sits, so a union sized from
// the declared type puts `tail` three bytes further on -- and a `value` read
// through a four-byte unit would take `tail` with it on every store.
struct __attribute__((packed)) packed_bit_union_record
{
    union packed_bit_union bits;
    char tail;
};

static union packed_bit_union packed_bit_union_initialized = {.value = -6};

// Packing is not transitive: the nested aggregate keeps its own natural
// alignment and only its placement inside the packed parent moves.
struct __attribute__((packed)) nested
{
    char byte;
    struct
    {
        char inner_byte;
        int inner_value;
    } inner;
};

// A packed bit-field takes the next bit rather than the next storage unit of
// its declared type; a zero-width one keeps aligning to that type even here.
struct __attribute__((packed)) packed_bits
{
    unsigned char low : 3;
    unsigned char high : 5;
    char tail;
};

// The unit a packed bit-field is read through is chosen from where its bits
// land, not from where an unpacked field of the same type would start: `value`
// begins at bit 8 and is read through the int at offset zero.
struct __attribute__((packed)) offset_bits
{
    char lead;
    int value : 24;
};

struct __attribute__((packed)) zero_width_bits
{
    int before : 3;
    int : 0;
    int after : 3;
};

// Three bytes hold no `int` at any offset, so the unit `value` is read through
// narrows to the byte it lands in rather than the declaration being refused --
// which is what Clang and GCC do with it.  The other two spellings ask for the
// same layout through different parses: the shared specifier, and the
// declarator's own list after the width, which is the only place a bit-field
// declarator can carry one.
struct __attribute__((packed)) narrow_unit
{
    char lead;
    int value : 5;
    char tail;
};

struct narrow_unit_member
{
    char lead;
    __attribute__((packed)) int value : 5;
    char tail;
};

struct narrow_unit_declarator
{
    char lead;
    int value : 5 __attribute__((packed));
    char tail;
};

// A wider field in a wider unit, so the narrowing is not only the byte case:
// twelve bits at bit eight of a four-byte object have no `int` unit either and
// take the two bytes at offset one.
struct __attribute__((packed)) narrow_unit_wide
{
    char lead;
    unsigned int value : 12;
    char tail;
};

static struct narrow_unit narrow_unit_initialized = {1, -6, 2};
static struct narrow_unit narrow_unit_designated = {.value = 5, .tail = 6};

// A brace initializer for an *automatic* aggregate writes each bit-field
// through the same unit, and packing is what lets that unit reach bytes other
// members own: `slid_unit`'s four-byte unit at offset zero covers `lead` and
// `tail` as well as the two fields it is the unit of, and
// `overlapping_units` has two units sharing a byte -- five bits have no `int`
// unit in a two-byte object and take the byte, while the nine that follow are
// read through the `short` that covers both.  Storing such a unit whole, out
// of an accumulator seeded with zero rather than with the bytes already in the
// slot, zeroed whichever of those the initializer wrote first (#705).  The
// static objects above answer a different question: they take the constant
// fold, so only an automatic object reaches the code generator's aggregate.
struct __attribute__((packed)) slid_unit
{
    char lead;
    int first : 5;
    int second : 7;
    char tail;
};

// The plain member on the far side of the bit-fields, which a brace list
// writes after the unit and a designated list can write before it: both orders
// have to keep it.
struct __attribute__((packed)) trailing_member
{
    int first : 5;
    int second : 7;
    int third : 12;
    char tail;
};

struct __attribute__((packed)) overlapping_units
{
    int narrow : 5;
    short wide : 9;
};

struct __attribute__((packed)) overlapping_units_reversed
{
    short wide : 9;
    int narrow : 5;
};

// A bit-field whose bits fit no power-of-two unit that lies inside the
// aggregate has no single access at all: five bytes hold no five-byte integer,
// so forty bits are read and written as a four-byte piece plus a one-byte one
// -- which is what Clang writes as `i40` and lowers to the same pair.  The
// widths that land here are exactly the ones whose byte count is not a power
// of two, so the set below walks one of each: three, five, six, seven and nine
// bytes.  The neighbours on either side are what make a value that survives a
// read-modify-write a claim about the pieces rather than about the field.
struct __attribute__((packed)) split_unit
{
    long long value : 40;
};

union __attribute__((packed)) split_union
{
    long long value : 40;
};

struct __attribute__((packed)) split_neighbours
{
    char lead;
    long long value : 40;
    char tail;
};

struct __attribute__((packed)) split_offset
{
    long long lead : 1;
    long long value : 40;
    char tail;
};

// A seven-byte span, which is the one that takes three pieces: four, two and
// one.
struct __attribute__((packed)) split_three_pieces
{
    long long lead : 1;
    long long value : 55;
    char tail;
};

// The widest span there is: sixty-four bits starting one bit in, which no
// aggregate can hold in fewer than nine bytes.
struct __attribute__((packed)) split_nine_bytes
{
    long long lead : 1;
    unsigned long long value : 64;
    char tail;
};

// Three bytes, the smallest span with no unit, in both signednesses: an
// unsigned field must not come back sign-extended and a signed one must.
struct __attribute__((packed)) split_three_bytes
{
    char lead;
    unsigned int value : 17;
    char tail;
};

struct __attribute__((packed)) split_six_bytes
{
    char lead;
    long long value : 48;
    char tail;
};

static struct split_unit split_unit_initialized = {-0x123456789LL};
static union split_union split_union_initialized = {-6};
static struct split_neighbours split_neighbours_initialized = {'a', 0x7fedcba987LL, 'b'};
static struct split_offset split_offset_designated = {.value = -2, .tail = 'c'};
static struct split_three_pieces split_three_pieces_initialized = {-1, 0x123456789abcdLL, 'd'};
static struct split_nine_bytes split_nine_bytes_initialized = {0, 0xfedcba9876543210ULL, 'e'};

// A member declarator may carry an attribute list of its own, in two places
// the shared-specifier position does not cover: after a parenthesized
// declarator, where the list follows the whole derivation rather than the
// name, and at the head of a declarator that is not the first of its list.
// Reading either as part of the declarator failed the segment, which left the
// aggregate with no members at all and blamed the first member access.
struct member_pointer_aligned
{
    char byte;
    void (*handler)(int) __attribute__((aligned(32)));
};

// The attributed declarator is the second of its list, so it also pins the
// list boundary: the attribute reaches `second` and stops there.  `first` sits
// at offset zero, where every alignment is already satisfied, so these numbers
// hold under either reading of whose attribute it is; the four shapes below
// are the ones that separate them.
struct list_declarator_aligned
{
    int first, __attribute__((aligned(32))) second;
    char tail;
};

// Whose declarator an `aligned` list sits on decides which members move, and
// the three positions disagree on every number: only the shared-specifier one
// raises the whole list.  Reading a declarator's own list as if it had been
// written in the shared position -- #680 -- gave all three the numbers of
// `shared_specifier_aligned` alone, which moved `first` and the size with it.
struct second_declarator_aligned
{
    char byte;
    int first, second __attribute__((aligned(32)));
};

struct first_declarator_aligned
{
    char byte;
    int first __attribute__((aligned(32))), second;
};

struct shared_specifier_aligned
{
    char byte;
    __attribute__((aligned(32))) int first, second;
};

// Both positions at once, which is the shape whose member runs are not
// contiguous: `second` takes the shared record and its own while `first`'s
// sits between them.  The shared `aligned(4)` asks for no more than an `int`
// already has, so a member that wrongly collected every record in the segment
// is visible in `second` rather than hidden behind the widest one.
struct shared_and_declarator_aligned
{
    char byte;
    __attribute__((aligned(4))) int first __attribute__((aligned(64))), second __attribute__((aligned(8)));
};

// `packed` splits a declarator list the same way `aligned` does, and the same
// four positions disagree: only the shared-specifier one packs the whole list.
// Reading a declarator's own `packed` as the segment's -- #688 -- gave all
// four the numbers of `packed_shared_specifier` alone, which packed `first`
// even where the attribute was written on the declarator *after* it.
struct packed_first_declarator
{
    char byte;
    int first __attribute__((packed)), second;
};

struct packed_second_declarator
{
    char byte;
    int first, second __attribute__((packed));
};

struct packed_list_declarator
{
    char byte;
    int first, __attribute__((packed)) second;
};

struct packed_shared_specifier
{
    char byte;
    __attribute__((packed)) int first, second;
};

// The parenthesized-declarator position, where the list follows the whole
// derivation rather than the name: `handler` packs and `tail` does not.
struct packed_member_pointer
{
    char byte;
    void (*handler)(int) __attribute__((packed));
    int tail;
};

// GNU `aligned(N)` only ever raises, so a request the member already satisfies
// is a no-op rather than an error -- a header writing `aligned(2)` on an
// already 4-aligned field is ordinary defensive style, and refusing it left
// the aggregate with no layout at all and a folded `sizeof` of 4 (#689).  All
// three attribute positions reach the same two layout engines, so all three
// are pinned here.
struct specifier_below_natural
{
    char byte;
    __attribute__((aligned(2))) int value;
};

struct declarator_below_natural
{
    char byte;
    int value __attribute__((aligned(2)));
};

struct aggregate_below_natural
{
    char byte;
    int value;
} __attribute__((aligned(2)));

// The same request written on a member whose natural alignment is under it,
// which is the direction that does move: the maximum that ignores the shapes
// above is what places `pad` here.
struct member_below_and_above
{
    char byte;
    __attribute__((aligned(2))) char pad[3];
};

// A bit-field declarator's own attribute list is written *after* the width --
// `int b __attribute__((packed)) : 5` is a syntax error, so this is the only
// spelling there is -- and reading the list as part of the width (#693) left
// the width unfoldable: `sizeof` still answered while `_Alignof` failed to
// lower and `offsetof` rejected the designator, on a declaration both
// reference compilers accept.  `b` is what makes the packing observable: it
// crosses the storage unit `a` opened, so packed it takes the next bit and
// unpacked it starts a new unit two bytes further on.  `tail` is where the
// two answers separate.
struct packed_bits_declarator
{
    char byte;
    int a : 8;
    int b : 24 __attribute__((packed));
    char tail;
};

struct packed_bits_list_declarator
{
    char byte;
    int a : 8, b : 24 __attribute__((packed));
    char tail;
};

// The same list with the attribute on the *first* declarator: `a` already
// starts at a unit boundary, so packing it moves nothing and `b` keeps the
// unpacked placement.  Whose declarator the list sits on is a separate
// question from where in that declarator it is written, and this is the shape
// that answers the first one for a bit-field.
struct packed_bits_first_declarator
{
    char byte;
    int a : 8 __attribute__((packed)), b : 24;
    char tail;
};

// The shared-specifier position, which packs every declarator of the list and
// so agrees with the trailing one here; it is the control that shows the
// trailing position is read at all rather than dropped.
struct packed_bits_shared_specifier
{
    char byte;
    int a : 8;
    __attribute__((packed)) int b : 24;
    char tail;
};

#pragma pack(push, 1)
struct pragma_packed
{
    char byte;
    int value;
};
#pragma pack(pop)

#pragma pack(push, 2)
struct pragma_packed_two
{
    char byte;
    int value;
};
#pragma pack(pop)

// The attributes an object declarator carries, before and after the name.
static char leading_aligned[3] __attribute__((aligned(64)));
__attribute__((aligned(128))) static char specifier_aligned[3];
static char between_them = 1;

// Which declarator of a list an `aligned` sits on decides which objects move,
// the same question #680 answered for a member and #701 for these: the scan
// that collects a declaration's alignment specifiers runs from the
// declaration's start, so every declarator but the first read the ones its
// siblings wrote and the whole list was raised.  Three unattributed names are
// what make that observable without an exact offset, which separate objects
// do not have: three four-byte objects cannot all sit on a 64-byte boundary,
// while a list raised as a whole puts every one of them there.
static int object_list_leader __attribute__((aligned(64))), object_list_second, object_list_third, object_list_fourth;

// The three specifier-position spellings, which are the ones that *do* raise
// the whole list and so are the control that the partition above keeps the
// shared half rather than dropping it: a GNU attribute before the specifiers,
// one among them, and `_Alignas`, which is a declaration specifier and never
// a declarator's.
__attribute__((aligned(64))) static int shared_list_first, shared_list_second;
static int __attribute__((aligned(64))) middle_list_first, middle_list_second;
static _Alignas(64) int alignas_list_first, alignas_list_second;

// A declarator that is not the first of its list carries its own list in the
// two positions any declarator has, at its head and after its name.  Both
// reached it before the partition, by way of the range that also reached its
// siblings; they have to still reach it and no longer reach them.
static int head_list_first, __attribute__((aligned(64))) head_list_second;
static int tail_list_first, tail_list_second __attribute__((aligned(64)));

// The same list written after a parenthesized object declarator, where it
// follows the whole derivation rather than the name.  The neighbours on either
// side are what make the boundary below a claim about the attribute: without
// it the pointer is placed at its natural alignment of 8, immediately after
// `before_file_pointer`.
static char before_file_pointer = 2;
static void (*file_pointer_aligned)(int) __attribute__((aligned(64)));
static char after_file_pointer = 3;

// An object whose declarator asks for less than its type already has, which
// both reference compilers accept and this one rejected as an invalid object
// alignment (#689).  What it is placed at is the type's own alignment, so the
// test is that the object exists, holds its value, and is at least as aligned
// as it asked to be.
static int object_below_natural __attribute__((aligned(2))) = 7;

// The same request on a typedef is the one that is honoured rather than
// ignored: there `aligned` sets the alignment of the type the name declares,
// in both directions, and `lowered_scalar` is two-byte aligned in GCC and
// Clang alike -- which no other spelling of the attribute achieves without
// `packed`.
// A header handing out an aligned scalar this way is the ordinary spelling for
// a cache-line- or SIMD-aligned one, and dropping the request (#696) left
// every object and every aggregate that embedded it at the alignment of the
// type it aliases, silently.
typedef int raised_scalar __attribute__((aligned(32)));
typedef int lowered_scalar __attribute__((aligned(2)));
typedef struct one_byte
{
    char byte;
} raised_aggregate __attribute__((aligned(16)));

// The alias is a type of its own: `int` and `struct one_byte` keep their
// natural alignments, and a typedef of the alias inherits the request rather
// than losing it.
typedef raised_scalar raised_again;

// The typedef spelling of the same declarator list, where raising the whole
// list was worse than silent: the leader's attribute landed in the
// specifier-position run that `alignment specifier cannot be applied to a
// typedef` rejects by name, so the declaration was dropped and both names
// went undeclared.  The diagnostic is right about `_Alignas`, which may not
// appear in a typedef declaration at all, and was reading a declarator's GNU
// attribute as one.
typedef int typedef_list_raised __attribute__((aligned(64))), typedef_list_plain;
typedef int typedef_list_plain_first, typedef_list_raised_second __attribute__((aligned(64)));

// The specifier position of the same list, where the request belongs to the
// declaration's type rather than to one declarator, so Clang and GCC give
// *every* name of the list the alignment.  Buster refused the declaration by
// the position alone (#715): the diagnostic is right that `_Alignas` may not
// appear in a typedef declaration -- a typedef declares no object for a
// declaration's alignment to apply to, and both reference compilers reject
// `typedef _Alignas(16) int t;` -- and a GNU `aligned` written there is an
// ordinary type attribute instead.
typedef int __attribute__((aligned(16))) typedef_specifier_first, typedef_specifier_second;

// Both positions in one declaration: the shared request reaches both names and
// a declarator's own raises the name that wrote it alone.  This is the one
// shape here the two reference compilers answer differently -- GCC drops the
// declarator's own and leaves `typedef_specifier_raised` at 8 -- and Clang is
// the oracle, as it is for every other alignment answer in this file.
typedef int __attribute__((aligned(8))) typedef_specifier_shared, typedef_specifier_raised __attribute__((aligned(64)));

struct raised_typedef_member
{
    char byte;
    raised_scalar value;
};

// The lowering direction, where the member sits *earlier* than its natural
// alignment would put it and the aggregate's own alignment drops with it.
struct lowered_typedef_member
{
    char byte;
    lowered_scalar value;
    char tail;
};

struct raised_aggregate_member
{
    char byte;
    raised_aggregate value;
    char tail;
};

static raised_scalar file_raised_object;
static char between_typedef_objects = 7;
static lowered_scalar file_lowered_object;

// An array element has to be addressable at its own alignment in every slot,
// so an element whose size is not a multiple of its alignment is refused by
// GCC and Clang alike, and now here too (#703).  These are the neighbouring
// shapes that stay well-formed, and they are the reason the rule is stated as
// "size is not a multiple of the alignment" rather than "alignment above the
// size": an aggregate's own `aligned` rounds its *size* up to the alignment,
// so `padded_element` is sixteen bytes and tiles; the lowering direction
// leaves the size alone and divides; and a request the type already satisfies
// changes nothing at all.
struct __attribute__((aligned(16))) padded_element
{
    char byte;
};
typedef struct padded_element padded_alias;
typedef int exact_scalar __attribute__((aligned(4)));

// The typedef spelling of the array, which is the one #703 was reported
// against and the one that carries no object at all.
typedef struct padded_element padded_element_pair[2];
typedef lowered_scalar lowered_scalar_pair[2];
typedef exact_scalar exact_scalar_pair[2];

static padded_element_pair padded_element_array;
static padded_alias padded_alias_array[2];
static lowered_scalar_pair lowered_scalar_array;
static exact_scalar_pair exact_scalar_array;

// The two spellings of an array of an over-aligned element that the check
// above could not see, in their well-formed form (#713).  A parenthesized
// declarator whose base is a typedef name or a tag was read as a function
// declaration named by that base, so neither pointer below existed at all --
// no definition emitted, and a `sizeof` of the pointee that folded nothing --
// while the array type name in an expression is resolved during lowering and
// never reaches the type table.  Both are ordinary shapes with an element
// whose size divides its alignment, so both stay accepted.
typedef int plain_scalar;
static plain_scalar file_rows[2][3];
static plain_scalar (*typedef_pointer_to_array)[3] = &file_rows[1];
static struct padded_element padded_pairs[2][2];
static struct padded_element (*tag_pointer_to_array)[2] = &padded_pairs[1];

// A qualifier does not take the request away.  `const` and `volatile` of an
// aligned alias are the alias's own alignment in GCC and Clang alike, and
// losing it there was #696's defect reached through one more spelling: a
// header handing out a cache-line-aligned scalar produced an underaligned
// object the moment a program wrote `const` in front of it, in every position
// and with no diagnostic (#714).
struct const_typedef_member
{
    char byte;
    const raised_scalar value;
};

struct volatile_typedef_member
{
    char byte;
    volatile raised_scalar value;
};

// The lowering direction through a qualifier, where the member sits *earlier*
// than its natural alignment would put it and the aggregate's own alignment
// drops with it.
struct const_lowered_typedef_member
{
    char byte;
    const lowered_scalar value;
    char tail;
};

union const_typedef_union
{
    char byte;
    const raised_scalar value;
};

// A qualified alias is itself an alias, so a typedef of one and a second
// qualifier both carry the request on.
typedef const raised_scalar const_raised_alias;
typedef volatile const_raised_alias const_volatile_raised_alias;

static const raised_scalar file_const_raised_object;
static char between_const_typedef_objects = 11;
static volatile lowered_scalar file_volatile_lowered_object;

// The array element position through a qualifier.  A qualified alias tiles
// exactly when the alias does, which is why `const raised_scalar a[2]` is
// absent: it is the over-aligned element #703 refuses, and it only became
// reachable once the qualified form kept its alignment.
typedef const lowered_scalar const_lowered_pair[2];
typedef const exact_scalar const_exact_pair[2];
static const_lowered_pair const_lowered_array;
static const_exact_pair const_exact_array;

// `_Atomic` is the one that does take it away, and it is the one place the two
// reference compilers disagree: Clang treats `_Atomic T` as constructing a type
// rather than qualifying one, so the alias's request does not travel into it,
// while GCC keeps it.  Clang is the oracle here, and it was already the answer
// this frontend gave through a typedef of the atomic type -- while the *inline*
// spelling in a type name answered the alias's own number, so one type had two
// alignments depending on how it was written (#726).  Every shape below is
// therefore written twice, once each way, and the pair is what the fixture
// pins; the numbers are Clang's.  None of it joins the cross-linked pair in
// basic_c_packed_layout_shapes.h: that half is compiled by whichever host
// compiler the platform has, and a GCC host answers 32 where a Clang host
// answers 4.
struct atomic_typedef_member
{
    char byte;
    _Atomic raised_scalar value;
};

union atomic_typedef_union
{
    char byte;
    _Atomic raised_scalar value;
};

// The lowering direction, where the alias asked for *less* than the natural
// alignment and `_Atomic` hands the natural one back: the member sits at four
// rather than at two, which is the opposite move from the `const` spelling
// above and is why dropping the request is not the same as ignoring it.
struct atomic_lowered_typedef_member
{
    char byte;
    _Atomic lowered_scalar value;
    char tail;
};

struct atomic_aggregate_member
{
    char byte;
    _Atomic raised_aggregate value;
    char tail;
};

typedef _Atomic raised_scalar atomic_raised_alias;
typedef _Atomic lowered_scalar atomic_lowered_alias;

// `_Atomic ( T )` is the operator spelling of the same type, resolved by a
// branch of its own that reaches the alias's IrType the same way the qualifier
// spelling did.
typedef _Atomic(raised_scalar) atomic_operator_alias;

// `_Atomic` written on a type that is *already* atomic keeps the request in
// both references, which is why the rule tests the step rather than the
// result: nothing is being constructed there, so nothing takes the request
// away.
typedef _Atomic int atomic_int_scalar;
typedef atomic_int_scalar atomic_raised_atomic __attribute__((aligned(32)));

static _Atomic raised_scalar file_atomic_raised_object;
static char between_atomic_typedef_objects = 13;

// The array element position, where dropping the request is what makes the
// element tile at all: `raised_scalar a[2]` is the over-aligned element #703
// refuses, and the atomic of it is four-byte aligned and four bytes wide.  GCC
// refuses both spellings of this array for the same reason it refuses the
// unqualified one, which is the second opinion rather than the target.
typedef _Atomic raised_scalar atomic_raised_pair[2];
static atomic_raised_pair atomic_raised_array;

// The parse-time layout engine folds a `_Static_assert` where the lowering one
// folds the same expression inside main, and the two must agree or a folded
// `_Alignof` contradicts the object it measures.  These are the shapes that
// disagreed, stated once for each engine.
_Static_assert(_Alignof(_Atomic raised_scalar) == _Alignof(atomic_raised_alias), "one type, one alignment");
_Static_assert(_Alignof(_Atomic(raised_scalar)) == _Alignof(atomic_operator_alias), "the operator spelling too");
_Static_assert(sizeof(_Atomic raised_scalar) == sizeof(atomic_raised_alias), "one type, one size");
_Static_assert(_Alignof(_Atomic raised_scalar) == 4, "_Atomic drops the request");
_Static_assert(_Alignof(_Atomic atomic_raised_atomic) == 32, "an already atomic type keeps it");

// The other half of what `_Atomic` constructs: Clang pads an atomic aggregate
// up to the next power of two and aligns it there, so a value it can operate
// on lock-free has an instruction that reaches it -- there is no three-byte
// atomic instruction and there is a four-byte one.  The ceiling is the
// target's maximum lock-free width, 128 bits here, and above it the type keeps
// the aggregate's own layout: `_Atomic` of a seventeen-byte record is
// seventeen bytes aligned one in Clang too, so the rule is not "round every
// aggregate up" (#731).  GCC pads nothing -- it answers 3 aligned 1 for the
// first shape below and 12 aligned 4 for `atomic_twelve` -- and AGENTS.md
// names Clang the oracle, which is what these numbers are.  Like the shapes
// above they stay out of basic_c_packed_layout_shapes.h, whose other half is
// whichever host compiler the platform has.
//
// All four spellings of the type are pinned, and they have to answer alike.
// The three below are written on a typedef of an anonymous aggregate; the
// fourth, `_Atomic` written directly in front of the tag keyword, needs a tag
// and is grouped with the tagged shapes further down (#761).
typedef struct
{
    char a, b, c;
} atomic_three;

typedef union
{
    char a[3];
} atomic_union_three;

typedef struct
{
    char a[5];
} atomic_five;

typedef struct
{
    int word;
    char tail[8];
} atomic_twelve;

typedef struct
{
    char a[16];
} atomic_sixteen;

// One byte past the ceiling, which is where the promotion stops.
typedef struct
{
    char a[17];
} atomic_seventeen;

// A zero-sized aggregate still takes a byte: an atomic operation on it has to
// touch something.  GCC answers zero for the record and one for the atomic,
// like Clang.  Whether the record is zero-sized at all is the ABI's business
// rather than this rule's -- it is four bytes under the Microsoft one -- so
// the two shapes below are stated as a relation to it rather than as a number,
// which is the same sentence on either: the padded size, or one byte when
// there is nothing to pad.
typedef struct
{
} atomic_empty;

typedef _Atomic atomic_three atomic_three_alias;
typedef _Atomic(atomic_three) atomic_three_operator_alias;
typedef _Atomic atomic_union_three atomic_union_alias;
typedef _Atomic atomic_five atomic_five_alias;
typedef _Atomic atomic_twelve atomic_twelve_alias;
typedef _Atomic atomic_sixteen atomic_sixteen_alias;
typedef _Atomic atomic_seventeen atomic_seventeen_alias;
typedef _Atomic atomic_empty atomic_empty_alias;

// An alias over the atomic type replaces the alignment and keeps the padding,
// in both directions: the size is four whether the request is above the
// promoted alignment or below it.
typedef atomic_three_alias atomic_three_raised __attribute__((aligned(32)));
typedef atomic_three_alias atomic_three_lowered __attribute__((aligned(1)));

// The array element position, where the padding is what the stride is.
typedef atomic_three_alias atomic_three_pair[2];

struct atomic_promoted_member
{
    char byte;
    atomic_three_alias value;
    char tail;
};

// Past the ceiling the member sits at one and the record is nineteen bytes,
// which is the same aggregate laid out with no promotion at all.
struct atomic_unpromoted_member
{
    char byte;
    atomic_seventeen_alias value;
    char tail;
};

struct atomic_promoted_array_member
{
    char byte;
    atomic_three_pair values;
    char tail;
};

static atomic_three_alias file_atomic_three_object;
static char between_atomic_promoted_objects = 17;
static atomic_three_pair file_atomic_three_array;

// The fourth spelling: `_Atomic` written in front of the tag keyword.  It
// builds the same type the other three do, and it is checked against them on
// the same tag rather than only against a number, because what went wrong was
// that the qualifier reached neither -- the prefix scan stepped over it and
// the aggregate branch handed back the tag's own type, so `sizeof` answered 3
// and an assignment was an ordinary aggregate copy where the program asked
// for an atomic store (#761).  `const` and `volatile` ride the same prefix
// run and are pinned beside it: neither promotes anything, so a run that
// reached the type carrying the wrong qualifier would show up here as a
// changed size.
struct atomic_tag_three
{
    char a, b, c;
};

union atomic_tag_union
{
    char a[3];
};

enum atomic_tag_enum
{
    ATOMIC_TAG_ENUM_VALUE = 1
};

typedef _Atomic struct atomic_tag_three atomic_tag_leading_alias;

struct atomic_leading_member
{
    char byte;
    _Atomic struct atomic_tag_three value;
    char tail;
};

static _Atomic struct atomic_tag_three file_atomic_leading_object;
static char between_atomic_leading_objects = 19;

// The enum-constant evaluator folds sizeof on a third road into the parse
// engine -- an operand walk that never enters the type-parse machine, because
// the machine is already running the enum body it is folding for.  That walk
// dropped the leading run too, and dropped it for `const` and `volatile` hard
// enough that the whole enumerator failed to fold.
//
// The fourth spelling, `_Atomic ( T )`, is the operator rather than the
// qualifier, and that walk had no branch for it at all: it resolved nothing
// whatever T was -- a tag, a typedef name, or `int` -- and the enumerator that
// spelled it failed, which fails the enum type and leaves every enumerator
// beside it undeclared for the rest of the file (#784).  It is pinned here on
// all three operands, and with a `*` and an array suffix over it, because what
// the branch has to do is resolve a whole type name rather than a base type.
enum
{
    atomic_leading_folded_size = sizeof(_Atomic struct atomic_tag_three),
    atomic_leading_folded_alignment = _Alignof(_Atomic struct atomic_tag_three),
    atomic_trailing_folded_size = sizeof(struct atomic_tag_three _Atomic),
    atomic_plain_folded_size = sizeof(struct atomic_tag_three),
    atomic_const_folded_size = sizeof(const struct atomic_tag_three),
    atomic_volatile_folded_size = sizeof(volatile struct atomic_tag_three),
    atomic_operator_folded_size = sizeof(_Atomic(struct atomic_tag_three)),
    atomic_operator_folded_alignment = _Alignof(_Atomic(struct atomic_tag_three)),
    atomic_operator_alias_folded_size = sizeof(_Atomic(atomic_three)),
    atomic_operator_alias_folded_alignment = _Alignof(_Atomic(atomic_three)),
    atomic_operator_scalar_folded_size = sizeof(_Atomic(int)),
    atomic_operator_pointer_folded_size = sizeof(_Atomic(atomic_three) *),
    atomic_operator_array_folded_size = sizeof(_Atomic(atomic_three)[2]),
    // The qualifier the operand keeps is the one written outside the
    // parentheses, in either position; neither moves the layout, so a run
    // dropped on one side would show up as a failure to fold rather than as a
    // changed number.
    atomic_operator_const_leading_folded_size = sizeof(const _Atomic(int)),
    atomic_operator_const_trailing_folded_size = sizeof(_Atomic(int) const),
    // The operand of one specifier may be another one's pointer, so the
    // nesting follows the source and the walk keeps its levels on an explicit
    // stack.  This is the shape that unwinds it more than once.
    atomic_operator_nested_folded_size = sizeof(_Atomic(_Atomic(int) *))
};

// A complex type is the one scalar whose size is a power of two while its
// alignment is not equal to it, so the promotion moves the alignment alone:
// Clang and GCC agree on both of these.
_Static_assert(sizeof(_Atomic _Complex float) == 8 && _Alignof(_Atomic _Complex float) == 8, "atomic complex float");
_Static_assert(sizeof(_Atomic _Complex double) == 16 && _Alignof(_Atomic _Complex double) == 16, "atomic complex double");
// `long double` is where the target shows through, so the third one is stated
// as a relation rather than as a number: it is `double` on Apple AArch64,
// where `_Complex long double` is sixteen bytes and does take the promotion,
// and sixteen bytes wide elsewhere here, where the complex type is thirty-two
// and past the ceiling.  Either way the size is untouched, which is the
// sentence both answers share; `atomic_seventeen_alias` above pins the
// ceiling itself with a size no target moves.
_Static_assert(sizeof(_Atomic _Complex long double) == sizeof(_Complex long double), "the size is untouched");

// The three spellings of one type, stated for the parse engine the way the
// runtime checks state them for the lowering one.
_Static_assert(sizeof(_Atomic atomic_three) == 4 && _Alignof(_Atomic atomic_three) == 4, "written inline");
_Static_assert(sizeof(atomic_three_alias) == 4 && _Alignof(atomic_three_alias) == 4, "through a typedef");
_Static_assert(sizeof(atomic_three_operator_alias) == 4 && _Alignof(atomic_three_operator_alias) == 4, "the operator spelling");
_Static_assert(sizeof(atomic_union_alias) == 4 && _Alignof(atomic_union_alias) == 4, "a union promotes too");
_Static_assert(sizeof(atomic_five_alias) == 8 && _Alignof(atomic_five_alias) == 8, "five rounds to eight");
_Static_assert(sizeof(atomic_twelve_alias) == 16 && _Alignof(atomic_twelve_alias) == 16, "twelve rounds to sixteen");
_Static_assert(sizeof(atomic_sixteen_alias) == 16 && _Alignof(atomic_sixteen_alias) == 16, "sixteen is the last one");
_Static_assert(sizeof(atomic_seventeen_alias) == 17 && _Alignof(atomic_seventeen_alias) == 1, "seventeen is past the ceiling");
_Static_assert(sizeof(atomic_empty_alias) == (sizeof(atomic_empty) ? sizeof(atomic_empty) : 1), "a zero-sized aggregate takes a byte");
_Static_assert(_Alignof(atomic_empty_alias) == (sizeof(atomic_empty) ? sizeof(atomic_empty) : 1), "and is aligned to it");
_Static_assert(sizeof(const atomic_three_alias) == 4 && _Alignof(const atomic_three_alias) == 4, "a qualifier keeps the promotion");
_Static_assert(sizeof(atomic_three_raised) == 4 && _Alignof(atomic_three_raised) == 32, "an alias over it keeps the padding");
_Static_assert(sizeof(atomic_three_lowered) == 4 && _Alignof(atomic_three_lowered) == 1, "and keeps it when it lowers too");
_Static_assert(sizeof(atomic_three_pair) == 8 && _Alignof(atomic_three_pair) == 4, "the element tiles at four");
_Static_assert(sizeof(struct atomic_promoted_member) == 12, "the member sits at four");
_Static_assert(sizeof(struct atomic_unpromoted_member) == 19, "and at one past the ceiling");
_Static_assert(sizeof(_Atomic struct atomic_tag_three) == 4 && _Alignof(_Atomic struct atomic_tag_three) == 4, "written before the tag");
_Static_assert(sizeof(_Atomic struct atomic_tag_three) == sizeof(struct atomic_tag_three _Atomic) &&
                   _Alignof(_Atomic struct atomic_tag_three) == _Alignof(struct atomic_tag_three _Atomic),
               "the leading and trailing spellings are one type");
_Static_assert(sizeof(_Atomic struct atomic_tag_three) == sizeof(_Atomic(struct atomic_tag_three)) &&
                   _Alignof(_Atomic struct atomic_tag_three) == _Alignof(_Atomic(struct atomic_tag_three)),
               "and so is the operator spelling");
_Static_assert(sizeof(atomic_tag_leading_alias) == 4 && _Alignof(atomic_tag_leading_alias) == 4, "a typedef of it carries the promotion");
_Static_assert(sizeof(_Atomic union atomic_tag_union) == 4 && _Alignof(_Atomic union atomic_tag_union) == 4, "a union before the tag");
_Static_assert(sizeof(_Atomic enum atomic_tag_enum) == 4 && _Alignof(_Atomic enum atomic_tag_enum) == 4, "an enum before the tag");
_Static_assert(sizeof(const struct atomic_tag_three) == 3 && _Alignof(const struct atomic_tag_three) == 1, "const before the tag promotes nothing");
_Static_assert(sizeof(volatile struct atomic_tag_three) == 3, "and neither does volatile");
_Static_assert(sizeof(_Atomic const struct atomic_tag_three) == 4 && sizeof(const _Atomic struct atomic_tag_three) == 4,
               "either order of the two words is the atomic type");
_Static_assert(sizeof(struct atomic_leading_member) == 12, "the member sits at four here too");
_Static_assert(atomic_leading_folded_size == 4 && atomic_leading_folded_alignment == 4, "the enum-constant fold answers alike");
_Static_assert(atomic_trailing_folded_size == 4 && atomic_plain_folded_size == 3, "and tells the two spellings apart");
_Static_assert(atomic_const_folded_size == 3 && atomic_volatile_folded_size == 3, "a const or volatile operand folds at all");
// The operator spelling folds the numbers the qualifier spellings fold, and
// each one is stated against what `_Static_assert` answers for the same
// operand rather than only against a literal: the two roads into the parse
// engine answering differently is what #784 was.
_Static_assert(atomic_operator_folded_size == 4 && atomic_operator_folded_alignment == 4, "the operator spelling folds over a tag");
_Static_assert(atomic_operator_folded_size == sizeof(_Atomic(struct atomic_tag_three)) &&
                   atomic_operator_folded_alignment == _Alignof(_Atomic(struct atomic_tag_three)),
               "and folds what the type-parse machine folds");
_Static_assert(atomic_operator_folded_size == atomic_leading_folded_size && atomic_operator_folded_size == atomic_trailing_folded_size,
               "all four spellings of one tag are one type");
_Static_assert(atomic_operator_alias_folded_size == 4 && atomic_operator_alias_folded_alignment == 4, "over a typedef name too");
_Static_assert(atomic_operator_alias_folded_size == sizeof(_Atomic(atomic_three)) &&
                   atomic_operator_alias_folded_alignment == _Alignof(_Atomic(atomic_three)),
               "which the machine folds alike");
// `int` is the operand that says the failure was never about tags.
_Static_assert(atomic_operator_scalar_folded_size == sizeof(int), "a scalar operand is not promoted");
_Static_assert(atomic_operator_pointer_folded_size == sizeof(void *), "a `*` over it is a pointer");
_Static_assert(atomic_operator_array_folded_size == 8, "and an array suffix tiles the padded element");
_Static_assert(atomic_operator_const_leading_folded_size == 4 && atomic_operator_const_trailing_folded_size == 4,
               "a qualifier on either side of the parentheses folds at all");
_Static_assert(atomic_operator_nested_folded_size == sizeof(_Atomic(_Atomic(int) *)), "a nested operand folds like the machine's");
_Static_assert(atomic_operator_nested_folded_size == sizeof(void *), "and a pointer to an atomic is a pointer");

int main(void)
{
    if (sizeof(struct leading) != 5 || _Alignof(struct leading) != 1) return 1;
    if (sizeof(struct trailing) != 5 || _Alignof(struct trailing) != 1) return 2;
    if (sizeof(struct member_packed) != 5 || _Alignof(struct member_packed) != 1) return 3;
    if (sizeof(struct packed_then_aligned) != 8 || _Alignof(struct packed_then_aligned) != 8) return 4;
    if (sizeof(union packed_union) != 4 || _Alignof(union packed_union) != 1) return 5;
    if (sizeof(union packed_bit_union) != 1 || _Alignof(union packed_bit_union) != 1) return 88;
    if (sizeof(union packed_bit_union_wide) != 2 || _Alignof(union packed_bit_union_wide) != 1) return 89;
    if (sizeof(union packed_bit_union_mixed) != 4 || _Alignof(union packed_bit_union_mixed) != 1) return 90;
    if (sizeof(union unpacked_bit_union) != 4 || _Alignof(union unpacked_bit_union) != 4) return 91;
    if (sizeof(struct packed_bit_union_record) != 2) return 92;
    if (sizeof(struct nested) != 9 || _Alignof(struct nested) != 1) return 6;
    if (sizeof(struct packed_bits) != 2) return 7;
    if (sizeof(struct offset_bits) != 4) return 8;
    if (sizeof(struct zero_width_bits) != 5) return 9;
    if (sizeof(struct narrow_unit) != 3 || _Alignof(struct narrow_unit) != 1) return 68;
    if (sizeof(struct narrow_unit_member) != 3 || _Alignof(struct narrow_unit_member) != 1) return 69;
    if (sizeof(struct narrow_unit_declarator) != 3 || _Alignof(struct narrow_unit_declarator) != 1) return 80;
    if (sizeof(struct narrow_unit_wide) != 4 || _Alignof(struct narrow_unit_wide) != 1) return 70;
    if (sizeof(struct slid_unit) != 4 || _Alignof(struct slid_unit) != 1) return 107;
    if (sizeof(struct trailing_member) != 4 || _Alignof(struct trailing_member) != 1) return 108;
    if (sizeof(struct overlapping_units) != 2 || _Alignof(struct overlapping_units) != 1) return 109;
    if (sizeof(struct overlapping_units_reversed) != 2 || _Alignof(struct overlapping_units_reversed) != 1) return 110;
    // The split shapes, every one a width whose byte count is not a power of
    // two, which is what used to be refused outright.
    if (sizeof(struct split_unit) != 5 || _Alignof(struct split_unit) != 1) return 118;
    if (sizeof(union split_union) != 5 || _Alignof(union split_union) != 1) return 119;
    if (sizeof(struct split_neighbours) != 7) return 120;
    if (sizeof(struct split_offset) != 7) return 121;
    if (sizeof(struct split_three_pieces) != 8) return 122;
    if (sizeof(struct split_nine_bytes) != 10) return 123;
    if (sizeof(struct split_three_bytes) != 5) return 124;
    if (sizeof(struct split_six_bytes) != 8) return 125;
    if (sizeof(struct pragma_packed) != 5 || _Alignof(struct pragma_packed) != 1) return 10;
    if (sizeof(struct pragma_packed_two) != 6 || _Alignof(struct pragma_packed_two) != 2) return 11;
    if (sizeof(struct member_pointer_aligned) != 64 || _Alignof(struct member_pointer_aligned) != 32) return 28;
    if (sizeof(struct list_declarator_aligned) != 64 || _Alignof(struct list_declarator_aligned) != 32) return 29;
    if (sizeof(struct second_declarator_aligned) != 64 || _Alignof(struct second_declarator_aligned) != 32) return 34;
    if (sizeof(struct first_declarator_aligned) != 64 || _Alignof(struct first_declarator_aligned) != 32) return 35;
    if (sizeof(struct shared_specifier_aligned) != 96 || _Alignof(struct shared_specifier_aligned) != 32) return 36;
    if (sizeof(struct shared_and_declarator_aligned) != 128 || _Alignof(struct shared_and_declarator_aligned) != 64) return 37;
    if (sizeof(struct packed_first_declarator) != 12 || _Alignof(struct packed_first_declarator) != 4) return 46;
    if (sizeof(struct packed_second_declarator) != 12 || _Alignof(struct packed_second_declarator) != 4) return 47;
    if (sizeof(struct packed_list_declarator) != 12 || _Alignof(struct packed_list_declarator) != 4) return 48;
    if (sizeof(struct packed_shared_specifier) != 9 || _Alignof(struct packed_shared_specifier) != 1) return 49;
    if (sizeof(struct packed_member_pointer) != 16 || _Alignof(struct packed_member_pointer) != 4) return 50;
    if (sizeof(struct specifier_below_natural) != 8 || _Alignof(struct specifier_below_natural) != 4) return 68;
    if (sizeof(struct declarator_below_natural) != 8 || _Alignof(struct declarator_below_natural) != 4) return 69;
    if (sizeof(struct aggregate_below_natural) != 8 || _Alignof(struct aggregate_below_natural) != 4) return 70;
    if (sizeof(struct member_below_and_above) != 6 || _Alignof(struct member_below_and_above) != 2) return 71;
    if (sizeof(struct packed_bits_declarator) != 8 || _Alignof(struct packed_bits_declarator) != 4) return 78;
    if (sizeof(struct packed_bits_list_declarator) != 8 || _Alignof(struct packed_bits_list_declarator) != 4) return 79;
    if (sizeof(struct packed_bits_first_declarator) != 8 || _Alignof(struct packed_bits_first_declarator) != 4) return 80;
    if (sizeof(struct packed_bits_shared_specifier) != 8 || _Alignof(struct packed_bits_shared_specifier) != 4) return 81;

    // Offsets, read through addresses so a size that happens to match cannot
    // hide a member in the wrong place.
    struct leading leading_value;
    if ((char *)&leading_value.value - (char *)&leading_value != 1) return 12;
    struct member_packed member_value;
    if ((char *)&member_value.value - (char *)&member_value != 1) return 13;
    struct nested nested_value;
    if ((char *)&nested_value.inner - (char *)&nested_value != 1) return 14;
    struct pragma_packed pragma_value;
    if ((char *)&pragma_value.value - (char *)&pragma_value != 1) return 15;
    struct pragma_packed_two pragma_two_value;
    if ((char *)&pragma_two_value.value - (char *)&pragma_two_value != 2) return 16;

    // Values survive the unaligned placement in both directions.
    leading_value.byte = 'p';
    leading_value.value = -123456789;
    if (leading_value.byte != 'p' || leading_value.value != -123456789) return 17;
    struct trailing trailing_value;
    trailing_value.byte = 'q';
    trailing_value.value = 0x7fedcba9;
    if (trailing_value.byte != 'q' || trailing_value.value != 0x7fedcba9) return 18;

    struct packed_bits bits;
    bits.low = 5;
    bits.high = 21;
    bits.tail = 'z';
    if (bits.low != 5 || bits.high != 21 || bits.tail != 'z') return 19;

    struct offset_bits offset_value;
    offset_value.lead = 'a';
    offset_value.value = -77;
    if (offset_value.lead != 'a' || offset_value.value != -77) return 20;

    struct zero_width_bits zero_width;
    zero_width.before = -3;
    zero_width.after = 2;
    if (zero_width.before != -3 || zero_width.after != 2) return 21;

    // The narrowed unit addresses the same bits the platform compiler does:
    // the neighbours on either side are what make a value that survives a
    // read-modify-write a claim about the unit rather than about the field.
    struct narrow_unit narrow;
    narrow.lead = 'a';
    narrow.value = -6;
    narrow.tail = 'b';
    if (narrow.lead != 'a' || narrow.value != -6 || narrow.tail != 'b') return 71;
    for (int narrow_value = -16; narrow_value < 16; narrow_value += 1)
    {
        narrow.value = narrow_value;
        if (narrow.value != narrow_value || narrow.lead != 'a' || narrow.tail != 'b') return 72;
    }
    if ((char *)&narrow.tail - (char *)&narrow != 2) return 73;
    struct narrow_unit_member narrow_member;
    narrow_member.lead = 'c';
    narrow_member.value = 7;
    narrow_member.tail = 'd';
    if (narrow_member.lead != 'c' || narrow_member.value != 7 || narrow_member.tail != 'd') return 74;
    if ((char *)&narrow_member.tail - (char *)&narrow_member != 2) return 75;
    struct narrow_unit_declarator narrow_declarator;
    narrow_declarator.lead = 'g';
    narrow_declarator.value = -9;
    narrow_declarator.tail = 'h';
    if (narrow_declarator.lead != 'g' || narrow_declarator.value != -9 || narrow_declarator.tail != 'h') return 81;
    if ((char *)&narrow_declarator.tail - (char *)&narrow_declarator != 2) return 82;
    struct narrow_unit_wide narrow_wide;
    narrow_wide.lead = 'e';
    narrow_wide.value = 4095;
    narrow_wide.tail = 'f';
    if (narrow_wide.lead != 'e' || narrow_wide.value != 4095u || narrow_wide.tail != 'f') return 76;
    if ((char *)&narrow_wide.tail - (char *)&narrow_wide != 3) return 77;
    // The folds that write a bit-field into static bytes read the same unit:
    // through the declared type they would run off the end of the object.
    if (narrow_unit_initialized.lead != 1 || narrow_unit_initialized.value != -6 || narrow_unit_initialized.tail != 2) return 78;
    if (narrow_unit_designated.lead != 0 || narrow_unit_designated.value != 5 || narrow_unit_designated.tail != 6) return 79;

    // A packed union's bit-field is read through a unit the union has room
    // for, and `tail` is what a store through the declared type's unit would
    // clobber: the field round-trips its thirty-two values with the neighbour
    // intact, and only a one-byte union puts that neighbour at offset one.
    struct packed_bit_union_record union_record;
    union_record.tail = 'z';
    for (int union_value = -16; union_value < 16; union_value += 1)
    {
        union_record.bits.value = union_value;
        if (union_record.bits.value != union_value || union_record.tail != 'z') return 93;
    }
    if ((char *)&union_record.tail - (char *)&union_record != 1) return 94;
    union_record.bits.lead = 'y';
    if (union_record.bits.lead != 'y' || union_record.tail != 'z') return 95;

    union packed_bit_union_wide union_wide;
    union_wide.value = 4095u;
    if (union_wide.value != 4095u) return 96;
    union packed_bit_union_mixed union_mixed;
    union_mixed.wide = -123456789;
    if (union_mixed.wide != -123456789) return 97;
    union_mixed.narrow = -6;
    if (union_mixed.narrow != -6) return 98;
    union unpacked_bit_union union_unpacked;
    union_unpacked.value = 7;
    if (union_unpacked.value != 7) return 99;
    // The fold that writes a union bit-field into static bytes reads the same
    // unit the loads above do.
    if (packed_bit_union_initialized.value != -6) return 100;

    // The same units reached from an automatic object's initializer, where
    // every member is written into one slot rather than folded into bytes.
    // The members a unit covers are the claim: a whole-unit store loses
    // `slid.lead`, `overlapping.narrow`, `overlapping_reversed.wide`, and --
    // in the designated forms, which write the trailing member before the
    // unit rather than after it -- `slid_designated.tail` and
    // `trailing_designated.tail`.
    struct narrow_unit narrow_automatic = {3, -7, 4};
    if (narrow_automatic.lead != 3 || narrow_automatic.value != -7 || narrow_automatic.tail != 4) return 111;
    struct slid_unit slid = {1, -6, 63, 2};
    if (slid.lead != 1 || slid.first != -6 || slid.second != 63 || slid.tail != 2) return 112;
    struct slid_unit slid_designated = {.tail = 9, .first = -6};
    if (slid_designated.lead != 0 || slid_designated.first != -6 || slid_designated.second != 0 || slid_designated.tail != 9) return 113;
    struct trailing_member trailing = {1, 2, 3, 4};
    if (trailing.first != 1 || trailing.second != 2 || trailing.third != 3 || trailing.tail != 4) return 114;
    struct trailing_member trailing_designated = {.tail = 5, .second = 6};
    if (trailing_designated.first != 0 || trailing_designated.second != 6 || trailing_designated.third != 0 || trailing_designated.tail != 5) return 115;
    struct overlapping_units overlapping = {13, -100};
    if (overlapping.narrow != 13 || overlapping.wide != -100) return 116;
    struct overlapping_units_reversed overlapping_reversed = {-100, 13};
    if (overlapping_reversed.wide != -100 || overlapping_reversed.narrow != 13) return 117;
    // A round trip through every piece, with the neighbours on both sides
    // checked after each write: a piece that wrote one byte too many takes a
    // neighbour with it, and a piece that wrote one too few loses the top of
    // the value.
    struct split_neighbours split_pair;
    split_pair.lead = 'a';
    split_pair.tail = 'b';
    split_pair.value = 0;
    for (int split_bit = 0; split_bit < 40; split_bit += 1)
    {
        long long split_value = (long long)1 << split_bit;
        long long split_expected = split_bit == 39 ? -split_value : split_value;
        split_pair.value = split_value;
        if (split_pair.value != split_expected || split_pair.lead != 'a' || split_pair.tail != 'b') return 126;
        split_pair.value = ~split_value;
        split_expected = (long long)(((unsigned long long)~split_value & 0xffffffffffULL) << 24) >> 24;
        if (split_pair.value != split_expected || split_pair.lead != 'a' || split_pair.tail != 'b') return 127;
    }

    struct split_offset split_shifted;
    split_shifted.lead = -1;
    split_shifted.tail = 'c';
    split_shifted.value = -0x123456789LL;
    if (split_shifted.value != -0x123456789LL || split_shifted.lead != -1 || split_shifted.tail != 'c') return 128;
    split_shifted.lead = 0;
    if (split_shifted.value != -0x123456789LL || split_shifted.lead != 0 || split_shifted.tail != 'c') return 129;

    struct split_three_pieces split_three;
    split_three.lead = -1;
    split_three.tail = 'd';
    split_three.value = 0x123456789abcdLL;
    if (split_three.value != 0x123456789abcdLL || split_three.lead != -1 || split_three.tail != 'd') return 130;
    split_three.value = -1;
    if (split_three.value != -1 || split_three.lead != -1 || split_three.tail != 'd') return 131;

    struct split_nine_bytes split_nine;
    split_nine.lead = 0;
    split_nine.tail = 'e';
    split_nine.value = 0xfedcba9876543210ULL;
    if (split_nine.value != 0xfedcba9876543210ULL || split_nine.lead != 0 || split_nine.tail != 'e') return 132;
    split_nine.lead = -1;
    if (split_nine.value != 0xfedcba9876543210ULL || split_nine.lead != -1 || split_nine.tail != 'e') return 133;

    // An unsigned field must not come back sign-extended and a signed one
    // must, which is the half of the assembly the masks alone do not decide.
    struct split_three_bytes split_three_byte;
    split_three_byte.lead = 'f';
    split_three_byte.tail = 'g';
    split_three_byte.value = 0x1ffffu;
    if (split_three_byte.value != 0x1ffffu || split_three_byte.lead != 'f' || split_three_byte.tail != 'g') return 134;
    struct split_six_bytes split_six;
    split_six.lead = 'h';
    split_six.tail = 'i';
    split_six.value = -1;
    if (split_six.value != -1 || split_six.lead != 'h' || split_six.tail != 'i') return 135;
    split_six.value = 0x7fffffffffffLL;
    if (split_six.value != 0x7fffffffffffLL || split_six.lead != 'h' || split_six.tail != 'i') return 136;

    // The same fields written by an aggregate initializer and by the folds
    // that build static bytes, which reach the pieces through code of their
    // own.
    struct split_neighbours split_local = {'a', 0x7fedcba987LL, 'b'};
    if (split_local.lead != 'a' || split_local.value != 0x7fedcba987LL || split_local.tail != 'b') return 137;
    if (split_unit_initialized.value != -0x123456789LL) return 138;
    if (split_union_initialized.value != -6) return 139;
    if (split_neighbours_initialized.lead != 'a' || split_neighbours_initialized.value != 0x7fedcba987LL ||
        split_neighbours_initialized.tail != 'b')
    {
        return 140;
    }
    if (split_offset_designated.lead != 0 || split_offset_designated.value != -2 || split_offset_designated.tail != 'c') return 141;
    if (split_three_pieces_initialized.lead != -1 || split_three_pieces_initialized.value != 0x123456789abcdLL ||
        split_three_pieces_initialized.tail != 'd')
    {
        return 142;
    }
    if (split_nine_bytes_initialized.lead != 0 || split_nine_bytes_initialized.value != 0xfedcba9876543210ULL ||
        split_nine_bytes_initialized.tail != 'e')
    {
        return 143;
    }

    // Objects the linker placed, and one automatic object.
    char automatic_aligned[3] __attribute__((aligned(64)));
    static char static_aligned[3] __attribute__((aligned(32)));
    if ((unsigned long long)(void *)leading_aligned % 64) return 22;
    if ((unsigned long long)(void *)specifier_aligned % 128) return 23;
    if ((unsigned long long)(void *)automatic_aligned % 64) return 24;
    if ((unsigned long long)(void *)static_aligned % 32) return 25;
    if (between_them != 1) return 26;

    automatic_aligned[0] = 1;
    if (automatic_aligned[0] != 1) return 27;

    // The two member-declarator attribute positions, read through addresses:
    // the aggregate sizes above match without the members being where the
    // platform compiler puts them.
    struct member_pointer_aligned pointer_aligned;
    if ((char *)&pointer_aligned.handler - (char *)&pointer_aligned != 32) return 30;
    struct list_declarator_aligned list_aligned;
    if ((char *)&list_aligned.first - (char *)&list_aligned != 0) return 31;
    if ((char *)&list_aligned.second - (char *)&list_aligned != 32) return 32;
    if ((char *)&list_aligned.tail - (char *)&list_aligned != 36) return 33;

    struct second_declarator_aligned second_aligned;
    if ((char *)&second_aligned.first - (char *)&second_aligned != 4) return 38;
    if ((char *)&second_aligned.second - (char *)&second_aligned != 32) return 39;
    struct first_declarator_aligned first_aligned;
    if ((char *)&first_aligned.first - (char *)&first_aligned != 32) return 40;
    if ((char *)&first_aligned.second - (char *)&first_aligned != 36) return 41;
    struct shared_specifier_aligned shared_aligned;
    if ((char *)&shared_aligned.first - (char *)&shared_aligned != 32) return 42;
    if ((char *)&shared_aligned.second - (char *)&shared_aligned != 64) return 43;
    struct shared_and_declarator_aligned shared_and_declarator;
    if ((char *)&shared_and_declarator.first - (char *)&shared_and_declarator != 64) return 44;
    if ((char *)&shared_and_declarator.second - (char *)&shared_and_declarator != 72) return 45;

    // Which declarator of the list carries the `packed` decides which members
    // move, and the offsets are what separate the four positions: the sizes
    // above agree for the three that pack one member.
    struct packed_first_declarator packed_first;
    if ((char *)&packed_first.first - (char *)&packed_first != 1) return 51;
    if ((char *)&packed_first.second - (char *)&packed_first != 8) return 52;
    struct packed_second_declarator packed_second;
    if ((char *)&packed_second.first - (char *)&packed_second != 4) return 53;
    if ((char *)&packed_second.second - (char *)&packed_second != 8) return 54;
    struct packed_list_declarator packed_list;
    if ((char *)&packed_list.first - (char *)&packed_list != 4) return 55;
    if ((char *)&packed_list.second - (char *)&packed_list != 8) return 56;
    struct packed_shared_specifier packed_shared;
    if ((char *)&packed_shared.first - (char *)&packed_shared != 1) return 57;
    if ((char *)&packed_shared.second - (char *)&packed_shared != 5) return 58;
    struct packed_member_pointer packed_pointer;
    if ((char *)&packed_pointer.handler - (char *)&packed_pointer != 1) return 59;
    if ((char *)&packed_pointer.tail - (char *)&packed_pointer != 12) return 60;

    // The values survive the unaligned placement the attribute asks for, so a
    // layout the compiler agrees with itself about is also one it can address.
    packed_first.first = -654321;
    packed_first.second = 0x11223344;
    if (packed_first.first != -654321 || packed_first.second != 0x11223344) return 61;
    packed_shared.first = 0x55667788;
    packed_shared.second = -1;
    if (packed_shared.first != 0x55667788 || packed_shared.second != -1) return 62;

    // The same two positions on an object declarator rather than a member one:
    // after a parenthesized declarator, at file scope just above and at block
    // scope here, and at the head of a declarator that is not the first of its
    // list.  Reading either list as part of the declarator dropped the whole
    // declaration, so the names below looked undeclared rather than
    // misaligned.  The padding around the two automatic objects gives the
    // frame something to place them after, so a satisfied boundary is the
    // attribute's doing.
    char local_pad[3];
    void (*local_pointer_aligned)(int) __attribute__((aligned(64))) = 0;
    char local_pad_two[5];
    int list_first = 1, __attribute__((aligned(32))) list_second = 2;
    local_pad[0] = 4;
    local_pad_two[0] = 5;
    if ((unsigned long long)(void *)&file_pointer_aligned % 64) return 63;
    if ((unsigned long long)(void *)&local_pointer_aligned % 64) return 64;
    if ((unsigned long long)(void *)&list_second % 32) return 65;
    if (local_pointer_aligned != 0 || list_first != 1 || list_second != 2) return 66;
    if (before_file_pointer != 2 || after_file_pointer != 3 || local_pad[0] != 4 || local_pad_two[0] != 5) return 67;

    // The ignored requests, read through addresses: a member the attribute
    // was allowed to lower would sit at offset 2 rather than 4.
    struct specifier_below_natural specifier_below;
    if ((char *)&specifier_below.value - (char *)&specifier_below != 4) return 72;
    struct declarator_below_natural declarator_below;
    if ((char *)&declarator_below.value - (char *)&declarator_below != 4) return 73;
    struct aggregate_below_natural aggregate_below;
    if ((char *)&aggregate_below.value - (char *)&aggregate_below != 4) return 74;
    struct member_below_and_above below_and_above;
    if ((char *)&below_and_above.pad - (char *)&below_and_above != 2) return 75;
    if ((unsigned long long)(void *)&object_below_natural % 2) return 76;
    if (object_below_natural != 7) return 77;

    // Where a bit-field's own attribute list sits decides where the members
    // after it land, and `tail` is the one address that separates the four
    // positions: the sizes above agree for all of them.
    struct packed_bits_declarator bits_declarator;
    if ((char *)&bits_declarator.tail - (char *)&bits_declarator != 5) return 82;
    struct packed_bits_list_declarator bits_list;
    if ((char *)&bits_list.tail - (char *)&bits_list != 5) return 83;
    struct packed_bits_first_declarator bits_first;
    if ((char *)&bits_first.tail - (char *)&bits_first != 7) return 84;
    struct packed_bits_shared_specifier bits_shared;
    if ((char *)&bits_shared.tail - (char *)&bits_shared != 5) return 85;

    // The widths have to fold to the ones that were written, which is what
    // the attribute's tokens stopped happening: a field read back through the
    // unit the packing chose returns the value stored in it.
    bits_declarator.a = -8;
    bits_declarator.b = -0x123456;
    bits_declarator.tail = 'w';
    if (bits_declarator.a != -8 || bits_declarator.b != -0x123456 || bits_declarator.tail != 'w') return 86;
    bits_first.a = 100;
    bits_first.b = 0x7fffff;
    bits_first.tail = 'x';
    if (bits_first.a != 100 || bits_first.b != 0x7fffff || bits_first.tail != 'x') return 87;

    // The typedef position, in both directions.  The size of the type is the
    // aliased one's untouched -- only the alignment moves -- so the sizes of
    // the aggregates below are what separate a request that was honoured from
    // one that was dropped.
    if (sizeof(raised_scalar) != 4 || _Alignof(raised_scalar) != 32) return 88;
    if (sizeof(lowered_scalar) != 4 || _Alignof(lowered_scalar) != 2) return 89;
    if (sizeof(raised_aggregate) != 1 || _Alignof(raised_aggregate) != 16) return 90;
    // The alias carries the request; the types it aliases do not.
    if (_Alignof(int) != 4 || _Alignof(struct one_byte) != 1) return 91;
    if (_Alignof(raised_again) != 32) return 92;

    if (sizeof(struct raised_typedef_member) != 64 || _Alignof(struct raised_typedef_member) != 32) return 93;
    if (sizeof(struct lowered_typedef_member) != 8 || _Alignof(struct lowered_typedef_member) != 2) return 94;
    if (sizeof(struct raised_aggregate_member) != 32 || _Alignof(struct raised_aggregate_member) != 16) return 95;

    struct raised_typedef_member raised_member;
    if ((char *)&raised_member.value - (char *)&raised_member != 32) return 96;
    struct lowered_typedef_member lowered_member;
    if ((char *)&lowered_member.value - (char *)&lowered_member != 2) return 97;
    if ((char *)&lowered_member.tail - (char *)&lowered_member != 6) return 98;
    struct raised_aggregate_member aggregate_member;
    if ((char *)&aggregate_member.value - (char *)&aggregate_member != 16) return 99;
    if ((char *)&aggregate_member.tail - (char *)&aggregate_member != 17) return 100;

    // Objects declared with the typedef, at file scope and at block scope,
    // where the alignment is the type's rather than any declarator's.  A
    // block-scope typedef is read by a parser of its own, and the neighbour
    // between the two automatic objects is what makes the boundary a claim
    // about the attribute rather than about where the frame happened to start.
    if ((unsigned long long)(void *)&file_raised_object % 32) return 101;
    if ((unsigned long long)(void *)&file_lowered_object % 2) return 102;
    if (between_typedef_objects != 7) return 103;

    typedef int block_raised __attribute__((aligned(64)));
    typedef int block_lowered __attribute__((aligned(1)));
    block_raised block_raised_object = 3;
    char between_block_objects = 8;
    block_lowered block_lowered_object = 4;
    if (_Alignof(block_raised) != 64 || _Alignof(block_lowered) != 1) return 104;
    if ((unsigned long long)(void *)&block_raised_object % 64) return 105;
    if (block_raised_object != 3 || block_lowered_object != 4 || between_block_objects != 8) return 106;

    // Arrays of the shapes above.  The stride is the element size, so the
    // second element of each of these lands exactly where its own alignment
    // requires it -- which is the property an over-aligned element cannot have
    // and is refused for.
    if (sizeof(struct padded_element) != 16 || _Alignof(struct padded_element) != 16) return 107;
    if (sizeof(padded_element_pair) != 32 || _Alignof(padded_element_pair) != 16) return 108;
    if (sizeof(padded_alias_array) != 32 || _Alignof(padded_alias) != 16) return 109;
    if (sizeof(lowered_scalar_pair) != 8 || _Alignof(lowered_scalar_pair) != 2) return 110;
    if (sizeof(exact_scalar_pair) != 8 || _Alignof(exact_scalar_pair) != 4) return 111;
    if ((char *)&padded_element_array[1] - (char *)&padded_element_array[0] != 16) return 112;
    if ((char *)&lowered_scalar_array[1] - (char *)&lowered_scalar_array[0] != 4) return 113;
    if ((char *)&padded_alias_array[1] - (char *)&padded_alias_array[0] != 16) return 114;
    if ((char *)&exact_scalar_array[1] - (char *)&exact_scalar_array[0] != 4) return 115;
    if ((unsigned long long)(void *)padded_element_array % 16) return 116;
    if ((unsigned long long)(void *)lowered_scalar_array % 2) return 117;

    // Whose declarator of a list the attribute belongs to, at file scope and
    // at block scope.  The block-scope path scans each declarator's own
    // segment already and the file-scope one did not (#701); the two have to
    // agree, so both are asked the same question here.
    if ((unsigned long long)(void *)&object_list_leader % 64) return 118;
    if (!((unsigned long long)(void *)&object_list_second % 64) + !((unsigned long long)(void *)&object_list_third % 64) +
            !((unsigned long long)(void *)&object_list_fourth % 64) ==
        3)
        return 119;
    if ((unsigned long long)(void *)&shared_list_first % 64 || (unsigned long long)(void *)&shared_list_second % 64) return 120;
    if ((unsigned long long)(void *)&middle_list_first % 64 || (unsigned long long)(void *)&middle_list_second % 64) return 121;
    if ((unsigned long long)(void *)&alignas_list_first % 64 || (unsigned long long)(void *)&alignas_list_second % 64) return 122;
    if ((unsigned long long)(void *)&head_list_second % 64 || !((unsigned long long)(void *)&head_list_first % 64)) return 123;
    if ((unsigned long long)(void *)&tail_list_second % 64 || !((unsigned long long)(void *)&tail_list_first % 64)) return 124;

    int block_list_leader __attribute__((aligned(64))) = 1, block_list_second = 2, block_list_third = 3, block_list_fourth = 4;
    if ((unsigned long long)(void *)&block_list_leader % 64) return 125;
    if (!((unsigned long long)(void *)&block_list_second % 64) + !((unsigned long long)(void *)&block_list_third % 64) +
            !((unsigned long long)(void *)&block_list_fourth % 64) ==
        3)
        return 126;
    if (block_list_leader != 1 || block_list_second != 2 || block_list_third != 3 || block_list_fourth != 4) return 127;

    // The typedef spelling of the list, which is exact: the alias carries the
    // request and the name beside it keeps the aliased type's own alignment.
    if (_Alignof(typedef_list_raised) != 64 || _Alignof(typedef_list_plain) != 4) return 128;
    if (_Alignof(typedef_list_plain_first) != 4 || _Alignof(typedef_list_raised_second) != 64) return 129;
    typedef int block_typedef_list_raised __attribute__((aligned(64))), block_typedef_list_plain;
    if (_Alignof(block_typedef_list_raised) != 64 || _Alignof(block_typedef_list_plain) != 4) return 130;

    // A pointer to an array measures, indexes and moves by the whole array.
    if (sizeof(*typedef_pointer_to_array) != 12 || sizeof(typedef_pointer_to_array) != sizeof(void *)) return 131;
    if (sizeof(*tag_pointer_to_array) != 32 || _Alignof(struct padded_element[2]) != 16) return 132;
    if ((char *)typedef_pointer_to_array - (char *)file_rows != 12) return 133;
    if ((char *)tag_pointer_to_array - (char *)padded_pairs != 32) return 134;
    // The store goes through the dereference spelling, which used to be
    // dropped whole (#719): `*p` materialized a copy of the array and the
    // subscript indexed the copy, so the assignment reached a temporary and
    // the object kept its old value.  All three spellings of the same store
    // reach the object, and the subscript spelling that stood in for them
    // still does.
    (*typedef_pointer_to_array)[2] = 9;
    if (file_rows[1][2] != 9 || (*typedef_pointer_to_array)[2] != 9) return 135;
    *(*typedef_pointer_to_array + 1) = 8;
    if (file_rows[1][1] != 8 || typedef_pointer_to_array[0][1] != 8) return 144;
    (*(typedef_pointer_to_array + 0))[0] = 7;
    if (file_rows[1][0] != 7) return 145;
    typedef_pointer_to_array[0][2] = 6;
    if (file_rows[1][2] != 6 || (*typedef_pointer_to_array)[2] != 6) return 146;
    // The same store through a pointer to an array of an over-aligned
    // aggregate, whose element the dereference would have copied sixteen
    // bytes at a time.
    (*tag_pointer_to_array)[1].byte = 5;
    if (padded_pairs[1][1].byte != 5 || (*tag_pointer_to_array)[1].byte != 5) return 147;

    // The array type name in an expression: the size and alignment of one the
    // compiler never places, and the compound literal that does place one.
    if (sizeof(lowered_scalar[2]) != 8 || _Alignof(lowered_scalar[2]) != 2) return 136;
    if (sizeof(struct padded_element[2]) != 32) return 137;
    if (((lowered_scalar[2]){3, 4})[1] != 4) return 138;

    // The specifier position, which is shared by the whole list rather than
    // owned by one declarator, at both scopes for the same reason the
    // declarator position is asked at both.
    if (_Alignof(typedef_specifier_first) != 16 || _Alignof(typedef_specifier_second) != 16) return 139;
    if (_Alignof(typedef_specifier_shared) != 8 || _Alignof(typedef_specifier_raised) != 64) return 140;
    typedef int __attribute__((aligned(16))) block_typedef_specifier_first, block_typedef_specifier_second;
    if (_Alignof(block_typedef_specifier_first) != 16 || _Alignof(block_typedef_specifier_second) != 16) return 141;

    // A qualifier written on an aligned alias, which keeps the request in both
    // reference compilers.  The size is still the aliased type's, so it is the
    // aggregates below that separate a request that survived the qualifier
    // from one that was dropped.
    if (sizeof(const raised_scalar) != 4 || _Alignof(const raised_scalar) != 32) return 156;
    if (_Alignof(volatile raised_scalar) != 32 || _Alignof(const volatile raised_scalar) != 32) return 157;
    if (_Alignof(const lowered_scalar) != 2 || _Alignof(volatile lowered_scalar) != 2) return 158;
    if (_Alignof(const_raised_alias) != 32 || _Alignof(const_volatile_raised_alias) != 32) return 159;
    // And the type the qualified alias strips to still keeps its own.
    if (_Alignof(const int) != 4) return 160;

    if (sizeof(struct const_typedef_member) != 64 || _Alignof(struct const_typedef_member) != 32) return 161;
    if (sizeof(struct volatile_typedef_member) != 64 || _Alignof(struct volatile_typedef_member) != 32) return 162;
    if (sizeof(struct const_lowered_typedef_member) != 8 || _Alignof(struct const_lowered_typedef_member) != 2) return 163;
    if (sizeof(union const_typedef_union) != 32 || _Alignof(union const_typedef_union) != 32) return 164;

    struct const_typedef_member const_member = {0};
    if ((char *)&const_member.value - (char *)&const_member != 32) return 165;
    struct volatile_typedef_member volatile_member;
    if ((char *)&volatile_member.value - (char *)&volatile_member != 32) return 166;
    struct const_lowered_typedef_member const_lowered_member = {0};
    if ((char *)&const_lowered_member.value - (char *)&const_lowered_member != 2) return 167;
    if ((char *)&const_lowered_member.tail - (char *)&const_lowered_member != 6) return 168;

    if ((unsigned long long)(void *)&file_const_raised_object % 32) return 169;
    if ((unsigned long long)(void *)&file_volatile_lowered_object % 2) return 170;
    if (between_const_typedef_objects != 11) return 171;

    typedef int block_qualified __attribute__((aligned(64)));
    const block_qualified block_const_object = 5;
    char between_block_qualified = 9;
    volatile block_qualified block_volatile_object = 6;
    if (_Alignof(const block_qualified) != 64 || _Alignof(volatile block_qualified) != 64) return 172;
    if ((unsigned long long)(void *)&block_const_object % 64) return 173;
    if ((unsigned long long)(void *)&block_volatile_object % 64) return 174;
    if (block_const_object != 5 || block_volatile_object != 6 || between_block_qualified != 9) return 175;

    if (sizeof(const_lowered_pair) != 8 || _Alignof(const_lowered_pair) != 2) return 176;
    if (sizeof(const_exact_pair) != 8 || _Alignof(const_exact_pair) != 4) return 177;
    if ((char *)&const_lowered_array[1] - (char *)&const_lowered_array[0] != 4) return 178;
    if ((char *)&const_exact_array[1] - (char *)&const_exact_array[0] != 4) return 179;
    if ((unsigned long long)(void *)const_lowered_array % 2) return 180;

    // `_Atomic` written in a type name and `_Atomic` reached through a typedef
    // of it are the same type, so every pair below has to answer once (#726).
    if (sizeof(_Atomic raised_scalar) != 4 || _Alignof(_Atomic raised_scalar) != 4) return 181;
    if (sizeof(atomic_raised_alias) != 4 || _Alignof(atomic_raised_alias) != 4) return 182;
    if (_Alignof(_Atomic raised_scalar) != _Alignof(atomic_raised_alias)) return 183;
    if (_Alignof(_Atomic(raised_scalar)) != 4 || sizeof(_Atomic(raised_scalar)) != 4) return 208;
    if (_Alignof(atomic_operator_alias) != 4 || sizeof(atomic_operator_alias) != 4) return 209;
    if (_Alignof(const _Atomic raised_scalar) != 4 || _Alignof(_Atomic const raised_scalar) != 4) return 184;
    if (_Alignof(volatile _Atomic raised_scalar) != 4) return 185;
    if (_Alignof(_Atomic lowered_scalar) != 4 || _Alignof(atomic_lowered_alias) != 4) return 186;
    if (sizeof(_Atomic raised_aggregate) != 1 || _Alignof(_Atomic raised_aggregate) != 1) return 187;
    // And the alias itself still has the request every other spelling reads.
    if (_Alignof(raised_scalar) != 32 || _Alignof(lowered_scalar) != 2) return 188;
    if (_Alignof(atomic_raised_atomic) != 32 || _Alignof(_Atomic atomic_raised_atomic) != 32) return 189;

    if (sizeof(struct atomic_typedef_member) != 8 || _Alignof(struct atomic_typedef_member) != 4) return 190;
    if (sizeof(union atomic_typedef_union) != 4 || _Alignof(union atomic_typedef_union) != 4) return 191;
    if (sizeof(struct atomic_lowered_typedef_member) != 12 || _Alignof(struct atomic_lowered_typedef_member) != 4) return 192;
    if (sizeof(struct atomic_aggregate_member) != 3 || _Alignof(struct atomic_aggregate_member) != 1) return 193;

    struct atomic_typedef_member atomic_member = {0};
    if ((char *)&atomic_member.value - (char *)&atomic_member != 4) return 194;
    struct atomic_lowered_typedef_member atomic_lowered_member = {0};
    if ((char *)&atomic_lowered_member.value - (char *)&atomic_lowered_member != 4) return 195;
    if ((char *)&atomic_lowered_member.tail - (char *)&atomic_lowered_member != 8) return 196;
    struct atomic_aggregate_member atomic_aggregate = {0};
    if ((char *)&atomic_aggregate.value - (char *)&atomic_aggregate != 1) return 197;
    if ((char *)&atomic_aggregate.tail - (char *)&atomic_aggregate != 2) return 198;

    if ((unsigned long long)(void *)&file_atomic_raised_object % 4) return 199;
    if (between_atomic_typedef_objects != 13) return 200;

    typedef int block_atomic __attribute__((aligned(64)));
    _Atomic block_atomic block_atomic_object = 5;
    char between_block_atomic = 12;
    if (_Alignof(_Atomic block_atomic) != 4 || sizeof(_Atomic block_atomic) != 4) return 201;
    if (_Alignof(block_atomic) != 64) return 202;
    if (block_atomic_object != 5 || between_block_atomic != 12) return 203;

    if (sizeof(atomic_raised_pair) != 8 || _Alignof(atomic_raised_pair) != 4) return 204;
    if (sizeof(_Atomic raised_scalar[2]) != 8) return 205;
    if ((char *)&atomic_raised_array[1] - (char *)&atomic_raised_array[0] != 4) return 206;
    if ((unsigned long long)(void *)atomic_raised_array % 4) return 207;

    // The promotion, folded by the lowering engine this time.  Every number
    // here is stated for the parse engine above; the two must agree or a
    // folded `sizeof` contradicts the object it measures.
    if (sizeof(_Atomic atomic_three) != 4 || _Alignof(_Atomic atomic_three) != 4) return 210;
    if (sizeof(atomic_three_alias) != 4 || _Alignof(atomic_three_alias) != 4) return 211;
    if (sizeof(atomic_three_operator_alias) != 4 || _Alignof(atomic_three_operator_alias) != 4) return 212;
    if (sizeof(_Atomic(atomic_three)) != 4 || _Alignof(_Atomic(atomic_three)) != 4) return 213;
    if (sizeof(atomic_union_alias) != 4 || _Alignof(atomic_union_alias) != 4) return 214;
    if (sizeof(atomic_five_alias) != 8 || _Alignof(atomic_five_alias) != 8) return 215;
    if (sizeof(atomic_twelve_alias) != 16 || _Alignof(atomic_twelve_alias) != 16) return 216;
    if (sizeof(atomic_sixteen_alias) != 16 || _Alignof(atomic_sixteen_alias) != 16) return 217;
    if (sizeof(atomic_seventeen_alias) != 17 || _Alignof(atomic_seventeen_alias) != 1) return 218;
    if (sizeof(atomic_empty_alias) != (sizeof(atomic_empty) ? sizeof(atomic_empty) : 1)) return 219;
    if (_Alignof(atomic_empty_alias) != (sizeof(atomic_empty) ? sizeof(atomic_empty) : 1)) return 245;
    if (sizeof(const atomic_three_alias) != 4 || _Alignof(const atomic_three_alias) != 4) return 220;
    if (sizeof(volatile atomic_three_alias) != 4 || _Alignof(volatile atomic_three_alias) != 4) return 221;
    if (sizeof(atomic_three_raised) != 4 || _Alignof(atomic_three_raised) != 32) return 222;
    if (sizeof(atomic_three_lowered) != 4 || _Alignof(atomic_three_lowered) != 1) return 223;
    if (sizeof(_Atomic _Complex float) != 8 || _Alignof(_Atomic _Complex float) != 8) return 224;
    if (sizeof(_Atomic _Complex double) != 16 || _Alignof(_Atomic _Complex double) != 16) return 225;
    if (sizeof(_Atomic _Complex long double) != sizeof(_Complex long double)) return 226;

    // The padding is where the object is, not only what `sizeof` says: the
    // member sits past the whole promoted size and the array element tiles at
    // it.
    if (sizeof(struct atomic_promoted_member) != 12 || _Alignof(struct atomic_promoted_member) != 4) return 227;
    struct atomic_promoted_member promoted_member = {0};
    if ((char *)&promoted_member.value - (char *)&promoted_member != 4) return 228;
    if ((char *)&promoted_member.tail - (char *)&promoted_member != 8) return 229;
    if (sizeof(struct atomic_unpromoted_member) != 19 || _Alignof(struct atomic_unpromoted_member) != 1) return 230;
    struct atomic_unpromoted_member unpromoted_member = {0};
    if ((char *)&unpromoted_member.value - (char *)&unpromoted_member != 1) return 231;
    if ((char *)&unpromoted_member.tail - (char *)&unpromoted_member != 18) return 232;
    if (sizeof(struct atomic_promoted_array_member) != 16 || _Alignof(struct atomic_promoted_array_member) != 4) return 233;
    struct atomic_promoted_array_member promoted_array_member = {0};
    if ((char *)&promoted_array_member.values - (char *)&promoted_array_member != 4) return 234;
    if ((char *)&promoted_array_member.tail - (char *)&promoted_array_member != 12) return 235;
    if (sizeof(atomic_three_pair) != 8 || _Alignof(atomic_three_pair) != 4) return 236;
    if ((char *)&file_atomic_three_array[1] - (char *)&file_atomic_three_array[0] != 4) return 237;
    if ((unsigned long long)(void *)file_atomic_three_array % 4) return 238;
    if ((unsigned long long)(void *)&file_atomic_three_object % 4) return 239;
    if (between_atomic_promoted_objects != 17) return 240;

    // The block-scope parser is a second parser and reads the same rule.
    typedef struct
    {
        char a, b, c;
    } block_three;
    typedef _Atomic block_three block_atomic_three;
    block_atomic_three block_atomic_three_object;
    char between_block_atomic_three = 18;
    if (sizeof(block_atomic_three) != 4 || _Alignof(block_atomic_three) != 4) return 241;
    if (sizeof(_Atomic block_three) != 4 || _Alignof(_Atomic block_three) != 4) return 242;
    if ((unsigned long long)(void *)&block_atomic_three_object % 4) return 243;
    if (between_block_atomic_three != 18) return 244;

    // `_Atomic` in front of the tag keyword, folded by the lowering engine.
    // Every number here is stated for the parse engine above, and the object
    // checks are what say the promotion reached the object and not only the
    // fold.
    if (sizeof(_Atomic struct atomic_tag_three) != 4 || _Alignof(_Atomic struct atomic_tag_three) != 4) return 246;
    if (sizeof(_Atomic struct atomic_tag_three) != sizeof(struct atomic_tag_three _Atomic)) return 247;
    if (sizeof(_Atomic struct atomic_tag_three) != sizeof(_Atomic(struct atomic_tag_three))) return 248;
    if (sizeof(atomic_tag_leading_alias) != 4 || _Alignof(atomic_tag_leading_alias) != 4) return 249;
    if (sizeof(_Atomic union atomic_tag_union) != 4 || _Alignof(_Atomic union atomic_tag_union) != 4) return 250;
    if (sizeof(_Atomic enum atomic_tag_enum) != 4 || _Alignof(_Atomic enum atomic_tag_enum) != 4) return 251;
    if (sizeof(const struct atomic_tag_three) != 3 || sizeof(volatile struct atomic_tag_three) != 3) return 252;
    if (sizeof(struct atomic_leading_member) != 12 || _Alignof(struct atomic_leading_member) != 4) return 253;
    struct atomic_leading_member leading_member = {0};
    if ((char *)&leading_member.value - (char *)&leading_member != 4) return 254;
    if ((char *)&leading_member.tail - (char *)&leading_member != 8) return 255;
    if ((unsigned long long)(void *)&file_atomic_leading_object % 4) return 256;
    if (between_atomic_leading_objects != 19) return 257;

    // The block-scope parser reads the leading run through the same machine.
    _Atomic struct atomic_tag_three block_leading_object;
    char between_block_leading = 20;
    if (sizeof(block_leading_object) != 4 || _Alignof(block_leading_object) != 4) return 258;
    if ((unsigned long long)(void *)&block_leading_object % 4) return 259;
    if (between_block_leading != 20) return 260;
    return 0;
}
