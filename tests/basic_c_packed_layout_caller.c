// The caller half of the packed/aligned layout pair.  Linked against a callee
// the other compiler built, so every check here is a question about the
// platform's object representation rather than about Buster's internal
// consistency: a single translation unit that ignores `packed` agrees with
// itself and passes.  Verified by reverting the attribute plumbing -- the
// single-file fixture and the Buster/Buster link still pass while both mixed
// links fail.
#include "basic_c_packed_layout_shapes.h"

int main(void)
{
    if (sizeof(struct packed_record) != 7) return 1;
    if (packed_layout_record_size() != sizeof(struct packed_record)) return 2;
    if (packed_layout_record_value_offset() != 1) return 3;
    if (packed_layout_aligned_record_size() != 16) return 4;
    if (packed_layout_aligned_record_alignment() != 16) return 5;
    if (packed_layout_bit_record_size() != 2) return 6;

    // The callee builds a packed aggregate and returns it by value; a caller
    // that reads it at different offsets sees different members.
    struct packed_record record = packed_layout_make_record('r', -987654321, 4242);
    if (record.tag != 'r' || record.value != -987654321 || record.trailer != 4242) return 7;

    // And the other direction: the caller builds one and the callee reads it.
    struct packed_record built;
    built.tag = 's';
    built.value = 0x0badf00d;
    built.trailer = -7;
    if (packed_layout_record_middle(built) != 0x0badf00d) return 8;

    struct packed_aligned_record aligned = packed_layout_make_aligned_record('a', 24680);
    if (aligned.tag != 'a' || aligned.value != 24680) return 9;
    if ((unsigned long long)(void *)&aligned % 16) return 10;

    struct packed_bit_record bits;
    bits.low = 6;
    bits.high = 19;
    bits.tail = 'b';
    if (packed_layout_bit_pair(bits) != 619) return 11;

    // The packed union, in both halves and across a value passed between them:
    // the size is where the callee put `tail`, and a caller reading the record
    // at the other size takes the wrong byte for it.
    if (sizeof(union packed_bit_union) != 1) return 19;
    if (packed_layout_bit_union_size() != sizeof(union packed_bit_union)) return 20;
    if (packed_layout_bit_union_tail_offset() != 1) return 21;
    struct packed_bit_union_record union_record = packed_layout_make_bit_union_record(-6, 'u');
    if (union_record.bits.value != -6 || union_record.tail != 'u') return 22;
    struct packed_bit_union_record built_union;
    built_union.bits.value = 7;
    built_union.tail = 3;
    if (packed_layout_bit_union_value(built_union) != 703) return 23;

    // The split access, written by one half and read by the other: a half that
    // refused the declaration never compiles at all, and one that chose a
    // different span for the forty bits reads a neighbouring member's byte as
    // part of the value.  The record crosses by address because how an
    // aggregate holding a bit-field is *passed* is a disagreement of its own
    // (#721), and this is the layout question rather than that one.
    if (sizeof(struct packed_split_record) != 7) return 30;
    if (packed_layout_split_size() != sizeof(struct packed_split_record)) return 31;
    if (packed_layout_split_tail_offset() != 6) return 32;
    struct packed_split_record split_record;
    packed_layout_write_split(&split_record, -0x123456789LL);
    if (split_record.lead != 'L' || split_record.value != -0x123456789LL || split_record.tail != 't') return 33;
    struct packed_split_record built_split;
    built_split.lead = 'M';
    built_split.value = 0x7fedcba987LL;
    built_split.tail = 5;
    if (packed_layout_read_split(&built_split) != 0x7fedcba987LL + 5 + 'M') return 34;

    packed_layout_fill_aligned_object('m');
    if (packed_layout_aligned_object[0] != 'm' || packed_layout_aligned_object[1] != 'n' ||
        packed_layout_aligned_object[2] != 'o')
    {
        return 12;
    }
    if ((unsigned long long)(void *)packed_layout_aligned_object % 64) return 13;

    // The ignored request, in both halves and across the value passed between
    // them: an aggregate the other compiler laid out at six bytes is read at
    // the wrong offset here.
    if (sizeof(struct below_natural_record) != 8) return 14;
    if (packed_layout_below_natural_size() != sizeof(struct below_natural_record)) return 15;
    if (packed_layout_below_natural_offset() != 4) return 16;
    struct below_natural_record below = packed_layout_make_below_natural('b', -1234567);
    if (below.tag != 'b' || below.value != -1234567) return 17;
    if (packed_layout_below_natural_object != 31337) return 18;

    // The same request on a typedef, where it is honoured rather than ignored,
    // in both directions.  A half that drops it lays `value` at offset 4 in
    // both records and sizes the raised one at 8, so every answer below is the
    // other compiler's.
    if (sizeof(struct packed_layout_raised_record) != 32) return 19;
    if (packed_layout_raised_record_size() != sizeof(struct packed_layout_raised_record)) return 20;
    if (packed_layout_raised_record_alignment() != 16) return 21;
    if (packed_layout_raised_record_value_offset() != 16) return 22;
    if (packed_layout_lowered_record_size() != 8) return 23;
    if (packed_layout_lowered_record_value_offset() != 2) return 24;

    struct packed_layout_raised_record raised = packed_layout_make_raised_record('v', -13572468);
    if (raised.tag != 'v' || raised.value != -13572468) return 25;
    if ((unsigned long long)(void *)&raised % 16) return 26;

    struct packed_layout_lowered_record lowered;
    lowered.tag = 'w';
    lowered.value = 0x1a2b3c4d;
    lowered.trailer = 'x';
    if (packed_layout_lowered_middle(lowered) != 0x1a2b3c4d) return 27;

    packed_layout_fill_raised_object(864209);
    if (packed_layout_raised_object != 864209) return 28;
    if ((unsigned long long)(void *)&packed_layout_raised_object % 16) return 29;
    return 0;
}
