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
// a declarator that is not the first of its list has a fourth.

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
// hold whether the attribute is read as this declarator's or -- as buster
// still reads it, #680 -- as the whole segment's.
struct list_declarator_aligned
{
    int first, __attribute__((aligned(32))) second;
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
    return 0;
}
