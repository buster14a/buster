#include <assert.h>
#include "buster/compiler/x86_64_encoder.c"

__attribute__((noinline, aligned(64))) int64_t measure_probe(const X86Instruction* records, uint64_t count, uint32_t* offsets)
{
    return x86_measure(records, count, offsets);
}

__attribute__((noinline, aligned(64))) int64_t encode_probe(const X86Instruction* records, uint64_t count, uint32_t* offsets, uint8_t* bytes)
{
    return x86_encode(records, count, bytes, BUSTER_X86_ENCODER_CAPACITY(count), offsets, 0);
}

bool initialize_probe(void)
{
    return x86_encoder_initialize();
}
