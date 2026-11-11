// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The tier-neutral emitted function (#1056): what a code generator hands the C driver's emission (check_program_emit),
// which sizes its branches, encodes it, publishes its atom, symbols and fixups, and from its metadata writes its source
// rows (-g) and its unwind table. Tier 0's code (tier0_compile's, its helpers', the assembly templates') adapts to it
// (emitted_from_tier0), and Tier 1's publishes it itself (check_tier1_compile): the emission's code, rows and unwind
// path no longer consume a Tier0Code (--verify's comparison with Tier 0's fixed-frame analyzer, unwind_tier0, still reads
// the records as one, and Tier 0's helpers carry theirs to the adapter), and no producer describes its frame as
// another's.
//
// Its slices, all borrowed from the producer's arenas, which outlive the emission's reading of them:
//
// - the code: the instruction records, the branch columns (each branch's record and its target's record, ascending,
//   which branch sizing reads), the symbolic references (LinkReference, by marked instruction) and each label's first
//   record, where the emission defines the label's symbol. UINT32_MAX marks a label without code, which no indirect
//   branch reaches; its symbol still gets a placeholder at the function's start. Record 0 remains a real target;
// - optional source events (-g only; none otherwise, so a compile without -g pays for none): `source_count` events, event
//   e at the RIGHT side of record boundary source_records[e] (nondecreasing), whose origin is source_origins[e], an
//   index into the body's typed entries (the lowering's entry tokens), or event e itself where source_origins is null
//   (Tier 0's: an event per typed entry), or BUSTER_EMISSION_NO_ORIGIN for generated code, which the rows give the
//   declaration's line (c_check_debug.c); and one more boundary, source_records[source_count], where the epilogue's
//   closing row starts (the record count for none). The declaration's row at (0, LEFT) is the consumer's, which knows
//   the declaration;
// - frame events (emission_metadata.c's EmissionFrameEvent, in the x86-64 unwind-register namespace): the CFA's and the
//   saved registers' rule changes at record boundaries, in order, from the CIE's initial rules (the CFA rsp + 8, the
//   return address at CFA - 8, the six callee-saved registers their own), with remembered and restored states around
//   early returns and tail calls. The emission resolves them through the final layout after branch sizing
//   (emitted_unwind: the records' starts and ends, which the protocol checks), and writes the fixed-frame CFI where
//   they spell a fixed shape and a row program otherwise (unwind.c), the same bytes either way for a fixed shape.
//
// emitted_verify_frames proves the frame events against the records and their control flow (with --verify, and in the
// tests): see its comment. Producers' own checks run at their production; a frame a producer can't describe is
// EMITTED_FRAME_UNDESCRIBED, which the emission reports as the backend's failure at the function. Signed 64-bit frame
// values cover every fixed imm32 allocation plus the return address and callee-saved pushes (#1149).

#define BUSTER_EMITTED_UNKNOWN INT64_MIN
#define BUSTER_EMITTED_NONE 0xffu
// The registers whose entry values the verifier follows: the general ones, and the return address's slot (16).
#define BUSTER_EMITTED_TRACKED 17u
// The deepest remembered state the verifier follows, as the writer does (BUSTER_UNWIND_ROW_STATES).
#define BUSTER_EMITTED_STATES BUSTER_UNWIND_ROW_STATES

typedef enum EmittedFrameStatus
{
    // No frame events were asked for (a compile without unwind tables).
    EMITTED_FRAME_NONE,
    // The events describe the frame: none for the CIE's rules throughout.
    EMITTED_FRAME_DESCRIBED,
    // The producer's frame is one its events can't describe, or isn't the frame it published.
    EMITTED_FRAME_UNDESCRIBED,
    // No room for the events.
    EMITTED_FRAME_MEMORY,
} EmittedFrameStatus;

// An optional source object's proven storage, at record boundaries in final scheduling order. The range is
// half-open; its local is the original typed function's local, never an allocator value or a compacted IR slot.
// No range means unavailable. A producer must exclude frame transitions and shared-storage lifetime holes.
typedef struct EmittedLocalRange EmittedLocalRange;
struct EmittedLocalRange
{
    Tier0Home home;
    uint64_t value;
    uint32_t local;
    uint32_t begin;
    uint32_t end;
    // Zero selects the storage home; 1..8 select these exact low bytes as an implicit scalar value.
    uint8_t value_bytes;
};

typedef struct EmittedCode EmittedCode;
struct EmittedCode
{
    X86Instruction* records;
    uint32_t count;
    uint32_t* branches;
    uint32_t* targets;
    uint32_t branch_count;
    LinkReference* references;
    uint32_t reference_count;
    // First records by label index; UINT32_MAX is an absent label, not an indirect control-flow target.
    uint32_t* label_records;
    uint32_t label_count;
    // The asm bodies' labels (#1839, Tier0Code.asm_label_records): each one's first record (the count for the end) and
    // its name, the front end's index (CheckProgramAsms.names).
    const uint32_t* asm_label_records;
    const uint32_t* asm_label_names;
    uint32_t asm_label_count;
    const Tier0Home* local_homes;
    uint32_t local_count;
    const EmittedLocalRange* local_ranges;
    uint32_t local_range_count;
    const uint32_t* source_records;
    const uint32_t* source_origins;
    uint32_t source_count;
    const EmissionFrameEvent* frames;
    uint32_t frame_count;
    uint8_t frame_status;
    // An assembly template's code (a naked function's, an asm statement's helper's): its frame events, if any, are the
    // frame the compiler built around the templates, whose own instructions the verifier doesn't read.
    bool assembly;
    // The producer's failure (Tier 0's Tier0Error, which the emission reports; 0 for none) and its node.
    uint32_t error;
    uint32_t node;
    // Tier 0's entries it deferred, for the emission's statistics.
    uint32_t deferred;
    // An interrupt service routine with an error code (#147, Tier0Code.frame_interrupt): entered with rsp 16 below the
    // CFA, the error code at rsp and the interrupted rip, its return address, at CFA - 8 (emitted_entry_state).
    bool entry_error_code;
};

typedef enum EmittedVerifyError
{
    EMITTED_VERIFY_OK,
    // The events break the protocol (an anchor out of order or past the code, an unknown kind or register, a state
    // restored that wasn't remembered).
    EMITTED_VERIFY_INVALID,
    // A rule the code doesn't establish where it holds: the CFA's register isn't the CFA less the rule's offset, or a
    // saved register isn't where its rule says.
    EMITTED_VERIFY_REFUTED,
    // States remembered deeper than the verifier follows.
    EMITTED_VERIFY_UNSUPPORTED,
    // A rule the verifier can't establish where a record of a form x86_semantics.c doesn't cover is on a path that
    // reaches it: that form's conservative record may write any register, rsp among them, and any byte, so the rule is
    // neither proved nor refuted (as Tier 0's analyzer leaves such code, UNWIND_ERROR_UNSUPPORTED). An x86 builtin's
    // leaf of such a form (monitorx's) is one.
    EMITTED_VERIFY_UNPROVEN,
    EMITTED_VERIFY_MEMORY,
} EmittedVerifyError;

typedef enum EmittedVerifyRule
{
    EMITTED_VERIFY_RULE_NONE,
    EMITTED_VERIFY_RULE_CFA,
    EMITTED_VERIFY_RULE_SAME,
    EMITTED_VERIFY_RULE_OFFSET,
    EMITTED_VERIFY_RULE_REGISTER,
} EmittedVerifyRule;

typedef struct EmittedVerdict EmittedVerdict;
struct EmittedVerdict
{
    EmittedVerifyError error;
    // The record where the rules first fail, and the last event applied before it (frame_count for none).
    uint32_t record;
    uint32_t event;
    // The first rule that fails (refuted or unproven only). Registers use DWARF numbering; holder is a REGISTER
    // rule's location. CFA/OFFSET compare CFA-relative offsets; SAME/REGISTER compare entry-register identities.
    // BUSTER_EMITTED_UNKNOWN means the actual value is unknown. These fields do not decide the verdict.
    EmittedVerifyRule rule;
    uint32_t reg;
    uint32_t holder;
    int64_t expected;
    int64_t actual;
};

static __attribute__((unused)) EmittedCode emitted_from_tier0(const Tier0Code* code, bool frames, bool sources, bool assembly, Arena* arena);
static __attribute__((unused)) UnwindError emitted_unwind(const EmittedCode* code, const uint32_t* offsets, uint32_t length, bool scalar, EmissionFrameRow* rows,
                                                          UnwindFunction* output);
// Whether two descriptors are the same CFI (emitted_unwind_same).
typedef enum EmittedSame
{
    EMITTED_SAME,
    EMITTED_DIFFERENT,
    // No room in the scratch arena for a fixed descriptor's rows: neither, as a resource failure.
    EMITTED_SAME_MEMORY,
} EmittedSame;

static __attribute__((unused)) EmittedSame emitted_unwind_same(const UnwindFunction* a, const UnwindFunction* b, Arena* scratch);
static __attribute__((unused)) EmittedVerdict emitted_verify_frames(const EmittedCode* code, Arena* scratch);

// How many registers a preserving function keeps for its caller past the callee-saved ones (#147): its general ones, and
// the vector and mask ones of its preserve block, each with three rows (emitted_tier0_preserve_rows).
static uint32_t emitted_tier0_preserved(const Tier0Code* code)
{
    const bool block = tier0_preserve_block_bytes(code->frame_preserve_vectors, code->frame_preserve_vector_bytes, code->frame_preserve_mask_bytes) != 0;
    return (uint32_t)__builtin_popcount(code->frame_preserved) + (block ? tier0_preserve_moves(code->frame_preserve_vectors, code->frame_preserve_mask_bytes) : 0);
}

// The rows of a fixed descriptor at most: the prologue's (2 per push and rbp's pointing, 15), the epilogue's (2 per
// pop, 16) and each tail's (its bracket and the epilogue's, 18); an eh_return function's rdx's and rax's saves
// (emitted_tier0_eh_rows); and a probed reservation's loop's two (#177, emitted_tier0_probe_rows), which only a frame
// without a frame pointer has, and so never an eh_return function's.
static uint64_t emitted_tier0_frame_room(const Tier0Code* code)
{
    return 31 + 18 * (uint64_t)code->tail_count + (code->frame_eh_return ? 4 + 2 * (uint64_t)code->tail_count : 0) + 2 +
           3 * (uint64_t)emitted_tier0_preserved(code) + (code->frame_interrupt == BUSTER_TIER0_INTERRUPT_ERROR_CODE ? 2 : 0);
}

// An interrupt service routine's frame with an error code (#147, Tier0Code.frame_interrupt), its `count` rows (room for
// two more): the processor pushed the interrupted code's rip, its return address, above the error code, so its CFA is 8
// bytes further up than a call's frame's, where rsp + 16 is at its entry. Each rule of the CFA's offset gains 8 and each
// register's slot from the CFA loses 8, the CFA rsp + 16 from the entry, and after the epilogue's add over the error code,
// rsp + 8 again for its iretq, which pops the rip at the CFA less 8 as a ret does (LLVM's x86_intrcc frames leave the
// return address the error code's slot instead). Returns the rows' count.
static uint32_t emitted_insert_row(EmissionFrameRow* rows, uint32_t count, EmissionFrameRow row);

static uint32_t emitted_tier0_interrupt_rows(const Tier0Code* code, EmissionFrameRow* rows, uint32_t count)
{
    for (uint32_t r = 0; r < count; r += 1)
    {
        const uint32_t kind = rows[r].kind;
        rows[r].value += kind == EMISSION_FRAME_CFA || kind == EMISSION_FRAME_CFA_OFFSET ? 8 : kind == EMISSION_FRAME_OFFSET ? -8 : 0;
    }
    memmove(rows + 1, rows, count * sizeof(EmissionFrameRow));
    rows[0] = (EmissionFrameRow){ .offset = 0, .value = 16, .kind = EMISSION_FRAME_CFA_OFFSET };
    // After the add, the epilogue's last record but the iretq (emitted_from_tier0's rows are before branch sizing).
    return emitted_insert_row(rows, count + 1, (EmissionFrameRow){ .offset = code->count - 1, .value = 8, .kind = EMISSION_FRAME_CFA_OFFSET });
}

// A row into rows sorted by their offsets, after those at its offset or before (room for it). Returns the count.
static uint32_t emitted_insert_row(EmissionFrameRow* rows, uint32_t count, EmissionFrameRow row)
{
    uint32_t at = count;
    while (at > 0 && rows[at - 1].offset > row.offset)
    {
        rows[at] = rows[at - 1];
        at -= 1;
    }
    rows[at] = row;
    return count + 1;
}

// An eh_return function's frame (#148, Tier0Code.frame_eh_return): rdx's and rax's saves, which its prologue pushes after
// the callee-saved registers and which no fixed descriptor describes, into its fixed shape's `count` rows (room for
// emitted_tier0_frame_room's more): after the prologue's rows (rbp's three and one per push), at the boundary after rax's
// push, as LLVM's CSR_64EHRet frame's CFI has them for libgcc's uw_install_context to write; a later row before that
// boundary, a tail's remembered state at its row before, moves to it. Their rules are restored where their slots are
// released: after the epilogue's release, which steps over them, after each __builtin_eh_return's pops of them (its
// tail's, Tier0Code.tail_records: the release, rax's pop and rdx's, the other pops, rbp's, rsp's mov and the jump), and
// after each musttail call's release, which steps over them as the epilogue's does (#148: a tail whose jump isn't
// emitted_handed's).
// Returns the rows' count.
static bool emitted_handed(const X86Instruction* records, uint32_t i);

static uint32_t emitted_tier0_eh_rows(const Tier0Code* code, EmissionFrameRow* rows, uint32_t count)
{
    const uint32_t pushes = (uint32_t)__builtin_popcount(code->frame_saved);
    const uint32_t prologue = 3 + pushes;
    // Record boundaries (emitted_from_tier0's rows are before branch sizing): rbp's push and mov, the pushes, rdx's and rax's.
    const uint32_t boundary = 2 + pushes + 2;
    const int32_t rdx = -(int32_t)(16 + 8 * (pushes + 1));
    for (uint32_t r = count; r > prologue; r -= 1)
    {
        rows[r + 1] = rows[r - 1];
        rows[r + 1].offset = rows[r + 1].offset < boundary ? boundary : rows[r + 1].offset;
    }
    rows[prologue] = (EmissionFrameRow){ .offset = boundary, .value = rdx, .reg = 1, .kind = EMISSION_FRAME_OFFSET };
    rows[prologue + 1] = (EmissionFrameRow){ .offset = boundary, .value = rdx - 8, .reg = 0, .kind = EMISSION_FRAME_OFFSET };
    count += 2;
    // The epilogue's release, its first record: its pops, rbp's and the ret follow.
    const uint32_t released = code->count - (pushes + 1 + 1);
    count = emitted_insert_row(rows, count, (EmissionFrameRow){ .offset = released, .reg = 1, .kind = EMISSION_FRAME_RESTORE });
    count = emitted_insert_row(rows, count, (EmissionFrameRow){ .offset = released, .reg = 0, .kind = EMISSION_FRAME_RESTORE });
    for (uint32_t t = 0; t < code->tail_count; t += 1)
    {
        const uint32_t jump = code->tail_records[t];
        if (emitted_handed(code->records, jump))
        {
            // rax's pop's end and rdx's: the jump less the mov, rbp's pop, the other pops and rdx's pop, then one more.
            const uint32_t rdx_popped = jump - 1 - 1 - pushes;
            count = emitted_insert_row(rows, count, (EmissionFrameRow){ .offset = rdx_popped - 1, .reg = 0, .kind = EMISSION_FRAME_RESTORE });
            count = emitted_insert_row(rows, count, (EmissionFrameRow){ .offset = rdx_popped, .reg = 1, .kind = EMISSION_FRAME_RESTORE });
        }
        else
        {
            // A musttail call's (#148), whose release steps over their slots as the epilogue's does: its end, the jump
            // less rbp's pop and the other pops.
            const uint32_t release_end = jump - 1 - pushes;
            count = emitted_insert_row(rows, count, (EmissionFrameRow){ .offset = release_end, .reg = 1, .kind = EMISSION_FRAME_RESTORE });
            count = emitted_insert_row(rows, count, (EmissionFrameRow){ .offset = release_end, .reg = 0, .kind = EMISSION_FRAME_RESTORE });
        }
    }
    return count;
}

// A preserving function's frame (#147, Tier0Code.frame_preserved and frame_preserve_vectors), into its fixed shape's
// `count` rows (room for emitted_tier0_frame_room's more): the rules of the registers it keeps for its caller past the
// callee-saved ones, which no fixed descriptor describes, at every boundary. Each holds the caller's value from the entry
// (same_value, which the CIE gives the callee-saved registers alone); then its slot from its save on: a general register's
// push after the callee-saved ones', in their numbers' order, or a vector or mask register's store into the preserve block
// below the pushes, at its slot's offset from the CFA (a vector register's low 128 bits, as DWARF's XMM numbers name them,
// and LLVM's CFI describes its spills); and its own value again from its reload on: a vector or mask register's load,
// right before the epilogue's release, or a general register's pop, right after it, in the reverse order. The frame has a
// frame pointer, so that none of these moves the CFA, and no tail call or eh_return (unwind_tier0_frame_tails). Returns
// the rows' count.
static uint32_t emitted_tier0_preserve_rows(const Tier0Code* code, EmissionFrameRow* rows, uint32_t count)
{
    // DWARF's numbers of the general registers, by the encoder's; of xmm0 to xmm15, xmm16 to xmm31 and k0 to k7 from these.
    static const uint8_t dwarf[16] = { 0, 2, 1, 3, 7, 6, 4, 5, 8, 9, 10, 11, 12, 13, 14, 15 };
    const uint32_t pushes = (uint32_t)__builtin_popcount(code->frame_saved);
    const uint32_t kept = (uint32_t)__builtin_popcount(code->frame_preserved);
    const uint32_t vectors = code->frame_preserve_vectors;
    const uint32_t block = tier0_preserve_block_bytes(vectors, code->frame_preserve_vector_bytes, code->frame_preserve_mask_bytes);
    const uint32_t moves = block != 0 ? tier0_preserve_moves(vectors, code->frame_preserve_mask_bytes) : 0;
    // Record boundaries (emitted_from_tier0's rows are before branch sizing). The epilogue: the block's loads, the release,
    // the kept registers' pops, the callee-saved ones', rbp's and the ret (an interrupt service routine's iretq, after its
    // add over the error code where it has one).
    const uint32_t release = code->count - 3 - pushes - kept - (code->frame_interrupt == BUSTER_TIER0_INTERRUPT_ERROR_CODE ? 1u : 0u);
    uint32_t pushed = 0;
    for (uint32_t r = 0; r < 16; r += 1)
    {
        if ((code->frame_preserved & (1u << r)) != 0)
        {
            const uint32_t saved = 2 + pushes + pushed + 1;
            const uint32_t popped = release + 1 + kept - pushed;
            const int32_t offset = -(int32_t)(16 + 8 * (pushes + pushed + 1));
            count = emitted_insert_row(rows, count, (EmissionFrameRow){ .offset = 0, .reg = dwarf[r], .kind = EMISSION_FRAME_SAME_VALUE });
            count = emitted_insert_row(rows, count, (EmissionFrameRow){ .offset = saved, .value = offset, .reg = dwarf[r], .kind = EMISSION_FRAME_OFFSET });
            count = emitted_insert_row(rows, count, (EmissionFrameRow){ .offset = popped, .reg = dwarf[r], .kind = EMISSION_FRAME_SAME_VALUE });
            pushed += 1;
        }
    }
    // The block's slots from the CFA, past rbp's push and the others; its stores after its sub.
    int32_t slot = -(int32_t)(16 + 8 * (pushes + kept) + block);
    for (uint32_t k = 0; k < moves; k += 1)
    {
        // The k-th saved register: a vector register in the numbers' order, then the mask registers.
        const uint32_t vector_count = (uint32_t)__builtin_popcount(vectors);
        uint32_t reg = 0;
        uint32_t seen = 0;
        for (uint32_t r = 0; k < vector_count && r < 32; r += 1)
        {
            reg = (vectors & (1u << r)) != 0 && seen == k ? (r < 16 ? 17 + r : 67 + (r - 16)) : reg;
            seen += (vectors & (1u << r)) != 0 ? 1 : 0;
        }
        reg = k < vector_count ? reg : 118 + (k - vector_count);
        const uint32_t stored = 2 + pushes + kept + 1 + k + 1;
        const uint32_t loaded = release - moves + k + 1;
        count = emitted_insert_row(rows, count, (EmissionFrameRow){ .offset = 0, .reg = (uint16_t)reg, .kind = EMISSION_FRAME_SAME_VALUE });
        count = emitted_insert_row(rows, count, (EmissionFrameRow){ .offset = stored, .value = slot, .reg = (uint16_t)reg, .kind = EMISSION_FRAME_OFFSET });
        count = emitted_insert_row(rows, count, (EmissionFrameRow){ .offset = loaded, .reg = (uint16_t)reg, .kind = EMISSION_FRAME_SAME_VALUE });
        slot += k < vector_count ? (int32_t)code->frame_preserve_vector_bytes : 8;
    }
    return count;
}

// A probed reservation's loop without a frame pointer (#177, tier0_emit_probed_reservation): its CFA in r11 from the end
// of r11's subtraction to the loop's bound, which no record of the loop changes, and back in rsp at the loop's exit,
// where rsp equals it, as Tier 1's frame events have it (tier1_frame_emit.c). The fixed shape's rows (`rows`, `count` of
// them, in order) then take the two at their places, before the reservation's own, with the pushes' before them; the
// emission resolves such rows into a row program. Returns the rows' count.
static uint32_t emitted_tier0_probe_rows(const Tier0Code* code, EmissionFrameRow* rows, uint32_t count, uint32_t room)
{
    const uint32_t pushes = (uint32_t)__builtin_popcount(code->frame_saved);
    const uint32_t pages = code->frame_size & ~(BUSTER_TIER0_PAGE_BYTES - 1);
    const bool loop = !code->frame_pointer && pages != 0 && tier0_probes_reservation(code->frame_probe, code->frame_size, code->frame_alignment_log2, false);
    uint32_t result = count;
    if (loop && count + 2 <= room)
    {
        // The records: the pushes, then r11's mov and its subtraction (the bound), the loop's three and its jne (the exit).
        const uint32_t bound = pushes + 2;
        const uint32_t exit = pushes + BUSTER_TIER0_PROBE_LOOP_RECORDS;
        const int32_t cfa = 8 + 8 * (int32_t)pushes + (int32_t)pages;
        uint32_t at = 0;
        while (at < count && rows[at].offset < bound)
        {
            at += 1;
        }
        memmove(rows + at + 2, rows + at, (count - at) * sizeof(EmissionFrameRow));
        rows[at] = (EmissionFrameRow){ .offset = bound, .value = cfa, .reg = (uint16_t)emission_x86_registers[X86_REGISTER_R11], .kind = EMISSION_FRAME_CFA };
        rows[at + 1] = (EmissionFrameRow){ .offset = exit, .value = cfa, .reg = BUSTER_EMISSION_X86_RSP, .kind = EMISSION_FRAME_CFA };
        result = count + 2;
    }
    return loop && count + 2 > room ? UINT32_MAX : result;
}

// Tier 0's code as the emission takes it: its records, columns, references and labels as they are; with `sources` its
// entries' first records as source events (an event per typed entry, the epilogue's first record after them); and with
// `frames` its frame's events, from the frame it published (Tier0Code.frame_*, tail_records) and the records that make
// it, which unwind_tier0_frame_tails checks are the frame's: its transitions at their records' boundaries, in the
// fixed writer's order (unwind_function_rows), from `arena`. A frame that isn't the published one is
// EMITTED_FRAME_UNDESCRIBED; no room, EMITTED_FRAME_MEMORY, with nothing left in `arena`.
static EmittedCode emitted_from_tier0(const Tier0Code* code, bool frames, bool sources, bool assembly, Arena* arena)
{
    EmittedCode result = {
        .records = code->records,
        .count = code->count,
        .branches = code->branches,
        .targets = code->targets,
        .branch_count = code->branch_count,
        .references = code->references,
        .reference_count = code->reference_count,
        .label_records = code->label_records,
        .label_count = code->label_count,
        .asm_label_records = code->asm_label_records,
        .asm_label_names = code->asm_label_names,
        .asm_label_count = code->asm_label_count,
        .assembly = assembly,
        .error = (uint32_t)code->error,
        .node = code->node,
        .deferred = code->deferred,
    };
    if (code->error == TIER0_OK && sources)
    {
        result.local_homes = code->local_homes;
        result.local_count = code->local_count;
        result.source_records = code->entry_records;
        result.source_count = code->entry_count;
    }
    if (code->error == TIER0_OK && frames)
    {
        const uint64_t room = emitted_tier0_frame_room(code);
        EmissionFrameEvent* events = BUSTER_ARENA_PUSH_ARRAY(arena, EmissionFrameEvent, room);
        const uint64_t mark = arena->length;
        const uint64_t tail_room = code->tail_count != 0 ? unwind_tier0_tail_room(code) : 0;
        uint32_t* tails = tail_room != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, tail_room) : 0;
        EmissionFrameRow* rows = BUSTER_ARENA_PUSH_ARRAY(arena, EmissionFrameRow, room);
        result.frame_status = events != 0 && rows != 0 && (tail_room == 0 || tails != 0) ? EMITTED_FRAME_DESCRIBED : EMITTED_FRAME_MEMORY;
        UnwindFunction boundaries;
        // Each transition at its record's boundary: unwind_tier0_frame_tails without final offsets.
        if (result.frame_status == EMITTED_FRAME_DESCRIBED)
        {
            const bool read = unwind_tier0_frame_tails(code, 0, code->count, &boundaries, tails) == UNWIND_OK;
            result.frame_status = read ? EMITTED_FRAME_DESCRIBED : EMITTED_FRAME_UNDESCRIBED;
        }
        const uint32_t fixed = result.frame_status == EMITTED_FRAME_DESCRIBED ? unwind_function_rows(&boundaries, rows, (uint32_t)room) : 0;
        // A probed reservation's loop's rows (#177), a frame without a frame pointer's, and an eh_return function's, one
        // with: never both.
        uint32_t count = fixed <= room ? emitted_tier0_probe_rows(code, rows, fixed, (uint32_t)room) : fixed;
        result.frame_status = count <= room ? result.frame_status : EMITTED_FRAME_UNDESCRIBED;
        const bool eh = result.frame_status == EMITTED_FRAME_DESCRIBED && code->frame_eh_return;
        result.frame_status = !eh || (count + 4 + 2 * (uint64_t)code->tail_count <= room && count >= 3 + (uint32_t)__builtin_popcount(code->frame_saved)) ? result.frame_status :
                                                                                                                                                        EMITTED_FRAME_UNDESCRIBED;
        count = eh && result.frame_status == EMITTED_FRAME_DESCRIBED ? emitted_tier0_eh_rows(code, rows, count) : count;
        // A preserving function's kept registers (#147), never an eh_return function's.
        const uint32_t kept = emitted_tier0_preserved(code);
        result.frame_status = kept == 0 || count + 3 * (uint64_t)kept <= room ? result.frame_status : EMITTED_FRAME_UNDESCRIBED;
        count = kept != 0 && result.frame_status == EMITTED_FRAME_DESCRIBED ? emitted_tier0_preserve_rows(code, rows, count) : count;
        // An interrupt service routine's with an error code (#147), whose CFA is 8 bytes further up, last.
        const bool error_code = code->frame_interrupt == BUSTER_TIER0_INTERRUPT_ERROR_CODE;
        result.frame_status = !error_code || count + 2 <= room ? result.frame_status : EMITTED_FRAME_UNDESCRIBED;
        count = error_code && result.frame_status == EMITTED_FRAME_DESCRIBED ? emitted_tier0_interrupt_rows(code, rows, count) : count;
        result.entry_error_code = error_code;
        for (uint32_t i = 0; result.frame_status == EMITTED_FRAME_DESCRIBED && i < count; i += 1)
        {
            events[i] = (EmissionFrameEvent){
                .at = { .boundary = rows[i].offset, .side = EMISSION_LEFT },
                .value = rows[i].value,
                .reg = rows[i].reg,
                .kind = rows[i].kind,
            };
        }
        arena_pop_to(arena, mark);
        if (result.frame_status == EMITTED_FRAME_DESCRIBED)
        {
            result.frames = count != 0 ? events : 0;
            result.frame_count = count;
        }
    }
    return result;
}

// A function's unwind descriptor from its frame events after branch sizing: `offsets` has each record's start and
// offsets[count] the code's length, `length`, so that record r's end is offsets[r + 1]; resolved through that layout
// (emission_metadata_resolve, which checks the protocol and the layout), put in canonical form (unwind_rows_canonical)
// and lowered (unwind_rows_function) to the fixed descriptor where they spell its simple shape, and otherwise to a row
// program over the rows, in `rows` (frame_count of them), which must outlive the descriptor. UNWIND_ERROR_INVALID for
// events the protocol refuses, UNWIND_ERROR_UNSUPPORTED for rows the writer can't encode and UNWIND_ERROR_LIMIT past the
// format's limits, leaving `output` alone.
static UnwindError emitted_unwind(const EmittedCode* code, const uint32_t* offsets, uint32_t length, bool scalar, EmissionFrameRow* rows, UnwindFunction* output)
{
    const EmissionLayout layout = {
        .starts = offsets,
        .ends = offsets + 1,
        .count = code->count,
        .size = length,
    };
    const EmissionMetadata metadata = {
        .frames = code->frames,
        .frame_count = code->frame_count,
        .register_count = BUSTER_EMISSION_X86_DWARF_REGISTERS,
    };
    UnwindError error = length == 0 || (code->frame_count != 0 && rows == 0) ? UNWIND_ERROR_INVALID : length > INT32_MAX ? UNWIND_ERROR_LIMIT : UNWIND_OK;
    if (error == UNWIND_OK && code->frame_count != 0)
    {
        const EmissionStorage storage = {
            .frames = rows,
            .frame_capacity = code->frame_count,
        };
        EmissionResolved resolved;
        const EmissionCheck check = emission_metadata_resolve(&metadata, &layout, &storage, scalar, &resolved);
        error = check.error == EMISSION_OK ? UNWIND_OK : UNWIND_ERROR_INVALID;
    }
    else if (error == UNWIND_OK)
    {
        // No events: the layout alone, which the frameless descriptor needs nothing of but its length.
        error = layout.count == 0 || offsets[layout.count] == length ? UNWIND_OK : UNWIND_ERROR_INVALID;
    }
    const uint32_t count = error == UNWIND_OK && code->frame_count != 0 ? unwind_rows_canonical(rows, code->frame_count, length) : 0;
    UnwindFunction built;
    error = error == UNWIND_OK ? unwind_rows_function(rows, count, length, &built) : error;
    if (error == UNWIND_OK)
    {
        *output = built;
    }
    return error;
}

// A descriptor's rows: a row program's, or a fixed shape's (unwind_function_rows), from `scratch`; UINT32_MAX rows
// without room.
static const EmissionFrameRow* emitted_descriptor_rows(const UnwindFunction* function, Arena* scratch, uint32_t* count)
{
    const EmissionFrameRow* rows = function->rows;
    *count = function->row_count;
    if (function->open != BUSTER_UNWIND_ROWS)
    {
        const uint32_t needed = unwind_function_rows(function, 0, 0);
        EmissionFrameRow* written = needed != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, EmissionFrameRow, needed) : 0;
        *count = written != 0 || needed == 0 ? unwind_function_rows(function, written, needed) : UINT32_MAX;
        rows = written;
    }
    return rows;
}

// Whether two descriptors are the same CFI: the same size and the same rows in order, a remembered state's offset aside
// (the writers take none for it: unwind_rows_scalar); EMITTED_SAME_MEMORY where `scratch` has no room for a fixed
// shape's rows, which says neither. Leaves `scratch` as it was.
static EmittedSame emitted_unwind_same(const UnwindFunction* a, const UnwindFunction* b, Arena* scratch)
{
    const uint64_t mark = scratch->length;
    uint32_t a_count = 0;
    uint32_t b_count = 0;
    const EmissionFrameRow* a_rows = emitted_descriptor_rows(a, scratch, &a_count);
    const EmissionFrameRow* b_rows = emitted_descriptor_rows(b, scratch, &b_count);
    const bool room = a_count != UINT32_MAX && b_count != UINT32_MAX;
    bool same = room && a->size == b->size && a_count == b_count;
    for (uint32_t i = 0; same && i < a_count; i += 1)
    {
        const EmissionFrameRow x = a_rows[i];
        const EmissionFrameRow y = b_rows[i];
        same = x.kind == y.kind && x.reg == y.reg && x.value == y.value && (x.offset == y.offset || x.kind == EMISSION_FRAME_REMEMBER_STATE);
    }
    arena_pop_to(scratch, mark);
    return !room ? EMITTED_SAME_MEMORY : same ? EMITTED_SAME : EMITTED_DIFFERENT;
}

// ---------------------------------------------------------------------------------------------------------------------
// The frame verifier: events against the records and their control flow

// What the verifier knows at a point of the code, beside the rules: each general register's value as the CFA plus an
// offset (BUSTER_EMITTED_UNKNOWN where it isn't one), which register's value at entry each holds (BUSTER_EMITTED_NONE
// for none), and where each register's entry value is saved in the frame, as an offset from the CFA (the return
// address's, 16, at CFA - 8 from the entry). Registers by the encoder's numbering. `unproven`: a record of a form
// x86_semantics.c doesn't cover is on a path that reaches the point (EMITTED_VERIFY_UNPROVEN).
typedef struct EmittedState EmittedState;
struct EmittedState
{
    int64_t value[16];
    uint8_t holds[16];
    int64_t slots[BUSTER_EMITTED_TRACKED];
    bool reached;
    bool unproven;
};

// The rules the events give at a point: the CFA's register (DWARF) and offset, and each register's rule.
typedef enum EmittedRuleKind
{
    // Nothing checked: a caller-saved register's (the CIE has none), or undefined.
    EMITTED_RULE_UNCHECKED,
    EMITTED_RULE_SAME,
    EMITTED_RULE_OFFSET,
    EMITTED_RULE_REGISTER,
} EmittedRuleKind;

typedef struct EmittedRules EmittedRules;
struct EmittedRules
{
    uint32_t cfa_register;
    int64_t cfa_offset;
    uint8_t kinds[BUSTER_EMITTED_TRACKED];
    int64_t values[BUSTER_EMITTED_TRACKED];
};

static uint32_t emitted_dwarf_gpr(uint32_t dwarf)
{
    uint32_t gpr = 16;
    for (uint32_t g = 0; g < 16; g += 1)
    {
        gpr = emission_x86_registers[g] == dwarf ? g : gpr;
    }
    return gpr;
}

// The CIE's rules: CFA rsp + 8, the return address at CFA - 8, the callee-saved registers their own.
static EmittedRules emitted_initial_rules(void)
{
    EmittedRules rules = { .cfa_register = BUSTER_EMISSION_X86_RSP, .cfa_offset = 8 };
    rules.kinds[BUSTER_EMISSION_X86_RETURN] = EMITTED_RULE_OFFSET;
    rules.values[BUSTER_EMISSION_X86_RETURN] = -8;
    for (uint32_t g = 0; g < 16; g += 1)
    {
        rules.kinds[emission_x86_registers[g]] = ((BUSTER_ABI_CALLEE_SAVED >> g) & 1) != 0 && g != X86_REGISTER_RSP ? EMITTED_RULE_SAME : EMITTED_RULE_UNCHECKED;
    }
    return rules;
}

static EmittedState emitted_entry_state(bool error_code)
{
    EmittedState state = { .reached = true };
    for (uint32_t g = 0; g < 16; g += 1)
    {
        state.value[g] = BUSTER_EMITTED_UNKNOWN;
        state.holds[g] = (uint8_t)g;
    }
    for (uint32_t h = 0; h < BUSTER_EMITTED_TRACKED; h += 1)
    {
        state.slots[h] = BUSTER_EMITTED_UNKNOWN;
    }
    state.value[X86_REGISTER_RSP] = error_code ? -16 : -8;
    state.slots[16] = -8;
    return state;
}

// The meet of two states (what holds on both paths); true if `into` changed.
static bool emitted_meet(EmittedState* into, const EmittedState* from)
{
    bool changed = false;
    if (!into->reached)
    {
        *into = *from;
        changed = from->reached;
    }
    else if (from->reached)
    {
        for (uint32_t g = 0; g < 16; g += 1)
        {
            const bool value = into->value[g] != from->value[g] && into->value[g] != BUSTER_EMITTED_UNKNOWN;
            const bool holds = into->holds[g] != from->holds[g] && into->holds[g] != BUSTER_EMITTED_NONE;
            into->value[g] = value ? BUSTER_EMITTED_UNKNOWN : into->value[g];
            into->holds[g] = holds ? BUSTER_EMITTED_NONE : into->holds[g];
            changed |= value || holds;
        }
        for (uint32_t h = 0; h < BUSTER_EMITTED_TRACKED; h += 1)
        {
            const bool slot = into->slots[h] != from->slots[h] && into->slots[h] != BUSTER_EMITTED_UNKNOWN;
            into->slots[h] = slot ? BUSTER_EMITTED_UNKNOWN : into->slots[h];
            changed |= slot;
        }
        changed |= from->unproven && !into->unproven;
        into->unproven |= from->unproven;
    }
    return changed;
}

static int64_t emitted_add(int64_t value, int64_t amount)
{
    int64_t sum;
    const bool overflow = __builtin_add_overflow(value, amount, &sum);
    return value == BUSTER_EMITTED_UNKNOWN || overflow || sum == BUSTER_EMITTED_UNKNOWN ? BUSTER_EMITTED_UNKNOWN : sum;
}

// A store of `bytes` at CFA + `at` overwrites the slots it overlaps.
static void emitted_clobber_slots(EmittedState* state, int64_t at, uint32_t bytes)
{
    for (uint32_t h = 0; at != BUSTER_EMITTED_UNKNOWN && h < BUSTER_EMITTED_TRACKED; h += 1)
    {
        const int64_t slot = state->slots[h];
        // Compare distances, not interval ends: neither at + bytes nor slot + 8 need fit int64_t.
        const bool overlaps = slot <= at ? (uint64_t)at - (uint64_t)slot < 8 : (uint64_t)slot - (uint64_t)at < bytes;
        state->slots[h] = slot != BUSTER_EMITTED_UNKNOWN && bytes != 0 && overlaps ? BUSTER_EMITTED_UNKNOWN : slot;
    }
}

// Slots below rsp aren't the frame's: a signal handler's frame, or a call's return address, may overwrite them.
static void emitted_release_slots(EmittedState* state)
{
    const int64_t rsp = state->value[X86_REGISTER_RSP];
    for (uint32_t h = 0; rsp != BUSTER_EMITTED_UNKNOWN && h < BUSTER_EMITTED_TRACKED; h += 1)
    {
        state->slots[h] = state->slots[h] != BUSTER_EMITTED_UNKNOWN && state->slots[h] < rsp ? BUSTER_EMITTED_UNKNOWN : state->slots[h];
    }
}

static void emitted_forget(EmittedState* state, uint32_t g)
{
    state->value[g] = BUSTER_EMITTED_UNKNOWN;
    state->holds[g] = BUSTER_EMITTED_NONE;
}

static bool emitted_direct(const X86Instruction* record)
{
    return record->mode == BUSTER_X86_REGISTER_DIRECT;
}

static bool emitted_jump(uint32_t form)
{
    form &= ~BUSTER_X86_FORM_FIXUP;
    return form == X86_FORM_JMP_REL8 || form == X86_FORM_JMP_REL32;
}

// Whether a record ends its path: a ret (an interrupt service routine's iretq, #147), a trap, or a jump out of the
// function (a tail call: a direct jmp with a reference, or Tier 0's and Tier 1's jmp through r11).
static bool emitted_exit(const X86Instruction* record, bool branch)
{
    const uint32_t form = record->form;
    return form == X86_FORM_RET || form == X86_FORM_IRETQ || form == X86_FORM_UD2 || form == X86_FORM_INT3 ||
           (!branch && form == (X86_FORM_JMP_REL32 | BUSTER_X86_FORM_FIXUP)) ||
           (form == X86_FORM_JMP_RM64 && emitted_direct(record) && record->rm == X86_REGISTER_R11);
}

// Whether record i is __builtin_eh_return's jump to its handler (#148, tier0_emit_tail's): through r11 right after rsp's
// mov from rcx, the handler's stack, which no rule describes (nor does LLVM's CFI of its EH_RETURN's ret). The verifier
// checks the rules up to the mov, and not at the jump.
static bool emitted_handed(const X86Instruction* records, uint32_t i)
{
    const X86Instruction* jump = &records[i];
    const X86Instruction* setting = i != 0 ? &records[i - 1] : 0;
    return setting != 0 && jump->form == X86_FORM_JMP_RM64 && emitted_direct(jump) && jump->rm == X86_REGISTER_R11 && setting->form == X86_FORM_MOV_RM64_R64 &&
           emitted_direct(setting) && setting->reg == X86_REGISTER_RCX && setting->rm == X86_REGISTER_RSP;
}

// An indirect jump within the function (a computed goto's, a jump table's): it may go to any of its labels.
static bool emitted_indirect(const X86Instruction* record)
{
    return record->form == X86_FORM_JMP_RM64 && !(emitted_direct(record) && record->rm == X86_REGISTER_R11);
}

// A record's effect on the state, as far as the rules depend on it: pushes and pops, moves, additions, subtractions and
// realignments of the stack and frame registers, loads' effective addresses, calls (which preserve the callee-saved
// registers and rsp, and leave the others unknown), stores into the frame (which overwrite the slots they reach), and
// any other write of a register (its value unknown), by x86_semantics.c's resolved effects. A store through a register
// the verifier doesn't follow is trusted not to reach the save area (the frame's own layout keeps it apart). A form
// x86_semantics.c doesn't cover writes every register and slot, and marks the state unproven.
static void emitted_transfer(const X86Instruction* record, EmittedState* state)
{
    const uint32_t form = record->form & ~BUSTER_X86_FORM_FIXUP;
    const uint32_t rsp = X86_REGISTER_RSP;
    const bool direct = emitted_direct(record);
    if (form == X86_FORM_PUSH_R64 && record->rm < 16)
    {
        state->value[rsp] = emitted_add(state->value[rsp], -8);
        emitted_clobber_slots(state, state->value[rsp], 8);
        const uint32_t held = state->holds[record->rm];
        if (held != BUSTER_EMITTED_NONE && state->value[rsp] != BUSTER_EMITTED_UNKNOWN)
        {
            state->slots[held] = state->value[rsp];
        }
    }
    else if (form == X86_FORM_POP_R64 && record->rm < 16)
    {
        const int64_t at = state->value[rsp];
        uint8_t held = BUSTER_EMITTED_NONE;
        for (uint32_t h = 0; at != BUSTER_EMITTED_UNKNOWN && h < 16; h += 1)
        {
            held = state->slots[h] == at ? (uint8_t)h : held;
        }
        emitted_forget(state, record->rm);
        state->holds[record->rm] = held;
        state->value[rsp] = record->rm == rsp ? BUSTER_EMITTED_UNKNOWN : emitted_add(at, 8);
        emitted_release_slots(state);
    }
    else if ((form == X86_FORM_MOV_RM64_R64 || form == X86_FORM_MOV_R64_RM64) && direct)
    {
        const uint32_t target = form == X86_FORM_MOV_RM64_R64 ? record->rm : record->reg;
        const uint32_t source = form == X86_FORM_MOV_RM64_R64 ? record->reg : record->rm;
        state->value[target] = state->value[source];
        state->holds[target] = state->holds[source];
        emitted_release_slots(state);
    }
    else if (form == X86_FORM_LEA_R64_M && record->mode == BUSTER_X86_MEMORY_BASE)
    {
        const int64_t base = state->value[record->rm];
        emitted_forget(state, record->reg);
        state->value[record->reg] = emitted_add(base, record->displacement);
        emitted_release_slots(state);
    }
    else if ((form == X86_FORM_SUB_RM64_IMM8 || form == X86_FORM_SUB_RM64_IMM32 || form == X86_FORM_ADD_RM64_IMM8 || form == X86_FORM_ADD_RM64_IMM32) && direct)
    {
        const int64_t amount = form == X86_FORM_SUB_RM64_IMM8 || form == X86_FORM_SUB_RM64_IMM32 ? -(int64_t)record->immediate : (int64_t)record->immediate;
        const int64_t value = state->value[record->rm];
        emitted_forget(state, record->rm);
        state->value[record->rm] = emitted_add(value, amount);
        emitted_release_slots(state);
    }
    else if (form == X86_FORM_CALL_REL32 || form == X86_FORM_CALL_RM64)
    {
        // The callee returns with rsp and the callee-saved registers as they were, the others its own.
        for (uint32_t g = 0; g < 16; g += 1)
        {
            if (((BUSTER_ABI_CALLEE_SAVED >> g) & 1) == 0 && g != rsp)
            {
                emitted_forget(state, g);
            }
        }
    }
    else
    {
        uint64_t reads = 0;
        uint64_t writes = 0;
        // With the opmask column, so that a record of opmask registers (AVX-512's masks) writes only what it writes.
        uint16_t opmasks = 0;
        x86_instruction_resolve(record, &reads, &writes, &opmasks);
        const X86Effects* effects = x86_effects(record->form);
        state->unproven |= effects->operation == X86_OPERATION_NONE;
        const bool frame_store = (writes & BUSTER_X86_RESOURCE_MEMORY) != 0 && !direct && record->mode == BUSTER_X86_MEMORY_BASE && record->rm < 16;
        // A store's address is its base register's value as the record reads it, before its own writes: `xchg [rbp],
        // rbp` overwrites the slot rbp pointed at, whatever rbp holds after it.
        const int64_t address = frame_store ? emitted_add(state->value[record->rm], record->displacement) : BUSTER_EMITTED_UNKNOWN;
        for (uint32_t g = 0; g < 16; g += 1)
        {
            if (((writes >> g) & 1) != 0)
            {
                emitted_forget(state, g);
            }
        }
        if ((writes & BUSTER_X86_RESOURCE_MEMORY) != 0)
        {
            if (frame_store)
            {
                const uint32_t bytes = effects->width != 0 ? effects->width : 64;
                emitted_clobber_slots(state, address, bytes);
            }
            else if (effects->operation == X86_OPERATION_NONE)
            {
                // An uncovered form may write anywhere.
                for (uint32_t h = 0; h < BUSTER_EMITTED_TRACKED; h += 1)
                {
                    state->slots[h] = BUSTER_EMITTED_UNKNOWN;
                }
            }
        }
        emitted_release_slots(state);
    }
}

// The equality a compare and the conditional jump right after it establish on one of the jump's edges: `cmp a, b`
// (registers) then jne falls through, or je jumps, where a equals b, the probe loop's exit (#1056). Returns false for an
// edge no path takes: both values known and apart (the loop's first pass, before the back edge's meet). `follows` says
// the compare is in the jump's block, so that it ran on every path to the jump: a jump that starts its block (a branch's
// target or a label) may be reached with flags the compare never set, and isn't refined.
static bool emitted_refine(const X86Instruction* records, uint32_t jump, bool follows, bool taken, EmittedState* state)
{
    const uint32_t form = records[jump].form & ~BUSTER_X86_FORM_FIXUP;
    const bool equal = taken ? form == X86_FORM_JE_REL8 || form == X86_FORM_JE_REL32 : form == X86_FORM_JNE_REL8 || form == X86_FORM_JNE_REL32;
    const X86Instruction* compare = follows && jump > 0 ? &records[jump - 1] : 0;
    bool feasible = true;
    if (equal && compare != 0 && compare->form == X86_FORM_CMP_RM64_R64 && emitted_direct(compare))
    {
        const uint32_t a = compare->rm;
        const uint32_t b = compare->reg;
        feasible = state->value[a] == BUSTER_EMITTED_UNKNOWN || state->value[b] == BUSTER_EMITTED_UNKNOWN || state->value[a] == state->value[b];
        state->value[a] = state->value[a] == BUSTER_EMITTED_UNKNOWN ? state->value[b] : state->value[a];
        state->value[b] = state->value[b] == BUSTER_EMITTED_UNKNOWN ? state->value[a] : state->value[b];
    }
    return feasible;
}

// Applies an event to the rules, with its remembered states. Returns false for an event the protocol refuses.
static bool emitted_apply(const EmissionFrameEvent* event, EmittedRules* rules, EmittedRules* states, uint32_t* depth, bool* deep)
{
    const EmittedRules initial = emitted_initial_rules();
    const uint32_t reg = event->reg;
    bool valid = event->reserved == 0;
    switch (event->kind)
    {
        case EMISSION_FRAME_CFA:
        {
            valid &= reg < 16 && event->value >= 0;
            rules->cfa_register = reg;
            rules->cfa_offset = event->value;
        } break;
        case EMISSION_FRAME_CFA_OFFSET:
        {
            valid &= reg == 0 && event->value >= 0;
            rules->cfa_offset = event->value;
        } break;
        case EMISSION_FRAME_OFFSET:
        case EMISSION_FRAME_SAME_VALUE:
        case EMISSION_FRAME_UNDEFINED:
        case EMISSION_FRAME_REGISTER:
        case EMISSION_FRAME_RESTORE:
        {
            // A vector or mask register's rule (#147, a preserving frame's), which the verifier doesn't follow: a slot or
            // the caller's value, never another register's or the CIE's.
            const bool untracked = emission_x86_saved_vector(reg) && event->kind != EMISSION_FRAME_REGISTER && event->kind != EMISSION_FRAME_RESTORE;
            valid &= untracked || (reg < BUSTER_EMITTED_TRACKED && reg != BUSTER_EMISSION_X86_RSP);
            if (!untracked)
            {
                const uint32_t r = valid ? reg : 0;
                rules->kinds[r] = event->kind == EMISSION_FRAME_OFFSET ? EMITTED_RULE_OFFSET : event->kind == EMISSION_FRAME_REGISTER ? EMITTED_RULE_REGISTER :
                                  event->kind == EMISSION_FRAME_SAME_VALUE ? EMITTED_RULE_SAME : event->kind == EMISSION_FRAME_RESTORE ? initial.kinds[r] :
                                                                                                                                  EMITTED_RULE_UNCHECKED;
                rules->values[r] = event->kind == EMISSION_FRAME_RESTORE ? initial.values[r] : event->value;
                valid &= event->kind != EMISSION_FRAME_REGISTER || (event->value >= 0 && event->value < 16);
            }
        } break;
        case EMISSION_FRAME_REMEMBER_STATE:
        {
            *deep |= *depth >= BUSTER_EMITTED_STATES;
            if (*depth < BUSTER_EMITTED_STATES)
            {
                states[*depth] = *rules;
            }
            *depth += 1;
        } break;
        case EMISSION_FRAME_RESTORE_STATE:
        {
            valid &= *depth != 0;
            *depth -= *depth != 0 ? 1 : 0;
            if (valid && *depth < BUSTER_EMITTED_STATES)
            {
                *rules = states[*depth];
            }
        } break;
        default:
        {
            valid = false;
        } break;
    }
    return valid;
}

// Whether the state establishes the rules: the CFA's register is the CFA less its offset, each callee-saved register
// the rules keep holds its entry value, each saved one is at its slot (the return address's too) or in its register.
static bool emitted_holds(const EmittedRules* rules, const EmittedState* state)
{
    const uint32_t cfa = emitted_dwarf_gpr(rules->cfa_register);
    const int64_t offset = rules->cfa_offset;
    bool valid = cfa < 16 && offset >= 0 && state->value[cfa] != BUSTER_EMITTED_UNKNOWN && state->value[cfa] == -offset;
    for (uint32_t d = 0; valid && d < BUSTER_EMITTED_TRACKED; d += 1)
    {
        const uint32_t g = d < 16 ? emitted_dwarf_gpr(d) : 16;
        switch (rules->kinds[d])
        {
            case EMITTED_RULE_SAME:
            {
                valid = g < 16 && state->holds[g] == g;
            } break;
            case EMITTED_RULE_OFFSET:
            {
                valid = state->slots[g] != BUSTER_EMITTED_UNKNOWN && state->slots[g] == rules->values[d];
            } break;
            case EMITTED_RULE_REGISTER:
            {
                const uint32_t holder = emitted_dwarf_gpr((uint32_t)rules->values[d]);
                valid = holder < 16 && state->holds[holder] == g;
            } break;
            default:
            {
            } break;
        }
    }
    return valid;
}

// Explain a failed emitted_holds without changing its proof: the first failing rule in the same order. This is
// diagnostic work only, after a failure, and emits nothing (the verifier's mutation tests expect refutations).
static EmittedVerdict emitted_failed_rule(const EmittedRules* rules, const EmittedState* state, uint32_t record, uint32_t event)
{
    EmittedVerdict failed = {
        .error = state->unproven ? EMITTED_VERIFY_UNPROVEN : EMITTED_VERIFY_REFUTED,
        .record = record,
        .event = event,
    };
    const uint32_t cfa = emitted_dwarf_gpr(rules->cfa_register);
    const int64_t offset = rules->cfa_offset;
    if (!(cfa < 16 && offset >= 0 && state->value[cfa] != BUSTER_EMITTED_UNKNOWN && state->value[cfa] == -offset))
    {
        failed.rule = EMITTED_VERIFY_RULE_CFA;
        failed.reg = rules->cfa_register;
        failed.expected = offset >= 0 ? -offset : BUSTER_EMITTED_UNKNOWN;
        failed.actual = cfa < 16 ? state->value[cfa] : BUSTER_EMITTED_UNKNOWN;
    }
    for (uint32_t d = 0; failed.rule == EMITTED_VERIFY_RULE_NONE && d < BUSTER_EMITTED_TRACKED; d += 1)
    {
        const uint32_t g = d < 16 ? emitted_dwarf_gpr(d) : 16;
        const uint32_t kind = rules->kinds[d];
        if (kind == EMITTED_RULE_OFFSET && !(state->slots[g] != BUSTER_EMITTED_UNKNOWN && state->slots[g] == rules->values[d]))
        {
            failed.rule = EMITTED_VERIFY_RULE_OFFSET;
            failed.reg = d;
            failed.expected = rules->values[d];
            failed.actual = state->slots[g];
        }
        else if (kind == EMITTED_RULE_SAME || kind == EMITTED_RULE_REGISTER)
        {
            const uint32_t holder = kind == EMITTED_RULE_SAME ? g : emitted_dwarf_gpr((uint32_t)rules->values[d]);
            const uint32_t held = holder < 16 ? state->holds[holder] : BUSTER_EMITTED_NONE;
            if (!(holder < 16 && held == g))
            {
                failed.rule = kind == EMITTED_RULE_SAME ? EMITTED_VERIFY_RULE_SAME : EMITTED_VERIFY_RULE_REGISTER;
                failed.reg = d;
                failed.holder = kind == EMITTED_RULE_SAME ? d : (uint32_t)rules->values[d];
                failed.expected = d;
                failed.actual = held < 16 ? emission_x86_registers[held] : BUSTER_EMITTED_UNKNOWN;
            }
        }
    }
    return failed;
}

// Proves a function's frame events against its records and their control flow, as an unwinder reads them: at every
// record the code reaches, the rules in force there (the events anchored at or before its start, in order, from the
// CIE's) must hold of every path that reaches it. A forward dataflow over the code's blocks (from its entry, each
// branch to its target and on, each indirect jump to every label, a ret, a trap or a tail call's jump ending its path)
// follows each register's value as the CFA plus an offset, which register's entry value each holds and where in the
// frame each entry value is saved (EmittedState), to its fixpoint, the paths' meet at each block; then each reached
// record is checked against the rules (emitted_holds); a rule that fails on a path through a form x86_semantics.c
// doesn't cover is unproven rather than refuted. Code nothing reaches isn't checked. Events at a boundary's two sides
// both hold from the record after it: a layout's padding between them executes nothing. An assembly template's code
// isn't the compiler's: its caller doesn't verify it. Scalar: this runs with --verify and in the tests, off the
// compile's path. Its memory, the leaders' states and two words per record, from `scratch`, is given back.
// The DWARF number of the vector or mask register `record` moves whole between it and a preserving frame's slot at
// [rbp + displacement] (#147, tier0_emit_preserve's forms): a store into the slot with `store`, else a load back from it;
// UINT32_MAX for any other. Whole: the form exactly one of those, with no segment override or fixup, in its encoding's
// registers (xmm16 to xmm31 an EVEX form's alone), its address rbp's alone, and no other operand: no opmask, zeroing or
// rounding (EVEX's decorations, an opmask's store or load partial), index, vvvv or immediate.
static uint32_t emitted_saved_vector_move(const X86Instruction* record, bool store)
{
    const uint32_t form = record->form;
    const bool legacy = store ? form == X86_FORM_MOVUPS_XM_X || form == X86_FORM_VMOVDQU_YM256_Y : form == X86_FORM_MOVUPS_X_XM || form == X86_FORM_VMOVDQU_Y_YM256;
    const bool evex = store ? form == X86_FORM_VMOVDQU64_ZM512_Z : form == X86_FORM_VMOVDQU64_Z_ZM512;
    const bool mask = store ? form == X86_FORM_KMOVQ_M64_K || form == X86_FORM_KMOVW_M16_K : form == X86_FORM_KMOVQ_K_M64 || form == X86_FORM_KMOVW_K_M16;
    const uint32_t reg = record->reg;
    const bool whole = record->rm == X86_REGISTER_RBP && record->mode == BUSTER_X86_MEMORY_BASE && record->index == 0 && record->vvvv == 0 && record->decorations == 0 &&
                       record->immediate == 0;
    const bool vector = whole && ((legacy && reg < 16) || (evex && reg < 32));
    return vector ? (reg < 16 ? 17 + reg : 67 + (reg - 16)) : whole && mask && reg < 8 ? 118 + reg : UINT32_MAX;
}

// A vector or mask register's rules (#147, a preserving frame's: emitted_tier0_preserve_rows), which the fixpoint below
// doesn't follow, proved by the records at their boundaries: a slot's rule right after the whole store of that register
// into that slot from rbp (emitted_saved_vector_move: a masked one is no save), the CFA rbp's by then (plus 16, or an
// interrupt service routine's with an error code plus 24); and its caller's value at the entry, or right after its whole
// load back from the slot its rule named. The frame keeps rbp and the slots past the body's places (tier0_layout_frame).
static EmittedVerdict emitted_verify_saved_vectors(const EmittedCode* code)
{
    EmittedVerdict verdict = { .error = EMITTED_VERIFY_OK, .event = code->frame_count };
    int64_t slots[BUSTER_EMISSION_X86_DWARF_REGISTERS];
    for (uint32_t r = 0; r < BUSTER_EMISSION_X86_DWARF_REGISTERS; r += 1)
    {
        slots[r] = BUSTER_EMITTED_UNKNOWN;
    }
    // The CFA's offset from rbp where the CFA is rbp's (16, an interrupt service routine's with an error code 24, #147),
    // BUSTER_EMITTED_UNKNOWN where it isn't.
    int64_t framed = BUSTER_EMITTED_UNKNOWN;
    bool rbp = false;
    for (uint32_t e = 0; verdict.error == EMITTED_VERIFY_OK && e < code->frame_count; e += 1)
    {
        const EmissionFrameEvent* event = &code->frames[e];
        const uint32_t boundary = event->at.boundary;
        const uint32_t kind = event->kind;
        rbp = kind == EMISSION_FRAME_CFA ? event->reg == BUSTER_EMISSION_X86_RBP : rbp;
        framed = kind == EMISSION_FRAME_CFA || kind == EMISSION_FRAME_CFA_OFFSET ? (rbp ? event->value : BUSTER_EMITTED_UNKNOWN) : framed;
        if (emission_x86_saved_vector(event->reg) && kind != EMISSION_FRAME_CFA && kind != EMISSION_FRAME_CFA_OFFSET)
        {
            const X86Instruction* record = boundary != 0 && boundary <= code->count ? &code->records[boundary - 1] : NULL;
            bool proved = false;
            if (kind == EMISSION_FRAME_OFFSET)
            {
                // The slot from rbp, where the load back must find it.
                slots[event->reg] = framed != BUSTER_EMITTED_UNKNOWN ? event->value + framed : BUSTER_EMITTED_UNKNOWN;
                proved = framed != BUSTER_EMITTED_UNKNOWN && record != NULL && emitted_saved_vector_move(record, true) == event->reg &&
                         record->displacement == slots[event->reg];
            }
            else if (kind == EMISSION_FRAME_SAME_VALUE)
            {
                proved = boundary == 0 || (record != NULL && emitted_saved_vector_move(record, false) == event->reg && slots[event->reg] != BUSTER_EMITTED_UNKNOWN &&
                                           record->displacement == slots[event->reg]);
            }
            verdict = proved ? verdict : (EmittedVerdict){ .error = EMITTED_VERIFY_REFUTED, .record = boundary, .event = e };
        }
    }
    return verdict;
}

static EmittedVerdict emitted_verify_frames(const EmittedCode* code, Arena* scratch)
{
    // A vector or mask register's rules (#147), which the fixpoint doesn't follow, first.
    EmittedVerdict verdict = emitted_verify_saved_vectors(code);
    const uint32_t count = code->count;
    const uint64_t mark = scratch->length;
    // Each record's branch target (UINT32_MAX for none), and the leaders: the blocks' first records, each's state.
    uint32_t* targets = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, count) : 0;
    uint32_t* leaders = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    verdict.error = count != 0 && (targets == 0 || leaders == 0) ? EMITTED_VERIFY_MEMORY : verdict.error;
    for (uint32_t i = 0; verdict.error == EMITTED_VERIFY_OK && i < count; i += 1)
    {
        targets[i] = UINT32_MAX;
        leaders[i] = UINT32_MAX;
    }
    for (uint32_t k = 0; verdict.error == EMITTED_VERIFY_OK && k < code->branch_count; k += 1)
    {
        const uint32_t record = code->branches[k];
        const uint32_t target = code->targets[k];
        verdict.error = record < count && target < count ? verdict.error : EMITTED_VERIFY_INVALID;
        if (verdict.error == EMITTED_VERIFY_OK)
        {
            targets[record] = target;
            leaders[target] = 0;
            if (record + 1 < count)
            {
                leaders[record + 1] = 0;
            }
        }
    }
    for (uint32_t k = 0; verdict.error == EMITTED_VERIFY_OK && k < code->label_count; k += 1)
    {
        if (code->label_records[k] < count)
        {
            leaders[code->label_records[k]] = 0;
        }
    }
    for (uint32_t i = 0; verdict.error == EMITTED_VERIFY_OK && i + 1 < count; i += 1)
    {
        const X86Instruction* record = &code->records[i];
        if (emitted_exit(record, targets[i] != UINT32_MAX) || emitted_indirect(record))
        {
            leaders[i + 1] = 0;
        }
    }
    uint32_t leader_count = 0;
    if (verdict.error == EMITTED_VERIFY_OK && count != 0)
    {
        leaders[0] = 0;
        for (uint32_t i = 0; i < count; i += 1)
        {
            leaders[i] = leaders[i] != UINT32_MAX ? leader_count++ : UINT32_MAX;
        }
    }
    EmittedState* states = leader_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, EmittedState, leader_count) : 0;
    uint32_t* starts = leader_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, leader_count) : 0;
    uint32_t* work = leader_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, leader_count) : 0;
    uint8_t* queued = leader_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, leader_count) : 0;
    verdict.error = leader_count != 0 && (states == 0 || starts == 0 || work == 0 || queued == 0) ? EMITTED_VERIFY_MEMORY : verdict.error;
    uint32_t pending = 0;
    if (verdict.error == EMITTED_VERIFY_OK && leader_count != 0)
    {
        for (uint32_t i = 0; i < count; i += 1)
        {
            if (leaders[i] != UINT32_MAX)
            {
                starts[leaders[i]] = i;
                states[leaders[i]] = (EmittedState){ .reached = false };
                queued[leaders[i]] = 0;
            }
        }
        states[0] = emitted_entry_state(code->entry_error_code);
        work[pending++] = 0;
        queued[0] = 1;
    }
    // The fixpoint: each block from its state to its end, each successor's state met with what reaches it.
    while (verdict.error == EMITTED_VERIFY_OK && pending != 0)
    {
        pending -= 1;
        const uint32_t leader = work[pending];
        queued[leader] = 0;
        EmittedState state = states[leader];
        for (uint32_t i = starts[leader]; i < count; i += 1)
        {
            const X86Instruction* record = &code->records[i];
            const bool branch = targets[i] != UINT32_MAX;
            emitted_transfer(record, &state);
            uint32_t successors[2];
            EmittedState edges[2];
            uint32_t successor_count = 0;
            bool stop = emitted_exit(record, branch);
            if (branch)
            {
                // The record before this one ran on this path only if it is in this block.
                const bool follows = i > starts[leader];
                edges[successor_count] = state;
                const bool taken = emitted_refine(code->records, i, follows, true, &edges[successor_count]);
                successors[successor_count] = leaders[targets[i]];
                successor_count += taken ? 1 : 0;
                stop = emitted_jump(record->form) || !emitted_refine(code->records, i, follows, false, &state);
            }
            if (!stop && emitted_indirect(record))
            {
                for (uint32_t k = 0; k < code->label_count; k += 1)
                {
                    const uint32_t to = code->label_records[k] < count ? leaders[code->label_records[k]] : UINT32_MAX;
                    if (to != UINT32_MAX && emitted_meet(&states[to], &state) && !queued[to])
                    {
                        queued[to] = 1;
                        work[pending++] = to;
                    }
                }
                stop = true;
            }
            if (!stop && i + 1 < count && leaders[i + 1] != UINT32_MAX)
            {
                edges[successor_count] = state;
                successors[successor_count++] = leaders[i + 1];
                stop = true;
            }
            for (uint32_t k = 0; k < successor_count; k += 1)
            {
                const uint32_t to = successors[k];
                if (emitted_meet(&states[to], &edges[k]) && !queued[to])
                {
                    queued[to] = 1;
                    work[pending++] = to;
                }
            }
            if (stop)
            {
                break;
            }
        }
    }
    // The check: the records in order, the rules as the events give them, each reached record's state against them.
    EmittedRules rules = emitted_initial_rules();
    EmittedRules remembered[BUSTER_EMITTED_STATES];
    uint32_t depth = 0;
    bool deep = false;
    uint32_t event = 0;
    EmittedState state = { .reached = false };
    EmissionAnchor previous = { 0 };
    // The first rule left unproven, which a refutation anywhere in the code overrides.
    EmittedVerdict unproven = { .error = EMITTED_VERIFY_OK };
    for (uint32_t i = 0; verdict.error == EMITTED_VERIFY_OK && i <= count; i += 1)
    {
        for (; verdict.error == EMITTED_VERIFY_OK && event < code->frame_count && code->frames[event].at.boundary <= i; event += 1)
        {
            const EmissionFrameEvent* applied = &code->frames[event];
            const bool ordered = !emission_anchor_before(applied->at, previous) && applied->at.side <= EMISSION_RIGHT;
            verdict.error = ordered && emitted_apply(applied, &rules, remembered, &depth, &deep) ? verdict.error : EMITTED_VERIFY_INVALID;
            verdict.error = verdict.error == EMITTED_VERIFY_OK && deep ? EMITTED_VERIFY_UNSUPPORTED : verdict.error;
            verdict.record = verdict.error == EMITTED_VERIFY_OK ? verdict.record : i;
            verdict.event = verdict.error == EMITTED_VERIFY_OK ? verdict.event : event;
            previous = applied->at;
        }
        if (verdict.error != EMITTED_VERIFY_OK || i == count)
        {
            break;
        }
        state = leaders[i] != UINT32_MAX ? states[leaders[i]] : state;
        if (state.reached && !emitted_handed(code->records, i) && !emitted_holds(&rules, &state))
        {
            const EmittedVerdict failed = emitted_failed_rule(&rules, &state, i, event != 0 ? event - 1 : code->frame_count);
            verdict = state.unproven ? verdict : failed;
            unproven = state.unproven && unproven.error == EMITTED_VERIFY_OK ? failed : unproven;
        }
        if (state.reached)
        {
            emitted_transfer(&code->records[i], &state);
            if (targets[i] != UINT32_MAX)
            {
                // A leader's state is its paths' meet, so the record before it didn't run on all of them.
                (void)emitted_refine(code->records, i, leaders[i] == UINT32_MAX, false, &state);
            }
        }
    }
    // Events past the code: an anchor beyond its end.
    if (verdict.error == EMITTED_VERIFY_OK && event != code->frame_count)
    {
        verdict = (EmittedVerdict){ .error = EMITTED_VERIFY_INVALID, .record = count, .event = event };
    }
    verdict = verdict.error == EMITTED_VERIFY_OK ? unproven : verdict;
    arena_pop_to(scratch, mark);
    return verdict;
}
