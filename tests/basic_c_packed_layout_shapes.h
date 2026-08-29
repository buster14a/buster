// The shapes the packed/aligned layout pair passes across the compiler
// boundary.  Both halves include this file, and each half may be built by a
// different compiler, so a disagreement about where a member sits or how
// large an object is becomes a wrong answer rather than a wrong-looking
// disassembly.
#ifndef BASIC_C_PACKED_LAYOUT_SHAPES_H
#define BASIC_C_PACKED_LAYOUT_SHAPES_H

struct __attribute__((packed)) packed_record
{
    char tag;
    int value;
    short trailer;
};

struct __attribute__((packed, aligned(16))) packed_aligned_record
{
    char tag;
    int value;
};

struct __attribute__((packed)) packed_bit_record
{
    unsigned char low : 3;
    unsigned char high : 5;
    char tail;
};

// A packed union sizes to the bits its widest member occupies rather than to
// that member's declared type, so this is one byte and `tail` sits at offset
// one: a compiler that sized it from the `int` puts `tail` three bytes further
// on and reads the wrong byte out of a record the other half built (#706).
union __attribute__((packed)) packed_bit_union
{
    char lead;
    int value : 5;
};

struct __attribute__((packed)) packed_bit_union_record
{
    union packed_bit_union bits;
    char tail;
};

// A member asking for less alignment than its type already has, which GNU
// `aligned` ignores rather than applies: a compiler that lowered `value` to
// two bytes puts it at offset 2 and makes this a 6-byte record, which is what
// the pair catches and a single translation unit cannot (#689).
struct below_natural_record
{
    char tag;
    __attribute__((aligned(2))) int value;
};

// The same request written on a *typedef* rather than on the member is the
// case that is not ignored: there `aligned` sets the alignment of the type, so
// it lowers as well as raises.  This is what a header writes for a cache-line-
// or SIMD-aligned scalar, and the records around them are what cross the
// compiler boundary -- a half that drops the request puts `value` at offset 4
// in both, which the other half reads as a different member.
typedef int packed_layout_raised __attribute__((aligned(16)));
typedef int packed_layout_lowered __attribute__((aligned(2)));

struct packed_layout_raised_record
{
    char tag;
    packed_layout_raised value;
};

struct packed_layout_lowered_record
{
    char tag;
    packed_layout_lowered value;
    char trailer;
};

// The declarator-position attribute, on an object both halves address.
extern char packed_layout_aligned_object[3] __attribute__((aligned(64)));

// The same request on an object declarator, which both reference compilers
// accept and place at the type's own alignment.
extern int packed_layout_below_natural_object __attribute__((aligned(2)));

// And the typedef position, on an object whose alignment is its type's.
extern packed_layout_raised packed_layout_raised_object;

extern unsigned long long packed_layout_record_size(void);
extern unsigned long long packed_layout_record_value_offset(void);
extern unsigned long long packed_layout_aligned_record_size(void);
extern unsigned long long packed_layout_aligned_record_alignment(void);
extern unsigned long long packed_layout_bit_record_size(void);
extern unsigned long long packed_layout_bit_union_size(void);
extern unsigned long long packed_layout_bit_union_tail_offset(void);
extern struct packed_bit_union_record packed_layout_make_bit_union_record(int value, char tail);
extern int packed_layout_bit_union_value(struct packed_bit_union_record record);
extern unsigned long long packed_layout_below_natural_size(void);
extern unsigned long long packed_layout_below_natural_offset(void);
extern struct below_natural_record packed_layout_make_below_natural(char tag, int value);
extern struct packed_record packed_layout_make_record(char tag, int value, short trailer);
extern int packed_layout_record_middle(struct packed_record record);
extern struct packed_aligned_record packed_layout_make_aligned_record(char tag, int value);
extern int packed_layout_bit_pair(struct packed_bit_record record);
extern void packed_layout_fill_aligned_object(char first);
extern unsigned long long packed_layout_raised_record_size(void);
extern unsigned long long packed_layout_raised_record_alignment(void);
extern unsigned long long packed_layout_raised_record_value_offset(void);
extern unsigned long long packed_layout_lowered_record_size(void);
extern unsigned long long packed_layout_lowered_record_value_offset(void);
extern struct packed_layout_raised_record packed_layout_make_raised_record(char tag, int value);
extern int packed_layout_lowered_middle(struct packed_layout_lowered_record record);
extern void packed_layout_fill_raised_object(int value);

#endif
