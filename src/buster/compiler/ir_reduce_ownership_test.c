// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original ownership regressions for the public rebuild/reducer entry points. Borrowed plans may occupy a
// scratch prefix, but neither writable tail nor result publication may overwrite any retained owner or input.
// Include after the fixtures and helpers in ir_reduce_test.c; no compiler consumer or external process is used.

typedef struct IrReduceOwnershipPlan IrReduceOwnershipPlan;
struct IrReduceOwnershipPlan
{
    IrRebuildPlan* plan;
    uint8_t* spans[5];
};

// #732's sparse vector payload belongs to the source even when every instruction is scalar. Refuse it before
// scalar-only copies, and protect its entire borrowed span before either verifier can allocate into a writable tail.
static void ir_reduce_test_simd_ownership(IrReduceTest* test)
{
    Arena input = arena_reserve(UINT64_C(1) << 20);
    Arena output = arena_reserve(UINT64_C(1) << 20);
    Arena scratch = arena_reserve(UINT64_C(1) << 20);
    Arena verification = arena_reserve(UINT64_C(1) << 20);
    const IrSizes sizes = { .instruction_count = 1, .block_count = 1, .vector_constant_count = 2 };
    IrFunction source = { 0 };
    const bool ready = input.base != NULL && output.base != NULL && scratch.base != NULL && verification.base != NULL &&
                       ir_create(&input, &sizes, &source) == IR_OK;
    ir_reduce_test_expect(test, ready, "unused vector-pool fixture storage");
    if (ready)
    {
        source.result_type = IR_VOID;
        source.block_offsets[0] = 0;
        source.block_offsets[1] = 1;
        source.op[0] = IR_RET;
        source.type[0] = IR_VOID;
        source.flags[0] = 0;
        source.alignment_log2[0] = 0;
        source.a[0] = BUSTER_IR_NONE;
        source.b[0] = BUSTER_IR_NONE;
        source.c[0] = BUSTER_IR_NONE;
        IrSimdValue* own = source.vector_constants;
        for (uint32_t placement = 0; placement < 5; placement += 1)
        {
            memset(output.base, 0xa4, 384);
            memset(scratch.base, 0xb5, 384);
            output.length = 128;
            scratch.length = 128;
            // The crossing cases begin in a retained prefix: only the second payload overlaps the writable tail.
            source.vector_constants = placement == 0 ? own :
                                      (IrSimdValue*)((placement & 1 ? output.base : scratch.base) + (placement < 3 ? 128 : 64));
            memset(source.vector_constants, 0x73, 2 * sizeof(IrSimdValue));
            const bool verified = ir_verify_complete(&source, &verification, true).error == IR_OK &&
                                  ir_verify_complete(&source, &verification, false).error == IR_OK;
            ir_reduce_test_expect(test, verified && ir_function_has_simd(&source) && ir_function_has_simd_scalar(&source),
                                  "scalar return with an unused vector pool is valid and still a SIMD function");
            uint8_t out_before[384];
            uint8_t temp_before[384];
            uint8_t pool_before[2 * sizeof(IrSimdValue)];
            memcpy(out_before, output.base, sizeof(out_before));
            memcpy(temp_before, scratch.base, sizeof(temp_before));
            memcpy(pool_before, source.vector_constants, sizeof(pool_before));
            IrFunction unpublished;
            memset(&unpublished, 0x59, sizeof(unpublished));
            const IrFunction unchanged = unpublished;
            const IrCheck check = verified ? ir_rebuild(&source, NULL, &output, &scratch, &unpublished) : (IrCheck){ .error = IR_OK };
            const uint64_t checked_scratch = placement == 0 ? 128 : sizeof(temp_before);
            ir_reduce_test_expect(test, check.error == (placement == 0 ? IR_ERROR_TYPE : IR_ERROR_STRUCTURE) &&
                                  output.length == 128 && scratch.length == 128 && memcmp(&unpublished, &unchanged, sizeof(unpublished)) == 0 &&
                                  memcmp(out_before, output.base, sizeof(out_before)) == 0 && memcmp(temp_before, scratch.base, checked_scratch) == 0 &&
                                  memcmp(pool_before, source.vector_constants, sizeof(pool_before)) == 0,
                                  "rebuild refuses the whole SIMD function and protects a vector pool in either writable tail");
            uint32_t calls = 0;
            const IrReduceLimits limits = ir_reduce_limits();
            IrReduceResult result;
            memset(&result, 0x6a, sizeof(result));
            const IrReduceResult before = result;
            const IrReduceStatus status = verified ? ir_reduce(&source, ir_reduce_test_counting, &calls, &limits, &output, &scratch, &result) :
                                                     IR_REDUCE_COMPLETE;
            ir_reduce_test_expect(test, status == IR_REDUCE_INVALID && calls == 0 && output.length == 128 && scratch.length == 128 &&
                                  memcmp(&result, &before, sizeof(result)) == 0 && memcmp(out_before, output.base, sizeof(out_before)) == 0 &&
                                  memcmp(temp_before, scratch.base, checked_scratch) == 0 && memcmp(pool_before, source.vector_constants, sizeof(pool_before)) == 0,
                                  "reducer protects the whole borrowed vector pool before allocation and never shows a stripped copy to its predicate");
        }
    }
    arena_release(&verification);
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&input);
}

static bool ir_reduce_test_ownership_plan(const IrFunction* source, Arena* scratch, IrReduceOwnershipPlan* result)
{
    const uint64_t mark = scratch->length;
    const uint64_t sizes[] = {
        sizeof(IrRebuildPlan),
        (uint64_t)source->block_count * sizeof(uint32_t),
        (uint64_t)source->instruction_count * sizeof(uint32_t),
        ((uint64_t)source->block_count + 1) * sizeof(uint32_t),
        (uint64_t)source->instruction_count * sizeof(uint32_t),
    };
    IrReduceOwnershipPlan staged = { 0 };
    bool valid = true;
    for (uint32_t i = 0; valid && i < BUSTER_ARRAY_LENGTH(sizes); i += 1)
    {
        // Each deliberately aliased result has a complete, aligned backing object inside the retained prefix.
        const uint64_t bytes = sizes[i] > sizeof(IrFunction) ? sizes[i] : sizeof(IrFunction);
        staged.spans[i] = arena_push(scratch, bytes, 64);
        valid = staged.spans[i] != NULL;
        if (valid)
        {
            memset(staged.spans[i], 0x73, bytes);
        }
    }
    if (valid)
    {
        staged.plan = (IrRebuildPlan*)staged.spans[0];
        uint32_t* blocks = (uint32_t*)staged.spans[1];
        uint32_t* rows = (uint32_t*)staged.spans[2];
        uint32_t* offsets = (uint32_t*)staged.spans[3];
        uint32_t* aliases = (uint32_t*)staged.spans[4];
        for (uint32_t b = 0; b < source->block_count; b += 1)
        {
            blocks[b] = b;
        }
        for (uint32_t i = 0; i < source->instruction_count; i += 1)
        {
            rows[i] = i;
            aliases[i] = BUSTER_IR_NONE;
        }
        memcpy(offsets, source->block_offsets, ((uint64_t)source->block_count + 1) * sizeof(*offsets));
        *staged.plan = (IrRebuildPlan){
            .blocks = blocks,
            .block_count = source->block_count,
            .rows = rows,
            .offsets = offsets,
            .instruction_count = source->instruction_count,
            .aliases = aliases,
        };
        *result = staged;
    }
    else
    {
        arena_pop_to(scratch, mark);
    }
    return valid;
}

static void ir_reduce_test_ownership(IrReduceTest* test)
{
    Arena input = arena_reserve(UINT64_C(1) << 20);
    Arena output = arena_reserve(UINT64_C(1) << 20);
    Arena scratch = arena_reserve(UINT64_C(1) << 20);
    Arena shared = arena_reserve(UINT64_C(1) << 20);
    Arena verification = arena_reserve(UINT64_C(1) << 20);
    IrFunction source = { 0 };
    bool ready = input.base != NULL && output.base != NULL && scratch.base != NULL && shared.base != NULL && verification.base != NULL &&
                 ir_reduce_test_parse(ir_reduce_test_phi, &input, &scratch, &source);
    const uint64_t source_bytes = input.length;
    uint8_t* source_copy = ready ? arena_push(&input, source_bytes, 1) : NULL;
    ready = ready && source_copy != NULL;
    if (ready)
    {
        memcpy(source_copy, input.base, source_bytes);
    }
    ir_reduce_test_expect(test, ready, "ownership fixture retains independent valid CFG/phi source storage");
    for (uint32_t direction = 0; ready && direction < 2; direction += 1)
    {
        for (uint32_t api = 0; api < 2; api += 1)
        {
            memset(shared.base, 0xb7, 128);
            uint8_t before[128];
            memcpy(before, shared.base, sizeof(before));
            Arena out = direction == 0 ? (Arena){ .base = shared.base + 32, .capacity = 32 } :
                                        (Arena){ .base = shared.base, .length = 64, .capacity = 64 };
            Arena temp = direction == 0 ? (Arena){ .base = shared.base, .length = 64, .capacity = shared.capacity } :
                                         (Arena){ .base = shared.base + 32, .capacity = shared.capacity - 32 };
            const uint64_t out_mark = out.length;
            const uint64_t temp_mark = temp.length;
            bool rejected = false;
            if (api == 0)
            {
                IrFunction unpublished;
                memset(&unpublished, 0x59, sizeof(unpublished));
                const IrFunction unchanged = unpublished;
                const IrCheck check = ir_rebuild(&source, NULL, &out, &temp, &unpublished);
                rejected = check.error == IR_ERROR_STRUCTURE && memcmp(&unpublished, &unchanged, sizeof(unpublished)) == 0;
            }
            else
            {
                IrReduceLimits limits = ir_reduce_limits();
                limits.memory_bytes = UINT64_C(1) << 16;
                limits.max_candidates = 1;
                uint32_t mode = 0;
                IrReduceResult unpublished;
                memset(&unpublished, 0x59, sizeof(unpublished));
                const IrReduceResult unchanged = unpublished;
                const IrReduceStatus status = ir_reduce(&source, ir_reduce_test_predicate, &mode, &limits, &out, &temp, &unpublished);
                rejected = status == IR_REDUCE_INVALID && memcmp(&unpublished, &unchanged, sizeof(unpublished)) == 0;
            }
            ir_reduce_test_expect(test, rejected && out.length == out_mark && temp.length == temp_mark &&
                                  memcmp(before, shared.base, sizeof(before)) == 0,
                                  "either API rejects a writable tail over the other retained prefix before touching bytes");
        }
    }
    for (uint32_t span = 0; ready && span < 5; span += 1)
    {
        arena_pop_to(&scratch, 0);
        arena_pop_to(&output, 0);
        uint8_t* prefix = arena_push(&scratch, 64, 1);
        memset(prefix, 0xc8, 64);
        IrReduceOwnershipPlan owned = { 0 };
        const bool planned = ir_reduce_test_ownership_plan(&source, &scratch, &owned);
        const uint64_t mark = scratch.length;
        uint8_t* before = planned ? arena_push(&input, mark, 1) : NULL;
        bool preserved = before != NULL;
        if (preserved)
        {
            memcpy(before, scratch.base, mark);
            IrFunction* unpublished = (IrFunction*)owned.spans[span];
            const IrCheck check = ir_rebuild(&source, owned.plan, &output, &scratch, unpublished);
            preserved = check.error == IR_ERROR_STRUCTURE && output.length == 0 && scratch.length == mark &&
                        memcmp(before, scratch.base, mark) == 0;
        }
        ir_reduce_test_expect(test, preserved, "result overlap with any retained plan struct or array rejects without changing the plan prefix");
    }
    if (ready)
    {
        arena_pop_to(&scratch, 0);
        arena_pop_to(&output, 0);
        uint8_t* prefix = arena_push(&scratch, 64, 1);
        memset(prefix, 0xd9, 128);
        uint32_t* offsets = (uint32_t*)(scratch.base + 60);
        memcpy(offsets, source.block_offsets, ((uint64_t)source.block_count + 1) * sizeof(*offsets));
        uint8_t before[128];
        memcpy(before, scratch.base, sizeof(before));
        const IrRebuildPlan malformed = { .offsets = offsets };
        IrFunction unpublished;
        memset(&unpublished, 0x6a, sizeof(unpublished));
        const IrFunction unchanged = unpublished;
        const IrCheck check = ir_rebuild(&source, &malformed, &output, &scratch, &unpublished);
        ir_reduce_test_expect(test, check.error == IR_ERROR_STRUCTURE && output.length == 0 && scratch.length == 64 &&
                              memcmp(&unpublished, &unchanged, sizeof(unpublished)) == 0 && memcmp(before, scratch.base, sizeof(before)) == 0,
                              "identity-null plan rejects non-NULL offsets crossing the scratch-prefix boundary before verification");
        arena_pop_to(&scratch, 0);
        uint8_t* out_prefix = arena_push(&output, 64, 1);
        memset(out_prefix, 0xea, 64);
        prefix = arena_push(&scratch, 64, 1);
        memset(prefix, 0xfb, 64);
        IrReduceOwnershipPlan owned = { 0 };
        const bool planned = ir_reduce_test_ownership_plan(&source, &scratch, &owned);
        const uint64_t mark = scratch.length;
        uint8_t* plan_copy = planned ? arena_push(&input, mark, 1) : NULL;
        bool preserved = plan_copy != NULL;
        if (preserved)
        {
            memcpy(plan_copy, scratch.base, mark);
            IrFunction rebuilt = { 0 };
            const IrCheck rebuilt_check = ir_rebuild(&source, owned.plan, &output, &scratch, &rebuilt);
            const IrCheck vector = rebuilt_check.error == IR_OK ? ir_verify_complete(&rebuilt, &verification, true) : rebuilt_check;
            const IrCheck scalar = rebuilt_check.error == IR_OK ? ir_verify_complete(&rebuilt, &verification, false) : rebuilt_check;
            preserved = rebuilt_check.error == IR_OK && vector.error == IR_OK && scalar.error == IR_OK && scratch.length == mark &&
                        output.length > 64 && rebuilt.instruction_count == source.instruction_count && rebuilt.block_count == source.block_count &&
                        rebuilt.result_type == source.result_type && rebuilt.parameter_count == source.parameter_count &&
                        memcmp(rebuilt.op, source.op, source.instruction_count) == 0 &&
                        memcmp(rebuilt.block_offsets, source.block_offsets, ((uint64_t)source.block_count + 1) * sizeof(uint32_t)) == 0 &&
                        memcmp(plan_copy, scratch.base, mark) == 0;
            for (uint32_t i = 0; preserved && i < 64; i += 1)
            {
                preserved = output.base[i] == 0xea;
            }
        }
        ir_reduce_test_expect(test, preserved, "a complete scratch-prefix plan remains valid and both verifiers approve its published rebuild");
    }
    const uint64_t available[] = { 0, 1, 7, 8, 15, 16, 31, 32, 63, 64, 65, 127, 128, 255, 256, 511, 512, 1023, 2048, 4096, 8192, 16384, 65536 };
    for (uint32_t axis = 0; ready && axis < 2; axis += 1)
    {
        uint32_t failed = 0;
        uint32_t succeeded = 0;
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(available); i += 1)
        {
            memset(output.base, 0x3c, 64);
            memset(scratch.base, 0x4d, 64);
            Arena out = { .base = output.base, .length = 64, .capacity = axis == 0 ? 64 + available[i] : output.capacity };
            Arena temp = { .base = scratch.base, .length = 64, .capacity = axis == 1 ? 64 + available[i] : scratch.capacity };
            IrFunction unpublished;
            memset(&unpublished, 0x7e, sizeof(unpublished));
            const IrFunction unchanged = unpublished;
            const IrCheck check = ir_rebuild(&source, NULL, &out, &temp, &unpublished);
            bool preserved = temp.length == 64 && out.length <= out.capacity;
            if (check.error == IR_OK)
            {
                succeeded += 1;
                preserved = preserved && out.length > 64 && ir_verify_complete(&unpublished, &verification, true).error == IR_OK &&
                            ir_verify_complete(&unpublished, &verification, false).error == IR_OK;
            }
            else
            {
                failed += 1;
                preserved = preserved && (check.error == IR_ERROR_MEMORY || check.error == IR_ERROR_LIMIT) && out.length == 64 &&
                            memcmp(&unpublished, &unchanged, sizeof(unpublished)) == 0;
            }
            for (uint32_t p = 0; preserved && p < 64; p += 1)
            {
                preserved = output.base[p] == 0x3c && scratch.base[p] == 0x4d;
            }
            ir_reduce_test_expect(test, preserved, "bounded output/scratch capacity sweeps preserve entry prefixes and publish only complete valid views");
        }
        ir_reduce_test_expect(test, failed != 0 && succeeded != 0, "each ownership capacity sweep exercises both allocation refusal and successful publication");
    }
    if (ready)
    {
        ir_reduce_test_expect(test, memcmp(source_copy, input.base, source_bytes) == 0, "all ownership refusals and capacity cases preserve the original borrowed source bytes");
    }
    arena_release(&verification);
    arena_release(&shared);
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&input);
}
