// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Tests of the native Tier 0 unwind proof, included after tier0_test.c. Actual typed programs exercise both producers
// and both branch sizers before both analyzers consume their public records. Deep global expressions force every
// nonvolatile register; generated programs add loops, alternate returns, calls (also through a register), floating
// values, pointer stores and objects in the frame.
// Original record fixtures cover all saved masks and isolate refusals, with poisoned output rollback and immutable
// input checks. These are descriptor tests, not machine execution or an independent CFI interpreter.

#define BUSTER_UNWIND_TIER0_TEST_RECORDS 256

typedef struct UnwindTier0Test UnwindTier0Test;
struct UnwindTier0Test
{
    uint64_t checked;
    uint64_t failed;
    uint64_t generated_masks;
    uint32_t functions;
    uint32_t calls;
    uint32_t loops;
    uint32_t returns;
    uint32_t pointers;
    uint32_t indirect_calls;
    uint32_t objects;
    // Functions with a frame pointer (variable-length arrays), which only the published frame describes.
    uint32_t framed;
    uint64_t framed_masks;
};

static bool unwind_tier0_test_expect(UnwindTier0Test* test, bool condition, const char* check, uint64_t id)
{
    test->checked += 1;
    if (!condition)
    {
        if (test->failed < 20)
        {
            fprintf(stderr, "native unwind analyzer: %s, case %" PRIu64 "\n", check, id);
        }
        test->failed += 1;
    }
    return condition;
}

static X86Instruction unwind_tier0_test_record(uint16_t form, uint8_t reg, uint8_t rm, uint8_t mode, int32_t displacement, int32_t immediate)
{
    const X86Instruction result = {
        .immediate = immediate,
        .displacement = displacement,
        .reg = reg,
        .rm = rm,
        .mode = mode,
        .form = form,
    };
    return result;
}

// A frame pointer's descriptor as tier0_emit_prologue and tier0_emit_epilogue lay its records out: rbp's push, its mov
// and the other pushes first, then the reservation if any (no transition); the release (no transition), the other pops,
// rbp's and the ret last. Each transition ends where the next record starts.
static UnwindFunction unwind_tier0_test_pointer_frame(const Tier0Code* code, const uint32_t* offsets, uint32_t size)
{
    const uint32_t pushes = (uint32_t)__builtin_popcount(code->frame_saved);
    UnwindFunction expected = {
        .size = size,
        .saved = (uint8_t)code->frame_saved,
        .pointer = 1,
        .event_count = (uint8_t)(2 * pushes + 3),
    };
    for (uint32_t event = 0; event < 2 + pushes; event += 1)
    {
        expected.offsets[event] = offsets[event + 1];
    }
    for (uint32_t pop = 0; pop <= pushes; pop += 1)
    {
        expected.offsets[2 + pushes + pop] = offsets[code->count - 1 - pushes + pop];
    }
    return expected;
}

// Whether a frame pointer's body, from the prologue's end to the epilogue's release, leaves rbp alone, by each record's
// effects (x86_semantics.c's, apart from the frame reader, which takes the producer's word for it): the CFA is rbp's
// there. A form the effects tables don't cover gets their conservative record, which writes everything.
static bool unwind_tier0_test_keeps_rbp(const Tier0Code* code)
{
    const uint32_t pushes = (uint32_t)__builtin_popcount(code->frame_saved);
    const uint32_t first = 2 + pushes + (code->frame_size != 0 ? 1 : 0);
    const uint32_t release = code->count - (pushes + 3);
    bool keeps = first <= release;
    for (uint32_t r = first; keeps && r < release; r += 1)
    {
        uint64_t reads = 0;
        uint64_t writes = 0;
        x86_instruction_effects_scalar(&code->records[r], &reads, &writes);
        keeps = (writes & BUSTER_X86_RESOURCE_GPR(X86_REGISTER_RBP)) == 0;
    }
    return keeps;
}

// Every producer/analysis combination must describe the same final records. The test allocates exactly count
// offsets: no sentinel element is offered to the analyzer. No function or code is executed by this helper.
static bool unwind_tier0_test_function(UnwindTier0Test* test, const Tier0Function* function, Arena* arena, uint64_t id, uint32_t expected_saved)
{
    const uint64_t mark = arena->length;
    const Tier0Check checked = tier0_validate(function);
    bool valid = unwind_tier0_test_expect(test, checked.error == TIER0_OK, "typed input validates", id);
    Tier0Code codes[2] = { 0 };
    uint32_t* offsets[2] = { 0 };
    uint32_t sizes[2] = { 0 };
    if (valid)
    {
        codes[0] = tier0_compile_scalar(function, arena);
        codes[1] = tier0_compile(function, arena);
        valid = unwind_tier0_test_expect(test, codes[0].error == TIER0_OK && codes[1].error == TIER0_OK &&
                                                tier0_test_same_code(&codes[0], &codes[1]), "actual producer records agree", id);
    }
    for (uint32_t path = 0; valid && path < 2; path += 1)
    {
        Tier0Code* code = &codes[path];
        offsets[path] = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, code->count);
        const BranchSizingCheck check = branch_sizing_validate(code->records, code->count, code->branches, code->targets, code->branch_count);
        valid = unwind_tier0_test_expect(test, offsets[path] != 0 && check.error == BRANCH_SIZING_OK, "records and branch metadata validate", id);
        if (valid)
        {
            const BranchSizing sized = path == 0 ? size_branches_scalar(code->records, code->count, code->branches, code->targets, code->branch_count, offsets[path]) :
                                                  size_branches(code->records, code->count, code->branches, code->targets, code->branch_count, offsets[path]);
            valid = unwind_tier0_test_expect(test, sized.error == BRANCH_SIZING_OK && sized.length <= INT32_MAX, "actual branch sizing succeeds", id);
            sizes[path] = (uint32_t)sized.length;
        }
    }
    UnwindFunction reference = { 0 };
    if (valid)
    {
        valid = unwind_tier0_test_expect(test, sizes[0] == sizes[1] && tier0_test_same_code(&codes[0], &codes[1]) &&
                                                memcmp(offsets[0], offsets[1], (uint64_t)codes[0].count * sizeof(uint32_t)) == 0,
                                        "final producer records and PCs agree", id);
    }
    const bool pointer = valid && codes[0].frame_pointer;
    for (uint32_t producer = 0; valid && producer < 2; producer += 1)
    {
        for (uint32_t exact = 0; exact < 2; exact += 1)
        {
            UnwindFunction descriptor;
            memset(&descriptor, 0xa5 + exact, sizeof(descriptor));
            UnwindFunction sentinel;
            memcpy(&sentinel, &descriptor, sizeof(sentinel));
            const UnwindError error = exact != 0 ? unwind_tier0_scalar(&codes[producer], offsets[producer], sizes[producer], &descriptor) :
                                                  unwind_tier0(&codes[producer], offsets[producer], sizes[producer], &descriptor);
            bool accepted = false;
            if (pointer)
            {
                // The analyzer proves fixed frames alone: a frame pointer's prologue is no shape it takes.
                accepted = unwind_tier0_test_expect(test, error == UNWIND_ERROR_UNSUPPORTED && memcmp(&descriptor, &sentinel, sizeof(descriptor)) == 0,
                                                    "the analyzer refuses a frame pointer's function", id);
            }
            else
            {
                accepted = unwind_tier0_test_expect(test, error == UNWIND_OK && unwind_check(&descriptor, 1) == UNWIND_OK,
                                                    "actual generated function has a valid descriptor", id);
            }
            if (accepted && !pointer)
            {
                if (producer == 0 && exact == 0)
                {
                    reference = descriptor;
                }
                unwind_tier0_test_expect(test, memcmp(&descriptor, &reference, sizeof(descriptor)) == 0,
                                        "scalar/vector producers and analyzers agree", id);
                unwind_tier0_test_expect(test, expected_saved == UINT32_MAX || descriptor.saved == expected_saved,
                                        "typed expression forces its expected saved registers", id);
            }
            valid = accepted && valid;
        }
    }
    // The frame each producer published (unwind_tier0_frame, the C driver's): the analyzer's descriptor wherever the
    // analyzer takes the function, and a frame pointer's as its prologue and epilogue lay it out.
    for (uint32_t producer = 0; valid && producer < 2; producer += 1)
    {
        UnwindFunction framed;
        memset(&framed, 0xc3, sizeof(framed));
        const UnwindError error = unwind_tier0_frame(&codes[producer], offsets[producer], sizes[producer], &framed);
        const UnwindFunction expected = pointer ? unwind_tier0_test_pointer_frame(&codes[producer], offsets[producer], sizes[producer]) : reference;
        valid = unwind_tier0_test_expect(test, error == UNWIND_OK && unwind_check(&framed, 1) == UNWIND_OK && memcmp(&framed, &expected, sizeof(framed)) == 0,
                                         pointer ? "the published frame pointer's prologue and epilogue" : "the published frame gives the analyzer's descriptor", id);
    }
    if (valid && pointer)
    {
        valid = unwind_tier0_test_expect(test, unwind_tier0_test_keeps_rbp(&codes[0]), "a frame pointer's body leaves rbp alone", id);
        test->framed += valid ? 1 : 0;
        test->framed_masks |= valid ? 1ull << codes[0].frame_saved : 0;
    }
    if (valid)
    {
        test->functions += 1;
        test->generated_masks |= pointer ? 0 : 1ull << reference.saved;
        test->calls += function->call_count != 0 ? 1 : 0;
        bool indirect = false;
        for (uint32_t c = 0; c < function->call_count; c += 1)
        {
            indirect |= function->call_targets[c] == 0;
        }
        test->indirect_calls += indirect ? 1 : 0;
        test->objects += function->local_sizes != 0 ? 1 : 0;
        for (uint32_t branch = 0; branch < codes[0].branch_count; branch += 1)
        {
            test->loops += codes[0].targets[branch] < codes[0].branches[branch] ? 1 : 0;
        }
        uint32_t returns = 0;
        for (uint32_t i = 0; i < function->count; i += 1)
        {
            returns += function->kinds[i] == BUSTER_TYPED_RETURN ? 1 : 0;
            test->pointers += function->kinds[i] == BUSTER_TYPED_STORE || function->kinds[i] == BUSTER_TYPED_SET ? 1 : 0;
        }
        test->returns += returns > 1 ? 1 : 0;
    }
    arena_pop_to(arena, mark);
    return valid;
}

static void unwind_tier0_test_generated(UnwindTier0Test* test, Arena* arena)
{
    // Globals cannot be constant/home-fused away. The fifteen simultaneous values reach all six nonvolatile
    // depth registers, including RBP as an ordinary saved general register rather than a frame pointer.
    for (uint32_t depth = 1; depth <= 15; depth += 1)
    {
        uint8_t kinds[30] = { 0 };
        uint8_t classes[30] = { 0 };
        uint64_t values[30] = { 0 };
        for (uint32_t i = 0; i < depth; i += 1)
        {
            kinds[i] = BUSTER_TYPED_GLOBAL;
            values[i] = i + 1;
        }
        for (uint32_t i = depth; i + 1 < 2 * depth; i += 1)
        {
            kinds[i] = BUSTER_TYPED_ADD;
        }
        kinds[2 * depth - 1] = BUSTER_TYPED_RETURN;
        memset(classes, BUSTER_TYPED_CLASS_U64, 2 * depth);
        const Tier0Function function = {
            .kinds = kinds,
            .classes = classes,
            .values = values,
            .count = 2 * depth,
        };
        const uint32_t saved = (depth > 9 ? 1u : 0u) | (depth > 14 ? 2u : 0u) | (depth > 10 ? 4u : 0u) |
                               (depth > 11 ? 8u : 0u) | (depth > 12 ? 16u : 0u) | (depth > 13 ? 32u : 0u);
        (void)unwind_tier0_test_function(test, &function, arena, depth, saved);
    }
    static const uint64_t seeds[] = {
        1,
        7,
        0x12345678,
        0xdeadbeef,
    };
    static const Tier0ProgramOptions options[] = {
        { .functions = 1, .statements = 6, .expression_nodes = 6 },
        { .functions = 3, .statements = 12, .expression_nodes = 10, .calls = true, .loops = true },
        { .functions = 4, .statements = 16, .expression_nodes = 14, .calls = true, .loops = true, .floating = true, .pointers = true },
        { .functions = 8, .statements = 24, .expression_nodes = 20, .calls = true, .loops = true, .floating = true, .pointers = true },
        // Calls through pointers (`call r11`) and objects in the frame.
        { .functions = 8, .statements = 24, .expression_nodes = 20, .calls = true, .loops = true, .floating = true, .pointers = true, .objects = true, .indirect = true },
        // test-tier0's larger shape: symbols through the GOT and TLS, and variable-length arrays, whose functions have a
        // frame pointer; then with records by value, and past the depth tables (below).
        { .functions = 8, .statements = 24, .expression_nodes = 20, .calls = true, .loops = true, .floating = true, .pointers = true, .objects = true, .indirect = true,
          .access = true, .vlas = true, .unsigned_floating = true },
    };
    Tier0Tables tables = { 0 };
    const bool opened = tier0_tables_open(&tables);
    unwind_tier0_test_expect(test, opened, "type and ABI tables", 0);
    const uint32_t shapes = BUSTER_ARRAY_LENGTH(options) + 2;
    for (uint32_t shape = 0; opened && shape < shapes; shape += 1)
    {
        Tier0ProgramOptions shape_options = options[shape < BUSTER_ARRAY_LENGTH(options) ? shape : BUSTER_ARRAY_LENGTH(options) - 1];
        shape_options.records = shape == BUSTER_ARRAY_LENGTH(options) ? &tables.records : 0;
        shape_options.deep = shape == BUSTER_ARRAY_LENGTH(options) + 1;
        for (uint32_t seed = 0; seed < BUSTER_ARRAY_LENGTH(seeds); seed += 1)
        {
            const uint64_t mark = arena->length;
            const uint64_t id = seeds[seed] + shape * 1000003ull;
            Tier0Program* program = BUSTER_ARENA_PUSH_ARRAY(arena, Tier0Program, 1);
            Tier0Functions* functions = BUSTER_ARENA_PUSH_ARRAY(arena, Tier0Functions, 1);
            const bool generated = program != 0 && functions != 0 && tier0_generate_program(program, arena, id, shape_options, "unwind_") &&
                                   tier0_program_functions(program, &tables, arena, functions);
            unwind_tier0_test_expect(test, generated, "fixed-seed typed program generation", id);
            for (uint32_t f = 0; generated && f < functions->count; f += 1)
            {
                (void)unwind_tier0_test_function(test, &functions->functions[f], arena, id * 64 + f, UINT32_MAX);
            }
            arena_pop_to(arena, mark);
        }
    }
    tier0_tables_close(&tables);
    unwind_tier0_test_expect(test, (test->generated_masks & (1ull << 63)) != 0 && (test->generated_masks & 1) != 0,
                            "real producer covers no saves and all six saves", 0);
    unwind_tier0_test_expect(test, test->calls != 0 && test->loops != 0 && test->returns != 0 && test->pointers != 0,
                            "generated coverage includes calls, loops, alternate returns and pointer stores", 0);
    unwind_tier0_test_expect(test, test->indirect_calls != 0 && test->objects != 0, "generated coverage includes indirect calls and objects", 0);
    unwind_tier0_test_expect(test, test->framed != 0 && __builtin_popcountll(test->framed_masks) > 1, "generated coverage includes frame pointers' functions", 0);
}

typedef struct UnwindTier0Fixture UnwindTier0Fixture;
struct UnwindTier0Fixture
{
    X86Instruction records[BUSTER_UNWIND_TIER0_TEST_RECORDS];
    uint32_t offsets[BUSTER_UNWIND_TIER0_TEST_RECORDS];
    uint32_t branches[4];
    uint32_t targets[4];
    uint32_t event_ends[14];
    LinkReference references[1];
    Tier0Code code;
    UnwindFunction expected;
    uint32_t body;
    uint32_t epilogue;
    uint32_t size;
};

static void unwind_tier0_test_bind(UnwindTier0Fixture* fixture)
{
    fixture->code.records = fixture->records;
    fixture->code.branches = fixture->branches;
    fixture->code.targets = fixture->targets;
    fixture->code.references = fixture->references;
}

static bool unwind_tier0_test_fixture(UnwindTier0Fixture* fixture, uint8_t saved, uint32_t frame, uint32_t filler)
{
    static const uint8_t registers[] = {
        X86_REGISTER_RBX,
        X86_REGISTER_RBP,
        X86_REGISTER_R12,
        X86_REGISTER_R13,
        X86_REGISTER_R14,
        X86_REGISTER_R15,
    };
    memset(fixture, 0, sizeof(*fixture));
    unwind_tier0_test_bind(fixture);
    fixture->code.frame_saved = saved;
    fixture->code.frame_size = frame;
    fixture->expected.saved = saved;
    fixture->expected.frame_size = frame;
    uint32_t* count = &fixture->code.count;
    for (uint32_t reg = 0; reg < BUSTER_ARRAY_LENGTH(registers); reg += 1)
    {
        if ((saved & (1u << reg)) != 0)
        {
            fixture->records[*count] = unwind_tier0_test_record(X86_FORM_PUSH_R64, 0, registers[reg], 0, 0, 0);
            *count += 1;
            fixture->event_ends[fixture->expected.event_count] = *count;
            fixture->expected.event_count += 1;
        }
    }
    if (frame != 0)
    {
        const uint16_t form = frame <= 127 ? X86_FORM_SUB_RM64_IMM8 : X86_FORM_SUB_RM64_IMM32;
        fixture->records[*count] = unwind_tier0_test_record(form, 0, X86_REGISTER_RSP, 0, 0, (int32_t)frame);
        *count += 1;
        fixture->event_ends[fixture->expected.event_count] = *count;
        fixture->expected.event_count += 1;
    }
    fixture->body = *count;
    fixture->records[*count] = unwind_tier0_test_record(X86_FORM_NOP, 0, 0, 0, 0, 0);
    *count += 1;
    const bool aligned_call = (8 + 8 * (uint32_t)__builtin_popcount(saved) + frame) % 16 == 0;
    fixture->records[*count] = unwind_tier0_test_record(aligned_call ? X86_FORM_CALL_REL32 | BUSTER_X86_FORM_FIXUP : X86_FORM_NOP, 0, 0, 0, 0, 0);
    fixture->references[0] = (LinkReference){
        .instruction = *count,
        .field = X86_FIXUP_DISPLACEMENT,
        .kind = BUSTER_LINK_FIXUP_CALL,
        .target = 1,
    };
    fixture->code.reference_count = aligned_call ? 1 : 0;
    *count += 1;
    fixture->branches[0] = *count;
    fixture->records[*count] = unwind_tier0_test_record(X86_FORM_JE_REL8, 0, 0, 0, 0, 0);
    *count += 1;
    fixture->branches[1] = *count;
    fixture->records[*count] = unwind_tier0_test_record(X86_FORM_JMP_REL8, 0, 0, 0, 0, 0);
    *count += 1;
    for (uint32_t i = 0; i < filler; i += 1)
    {
        fixture->records[*count] = unwind_tier0_test_record(X86_FORM_NOP, 0, 0, 0, 0, 0);
        *count += 1;
    }
    fixture->branches[2] = *count;
    fixture->targets[2] = fixture->body;
    fixture->records[*count] = unwind_tier0_test_record(X86_FORM_JMP_REL8, 0, 0, 0, 0, 0);
    *count += 1;
    fixture->epilogue = *count;
    fixture->targets[0] = *count;
    fixture->targets[1] = *count;
    fixture->code.branch_count = 3;
    if (frame != 0)
    {
        const uint16_t form = frame <= 127 ? X86_FORM_ADD_RM64_IMM8 : X86_FORM_ADD_RM64_IMM32;
        fixture->records[*count] = unwind_tier0_test_record(form, 0, X86_REGISTER_RSP, 0, 0, (int32_t)frame);
        *count += 1;
        fixture->event_ends[fixture->expected.event_count] = *count;
        fixture->expected.event_count += 1;
    }
    for (uint32_t reg = BUSTER_ARRAY_LENGTH(registers); reg > 0; reg -= 1)
    {
        if ((saved & (1u << (reg - 1))) != 0)
        {
            fixture->records[*count] = unwind_tier0_test_record(X86_FORM_POP_R64, 0, registers[reg - 1], 0, 0, 0);
            *count += 1;
            fixture->event_ends[fixture->expected.event_count] = *count;
            fixture->expected.event_count += 1;
        }
    }
    fixture->records[*count] = unwind_tier0_test_record(X86_FORM_RET, 0, 0, 0, 0, 0);
    *count += 1;
    const BranchSizingCheck checked = branch_sizing_validate(fixture->records, *count, fixture->branches, fixture->targets, fixture->code.branch_count);
    bool valid = checked.error == BRANCH_SIZING_OK;
    if (valid)
    {
        const BranchSizing sized = size_branches(fixture->records, *count, fixture->branches, fixture->targets, fixture->code.branch_count, fixture->offsets);
        valid = sized.error == BRANCH_SIZING_OK && sized.length <= INT32_MAX;
        fixture->size = (uint32_t)sized.length;
        fixture->expected.size = fixture->size;
        for (uint32_t event = 0; valid && event < fixture->expected.event_count; event += 1)
        {
            fixture->expected.offsets[event] = fixture->offsets[fixture->event_ends[event]];
        }
    }
    return valid;
}

// Mutated instructions still get genuine final byte offsets. Structural metadata/offset corruption cases skip this
// helper intentionally. No generic encoder validation is moved into the production analyzer.
static bool unwind_tier0_test_measure(UnwindTier0Fixture* fixture)
{
    bool valid = x86_validate(fixture->records, fixture->code.count) == fixture->code.count;
    const int64_t size = valid ? x86_measure_scalar(fixture->records, fixture->code.count, fixture->offsets) : -1;
    valid = valid && size > 0 && size <= INT32_MAX;
    if (valid)
    {
        fixture->size = (uint32_t)size;
        fixture->expected.size = fixture->size;
        for (uint32_t event = 0; event < fixture->expected.event_count; event += 1)
        {
            fixture->expected.offsets[event] = fixture->offsets[fixture->event_ends[event]];
        }
        for (uint32_t branch = 0; branch < fixture->code.branch_count; branch += 1)
        {
            const uint32_t record = fixture->branches[branch];
            const uint32_t target = fixture->targets[branch];
            if (record + 1 < fixture->code.count && target < fixture->code.count && unwind_tier0_branch(fixture->records[record].form))
            {
                fixture->records[record].displacement = (int32_t)((int64_t)fixture->offsets[target] - fixture->offsets[record + 1]);
            }
        }
    }
    return valid;
}

static void unwind_tier0_test_result(UnwindTier0Test* test, UnwindTier0Fixture* fixture, UnwindError wanted, uint64_t id)
{
    UnwindTier0Fixture before;
    memcpy(&before, fixture, sizeof(before));
    for (uint32_t exact = 0; exact < 2; exact += 1)
    {
        UnwindFunction descriptor;
        UnwindFunction sentinel;
        memset(&descriptor, 0xb6 + exact, sizeof(descriptor));
        memcpy(&sentinel, &descriptor, sizeof(sentinel));
        const UnwindError error = exact != 0 ? unwind_tier0_scalar(&fixture->code, fixture->offsets, fixture->size, &descriptor) :
                                              unwind_tier0(&fixture->code, fixture->offsets, fixture->size, &descriptor);
        unwind_tier0_test_expect(test, error == wanted, "fixture acceptance/refusal code", id);
        const bool output = wanted == UNWIND_OK ? memcmp(&descriptor, &fixture->expected, sizeof(descriptor)) == 0 :
                                                 memcmp(&descriptor, &sentinel, sizeof(descriptor)) == 0;
        unwind_tier0_test_expect(test, output, "exact expected events or unchanged poisoned descriptor", id);
        unwind_tier0_test_expect(test, memcmp(&before, fixture, sizeof(before)) == 0, "analyzer preserves all input records and columns", id);
    }
}

static void unwind_tier0_test_shapes(UnwindTier0Test* test)
{
    static const uint32_t frames[] = { 0, 8, 4096 };
    for (uint32_t saved = 0; saved < 64; saved += 1)
    {
        for (uint32_t frame = 0; frame < BUSTER_ARRAY_LENGTH(frames); frame += 1)
        {
            UnwindTier0Fixture fixture;
            const uint64_t id = saved * BUSTER_ARRAY_LENGTH(frames) + frame;
            if (unwind_tier0_test_expect(test, unwind_tier0_test_fixture(&fixture, (uint8_t)saved, frames[frame], saved == 63 ? 160 : saved % 9),
                                       "build each saved-mask fixture", id))
            {
                unwind_tier0_test_result(test, &fixture, UNWIND_OK, id);
                if (saved == 63)
                {
                    unwind_tier0_test_expect(test, fixture.records[fixture.branches[0]].form == X86_FORM_JE_REL32 &&
                                                      fixture.records[fixture.branches[1]].form == X86_FORM_JMP_REL32 &&
                                                      fixture.records[fixture.branches[2]].form == X86_FORM_JMP_REL32,
                                            "forward and backward long branches preserve the frame state", id);
                }
            }
        }
    }
    UnwindTier0Fixture leaf;
    memset(&leaf, 0, sizeof(leaf));
    unwind_tier0_test_bind(&leaf);
    leaf.records[0].form = X86_FORM_RET;
    leaf.code.count = 1;
    leaf.size = 1;
    leaf.expected.size = 1;
    unwind_tier0_test_result(test, &leaf, UNWIND_OK, 1000);
}

static void unwind_tier0_test_mutations(UnwindTier0Test* test)
{
    UnwindTier0Fixture original;
    const bool ready = unwind_tier0_test_fixture(&original, 5, 40, 7);
    if (unwind_tier0_test_expect(test, ready, "build refusal baseline with two saves and an aligned call", 0))
    {
        unwind_tier0_test_result(test, &original, UNWIND_OK, 2000);
        for (uint32_t mutation = 0; mutation < 53; mutation += 1)
        {
            UnwindTier0Fixture fixture;
            memcpy(&fixture, &original, sizeof(fixture));
            unwind_tier0_test_bind(&fixture);
            X86Instruction* slot = &fixture.records[fixture.body];
            X86Instruction* call = &fixture.records[fixture.body + 1];
            UnwindError wanted = UNWIND_ERROR_UNSUPPORTED;
            bool measure = false;
            switch (mutation)
            {
                case 0: fixture.code.branch_count -= 1; break;
                case 1: fixture.branches[1] = fixture.branches[0]; break;
                case 2: fixture.branches[0] = original.branches[1]; fixture.branches[1] = original.branches[0]; break;
                case 3: slot->form = X86_FORM_JMP_REL8; measure = true; break;
                case 4: fixture.records[fixture.branches[0]] = unwind_tier0_test_record(X86_FORM_NOP, 0, 0, 0, 0, 0); measure = true; break;
                case 5: fixture.targets[0] = 0; break;
                case 6: fixture.targets[0] = fixture.body - 1; break;
                case 7: fixture.targets[0] = fixture.epilogue + 1; break;
                case 8: fixture.targets[0] = fixture.code.count - 1; break;
                case 9: fixture.targets[0] = fixture.code.count; break;
                case 10: fixture.records[fixture.branches[0]] = unwind_tier0_test_record(X86_FORM_JMP_RM64, 0, X86_REGISTER_RAX, 0, 0, 0); measure = true; break;
                case 11: *slot = unwind_tier0_test_record(X86_FORM_MOV_R64_RM64, X86_REGISTER_RSP, X86_REGISTER_RAX, 0, 0, 0); measure = true; break;
                case 12: *slot = unwind_tier0_test_record(X86_FORM_MOV_R32_IMM32, 0, X86_REGISTER_RSP, 0, 0, 1); measure = true; break;
                case 13: *slot = unwind_tier0_test_record(X86_FORM_MOV_RM16_IMM16, 0, X86_REGISTER_RSP, 0, 0, 1); measure = true; break;
                case 14: *slot = unwind_tier0_test_record(X86_FORM_MOV_RM8_IMM8, 0, X86_REGISTER_RSP, 0, 0, 1); measure = true; break;
                case 15: *slot = unwind_tier0_test_record(X86_FORM_AND_RM64_IMM8, 0, X86_REGISTER_RSP, 0, 0, -16); measure = true; break;
                case 16: *slot = unwind_tier0_test_record(X86_FORM_LEA_R64_M, X86_REGISTER_RSP, X86_REGISTER_RAX, BUSTER_X86_MEMORY_BASE, 8, 0); measure = true; break;
                case 17: *slot = unwind_tier0_test_record(X86_FORM_XCHG_RM64_R64, X86_REGISTER_RAX, X86_REGISTER_RSP, 0, 0, 0); measure = true; break;
                case 18: *slot = unwind_tier0_test_record(X86_FORM_PUSH_R64, 0, X86_REGISTER_RAX, 0, 0, 0); measure = true; break;
                case 19: *slot = unwind_tier0_test_record(X86_FORM_POP_R64, 0, X86_REGISTER_RAX, 0, 0, 0); measure = true; break;
                case 20: slot->form = X86_FORM_LEAVE; measure = true; break;
                case 21: slot->form = X86_FORM_ENTER; measure = true; break;
                case 22: slot->form = X86_FORM_RET; measure = true; break;
                case 23: *slot = unwind_tier0_test_record(X86_FORM_RET_IMM16, 0, 0, 0, 0, 8); measure = true; break;
                case 24: slot->form = X86_FORM_UD2; measure = true; break;
                case 25: slot->form = X86_FORM_CPUID; measure = true; break;
                case 26: *call = unwind_tier0_test_record(X86_FORM_CALL_RM64, 0, X86_REGISTER_RAX, 0, 0, 0); measure = true; break;
                case 27: call->form = X86_FORM_CALL_REL32; measure = true; break;
                case 28: *slot = unwind_tier0_test_record(X86_FORM_MOV_R64_IMM64, 0, X86_REGISTER_RBP, 0, 0, 1); measure = true; break;
                case 29: fixture.records[1].rm = fixture.records[0].rm; measure = true; break;
                case 30: fixture.records[0].rm = X86_REGISTER_R12; fixture.records[1].rm = X86_REGISTER_RBX; measure = true; break;
                case 31: fixture.records[0].rm = X86_REGISTER_RAX; break;
                case 32: fixture.records[fixture.code.count - 2].rm = X86_REGISTER_RBP; break;
                case 33: fixture.records[fixture.epilogue].immediate = 41; break;
                case 34: fixture.records[fixture.body - 1].immediate = -40; break;
                case 35: fixture.records[fixture.code.count - 1].form = X86_FORM_NOP; break;
                case 36: *slot = unwind_tier0_test_record(X86_FORM_MOV_RM64_R64, X86_REGISTER_RAX, X86_REGISTER_RSP, BUSTER_X86_MEMORY_BASE, 40, 0); measure = true; break;
                case 37: *slot = unwind_tier0_test_record(X86_FORM_MOV_RM64_R64, X86_REGISTER_RAX, X86_REGISTER_RSP, BUSTER_X86_MEMORY_BASE, 56, 0); measure = true; break;
                case 38: *slot = unwind_tier0_test_record(X86_FORM_MOV_RM64_R64, X86_REGISTER_RAX, X86_REGISTER_RSP, BUSTER_X86_MEMORY_BASE, 36, 0); measure = true; break;
                case 39: *slot = unwind_tier0_test_record(X86_FORM_MOV_RM64_R64, X86_REGISTER_RAX, X86_REGISTER_RSP, BUSTER_X86_MEMORY_BASE | BUSTER_X86_MODE_INDEX, 0, 0); slot->index = X86_REGISTER_RCX; measure = true; break;
                case 40: *slot = unwind_tier0_test_record(X86_FORM_MOV_RM64_R64 + BUSTER_X86_FORM_FS, X86_REGISTER_RAX, X86_REGISTER_RSP, BUSTER_X86_MEMORY_BASE, 0, 0); measure = true; break;
                case 41: *slot = unwind_tier0_test_record(X86_FORM_MOV_RM64_R64 | BUSTER_X86_FORM_FIXUP, X86_REGISTER_RAX, X86_REGISTER_RSP, BUSTER_X86_MEMORY_BASE, 0, 0); measure = true; break;
                case 42: fixture.offsets[0] = 1; wanted = UNWIND_ERROR_INVALID; break;
                case 43: fixture.offsets[fixture.body + 1] = fixture.offsets[fixture.body]; wanted = UNWIND_ERROR_INVALID; break;
                case 44: fixture.offsets[fixture.code.count - 1] = fixture.size; wanted = UNWIND_ERROR_INVALID; break;
                case 45: fixture.size = 0; wanted = UNWIND_ERROR_INVALID; break;
                case 46: fixture.code.count = 0; wanted = UNWIND_ERROR_INVALID; break;
                case 47: fixture.code.error = TIER0_ERROR_MEMORY; wanted = UNWIND_ERROR_INVALID; break;
                case 48: fixture.code.branch_count = fixture.code.count + 1; wanted = UNWIND_ERROR_INVALID; break;
                case 49: fixture.code.branches = 0; wanted = UNWIND_ERROR_INVALID; break;
                case 50: fixture.code.targets = 0; wanted = UNWIND_ERROR_INVALID; break;
                case 51: fixture.code.records = 0; wanted = UNWIND_ERROR_INVALID; break;
                // Tier 0's indirect call, through r11, is a call like its direct one (26's through rax isn't).
                case 52: *call = unwind_tier0_test_record(X86_FORM_CALL_RM64, 0, X86_REGISTER_R11, 0, 0, 0); measure = true; wanted = UNWIND_OK; break;
            }
            if (!measure || unwind_tier0_test_expect(test, unwind_tier0_test_measure(&fixture), "mutated instruction remains encoder-valid", mutation))
            {
                unwind_tier0_test_result(test, &fixture, wanted, 3000 + mutation);
            }
        }
    }
}

static void unwind_tier0_test_boundaries(UnwindTier0Test* test)
{
    static const uint16_t branches[][2] = {
        { X86_FORM_JMP_REL8, X86_FORM_JMP_REL32 },
        { X86_FORM_JO_REL8, X86_FORM_JO_REL32 },
        { X86_FORM_JNO_REL8, X86_FORM_JNO_REL32 },
        { X86_FORM_JB_REL8, X86_FORM_JB_REL32 },
        { X86_FORM_JAE_REL8, X86_FORM_JAE_REL32 },
        { X86_FORM_JE_REL8, X86_FORM_JE_REL32 },
        { X86_FORM_JNE_REL8, X86_FORM_JNE_REL32 },
        { X86_FORM_JBE_REL8, X86_FORM_JBE_REL32 },
        { X86_FORM_JA_REL8, X86_FORM_JA_REL32 },
        { X86_FORM_JS_REL8, X86_FORM_JS_REL32 },
        { X86_FORM_JNS_REL8, X86_FORM_JNS_REL32 },
        { X86_FORM_JP_REL8, X86_FORM_JP_REL32 },
        { X86_FORM_JNP_REL8, X86_FORM_JNP_REL32 },
        { X86_FORM_JL_REL8, X86_FORM_JL_REL32 },
        { X86_FORM_JGE_REL8, X86_FORM_JGE_REL32 },
        { X86_FORM_JLE_REL8, X86_FORM_JLE_REL32 },
        { X86_FORM_JG_REL8, X86_FORM_JG_REL32 },
    };
    UnwindTier0Fixture original;
    const bool ready = unwind_tier0_test_fixture(&original, 5, 40, 7);
    if (unwind_tier0_test_expect(test, ready, "build accepted-boundary baseline", 0))
    {
        // The protected span is [40,64): two saves and the return address. Ordinary local slots, incoming
        // parameters, and non-stack pointer stores are permitted by the stated compiler/ABI contract.
        for (uint32_t variant = 0; variant < 5; variant += 1)
        {
            UnwindTier0Fixture fixture;
            memcpy(&fixture, &original, sizeof(fixture));
            unwind_tier0_test_bind(&fixture);
            X86Instruction* slot = &fixture.records[fixture.body];
            switch (variant)
            {
                case 0: *slot = unwind_tier0_test_record(X86_FORM_MOV_RM64_R64, X86_REGISTER_RAX, X86_REGISTER_RSP, BUSTER_X86_MEMORY_BASE, 32, 0); break;
                case 1: *slot = unwind_tier0_test_record(X86_FORM_MOV_RM64_R64, X86_REGISTER_RAX, X86_REGISTER_RSP, BUSTER_X86_MEMORY_BASE, 64, 0); break;
                case 2: *slot = unwind_tier0_test_record(X86_FORM_MOV_RM64_R64, X86_REGISTER_RAX, X86_REGISTER_RSP, BUSTER_X86_MEMORY_BASE, -8, 0); break;
                case 3: *slot = unwind_tier0_test_record(X86_FORM_MOV_R64_IMM64, 0, X86_REGISTER_RBX, 0, 0, 1); break;
                case 4: *slot = unwind_tier0_test_record(X86_FORM_MOV_RM64_R64, X86_REGISTER_RCX, X86_REGISTER_RAX, BUSTER_X86_MEMORY_BASE, 0, 0); break;
            }
            if (unwind_tier0_test_expect(test, unwind_tier0_test_measure(&fixture), "accepted boundary remains encoder-valid", variant))
            {
                unwind_tier0_test_result(test, &fixture, UNWIND_OK, 4000 + variant);
            }
        }
        for (uint32_t form = 0; form < BUSTER_ARRAY_LENGTH(branches); form += 1)
        {
            for (uint32_t width = 0; width < 2; width += 1)
            {
                UnwindTier0Fixture fixture;
                memcpy(&fixture, &original, sizeof(fixture));
                unwind_tier0_test_bind(&fixture);
                fixture.records[fixture.branches[0]].form = branches[form][width];
                const uint64_t id = 4100 + 2 * form + width;
                if (unwind_tier0_test_expect(test, unwind_tier0_test_measure(&fixture), "each normalized branch remains encoder-valid", id))
                {
                    unwind_tier0_test_result(test, &fixture, UNWIND_OK, id);
                }
            }
        }
    }
}

// A frame pointer's function as Tier 0 lays it out (`saved` without RBP's bit): rbp pushed and pointed at its push,
// the other pushes and the reservation; a body that allocates on the stack as ALLOCA's records do; the release, the
// pops, rbp's and the ret. Its offsets measured, its expected descriptor from its transitions' records.
static bool unwind_tier0_test_pointer_fixture(UnwindTier0Fixture* fixture, uint8_t saved, uint32_t frame)
{
    static const uint8_t registers[] = {
        X86_REGISTER_RBX,
        X86_REGISTER_RBP,
        X86_REGISTER_R12,
        X86_REGISTER_R13,
        X86_REGISTER_R14,
        X86_REGISTER_R15,
    };
    memset(fixture, 0, sizeof(*fixture));
    unwind_tier0_test_bind(fixture);
    fixture->code.frame_saved = saved;
    fixture->code.frame_size = frame;
    fixture->code.frame_pointer = true;
    fixture->expected.saved = saved;
    fixture->expected.pointer = 1;
    uint32_t* count = &fixture->code.count;
    const uint32_t pushes = (uint32_t)__builtin_popcount(saved);
    fixture->records[(*count)++] = unwind_tier0_test_record(X86_FORM_PUSH_R64, 0, X86_REGISTER_RBP, 0, 0, 0);
    fixture->event_ends[fixture->expected.event_count++] = *count;
    fixture->records[(*count)++] = unwind_tier0_test_record(X86_FORM_MOV_RM64_R64, X86_REGISTER_RSP, X86_REGISTER_RBP, BUSTER_X86_REGISTER_DIRECT, 0, 0);
    fixture->event_ends[fixture->expected.event_count++] = *count;
    for (uint32_t reg = 0; reg < BUSTER_ARRAY_LENGTH(registers); reg += 1)
    {
        if ((saved & (1u << reg)) != 0)
        {
            fixture->records[(*count)++] = unwind_tier0_test_record(X86_FORM_PUSH_R64, 0, registers[reg], 0, 0, 0);
            fixture->event_ends[fixture->expected.event_count++] = *count;
        }
    }
    if (frame != 0)
    {
        const uint16_t form = frame <= 127 ? X86_FORM_SUB_RM64_IMM8 : X86_FORM_SUB_RM64_IMM32;
        fixture->records[(*count)++] = unwind_tier0_test_record(form, 0, X86_REGISTER_RSP, 0, 0, (int32_t)frame);
    }
    fixture->body = *count;
    fixture->records[(*count)++] = unwind_tier0_test_record(X86_FORM_MOV_R32_IMM32, 0, X86_REGISTER_RAX, 0, 0, 100);
    fixture->records[(*count)++] = unwind_tier0_test_record(X86_FORM_ADD_RM64_IMM8, 0, X86_REGISTER_RAX, BUSTER_X86_REGISTER_DIRECT, 0, 15);
    fixture->records[(*count)++] = unwind_tier0_test_record(X86_FORM_AND_RM64_IMM8, 0, X86_REGISTER_RAX, BUSTER_X86_REGISTER_DIRECT, 0, -16);
    fixture->records[(*count)++] = unwind_tier0_test_record(X86_FORM_SUB_RM64_R64, X86_REGISTER_RAX, X86_REGISTER_RSP, BUSTER_X86_REGISTER_DIRECT, 0, 0);
    fixture->records[(*count)++] = unwind_tier0_test_record(X86_FORM_MOV_R64_RM64, X86_REGISTER_RAX, X86_REGISTER_RSP, BUSTER_X86_REGISTER_DIRECT, 0, 0);
    fixture->epilogue = *count;
    fixture->records[(*count)++] = pushes != 0 ? unwind_tier0_test_record(X86_FORM_LEA_R64_M, X86_REGISTER_RSP, X86_REGISTER_RBP, BUSTER_X86_MEMORY_BASE, -8 * (int32_t)pushes, 0)
                                               : unwind_tier0_test_record(X86_FORM_MOV_RM64_R64, X86_REGISTER_RBP, X86_REGISTER_RSP, BUSTER_X86_REGISTER_DIRECT, 0, 0);
    for (uint32_t reg = BUSTER_ARRAY_LENGTH(registers); reg > 0; reg -= 1)
    {
        if ((saved & (1u << (reg - 1))) != 0)
        {
            fixture->records[(*count)++] = unwind_tier0_test_record(X86_FORM_POP_R64, 0, registers[reg - 1], 0, 0, 0);
            fixture->event_ends[fixture->expected.event_count++] = *count;
        }
    }
    fixture->records[(*count)++] = unwind_tier0_test_record(X86_FORM_POP_R64, 0, X86_REGISTER_RBP, 0, 0, 0);
    fixture->event_ends[fixture->expected.event_count++] = *count;
    fixture->records[(*count)++] = unwind_tier0_test_record(X86_FORM_RET, 0, 0, 0, 0, 0);
    return unwind_tier0_test_measure(fixture);
}

// unwind_tier0_frame on a fixture: `wanted`, with the fixture's expected descriptor or the poisoned one left alone, and
// its input as it was.
static void unwind_tier0_test_frame_result(UnwindTier0Test* test, const UnwindTier0Fixture* fixture, UnwindError wanted, uint64_t id)
{
    UnwindTier0Fixture before;
    memcpy(&before, fixture, sizeof(before));
    UnwindFunction descriptor;
    memset(&descriptor, 0xe7, sizeof(descriptor));
    UnwindFunction sentinel;
    memcpy(&sentinel, &descriptor, sizeof(sentinel));
    const UnwindError error = unwind_tier0_frame(&fixture->code, fixture->offsets, fixture->size, &descriptor);
    unwind_tier0_test_expect(test, error == wanted, "published frame acceptance/refusal code", id);
    const bool output = wanted == UNWIND_OK ? memcmp(&descriptor, &fixture->expected, sizeof(descriptor)) == 0 && unwind_check(&descriptor, 1) == UNWIND_OK :
                                             memcmp(&descriptor, &sentinel, sizeof(descriptor)) == 0;
    unwind_tier0_test_expect(test, output, "published frame's expected events or unchanged poisoned descriptor", id);
    unwind_tier0_test_expect(test, memcmp(&before, fixture, sizeof(before)) == 0, "published frame reader preserves its input", id);
}

// The published frame's reader (unwind_tier0_frame): every mask with and without a frame pointer, at every kind of
// reservation, then each record and field it checks broken in turn, refused with the descriptor left alone.
static void unwind_tier0_test_frames(UnwindTier0Test* test)
{
    static const uint32_t frames[] = { 0, 8, 4096 };
    for (uint32_t saved = 0; saved < 64; saved += 1)
    {
        for (uint32_t frame = 0; frame < BUSTER_ARRAY_LENGTH(frames); frame += 1)
        {
            UnwindTier0Fixture fixture;
            const uint64_t id = 5000 + saved * BUSTER_ARRAY_LENGTH(frames) + frame;
            if (unwind_tier0_test_expect(test, unwind_tier0_test_fixture(&fixture, (uint8_t)saved, frames[frame], saved % 9), "build each fixed frame", id))
            {
                unwind_tier0_test_frame_result(test, &fixture, UNWIND_OK, id);
            }
            if ((saved & 2) == 0 &&
                unwind_tier0_test_expect(test, unwind_tier0_test_pointer_fixture(&fixture, (uint8_t)saved, frames[frame]), "build each frame pointer's frame", id))
            {
                unwind_tier0_test_frame_result(test, &fixture, UNWIND_OK, id + 1000);
            }
        }
    }
    UnwindTier0Fixture pointer;
    UnwindTier0Fixture fixed;
    const bool ready = unwind_tier0_test_expect(test, unwind_tier0_test_pointer_fixture(&pointer, 5, 40) && unwind_tier0_test_fixture(&fixed, 5, 40, 7),
                                                "build the published frame's refusal baselines", 0);
    for (uint32_t mutation = 0; ready && mutation < 34; mutation += 1)
    {
        UnwindTier0Fixture fixture;
        memcpy(&fixture, mutation < 24 ? &pointer : &fixed, sizeof(fixture));
        unwind_tier0_test_bind(&fixture);
        X86Instruction* records = fixture.records;
        const uint32_t last = fixture.code.count - 1;
        UnwindError wanted = UNWIND_ERROR_UNSUPPORTED;
        switch (mutation)
        {
            // A frame pointer's: push rbp, mov rbp, rsp, push rbx, push r12, sub rsp, 40 ... lea rsp, [rbp - 16], pop r12,
            // pop rbx, pop rbp, ret.
            case 0: records[0].rm = X86_REGISTER_RBX; break;
            case 1: records[1] = unwind_tier0_test_record(X86_FORM_MOV_RM64_R64, X86_REGISTER_RBP, X86_REGISTER_RSP, BUSTER_X86_REGISTER_DIRECT, 0, 0); break;
            case 2: records[1].mode = BUSTER_X86_MEMORY_BASE; break;
            case 3: records[2].rm = X86_REGISTER_R12; break;
            case 4: records[4].immediate = 48; break;
            case 5: records[4].form = X86_FORM_ADD_RM64_IMM8; break;
            case 6: records[fixture.epilogue].displacement = -8; break;
            case 7: records[fixture.epilogue] = unwind_tier0_test_record(X86_FORM_MOV_RM64_R64, X86_REGISTER_RBP, X86_REGISTER_RSP, BUSTER_X86_REGISTER_DIRECT, 0, 0); break;
            case 8: records[last - 3].rm = X86_REGISTER_R13; break;
            case 9: records[last - 1].rm = X86_REGISTER_RBX; break;
            case 10: records[last].form = X86_FORM_NOP; break;
            case 11: fixture.code.frame_saved |= 2; wanted = UNWIND_ERROR_INVALID; break;
            case 12: fixture.code.frame_saved = 64; wanted = UNWIND_ERROR_INVALID; break;
            case 13: fixture.code.count = 3; wanted = UNWIND_ERROR_INVALID; break;
            case 14: fixture.size = 0; wanted = UNWIND_ERROR_INVALID; break;
            case 15: fixture.size = (uint32_t)INT32_MAX + 1; wanted = UNWIND_ERROR_LIMIT; break;
            case 16: fixture.offsets[0] = 1; wanted = UNWIND_ERROR_INVALID; break;
            case 17: fixture.offsets[2] = fixture.offsets[1]; wanted = UNWIND_ERROR_INVALID; break;
            case 18: fixture.offsets[last] = fixture.size; wanted = UNWIND_ERROR_INVALID; break;
            case 19: fixture.code.error = TIER0_ERROR_MEMORY; wanted = UNWIND_ERROR_INVALID; break;
            case 20: fixture.code.records = 0; wanted = UNWIND_ERROR_INVALID; break;
            case 21: fixture.code.frame_pointer = false; break;
            // A reservation the producer doesn't publish is the body's, which a frame pointer's CFA doesn't follow.
            case 22: fixture.code.frame_size = 0; wanted = UNWIND_OK; break;
            case 23: memmove(&records[4], &records[5], (fixture.code.count - 5) * sizeof(X86Instruction)); fixture.code.count -= 1; break;
            // A fixed frame's: push rbx, push r12, sub rsp, 40 ... add rsp, 40, pop r12, pop rbx, ret.
            case 24: fixture.code.frame_saved = 4; break;
            case 25: fixture.code.frame_size = 48; break;
            case 26: records[fixture.epilogue].immediate = 48; break;
            case 27: fixture.code.frame_pointer = true; break;
            case 28: records[last].form = X86_FORM_NOP; break;
            case 29: records[last - 1].rm = X86_REGISTER_R12; break;
            case 30: fixture.code.frame_size = (uint32_t)INT32_MAX + 1; wanted = UNWIND_ERROR_LIMIT; break;
            case 31: records[fixture.body - 1].form = X86_FORM_NOP; break;
            case 32: records[0].form = X86_FORM_POP_R64; break;
            case 33: fixture.code.count = 1; wanted = UNWIND_ERROR_INVALID; break;
        }
        if (mutation == 22)
        {
            fixture.expected.frame_size = 0;
        }
        unwind_tier0_test_frame_result(test, &fixture, wanted, 6000 + mutation);
    }
    // Missing inputs.
    UnwindFunction descriptor;
    memset(&descriptor, 0xe7, sizeof(descriptor));
    unwind_tier0_test_expect(test, ready && unwind_tier0_frame(0, fixed.offsets, fixed.size, &descriptor) == UNWIND_ERROR_INVALID &&
                                       unwind_tier0_frame(&fixed.code, 0, fixed.size, &descriptor) == UNWIND_ERROR_INVALID &&
                                       unwind_tier0_frame(&fixed.code, fixed.offsets, fixed.size, 0) == UNWIND_ERROR_INVALID,
                            "published frame reader refuses missing inputs", 6100);
}

static void unwind_tier0_test_guards(UnwindTier0Test* test)
{
    const int64_t page_size = sysconf(_SC_PAGESIZE);
    const uint64_t page = page_size > 0 ? (uint64_t)page_size : 0;
    uint8_t* mapping = MAP_FAILED;
    bool ready = page >= 66 * sizeof(X86Instruction) && page >= sizeof(UnwindFunction);
    if (ready)
    {
        mapping = mmap(0, 6 * page, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        ready = mapping != MAP_FAILED;
    }
    if (ready)
    {
        ready = mprotect(mapping, page, PROT_READ | PROT_WRITE) == 0 &&
                mprotect(mapping + 2 * page, page, PROT_READ | PROT_WRITE) == 0 &&
                mprotect(mapping + 4 * page, page, PROT_READ | PROT_WRITE) == 0;
    }
    if (unwind_tier0_test_expect(test, ready, "guarded records, offsets and descriptor", 0))
    {
        // A count of zero must reject without reading either guarded pointer. Every other fixture has a body of
        // count-1 one-byte NOPs followed by RET; the final record and offset end exactly at inaccessible pages.
        for (uint32_t count = 0; count <= 66; count += 1)
        {
            X86Instruction* records = (X86Instruction*)(mapping + page - count * sizeof(X86Instruction));
            uint32_t* offsets = (uint32_t*)(mapping + 3 * page - count * sizeof(uint32_t));
            UnwindFunction* output = (UnwindFunction*)(mapping + 5 * page - sizeof(UnwindFunction));
            for (uint32_t i = 0; i < count; i += 1)
            {
                records[i] = unwind_tier0_test_record(i + 1 == count ? X86_FORM_RET : X86_FORM_NOP, 0, 0, 0, 0, 0);
                offsets[i] = i;
            }
            const Tier0Code code = {
                .records = records,
                .count = count,
            };
            for (uint32_t exact = 0; exact < 2; exact += 1)
            {
                memset(output, 0xd8 + exact, sizeof(*output));
                UnwindFunction expected = {
                    .size = count,
                };
                if (count == 0)
                {
                    memcpy(&expected, output, sizeof(expected));
                }
                const UnwindError error = exact != 0 ? unwind_tier0_scalar(&code, offsets, count, output) : unwind_tier0(&code, offsets, count, output);
                unwind_tier0_test_expect(test, error == (count != 0 ? UNWIND_OK : UNWIND_ERROR_INVALID) &&
                                                  memcmp(output, &expected, sizeof(expected)) == 0, "exact guarded analyzer tails", count);
                memset(output, 0xd8 + exact, sizeof(*output));
                const UnwindError framed = unwind_tier0_frame(&code, offsets, count, output);
                unwind_tier0_test_expect(test, framed == (count != 0 ? UNWIND_OK : UNWIND_ERROR_INVALID) &&
                                                  memcmp(output, &expected, sizeof(expected)) == 0, "guarded published frame tails", count);
            }
        }
    }
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, 6 * page);
    }
}

static bool unwind_tier0_test(void)
{
    UnwindTier0Test test = { 0 };
    Arena arena = arena_reserve(1ull << 28);
    const bool ready = arena.base != 0 && x86_encoder_initialize() && x86_semantics_initialize() && tier0_initialize();
    if (unwind_tier0_test_expect(&test, ready, "initialize analyzer test dependencies", 0))
    {
        unwind_tier0_test_shapes(&test);
        unwind_tier0_test_mutations(&test);
        unwind_tier0_test_boundaries(&test);
        unwind_tier0_test_guards(&test);
        unwind_tier0_test_frames(&test);
        unwind_tier0_test_generated(&test, &arena);
    }
    arena_release(&arena);
    printf("native unwind analyzer: %" PRIu64 " checks, %" PRIu64 " failed; %u actual functions, saved-mask coverage %016" PRIx64
           ", %u calling functions, %u backward branches, %u alternate-return functions, %u pointer stores; %u with a frame pointer, masks %016" PRIx64 "\n",
           test.checked, test.failed, test.functions, test.generated_masks, test.calls, test.loops, test.returns, test.pointers, test.framed, test.framed_masks);
    return test.failed == 0;
}
