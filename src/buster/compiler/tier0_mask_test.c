// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// test-tier0's masks check (#179): Tier 0's mask class, its values in k1 to k7 by the mask stack's depths and in their
// frame slots past them. Programs over masks are compiled by both paths, which must give the same records, encoded by
// both encoders, which must give the same bytes, linked with a callee of their own (whose masks overwrite k1 to k7) and,
// where the host has AVX-512 F, BW and DQ, run natively in a child process:
//   - random programs: statements that each store a mask of 1, 2, 4 or 8 bytes, or an integer converted from one, into
//     an output slot, or drop one, from masks loaded from the inputs, converted from integers (constants, loads of every
//     integer class, a call's results) or from frame objects of each width, NOT, AND, OR and XOR, DUP, STORE's kept mask,
//     ?: of masks, chains that pass k7 (where AND, OR and XOR meet masks in their slots), calls made with masks live, and
//     the rows of tier0_test_mask_row (by role, with scratch registers, from and to integers, and by number), against
//     this file's own stack machine over the same stream (tier0_test_mask_model) and, for the programs without rows or
//     objects, which interpreter.c doesn't model, the typed interpreter's (#176);
//   - fixtures: a zero-masked vector move whose decoration is a mask role, under no live mask and under seven (a row
//     past the registers, which borrows them), against C;
//   - the rules: each break of the class's refused by tier0_validate, and its valid neighbours taken.
// main.c includes it after interpreter_test.c, whose interpreter it runs; tier0_test.c calls it (tier0_test_masks).

#define BUSTER_TIER0_TEST_MASK_SLOTS 16
#define BUSTER_TIER0_TEST_MASK_ENTRIES 4096
#define BUSTER_TIER0_TEST_MASK_TASKS 1024
#define BUSTER_TIER0_TEST_MASK_STATEMENTS 6
#define BUSTER_TIER0_TEST_MASK_VALUES 256
#define BUSTER_TIER0_TEST_MASK_PROGRAMS 300
// The program's function's locals: its two pointer parameters (the inputs' and the outputs'), then one frame object per
// mask width (1, 2, 4 and 8 bytes) where it has them.
#define BUSTER_TIER0_TEST_MASK_IN 0
#define BUSTER_TIER0_TEST_MASK_OUT 1
#define BUSTER_TIER0_TEST_MASK_OBJECTS 4
// Symbols: the entry, and the callee every call calls.
#define BUSTER_TIER0_TEST_MASK_ENTRY 1
#define BUSTER_TIER0_TEST_MASK_LEAF 2

// ---------------------------------------------------------------------------------------------------------------------
// The rows

// Each row of a width (tier0_test_mask_width's index: 1, 2, 4 and 8 bytes), and the one row of none, by number.
#define BUSTER_TIER0_TEST_MASK_ROW_ANDN 0
#define BUSTER_TIER0_TEST_MASK_ROW_ADD 1
#define BUSTER_TIER0_TEST_MASK_ROW_SHIFT 2
#define BUSTER_TIER0_TEST_MASK_ROW_SCRATCH 3
#define BUSTER_TIER0_TEST_MASK_ROW_FROM 4
#define BUSTER_TIER0_TEST_MASK_ROW_TO 5
#define BUSTER_TIER0_TEST_MASK_ROW_KINDS 6
#define BUSTER_TIER0_TEST_MASK_ROW_NUMBERED (4 * BUSTER_TIER0_TEST_MASK_ROW_KINDS)
#define BUSTER_TIER0_TEST_MASK_ROWS (BUSTER_TIER0_TEST_MASK_ROW_NUMBERED + 1)

static Tier0Operation tier0_test_mask_rows[BUSTER_TIER0_TEST_MASK_ROWS];

static uint32_t tier0_test_mask_width(uint32_t bytes)
{
    return bytes == 1 ? 0u : bytes == 2 ? 1u : bytes == 4 ? 2u : 3u;
}

static uint64_t tier0_test_mask_bits(uint32_t bytes)
{
    return bytes >= 8 ? UINT64_MAX : (UINT64_C(1) << (8 * bytes)) - 1;
}

static X86Instruction tier0_test_mask_record(uint32_t form, uint8_t reg, uint8_t vvvv, uint8_t rm)
{
    X86Instruction record;
    memset(&record, 0, sizeof(record));
    record.form = (uint16_t)form;
    record.reg = reg;
    record.vvvv = vvvv;
    record.rm = rm;
    return record;
}

// The rows, by role but the last: for each width, ANDN (~a & b), ADD (a + b), SHIFT (a << the width's bytes plus 1),
// SCRATCH (a | a >> 1, through a scratch register) of masks, FROM (an integer's low bits as a mask) and TO (a mask as an
// integer); and NUMBERED, which names k1 and k2 by number to complement an integer.
static void tier0_test_mask_build_rows(void)
{
    static const uint16_t andn[4] = { X86_FORM_KANDNB_K_K_K, X86_FORM_KANDNW_K_K_K, X86_FORM_KANDND_K_K_K, X86_FORM_KANDNQ_K_K_K };
    static const uint16_t add[4] = { X86_FORM_KADDB_K_K_K, X86_FORM_KADDW_K_K_K, X86_FORM_KADDD_K_K_K, X86_FORM_KADDQ_K_K_K };
    static const uint16_t left[4] = { X86_FORM_KSHIFTLB_K_K_IMM8, X86_FORM_KSHIFTLW_K_K_IMM8, X86_FORM_KSHIFTLD_K_K_IMM8, X86_FORM_KSHIFTLQ_K_K_IMM8 };
    static const uint16_t right[4] = { X86_FORM_KSHIFTRB_K_K_IMM8, X86_FORM_KSHIFTRW_K_K_IMM8, X86_FORM_KSHIFTRD_K_K_IMM8, X86_FORM_KSHIFTRQ_K_K_IMM8 };
    static const uint16_t or[4] = { X86_FORM_KORB_K_K_K, X86_FORM_KORW_K_K_K, X86_FORM_KORD_K_K_K, X86_FORM_KORQ_K_K_K };
    static const uint16_t in[4] = { X86_FORM_KMOVB_K_R32, X86_FORM_KMOVW_K_R32, X86_FORM_KMOVD_K_R32, X86_FORM_KMOVQ_K_R64 };
    static const uint16_t out[4] = { X86_FORM_KMOVB_R32_K, X86_FORM_KMOVW_R32_K, X86_FORM_KMOVD_R32_K, X86_FORM_KMOVQ_R64_K };
    const uint8_t m0 = BUSTER_TIER0_ROLE_MASK(0);
    const uint8_t m1 = BUSTER_TIER0_ROLE_MASK(1);
    const uint8_t result = BUSTER_TIER0_ROLE_MASK_RESULT;
    const uint8_t scratch = BUSTER_TIER0_ROLE_MASK_SCRATCH(0);
    memset(tier0_test_mask_rows, 0, sizeof(tier0_test_mask_rows));
    for (uint32_t w = 0; w < 4; w += 1)
    {
        const uint8_t bytes = (uint8_t)(1u << w);
        Tier0Operation* rows = tier0_test_mask_rows + BUSTER_TIER0_TEST_MASK_ROW_KINDS * w;
        for (uint32_t k = 0; k < BUSTER_TIER0_TEST_MASK_ROW_KINDS; k += 1)
        {
            rows[k].result_class = BUSTER_TYPED_CLASS_MASK;
            rows[k].result_bytes = bytes;
            rows[k].operand_count = 1;
            rows[k].operand_classes[0] = BUSTER_TYPED_CLASS_MASK;
            rows[k].operand_bytes[0] = bytes;
            rows[k].operand_classes[1] = BUSTER_TYPED_CLASS_MASK;
            rows[k].operand_bytes[1] = bytes;
            rows[k].record_count = 1;
        }
        rows[BUSTER_TIER0_TEST_MASK_ROW_ANDN].operand_count = 2;
        rows[BUSTER_TIER0_TEST_MASK_ROW_ANDN].records[0] = tier0_test_mask_record(andn[w], result, m0, m1);
        rows[BUSTER_TIER0_TEST_MASK_ROW_ADD].operand_count = 2;
        rows[BUSTER_TIER0_TEST_MASK_ROW_ADD].records[0] = tier0_test_mask_record(add[w], result, m0, m1);
        rows[BUSTER_TIER0_TEST_MASK_ROW_SHIFT].records[0] = tier0_test_mask_record(left[w], result, 0, m0);
        rows[BUSTER_TIER0_TEST_MASK_ROW_SHIFT].records[0].immediate = (int32_t)bytes + 1;
        rows[BUSTER_TIER0_TEST_MASK_ROW_SCRATCH].records[0] = tier0_test_mask_record(right[w], scratch, 0, m0);
        rows[BUSTER_TIER0_TEST_MASK_ROW_SCRATCH].records[0].immediate = 1;
        rows[BUSTER_TIER0_TEST_MASK_ROW_SCRATCH].records[1] = tier0_test_mask_record(or[w], result, m0, scratch);
        rows[BUSTER_TIER0_TEST_MASK_ROW_SCRATCH].record_count = 2;
        rows[BUSTER_TIER0_TEST_MASK_ROW_FROM].operand_classes[0] = BUSTER_TYPED_CLASS_U64;
        rows[BUSTER_TIER0_TEST_MASK_ROW_FROM].operand_bytes[0] = 0;
        rows[BUSTER_TIER0_TEST_MASK_ROW_FROM].records[0] = tier0_test_mask_record(in[w], result, 0, BUSTER_TIER0_ROLE_INTEGER(0));
        rows[BUSTER_TIER0_TEST_MASK_ROW_TO].result_class = BUSTER_TYPED_CLASS_U64;
        rows[BUSTER_TIER0_TEST_MASK_ROW_TO].result_bytes = 0;
        rows[BUSTER_TIER0_TEST_MASK_ROW_TO].records[0] = tier0_test_mask_record(out[w], BUSTER_TIER0_ROLE_INTEGER_RESULT, 0, m0);
    }
    Tier0Operation* numbered = &tier0_test_mask_rows[BUSTER_TIER0_TEST_MASK_ROW_NUMBERED];
    numbered->operand_count = 1;
    numbered->operand_classes[0] = BUSTER_TYPED_CLASS_U64;
    numbered->result_class = BUSTER_TYPED_CLASS_U64;
    numbered->records[0] = tier0_test_mask_record(X86_FORM_KMOVQ_K_R64, 1, 0, BUSTER_TIER0_ROLE_INTEGER(0));
    numbered->records[1] = tier0_test_mask_record(X86_FORM_KNOTQ_K_K, 2, 0, 1);
    numbered->records[2] = tier0_test_mask_record(X86_FORM_KMOVQ_R64_K, BUSTER_TIER0_ROLE_INTEGER_RESULT, 0, 2);
    numbered->record_count = 3;
}

// A row's result from its operands (a and b, masks of its width or an integer), as its instructions compute it: the
// model's (tier0_test_mask_model's).
static uint64_t tier0_test_mask_row_model(uint32_t row, uint64_t a, uint64_t b)
{
    uint64_t result = ~a;
    if (row < BUSTER_TIER0_TEST_MASK_ROW_NUMBERED)
    {
        const uint32_t bytes = 1u << (row / BUSTER_TIER0_TEST_MASK_ROW_KINDS);
        const uint64_t bits = tier0_test_mask_bits(bytes);
        const uint32_t kind = row % BUSTER_TIER0_TEST_MASK_ROW_KINDS;
        result = kind == BUSTER_TIER0_TEST_MASK_ROW_ANDN    ? ~a & b
                 : kind == BUSTER_TIER0_TEST_MASK_ROW_ADD   ? a + b
                 : kind == BUSTER_TIER0_TEST_MASK_ROW_SHIFT ? a << (bytes + 1)
                 : kind == BUSTER_TIER0_TEST_MASK_ROW_SCRATCH ? a | (a >> 1)
                                                               : a;
        result &= bits;
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The programs

typedef struct Tier0TestMaskProgram Tier0TestMaskProgram;
struct Tier0TestMaskProgram
{
    uint8_t kinds[BUSTER_TIER0_TEST_MASK_ENTRIES];
    uint8_t classes[BUSTER_TIER0_TEST_MASK_ENTRIES];
    uint64_t values[BUSTER_TIER0_TEST_MASK_ENTRIES];
    uint32_t count;
    bool rows;
    bool objects;
    bool overflow;
};

// The generator's tasks, run from a stack (the last pushed first), so that an expression's entries come out in postorder
// with no recursion: an expression of a mask's width or an integer (U64), an entry, a branch or a jump whose target a
// later LABEL task gives (its patch slot), and a label.
#define BUSTER_TIER0_TEST_MASK_TASK_MASK 0
#define BUSTER_TIER0_TEST_MASK_TASK_INTEGER 1
#define BUSTER_TIER0_TEST_MASK_TASK_ENTRY 2
#define BUSTER_TIER0_TEST_MASK_TASK_BRANCH 3
#define BUSTER_TIER0_TEST_MASK_TASK_JUMP 4
#define BUSTER_TIER0_TEST_MASK_TASK_LABEL 5

typedef struct Tier0TestMaskTask Tier0TestMaskTask;
struct Tier0TestMaskTask
{
    uint8_t task;
    uint8_t kind;
    uint8_t class;
    uint32_t budget;
    uint64_t value;
};

typedef struct Tier0TestMaskTasks Tier0TestMaskTasks;
struct Tier0TestMaskTasks
{
    Tier0TestMaskTask tasks[BUSTER_TIER0_TEST_MASK_TASKS];
    uint32_t count;
    uint32_t patches[BUSTER_TIER0_TEST_MASK_TASKS];
    uint32_t patch_count;
};

static uint64_t tier0_test_mask_random(uint64_t* state)
{
    *state ^= *state << 13;
    *state ^= *state >> 7;
    *state ^= *state << 17;
    return *state;
}

static void tier0_test_mask_emit(Tier0TestMaskProgram* program, uint32_t kind, uint32_t class, uint64_t value)
{
    if (program->count < BUSTER_TIER0_TEST_MASK_ENTRIES)
    {
        program->kinds[program->count] = (uint8_t)kind;
        program->classes[program->count] = (uint8_t)class;
        program->values[program->count] = value;
        program->count += 1;
    }
    else
    {
        program->overflow = true;
    }
}

static void tier0_test_mask_push(Tier0TestMaskProgram* program, Tier0TestMaskTasks* tasks, Tier0TestMaskTask task)
{
    if (tasks->count < BUSTER_TIER0_TEST_MASK_TASKS)
    {
        tasks->tasks[tasks->count] = task;
        tasks->count += 1;
    }
    else
    {
        program->overflow = true;
    }
}

static Tier0TestMaskTask tier0_test_mask_entry(uint32_t kind, uint32_t class, uint64_t value)
{
    return (Tier0TestMaskTask){ .task = BUSTER_TIER0_TEST_MASK_TASK_ENTRY, .kind = (uint8_t)kind, .class = (uint8_t)class, .value = value };
}

static Tier0TestMaskTask tier0_test_mask_expression(uint32_t task, uint32_t bytes, uint32_t budget)
{
    return (Tier0TestMaskTask){ .task = (uint8_t)task, .class = (uint8_t)bytes, .budget = budget };
}

// The address of slot `slot` of the inputs or the outputs (local `pointer`): its pointer plus 8 bytes a slot.
static void tier0_test_mask_address(Tier0TestMaskProgram* program, uint32_t pointer, uint32_t slot)
{
    tier0_test_mask_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, pointer);
    tier0_test_mask_emit(program, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 8u * slot);
    tier0_test_mask_emit(program, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, 1);
}

// A mask expression's task of `bytes` and `budget`, pushed as the tasks that make it: each choice's entries after its
// operands', in the reverse of their order, as the stack runs them.
static void tier0_test_mask_choose_mask(Tier0TestMaskProgram* program, Tier0TestMaskTasks* tasks, uint32_t bytes, uint32_t budget, uint64_t* state)
{
    const uint32_t choice = budget == 0 ? (uint32_t)(tier0_test_mask_random(state) % 3) : (uint32_t)(tier0_test_mask_random(state) % 16);
    const uint32_t half = budget / 2;
    const uint32_t width = tier0_test_mask_width(bytes);
    if (choice == 0)
    {
        // A load from an input.
        tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, (uint32_t)(tier0_test_mask_random(state) % BUSTER_TIER0_TEST_MASK_SLOTS));
        tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_MASK, bytes);
    }
    else if (choice == 1)
    {
        // An integer of any class loaded and converted, extended first where it is narrower.
        const uint32_t class = BUSTER_TYPED_CLASS_I8 + (uint32_t)(tier0_test_mask_random(state) % 8);
        tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, (uint32_t)(tier0_test_mask_random(state) % BUSTER_TIER0_TEST_MASK_SLOTS));
        tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, class, 0);
        tier0_test_mask_emit(program, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_MASK, bytes);
    }
    else if (choice == 2 && program->objects)
    {
        tier0_test_mask_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_MASK, 2 + width);
    }
    else if (choice == 2)
    {
        // A constant converted.
        tier0_test_mask_emit(program, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, tier0_test_mask_random(state));
        tier0_test_mask_emit(program, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_MASK, bytes);
    }
    else if (choice == 3)
    {
        tier0_test_mask_push(program, tasks, tier0_test_mask_entry(BUSTER_TYPED_NOT, BUSTER_TYPED_CLASS_MASK, bytes));
        tier0_test_mask_push(program, tasks, tier0_test_mask_expression(BUSTER_TIER0_TEST_MASK_TASK_MASK, bytes, budget - 1));
    }
    else if (choice <= 6)
    {
        static const uint8_t logic[3] = { BUSTER_TYPED_AND, BUSTER_TYPED_OR, BUSTER_TYPED_XOR };
        tier0_test_mask_push(program, tasks, tier0_test_mask_entry(logic[choice - 4], BUSTER_TYPED_CLASS_MASK, bytes));
        tier0_test_mask_push(program, tasks, tier0_test_mask_expression(BUSTER_TIER0_TEST_MASK_TASK_MASK, bytes, half));
        tier0_test_mask_push(program, tasks, tier0_test_mask_expression(BUSTER_TIER0_TEST_MASK_TASK_MASK, bytes, budget - 1 - half));
    }
    else if (choice == 7)
    {
        // DUP, then a logic of the two copies.
        tier0_test_mask_push(program, tasks, tier0_test_mask_entry(BUSTER_TYPED_OR, BUSTER_TYPED_CLASS_MASK, bytes));
        tier0_test_mask_push(program, tasks, tier0_test_mask_entry(BUSTER_TYPED_DUP, BUSTER_TYPED_CLASS_MASK, 0));
        tier0_test_mask_push(program, tasks, tier0_test_mask_expression(BUSTER_TIER0_TEST_MASK_TASK_MASK, bytes, budget - 1));
    }
    else if (choice == 8)
    {
        // ?: of masks on an integer's truth: the condition, its branch to the second arm, the first arm and its jump to
        // the end with its value, the second arm, the end.
        const uint32_t patch = tasks->patch_count;
        tasks->patch_count += 2;
        program->overflow |= tasks->patch_count > BUSTER_TIER0_TEST_MASK_TASKS;
        tier0_test_mask_push(program, tasks, (Tier0TestMaskTask){ .task = BUSTER_TIER0_TEST_MASK_TASK_LABEL, .value = patch + 1 });
        tier0_test_mask_push(program, tasks, tier0_test_mask_expression(BUSTER_TIER0_TEST_MASK_TASK_MASK, bytes, half));
        tier0_test_mask_push(program, tasks, (Tier0TestMaskTask){ .task = BUSTER_TIER0_TEST_MASK_TASK_LABEL, .value = patch });
        tier0_test_mask_push(program, tasks, (Tier0TestMaskTask){ .task = BUSTER_TIER0_TEST_MASK_TASK_JUMP, .class = BUSTER_TYPED_CLASS_MASK, .value = patch + 1 });
        tier0_test_mask_push(program, tasks, tier0_test_mask_expression(BUSTER_TIER0_TEST_MASK_TASK_MASK, bytes, half));
        tier0_test_mask_push(program, tasks, (Tier0TestMaskTask){ .task = BUSTER_TIER0_TEST_MASK_TASK_BRANCH, .class = BUSTER_TYPED_CLASS_U64, .value = patch });
        tier0_test_mask_push(program, tasks, tier0_test_mask_expression(BUSTER_TIER0_TEST_MASK_TASK_INTEGER, 0, 0));
    }
    else if (choice == 9)
    {
        // A chain whose masks are all live before its first logic: 6 to 12 of them, which past seven are in their slots.
        const uint32_t length = 6 + (uint32_t)(tier0_test_mask_random(state) % 7);
        static const uint8_t logic[3] = { BUSTER_TYPED_AND, BUSTER_TYPED_OR, BUSTER_TYPED_XOR };
        for (uint32_t k = 1; k < length; k += 1)
        {
            tier0_test_mask_push(program, tasks, tier0_test_mask_entry(logic[tier0_test_mask_random(state) % 3], BUSTER_TYPED_CLASS_MASK, bytes));
        }
        // Its last operand (the first pushed, run last) a whole expression, under every other one: calls and rows past k7.
        for (uint32_t k = 0; k < length; k += 1)
        {
            tier0_test_mask_push(program, tasks, tier0_test_mask_expression(BUSTER_TIER0_TEST_MASK_TASK_MASK, bytes, k == 0 ? budget - 1 : 0));
        }
    }
    else if (choice == 10)
    {
        // A mask stored and kept: its store's address first, into one of the last four output slots.
        tier0_test_mask_push(program, tasks, tier0_test_mask_entry(BUSTER_TYPED_STORE, BUSTER_TYPED_CLASS_MASK, bytes));
        tier0_test_mask_push(program, tasks, tier0_test_mask_expression(BUSTER_TIER0_TEST_MASK_TASK_MASK, bytes, budget - 1));
        const uint32_t slot = BUSTER_TIER0_TEST_MASK_SLOTS - 4 + (uint32_t)(tier0_test_mask_random(state) % 4);
        tier0_test_mask_push(program, tasks, tier0_test_mask_entry(BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, 1));
        tier0_test_mask_push(program, tasks, tier0_test_mask_entry(BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 8u * slot));
        tier0_test_mask_push(program, tasks, tier0_test_mask_entry(BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, BUSTER_TIER0_TEST_MASK_OUT));
    }
    else if (choice == 11)
    {
        // An integer expression's mask: a call's result among its choices, while the masks under it are live.
        tier0_test_mask_push(program, tasks, tier0_test_mask_entry(BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_MASK, bytes));
        tier0_test_mask_push(program, tasks, tier0_test_mask_expression(BUSTER_TIER0_TEST_MASK_TASK_INTEGER, 0, budget - 1));
    }
    else if (choice == 12 && program->objects)
    {
        // A mask through its object: stored there (and kept), then the object's.
        tier0_test_mask_push(program, tasks, tier0_test_mask_entry(BUSTER_TYPED_XOR, BUSTER_TYPED_CLASS_MASK, bytes));
        tier0_test_mask_push(program, tasks, tier0_test_mask_entry(BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_MASK, 2 + width));
        tier0_test_mask_push(program, tasks, tier0_test_mask_entry(BUSTER_TYPED_STORE_LOCAL, BUSTER_TYPED_CLASS_MASK, 2 + width));
        tier0_test_mask_push(program, tasks, tier0_test_mask_expression(BUSTER_TIER0_TEST_MASK_TASK_MASK, bytes, budget - 1));
    }
    else if (choice >= 12 && program->rows)
    {
        // A row of the width: of two masks, of one, or from an integer.
        const uint32_t kind = (uint32_t)(tier0_test_mask_random(state) % 5);
        const uint32_t row = BUSTER_TIER0_TEST_MASK_ROW_KINDS * width + kind;
        tier0_test_mask_push(program, tasks, tier0_test_mask_entry(BUSTER_TYPED_X86, BUSTER_TYPED_CLASS_MASK, row));
        if (kind == BUSTER_TIER0_TEST_MASK_ROW_FROM)
        {
            tier0_test_mask_push(program, tasks, tier0_test_mask_expression(BUSTER_TIER0_TEST_MASK_TASK_INTEGER, 0, budget - 1));
        }
        else
        {
            const bool two = kind == BUSTER_TIER0_TEST_MASK_ROW_ANDN || kind == BUSTER_TIER0_TEST_MASK_ROW_ADD;
            if (two)
            {
                tier0_test_mask_push(program, tasks, tier0_test_mask_expression(BUSTER_TIER0_TEST_MASK_TASK_MASK, bytes, half));
            }
            tier0_test_mask_push(program, tasks, tier0_test_mask_expression(BUSTER_TIER0_TEST_MASK_TASK_MASK, bytes, two ? budget - 1 - half : budget - 1));
        }
    }
    else
    {
        tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, (uint32_t)(tier0_test_mask_random(state) % BUSTER_TIER0_TEST_MASK_SLOTS));
        tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_MASK, bytes);
    }
}

// An integer expression's task (U64): a constant, a load of any class, a call of the callee, a mask converted to an
// integer of any class, or a row's: TO's of a mask, NUMBERED's of an integer.
static void tier0_test_mask_choose_integer(Tier0TestMaskProgram* program, Tier0TestMaskTasks* tasks, uint32_t budget, uint64_t* state)
{
    const uint32_t choice = budget == 0 ? (uint32_t)(tier0_test_mask_random(state) % 2) : (uint32_t)(tier0_test_mask_random(state) % 6);
    const uint32_t bytes = 1u << (tier0_test_mask_random(state) % 4);
    const uint32_t class = BUSTER_TYPED_CLASS_I8 + (uint32_t)(tier0_test_mask_random(state) % 8);
    if (choice == 0)
    {
        tier0_test_mask_emit(program, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, tier0_test_mask_random(state));
    }
    else if (choice == 1)
    {
        tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, (uint32_t)(tier0_test_mask_random(state) % BUSTER_TIER0_TEST_MASK_SLOTS));
        tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, class, 0);
        if (class != BUSTER_TYPED_CLASS_U64)
        {
            tier0_test_mask_emit(program, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
        }
    }
    else if (choice == 2)
    {
        tier0_test_mask_push(program, tasks, tier0_test_mask_entry(BUSTER_TYPED_CALL, BUSTER_TYPED_CLASS_U64, 0));
        tier0_test_mask_push(program, tasks, tier0_test_mask_expression(BUSTER_TIER0_TEST_MASK_TASK_INTEGER, 0, budget - 1));
    }
    else if (choice == 3 || (choice >= 4 && !program->rows))
    {
        if (class != BUSTER_TYPED_CLASS_U64)
        {
            tier0_test_mask_push(program, tasks, tier0_test_mask_entry(BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0));
        }
        tier0_test_mask_push(program, tasks, tier0_test_mask_entry(BUSTER_TYPED_CONVERT, class, bytes));
        tier0_test_mask_push(program, tasks, tier0_test_mask_expression(BUSTER_TIER0_TEST_MASK_TASK_MASK, bytes, budget - 1));
    }
    else if (choice == 4)
    {
        tier0_test_mask_push(program, tasks, tier0_test_mask_entry(BUSTER_TYPED_X86, BUSTER_TYPED_CLASS_U64,
                                                                   BUSTER_TIER0_TEST_MASK_ROW_KINDS * tier0_test_mask_width(bytes) + BUSTER_TIER0_TEST_MASK_ROW_TO));
        tier0_test_mask_push(program, tasks, tier0_test_mask_expression(BUSTER_TIER0_TEST_MASK_TASK_MASK, bytes, budget - 1));
    }
    else
    {
        tier0_test_mask_push(program, tasks, tier0_test_mask_entry(BUSTER_TYPED_X86, BUSTER_TYPED_CLASS_U64, BUSTER_TIER0_TEST_MASK_ROW_NUMBERED));
        tier0_test_mask_push(program, tasks, tier0_test_mask_expression(BUSTER_TIER0_TEST_MASK_TASK_INTEGER, 0, budget - 1));
    }
}

static void tier0_test_mask_run_tasks(Tier0TestMaskProgram* program, Tier0TestMaskTasks* tasks, uint64_t* state)
{
    while (tasks->count != 0 && !program->overflow)
    {
        tasks->count -= 1;
        const Tier0TestMaskTask task = tasks->tasks[tasks->count];
        if (task.task == BUSTER_TIER0_TEST_MASK_TASK_MASK)
        {
            tier0_test_mask_choose_mask(program, tasks, task.class, task.budget, state);
        }
        else if (task.task == BUSTER_TIER0_TEST_MASK_TASK_INTEGER)
        {
            tier0_test_mask_choose_integer(program, tasks, task.budget, state);
        }
        else if (task.task == BUSTER_TIER0_TEST_MASK_TASK_ENTRY)
        {
            tier0_test_mask_emit(program, task.kind, task.class, task.value);
        }
        else if (task.task == BUSTER_TIER0_TEST_MASK_TASK_BRANCH || task.task == BUSTER_TIER0_TEST_MASK_TASK_JUMP)
        {
            tasks->patches[task.value] = program->count;
            tier0_test_mask_emit(program, task.task == BUSTER_TIER0_TEST_MASK_TASK_BRANCH ? BUSTER_TYPED_BRANCH_FALSE : BUSTER_TYPED_JUMP_VALUE, task.class, 0);
        }
        else
        {
            program->values[tasks->patches[task.value]] = program->count;
        }
    }
}

// A program of BUSTER_TIER0_TEST_MASK_STATEMENTS statements, each a mask's store into its output slot, an integer's
// (from masks) or a mask dropped, with rows and objects as the program says.
static void tier0_test_mask_program(Tier0TestMaskProgram* program, Tier0TestMaskTasks* tasks, uint64_t* state)
{
    program->count = 0;
    program->overflow = false;
    // Each frame object's first contents, a constant converted: a front end writes an object before it reads it.
    for (uint32_t w = 0; program->objects && w < BUSTER_TIER0_TEST_MASK_OBJECTS; w += 1)
    {
        tier0_test_mask_emit(program, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, tier0_test_mask_random(state));
        tier0_test_mask_emit(program, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_MASK, 1u << w);
        tier0_test_mask_emit(program, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_MASK, 2 + w);
    }
    for (uint32_t s = 0; s < BUSTER_TIER0_TEST_MASK_STATEMENTS && !program->overflow; s += 1)
    {
        const uint32_t bytes = 1u << (tier0_test_mask_random(state) % 4);
        const uint32_t budget = 2 + (uint32_t)(tier0_test_mask_random(state) % 10);
        const uint32_t statement = (uint32_t)(tier0_test_mask_random(state) % 4);
        tasks->count = 0;
        tasks->patch_count = 0;
        if (statement == 3)
        {
            tier0_test_mask_push(program, tasks, tier0_test_mask_entry(BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_MASK, 0));
            tier0_test_mask_push(program, tasks, tier0_test_mask_expression(BUSTER_TIER0_TEST_MASK_TASK_MASK, bytes, budget));
        }
        else
        {
            tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_OUT, s);
            if (statement == 2)
            {
                tier0_test_mask_push(program, tasks, tier0_test_mask_entry(BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_U64, 0));
                tier0_test_mask_push(program, tasks, tier0_test_mask_expression(BUSTER_TIER0_TEST_MASK_TASK_INTEGER, 0, budget));
            }
            else
            {
                tier0_test_mask_push(program, tasks, tier0_test_mask_entry(BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_MASK, bytes));
                tier0_test_mask_push(program, tasks, tier0_test_mask_expression(BUSTER_TIER0_TEST_MASK_TASK_MASK, bytes, budget));
            }
        }
        tier0_test_mask_run_tasks(program, tasks, state);
    }
    tier0_test_mask_emit(program, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
}

// ---------------------------------------------------------------------------------------------------------------------
// The functions

static const uint8_t tier0_test_mask_local_classes[2 + BUSTER_TIER0_TEST_MASK_OBJECTS] = { BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64 };
static const uint8_t tier0_test_mask_local_flags[2 + BUSTER_TIER0_TEST_MASK_OBJECTS] = {
    0, 0, BUSTER_TYPED_LOCAL_ADDRESS_TAKEN, BUSTER_TYPED_LOCAL_ADDRESS_TAKEN, BUSTER_TYPED_LOCAL_ADDRESS_TAKEN, BUSTER_TYPED_LOCAL_ADDRESS_TAKEN,
};
static const uint32_t tier0_test_mask_local_sizes[2 + BUSTER_TIER0_TEST_MASK_OBJECTS] = { 0, 0, 1, 2, 4, 8 };
static const uint8_t tier0_test_mask_local_alignments[2 + BUSTER_TIER0_TEST_MASK_OBJECTS] = { 0, 0, 0, 1, 2, 3 };
static const AbiLocation tier0_test_mask_parameters[2] = {
    { .kind = BUSTER_ABI_LOCATION_REGISTERS, .registers = { BUSTER_ABI_RDI, BUSTER_ABI_REGISTER_NONE } },
    { .kind = BUSTER_ABI_LOCATION_REGISTERS, .registers = { BUSTER_ABI_RSI, BUSTER_ABI_REGISTER_NONE } },
};
// The one call's row: leaf(U64) returning U64, in rdi and rax.
static const SymbolId tier0_test_mask_call_targets[1] = { BUSTER_TIER0_TEST_MASK_LEAF };
static const uint32_t tier0_test_mask_call_first[1] = { 0 };
static const uint32_t tier0_test_mask_call_counts[1] = { 1 };
static const uint8_t tier0_test_mask_call_variadic[1] = { 0 };
static const AbiCall tier0_test_mask_calls[1] = {
    { .integer_registers = 1, .result = { .kind = BUSTER_ABI_RESULT_REGISTERS, .registers = { BUSTER_ABI_RAX, BUSTER_ABI_REGISTER_NONE } } },
};
static const AbiLocation tier0_test_mask_argument_locations[1] = {
    { .kind = BUSTER_ABI_LOCATION_REGISTERS, .registers = { BUSTER_ABI_RDI, BUSTER_ABI_REGISTER_NONE } },
};
static const uint8_t tier0_test_mask_argument_classes[1] = { BUSTER_TYPED_CLASS_U64 };

// A program's entry, (const uint64_t* in, uint8_t* out) returning nothing, with the frame objects where it has them.
static Tier0Function tier0_test_mask_function(const Tier0TestMaskProgram* program)
{
    Tier0Function function;
    memset(&function, 0, sizeof(function));
    function.kinds = program->kinds;
    function.classes = program->classes;
    function.values = program->values;
    function.count = program->count;
    function.local_classes = tier0_test_mask_local_classes;
    function.local_flags = tier0_test_mask_local_flags;
    function.local_count = program->objects ? 2 + BUSTER_TIER0_TEST_MASK_OBJECTS : 2;
    function.local_sizes = program->objects ? tier0_test_mask_local_sizes : 0;
    function.local_alignments = program->objects ? tier0_test_mask_local_alignments : 0;
    function.parameter_count = 2;
    function.parameters = tier0_test_mask_parameters;
    function.call_targets = tier0_test_mask_call_targets;
    function.call_first = tier0_test_mask_call_first;
    function.call_argument_counts = tier0_test_mask_call_counts;
    function.call_variadic = tier0_test_mask_call_variadic;
    function.calls = tier0_test_mask_calls;
    function.call_count = 1;
    function.argument_locations = tier0_test_mask_argument_locations;
    function.argument_classes = tier0_test_mask_argument_classes;
    function.argument_count = 1;
    function.operations = tier0_test_mask_rows;
    function.operation_count = BUSTER_TIER0_TEST_MASK_ROWS;
    function.vectors = true;
    function.masks = true;
    return function;
}

// The callee: ~x + 7, by a mask of x copied into all seven registers (so that a caller's masks there are lost unless
// saved), six of them dropped and the last complemented.
static const uint8_t tier0_test_mask_leaf_kinds[] = {
    BUSTER_TYPED_LOCAL, BUSTER_TYPED_CONVERT, BUSTER_TYPED_DUP, BUSTER_TYPED_DUP, BUSTER_TYPED_DUP, BUSTER_TYPED_DUP, BUSTER_TYPED_DUP, BUSTER_TYPED_DUP,
    BUSTER_TYPED_DISCARD, BUSTER_TYPED_DISCARD, BUSTER_TYPED_DISCARD, BUSTER_TYPED_DISCARD, BUSTER_TYPED_DISCARD, BUSTER_TYPED_DISCARD, BUSTER_TYPED_NOT,
    BUSTER_TYPED_CONVERT, BUSTER_TYPED_CONST, BUSTER_TYPED_ADD, BUSTER_TYPED_RETURN,
};
static const uint8_t tier0_test_mask_leaf_classes[] = {
    BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_MASK, BUSTER_TYPED_CLASS_MASK, BUSTER_TYPED_CLASS_MASK, BUSTER_TYPED_CLASS_MASK, BUSTER_TYPED_CLASS_MASK,
    BUSTER_TYPED_CLASS_MASK, BUSTER_TYPED_CLASS_MASK, BUSTER_TYPED_CLASS_MASK, BUSTER_TYPED_CLASS_MASK, BUSTER_TYPED_CLASS_MASK, BUSTER_TYPED_CLASS_MASK,
    BUSTER_TYPED_CLASS_MASK, BUSTER_TYPED_CLASS_MASK, BUSTER_TYPED_CLASS_MASK, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64,
    BUSTER_TYPED_CLASS_U64,
};
static const uint64_t tier0_test_mask_leaf_values[] = { 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 8, 7, 0, 0 };

static Tier0Function tier0_test_mask_leaf(void)
{
    Tier0Function function;
    memset(&function, 0, sizeof(function));
    function.kinds = tier0_test_mask_leaf_kinds;
    function.classes = tier0_test_mask_leaf_classes;
    function.values = tier0_test_mask_leaf_values;
    function.count = BUSTER_ARRAY_LENGTH(tier0_test_mask_leaf_kinds);
    function.local_classes = tier0_test_mask_local_classes;
    function.local_flags = tier0_test_mask_local_flags;
    function.local_count = 1;
    function.parameter_count = 1;
    function.parameters = tier0_test_mask_parameters;
    function.result = (AbiReturn){ .kind = BUSTER_ABI_RESULT_REGISTERS, .registers = { BUSTER_ABI_RAX, BUSTER_ABI_REGISTER_NONE } };
    function.vectors = true;
    function.masks = true;
    return function;
}

// ---------------------------------------------------------------------------------------------------------------------
// The model

// What the programs reach in the model, which the default count must cover whole: a bit per mask kind of each width
// (a conversion from a mask by its kind's bit of CONVERT in `from`), each row, the ?:'s arms of a mask, the deepest the
// mask stack went, and calls and the numbered row made with masks live (and with masks past k7).
typedef struct Tier0TestMaskReach Tier0TestMaskReach;
struct Tier0TestMaskReach
{
    uint64_t kinds[4];
    uint8_t from;
    uint32_t rows;
    uint8_t arms;
    uint32_t deepest;
    bool call_live;
    bool call_deep;
    bool numbered_live;
    bool row_deep;
};

typedef struct Tier0TestMaskValue Tier0TestMaskValue;
struct Tier0TestMaskValue
{
    uint64_t bits;
    uint8_t class;
    uint8_t bytes;
};

// An integer of `class` as a load or a conversion leaves it, sign- or zero-extended to 64 bits.
static uint64_t tier0_test_mask_extend(uint32_t class, uint64_t bits)
{
    const uint32_t width = 8 * tier0_class_bytes(class);
    const uint64_t low = width >= 64 ? bits : bits & ((UINT64_C(1) << width) - 1);
    const bool negative = width < 64 && tier0_class_signed(class) && ((low >> (width - 1)) & 1) != 0;
    return negative ? low | ~((UINT64_C(1) << width) - 1) : low;
}

// The program's stream run as a stack machine on `in` (16 slots of 8 bytes) into `out` (the outputs' bytes), with the
// frame objects of its own, as Tier 0's code must: the kinds and the rows the generator makes, masks as their bits with
// zeros past their bytes. Records what it reached into *reach. Returns false for an entry it doesn't model.
static bool tier0_test_mask_model(const Tier0TestMaskProgram* program, const uint64_t* in, uint8_t* out, Tier0TestMaskReach* reach)
{
    Tier0TestMaskValue stack[BUSTER_TIER0_TEST_MASK_VALUES];
    uint64_t objects[BUSTER_TIER0_TEST_MASK_OBJECTS] = { 0 };
    uint32_t top = 0;
    uint32_t masks = 0;
    uint32_t i = 0;
    bool valid = true;
    while (valid && i < program->count && program->kinds[i] != BUSTER_TYPED_RETURN)
    {
        const uint32_t kind = program->kinds[i];
        const uint32_t class = program->classes[i];
        const uint64_t value = program->values[i];
        const uint32_t width = tier0_test_mask_width((uint32_t)value);
        uint32_t next = i + 1;
        valid = top + 1 < BUSTER_TIER0_TEST_MASK_VALUES;
        if (class == BUSTER_TYPED_CLASS_MASK && kind != BUSTER_TYPED_X86 && kind != BUSTER_TYPED_DUP && kind != BUSTER_TYPED_DISCARD && kind != BUSTER_TYPED_JUMP_VALUE &&
            kind != BUSTER_TYPED_LOCAL && kind != BUSTER_TYPED_STORE_LOCAL && kind != BUSTER_TYPED_SET_LOCAL)
        {
            reach->kinds[width] |= UINT64_C(1) << kind;
        }
        if (kind == BUSTER_TYPED_LOCAL && class == BUSTER_TYPED_CLASS_MASK)
        {
            const uint32_t object = (uint32_t)value - 2;
            stack[top] = (Tier0TestMaskValue){ objects[object], BUSTER_TYPED_CLASS_MASK, (uint8_t)(1u << object) };
            reach->kinds[object] |= UINT64_C(1) << kind;
            top += 1;
        }
        else if (kind == BUSTER_TYPED_STORE_LOCAL || kind == BUSTER_TYPED_SET_LOCAL)
        {
            const uint32_t object = (uint32_t)value - 2;
            objects[object] = stack[top - 1].bits;
            reach->kinds[object] |= UINT64_C(1) << kind;
            top -= kind == BUSTER_TYPED_SET_LOCAL ? 1 : 0;
        }
        else if (kind == BUSTER_TYPED_LOCAL)
        {
            stack[top] = (Tier0TestMaskValue){ value == BUSTER_TIER0_TEST_MASK_IN ? 0 : 1, BUSTER_TYPED_CLASS_U64, 0 };
            top += 1;
        }
        else if (kind == BUSTER_TYPED_CONST)
        {
            stack[top] = (Tier0TestMaskValue){ value, BUSTER_TYPED_CLASS_U64, 0 };
            top += 1;
        }
        else if (kind == BUSTER_TYPED_PTR_ADD)
        {
            // An address is its pointer (0 for the inputs, 1 for the outputs) and a byte offset, kept apart.
            stack[top - 2].bits |= stack[top - 1].bits << 8;
            top -= 1;
        }
        else if (kind == BUSTER_TYPED_LOAD)
        {
            const uint32_t slot = (uint32_t)(stack[top - 1].bits >> 8) / 8;
            const uint32_t bytes = class == BUSTER_TYPED_CLASS_MASK ? (uint32_t)value : tier0_class_bytes(class);
            const uint64_t word = in[slot];
            stack[top - 1] = class == BUSTER_TYPED_CLASS_MASK ? (Tier0TestMaskValue){ word & tier0_test_mask_bits(bytes), BUSTER_TYPED_CLASS_MASK, (uint8_t)bytes }
                                                                : (Tier0TestMaskValue){ tier0_test_mask_extend(class, word), (uint8_t)class, 0 };
        }
        else if (kind == BUSTER_TYPED_CONVERT && class == BUSTER_TYPED_CLASS_MASK)
        {
            stack[top - 1] = (Tier0TestMaskValue){ stack[top - 1].bits & tier0_test_mask_bits((uint32_t)value), BUSTER_TYPED_CLASS_MASK, (uint8_t)value };
        }
        else if (kind == BUSTER_TYPED_CONVERT && stack[top - 1].class == BUSTER_TYPED_CLASS_MASK)
        {
            reach->from |= (uint8_t)(1u << width);
            stack[top - 1] = (Tier0TestMaskValue){ tier0_test_mask_extend(class, stack[top - 1].bits), (uint8_t)class, 0 };
        }
        else if (kind == BUSTER_TYPED_CONVERT)
        {
            stack[top - 1] = (Tier0TestMaskValue){ tier0_test_mask_extend(stack[top - 1].class, stack[top - 1].bits), (uint8_t)class, 0 };
        }
        else if (kind == BUSTER_TYPED_NOT)
        {
            stack[top - 1].bits = ~stack[top - 1].bits & tier0_test_mask_bits((uint32_t)value);
        }
        else if (kind == BUSTER_TYPED_AND || kind == BUSTER_TYPED_OR || kind == BUSTER_TYPED_XOR)
        {
            const uint64_t a = stack[top - 2].bits;
            const uint64_t b = stack[top - 1].bits;
            stack[top - 2].bits = kind == BUSTER_TYPED_AND ? a & b : kind == BUSTER_TYPED_OR ? a | b : a ^ b;
            top -= 1;
        }
        else if (kind == BUSTER_TYPED_DUP)
        {
            stack[top] = stack[top - 1];
            top += 1;
        }
        else if (kind == BUSTER_TYPED_STORE || kind == BUSTER_TYPED_SET)
        {
            const uint32_t offset = (uint32_t)(stack[top - 2].bits >> 8);
            const uint32_t bytes = class == BUSTER_TYPED_CLASS_MASK ? (uint32_t)value : 8;
            memcpy(out + offset, &stack[top - 1].bits, bytes);
            stack[top - 2] = stack[top - 1];
            top -= kind == BUSTER_TYPED_STORE ? 1 : 2;
        }
        else if (kind == BUSTER_TYPED_DISCARD)
        {
            top -= 1;
        }
        else if (kind == BUSTER_TYPED_BRANCH_FALSE)
        {
            top -= 1;
            next = stack[top].bits == 0 ? (uint32_t)value : next;
            reach->arms |= stack[top].bits == 0 ? 2 : 1;
        }
        else if (kind == BUSTER_TYPED_JUMP_VALUE)
        {
            next = (uint32_t)value;
        }
        else if (kind == BUSTER_TYPED_CALL)
        {
            reach->call_live |= masks != 0;
            reach->call_deep |= masks > BUSTER_TIER0_MASK_DEPTHS;
            stack[top - 1].bits = ~stack[top - 1].bits + 7;
        }
        else if (kind == BUSTER_TYPED_X86)
        {
            const Tier0Operation* row = &tier0_test_mask_rows[value];
            const uint32_t operands = row->operand_count;
            const uint64_t a = stack[top - operands].bits;
            const uint64_t b = operands == 2 ? stack[top - 1].bits : 0;
            const uint32_t below = masks - (row->operand_classes[0] == BUSTER_TYPED_CLASS_MASK ? operands : 0);
            reach->rows |= 1u << value;
            reach->numbered_live |= value == BUSTER_TIER0_TEST_MASK_ROW_NUMBERED && masks != 0;
            reach->row_deep |= value != BUSTER_TIER0_TEST_MASK_ROW_NUMBERED && below + operands > BUSTER_TIER0_MASK_DEPTHS;
            top -= operands;
            stack[top] = (Tier0TestMaskValue){ tier0_test_mask_row_model((uint32_t)value, a, b), row->result_class, row->result_bytes };
            top += 1;
        }
        else
        {
            valid = false;
        }
        masks = 0;
        for (uint32_t d = 0; d < top; d += 1)
        {
            masks += stack[d].class == BUSTER_TYPED_CLASS_MASK ? 1 : 0;
        }
        reach->deepest = masks > reach->deepest ? masks : reach->deepest;
        i = next;
    }
    return valid && top == 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// The native run

#define BUSTER_TIER0_TEST_MASK_CODE (16 * BUSTER_TIER0_TEST_PAGE)

typedef struct Tier0TestMaskShared Tier0TestMaskShared;
struct Tier0TestMaskShared
{
    uint64_t in[BUSTER_TIER0_TEST_MASK_SLOTS * 2];
    uint8_t out[BUSTER_TIER0_TEST_MASK_SLOTS * 8 * 2];
};

// Whether the host runs the masks' instructions: AVX-512 F (kmovw, kandw), BW (kmovd, kmovq) and DQ (kmovb), and VL for
// the fixtures' moves.
static bool tier0_test_mask_host(void)
{
    const uint64_t needed = BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512BW) | BUSTER_X86_FEATURE(AVX512DQ) | BUSTER_X86_FEATURE(AVX512VL);
    return (x86_target_native().features & needed) == needed;
}

// Generated entry points have no Clang function-type prefix.
static __attribute__((no_sanitize("function"))) void tier0_test_mask_call(const void* code, uint64_t* in, uint8_t* out)
{
    typedef void (*Entry)(const uint64_t*, uint8_t*);
    Entry entry = 0;
    memcpy(&entry, &code, sizeof(entry));
    entry(in, out);
}

// `code` (`length` bytes) run in a child, which must exit with status 0, its entry (at offset 0) called on `shared`'s
// inputs and outputs, whose outputs it writes back through a shared mapping.
static bool tier0_test_mask_native(const uint8_t* code, uint32_t length, Tier0TestMaskShared* shared)
{
    Tier0TestMaskShared* mapped = mmap(0, sizeof(*mapped), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    bool ran = mapped != MAP_FAILED;
    if (ran)
    {
        memcpy(mapped, shared, sizeof(*mapped));
        const pid_t child = fork();
        if (child == 0)
        {
            alarm(BUSTER_TIER0_TEST_RUN_SECONDS);
            uint8_t* page = mmap(0, BUSTER_TIER0_TEST_MASK_CODE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (page == MAP_FAILED)
            {
                _exit(3);
            }
            memcpy(page, code, length);
            if (mprotect(page, BUSTER_TIER0_TEST_MASK_CODE, PROT_READ | PROT_EXEC) != 0)
            {
                _exit(4);
            }
            tier0_test_mask_call(page, mapped->in, mapped->out);
            _exit(0);
        }
        int status = 0;
        ran = child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
        memcpy(shared->out, mapped->out, sizeof(shared->out));
        munmap(mapped, sizeof(*mapped));
    }
    return ran;
}

// Each of `count` functions (symbol f + 1) compiled by both paths into the same code, checked as the other checks check
// theirs, sized and encoded by both encoders into the same bytes, one after another at `image` (16-byte aligned), and
// their calls' fields patched to their targets. Returns the image's length, 0 if a step failed.
static uint32_t tier0_test_mask_build(Tier0TestTally* tally, const char* name, const Tier0Function* functions, uint32_t count, Arena* arena, uint8_t* image)
{
    uint32_t size = 0;
    uint32_t starts[2] = { 0, 0 };
    uint32_t fields[64];
    uint32_t ends[64];
    uint32_t targets[64];
    uint32_t fixups = 0;
    bool ready = true;
    for (uint32_t f = 0; ready && f < count; f += 1)
    {
        const uint64_t mark = arena->length;
        const Tier0Check check = tier0_validate(&functions[f]);
        const Tier0Code scalar = tier0_compile_scalar(&functions[f], arena);
        const Tier0Code vector = tier0_compile(&functions[f], arena);
        ready = tier0_test_expect(tally, check.error == TIER0_OK, "masks", "%s: function %u refused: rule \"%s\" at %u", name, f, check.rule != 0 ? check.rule : "",
                                  check.node);
        ready = ready && tier0_test_expect(tally, scalar.error == TIER0_OK && tier0_test_same_code(&scalar, &vector), "masks",
                                           "%s: function %u: the paths differ or fail (%d at %u, %d)", name, f, scalar.error, scalar.node, vector.error);
        ready = ready && tier0_test_expect(tally, x86_validate(scalar.records, scalar.count) == scalar.count &&
                                                      branch_sizing_validate(scalar.records, scalar.count, scalar.branches, scalar.targets, scalar.branch_count).error ==
                                                          BRANCH_SIZING_OK,
                                           "masks", "%s: function %u: an invalid record or branch column", name, f);
        ready = ready && tier0_test_covered(tally, "masks", name, &scalar);
        uint32_t* offsets = ready ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)scalar.count + 1) : 0;
        uint32_t* fixup_ends = ready ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, BUSTER_X86_ENCODER_FIXUP_CAPACITY(scalar.reference_count)) : 0;
        uint8_t* other = ready ? BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, BUSTER_X86_ENCODER_CAPACITY(scalar.count)) : 0;
        const BranchSizing sizing = offsets != 0 ? size_branches(scalar.records, scalar.count, scalar.branches, scalar.targets, scalar.branch_count, offsets) : (BranchSizing){ 0 };
        const uint32_t start = (size + 15) & ~15u;
        const uint64_t room = BUSTER_X86_ENCODER_CAPACITY(scalar.count);
        ready = ready && tier0_test_expect(tally, offsets != 0 && fixup_ends != 0 && other != 0 && sizing.error == BRANCH_SIZING_OK &&
                                                      start + room <= BUSTER_TIER0_TEST_MASK_CODE && fixups + scalar.reference_count <= 64,
                                           "masks", "%s: function %u: no room, or the branches can't be sized", name, f);
        const int64_t length = ready ? x86_encode(scalar.records, scalar.count, image + start, room, 0, fixup_ends) : -1;
        const int64_t scalar_length = ready ? x86_encode_scalar(scalar.records, scalar.count, other, room, 0, 0) : -1;
        ready = ready && tier0_test_expect(tally, length > 0 && length == scalar_length && memcmp(image + start, other, (size_t)length) == 0, "masks",
                                           "%s: function %u: the encoders differ, or fail (%" PRId64 ", %" PRId64 ")", name, f, length, scalar_length);
        for (uint32_t r = 0; ready && r < scalar.reference_count; r += 1)
        {
            const LinkReference* reference = &scalar.references[r];
            fields[fixups] = start + fixup_ends[r] - x86_fixup_distance(scalar.records[reference->instruction].form, reference->field);
            ends[fixups] = start + fixup_ends[r];
            targets[fixups] = reference->target;
            fixups += 1;
        }
        starts[f] = start;
        size = ready ? start + (uint32_t)length : size;
        arena_pop_to(arena, mark);
    }
    for (uint32_t k = 0; ready && k < fixups; k += 1)
    {
        ready = tier0_test_expect(tally, targets[k] >= 1 && targets[k] <= count, "masks", "%s: a reference to symbol %u, which isn't one of its functions", name, targets[k]);
        const int32_t distance = ready ? (int32_t)starts[targets[k] - 1] - (int32_t)ends[k] : 0;
        memcpy(image + fields[k], &distance, sizeof(distance));
    }
    return ready ? size : 0;
}

// The typed interpreter's run of the program (the entry and the callee) on `in` into `out`.
static bool tier0_test_mask_interpret(const Tier0Function* functions, uint64_t* in, uint8_t* out, Arena* arena)
{
    static const SymbolId symbols[2] = { BUSTER_TIER0_TEST_MASK_ENTRY, BUSTER_TIER0_TEST_MASK_LEAF };
    const InterpreterProgram program = {
        .functions = functions,
        .function_symbols = symbols,
        .function_count = 2,
    };
    const InterpreterValue arguments[2] = { { (uint64_t)(uintptr_t)in, 0 }, { (uint64_t)(uintptr_t)out, 0 } };
    // The test's own buffers, which no registration covers: unchecked, as a native call's would be.
    InterpreterOptions options = interpreter_default_options();
    options.check_memory = false;
    options.check_undefined = false;
    InterpreterResult result;
    const bool ran = interpreter_typed_run(&program, BUSTER_TIER0_TEST_MASK_ENTRY, arguments, 2, arena, &options, &result);
    return ran && result.status == INTERPRETER_OK;
}

// ---------------------------------------------------------------------------------------------------------------------
// The checks

static void tier0_test_mask_inputs(uint64_t* in, uint64_t* state)
{
    for (uint32_t k = 0; k < BUSTER_TIER0_TEST_MASK_SLOTS; k += 1)
    {
        const uint64_t word = tier0_test_mask_random(state);
        // Now and then a word whose low bytes are all set or all clear, at the mask widths' edges.
        in[k] = (word & 7) == 0 ? 0 : (word & 7) == 1 ? UINT64_MAX : (word & 7) == 2 ? UINT64_C(0x8000000000000080) : word;
    }
}

static void tier0_test_mask_programs(Tier0TestTally* tally, uint64_t seed, uint32_t count, bool host, Arena* arena, uint64_t* entries)
{
    Tier0TestMaskProgram* program = malloc(sizeof(*program));
    Tier0TestMaskTasks* tasks = malloc(sizeof(*tasks));
    uint8_t* image = malloc(BUSTER_TIER0_TEST_MASK_CODE);
    Tier0TestMaskShared shared;
    Tier0TestMaskReach reach;
    memset(&reach, 0, sizeof(reach));
    uint64_t state = seed * 0x9e3779b97f4a7c15ull + 179;
    uint32_t ran = 0;
    uint32_t interpreted = 0;
    for (uint32_t p = 0; program != 0 && tasks != 0 && image != 0 && p < count; p += 1)
    {
        const uint32_t shape = p % 4;
        program->rows = shape == 1 || shape == 3;
        program->objects = shape >= 2;
        tier0_test_mask_program(program, tasks, &state);
        if (!tier0_test_expect(tally, !program->overflow, "masks", "program %u: the generator ran out of room", p))
        {
            continue;
        }
        *entries += program->count;
        char name[64];
        snprintf(name, sizeof(name), "seed %" PRIu64 " program %u", seed, p);
        const Tier0Function functions[2] = { tier0_test_mask_function(program), tier0_test_mask_leaf() };
        const uint32_t length = tier0_test_mask_build(tally, name, functions, 2, arena, image);
        tier0_test_mask_inputs(shared.in, &state);
        uint8_t expected[sizeof(shared.out)];
        memset(expected, 0xa5, sizeof(expected));
        Tier0TestMaskReach program_reach;
        memset(&program_reach, 0, sizeof(program_reach));
        const bool modelled = tier0_test_expect(tally, tier0_test_mask_model(program, shared.in, expected, &program_reach), "masks", "%s: the model can't run it", name);
        bool same = modelled && length != 0;
        if (same && host)
        {
            memset(shared.out, 0xa5, sizeof(shared.out));
            const bool native = tier0_test_expect(tally, tier0_test_mask_native(image, length, &shared), "masks", "%s: the native run failed", name);
            same = native && tier0_test_expect(tally, memcmp(shared.out, expected, sizeof(expected)) == 0, "masks", "%s: the outputs differ from the model's", name);
            ran += same ? 1 : 0;
        }
        if (modelled && !program->rows && !program->objects)
        {
            // The typed interpreter (#176), which models neither rows nor frame objects, must give the model's outputs too.
            uint8_t outputs[sizeof(shared.out)];
            memset(outputs, 0xa5, sizeof(outputs));
            const bool run = tier0_test_expect(tally, tier0_test_mask_interpret(functions, shared.in, outputs, arena), "masks", "%s: the interpreter can't run it", name);
            const bool agree = run && tier0_test_expect(tally, memcmp(outputs, expected, sizeof(expected)) == 0, "masks", "%s: the interpreter's outputs differ from the model's", name);
            same = same && agree;
            interpreted += agree ? 1 : 0;
        }
        if (same)
        {
            for (uint32_t w = 0; w < 4; w += 1)
            {
                reach.kinds[w] |= program_reach.kinds[w];
            }
            reach.from |= program_reach.from;
            reach.rows |= program_reach.rows;
            reach.arms |= program_reach.arms;
            reach.deepest = program_reach.deepest > reach.deepest ? program_reach.deepest : reach.deepest;
            reach.call_live |= program_reach.call_live;
            reach.call_deep |= program_reach.call_deep;
            reach.numbered_live |= program_reach.numbered_live;
            reach.row_deep |= program_reach.row_deep;
        }
    }
    // What the default count must reach: every kind of each width, conversions from each, every row, both arms, masks
    // past k7, calls and the numbered row with masks live, a call with masks past k7, and a row past the registers.
    if (count >= BUSTER_TIER0_TEST_MASK_PROGRAMS)
    {
        const uint64_t wanted = UINT64_C(1) << BUSTER_TYPED_CONVERT | UINT64_C(1) << BUSTER_TYPED_LOAD | UINT64_C(1) << BUSTER_TYPED_STORE |
                                UINT64_C(1) << BUSTER_TYPED_SET | UINT64_C(1) << BUSTER_TYPED_NOT | UINT64_C(1) << BUSTER_TYPED_AND | UINT64_C(1) << BUSTER_TYPED_OR |
                                UINT64_C(1) << BUSTER_TYPED_XOR | UINT64_C(1) << BUSTER_TYPED_LOCAL | UINT64_C(1) << BUSTER_TYPED_STORE_LOCAL;
        for (uint32_t w = 0; w < 4; w += 1)
        {
            tier0_test_expect(tally, (reach.kinds[w] & wanted) == wanted, "masks", "the programs reach only kinds %016" PRIx64 " of masks of %u bytes", reach.kinds[w], 1u << w);
        }
        tier0_test_expect(tally, reach.from == 0xf && reach.rows == (1u << BUSTER_TIER0_TEST_MASK_ROWS) - 1 && reach.arms == 3, "masks",
                          "the programs reach conversions from %x, rows %x and arms %x", reach.from, reach.rows, reach.arms);
        tier0_test_expect(tally, reach.deepest > BUSTER_TIER0_MASK_DEPTHS + 2 && reach.call_live && reach.call_deep && reach.numbered_live && reach.row_deep, "masks",
                          "the programs reach depth %u, calls with masks live %d and past k7 %d, the numbered row with masks live %d, a row past k7 %d",
                          reach.deepest, reach.call_live, reach.call_deep, reach.numbered_live, reach.row_deep);
    }
    printf("    %u programs run natively%s, %u through the interpreter too, the mask stack %u deep at most\n", ran, host ? "" : " (none: the host lacks AVX-512)",
           interpreted, reach.deepest);
    free(image);
    free(tasks);
    free(program);
}

// A zero-masked move under a mask role decoration: the dwords of a 64-byte vector of the inputs where a 16-bit mask's
// bits are set, zeros elsewhere, stored to the outputs; under `live` masks loaded first (seven: the row's mask past the
// registers, which borrows k1), dropped after.
static void tier0_test_mask_decoration(Tier0TestTally* tally, uint32_t live, bool host, Arena* arena)
{
    static Tier0Operation row;
    memset(&row, 0, sizeof(row));
    row.record_count = 1;
    row.records[0] = tier0_test_mask_record(X86_FORM_VMOVDQU32_Z_ZM512, BUSTER_TIER0_ROLE_VECTOR(0), 0, BUSTER_TIER0_ROLE_VECTOR(0));
    row.records[0].decorations = (uint8_t)(0x80 | BUSTER_TIER0_DECORATION_MASK(1));
    row.operand_count = 2;
    row.operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
    row.operand_bytes[0] = 64;
    row.operand_classes[1] = BUSTER_TYPED_CLASS_MASK;
    row.operand_bytes[1] = 2;
    row.result_class = BUSTER_TYPED_CLASS_VECTOR;
    row.result_bytes = 64;
    Tier0TestMaskProgram* program = malloc(sizeof(*program));
    uint8_t* image = malloc(BUSTER_TIER0_TEST_MASK_CODE);
    if (program != 0 && image != 0)
    {
        program->count = 0;
        program->overflow = false;
        for (uint32_t k = 0; k < live; k += 1)
        {
            tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, k);
            tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_MASK, 2);
        }
        tier0_test_mask_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, BUSTER_TIER0_TEST_MASK_OUT);
        tier0_test_mask_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, BUSTER_TIER0_TEST_MASK_IN);
        tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_VECTOR, 64);
        tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, 8);
        tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_MASK, 2);
        tier0_test_mask_emit(program, BUSTER_TYPED_X86, BUSTER_TYPED_CLASS_VECTOR, 0);
        tier0_test_mask_emit(program, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_VECTOR, 64);
        // A copy of the top mask first, so that each DISCARD's operand is of its entry before's class.
        if (live != 0)
        {
            tier0_test_mask_emit(program, BUSTER_TYPED_DUP, BUSTER_TYPED_CLASS_MASK, 0);
        }
        for (uint32_t k = 0; k < live + (live != 0 ? 1u : 0u); k += 1)
        {
            tier0_test_mask_emit(program, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_MASK, 0);
        }
        tier0_test_mask_emit(program, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
        program->objects = false;
        Tier0Function function = tier0_test_mask_function(program);
        function.operations = &row;
        function.operation_count = 1;
        char name[64];
        snprintf(name, sizeof(name), "decoration under %u masks", live);
        const uint32_t length = tier0_test_mask_build(tally, name, &function, 1, arena, image);
        Tier0TestMaskShared shared;
        uint64_t state = 0x179 + live;
        tier0_test_mask_inputs(shared.in, &state);
        shared.in[8] = 0xa53c;
        if (length != 0 && host)
        {
            memset(shared.out, 0xa5, sizeof(shared.out));
            const bool native = tier0_test_expect(tally, tier0_test_mask_native(image, length, &shared), "masks", "%s: the native run failed", name);
            uint32_t dwords[16];
            const uint8_t* bytes = (const uint8_t*)shared.in;
            for (uint32_t j = 0; j < 16; j += 1)
            {
                uint32_t dword = 0;
                memcpy(&dword, bytes + 4 * j, 4);
                dwords[j] = ((shared.in[8] >> j) & 1) != 0 ? dword : 0;
            }
            tier0_test_expect(tally, native && memcmp(shared.out, dwords, sizeof(dwords)) == 0, "masks", "%s: the masked dwords differ from C's", name);
        }
    }
    free(image);
    free(program);
}

// The class's rules: each break refused by tier0_validate (with its rule's words), and a valid neighbour taken. Each case
// is a statement of the program's function (or the callee's) changed in one place.
typedef struct Tier0TestMaskRule Tier0TestMaskRule;
struct Tier0TestMaskRule
{
    const char* name;
    uint8_t kinds[12];
    uint8_t classes[12];
    uint64_t values[12];
    uint32_t count;
    bool valid;
};

static void tier0_test_mask_rules(Tier0TestTally* tally)
{
#define M BUSTER_TYPED_CLASS_MASK
#define U BUSTER_TYPED_CLASS_U64
#define V BUSTER_TYPED_CLASS_VOID
    // Most cases read a mask from the inputs (LOCAL in, LOAD of 2 bytes) first.
    static const Tier0TestMaskRule rules[] = {
        { "a load and a drop", { BUSTER_TYPED_LOCAL, BUSTER_TYPED_LOAD, BUSTER_TYPED_DISCARD, BUSTER_TYPED_RETURN }, { U, M, M, V }, { 0, 2, 0, 0 }, 4, true },
        { "a load of 3 bytes", { BUSTER_TYPED_LOCAL, BUSTER_TYPED_LOAD, BUSTER_TYPED_DISCARD, BUSTER_TYPED_RETURN }, { U, M, M, V }, { 0, 3, 0, 0 }, 4, false },
        { "a load from a mask", { BUSTER_TYPED_LOCAL, BUSTER_TYPED_LOAD, BUSTER_TYPED_LOAD, BUSTER_TYPED_DISCARD, BUSTER_TYPED_RETURN }, { U, M, M, M, V }, { 0, 2, 2, 0, 0 }, 5, false },
        { "a conversion and back", { BUSTER_TYPED_CONST, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CONVERT, BUSTER_TYPED_DISCARD, BUSTER_TYPED_RETURN }, { U, M, U, U, V }, { 5, 4, 4, 0, 0 }, 5, true },
        { "a conversion to a mask of 0 bytes", { BUSTER_TYPED_CONST, BUSTER_TYPED_CONVERT, BUSTER_TYPED_DISCARD, BUSTER_TYPED_RETURN }, { U, M, M, V }, { 5, 0, 0, 0 }, 4, false },
        { "a conversion back of other bytes", { BUSTER_TYPED_CONST, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CONVERT, BUSTER_TYPED_DISCARD, BUSTER_TYPED_RETURN }, { U, M, U, U, V }, { 5, 4, 8, 0, 0 }, 5, false },
        { "a conversion of a mask to a float", { BUSTER_TYPED_CONST, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CONVERT, BUSTER_TYPED_DISCARD, BUSTER_TYPED_RETURN }, { U, M, BUSTER_TYPED_CLASS_F64, BUSTER_TYPED_CLASS_F64, V }, { 5, 4, 4, 0, 0 }, 5, false },
        { "a conversion of a float to a mask", { BUSTER_TYPED_CONST, BUSTER_TYPED_CONVERT, BUSTER_TYPED_DISCARD, BUSTER_TYPED_RETURN }, { BUSTER_TYPED_CLASS_F64, M, M, V }, { 0, 4, 0, 0 }, 4, false },
        { "a mask's constant", { BUSTER_TYPED_CONST, BUSTER_TYPED_DISCARD, BUSTER_TYPED_RETURN }, { M, M, V }, { 1, 0, 0 }, 3, false },
        { "logic of one width", { BUSTER_TYPED_LOCAL, BUSTER_TYPED_LOAD, BUSTER_TYPED_DUP, BUSTER_TYPED_AND, BUSTER_TYPED_NOT, BUSTER_TYPED_DISCARD, BUSTER_TYPED_RETURN }, { U, M, M, M, M, M, V }, { 0, 2, 0, 2, 2, 0, 0 }, 7, true },
        { "logic of another width", { BUSTER_TYPED_LOCAL, BUSTER_TYPED_LOAD, BUSTER_TYPED_DUP, BUSTER_TYPED_AND, BUSTER_TYPED_DISCARD, BUSTER_TYPED_RETURN }, { U, M, M, M, M, V }, { 0, 2, 0, 4, 0, 0 }, 6, false },
        { "NOT of another width", { BUSTER_TYPED_LOCAL, BUSTER_TYPED_LOAD, BUSTER_TYPED_NOT, BUSTER_TYPED_DISCARD, BUSTER_TYPED_RETURN }, { U, M, M, M, V }, { 0, 2, 1, 0, 0 }, 5, false },
        { "ADD of masks", { BUSTER_TYPED_LOCAL, BUSTER_TYPED_LOAD, BUSTER_TYPED_DUP, BUSTER_TYPED_ADD, BUSTER_TYPED_DISCARD, BUSTER_TYPED_RETURN }, { U, M, M, M, M, V }, { 0, 2, 0, 2, 0, 0 }, 6, false },
        { "a compare of masks", { BUSTER_TYPED_LOCAL, BUSTER_TYPED_LOAD, BUSTER_TYPED_DUP, BUSTER_TYPED_EQ, BUSTER_TYPED_DISCARD, BUSTER_TYPED_RETURN }, { U, M, M, BUSTER_TYPED_CLASS_I32, BUSTER_TYPED_CLASS_I32, V }, { 0, 2, 0, 0, 0, 0 }, 6, false },
        { "an integer's DISCARD of a mask", { BUSTER_TYPED_LOCAL, BUSTER_TYPED_LOAD, BUSTER_TYPED_DISCARD, BUSTER_TYPED_RETURN }, { U, M, U, V }, { 0, 2, 0, 0 }, 4, false },
        { "a mask's DISCARD of an integer", { BUSTER_TYPED_LOCAL, BUSTER_TYPED_DISCARD, BUSTER_TYPED_RETURN }, { U, M, V }, { 0, 0, 0 }, 3, false },
        { "a mask's DUP of an integer", { BUSTER_TYPED_LOCAL, BUSTER_TYPED_DUP, BUSTER_TYPED_DISCARD, BUSTER_TYPED_DISCARD, BUSTER_TYPED_RETURN }, { U, M, M, U, V }, { 0, 0, 0, 0, 0 }, 5, false },
        { "a store of the mask's bytes", { BUSTER_TYPED_LOCAL, BUSTER_TYPED_LOCAL, BUSTER_TYPED_LOAD, BUSTER_TYPED_SET, BUSTER_TYPED_RETURN }, { U, U, M, M, V }, { 1, 0, 2, 2, 0 }, 5, true },
        { "a store of other bytes", { BUSTER_TYPED_LOCAL, BUSTER_TYPED_LOCAL, BUSTER_TYPED_LOAD, BUSTER_TYPED_SET, BUSTER_TYPED_RETURN }, { U, U, M, M, V }, { 1, 0, 2, 8, 0 }, 5, false },
        { "an integer's store of a mask", { BUSTER_TYPED_LOCAL, BUSTER_TYPED_LOCAL, BUSTER_TYPED_LOAD, BUSTER_TYPED_SET, BUSTER_TYPED_RETURN }, { U, U, M, BUSTER_TYPED_CLASS_U16, V }, { 1, 0, 2, 0, 0 }, 5, false },
        { "a mask's local that isn't an object", { BUSTER_TYPED_LOCAL, BUSTER_TYPED_DISCARD, BUSTER_TYPED_RETURN }, { M, M, V }, { 0, 0, 0 }, 3, false },
        { "a mask's object of its bytes", { BUSTER_TYPED_LOCAL, BUSTER_TYPED_DISCARD, BUSTER_TYPED_RETURN }, { M, M, V }, { 3, 0, 0 }, 3, true },
        { "a mask's object of other bytes", { BUSTER_TYPED_LOCAL, BUSTER_TYPED_LOAD, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_RETURN }, { U, M, M, V }, { 0, 2, 4, 0 }, 4, false },
        { "a call's mask argument", { BUSTER_TYPED_LOCAL, BUSTER_TYPED_LOAD, BUSTER_TYPED_CALL, BUSTER_TYPED_DISCARD, BUSTER_TYPED_RETURN }, { U, M, U, U, V }, { 0, 2, 0, 0, 0 }, 5, false },
        { "a call's mask result", { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CALL, BUSTER_TYPED_DISCARD, BUSTER_TYPED_RETURN }, { U, M, M, V }, { 0, 0, 0, 0 }, 4, false },
        { "a branch on a mask", { BUSTER_TYPED_LOCAL, BUSTER_TYPED_LOAD, BUSTER_TYPED_BRANCH_FALSE, BUSTER_TYPED_RETURN }, { U, M, M, V }, { 0, 2, 3, 0 }, 4, false },
        { "a return of a mask", { BUSTER_TYPED_LOCAL, BUSTER_TYPED_LOAD, BUSTER_TYPED_RETURN }, { U, M, M }, { 0, 2, 0 }, 3, false },
        { "?: of one width", { BUSTER_TYPED_LOCAL, BUSTER_TYPED_BRANCH_FALSE, BUSTER_TYPED_LOCAL, BUSTER_TYPED_LOAD, BUSTER_TYPED_JUMP_VALUE, BUSTER_TYPED_LOCAL, BUSTER_TYPED_LOAD, BUSTER_TYPED_DISCARD, BUSTER_TYPED_RETURN }, { U, U, U, M, M, U, M, M, V }, { 0, 5, 0, 2, 7, 0, 2, 0, 0 }, 9, true },
        { "?: of two widths", { BUSTER_TYPED_LOCAL, BUSTER_TYPED_BRANCH_FALSE, BUSTER_TYPED_LOCAL, BUSTER_TYPED_LOAD, BUSTER_TYPED_JUMP_VALUE, BUSTER_TYPED_LOCAL, BUSTER_TYPED_LOAD, BUSTER_TYPED_DISCARD, BUSTER_TYPED_RETURN }, { U, U, U, M, M, U, M, M, V }, { 0, 5, 0, 2, 7, 0, 4, 0, 0 }, 9, false },
        { "the numbered row under a mask", { BUSTER_TYPED_LOCAL, BUSTER_TYPED_LOAD, BUSTER_TYPED_LOCAL, BUSTER_TYPED_X86, BUSTER_TYPED_DISCARD, BUSTER_TYPED_DUP, BUSTER_TYPED_DISCARD, BUSTER_TYPED_DISCARD, BUSTER_TYPED_RETURN }, { U, M, U, U, U, M, M, M, V }, { 0, 2, 0, BUSTER_TIER0_TEST_MASK_ROW_NUMBERED, 0, 0, 0, 0, 0 }, 9, true },
        { "a mask's DISCARD after an integer's", { BUSTER_TYPED_LOCAL, BUSTER_TYPED_LOAD, BUSTER_TYPED_LOCAL, BUSTER_TYPED_DISCARD, BUSTER_TYPED_DISCARD, BUSTER_TYPED_RETURN }, { U, M, U, U, M, V }, { 0, 2, 0, 0, 0, 0 }, 6, false },
        { "a row's mask operand of other bytes", { BUSTER_TYPED_LOCAL, BUSTER_TYPED_LOAD, BUSTER_TYPED_X86, BUSTER_TYPED_DISCARD, BUSTER_TYPED_RETURN }, { U, M, M, M, V }, { 0, 4, BUSTER_TIER0_TEST_MASK_ROW_SHIFT + BUSTER_TIER0_TEST_MASK_ROW_KINDS, 0, 0 }, 5, false },
        { "a row's mask operand", { BUSTER_TYPED_LOCAL, BUSTER_TYPED_LOAD, BUSTER_TYPED_X86, BUSTER_TYPED_DISCARD, BUSTER_TYPED_RETURN }, { U, M, M, M, V }, { 0, 2, BUSTER_TIER0_TEST_MASK_ROW_SHIFT + BUSTER_TIER0_TEST_MASK_ROW_KINDS, 0, 0 }, 5, true },
    };
#undef M
#undef U
#undef V
    for (uint32_t r = 0; r < BUSTER_ARRAY_LENGTH(rules); r += 1)
    {
        Tier0TestMaskProgram program;
        memset(&program, 0, sizeof(program));
        memcpy(program.kinds, rules[r].kinds, rules[r].count);
        memcpy(program.classes, rules[r].classes, rules[r].count);
        memcpy(program.values, rules[r].values, rules[r].count * sizeof(uint64_t));
        program.count = rules[r].count;
        program.objects = true;
        const Tier0Function function = tier0_test_mask_function(&program);
        const Tier0Check check = tier0_validate(&function);
        tier0_test_expect(tally, (check.error == TIER0_OK) == rules[r].valid, "mask rules", "%s: %s (rule \"%s\" at %u)", rules[r].name, rules[r].valid ? "refused" : "taken",
                          check.rule != 0 ? check.rule : "", check.node);
        // A function's flags: masks without vectors, and on a vector transport without AVX-512, are refused, and a mask's
        // entry in a function without masks.
        if (rules[r].valid && r == 0)
        {
            Tier0Function changed = function;
            changed.vectors = false;
            tier0_test_expect(tally, tier0_validate(&changed).error != TIER0_OK, "mask rules", "masks without vectors: taken");
            changed = function;
            changed.vector_bytes = 16;
            tier0_test_expect(tally, tier0_validate(&changed).error != TIER0_OK, "mask rules", "masks on SSE2's transport: taken");
            changed = function;
            changed.masks = false;
            tier0_test_expect(tally, tier0_validate(&changed).error != TIER0_OK, "mask rules", "a mask entry without masks: taken");
        }
    }
    // The rows' rules (tier0_operation_valid): each row of the file's taken, and each break of a row refused.
    for (uint32_t k = 0; k < BUSTER_TIER0_TEST_MASK_ROWS; k += 1)
    {
        tier0_test_expect(tally, tier0_operation_valid(&tier0_test_mask_rows[k]), "mask rules", "row %u refused", k);
    }
    Tier0Operation broken = tier0_test_mask_rows[BUSTER_TIER0_TEST_MASK_ROW_ANDN + BUSTER_TIER0_TEST_MASK_ROW_KINDS];
    broken.records[0].vvvv = 3;
    tier0_test_expect(tally, !tier0_operation_valid(&broken), "mask rules", "a row of mask roles that names k3 by number: taken");
    broken = tier0_test_mask_rows[BUSTER_TIER0_TEST_MASK_ROW_ANDN + BUSTER_TIER0_TEST_MASK_ROW_KINDS];
    broken.operand_classes[1] = BUSTER_TYPED_CLASS_U64;
    broken.operand_bytes[1] = 0;
    tier0_test_expect(tally, !tier0_operation_valid(&broken), "mask rules", "MASK(1) of an integer operand: taken");
    broken = tier0_test_mask_rows[BUSTER_TIER0_TEST_MASK_ROW_TO + BUSTER_TIER0_TEST_MASK_ROW_KINDS];
    broken.records[0] = tier0_test_mask_record(X86_FORM_KMOVW_K_K, BUSTER_TIER0_ROLE_MASK_RESULT, 0, BUSTER_TIER0_ROLE_MASK(0));
    tier0_test_expect(tally, !tier0_operation_valid(&broken), "mask rules", "MASK_RESULT without a mask result: taken");
    broken = tier0_test_mask_rows[BUSTER_TIER0_TEST_MASK_ROW_SHIFT + BUSTER_TIER0_TEST_MASK_ROW_KINDS];
    broken.operand_bytes[0] = 3;
    tier0_test_expect(tally, !tier0_operation_valid(&broken), "mask rules", "a mask operand of 3 bytes: taken");
    broken = tier0_test_mask_rows[BUSTER_TIER0_TEST_MASK_ROW_SCRATCH + BUSTER_TIER0_TEST_MASK_ROW_KINDS];
    broken.records[1].rm = BUSTER_TIER0_ROLE_MASK_SCRATCH(3);
    broken.records[0].reg = BUSTER_TIER0_ROLE_MASK_SCRATCH(3);
    broken.operand_count = 7;
    for (uint32_t j = 1; j < 7; j += 1)
    {
        broken.operand_classes[j] = BUSTER_TYPED_CLASS_MASK;
        broken.operand_bytes[j] = 2;
    }
    tier0_test_expect(tally, !tier0_operation_valid(&broken), "mask rules", "seven mask operands and four scratch registers: taken");
    broken.operand_count = 3;
    tier0_test_expect(tally, tier0_operation_valid(&broken), "mask rules", "three mask operands and four scratch registers: refused");
    // A decoration's roles: MASK(j) of a vector operand, an invalid pair of bits, and a scratch one past the four.
    static Tier0Operation decorated;
    memset(&decorated, 0, sizeof(decorated));
    decorated.record_count = 1;
    decorated.records[0] = tier0_test_mask_record(X86_FORM_VMOVDQU32_Z_ZM512, BUSTER_TIER0_ROLE_VECTOR(0), 0, BUSTER_TIER0_ROLE_VECTOR(0));
    decorated.operand_count = 2;
    decorated.operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
    decorated.operand_bytes[0] = 64;
    decorated.operand_classes[1] = BUSTER_TYPED_CLASS_MASK;
    decorated.operand_bytes[1] = 2;
    decorated.result_class = BUSTER_TYPED_CLASS_VECTOR;
    decorated.result_bytes = 64;
    static const uint8_t decorations[5] = { BUSTER_TIER0_DECORATION_MASK(1), BUSTER_TIER0_DECORATION_MASK_SCRATCH(3), BUSTER_TIER0_DECORATION_MASK(0),
                                            BUSTER_TIER0_DECORATION_ROLE, BUSTER_TIER0_DECORATION_MASK_SCRATCH(4) };
    for (uint32_t d = 0; d < 5; d += 1)
    {
        decorated.records[0].decorations = decorations[d];
        tier0_test_expect(tally, tier0_operation_valid(&decorated) == (d < 2), "mask rules", "decoration %02x: %s", decorations[d], d < 2 ? "refused" : "taken");
    }
}

static void tier0_test_masks(Tier0TestTally* tally, uint64_t seed, uint32_t count, uint64_t* entries)
{
    tier0_test_mask_build_rows();
    const bool host = tier0_test_mask_host();
    Arena arena = arena_reserve(UINT64_C(1) << 32);
    tier0_test_mask_rules(tally);
    if (arena.base != 0)
    {
        tier0_test_mask_decoration(tally, 0, host, &arena);
        tier0_test_mask_decoration(tally, 7, host, &arena);
        tier0_test_mask_programs(tally, seed, count, host, &arena, entries);
    }
    arena_release(&arena);
}
