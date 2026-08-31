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
    // part of the value.  The record crosses by address here and by value
    // below, so a change to how such a record is classified cannot quietly
    // take the layout question with it.
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

    // How an aggregate holding a bit-field is *passed* (#721).  System V
    // classifies the eightbytes the bit-field's bits fall in, so all four
    // records below ride registers; a half that asked the bit-field's declared
    // type where it sits returned them through a hidden pointer instead and
    // the other half read whatever its own buffer happened to hold.  Every
    // record crosses in both directions, since the result convention and the
    // argument convention are separate answers to the same classification.
    struct packed_split_record passed_split = packed_layout_make_split(-0x1234567890LL);
    if (passed_split.lead != 'S' || passed_split.value != -0x1234567890LL || passed_split.tail != 'v') return 40;
    if (packed_layout_split_sum(built_split) != 0x7fedcba987LL + 5 + 'M') return 41;

    struct packed_bit_pass_record pass = packed_layout_make_bit_pass(7, 0x12345, 9);
    if (pass.lead != 7 || pass.value != 0x12345 || pass.tail != 9) return 42;
    struct packed_bit_pass_record built_pass;
    built_pass.lead = 11;
    built_pass.value = -12345;
    built_pass.tail = 13;
    if (packed_layout_bit_pass_sum(built_pass) != 11000000LL - 12345 + 13) return 43;

    // The bits straddle the eightbyte boundary, which is the shape only the
    // eightbyte merge answers: `value` occupies bits 56 through 75.
    if (sizeof(struct packed_bit_cross_record) != 11) return 44;
    struct packed_bit_cross_record cross = packed_layout_make_bit_cross(-98765);
    if (cross.lead[0] != 1 || cross.lead[6] != 7 || cross.value != -98765 || cross.tail != 'c') return 45;
    struct packed_bit_cross_record built_cross;
    for (int index = 0; index < 7; index += 1) built_cross.lead[index] = (char)(index + 2);
    built_cross.value = 54321;
    built_cross.tail = 'd';
    if (packed_layout_bit_cross_sum(built_cross) != 2000000LL + 800000 + 54321 + 'd') return 46;

    // A named bit-field merges INTEGER into the eightbyte the float already
    // claimed and an unnamed one does not, so the first record rides a
    // general-purpose register and the second rides `xmm0`.
    struct packed_bit_named_record named = packed_layout_make_bit_named(2.5f, -3);
    if (named.lead != 2.5f || named.value != -3) return 47;
    struct packed_bit_named_record built_named;
    built_named.lead = -1.25f;
    built_named.value = 400;
    if (packed_layout_bit_named_sum(built_named) != -5 + 400) return 48;

    struct packed_bit_padded_record padded = packed_layout_make_bit_padded(6.25f);
    if (padded.lead != 6.25f) return 49;
    struct packed_bit_padded_record built_padded;
    built_padded.lead = -8.5f;
    if (packed_layout_bit_padded_lead(built_padded) != -8.5f) return 50;

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

#if PACKED_LAYOUT_HOST_PASSES_UNDERALIGNED_IN_MEMORY
    struct packed_layout_lowered_record lowered;
    lowered.tag = 'w';
    lowered.value = 0x1a2b3c4d;
    lowered.trailer = 'x';
    if (packed_layout_lowered_middle(lowered) != 0x1a2b3c4d) return 27;
#endif

    packed_layout_fill_raised_object(864209);
    if (packed_layout_raised_object != 864209) return 28;
    if ((unsigned long long)(void *)&packed_layout_raised_object % 16) return 29;

    // And with a qualifier in front of the alias, which keeps the request: a
    // half that dropped it there lays `value` at offset 4 in both records and
    // sizes the const one at 8, so each half reads a different member.
    if (sizeof(struct packed_layout_const_record) != 32) return 35;
    if (packed_layout_const_record_size() != sizeof(struct packed_layout_const_record)) return 36;
    if (packed_layout_const_record_alignment() != 16) return 37;
    if (packed_layout_const_record_value_offset() != 16) return 38;
    if (packed_layout_volatile_record_size() != 8) return 39;
    if (packed_layout_volatile_record_value_offset() != 2) return 40;

    struct packed_layout_const_record const_record = packed_layout_make_const_record('y', -24681357);
    if (const_record.tag != 'y' || const_record.value != -24681357) return 41;
    if ((unsigned long long)(void *)&const_record % 16) return 42;

    struct packed_layout_volatile_record volatile_record = {'z', 0x5e6f7a8b, '!'};
    if (packed_layout_volatile_middle(volatile_record) != 0x5e6f7a8b) return 43;
    return 0;
}
