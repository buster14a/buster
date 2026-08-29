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

// The declarator-position attribute, on an object both halves address.
extern char packed_layout_aligned_object[3] __attribute__((aligned(64)));

extern unsigned long packed_layout_record_size(void);
extern unsigned long packed_layout_record_value_offset(void);
extern unsigned long packed_layout_aligned_record_size(void);
extern unsigned long packed_layout_aligned_record_alignment(void);
extern unsigned long packed_layout_bit_record_size(void);
extern struct packed_record packed_layout_make_record(char tag, int value, short trailer);
extern int packed_layout_record_middle(struct packed_record record);
extern struct packed_aligned_record packed_layout_make_aligned_record(char tag, int value);
extern int packed_layout_bit_pair(struct packed_bit_record record);
extern void packed_layout_fill_aligned_object(char first);

#endif
