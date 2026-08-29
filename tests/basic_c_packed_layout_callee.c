// The callee half of the packed/aligned layout pair.  It answers the sizes and
// offsets its own compiler computed and passes the packed aggregates by value,
// which is where a layout disagreement between the two compilers surfaces.
// See basic_c_packed_layout_caller.c for what each answer pins down.
#include "basic_c_packed_layout_shapes.h"

char packed_layout_aligned_object[3] __attribute__((aligned(64)));
packed_layout_raised packed_layout_raised_object;

int packed_layout_below_natural_object __attribute__((aligned(2))) = 31337;

unsigned long long packed_layout_record_size(void)
{
    return sizeof(struct packed_record);
}

unsigned long long packed_layout_record_value_offset(void)
{
    struct packed_record record;
    return (unsigned long long)((char *)&record.value - (char *)&record);
}

unsigned long long packed_layout_aligned_record_size(void)
{
    return sizeof(struct packed_aligned_record);
}

unsigned long long packed_layout_aligned_record_alignment(void)
{
    return _Alignof(struct packed_aligned_record);
}

unsigned long long packed_layout_bit_record_size(void)
{
    return sizeof(struct packed_bit_record);
}

unsigned long long packed_layout_bit_union_size(void)
{
    return sizeof(union packed_bit_union);
}

unsigned long long packed_layout_bit_union_tail_offset(void)
{
    struct packed_bit_union_record record;
    return (unsigned long long)((char *)&record.tail - (char *)&record);
}

struct packed_bit_union_record packed_layout_make_bit_union_record(int value, char tail)
{
    struct packed_bit_union_record record;
    record.bits.value = value;
    record.tail = tail;
    return record;
}

int packed_layout_bit_union_value(struct packed_bit_union_record record)
{
    return record.bits.value * 100 + record.tail;
}

unsigned long long packed_layout_below_natural_size(void)
{
    return sizeof(struct below_natural_record);
}

unsigned long long packed_layout_below_natural_offset(void)
{
    struct below_natural_record record;
    return (unsigned long long)((char *)&record.value - (char *)&record);
}

struct below_natural_record packed_layout_make_below_natural(char tag, int value)
{
    struct below_natural_record record;
    record.tag = tag;
    record.value = value;
    return record;
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

unsigned long long packed_layout_raised_record_size(void)
{
    return sizeof(struct packed_layout_raised_record);
}

unsigned long long packed_layout_raised_record_alignment(void)
{
    return _Alignof(struct packed_layout_raised_record);
}

unsigned long long packed_layout_raised_record_value_offset(void)
{
    struct packed_layout_raised_record record;
    return (unsigned long long)((char *)&record.value - (char *)&record);
}

unsigned long long packed_layout_lowered_record_size(void)
{
    return sizeof(struct packed_layout_lowered_record);
}

unsigned long long packed_layout_lowered_record_value_offset(void)
{
    struct packed_layout_lowered_record record;
    return (unsigned long long)((char *)&record.value - (char *)&record);
}

struct packed_layout_raised_record packed_layout_make_raised_record(char tag, int value)
{
    struct packed_layout_raised_record record;
    record.tag = tag;
    record.value = value;
    return record;
}

int packed_layout_lowered_middle(struct packed_layout_lowered_record record)
{
    return record.value;
}

void packed_layout_fill_raised_object(int value)
{
    packed_layout_raised_object = value;
}
