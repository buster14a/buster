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
// and at block scope, each parsed by code of its own.

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

int main(void)
{
    if (sizeof(struct leading) != 5 || _Alignof(struct leading) != 1) return 1;
    if (sizeof(struct trailing) != 5 || _Alignof(struct trailing) != 1) return 2;
    if (sizeof(struct member_packed) != 5 || _Alignof(struct member_packed) != 1) return 3;
    if (sizeof(struct packed_then_aligned) != 8 || _Alignof(struct packed_then_aligned) != 8) return 4;
    if (sizeof(union packed_union) != 4 || _Alignof(union packed_union) != 1) return 5;
    if (sizeof(struct nested) != 9 || _Alignof(struct nested) != 1) return 6;
    if (sizeof(struct packed_bits) != 2) return 7;
    if (sizeof(struct offset_bits) != 4) return 8;
    if (sizeof(struct zero_width_bits) != 5) return 9;
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
    // attribute's doing.  Only the attributed declarator's own boundary is
    // checked: whose alignment an object declarator's list raises is the
    // question #680 answered for a member, and these numbers hold under
    // either answer.
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
    return 0;
}
