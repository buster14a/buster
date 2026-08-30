// The half of the bit-field alignment story that is not a gap: C11 6.7.5p2
// forbids `_Alignas` on a bit-field, and clang and gcc both refuse it. GNU
// `aligned` on the same member is accepted and lays the field out --
// tests/basic_c_bit_field_aligned.c is that half -- and the two spellings share
// one record table, so this file is the control that says the partition between
// them did not become an unconditional acceptance. It is expected never to
// compile.

struct Refused
{
    unsigned a : 3;
    _Alignas(4) unsigned b : 5;
};

int main(void)
{
    return (int)sizeof(struct Refused);
}
