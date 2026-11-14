// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Included by emitted_code_test.c after its layout, verifier-mutation and temporary-directory helpers.

// Near-limit fixed frames (#1149), including all six saved-register pushes. No generated code in these tests runs:
// only instruction records, frame events and object files are produced, so no large stack allocation is attempted.
static void emitted_test_large_fixed(EmittedTest* test)
{
    for (uint32_t saved = 0; saved < 64; saved += 1)
    {
        const uint32_t base = 8 + 8 * (uint32_t)__builtin_popcount(saved);
        const uint32_t sizes[] = { INT32_MAX - base, INT32_MAX - base + 1, 0x7ffffff8u, INT32_MAX };
        for (uint32_t k = 0; k < BUSTER_ARRAY_LENGTH(sizes); k += 1)
        {
            const uint64_t mark = test->arena.length;
            const uint64_t id = saved * BUSTER_ARRAY_LENGTH(sizes) + k;
            X86Instruction records[2 * BUSTER_UNWIND_SAVED + 3];
            uint32_t count = 0;
            for (uint32_t r = 0; r < BUSTER_UNWIND_SAVED; r += 1)
            {
                if ((saved & (1u << r)) != 0)
                {
                    records[count++] = (X86Instruction){ .form = X86_FORM_PUSH_R64, .rm = unwind_tier0_saved_registers[r] };
                }
            }
            records[count++] = tier1_frame_emit_immediate(X86_FORM_SUB_RM64_IMM32, X86_REGISTER_RSP, (int32_t)sizes[k]);
            records[count++] = tier1_frame_emit_immediate(X86_FORM_ADD_RM64_IMM32, X86_REGISTER_RSP, (int32_t)sizes[k]);
            for (uint32_t r = BUSTER_UNWIND_SAVED; r > 0; r -= 1)
            {
                if ((saved & (1u << (r - 1))) != 0)
                {
                    records[count++] = (X86Instruction){ .form = X86_FORM_POP_R64, .rm = unwind_tier0_saved_registers[r - 1] };
                }
            }
            records[count++] = (X86Instruction){ .form = X86_FORM_RET };
            const Tier0Code code = {
                .records = records,
                .count = count,
                .frame_saved = saved,
                .frame_size = sizes[k],
                .error = TIER0_OK,
            };
            EmittedTestLayout layout;
            const bool laid = emitted_test_layout(records, count, 0, 0, 0, 0, &test->arena, &layout);
            const EmittedCode emitted = emitted_from_tier0(&code, true, false, false, &test->arena);
            bool valid = emitted_test_expect(test, laid && emitted.frame_status == EMITTED_FRAME_DESCRIBED, "the entire fixed imm32 frame range is described", id);
            int64_t largest = 8;
            for (uint32_t e = 0; valid && e < emitted.frame_count; e += 1)
            {
                const EmissionFrameEvent event = emitted.frames[e];
                largest = event.kind == EMISSION_FRAME_CFA_OFFSET && event.value > largest ? event.value : largest;
            }
            emitted_test_expect(test, valid && largest == (int64_t)base + sizes[k], "frame events retain the CFA beyond INT32_MAX", id);
            UnwindFunction fixed;
            valid = valid && unwind_tier0_frame(&code, layout.offsets, layout.length, &fixed) == UNWIND_OK;
            EmissionFrameRow rows[3 * BUSTER_UNWIND_EVENTS];
            valid = valid && emitted.frame_count <= BUSTER_ARRAY_LENGTH(rows);
            for (uint32_t scalar = 0; valid && scalar < 2; scalar += 1)
            {
                UnwindFunction frame = { 0 };
                const char* reason = 0;
                const CheckProgramStatus status = check_program_emit_frame(&emitted, layout.offsets, layout.length, true, scalar != 0, rows, &test->scratch, &frame, &reason);
                emitted_test_expect(test, status == CHECK_PROGRAM_OK && reason == 0 && frame.open == 0 && frame.saved == saved && frame.frame_size == sizes[k],
                                    "the large frame lowers to its fixed descriptor with both verifier paths", id);
                emitted_test_expect(test, status == CHECK_PROGRAM_OK && emitted_test_same_bytes(&fixed, &frame, false) && emitted_test_same_bytes(&fixed, &frame, true),
                                    "near-limit frames keep the fixed writer's CFI bytes", id);
            }
            if (valid)
            {
                emitted_test_mutants(test, &emitted, 3 * BUSTER_UNWIND_EVENTS, id);
            }
            arena_pop_to(&test->arena, mark);
        }
    }
}

// Wider rows also reach the general writer, including saved state and a factored offset larger than uint32_t.
static void emitted_test_wide_rows(EmittedTest* test)
{
    const EmissionFrameRow rows[] = {
        { .offset = 1, .kind = EMISSION_FRAME_CFA_OFFSET, .value = INT64_C(0x100000008) },
        { .offset = 2, .kind = EMISSION_FRAME_REMEMBER_STATE },
        { .offset = 2, .kind = EMISSION_FRAME_CFA_OFFSET, .value = 8 },
        { .offset = 3, .kind = EMISSION_FRAME_RESTORE_STATE },
        { .offset = 4, .kind = EMISSION_FRAME_CFA, .reg = BUSTER_EMISSION_X86_RBP, .value = INT64_C(0x100000008) },
        { .offset = 4, .kind = EMISSION_FRAME_OFFSET, .reg = 3, .value = INT64_MIN },
    };
    const uint8_t expected[] = {
        0x41, 0x0e, 0x88, 0x80, 0x80, 0x80, 0x10,
        0x0a,
        0x41, 0x0e, 8,
        0x41, 0x0b,
        0x41, 0x0d, 6,
        0x83, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x10,
    };
    const UnwindFunction program = {
        .size = 5,
        .open = BUSTER_UNWIND_ROWS,
        .rows = rows,
        .row_count = BUSTER_ARRAY_LENGTH(rows),
    };
    uint8_t bytes[256];
    const uint64_t size = emitted_test_write(&program, false, true, bytes, sizeof(bytes));
    const uint32_t instructions = BUSTER_UNWIND_CIE_BYTES + 17;
    // Four one-byte advances leave 26 instruction bytes: the 17-byte FDE header and those bytes align to 48.
    emitted_test_expect(test, size == BUSTER_UNWIND_CIE_BYTES + 48 && memcmp(bytes + instructions, expected, sizeof(expected)) == 0,
                        "wide rows preserve ULEBs, saved CFA state and factored signed slots", 0);
    const EmissionFrameRow simple[] = {
        { .offset = 1, .kind = EMISSION_FRAME_CFA_OFFSET, .value = INT64_MAX },
        { .offset = 2, .kind = EMISSION_FRAME_CFA_OFFSET, .value = 8 },
    };
    UnwindFunction lowered = { 0 };
    const bool valid = unwind_rows_function(simple, BUSTER_ARRAY_LENGTH(simple), 3, &lowered) == UNWIND_OK;
    emitted_test_expect(test, valid && lowered.open == BUSTER_UNWIND_ROWS && lowered.frame_size == 0,
                        "a row beyond the imm32 allocation range is not narrowed into a fixed frame", 0);
    const uint64_t large_size = valid ? emitted_test_write(&lowered, false, true, bytes, sizeof(bytes)) : 0;
    const uint8_t largest[] = {
        0x41, 0x0e, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f,
        0x41, 0x0e, 8,
    };
    emitted_test_expect(test, large_size == BUSTER_UNWIND_CIE_BYTES + 32 && memcmp(bytes + instructions, largest, sizeof(largest)) == 0,
                        "the row writer retains INT64_MAX's nine-byte ULEB", 0);
}

static void emitted_test_wide_tracking(EmittedTest* test)
{
    emitted_test_expect(test, emitted_add(-8, -(int64_t)INT32_MAX) == -(int64_t)INT32_MAX - 8 && emitted_add(INT32_MIN, -8) == (int64_t)INT32_MIN - 8,
                        "CFA-relative tracking crosses the old sentinel and both signed 32-bit bounds", 0);
    emitted_test_expect(test, emitted_add(INT64_MAX, 1) == BUSTER_EMITTED_UNKNOWN && emitted_add(INT64_MIN + 1, -2) == BUSTER_EMITTED_UNKNOWN &&
                              emitted_add(INT64_MIN + 1, -1) == BUSTER_EMITTED_UNKNOWN && emitted_add(BUSTER_EMITTED_UNKNOWN, 8) == BUSTER_EMITTED_UNKNOWN,
                        "64-bit overflow and the reserved unknown value remain unknown", 0);
    EmittedState state = emitted_entry_state(false);
    EmittedRules rules = emitted_initial_rules();
    state.value[X86_REGISTER_RSP] = INT32_MIN;
    rules.cfa_offset = INT64_C(2147483648);
    emitted_test_expect(test, emitted_holds(&rules, &state), "the old sentinel is now a known CFA-relative address", 0);
    state.value[X86_REGISTER_RSP] = -INT64_MAX;
    rules.cfa_offset = INT64_MAX;
    emitted_test_expect(test, emitted_holds(&rules, &state), "the largest positive CFA is checked without summing offsets", 0);
    state.value[X86_REGISTER_RSP] = INT64_MAX;
    emitted_test_expect(test, !emitted_holds(&rules, &state), "a mismatched extreme CFA cannot wrap into a proof", 0);
    state = emitted_entry_state(false);
    rules = emitted_initial_rules();
    rules.kinds[3] = EMITTED_RULE_OFFSET;
    rules.values[3] = BUSTER_EMITTED_UNKNOWN;
    emitted_test_expect(test, !emitted_holds(&rules, &state), "an unknown slot is not proof of a sentinel-valued saved rule", 0);
    state.slots[X86_REGISTER_RBX] = INT64_MAX - 3;
    emitted_clobber_slots(&state, INT64_MAX - 1, 8);
    emitted_test_expect(test, state.slots[X86_REGISTER_RBX] == BUSTER_EMITTED_UNKNOWN, "overlap at INT64_MAX is detected without endpoint overflow", 0);
    state.slots[X86_REGISTER_RBX] = INT64_MIN + 1;
    emitted_clobber_slots(&state, INT64_MAX - 1, 8);
    emitted_test_expect(test, state.slots[X86_REGISTER_RBX] == INT64_MIN + 1, "distant extreme slots do not overlap", 0);
    emitted_clobber_slots(&state, INT64_MIN + 2, 0);
    emitted_test_expect(test, state.slots[X86_REGISTER_RBX] == INT64_MIN + 1, "a zero-width store clobbers no slot", 0);
}

static void emitted_test_large_tier1(EmittedTest* test)
{
    const uint32_t start = 0;
    const uint32_t end = 1;
    const uint32_t size = 0x7ffffff0u;
    const uint32_t alignment = 8;
    for (uint32_t probe = 0; probe < 2; probe += 1)
    {
        const uint64_t mark = test->arena.length;
        const Tier1FrameRequest request = {
            .slots = { .start = &start, .end = &end, .size = &size, .alignment = &alignment, .count = 1, .column_count = 1 },
            .flags = BUSTER_TIER1_FRAME_NO_RED_ZONE | (probe != 0 ? BUSTER_TIER1_FRAME_PROBE_STACK : 0),
        };
        Tier1Frame frame;
        EmittedTestFrame code;
        const bool planned = tier1_frame_plan(&request, &test->arena, &test->scratch, &frame) == TIER1_FRAME_OK;
        const bool built = planned && frame.stack_adjust_bytes == 0x7ffffff8u && emitted_test_frame_code(&frame, 0, false, false, false, &code);
        emitted_test_expect(test, built, "Tier 1 emits a CFA of 2^31 with and without probes", probe);
        if (built)
        {
            const EmittedCode emitted = {
                .records = code.records,
                .count = code.buffer.count,
                .branches = code.branches,
                .targets = code.targets,
                .branch_count = code.buffer.branch_count,
                .frames = code.events,
                .frame_count = code.buffer.event_count,
                .frame_status = EMITTED_FRAME_DESCRIBED,
            };
            const EmittedVerdict verdict = emitted_verify_frames(&emitted, &test->scratch);
            emitted_test_expect(test, verdict.error == EMITTED_VERIFY_OK, "Tier 1's wide CFA and early-return state are proved", probe);
            emitted_test_mutants(test, &emitted, BUSTER_TIER1_FRAME_EMIT_EVENTS, 256 + probe);
        }
        arena_pop_to(&test->arena, mark);
    }
}

static void emitted_test_large_driver(EmittedTest* test)
{
    char directory[PATH_MAX];
    const bool made = emitted_test_directory(directory, sizeof(directory), "buster-large-frame");
    emitted_test_expect(test, made, "make the compile-only large-frame directory", 0);
    if (made)
    {
        const uint64_t mark = test->arena.length;
        char source[PATH_MAX];
        char object[PATH_MAX];
        char errors[PATH_MAX];
        snprintf(source, sizeof(source), "%s/frame.c", directory);
        snprintf(object, sizeof(object), "%s/frame.o", directory);
        snprintf(errors, sizeof(errors), "%s/frame.err", directory);
        // One escaping object within the rounded object budget; call alignment adds 8 bytes for a CFA of 2^31.
        static const char text[] = "extern void sink(void *); void large_frame(void) { char bytes[2147483632]; sink(bytes); }\n";
        const bool written = debug_test_write_file(source, (const uint8_t*)text, sizeof(text) - 1);
        emitted_test_expect(test, written, "write the compile-only near-limit frame", 0);
        String8 first = { 0 };
        for (uint32_t mode = 0; written && mode < 8; mode += 1)
        {
            char* arguments[12] = { "/proc/self/exe", "--lanes", "1", "-c", "-o", object, source };
            uint32_t used = 7;
            if ((mode & 1) != 0)
            {
                arguments[used++] = "--verify";
            }
            if ((mode & 2) != 0)
            {
                arguments[used++] = "--scalar";
            }
            if ((mode & 4) != 0)
            {
                arguments[used++] = "-fstack-clash-protection";
            }
            arguments[used] = 0;
            const bool compiled = object_test_run(arguments, "/dev/null", errors) == 0;
            emitted_test_expect(test, compiled, "-c compiles the large frame with both paths, verification and optional probes", mode);
            const String8 bytes = compiled ? object_test_read_file(&test->arena, object) : (String8){ 0 };
            const Elf64_Shdr* header = 0;
            const uint8_t* cfi = bytes.pointer != 0 ? x86_elf_section((const uint8_t*)bytes.pointer, bytes.length, SHT_X86_64_UNWIND, ".eh_frame", &header) : 0;
            // The one FDE starts after the shared CIE. Its allocation event must have an exact ULEB128(2^31), not a
            // truncated offset or a smaller frame that would accidentally make a compile-only regression pass.
            const uint8_t wide_cfa[] = { 0x0e, 0x80, 0x80, 0x80, 0x80, 0x08 };
            bool found = false;
            for (uint64_t at = BUSTER_UNWIND_CIE_BYTES; cfi != 0 && at + sizeof(wide_cfa) <= header->sh_size; at += 1)
            {
                found |= memcmp(cfi + at, wide_cfa, sizeof(wide_cfa)) == 0;
            }
            emitted_test_expect(test, found, "the driver's object really describes a CFA of 2^31", mode);
            if ((mode & 3) == 0)
            {
                first = bytes;
            }
            else
            {
                emitted_test_expect(test, bytes.pointer != 0 && first.pointer != 0 && bytes.length == first.length && memcmp(bytes.pointer, first.pointer, first.length) == 0,
                                    "verification and scalar selection preserve the complete object bytes", mode);
            }
            if (!compiled && test->failures < 20)
            {
                const String8 message = object_test_read_file(&test->arena, errors);
                fprintf(stderr, "  %.*s", (int)message.length, message.pointer != 0 ? (const char*)message.pointer : "");
            }
        }
        unlink(source);
        unlink(object);
        unlink(errors);
        rmdir(directory);
        arena_pop_to(&test->arena, mark);
    }
}
