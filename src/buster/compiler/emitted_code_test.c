// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// test-emitted-code [--seed N] [--count N] [--cross-check] [--keep]: the tier-neutral emitted function (#1056,
// emitted_code.c) and the row programs it lowers frames to (unwind.c). Included after c_check_tier1.c. Checks:
//
// - row programs: every fixed descriptor test-unwind writes (each save mask with frames of every LEB width, frame
//   pointers', tail calls' and open ones), as its rows (unwind_function_rows), written by the row writer into the fixed
//   writer's bytes, as pairs and with one CIE, by the exact and the batch paths; and lowered back
//   (unwind_rows_function) to the same descriptor where it is the simple shape, otherwise to a row program of the
//   same bytes;
// - canonical rows: a closed frame's last epilogue's bracket (its remembered state and the restoring at the end) and
//   rows at the end dropped; and the row checker's refusals, one break at a time;
// - Tier 0: generated programs of test-unwind's shapes (calls, loops, alternate returns, objects, frame pointers,
//   realigned frames with and without a base, deep values), each function's frame events (emitted_from_tier0) resolved
//   after sizing into the descriptor the fixed reader gives (unwind_tier0_frame_tails, the previous path), byte for
//   byte; proved against the records (emitted_verify_frames); and each CFA and slot event's value moved by 8 and each
//   anchored one record early, which the verifier must refute;
// - Tier 1 frames: the planner's every flag (variadic, dynamic stack, kept pointer, no red zone, probes) at alignments
//   to 8192, sizes past two pages and no or all callee-saved registers, emitted with a body of a call, an early return
//   and a late one (closed) or a trap (open), with their events (tier1_frame_emit); proved; resolved into the descriptor
//   the previous Tier 1 adapter's fixed reader gives where it described the frame (no probes), byte for byte; and with
//   probes past a page without a frame pointer, the shape the fixed descriptor can't express (the CFA in r11 through
//   the loop): proved, lowered to a row program, the fixed reader refusing it, and the same mutations refuted, the loop's
//   exit rule anchored inside the loop and a single allocation's rule after it (what a fixed writer would say) among
//   them;
// - limits (emitted_test_limits): every save mask at and beyond the old signed 32-bit CFA edge (#1149), wide row
//   encoding and verifier arithmetic, Tier 1 probes and compile-only -c/--verify, and emitted_unwind_same's answers;
// - uncovered forms (emitted_test_uncovered): code through monitorx, whose conservative record may write rsp, unproven
//   where a rule fails on a path through it and refuted where one fails off its paths, though after an unproven one;
//   and the driver's --verify -c -march=znver5 of monitorx's and mwaitx's leaves, by both paths;
// - with --cross-check (GNU as and ld, gdb): that probe frame natively, its CFI the row writer's, gdb stopped at every
//   instruction of it, the probe loop's each time it runs, whose backtrace must reach the caller and whose caller's
//   frame must have its callee-saved registers and its rsp as they were (a third rule: rsp the CFA).

typedef struct EmittedTest EmittedTest;
struct EmittedTest
{
    uint64_t checks;
    uint64_t failures;
    uint64_t descriptors;
    uint64_t programs;
    uint64_t tier0_functions;
    uint64_t tier1_frames;
    uint64_t probe_frames;
    uint64_t row_programs;
    // Tier 0's functions that probe (#177, emitted_test_tier0_probes and every other round's programs).
    uint64_t tier0_probed;
    uint64_t mutants;
    uint64_t refuted;
    Arena arena;
    Arena scratch;
};

static bool emitted_test_expect(EmittedTest* test, bool condition, const char* check, uint64_t id)
{
    test->checks += 1;
    if (!condition)
    {
        if (test->failures < 20)
        {
            fprintf(stderr, "test-emitted-code: %s, case %" PRIu64 "\n", check, id);
        }
        test->failures += 1;
    }
    return condition;
}

// A descriptor's FDE written alone by the exact writer (`shared` with one CIE), into `bytes`: its size, 0 if refused.
static uint64_t emitted_test_write(const UnwindFunction* function, bool shared, bool exact, uint8_t* bytes, uint64_t capacity)
{
    uint32_t location = 0;
    UnwindOutput output = { .bytes = bytes, .capacity = capacity, .locations = &location, .location_capacity = 1, .shared = shared };
    return unwind_write(function, 1, &output, exact) == UNWIND_OK ? output.size : 0;
}

static bool emitted_test_same_bytes(const UnwindFunction* a, const UnwindFunction* b, bool shared)
{
    static uint8_t first[8192];
    static uint8_t second[8192];
    const uint64_t a_size = emitted_test_write(a, shared, true, first, sizeof(first));
    const uint64_t b_size = emitted_test_write(b, shared, true, second, sizeof(second));
    return a_size != 0 && a_size == b_size && memcmp(first, second, a_size) == 0;
}

// The fixed descriptors test-unwind writes (unwind_test_emitters'), into `functions`; returns how many.
static uint32_t emitted_test_descriptors(UnwindFunction* functions, uint32_t (*tail_events)[3 * 8])
{
    const uint32_t thresholds[] = { 128, 16384, 2097152, 268435456 };
    uint32_t count = 0;
    uint32_t tailed = 0;
    for (uint32_t saved = 0; saved < 64; saved += 1)
    {
        const uint32_t base = 8 + 8 * (uint32_t)__builtin_popcount(saved);
        functions[count++] = unwind_test_description(saved, 0);
        functions[count++] = unwind_test_description(saved, 1);
        functions[count++] = unwind_test_description(saved, INT32_MAX);
        for (uint32_t width = 0; width < BUSTER_ARRAY_LENGTH(thresholds); width += 1)
        {
            functions[count++] = unwind_test_description(saved, thresholds[width] - base - 1);
            functions[count++] = unwind_test_description(saved, thresholds[width] - base);
        }
        if ((saved & 2) == 0)
        {
            functions[count++] = unwind_test_pointer_description(saved);
        }
        const uint32_t frames[] = { 0, 16, 4096, thresholds[1] - base };
        functions[count++] = unwind_test_tail_description(saved, frames[saved % 4], false, saved % 3 + 1, tail_events[tailed++]);
        if ((saved & 2) == 0)
        {
            functions[count++] = unwind_test_tail_description(saved, 0, true, (saved + 1) % 3 + 1, tail_events[tailed++]);
        }
        functions[count++] = unwind_test_open_description(saved, frames[(saved + 1) % 4], false, saved % 4, tail_events[tailed++]);
        if ((saved & 2) == 0)
        {
            functions[count++] = unwind_test_open_description(saved, 0, true, (saved + 2) % 4, tail_events[tailed++]);
        }
    }
    return count;
}

#define BUSTER_EMITTED_TEST_ROWS 256

static void emitted_test_rows(EmittedTest* test)
{
    static UnwindFunction functions[64 * 16];
    static UnwindFunction programs[64 * 16];
    static uint32_t tail_events[64 * 4][3 * 8];
    static EmissionFrameRow rows[64 * 16][BUSTER_EMITTED_TEST_ROWS];
    const uint32_t count = emitted_test_descriptors(functions, tail_events);
    emitted_test_expect(test, unwind_check(functions, count) == UNWIND_OK, "the fixed descriptors are valid", 0);
    for (uint32_t f = 0; f < count; f += 1)
    {
        const UnwindFunction* function = &functions[f];
        const uint32_t row_count = unwind_function_rows(function, rows[f], BUSTER_EMITTED_TEST_ROWS);
        emitted_test_expect(test, row_count <= BUSTER_EMITTED_TEST_ROWS, "a descriptor's rows fit", f);
        programs[f] = (UnwindFunction){ .size = function->size, .open = BUSTER_UNWIND_ROWS, .rows = rows[f], .row_count = row_count };
        emitted_test_expect(test, unwind_check(&programs[f], 1) == UNWIND_OK, "a descriptor's rows are a valid row program", f);
        for (uint32_t shared = 0; shared < 2; shared += 1)
        {
            emitted_test_expect(test, emitted_test_same_bytes(function, &programs[f], shared != 0), "a row program is its fixed descriptor's bytes", f);
        }
        UnwindFunction lowered;
        const bool lowers = unwind_rows_function(rows[f], row_count, function->size, &lowered) == UNWIND_OK;
        emitted_test_expect(test, lowers && emitted_test_same_bytes(function, &lowered, false), "rows lowered back write the descriptor's bytes", f);
        const bool simple = function->pointer == 0 && function->tail_count == 0 && function->open == 0;
        const bool fixed = lowers && lowered.open != BUSTER_UNWIND_ROWS && lowered.size == function->size && lowered.frame_size == function->frame_size &&
                           lowered.saved == function->saved && lowered.event_count == function->event_count &&
                           memcmp(lowered.offsets, function->offsets, sizeof(lowered.offsets)) == 0;
        emitted_test_expect(test, simple ? fixed : lowers && lowered.open == BUSTER_UNWIND_ROWS, "the simple shape lowers to its fixed descriptor, the others to rows", f);
        test->descriptors += 1;
    }
    // The whole batch as row programs, by the batch path (which leaves them to the exact writer) and the exact path: the
    // fixed descriptors' bytes.
    static uint8_t bytes[3][64 * 16 * 384];
    static uint32_t locations[3][64 * 16];
    for (uint32_t shared = 0; shared < 2; shared += 1)
    {
        UnwindOutput outputs[3];
        for (uint32_t way = 0; way < 3; way += 1)
        {
            outputs[way] = (UnwindOutput){ .bytes = bytes[way], .capacity = sizeof(bytes[way]), .locations = locations[way], .location_capacity = count, .shared = shared != 0 };
            memset(bytes[way], 0x5a, sizeof(bytes[way]));
        }
        const bool written = unwind_write(functions, count, &outputs[0], true) == UNWIND_OK && unwind_write(programs, count, &outputs[1], true) == UNWIND_OK &&
                             unwind_write(programs, count, &outputs[2], false) == UNWIND_OK;
        emitted_test_expect(test, written && outputs[0].size == outputs[1].size && outputs[1].size == outputs[2].size &&
                                      memcmp(bytes[0], bytes[1], outputs[0].size) == 0 && memcmp(bytes[1], bytes[2], outputs[0].size) == 0 &&
                                      memcmp(locations[0], locations[1], count * 4) == 0 && memcmp(locations[1], locations[2], count * 4) == 0,
                            "a batch of row programs is the fixed batch's bytes by either writer", shared);
    }
    // Canonical form: a closed frame's last epilogue bracketed as a frame emitter brackets every epilogue (its state
    // remembered before its first row, restored at the end), and a rule at the end: both dropped, the rest as it was.
    for (uint32_t f = 0; f < count; f += 1)
    {
        const UnwindFunction* function = &functions[f];
        const uint32_t row_count = programs[f].row_count;
        if (function->open != 0 || programs[f].open != BUSTER_UNWIND_ROWS || row_count == 0 || row_count + 3 > BUSTER_EMITTED_TEST_ROWS)
        {
            continue;
        }
        // The epilogue's first row: past the tails' brackets, the first after the prologue's transitions' offsets.
        const uint32_t epilogue_offset = function->offsets[unwind_prologue_events(function)];
        uint32_t first = row_count;
        for (uint32_t r = row_count; r > 0 && rows[f][r - 1].offset >= epilogue_offset; r -= 1)
        {
            first = r - 1;
        }
        if (first == row_count || function->event_count == unwind_prologue_events(function))
        {
            continue;
        }
        EmissionFrameRow bracketed[BUSTER_EMITTED_TEST_ROWS];
        memcpy(bracketed, rows[f], first * sizeof(EmissionFrameRow));
        bracketed[first] = (EmissionFrameRow){ .offset = first != 0 ? rows[f][first - 1].offset : 0, .kind = EMISSION_FRAME_REMEMBER_STATE };
        memcpy(bracketed + first + 1, rows[f] + first, (row_count - first) * sizeof(EmissionFrameRow));
        bracketed[row_count + 1] = (EmissionFrameRow){ .offset = function->size, .kind = EMISSION_FRAME_RESTORE_STATE };
        bracketed[row_count + 2] = (EmissionFrameRow){ .offset = function->size, .kind = EMISSION_FRAME_CFA_OFFSET, .value = 8 };
        const uint32_t canonical = unwind_rows_canonical(bracketed, row_count + 3, function->size);
        emitted_test_expect(test, canonical == row_count && memcmp(bracketed, rows[f], row_count * sizeof(EmissionFrameRow)) == 0,
                            "the last epilogue's bracket and the rows at the end are dropped", f);
    }
    // The row checker's refusals, each break alone on a frame's rows.
    const UnwindFunction* sample = &functions[64 * 0 + 3];
    EmissionFrameRow broken[BUSTER_EMITTED_TEST_ROWS];
    const uint32_t sample_count = unwind_function_rows(sample, broken, BUSTER_EMITTED_TEST_ROWS);
    struct
    {
        uint32_t row;
        EmissionFrameRow value;
        UnwindError error;
    } breaks[] = {
        { 1, { .offset = 0, .kind = EMISSION_FRAME_CFA_OFFSET, .value = 16 }, UNWIND_ERROR_INVALID },
        { 0, { .offset = UINT32_MAX, .kind = EMISSION_FRAME_CFA_OFFSET, .value = 16 }, UNWIND_ERROR_INVALID },
        { 0, { .offset = 1, .kind = EMISSION_FRAME_OFFSET, .reg = 3, .value = -12 }, UNWIND_ERROR_UNSUPPORTED },
        { 0, { .offset = 1, .kind = EMISSION_FRAME_CFA_OFFSET, .value = -8 }, UNWIND_ERROR_UNSUPPORTED },
        { 0, { .offset = 1, .kind = 99 }, UNWIND_ERROR_INVALID },
        { 0, { .offset = 1, .kind = EMISSION_FRAME_RESTORE_STATE }, UNWIND_ERROR_INVALID },
        { 0, { .offset = 1, .kind = EMISSION_FRAME_OFFSET, .reg = BUSTER_EMISSION_X86_RSP, .value = -16 }, UNWIND_ERROR_UNSUPPORTED },
        // Past the general registers, DWARF's 33 names none a frame saves (#147: xmm0 to xmm15 are 17 to 32); a vector
        // register's rule is a slot or its caller's value, never the CIE's (DW_CFA_restore's byte) or another register's.
        { 0, { .offset = 1, .kind = EMISSION_FRAME_OFFSET, .reg = 33, .value = -16 }, UNWIND_ERROR_UNSUPPORTED },
        { 0, { .offset = 1, .kind = EMISSION_FRAME_RESTORE, .reg = 17 }, UNWIND_ERROR_UNSUPPORTED },
        { 0, { .offset = 1, .kind = EMISSION_FRAME_REGISTER, .reg = 17, .value = 3 }, UNWIND_ERROR_UNSUPPORTED },
        { 0, { .offset = 1, .kind = EMISSION_FRAME_CFA, .reg = 16, .value = 8 }, UNWIND_ERROR_UNSUPPORTED },
        { 0, { .offset = 1, .kind = EMISSION_FRAME_CFA_OFFSET, .value = 16, .reserved = 1 }, UNWIND_ERROR_INVALID },
    };
    for (uint32_t k = 0; sample_count != 0 && k < BUSTER_ARRAY_LENGTH(breaks); k += 1)
    {
        EmissionFrameRow mutated[BUSTER_EMITTED_TEST_ROWS];
        memcpy(mutated, broken, sample_count * sizeof(EmissionFrameRow));
        mutated[breaks[k].row] = breaks[k].value;
        const UnwindFunction program = { .size = sample->size, .open = BUSTER_UNWIND_ROWS, .rows = mutated, .row_count = sample_count };
        emitted_test_expect(test, unwind_check(&program, 1) == breaks[k].error, "the row checker refuses a break", k);
    }
    EmissionFrameRow deep[BUSTER_UNWIND_ROW_STATES + 1];
    for (uint32_t k = 0; k < BUSTER_ARRAY_LENGTH(deep); k += 1)
    {
        deep[k] = (EmissionFrameRow){ .offset = 0, .kind = EMISSION_FRAME_REMEMBER_STATE };
    }
    const UnwindFunction nested = { .size = 4, .open = BUSTER_UNWIND_ROWS, .rows = deep, .row_count = BUSTER_ARRAY_LENGTH(deep) };
    emitted_test_expect(test, unwind_check(&nested, 1) == UNWIND_ERROR_UNSUPPORTED, "states remembered past the writer's depth are refused", 0);
}

// A function's code sized and encoded: each record's start, the length at the record count, and the bytes.
typedef struct EmittedTestLayout EmittedTestLayout;
struct EmittedTestLayout
{
    uint32_t* offsets;
    uint32_t* ends;
    uint8_t* bytes;
    uint32_t length;
};

static bool emitted_test_layout(const X86Instruction* records, uint32_t count, uint32_t* branches, uint32_t* targets, uint32_t branch_count, uint32_t references,
                                Arena* arena, EmittedTestLayout* layout)
{
    layout->offsets = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)count + 1);
    layout->ends = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, BUSTER_X86_ENCODER_FIXUP_CAPACITY(references));
    const uint64_t capacity = BUSTER_X86_ENCODER_CAPACITY(count);
    layout->bytes = arena_push(arena, capacity, 16);
    bool valid = layout->offsets != 0 && layout->ends != 0 && layout->bytes != 0 && count != 0;
    const BranchSizing sizing = valid ? size_branches((X86Instruction*)records, count, branches, targets, branch_count, layout->offsets) : (BranchSizing){ .error = BRANCH_SIZING_ERROR_TOO_MANY_INSTRUCTIONS };
    valid = valid && sizing.error == BRANCH_SIZING_OK && sizing.length <= INT32_MAX;
    const int64_t encoded = valid ? x86_encode(records, count, layout->bytes, capacity, 0, layout->ends) : -1;
    valid = valid && encoded > 0 && (uint64_t)encoded == sizing.length;
    layout->length = valid ? (uint32_t)encoded : 0;
    if (valid)
    {
        layout->offsets[count] = layout->length;
    }
    return valid;
}

// Mutants of a function's frame events, each of which the verifier must not accept: each CFA, CFA offset and slot event
// before the end (at most `limit` of them) with its value moved by 8, and anchored a record early where that keeps
// the events in order.
static void emitted_test_mutants(EmittedTest* test, const EmittedCode* code, uint32_t limit, uint64_t id)
{
    EmissionFrameEvent* events = code->frame_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(&test->arena, EmissionFrameEvent, code->frame_count) : 0;
    uint32_t tried = 0;
    for (uint32_t e = 0; events != 0 && e < code->frame_count && tried < limit; e += 1)
    {
        const EmissionFrameEvent event = code->frames[e];
        const bool mutable = (event.kind == EMISSION_FRAME_CFA || event.kind == EMISSION_FRAME_CFA_OFFSET || event.kind == EMISSION_FRAME_OFFSET) &&
                             event.at.boundary < code->count;
        for (uint32_t way = 0; mutable && way < 2; way += 1)
        {
            memcpy(events, code->frames, code->frame_count * sizeof(EmissionFrameEvent));
            const bool earlier_fits = event.at.boundary > 0 && (e == 0 || code->frames[e - 1].at.boundary < event.at.boundary);
            if (way == 0)
            {
                events[e].value += event.kind == EMISSION_FRAME_OFFSET ? -8 : 8;
            }
            else if (earlier_fits)
            {
                events[e].at.boundary -= 1;
            }
            else
            {
                continue;
            }
            EmittedCode mutant = *code;
            mutant.frames = events;
            const EmittedVerdict verdict = emitted_verify_frames(&mutant, &test->arena);
            test->mutants += 1;
            const bool refuted = verdict.error == EMITTED_VERIFY_REFUTED || verdict.error == EMITTED_VERIFY_INVALID;
            test->refuted += refuted;
            emitted_test_expect(test, refuted, way == 0 ? "a frame event's value moved is refuted" : "a frame event anchored early is refuted",
                                id * 1024 + e);
        }
        tried += mutable;
    }
}

// A Tier 0 function: its frame events (emitted_from_tier0) resolved after sizing are the fixed reader's descriptor,
// byte for byte; they are its records' (emitted_verify_frames); and their mutants aren't. A probed reservation's loop
// without a frame pointer (#177) has its CFA in r11 through the loop, which no fixed descriptor says: its events resolve
// into a row program, which the verifier proves and whose mutants it refutes. Both paths compile the same code.
static void emitted_test_tier0_function(EmittedTest* test, const Tier0Function* function, uint64_t id)
{
    Arena* arena = &test->arena;
    const uint64_t mark = arena->length;
    const Tier0Code code = tier0_validate(function).error == TIER0_OK ? tier0_compile(function, arena) : (Tier0Code){ .error = TIER0_ERROR_INVALID };
    const Tier0Code twin = code.error == TIER0_OK ? tier0_compile_scalar(function, arena) : (Tier0Code){ .error = TIER0_ERROR_INVALID };
    emitted_test_expect(test, code.error != TIER0_OK || (twin.error == TIER0_OK && twin.count == code.count && twin.branch_count == code.branch_count &&
                                                         memcmp(twin.records, code.records, code.count * sizeof(X86Instruction)) == 0 &&
                                                         (code.branch_count == 0 || (memcmp(twin.branches, code.branches, code.branch_count * sizeof(uint32_t)) == 0 &&
                                                                                     memcmp(twin.targets, code.targets, code.branch_count * sizeof(uint32_t)) == 0))),
                        "Tier 0's paths compile the same code", id);
    const bool r11 = code.error == TIER0_OK && !code.frame_pointer && (code.frame_size & ~(BUSTER_TIER0_PAGE_BYTES - 1)) != 0 &&
                     tier0_probes_reservation(code.frame_probe, code.frame_size, code.frame_alignment_log2, false);
    EmittedTestLayout layout;
    bool valid = emitted_test_expect(test, code.error == TIER0_OK, "Tier 0 compiles the generated function", id) &&
                 emitted_test_expect(test, emitted_test_layout(code.records, code.count, code.branches, code.targets, code.branch_count, code.reference_count, arena, &layout),
                                     "the function sizes and encodes", id);
    const uint64_t tail_room = valid && code.tail_count != 0 ? unwind_tier0_tail_room(&code) : 0;
    uint32_t* tails = tail_room != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, tail_room) : 0;
    UnwindFunction previous;
    valid = valid && unwind_tier0_frame_tails(&code, layout.offsets, layout.length, &previous, tails) == UNWIND_OK;
    emitted_test_expect(test, valid, "the fixed reader takes Tier 0's frame", id);
    const EmittedCode emitted = valid ? emitted_from_tier0(&code, true, false, false, arena) : (EmittedCode){ 0 };
    valid = valid && emitted_test_expect(test, emitted.frame_status == EMITTED_FRAME_DESCRIBED, "Tier 0's adapter describes the frame", id);
    EmissionFrameRow* rows = valid && emitted.frame_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, EmissionFrameRow, emitted.frame_count) : 0;
    UnwindFunction resolved;
    for (uint32_t scalar = 0; valid && scalar < 2; scalar += 1)
    {
        const bool lowered = emitted_unwind(&emitted, layout.offsets, layout.length, scalar != 0, rows, &resolved) == UNWIND_OK;
        const bool same = r11 ? resolved.open == BUSTER_UNWIND_ROWS : emitted_unwind_same(&previous, &resolved, arena) == EMITTED_SAME && emitted_test_same_bytes(&previous, &resolved, true);
        emitted_test_expect(test, lowered && same, r11 ? "a probed frame's loop without a frame pointer resolves into a row program" : "the resolved frame events are the fixed reader's descriptor", id);
    }
    if (valid)
    {
        const EmittedVerdict verdict = emitted_verify_frames(&emitted, arena);
        emitted_test_expect(test, verdict.error == EMITTED_VERIFY_OK, "Tier 0's frame events are its records'", id);
        if (verdict.error != EMITTED_VERIFY_OK && test->failures < 20)
        {
            fprintf(stderr, "  verdict %u at record %u, event %u of %u (records %u)\n", verdict.error, verdict.record, verdict.event, emitted.frame_count, emitted.count);
        }
        emitted_test_mutants(test, &emitted, 6, id);
        test->tier0_functions += 1;
        test->row_programs += resolved.open == BUSTER_UNWIND_ROWS;
        test->tier0_probed += code.frame_probe;
    }
    arena_pop_to(arena, mark);
}

// Tier 0's probed frames (#177, -fstack-clash-protection): a function of one address-taken object of each size (a page's
// multiple, past a page, below one) and alignment (16, 64, two pages), with a frame pointer and without, its prologue a
// probed reservation and realignment (tier0_emit_probed_reservation, tier0_emit_realignment), through
// emitted_test_tier0_function: its events proved, the loop's without a frame pointer a row program's.
static void emitted_test_tier0_probes(EmittedTest* test)
{
    static const uint32_t sizes[] = { 3 * BUSTER_TIER0_PAGE_BYTES, BUSTER_TIER0_PAGE_BYTES + 904, 100 };
    static const uint8_t alignments[] = { 4, 6, 13 };
    static const uint8_t kinds[3] = { BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_DISCARD, BUSTER_TYPED_RETURN };
    static const uint8_t classes[3] = { BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_VOID };
    static const uint64_t values[3] = { 0, 0, 0 };
    static const uint8_t local_classes[1] = { BUSTER_TYPED_CLASS_VOID };
    static const uint8_t local_flags[1] = { BUSTER_TYPED_LOCAL_ADDRESS_TAKEN };
    for (uint32_t s = 0; s < BUSTER_ARRAY_LENGTH(sizes); s += 1)
    {
        for (uint32_t a = 0; a < BUSTER_ARRAY_LENGTH(alignments); a += 1)
        {
            for (uint32_t pointer = 0; pointer < 2; pointer += 1)
            {
                const uint32_t local_sizes[1] = { sizes[s] };
                const uint8_t local_alignments[1] = { alignments[a] };
                Tier0Function function;
                memset(&function, 0, sizeof(function));
                function.kinds = kinds;
                function.classes = classes;
                function.values = values;
                function.count = 3;
                function.local_classes = local_classes;
                function.local_flags = local_flags;
                function.local_sizes = local_sizes;
                function.local_alignments = local_alignments;
                function.local_count = 1;
                function.frame_pointer = pointer != 0 ? BUSTER_TIER0_FRAME_POINTER_ALL : BUSTER_TIER0_FRAME_POINTER_NONE;
                function.probe_stack = true;
                emitted_test_tier0_function(test, &function, 0x50000 + s * 16 + a * 2 + pointer);
            }
        }
    }
}

// Whether every register a preserving frame keeps (#147, Tier0Code.frame_preserved, frame_preserve_vectors and
// frame_preserve_mask_bytes) has a rule at every boundary of its events: its caller's value from the entry, its slot from
// its save, its own value again from its reload (emitted_tier0_preserve_rows), each once, and never the CIE's; and no other
// register past the general ones has one.
static bool emitted_test_preserve_complete(const EmittedCode* emitted, const Tier0Code* code)
{
    static const uint8_t dwarf[16] = { 0, 2, 1, 3, 7, 6, 4, 5, 8, 9, 10, 11, 12, 13, 14, 15 };
    uint8_t kept[BUSTER_EMISSION_X86_DWARF_REGISTERS] = { 0 };
    uint8_t seen[BUSTER_EMISSION_X86_DWARF_REGISTERS] = { 0 };
    for (uint32_t r = 0; r < 16; r += 1)
    {
        kept[dwarf[r]] = (code->frame_preserved >> r & 1) != 0;
    }
    for (uint32_t r = 0; r < 32; r += 1)
    {
        kept[r < 16 ? 17 + r : 67 + (r - 16)] = (code->frame_preserve_vectors >> r & 1) != 0;
    }
    for (uint32_t k = 0; code->frame_preserve_mask_bytes != 0 && k < 8; k += 1)
    {
        kept[118 + k] = 1;
    }
    bool complete = true;
    for (uint32_t e = 0; complete && e < emitted->frame_count; e += 1)
    {
        const EmissionFrameEvent* event = &emitted->frames[e];
        const uint32_t reg = event->reg;
        const bool rule = event->kind != EMISSION_FRAME_CFA && event->kind != EMISSION_FRAME_CFA_OFFSET && event->kind != EMISSION_FRAME_REMEMBER_STATE &&
                          event->kind != EMISSION_FRAME_RESTORE_STATE;
        if (rule && reg < BUSTER_EMISSION_X86_DWARF_REGISTERS && kept[reg])
        {
            // Its caller's value at the entry, its slot, then its own value: in that order.
            const uint8_t expected = seen[reg] == 1 ? EMISSION_FRAME_OFFSET : EMISSION_FRAME_SAME_VALUE;
            complete = seen[reg] < 3 && event->kind == expected && (seen[reg] != 0 || event->at.boundary == 0);
            seen[reg] += 1;
        }
        complete = complete && (!rule || reg < 16 || reg == BUSTER_EMISSION_X86_RETURN || kept[reg]);
    }
    for (uint32_t r = 0; complete && r < BUSTER_EMISSION_X86_DWARF_REGISTERS; r += 1)
    {
        complete = !kept[r] || seen[r] == 3;
    }
    return complete;
}

// Tier 0's preserving frames (#147, Tier0Function.preserve_general and the rest), each convention's registers (preserve_most's,
// preserve_all's of 16 and 32 bytes, and every register's of 64 bytes with the masks, no_caller_saved_registers' set) in a
// function of one address-taken object of each size (below a page, past one) and alignment (16, 64: a realigned frame),
// probed and not. Both paths compile the same code; the fixed reader takes its frame; its events resolve, describe every
// kept register at every boundary (emitted_test_preserve_complete) and are its records' (emitted_verify_frames, the vector
// and mask rules by emitted_verify_saved_vectors); and their mutants aren't.
static void emitted_test_tier0_preserving(EmittedTest* test)
{
    static const uint32_t sizes[] = { 100, BUSTER_TIER0_PAGE_BYTES + 904 };
    static const uint8_t alignments[] = { 4, 6 };
    static const uint8_t kinds[3] = { BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_DISCARD, BUSTER_TYPED_RETURN };
    static const uint8_t classes[3] = { BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_VOID };
    static const uint64_t values[3] = { 0, 0, 0 };
    static const uint8_t local_classes[1] = { BUSTER_TYPED_CLASS_VOID };
    static const uint8_t local_flags[1] = { BUSTER_TYPED_LOCAL_ADDRESS_TAKEN };
    const uint16_t most = (uint16_t)(1u << X86_REGISTER_RAX | 1u << X86_REGISTER_RCX | 1u << X86_REGISTER_RDX | 1u << X86_REGISTER_RSI | 1u << X86_REGISTER_RDI |
                                     1u << X86_REGISTER_R8 | 1u << X86_REGISTER_R9 | 1u << X86_REGISTER_R10);
    const struct
    {
        uint16_t general;
        uint8_t vectors;
        uint8_t bytes;
        uint8_t masks;
    } conventions[] = {
        { most, 0, 0, 0 },
        { most, 16, 16, 0 },
        { most, 16, 32, 0 },
        { (uint16_t)(most | 1u << X86_REGISTER_R11), 32, 64, 8 },
    };
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(conventions); c += 1)
    {
        for (uint32_t s = 0; s < BUSTER_ARRAY_LENGTH(sizes); s += 1)
        {
            for (uint32_t a = 0; a < BUSTER_ARRAY_LENGTH(alignments); a += 1)
            {
                for (uint32_t probe = 0; probe < 2; probe += 1)
                {
                    const uint64_t id = 0x60000 + c * 64 + s * 16 + a * 2 + probe;
                    const uint32_t local_sizes[1] = { sizes[s] };
                    const uint8_t local_alignments[1] = { alignments[a] };
                    Tier0Function function;
                    memset(&function, 0, sizeof(function));
                    function.kinds = kinds;
                    function.classes = classes;
                    function.values = values;
                    function.count = 3;
                    function.local_classes = local_classes;
                    function.local_flags = local_flags;
                    function.local_sizes = local_sizes;
                    function.local_alignments = local_alignments;
                    function.local_count = 1;
                    function.probe_stack = probe != 0;
                    function.preserve_general = conventions[c].general;
                    function.preserve_vectors = conventions[c].vectors;
                    function.preserve_vector_bytes = conventions[c].bytes;
                    function.preserve_mask_bytes = conventions[c].masks;
                    Arena* arena = &test->arena;
                    const uint64_t mark = arena->length;
                    const Tier0Code code = tier0_validate(&function).error == TIER0_OK ? tier0_compile(&function, arena) : (Tier0Code){ .error = TIER0_ERROR_INVALID };
                    const Tier0Code twin = code.error == TIER0_OK ? tier0_compile_scalar(&function, arena) : (Tier0Code){ .error = TIER0_ERROR_INVALID };
                    bool valid = emitted_test_expect(test, code.error == TIER0_OK && twin.error == TIER0_OK && twin.count == code.count &&
                                                               memcmp(twin.records, code.records, code.count * sizeof(X86Instruction)) == 0,
                                                     "Tier 0's paths compile the same preserving frame", id);
                    EmittedTestLayout layout;
                    valid = valid && emitted_test_expect(test, emitted_test_layout(code.records, code.count, code.branches, code.targets, code.branch_count,
                                                                                   code.reference_count, arena, &layout),
                                                         "the preserving function sizes and encodes", id);
                    UnwindFunction fixed;
                    valid = valid && emitted_test_expect(test, unwind_tier0_frame_tails(&code, layout.offsets, layout.length, &fixed, 0) == UNWIND_OK,
                                                         "the fixed reader takes a preserving frame", id);
                    const EmittedCode emitted = valid ? emitted_from_tier0(&code, true, false, false, arena) : (EmittedCode){ 0 };
                    valid = valid && emitted_test_expect(test, emitted.frame_status == EMITTED_FRAME_DESCRIBED, "Tier 0's adapter describes a preserving frame", id);
                    EmissionFrameRow* rows = valid && emitted.frame_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, EmissionFrameRow, emitted.frame_count) : 0;
                    UnwindFunction resolved;
                    valid = valid && emitted_test_expect(test, rows != 0 && emitted_unwind(&emitted, layout.offsets, layout.length, false, rows, &resolved) == UNWIND_OK,
                                                         "a preserving frame's events resolve", id);
                    valid = valid && emitted_test_expect(test, emitted_test_preserve_complete(&emitted, &code),
                                                         "a preserving frame describes every kept register at every boundary", id);
                    if (valid)
                    {
                        const EmittedVerdict verdict = emitted_verify_frames(&emitted, arena);
                        emitted_test_expect(test, verdict.error == EMITTED_VERIFY_OK, "a preserving frame's events are its records'", id);
                        emitted_test_mutants(test, &emitted, 6, id);
                        test->tier0_functions += 1;
                    }
                    arena_pop_to(arena, mark);
                }
            }
        }
    }
}

static void emitted_test_tier0(EmittedTest* test, uint64_t seed, uint32_t rounds)
{
    static const Tier0ProgramOptions options[] = {
        { .functions = 3, .statements = 12, .expression_nodes = 10, .calls = true, .loops = true },
        { .functions = 8, .statements = 24, .expression_nodes = 20, .calls = true, .loops = true, .floating = true, .pointers = true, .objects = true, .indirect = true },
        { .functions = 8, .statements = 24, .expression_nodes = 20, .calls = true, .loops = true, .floating = true, .pointers = true, .objects = true, .indirect = true,
          .access = true, .vlas = true, .unsigned_floating = true },
    };
    Tier0Tables tables = { 0 };
    const bool opened = tier0_tables_open(&tables);
    emitted_test_expect(test, opened, "type and ABI tables", 0);
    const uint32_t shapes = BUSTER_ARRAY_LENGTH(options) + 2;
    for (uint32_t round = 0; opened && round < rounds; round += 1)
    {
        for (uint32_t shape = 0; shape < shapes; shape += 1)
        {
            Tier0ProgramOptions shape_options = options[shape < BUSTER_ARRAY_LENGTH(options) ? shape : BUSTER_ARRAY_LENGTH(options) - 1];
            shape_options.deep = shape >= BUSTER_ARRAY_LENGTH(options);
            shape_options.over_aligned = shape == BUSTER_ARRAY_LENGTH(options) + 1;
            const uint64_t mark = test->arena.length;
            const uint64_t id = seed + round * 7919 + shape * 1000003ull;
            Tier0Program* program = BUSTER_ARENA_PUSH_ARRAY(&test->arena, Tier0Program, 1);
            Tier0Functions* functions = BUSTER_ARENA_PUSH_ARRAY(&test->arena, Tier0Functions, 1);
            const bool generated = program != 0 && functions != 0 && tier0_generate_program(program, &test->arena, id, shape_options, "emitted_") &&
                                   tier0_program_functions(program, &tables, &test->arena, functions);
            emitted_test_expect(test, generated, "typed program generation", id);
            for (uint32_t f = 0; generated && f < functions->count; f += 1)
            {
                // Every other round's programs probe their frames and allocations (#177, -fstack-clash-protection).
                functions->functions[f].probe_stack = round % 2 == 1;
                emitted_test_tier0_function(test, &functions->functions[f], id * 64 + f);
            }
            test->programs += generated;
            arena_pop_to(&test->arena, mark);
        }
    }
    tier0_tables_close(&tables);
    emitted_test_tier0_probes(test);
    emitted_test_tier0_preserving(test);
}

// A Tier 1 frame's function: its prologue (with its events), a body that clobbers the saved registers, moves rsp with
// `dynamic` and an opmask register with `opmask`, calls a function (through a reference) and branches to a late block or
// falls to an early return, and the late block either returns (closed) or calls and traps (open), each return after its
// epilogue (with its events). Its records, columns, events and the exits' rets in `out`; false if the frame emitter
// refuses the frame.
typedef struct EmittedTestFrame EmittedTestFrame;
struct EmittedTestFrame
{
    X86Instruction records[BUSTER_TIER1_FRAME_EMIT_RECORDS * 3 + 64];
    uint32_t branches[BUSTER_TIER1_FRAME_EMIT_BRANCHES * 3 + 8];
    uint32_t targets[BUSTER_TIER1_FRAME_EMIT_BRANCHES * 3 + 8];
    EmissionFrameEvent events[BUSTER_TIER1_FRAME_EMIT_EVENTS * 3];
    uint32_t exits[2];
    LinkReference references[2];
    Tier1EmitBuffer buffer;
    uint32_t exit_count;
    uint32_t reference_count;
};

static bool emitted_test_frame_code(const Tier1Frame* frame, uint32_t used, bool open, bool dynamic, bool opmask, EmittedTestFrame* out)
{
    out->buffer = (Tier1EmitBuffer){
        .records = out->records,
        .capacity = BUSTER_ARRAY_LENGTH(out->records),
        .branches = out->branches,
        .targets = out->targets,
        .branch_capacity = BUSTER_ARRAY_LENGTH(out->branches),
        .events = out->events,
        .event_capacity = BUSTER_ARRAY_LENGTH(out->events),
    };
    out->exit_count = 0;
    out->reference_count = 0;
    Tier1EmitBuffer* buffer = &out->buffer;
    bool valid = tier1_frame_emit(frame, false, buffer) == TIER1_FRAME_EMIT_OK;
    for (uint32_t reg = 0; valid && reg < 16; reg += 1)
    {
        if ((used & (1u << reg)) != 0)
        {
            tier1_frame_emit_add(buffer, tier1_frame_emit_immediate(X86_FORM_MOV_RM64_IMM32, reg, 0xdead));
        }
    }
    if (valid && dynamic)
    {
        tier1_frame_emit_add(buffer, tier1_frame_emit_immediate(X86_FORM_SUB_RM64_IMM32, X86_REGISTER_RSP, 512));
    }
    // An opmask register's move, which writes no general register (x86_instruction_resolve's opmask column): AVX-512's
    // masks in a frame whose CFA is rsp's (#179's code under --verify). Not in the native check's code, which any host runs.
    if (valid && opmask)
    {
        tier1_frame_emit_add(buffer, tier1_frame_emit_register(X86_FORM_KMOVQ_K_K, 1, 2));
    }
    if (valid)
    {
        out->references[out->reference_count++] = (LinkReference){ .instruction = buffer->count, .field = X86_FIXUP_DISPLACEMENT, .kind = BUSTER_LINK_FIXUP_CALL, .target = 1 };
        tier1_frame_emit_add(buffer, (X86Instruction){ .form = X86_FORM_CALL_REL32 | BUSTER_X86_FORM_FIXUP });
        tier1_frame_emit_add(buffer, tier1_frame_emit_register(X86_FORM_TEST_RM64_R64, X86_REGISTER_RAX, X86_REGISTER_RAX));
    }
    const uint32_t late = valid ? tier1_frame_emit_branch(buffer, X86_FORM_JNE_REL8, 0) : 0;
    valid = valid && tier1_frame_emit(frame, true, buffer) == TIER1_FRAME_EMIT_OK;
    if (valid)
    {
        out->exits[out->exit_count++] = buffer->count;
        tier1_frame_emit_add(buffer, (X86Instruction){ .form = X86_FORM_RET });
        buffer->targets[late] = buffer->count;
        tier1_frame_emit_add(buffer, tier1_frame_emit_immediate(X86_FORM_MOV_RM64_IMM32, X86_REGISTER_RAX, 1));
    }
    if (valid && open)
    {
        out->references[out->reference_count++] = (LinkReference){ .instruction = buffer->count, .field = X86_FIXUP_DISPLACEMENT, .kind = BUSTER_LINK_FIXUP_CALL, .target = 1 };
        tier1_frame_emit_add(buffer, (X86Instruction){ .form = X86_FORM_CALL_REL32 | BUSTER_X86_FORM_FIXUP });
        tier1_frame_emit_add(buffer, (X86Instruction){ .form = X86_FORM_UD2 });
    }
    else if (valid)
    {
        valid = tier1_frame_emit(frame, true, buffer) == TIER1_FRAME_EMIT_OK;
        out->exits[out->exit_count++] = buffer->count;
        tier1_frame_emit_add(buffer, (X86Instruction){ .form = X86_FORM_RET });
    }
    return valid;
}

// A new directory for a test's files under $TMPDIR (or /tmp), as the other tests make theirs: `name` with mkdtemp's
// six random characters, in `directory` (`size` bytes). False where the name doesn't fit or the directory isn't made.
static bool emitted_test_directory(char* directory, uint64_t size, const char* name)
{
    const char* temporary = getenv("TMPDIR");
    const int written = snprintf(directory, size, "%s/%s-XXXXXX", temporary != 0 && temporary[0] != 0 ? temporary : "/tmp", name);
    return written > 0 && (uint64_t)written < size && mkdtemp(directory) != 0;
}

#include "emitted_code_limits_test.c"

// The widened frame range and emitted_unwind_same's three answers: same CFI, different CFI and insufficient scratch.
static void emitted_test_limits(EmittedTest* test)
{
    emitted_test_large_fixed(test);
    emitted_test_wide_rows(test);
    emitted_test_wide_tracking(test);
    emitted_test_large_tier1(test);
    emitted_test_large_driver(test);
    const UnwindFunction pushed = { .size = 16, .saved = 1 };
    const UnwindFunction other = { .size = 16, .saved = 2 };
    Arena full = test->scratch;
    full.capacity = full.length;
    emitted_test_expect(test, emitted_unwind_same(&pushed, &pushed, &test->scratch) == EMITTED_SAME, "a fixed frame is its own CFI", 0);
    emitted_test_expect(test, emitted_unwind_same(&pushed, &other, &test->scratch) == EMITTED_DIFFERENT, "frames of other pushes are other CFI", 0);
    emitted_test_expect(test, emitted_unwind_same(&pushed, &pushed, &full) == EMITTED_SAME_MEMORY && full.length == test->scratch.length,
                        "without room for a fixed frame's rows, the comparison is a resource failure", 0);
}

// One case of emitted_test_uncovered: `count` records with a branch from `branch` to `target` (UINT32_MAX for none) and
// no frame events (the CIE's rules), whose verdict must be `expected` at `record`.
static void emitted_test_uncovered_case(EmittedTest* test, X86Instruction* records, uint32_t count, uint32_t branch, uint32_t target, EmittedVerifyError expected,
                                        uint32_t record, const char* check, uint64_t id)
{
    uint32_t branches[1] = { branch };
    uint32_t targets[1] = { target };
    const EmittedCode code = {
        .records = records,
        .count = count,
        .branches = branches,
        .targets = targets,
        .branch_count = branch != UINT32_MAX ? 1 : 0,
        .frame_status = EMITTED_FRAME_DESCRIBED,
    };
    const EmittedVerdict verdict = emitted_verify_frames(&code, &test->arena);
    emitted_test_expect(test, verdict.error == expected && (expected == EMITTED_VERIFY_OK || verdict.record == record), check, id);
}

// The verifier on `count` records, with `branch_count` branches (ascending by record) and `frame_count` events, whose
// verdict must be `expected` at `record`.
static void emitted_test_verify_case(EmittedTest* test, X86Instruction* records, uint32_t count, uint32_t* branches, uint32_t* targets, uint32_t branch_count,
                                     EmissionFrameEvent* frames, uint32_t frame_count, EmittedVerifyError expected, uint32_t record, const char* check, uint64_t id)
{
    const EmittedCode code = {
        .records = records,
        .count = count,
        .branches = branches,
        .targets = targets,
        .branch_count = branch_count,
        .frames = frames,
        .frame_count = frame_count,
        .frame_status = EMITTED_FRAME_DESCRIBED,
    };
    const EmittedVerdict verdict = emitted_verify_frames(&code, &test->arena);
    emitted_test_expect(test, verdict.error == expected && (expected == EMITTED_VERIFY_OK || verdict.record == record), check, id);
}

// Two programs the verifier once accepted (#1145's review), and their controls:
// - `push rbp; mov rbp, rsp; xchg [rbp], rbp; pop rbp; ret` with the push's and the pop's ordinary events: the xchg
//   overwrites the slot rbp pointed at, its address rbp's before the xchg's own write, so rbp's saved rule fails at the
//   pop, which no longer reloads rbp's entry value.
// - `mov r11, rsp; sub r11, 8; xor eax, eax; jmp J; cmp rsp, r11; J: jne R; push rbx; R: ret` without events: J's jne
//   follows the xor's flags, not the skipped cmp's, so it falls through to the push, whose ret the CIE's rules don't
//   describe. With the cmp right before the jne, which then always jumps, the push is unreachable, and the code proved.
static void emitted_test_counterexamples(EmittedTest* test)
{
    const X86Instruction push_rbp = { .form = X86_FORM_PUSH_R64, .rm = X86_REGISTER_RBP };
    const X86Instruction pointer = tier1_frame_emit_register(X86_FORM_MOV_RM64_R64, X86_REGISTER_RSP, X86_REGISTER_RBP);
    const X86Instruction exchange = tier1_frame_emit_memory(X86_FORM_XCHG_RM64_R64, X86_REGISTER_RBP, X86_REGISTER_RBP, 0);
    const X86Instruction pop_rbp = { .form = X86_FORM_POP_R64, .rm = X86_REGISTER_RBP };
    const X86Instruction ret = { .form = X86_FORM_RET };
    X86Instruction swapped[] = { push_rbp, pointer, exchange, pop_rbp, ret };
    // The push's and the pop's events alone: the CFA stays rsp's, so only rbp's saved slot can refute the frame.
    EmissionFrameEvent swapped_events[] = {
        { .at = { .boundary = 1 }, .kind = EMISSION_FRAME_CFA_OFFSET, .value = 16 },
        { .at = { .boundary = 1 }, .kind = EMISSION_FRAME_OFFSET, .reg = BUSTER_EMISSION_X86_RBP, .value = -16 },
        { .at = { .boundary = 4 }, .kind = EMISSION_FRAME_CFA_OFFSET, .value = 8 },
        { .at = { .boundary = 4 }, .kind = EMISSION_FRAME_RESTORE, .reg = BUSTER_EMISSION_X86_RBP },
    };
    emitted_test_verify_case(test, swapped, 5, 0, 0, 0, swapped_events, 4, EMITTED_VERIFY_REFUTED, 3, "an xchg through rbp of rbp's saved slot refutes its rule", 1);
    X86Instruction kept[] = { push_rbp, pointer, pop_rbp, ret };
    EmissionFrameEvent kept_events[] = {
        { .at = { .boundary = 1 }, .kind = EMISSION_FRAME_CFA_OFFSET, .value = 16 },
        { .at = { .boundary = 1 }, .kind = EMISSION_FRAME_OFFSET, .reg = BUSTER_EMISSION_X86_RBP, .value = -16 },
        { .at = { .boundary = 3 }, .kind = EMISSION_FRAME_CFA_OFFSET, .value = 8 },
        { .at = { .boundary = 3 }, .kind = EMISSION_FRAME_RESTORE, .reg = BUSTER_EMISSION_X86_RBP },
    };
    emitted_test_verify_case(test, kept, 4, 0, 0, 0, kept_events, 4, EMITTED_VERIFY_OK, 0, "the frame without the xchg is proved", 2);
    // A preserving frame's vector register (#147, emitted_verify_saved_vectors): xmm3 stored into its slot below rbp and
    // loaded back, its rules its caller's value at the entry, the slot's right after the store and its own after the load.
    // Each mutant (another slot, the rule before the store, its own value before the load, another register's store) is
    // refuted at its rule's boundary.
    const X86Instruction reserve = { .form = X86_FORM_SUB_RM64_IMM8, .rm = X86_REGISTER_RSP, .immediate = 16 };
    const X86Instruction store = { .form = X86_FORM_MOVUPS_XM_X, .reg = 3, .rm = X86_REGISTER_RBP, .mode = BUSTER_X86_MEMORY_BASE, .displacement = -16 };
    const X86Instruction load = { .form = X86_FORM_MOVUPS_X_XM, .reg = 3, .rm = X86_REGISTER_RBP, .mode = BUSTER_X86_MEMORY_BASE, .displacement = -16 };
    const X86Instruction release = tier1_frame_emit_register(X86_FORM_MOV_RM64_R64, X86_REGISTER_RBP, X86_REGISTER_RSP);
    const X86Instruction other = { .form = X86_FORM_MOVUPS_XM_X, .reg = 4, .rm = X86_REGISTER_RBP, .mode = BUSTER_X86_MEMORY_BASE, .displacement = -16 };
    for (uint32_t m = 0; m < 5; m += 1)
    {
        X86Instruction preserving[] = { push_rbp, pointer, reserve, m == 4 ? other : store, load, release, pop_rbp, ret };
        EmissionFrameEvent preserving_events[] = {
            { .at = { .boundary = 0 }, .kind = EMISSION_FRAME_SAME_VALUE, .reg = 20 },
            { .at = { .boundary = 1 }, .kind = EMISSION_FRAME_CFA_OFFSET, .value = 16 },
            { .at = { .boundary = 1 }, .kind = EMISSION_FRAME_OFFSET, .reg = BUSTER_EMISSION_X86_RBP, .value = -16 },
            { .at = { .boundary = 2 }, .kind = EMISSION_FRAME_CFA, .reg = BUSTER_EMISSION_X86_RBP, .value = 16 },
            { .at = { .boundary = m == 2 ? 3u : 4u }, .kind = EMISSION_FRAME_OFFSET, .reg = 20, .value = m == 1 ? -48 : -32 },
            { .at = { .boundary = m == 3 ? 4u : 5u }, .kind = EMISSION_FRAME_SAME_VALUE, .reg = 20 },
            { .at = { .boundary = 7 }, .kind = EMISSION_FRAME_CFA, .reg = BUSTER_EMISSION_X86_RSP, .value = 8 },
            { .at = { .boundary = 7 }, .kind = EMISSION_FRAME_RESTORE, .reg = BUSTER_EMISSION_X86_RBP },
        };
        static const uint32_t refuted_at[] = { 0, 4, 3, 4, 4 };
        static const char* const checks[] = {
            "a preserving frame's vector register's rules are proved by its store and load",
            "a vector register's slot other than its store's is refuted",
            "a vector register's slot before its store is refuted",
            "a vector register's own value before its load is refuted",
            "a vector register's slot that another register's store fills is refuted",
        };
        emitted_test_verify_case(test, preserving, 8, 0, 0, 0, preserving_events, BUSTER_ARRAY_LENGTH(preserving_events), m == 0 ? EMITTED_VERIFY_OK : EMITTED_VERIFY_REFUTED,
                                 refuted_at[m], checks[m], 10 + m);
    }
    const X86Instruction bound = tier1_frame_emit_register(X86_FORM_MOV_RM64_R64, X86_REGISTER_RSP, X86_REGISTER_R11);
    const X86Instruction lower = { .form = X86_FORM_SUB_RM64_IMM8, .rm = X86_REGISTER_R11, .immediate = 8 };
    const X86Instruction zero = tier1_frame_emit_register(X86_FORM_XOR_RM32_R32, X86_REGISTER_RAX, X86_REGISTER_RAX);
    const X86Instruction skip = { .form = X86_FORM_JMP_REL8 };
    const X86Instruction compare = tier1_frame_emit_register(X86_FORM_CMP_RM64_R64, X86_REGISTER_R11, X86_REGISTER_RSP);
    const X86Instruction jne = { .form = X86_FORM_JNE_REL8 };
    const X86Instruction push_rbx = { .form = X86_FORM_PUSH_R64, .rm = X86_REGISTER_RBX };
    X86Instruction skipped[] = { bound, lower, zero, skip, compare, jne, push_rbx, ret };
    uint32_t skipped_branches[] = { 3, 5 };
    uint32_t skipped_targets[] = { 5, 7 };
    emitted_test_verify_case(test, skipped, 8, skipped_branches, skipped_targets, 2, 0, 0, EMITTED_VERIFY_REFUTED, 7,
                             "a jne its block starts doesn't take a skipped compare's equality", 3);
    X86Instruction compared[] = { bound, lower, zero, compare, jne, push_rbx, ret };
    uint32_t compared_branches[] = { 4 };
    uint32_t compared_targets[] = { 6 };
    emitted_test_verify_case(test, compared, 7, compared_branches, compared_targets, 1, 0, 0, EMITTED_VERIFY_OK, 0,
                             "a compare right before its jne still decides the jne's edges", 4);
}

// An optimized-away label stays absent beside a live indirect target: it must not add an edge back to the prologue.
// A real label at record zero still adds that edge, and a live label skipping the epilogue still refutes the frame.
static void emitted_test_labels(EmittedTest* test)
{
    X86Instruction records[] = {
        { .form = X86_FORM_PUSH_R64, .rm = X86_REGISTER_RBP },
        { .form = X86_FORM_JMP_RM64, .rm = X86_REGISTER_RAX },
        { .form = X86_FORM_POP_R64, .rm = X86_REGISTER_RBP },
        { .form = X86_FORM_RET },
    };
    EmissionFrameEvent frames[] = {
        { .at = { .boundary = 1 }, .kind = EMISSION_FRAME_CFA_OFFSET, .value = 16 },
        { .at = { .boundary = 1 }, .kind = EMISSION_FRAME_OFFSET, .reg = BUSTER_EMISSION_X86_RBP, .value = -16 },
        { .at = { .boundary = 3 }, .kind = EMISSION_FRAME_CFA_OFFSET, .value = 8 },
        { .at = { .boundary = 3 }, .kind = EMISSION_FRAME_RESTORE, .reg = BUSTER_EMISSION_X86_RBP },
    };
    uint32_t labels[] = { UINT32_MAX, 2 };
    const EmittedCode code = {
        .records = records,
        .count = BUSTER_ARRAY_LENGTH(records),
        .label_records = labels,
        .label_count = BUSTER_ARRAY_LENGTH(labels),
        .frames = frames,
        .frame_count = BUSTER_ARRAY_LENGTH(frames),
        .frame_status = EMITTED_FRAME_DESCRIBED,
    };
    EmittedVerdict verdict = emitted_verify_frames(&code, &test->arena);
    emitted_test_expect(test, verdict.error == EMITTED_VERIFY_OK, "an absent label does not become an indirect edge to the prologue", 1);
    labels[0] = 0;
    verdict = emitted_verify_frames(&code, &test->arena);
    emitted_test_expect(test, verdict.error == EMITTED_VERIFY_REFUTED && verdict.record == 0 && verdict.rule == EMITTED_VERIFY_RULE_CFA,
                        "a real indirect target at record zero still refutes the entry frame", 2);
    labels[0] = UINT32_MAX;
    labels[1] = 3;
    verdict = emitted_verify_frames(&code, &test->arena);
    emitted_test_expect(test, verdict.error == EMITTED_VERIFY_REFUTED && verdict.record == 3 && verdict.rule == EMITTED_VERIFY_RULE_CFA,
                        "an indirect target skipping the epilogue still refutes the return frame", 3);
}

// Code through a form x86_semantics.c doesn't cover, whose conservative record may write rsp (monitorx's, an x86
// builtin's per-unit leaf, which main.c's --verify compile emits): a rule it leaves unknown is unproven, not refuted, on
// every path through it, and the driver keeps such a frame; a rule that fails before it, or on a path that doesn't go
// through it, is refuted, though an unproven one comes first. Then the driver's --verify compile of the leaves (`-c`,
// by both paths), which no host needs MWAITX for.
static void emitted_test_uncovered(EmittedTest* test, const char* directory)
{
    const X86Instruction monitorx = { .form = X86_FORM_MONITORX };
    const X86Instruction ret = { .form = X86_FORM_RET };
    const X86Instruction push = { .form = X86_FORM_PUSH_R64, .rm = X86_REGISTER_RBX };
    const X86Instruction pop = { .form = X86_FORM_POP_R64, .rm = X86_REGISTER_RBX };
    const X86Instruction test_rax = tier1_frame_emit_register(X86_FORM_TEST_RM64_R64, X86_REGISTER_RAX, X86_REGISTER_RAX);
    const X86Instruction jne = { .form = X86_FORM_JNE_REL8 };
    X86Instruction leaf[] = { tier1_frame_emit_register(X86_FORM_MOV_R64_RM64, X86_REGISTER_RAX, X86_REGISTER_RDI),
                              tier1_frame_emit_register(X86_FORM_MOV_R32_RM32, X86_REGISTER_RCX, X86_REGISTER_RSI), monitorx, ret };
    emitted_test_uncovered_case(test, leaf, 4, UINT32_MAX, UINT32_MAX, EMITTED_VERIFY_UNPROVEN, 3, "monitorx's leaf's return is unproven, not refuted", 1);
    leaf[2] = leaf[1];
    emitted_test_uncovered_case(test, leaf, 4, UINT32_MAX, UINT32_MAX, EMITTED_VERIFY_OK, 0, "the leaf without monitorx is proved", 2);
    X86Instruction early[] = { push, monitorx, pop, ret };
    emitted_test_uncovered_case(test, early, 4, UINT32_MAX, UINT32_MAX, EMITTED_VERIFY_REFUTED, 1, "a push its events don't describe, before monitorx, is refuted", 3);
    X86Instruction joined[] = { test_rax, jne, monitorx, ret };
    emitted_test_uncovered_case(test, joined, 4, 1, 3, EMITTED_VERIFY_UNPROVEN, 3, "a return that one path through monitorx reaches is unproven", 4);
    X86Instruction apart[] = { test_rax, jne, monitorx, ret, push, ret };
    emitted_test_uncovered_case(test, apart, 6, 1, 4, EMITTED_VERIFY_REFUTED, 5, "a push off monitorx's path is refuted after an unproven return", 5);
    char source[PATH_MAX + 32], object[PATH_MAX + 32], err[PATH_MAX + 32];
    snprintf(source, sizeof(source), "%s/leaves.c", directory);
    snprintf(object, sizeof(object), "%s/leaves.o", directory);
    snprintf(err, sizeof(err), "%s/leaves.err", directory);
    static const char text[] = "int x;int wait_for(int n){__builtin_ia32_monitorx(&x,0,0);__builtin_ia32_mwaitx(2,0,(unsigned)n);return x+n;}\n"
                               "int caller(int n){return wait_for(n)+1;}\n";
    const bool written = debug_test_write_file(source, (const uint8_t*)text, sizeof(text) - 1);
    emitted_test_expect(test, written, "write the leaves' source", 6);
    for (uint32_t path = 0; written && path < 2; path += 1)
    {
        char* arguments[12] = { "/proc/self/exe", "--lanes", "1", "--verify", "-c", "-march=znver5", "-o", object, source };
        uint32_t used = 9;
        arguments[used] = path != 0 ? "--scalar" : 0;
        used += path != 0;
        arguments[used] = 0;
        const bool compiled = object_test_run(arguments, "/dev/null", err) == 0;
        emitted_test_expect(test, compiled, path != 0 ? "--scalar --verify -c compiles monitorx's and mwaitx's leaves" : "--verify -c compiles monitorx's and mwaitx's leaves", 7 + path);
        if (!compiled && test->failures < 20)
        {
            const String8 errors = object_test_read_file(&test->arena, err);
            fprintf(stderr, "  %.*s", (int)errors.length, errors.pointer != 0 ? (const char*)errors.pointer : "");
        }
    }
    unlink(source);
    unlink(object);
    unlink(err);
}

// The previous Tier 1 adapter's reading of a frame (c_check_tier1.c's check_tier1_frame before #1056): its frame as Tier
// 0's published one, its early returns as tails, read by the fixed reader. False where it couldn't describe the frame.
static bool emitted_test_previous_tier1(const Tier1Frame* frame, const EmittedTestFrame* code, const EmittedTestLayout* layout, uint32_t* tails, UnwindFunction* output)
{
    bool describable = true;
    for (uint32_t i = 0; i < frame->prologue_count; i += 1)
    {
        describable &= frame->prologue[i].kind != TIER1_FRAME_PROBE_ALLOCATE && frame->prologue[i].kind != TIER1_FRAME_PROBE_ALIGN;
    }
    uint32_t saved = 0;
    for (uint32_t r = 0; r < BUSTER_ARRAY_LENGTH(unwind_tier0_saved_registers); r += 1)
    {
        saved |= ((frame->saved_registers >> unwind_tier0_saved_registers[r]) & 1u) << r;
    }
    const uint32_t count = code->buffer.count;
    const bool frameless = saved == 0 && !frame->frame_pointer && frame->stack_adjust_bytes == 0;
    const bool closed = code->exits[code->exit_count - 1] == count - 1;
    const uint32_t tail_count = frameless ? 0 : closed ? code->exit_count - 1 : code->exit_count;
    for (uint32_t k = 0; k < tail_count; k += 1)
    {
        tails[k] = code->exits[k];
    }
    const Tier0Code previous = {
        .records = (X86Instruction*)code->records,
        .count = count,
        .branches = (uint32_t*)code->branches,
        .targets = (uint32_t*)code->targets,
        .branch_count = code->buffer.branch_count,
        .frame_saved = saved,
        .frame_size = frame->stack_adjust_bytes,
        .frame_pointer = frame->frame_pointer,
        .frame_alignment_log2 = frame->realigned ? (uint8_t)__builtin_ctz(frame->alignment) : 0,
        .frame_base = frame->realigned && frame->base_register == BUSTER_TIER1_FRAME_REALIGN_BASE,
        .frame_open = !closed,
        .tail_records = tail_count != 0 ? tails : 0,
        .tail_count = tail_count,
        .error = TIER0_OK,
    };
    static uint32_t storage[64];
    return describable && unwind_tier0_frame_tails(&previous, layout->offsets, layout->length, output, storage) == UNWIND_OK;
}

static void emitted_test_tier1(EmittedTest* test)
{
    const uint32_t alignments[] = { 8, 16, 64, 8192 };
    const uint32_t sizes[] = { 8, 8200, 3 * 4096 + 24 };
    const uint32_t start = 0;
    const uint32_t end = 1;
    const uint32_t all_saved = (1u << X86_REGISTER_RBX) | (1u << X86_REGISTER_RBP) | (15u << X86_REGISTER_R12);
    static EmittedTestFrame code;
    uint64_t id = 0;
    for (uint32_t flags = 0; flags <= BUSTER_TIER1_FRAME_FLAGS; flags += 1)
    {
        for (uint32_t a = 0; a < BUSTER_ARRAY_LENGTH(alignments); a += 1)
        {
            for (uint32_t s = 0; s < BUSTER_ARRAY_LENGTH(sizes); s += 1)
            {
                for (uint32_t shape = 0; shape < 4; shape += 1)
                {
                    id += 1;
                    const uint64_t mark = test->arena.length;
                    Tier1FrameRequest request = {
                        .slots = { .start = &start, .end = &end, .size = sizes + s, .alignment = alignments + a, .count = 1, .column_count = 1 },
                        .flags = flags,
                    };
                    Tier1FrameRequirements requirements;
                    Tier1Frame frame;
                    const bool required = tier1_frame_requirements(&request, &requirements) == TIER1_FRAME_OK;
                    request.used_callee_saved = (shape & 1) != 0 ? all_saved & ~requirements.reserved_registers : 0;
                    const bool planned = required && tier1_frame_plan(&request, &test->arena, &test->scratch, &frame) == TIER1_FRAME_OK;
                    emitted_test_expect(test, planned, "the frame plans", id);
                    const bool open = (shape & 2) != 0;
                    const bool emitted = planned && emitted_test_frame_code(&frame, request.used_callee_saved, open, (flags & BUSTER_TIER1_FRAME_DYNAMIC_STACK) != 0, true, &code);
                    emitted_test_expect(test, emitted, "the frame's function emits with its events", id);
                    EmittedTestLayout layout;
                    const bool laid = emitted && emitted_test_layout(code.records, code.buffer.count, code.branches, code.targets, code.buffer.branch_count,
                                                                     code.reference_count, &test->arena, &layout);
                    emitted_test_expect(test, !emitted || laid, "the frame's function sizes and encodes", id);
                    if (laid)
                    {
                        const EmittedCode result = {
                            .records = code.records,
                            .count = code.buffer.count,
                            .branches = code.branches,
                            .targets = code.targets,
                            .branch_count = code.buffer.branch_count,
                            .references = code.references,
                            .reference_count = code.reference_count,
                            .frames = code.buffer.event_count != 0 ? code.events : 0,
                            .frame_count = code.buffer.event_count,
                            .frame_status = EMITTED_FRAME_DESCRIBED,
                        };
                        const EmittedVerdict verdict = emitted_verify_frames(&result, &test->arena);
                        emitted_test_expect(test, verdict.error == EMITTED_VERIFY_OK, "a Tier 1 frame's events are its records'", id);
                        if (verdict.error != EMITTED_VERIFY_OK && test->failures < 20)
                        {
                            fprintf(stderr, "  flags %u align %u size %u shape %u: verdict %u at record %u, event %u of %u (records %u)\n", flags, alignments[a], sizes[s], shape,
                                    verdict.error, verdict.record, verdict.event, result.frame_count, result.count);
                        }
                        EmissionFrameRow* rows = result.frame_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(&test->arena, EmissionFrameRow, result.frame_count) : 0;
                        UnwindFunction resolved;
                        const bool lowered = emitted_unwind(&result, layout.offsets, layout.length, false, rows, &resolved) == UNWIND_OK;
                        emitted_test_expect(test, lowered, "a Tier 1 frame's events resolve into a descriptor", id);
                        uint32_t tails[4];
                        UnwindFunction previous;
                        const bool described = emitted_test_previous_tier1(&frame, &code, &layout, tails, &previous);
                        bool probed = false;
                        bool pages = false;
                        for (uint32_t k = 0; k < frame.prologue_count; k += 1)
                        {
                            probed |= frame.prologue[k].kind == TIER1_FRAME_PROBE_ALLOCATE || frame.prologue[k].kind == TIER1_FRAME_PROBE_ALIGN;
                            pages |= frame.prologue[k].kind == TIER1_FRAME_PROBE_ALLOCATE && frame.prologue[k].amount >= BUSTER_TIER1_FRAME_PAGE_BYTES;
                        }
                        if (!probed)
                        {
                            emitted_test_expect(test, described && lowered && emitted_unwind_same(&previous, &resolved, &test->arena) == EMITTED_SAME &&
                                                          emitted_test_same_bytes(&previous, &resolved, true),
                                                "without probes, the previous adapter's descriptor, byte for byte", id);
                        }
                        else if (pages && !frame.frame_pointer)
                        {
                            // The shape the fixed descriptor can't express: the CFA in r11 through the probe loop.
                            bool r11 = false;
                            for (uint32_t k = 0; k < result.frame_count; k += 1)
                            {
                                r11 |= result.frames[k].kind == EMISSION_FRAME_CFA && result.frames[k].reg == 11;
                            }
                            emitted_test_expect(test, r11 && lowered && resolved.open == BUSTER_UNWIND_ROWS && !described,
                                                "a probe loop without a frame pointer is a row program with its CFA in r11", id);
                            test->probe_frames += 1;
                        }
                        if (verdict.error == EMITTED_VERIFY_OK)
                        {
                            emitted_test_mutants(test, &result, 64, id);
                        }
                        test->tier1_frames += 1;
                        test->row_programs += lowered && resolved.open == BUSTER_UNWIND_ROWS;
                    }
                    arena_pop_to(&test->arena, mark);
                }
            }
        }
    }
}

// The probe frame the native check runs: no frame pointer, every callee-saved register but r14 pushed (the caller's r14
// holds its rsp, which the CFA must give back), three pages and 24 bytes probed. Its code in `code`.
static bool emitted_test_probe_frame(EmittedTest* test, Tier1Frame* frame, EmittedTestFrame* code)
{
    const uint32_t start = 0;
    const uint32_t end = 1;
    const uint32_t size = 3 * 4096 + 24;
    const uint32_t alignment = 16;
    Tier1FrameRequest request = {
        .slots = { .start = &start, .end = &end, .size = &size, .alignment = &alignment, .count = 1, .column_count = 1 },
        .flags = BUSTER_TIER1_FRAME_NO_RED_ZONE | BUSTER_TIER1_FRAME_PROBE_STACK,
        .used_callee_saved = (1u << X86_REGISTER_RBX) | (1u << X86_REGISTER_RBP) | (1u << X86_REGISTER_R12) | (1u << X86_REGISTER_R13) | (1u << X86_REGISTER_R15),
    };
    bool valid = tier1_frame_plan(&request, &test->arena, &test->scratch, frame) == TIER1_FRAME_OK && !frame->frame_pointer;
    valid = valid && emitted_test_frame_code(frame, request.used_callee_saved, false, false, false, code);
    return valid;
}

// The probe frame natively (--cross-check): its bytes and CFI in an object GNU as assembles around them, linked by ld,
// called twice (its early return, then its late one), and gdb stopped at every instruction of it, each stop's
// backtrace through the caller (`_start`), whose frame must have its callee-saved registers (101 to 106 but r14) and
// its rsp (r14) back.
static void emitted_test_native(EmittedTest* test, bool keep)
{
    char directory[PATH_MAX];
    bool valid = emitted_test_directory(directory, sizeof(directory), "buster-emitted");
    Tier1Frame frame;
    static EmittedTestFrame code;
    EmittedTestLayout layout;
    valid = valid && emitted_test_probe_frame(test, &frame, &code) &&
            emitted_test_layout(code.records, code.buffer.count, code.branches, code.targets, code.buffer.branch_count, code.reference_count, &test->arena, &layout);
    emitted_test_expect(test, valid, "the native probe frame emits", 0);
    const EmittedCode result = {
        .records = code.records,
        .count = code.buffer.count,
        .branches = code.branches,
        .targets = code.targets,
        .branch_count = code.buffer.branch_count,
        .references = code.references,
        .reference_count = code.reference_count,
        .frames = code.events,
        .frame_count = code.buffer.event_count,
        .frame_status = EMITTED_FRAME_DESCRIBED,
    };
    EmissionFrameRow rows[BUSTER_ARRAY_LENGTH(code.events)];
    UnwindFunction unwind;
    valid = valid && emitted_verify_frames(&result, &test->arena).error == EMITTED_VERIFY_OK &&
            emitted_unwind(&result, layout.offsets, layout.length, false, rows, &unwind) == UNWIND_OK && unwind.open == BUSTER_UNWIND_ROWS;
    uint8_t frame_bytes[1024];
    uint32_t location = 0;
    UnwindOutput frames = { .bytes = frame_bytes, .capacity = sizeof(frame_bytes), .locations = &location, .location_capacity = 1 };
    valid = valid && unwind_write(&unwind, 1, &frames, false) == UNWIND_OK;
    char assembly[PATH_MAX + 32], object[PATH_MAX + 32], executable[PATH_MAX + 32], out[PATH_MAX + 32], err[PATH_MAX + 32], commands[PATH_MAX + 32],
        path[PATH_MAX + 32];
    snprintf(assembly, sizeof(assembly), "%s/fixture.s", directory);
    snprintf(object, sizeof(object), "%s/fixture.o", directory);
    snprintf(executable, sizeof(executable), "%s/fixture", directory);
    snprintf(out, sizeof(out), "%s/stdout", directory);
    snprintf(err, sizeof(err), "%s/stderr", directory);
    snprintf(commands, sizeof(commands), "%s/check.gdb", directory);
    FILE* source = valid ? fopen(assembly, "w") : 0;
    valid = valid && source != 0;
    if (source != 0)
    {
        // probe_leaf returns 1 then 0, so that the two calls take the late return and the early one.
        fputs(".text\n.globl _start,probe_frame,probe_leaf\n.type _start,@function\n_start:\n.cfi_startproc\n.cfi_undefined 16\n"
              "mov $101,%ebx\nmov $102,%ebp\nmov $103,%r12d\nmov $104,%r13d\nmov %rsp,%r14\nmov $106,%r15d\n"
              "call probe_frame\ncall probe_frame\nxor %edi,%edi\nmov $60,%eax\nsyscall\n.cfi_endproc\n.size _start,.-_start\n"
              ".type probe_leaf,@function\nprobe_leaf:\n.cfi_startproc\nmovl probe_flip(%rip),%eax\nxorl $1,%eax\nmovl %eax,probe_flip(%rip)\nret\n"
              ".cfi_endproc\n.size probe_leaf,.-probe_leaf\n.data\nprobe_flip:\n.long 0\n.text\n",
              source);
        snprintf(path, sizeof(path), "%s/code", directory);
        valid &= debug_test_write_file(path, layout.bytes, layout.length);
        fprintf(source, ".balign 16\n.type probe_frame,@function\nprobe_frame:\n.incbin \"%s\"\n.size probe_frame,.-probe_frame\n", path);
        for (uint32_t ref = 0; ref < code.reference_count; ref += 1)
        {
            const LinkReference* reference = &code.references[ref];
            const uint32_t distance = x86_fixup_distance(code.records[reference->instruction].form, reference->field);
            fprintf(source, ".reloc probe_frame+%u,R_X86_64_PC32,probe_leaf%+" PRId64 "\n", layout.ends[ref] - distance, reference->addend - distance);
        }
        snprintf(path, sizeof(path), "%s/frames", directory);
        valid &= debug_test_write_file(path, frame_bytes, frames.size);
        fprintf(source, ".section .eh_frame,\"a\",@progbits\n.balign 8\n.Lprobe_frames:\n.incbin \"%s\"\n.reloc .Lprobe_frames+%u,R_X86_64_PC32,probe_frame\n", path, location);
        fputs(".section .note.GNU-stack,\"\",@progbits\n", source);
        valid &= fclose(source) == 0;
    }
    // A breakpoint at every record's start; the loop's run each time it does.
    FILE* script = valid ? fopen(commands, "w") : 0;
    valid = valid && script != 0;
    if (script != 0)
    {
        fputs("set pagination off\nset confirm off\nset debuginfod enabled off\n", script);
        for (uint32_t r = 0; r < code.buffer.count; r += 1)
        {
            fprintf(script, "break *(probe_frame+%u)\ncommands\nsilent\nprintf \"PROBE_STOP %u\\n\"\nbt\nframe 1\n", layout.offsets[r], r);
            fputs("if $rbx != 101 || $rbp != 102 || $r12 != 103 || $r13 != 104 || $r15 != 106 || $rsp != $r14\nprintf \"PROBE_BAD_FRAME\\n\"\nend\n"
                  "frame 0\ncontinue\nend\n",
                  script);
        }
        fputs("run\n", script);
        valid &= fclose(script) == 0;
    }
    char* assemble[] = { "as", "--64", "-o", object, assembly, 0 };
    char* link[] = { "ld", "--eh-frame-hdr", "-o", executable, object, 0 };
    valid = valid && object_test_run(assemble, out, err) == 0 && object_test_run(link, out, err) == 0;
    char* gdb[] = { "gdb", "-nx", "-batch", "-x", commands, executable, 0 };
    const bool ran = valid && object_test_run(gdb, out, err) == 0;
    const String8 text = ran ? object_test_read_file(&test->arena, out) : (String8){ 0 };
    // Every record stopped at, the loop's each time it runs; each stop's frames the function's and its caller's.
    uint32_t stops = 0;
    uint32_t seen = 0;
    uint8_t* reached = BUSTER_ARENA_PUSH_ARRAY(&test->arena, uint8_t, code.buffer.count);
    bool frames_ok = text.pointer != 0 && reached != 0 && strstr(text.pointer, "PROBE_BAD_FRAME") == 0;
    if (reached != 0)
    {
        memset(reached, 0, code.buffer.count);
    }
    for (const char* cursor = text.pointer; frames_ok && cursor != 0 && (cursor = strstr(cursor, "PROBE_STOP ")) != 0; cursor += 1)
    {
        const char* next = strstr(cursor + 1, "PROBE_STOP ");
        const unsigned long record = strtoul(cursor + sizeof("PROBE_STOP ") - 1, 0, 10);
        const char* first = strstr(cursor, "#0 ");
        const char* second = strstr(cursor, "#1 ");
        const char* third = strstr(cursor, "#2 ");
        frames_ok = record < code.buffer.count && first != 0 && second != 0 && (next == 0 || second < next) && (third == 0 || (next != 0 && third > next)) &&
                    strstr(first, "probe_frame") != 0 && strstr(second, "_start") != 0 && (next == 0 || strstr(second, "_start") < next);
        if (frames_ok)
        {
            seen += reached[record] == 0;
            reached[record] = 1;
        }
        stops += 1;
    }
    // Every record runs: the loop's once per page each call, the others once a call, but the returns' one each.
    frames_ok = frames_ok && seen == code.buffer.count && stops > code.buffer.count;
    emitted_test_expect(test, frames_ok, "gdb unwinds the probe frame at every instruction to its caller, its registers and rsp restored", 0);
    printf("emitted code native probe frame: %u records, %u gdb stops (the loop's each pass), CFA in r11 through the loop: %s\n", code.buffer.count, stops,
           frames_ok ? "pass" : "FAIL");
    if (!frames_ok && text.pointer != 0)
    {
        printf("%.4000s\n", text.pointer);
    }
    if (keep || !frames_ok)
    {
        printf("emitted code native fixture files: %s\n", directory);
    }
    else
    {
        char* remove[] = { "rm", "-r", directory, 0 };
        object_test_run(remove, "/dev/null", "/dev/null");
    }
}

// The tests, with their options: 0 when every check passes.
static int emitted_test_run(bool cross, bool keep, uint64_t seed, uint32_t rounds)
{
    EmittedTest test = { .arena = arena_reserve(UINT64_C(1) << 32), .scratch = arena_reserve(UINT64_C(1) << 32) };
    const bool valid = test.arena.base != 0 && test.scratch.base != 0 && x86_encoder_initialize() && x86_semantics_initialize() && tier0_initialize() && unwind_initialize();
    emitted_test_expect(&test, valid, "the tables initialize", 0);
    if (valid)
    {
        emitted_test_rows(&test);
        emitted_test_tier0(&test, seed, rounds);
        emitted_test_tier1(&test);
        emitted_test_limits(&test);
        emitted_test_counterexamples(&test);
        emitted_test_labels(&test);
        char directory[PATH_MAX];
        const bool made = emitted_test_directory(directory, sizeof(directory), "buster-emitted-leaves");
        emitted_test_expect(&test, made, "make a directory for the leaves' compile", 0);
        if (made)
        {
            emitted_test_uncovered(&test, directory);
            rmdir(directory);
        }
        if (cross)
        {
            emitted_test_native(&test, keep);
        }
    }
    emitted_test_expect(&test, test.tier0_functions != 0 && test.tier0_probed != 0 && test.tier1_frames != 0 && test.probe_frames != 0 && test.mutants != 0 &&
                                   test.refuted == test.mutants,
                        "coverage: Tier 0 functions, probed ones, Tier 1 frames, probe frames and refuted mutants", 0);
    printf("emitted code: %" PRIu64 " fixed descriptors as row programs, %" PRIu64 " Tier 0 programs' %" PRIu64 " functions (%" PRIu64 " probed), %" PRIu64
           " Tier 1 frames (%" PRIu64 " probe loops without a frame pointer), %" PRIu64 " row programs, %" PRIu64 " of %" PRIu64 " mutants refuted; %" PRIu64
           " checks, %" PRIu64 " failed\n",
           test.descriptors, test.programs, test.tier0_functions, test.tier0_probed, test.tier1_frames, test.probe_frames, test.row_programs, test.refuted,
           test.mutants, test.checks, test.failures);
    arena_release(&test.arena);
    arena_release(&test.scratch);
    return test.failures == 0 ? 0 : 1;
}

static int run_emitted_code_tests(int argc, char** argv)
{
    bool cross = false;
    bool keep = false;
    bool valid = true;
    uint64_t seed = 1056;
    uint32_t rounds = 4;
    for (int i = 0; i < argc; i += 1)
    {
        if (strcmp(argv[i], "--cross-check") == 0)
        {
            cross = true;
        }
        else if (strcmp(argv[i], "--keep") == 0)
        {
            keep = true;
        }
        else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
        {
            seed = strtoull(argv[++i], 0, 0);
        }
        else if (strcmp(argv[i], "--count") == 0 && i + 1 < argc)
        {
            rounds = (uint32_t)strtoul(argv[++i], 0, 0);
        }
        else
        {
            valid = false;
        }
    }
    int status = 1;
    if (!valid)
    {
        fprintf(stderr, "usage: test-emitted-code [--seed N] [--count N] [--cross-check] [--keep]\n");
    }
    else
    {
        status = emitted_test_run(cross, keep, seed, rounds);
    }
    return status;
}
