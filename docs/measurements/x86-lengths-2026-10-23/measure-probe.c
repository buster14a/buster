#include <assert.h>
#include <stdio.h>
#include "buster/compiler/x86_64_encoder.c"
__attribute__((noinline)) int64_t probe_measure(const X86Instruction* instructions, uint64_t count, uint32_t* offsets)
{
    return x86_measure(instructions, count, offsets);
}
int main(void)
{
    X86Instruction records[65] = {0};
    uint32_t vector_offsets[65];
    uint32_t scalar_offsets[65];
    if (!x86_encoder_initialize()) return 1;
    for (unsigned i = 0; i < 65; i += 1) records[i].form = (i % 5) ? X86_FORM_NOP : X86_FORM_NONE;
    for (unsigned count = 0; count <= 65; count += 1)
    {
        int64_t vector_length = probe_measure(records, count, vector_offsets);
        int64_t scalar_length = x86_measure_scalar(records, count, scalar_offsets);
        if (vector_length != scalar_length || memcmp(vector_offsets, scalar_offsets, count * 4)) return 2;
    }
    puts("measure lengths 0..65 pass");
    return 0;
}
