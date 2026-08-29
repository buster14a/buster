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

// A bit-field whose bits fit no power-of-two storage unit that lies inside the
// aggregate: five bytes hold no five-byte integer, so the forty bits are read
// and written as several ordinary accesses.  A half that refused the
// declaration cannot build this at all, and one that chose a different span
// reads a neighbouring member's byte as part of the value (#709).  It crosses
// the boundary by address *and* by value: the by-address form is the layout
// question on its own, so a change to how such a record is classified cannot
// silently take the layout with it.
struct __attribute__((packed)) packed_split_record
{
    char lead;
    long long value : 40;
    char tail;
};

// How an aggregate holding a bit-field is *passed*, which is a question of its
// own (#721).  System V classifies the eightbytes a bit-field's bits fall in
// and never asks its declared type where it sits, so these five bytes are one
// INTEGER eightbyte and the record rides `rax` and the positional
// general-purpose register.  A half that asked the declared type where it sits
// -- `int value : 20` is not four bytes at offset one -- classified the whole
// record MEMORY and returned it through a hidden pointer, so the other half
// read an uninitialized buffer.
struct __attribute__((packed)) packed_bit_pass_record
{
    char lead;
    int value : 20;
    char tail;
};

// The same question with the bits straddling the eightbyte boundary: `value`
// occupies bits 56 through 75, so both eightbytes are INTEGER and the record
// rides two registers.  Only merging the eightbytes the bits fall in answers
// this one; a rule written in terms of the field's declared type cannot.
struct __attribute__((packed)) packed_bit_cross_record
{
    char lead[7];
    int value : 20;
    char tail;
};

// An *unnamed* bit-field is padding for the classification and contributes no
// class at all, which is observable beside a float: this record comes back in
// `xmm0` where the named spelling below comes back in `rax`, because only the
// named field merges INTEGER into the eightbyte the float already claimed.
struct packed_bit_named_record
{
    float lead;
    int value : 20;
};

struct packed_bit_padded_record
{
    float lead;
    int : 20;
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
extern unsigned long long packed_layout_split_size(void);
extern unsigned long long packed_layout_split_tail_offset(void);
extern void packed_layout_write_split(struct packed_split_record* record, long long value);
extern long long packed_layout_read_split(const struct packed_split_record* record);
extern struct packed_split_record packed_layout_make_split(long long value);
extern long long packed_layout_split_sum(struct packed_split_record record);
extern struct packed_bit_pass_record packed_layout_make_bit_pass(char lead, int value, char tail);
extern long long packed_layout_bit_pass_sum(struct packed_bit_pass_record record);
extern struct packed_bit_cross_record packed_layout_make_bit_cross(int value);
extern long long packed_layout_bit_cross_sum(struct packed_bit_cross_record record);
extern struct packed_bit_named_record packed_layout_make_bit_named(float lead, int value);
extern long long packed_layout_bit_named_sum(struct packed_bit_named_record record);
extern struct packed_bit_padded_record packed_layout_make_bit_padded(float lead);
extern float packed_layout_bit_padded_lead(struct packed_bit_padded_record record);
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
