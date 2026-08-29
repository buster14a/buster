// The callee half of the packed/aligned layout pair.  It answers the sizes and
// offsets its own compiler computed and passes the packed aggregates by value,
// which is where a layout disagreement between the two compilers surfaces.
// See basic_c_packed_layout_caller.c for what each answer pins down.
#include "basic_c_packed_layout_shapes.h"

char packed_layout_aligned_object[3] __attribute__((aligned(64)));

unsigned long packed_layout_record_size(void)
{
    return sizeof(struct packed_record);
}

unsigned long packed_layout_record_value_offset(void)
{
    struct packed_record record;
    return (unsigned long)((char *)&record.value - (char *)&record);
}

unsigned long packed_layout_aligned_record_size(void)
{
    return sizeof(struct packed_aligned_record);
}

unsigned long packed_layout_aligned_record_alignment(void)
{
    return _Alignof(struct packed_aligned_record);
}

unsigned long packed_layout_bit_record_size(void)
{
    return sizeof(struct packed_bit_record);
}

struct packed_record packed_layout_make_record(char tag, int value, short trailer)
{
    struct packed_record record;
    record.tag = tag;
    record.value = value;
    record.trailer = trailer;
    return record;
}

int packed_layout_record_middle(struct packed_record record)
{
    return record.value;
}

struct packed_aligned_record packed_layout_make_aligned_record(char tag, int value)
{
    struct packed_aligned_record record;
    record.tag = tag;
    record.value = value;
    return record;
}

int packed_layout_bit_pair(struct packed_bit_record record)
{
    return (int)record.low * 100 + (int)record.high;
}

void packed_layout_fill_aligned_object(char first)
{
    packed_layout_aligned_object[0] = first;
    packed_layout_aligned_object[1] = (char)(first + 1);
    packed_layout_aligned_object[2] = (char)(first + 2);
}
