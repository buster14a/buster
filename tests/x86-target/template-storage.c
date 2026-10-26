// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Standalone fault injection against the actual initializer, without a production allocator hook.
// clang -std=gnu17 -O3 -march=native -I src tests/x86-target/template-storage.c -o build/template-storage-test
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool storage_test_fail = true;
static uint32_t storage_test_allocations;

static void* storage_test_allocate(size_t alignment, size_t size)
{
    storage_test_allocations += 1;
    void* result = storage_test_fail ? 0 : aligned_alloc(alignment, size);
    if (result != 0)
    {
        // Ordinary allocation is uninitialized too; poison it so accidental dependence on BSS zeros fails reliably.
        memset(result, 0xa5, size);
    }
    return result;
}

#define aligned_alloc storage_test_allocate
#include "buster/compiler/x86_64_encoder.c"
#undef aligned_alloc

static bool storage_test_encode(void)
{
    static const X86Instruction cases[] = {
        { .form = X86_FORM_NOP },
        { .reg = X86_REGISTER_RDX, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_REGISTER_DIRECT, .form = X86_FORM_ADD_RM64_R64 },
        { .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RDI, .mode = BUSTER_X86_MEMORY_BASE, .form = X86_FORM_MOV_R64_RM64 + BUSTER_X86_FORM_FS },
        { .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RDI, .mode = BUSTER_X86_MEMORY_BASE, .form = X86_FORM_MOV_R64_RM64 + BUSTER_X86_FORM_GS },
        { .displacement = -16, .form = X86_FORM_CALL_REL32 | BUSTER_X86_FORM_FIXUP },
    };
    X86Instruction records[33];
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(records); i += 1)
    {
        records[i] = cases[i % BUSTER_ARRAY_LENGTH(cases)];
    }
    bool passed = true;
    for (uint32_t count = 0; count <= BUSTER_ARRAY_LENGTH(records); count += 1)
    {
        uint8_t scalar[BUSTER_X86_ENCODER_CAPACITY(BUSTER_ARRAY_LENGTH(records))] = {0};
        uint8_t vector[sizeof(scalar)] = {0};
        uint32_t scalar_offsets[BUSTER_ARRAY_LENGTH(records)] = {0};
        uint32_t vector_offsets[BUSTER_ARRAY_LENGTH(records)] = {0};
        uint32_t scalar_fixups[BUSTER_X86_ENCODER_FIXUP_CAPACITY(BUSTER_ARRAY_LENGTH(records))] = {0};
        uint32_t vector_fixups[BUSTER_ARRAY_LENGTH(scalar_fixups)] = {0};
        const int64_t scalar_bytes = x86_encode_scalar(records, count, scalar, sizeof(scalar), scalar_offsets, scalar_fixups);
        const int64_t vector_bytes = x86_encode(records, count, vector, sizeof(vector), vector_offsets, vector_fixups);
        uint32_t fixups = 0;
        for (uint32_t i = 0; i < count; i += 1)
        {
            fixups += (records[i].form & BUSTER_X86_FORM_FIXUP) != 0;
        }
        passed &= x86_validate(records, count) == count;
        const bool sizes = scalar_bytes >= 0 && scalar_bytes <= (int64_t)sizeof(scalar) && scalar_bytes == vector_bytes;
        passed &= sizes;
        if (sizes)
        {
            passed &= memcmp(scalar, vector, (size_t)scalar_bytes) == 0;
        }
        passed &= memcmp(scalar_offsets, vector_offsets, count * sizeof(uint32_t)) == 0;
        passed &= memcmp(scalar_fixups, vector_fixups, fixups * sizeof(uint32_t)) == 0;
    }
    return passed;
}

int main(void)
{
    bool passed = x86_templates == 0 && !x86_encoder_initialized;
    passed &= !x86_encoder_initialize();
    passed &= x86_templates == 0 && !x86_encoder_initialized && storage_test_allocations == 1;
    storage_test_fail = false;
    const bool ready = x86_encoder_initialize();
    passed &= ready && x86_templates != 0 && x86_encoder_initialized && storage_test_allocations == 2;
    if (ready)
    {
        X86TemplateEntry* storage = x86_templates - BUSTER_X86_TEMPLATE_PADDING;
        const size_t span_bytes = BUSTER_X86_FORM_TEMPLATES * sizeof(X86TemplateEntry);
        uint8_t* reference = malloc(span_bytes);
        passed &= reference != 0;
        passed &= (uintptr_t)storage % BUSTER_X86_TEMPLATE_ALIGNMENT == 0;
        passed &= memcmp(x86_templates, x86_templates + BUSTER_X86_FORM_FIXUP, span_bytes) == 0;
        passed &= storage_test_encode();
        const uint8_t* leading = (const uint8_t*)storage;
        const uint8_t* trailing = (const uint8_t*)&x86_templates[BUSTER_X86_FORM_FIXUP + BUSTER_X86_FORM_TEMPLATES];
        for (uint32_t i = 0; i < BUSTER_X86_TEMPLATE_PADDING * sizeof(X86TemplateEntry); i += 1)
        {
            passed &= leading[i] == 0 && trailing[i] == 0;
        }
        if (reference != 0)
        {
            memcpy(reference, x86_templates, span_bytes);
            memset(x86_templates, 0x6d, span_bytes);
            memset(x86_templates + BUSTER_X86_FORM_FIXUP, 0x79, span_bytes);
            storage_test_fail = true;
            passed &= x86_encoder_initialize();
            passed &= x86_encoder_initialized && storage_test_allocations == 2;
            passed &= x86_templates == storage + BUSTER_X86_TEMPLATE_PADDING;
            passed &= memcmp(reference, x86_templates, span_bytes) == 0;
            passed &= memcmp(reference, x86_templates + BUSTER_X86_FORM_FIXUP, span_bytes) == 0;
            passed &= storage_test_encode();
            free(reference);
        }
        free(storage);
        x86_templates = 0;
        x86_encoder_initialized = false;
    }
    printf("template storage failure, retry, padding, alignment and repeated rebuild: %s\n", passed ? "pass" : "FAIL");
    return passed ? 0 : 1;
}
