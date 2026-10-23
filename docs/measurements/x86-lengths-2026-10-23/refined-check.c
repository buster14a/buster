#include <assert.h>
#include "buster/compiler/x86_64_encoder.c"
#include "buster/compiler/x86_64_encoder_test.c"

int main(void)
{
    const uint64_t count = 300000;
    const uint64_t capacity = count + BUSTER_X86_GROUP_INSTRUCTIONS;
    X86Instruction* records = malloc(capacity * sizeof(*records));
    X86Buffers buffers = {
        .expected = malloc(BUSTER_X86_ENCODER_CAPACITY(capacity)),
        .output = malloc(BUSTER_X86_ENCODER_CAPACITY(capacity)),
        .expected_offsets = malloc(capacity * sizeof(uint32_t)),
        .offsets = malloc(capacity * sizeof(uint32_t)),
        .expected_fixup_ends = malloc(BUSTER_X86_ENCODER_FIXUP_CAPACITY(capacity) * sizeof(uint32_t)),
        .fixup_ends = malloc(BUSTER_X86_ENCODER_FIXUP_CAPACITY(capacity) * sizeof(uint32_t)),
    };
    bool valid = records != 0 && buffers.expected != 0 && buffers.output != 0 && buffers.expected_offsets != 0 && buffers.offsets != 0 &&
                 buffers.expected_fixup_ends != 0 && buffers.fixup_ends != 0 && x86_encoder_initialize();
    uint64_t state = 305 * BUSTER_X86_TEST_GOLDEN_RATIO + 1;
    x86_test_mode = X86_ENCODER_MODE_64;
    if (valid)
    {
        valid = x86_check_measure(&state, count, records, &buffers);
        valid = x86_check_measure_bounds(&state, records, &buffers) && valid;
    }
    free(buffers.fixup_ends);
    free(buffers.expected_fixup_ends);
    free(buffers.offsets);
    free(buffers.expected_offsets);
    free(buffers.output);
    free(buffers.expected);
    free(records);
    return valid ? 0 : 1;
}
