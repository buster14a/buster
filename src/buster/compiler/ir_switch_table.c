// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// A switch whose arms only give constants, as a table of the constants (#3298). Include after ir.c. The C lowering
// dispatches a dense switch through a jump table (c_check_program_lower.c's check_lower_switch_dispatch): a bounds
// check, a 32-bit load of the entry's offset from the table, the table's address plus it, and a `br_indirect` listing
// the table's labels. Where every label it may reach is an arm that only gives a constant to one value (each a `ret K`,
// or `br J` with J's phi taking K from it, or J itself with K its phi's input from the dispatch) and every other phi of
// J takes one value from all of them, the table's entries can be the constants themselves: the dispatch then takes the
// entry it already loads as the value and goes to J (or returns it), with no indirect jump, as LLVM's SimplifyCFG's
// switch-to-lookup-table makes of it. A mispredicted indirect jump per call cost the issue's kernel 6.9 times clang
// -O2's time.
//
// Entry point: ir_switch_tables. The caller names each jump table the function's dispatches may load (IrSwitchTable:
// its symbol and, per entry, its label's place among the dispatch's listed labels); the pass gives, per table it
// converted, the entries' values as 32-bit integers, which the caller must write in place of the label offsets (the
// C driver's emission does, check_program_emit's tables), and the rewritten function. Nothing else may read a converted
// table. A table is converted only where exactly one dispatch in the function loads it.
//
// The value's type is the phi's (or the function's result): i8, i16 and i32 values are their entries' low bits, and an
// i64's entries are sign-extended, so only i64 constants that fit 32 signed bits convert. The dispatch keeps its load
// and the bounds check, whose out-of-range edge still goes to the default; the arms stay where they are, now without the
// dispatch's edges, for the optimizer's dead-code and CFG cleanup, which removes those no other block reaches and leaves
// their labels without blocks. An arm that holds anything but constants is refused: skipping it would skip its effects.
//
// The rewrite keeps every row where it is. The dispatch's `br_indirect` becomes `br J` (or `ret`), whose value is the
// load itself (i32), the lowering's sign extension of it (i64), or the load retyped as an i8 or i16 load of the entry's
// low bytes (x86-64 is little-endian), where nothing but the dispatch's own address arithmetic reads the load. J's phis
// take a new slice of the extra array with the dispatch's input added (or, where J was among the arms, replaced). The
// result borrows the input's other tables, as ir_if_convert's and ir_thread's do, and is verified by the optimizer that
// follows.
//
// Data-parallel parts: the scan for indirect jumps is a 64-byte opcode compare, an arm's rows' check is one, the
// occurrence count that guards the retyped load compares 16 operands at once, and the values are one gather per 16
// entries from the per-label constants by the entries' label places.

#ifndef BUSTER_IR_SWITCH_TABLE_C
#define BUSTER_IR_SWITCH_TABLE_C

#define BUSTER_IR_SWITCH_TABLE_BATCH 64u
// An arm's rows the opcode compare checks at once: an arm of more is refused, as no constant-only arm is that long.
#define BUSTER_IR_SWITCH_TABLE_ARM_ROWS 64u
// The casts and copies between a symbol_addr and its use that the match looks through.
#define BUSTER_IR_SWITCH_TABLE_STRIP 8u
// A table whose second dispatch was found: it isn't converted.
#define BUSTER_IR_SWITCH_TABLE_POISONED (BUSTER_IR_NONE - 1)

// A jump table a function's dispatch may load (the C lowering's CheckLowerTable): the symbol its symbol_addr names, and
// for each entry from the switch's lowest value its label's place among the dispatch's listed labels, `first` plus it.
typedef struct IrSwitchTable IrSwitchTable;
struct IrSwitchTable
{
    const uint32_t* entries;
    uint32_t count;
    uint32_t first;
    uint32_t symbol;
};

typedef struct IrSwitchTableResult IrSwitchTableResult;
struct IrSwitchTableResult
{
    // The rewritten function, or the input itself (borrowed) where no table converted.
    IrFunction function;
    // Per input table, its entries' values, `count` of them, where it converted, and null where it didn't; null itself
    // where none did.
    int32_t** values;
    uint32_t converted;
};

// One dispatch's conversion, as the match decides it.
typedef struct IrSwitchTablePlan IrSwitchTablePlan;
struct IrSwitchTablePlan
{
    // The dispatch's terminator and block, the join (BUSTER_IR_NONE for returns), the value the dispatch then gives and
    // the type it has, the load, and the phi of J the table feeds (BUSTER_IR_NONE for returns).
    uint32_t terminator;
    uint32_t dispatch;
    uint32_t join;
    uint32_t value;
    uint32_t load;
    uint32_t phi;
    // An arm that branches to J, whose input a shared phi gives the dispatch too.
    uint32_t arm;
    uint8_t type;
    // Whether the load is retyped to `type` (an i8 or i16 value), and whether the dispatch was already one of J's
    // predecessors (an arm was J itself).
    bool retype;
    bool joined;
};

static uint32_t ir_switch_table_block(const IrFunction* f, uint32_t instruction)
{
    uint32_t first = 0;
    uint32_t end = f->block_count;
    while (end - first > 1)
    {
        const uint32_t middle = first + (end - first) / 2;
        const bool after = f->block_offsets[middle] <= instruction;
        first = after ? middle : first;
        end = after ? end : middle;
    }
    return first;
}

// The value `value` stands for through pointer casts and copies of 64-bit values, which change no bits.
static uint32_t ir_switch_table_strip(const IrFunction* f, uint32_t value)
{
    uint32_t result = value;
    bool more = true;
    for (uint32_t step = 0; more && step < BUSTER_IR_SWITCH_TABLE_STRIP; step += 1)
    {
        const uint32_t op = result < f->instruction_count ? f->op[result] : IR_INVALID;
        const uint32_t source = op == IR_INTTOPTR || op == IR_PTRTOINT || op == IR_COPY ? f->a[result] : BUSTER_IR_NONE;
        more = source < f->instruction_count && (f->type[source] == IR_I64 || f->type[source] == IR_PTR) && (f->type[result] == IR_I64 || f->type[result] == IR_PTR);
        result = more ? source : result;
    }
    return result;
}

// The symbol of the symbol_addr `value` stands for, or BUSTER_IR_NONE.
static uint32_t ir_switch_table_symbol(const IrFunction* f, uint32_t value)
{
    const uint32_t base = ir_switch_table_strip(f, value);
    const uint32_t result = base < f->instruction_count && f->op[base] == IR_SYMBOL_ADDR ? f->a[base] : BUSTER_IR_NONE;
    return result;
}

// The address row `value` stands for, with its scale and offset, or BUSTER_IR_NONE.
static uint32_t ir_switch_table_address(const IrFunction* f, uint32_t value, uint32_t scale)
{
    const uint32_t row = ir_switch_table_strip(f, value);
    const bool address = row < f->instruction_count && f->op[row] == IR_ADDRESS && f->b[row] != BUSTER_IR_NONE && f->c[row] < f->address_count;
    const bool shaped = address && f->addresses[f->c[row]].scale == scale && f->addresses[f->c[row]].offset == 0;
    const uint32_t result = shaped ? row : BUSTER_IR_NONE;
    return result;
}

// How often `value` appears among the rows' fields and the extra array's words: an upper bound of its uses, which the
// rows' non-value fields (blocks, table indices) can only raise.
static uint64_t ir_switch_table_occurrences(const IrFunction* f, uint32_t value)
{
    const __m512i key = _mm512_set1_epi32((int32_t)value);
    const uint32_t* const columns[3] = { f->a, f->b, f->c };
    uint64_t count = 0;
    for (uint32_t k = 0; k < 3; k += 1)
    {
        for (uint32_t first = 0; first < f->instruction_count; first += 16)
        {
            const uint32_t lanes = f->instruction_count - first < 16 ? f->instruction_count - first : 16;
            const __mmask16 valid = (__mmask16)(0xffffu >> (16 - lanes));
            count += (uint64_t)__builtin_popcount(_mm512_mask_cmpeq_epi32_mask(valid, _mm512_maskz_loadu_epi32(valid, columns[k] + first), key));
        }
    }
    for (uint32_t first = 0; first < f->extra_count; first += 16)
    {
        const uint32_t lanes = f->extra_count - first < 16 ? f->extra_count - first : 16;
        const __mmask16 valid = (__mmask16)(0xffffu >> (16 - lanes));
        count += (uint64_t)__builtin_popcount(_mm512_mask_cmpeq_epi32_mask(valid, _mm512_maskz_loadu_epi32(valid, f->extra + first), key));
    }
    return count;
}

// Whether every row of [first, end) is a constant: a 64-byte opcode compare.
static bool ir_switch_table_constants(const IrFunction* f, uint32_t first, uint32_t end)
{
    const uint32_t rows = end - first;
    bool result = rows <= BUSTER_IR_SWITCH_TABLE_ARM_ROWS;
    if (result && rows != 0)
    {
        const __mmask64 valid = UINT64_MAX >> (BUSTER_IR_SWITCH_TABLE_ARM_ROWS - rows);
        const __m512i op = _mm512_maskz_loadu_epi8(valid, f->op + first);
        result = _mm512_mask_cmpneq_epi8_mask(valid, op, _mm512_set1_epi8(IR_CONST)) == 0;
    }
    return result;
}

// Phi `phi`'s input from `predecessor`, or BUSTER_IR_NONE.
static uint32_t ir_switch_table_incoming(const IrFunction* f, uint32_t phi, uint32_t predecessor)
{
    uint32_t result = BUSTER_IR_NONE;
    for (uint32_t k = 0; k < f->b[phi]; k += 1)
    {
        result = f->extra[f->a[phi] + 2 * k] == predecessor ? f->extra[f->a[phi] + 2 * k + 1] : result;
    }
    return result;
}

// The 32-bit entry that gives constant row `row`'s value at `type`, or false where an i64 doesn't fit 32 signed bits.
static bool ir_switch_table_entry(const IrFunction* f, uint32_t row, uint32_t type, int32_t* entry)
{
    const bool constant = row < f->instruction_count && f->op[row] == IR_CONST && f->type[row] == type && f->a[row] < f->constant_count;
    const uint64_t low = constant ? f->constants[f->a[row]].low : 0;
    const int64_t wide = type == IR_I8 ? (int64_t)(int8_t)low : type == IR_I16 ? (int64_t)(int16_t)low : type == IR_I32 ? (int64_t)(int32_t)low : (int64_t)low;
    const bool fits = constant && wide >= INT32_MIN && wide <= INT32_MAX;
    *entry = fits ? (int32_t)wide : 0;
    return fits;
}

static bool ir_switch_table_type(uint32_t type)
{
    return type == IR_I8 || type == IR_I16 || type == IR_I32 || type == IR_I64;
}

// Whether dispatch `terminator`, which loads table `table`, converts, its plan in `plan` and its labels' constants'
// entries in `entries` (one per listed label).
static bool ir_switch_table_match(const IrFunction* f, uint32_t terminator, const IrSwitchTable* table, int32_t* entries, IrSwitchTablePlan* plan)
{
    const uint32_t listed = f->c[terminator];
    const uint32_t list = f->b[terminator];
    const uint32_t target = ir_switch_table_address(f, f->a[terminator], 1);
    const uint32_t extended = target != BUSTER_IR_NONE ? f->b[target] : BUSTER_IR_NONE;
    const bool sext = extended < f->instruction_count && f->op[extended] == IR_SEXT && f->type[extended] == IR_I64;
    const uint32_t load = sext ? f->a[extended] : BUSTER_IR_NONE;
    const bool loaded = load < f->instruction_count && f->op[load] == IR_LOAD && f->type[load] == IR_I32 &&
                        (f->flags[load] & (BUSTER_IR_ACCESS_FLAGS & ~BUSTER_IR_COPY_PIECE)) == 0;
    const uint32_t entry = loaded ? ir_switch_table_address(f, f->a[load], 4) : BUSTER_IR_NONE;
    bool valid = entry != BUSTER_IR_NONE && ir_switch_table_symbol(f, f->a[entry]) == table->symbol && listed != 0 && list != BUSTER_IR_NONE && table->count != 0;
    const uint32_t dispatch = valid ? ir_switch_table_block(f, terminator) : BUSTER_IR_NONE;
    // The arms: each a run of constants and then `ret`, `br J`, or J itself.
    uint32_t join = BUSTER_IR_NONE;
    uint32_t returns = 0;
    uint32_t branches = 0;
    uint32_t others = BUSTER_IR_NONE;
    uint32_t arm_block = BUSTER_IR_NONE;
    bool mixed = false;
    for (uint32_t p = 0; valid && p < listed; p += 1)
    {
        const uint32_t label = f->extra[list + p];
        const uint32_t block = label < f->label_count ? f->labels[label] : BUSTER_IR_NONE;
        valid = block < f->block_count && block != dispatch;
        const uint32_t last = valid ? f->block_offsets[block + 1] - 1 : 0;
        const uint32_t op = valid ? f->op[last] : IR_INVALID;
        const bool arm = valid && ir_switch_table_constants(f, f->block_offsets[block], last);
        returns += arm && op == IR_RET;
        const bool branch = arm && op == IR_BR;
        // An arm that branches to itself would be its own J.
        valid = valid && (!branch || f->a[last] != block);
        branches += branch;
        mixed |= branch && join != BUSTER_IR_NONE && f->a[last] != join;
        join = branch ? f->a[last] : join;
        arm_block = branch ? block : arm_block;
        mixed |= !(arm && (op == IR_RET || op == IR_BR)) && others != BUSTER_IR_NONE && others != block;
        others = !(arm && (op == IR_RET || op == IR_BR)) ? block : others;
    }
    // Returns alone, or branches to one J with J itself the only other arm.
    valid = valid && !mixed && ((returns == listed && ir_switch_table_type(f->result_type)) || (returns == 0 && branches != 0 && (others == BUSTER_IR_NONE || others == join)));
    valid = valid && (returns != 0 || (join < f->block_count && join != dispatch));
    const uint8_t type = returns != 0 ? f->result_type : IR_VOID;
    uint32_t phi = BUSTER_IR_NONE;
    uint32_t phi_type = type;
    // A join's phis: one takes a constant from each arm, and every other one value from all of them that no arm defines.
    const uint32_t join_first = valid && returns == 0 ? f->block_offsets[join] : 0;
    uint32_t join_end = join_first;
    while (valid && returns == 0 && join_end < f->block_offsets[join + 1] && f->op[join_end] == IR_PHI)
    {
        join_end += 1;
    }
    for (uint32_t r = join_first; valid && r < join_end; r += 1)
    {
        bool same = true;
        bool constants = true;
        uint32_t first_value = BUSTER_IR_NONE;
        for (uint32_t p = 0; valid && p < listed; p += 1)
        {
            const uint32_t block = f->labels[f->extra[list + p]];
            const uint32_t value = ir_switch_table_incoming(f, r, block == join ? dispatch : block);
            valid = value < f->instruction_count;
            first_value = p == 0 ? value : first_value;
            same &= value == first_value;
            constants &= valid && f->op[value] == IR_CONST;
        }
        // A shared value defined in an arm or in J doesn't reach the dispatch's edge.
        const uint32_t defined = valid && same ? ir_switch_table_block(f, first_value) : BUSTER_IR_NONE;
        for (uint32_t p = 0; valid && same && p < listed; p += 1)
        {
            valid = defined != f->labels[f->extra[list + p]];
        }
        valid = valid && (!same || defined != join);
        const bool feeds = valid && !same && constants && ir_switch_table_type(f->type[r]);
        valid = valid && (same || (feeds && phi == BUSTER_IR_NONE));
        phi = feeds ? r : phi;
        phi_type = feeds ? f->type[r] : phi_type;
    }
    valid = valid && (returns != 0 || phi != BUSTER_IR_NONE);
    for (uint32_t p = 0; valid && p < listed; p += 1)
    {
        const uint32_t block = f->labels[f->extra[list + p]];
        const uint32_t constant = returns != 0 ? f->a[f->block_offsets[block + 1] - 1] : ir_switch_table_incoming(f, phi, block == join ? dispatch : block);
        valid = ir_switch_table_entry(f, constant, phi_type, &entries[p]);
    }
    // The value the dispatch gives: the load (i32), its sign extension (i64), or the load retyped (i8, i16), where nothing
    // else reads the load or its address arithmetic up to the jump.
    const bool retype = phi_type == IR_I8 || phi_type == IR_I16;
    if (valid && retype)
    {
        valid = ir_switch_table_occurrences(f, load) == 1 && ir_switch_table_occurrences(f, extended) == 1;
        uint32_t row = f->a[terminator];
        for (uint32_t step = 0; valid && row != extended && step <= BUSTER_IR_SWITCH_TABLE_STRIP; step += 1)
        {
            valid = row < f->instruction_count && ir_switch_table_occurrences(f, row) == 1;
            row = valid ? (row == target ? extended : f->a[row]) : row;
        }
        valid = valid && row == extended;
    }
    *plan = (IrSwitchTablePlan){
        .terminator = terminator,
        .dispatch = dispatch,
        .join = returns != 0 ? BUSTER_IR_NONE : join,
        .value = phi_type == IR_I64 ? extended : load,
        .load = load,
        .phi = phi,
        .arm = arm_block,
        .type = (uint8_t)phi_type,
        .retype = retype,
        .joined = others != BUSTER_IR_NONE,
    };
    return valid;
}

// The table each indirect jump loads, `owners[k]` the terminator of table k's one dispatch (or none, or poisoned where
// it has two): a 64-byte opcode compare finds the jumps.
static void ir_switch_table_dispatches(const IrFunction* f, const IrSwitchTable* tables, uint32_t table_count, uint32_t* owners)
{
    for (uint32_t first = 0; first < f->instruction_count; first += BUSTER_IR_SWITCH_TABLE_BATCH)
    {
        const uint32_t rows = f->instruction_count - first < BUSTER_IR_SWITCH_TABLE_BATCH ? f->instruction_count - first : BUSTER_IR_SWITCH_TABLE_BATCH;
        const __mmask64 valid = UINT64_MAX >> (BUSTER_IR_SWITCH_TABLE_BATCH - rows);
        const __m512i op = _mm512_maskz_loadu_epi8(valid, f->op + first);
        for (uint64_t jumps = _mm512_mask_cmpeq_epi8_mask(valid, op, _mm512_set1_epi8(IR_BR_INDIRECT)); jumps != 0; jumps &= jumps - 1)
        {
            const uint32_t terminator = first + (uint32_t)__builtin_ctzll(jumps);
            const uint32_t target = ir_switch_table_strip(f, f->a[terminator]);
            const uint32_t symbol = target < f->instruction_count && f->op[target] == IR_ADDRESS ? ir_switch_table_symbol(f, f->a[target]) : BUSTER_IR_NONE;
            for (uint32_t k = 0; symbol != BUSTER_IR_NONE && k < table_count; k += 1)
            {
                owners[k] = tables[k].symbol != symbol ? owners[k] : owners[k] == BUSTER_IR_NONE ? terminator : BUSTER_IR_SWITCH_TABLE_POISONED;
            }
        }
    }
}

// The table's values: entry i's label's constant, a gather of 16 entries at once by their label places.
static void ir_switch_table_values(const IrSwitchTable* table, const int32_t* constants, int32_t* values)
{
    const __m512i first = _mm512_set1_epi32((int32_t)table->first);
    for (uint32_t i = 0; i < table->count; i += 16)
    {
        const uint32_t lanes = table->count - i < 16 ? table->count - i : 16;
        const __mmask16 valid = (__mmask16)(0xffffu >> (16 - lanes));
        const __m512i places = _mm512_sub_epi32(_mm512_maskz_loadu_epi32(valid, table->entries + i), first);
        const __m512i gathered = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), valid, places, constants, 4);
        _mm512_mask_storeu_epi32(values + i, valid, gathered);
    }
}

// Whether every entry's label place is among the dispatch's `listed` labels: 16 unsigned compares at once.
static bool ir_switch_table_places(const IrSwitchTable* table, uint32_t listed)
{
    const __m512i first = _mm512_set1_epi32((int32_t)table->first);
    const __m512i bound = _mm512_set1_epi32((int32_t)listed);
    __mmask16 outside = 0;
    for (uint32_t i = 0; i < table->count; i += 16)
    {
        const uint32_t lanes = table->count - i < 16 ? table->count - i : 16;
        const __mmask16 valid = (__mmask16)(0xffffu >> (16 - lanes));
        const __m512i entries = _mm512_maskz_loadu_epi32(valid, table->entries + i);
        outside |= _mm512_mask_cmplt_epu32_mask(valid, entries, first) | _mm512_mask_cmpge_epu32_mask(valid, _mm512_sub_epi32(entries, first), bound);
    }
    return outside == 0;
}

// Plan `plan` applied to `view`, whose columns and extra array are the output's own: the dispatch's jump replaced, the
// load retyped, and J's phis given new slices at `*extra` with the dispatch's input.
static void ir_switch_table_rewrite(IrFunction* view, const IrSwitchTablePlan* plan, uint32_t* extra)
{
    const uint32_t t = plan->terminator;
    view->op[t] = plan->join == BUSTER_IR_NONE ? IR_RET : IR_BR;
    view->type[t] = IR_VOID;
    view->flags[t] = 0;
    view->alignment_log2[t] = 0;
    view->a[t] = plan->join == BUSTER_IR_NONE ? plan->value : plan->join;
    view->b[t] = BUSTER_IR_NONE;
    view->c[t] = BUSTER_IR_NONE;
    view->type[plan->load] = plan->retype ? plan->type : view->type[plan->load];
    const uint32_t join_first = plan->join != BUSTER_IR_NONE ? view->block_offsets[plan->join] : 0;
    for (uint32_t r = join_first; plan->join != BUSTER_IR_NONE && view->op[r] == IR_PHI; r += 1)
    {
        // A phi that takes one value from all the arms takes it from the dispatch too (an arm's input, `plan->arm`'s), and
        // keeps its input from the dispatch where the dispatch already reached J, which is that value. The table's phi
        // takes the dispatch's value.
        const uint32_t old = view->a[r];
        const uint32_t count = view->b[r];
        const bool added = !plan->joined;
        const bool rewritten = added || r == plan->phi;
        for (uint32_t k = 0; k < count && rewritten; k += 1)
        {
            const uint32_t predecessor = view->extra[old + 2 * k];
            view->extra[*extra + 2 * k] = predecessor;
            view->extra[*extra + 2 * k + 1] = predecessor == plan->dispatch && r == plan->phi ? plan->value : view->extra[old + 2 * k + 1];
        }
        if (rewritten && added)
        {
            view->extra[*extra + 2 * count] = plan->dispatch;
            view->extra[*extra + 2 * count + 1] = r == plan->phi ? plan->value : ir_switch_table_incoming(view, r, plan->arm);
        }
        if (rewritten)
        {
            view->a[r] = *extra;
            view->b[r] = count + (added ? 1 : 0);
            *extra += 2 * (count + (added ? 1 : 0));
        }
    }
}

// The extra words plan `plan`'s phis may take: a new slice for each of J's phis where the dispatch is a new predecessor,
// and for the table's phi alone where it was one, each at most its inputs and one more per table (the plans before it
// that share J add theirs first).
static uint64_t ir_switch_table_extra(const IrFunction* f, const IrSwitchTablePlan* plan, uint32_t table_count)
{
    uint64_t words = 0;
    const uint32_t join_first = plan->join != BUSTER_IR_NONE ? f->block_offsets[plan->join] : 0;
    for (uint32_t r = join_first; plan->join != BUSTER_IR_NONE && f->op[r] == IR_PHI; r += 1)
    {
        words += !plan->joined || r == plan->phi ? 2 * ((uint64_t)f->b[r] + table_count + 1) : 0;
    }
    return words;
}

// Every table `tables` names whose one dispatch in `f` converts (see the header): `result` the rewritten function and
// each converted table's values, in `output`; the input borrowed and no values where none converts. Scratch is
// returned. `f` must be verified SSA, before use lists or a verified-analysis revision exist; the optimizer that
// follows verifies the result.
static IrCheck ir_switch_tables(const IrFunction* f, const IrSwitchTable* tables, uint32_t table_count, Arena* output, Arena* scratch, IrSwitchTableResult* result)
{
    const uint64_t om = output->length;
    const uint64_t sm = scratch->length;
    *result = (IrSwitchTableResult){ .function = *f };
    bool ok = output != scratch;
    IrError error = ok ? IR_OK : IR_ERROR_STRUCTURE;
    const bool any = ok && table_count != 0 && f->label_count != 0 && f->instruction_count != 0;
    uint32_t* owners = any ? ir_allocate(scratch, table_count, 4, &ok) : 0;
    IrSwitchTablePlan* plans = any ? ir_allocate(scratch, table_count, sizeof(IrSwitchTablePlan), &ok) : 0;
    int32_t** constants = any ? ir_allocate(scratch, table_count, sizeof(int32_t*), &ok) : 0;
    for (uint32_t k = 0; ok && any && k < table_count; k += 1)
    {
        owners[k] = BUSTER_IR_NONE;
    }
    if (ok && any)
    {
        ir_switch_table_dispatches(f, tables, table_count, owners);
    }
    uint32_t converted = 0;
    uint64_t words = 0;
    for (uint32_t k = 0; ok && any && k < table_count; k += 1)
    {
        const uint32_t terminator = owners[k];
        const bool owned = terminator < f->instruction_count && f->b[terminator] != BUSTER_IR_NONE && f->c[terminator] != 0;
        constants[k] = owned && ir_switch_table_places(&tables[k], f->c[terminator]) ? ir_allocate(scratch, f->c[terminator], 4, &ok) : 0;
        const bool planned = constants[k] != 0 && ir_switch_table_match(f, terminator, &tables[k], constants[k], &plans[k]);
        constants[k] = planned ? constants[k] : 0;
        converted += planned;
        words += planned ? ir_switch_table_extra(f, &plans[k], table_count) : 0;
    }
    error = ok ? error : IR_ERROR_MEMORY;
    if (error == IR_OK && converted != 0)
    {
        const uint32_t n = f->instruction_count;
        const uint64_t extra_count = (uint64_t)f->extra_count + words;
        error = extra_count <= UINT32_MAX ? IR_OK : IR_ERROR_LIMIT;
        IrFunction view = *f;
        view.op = ir_allocate(output, n, 1, &ok);
        view.type = ir_allocate(output, n, 1, &ok);
        view.flags = ir_allocate(output, n, 1, &ok);
        view.alignment_log2 = ir_allocate(output, n, 1, &ok);
        view.a = ir_allocate(output, n, 4, &ok);
        view.b = ir_allocate(output, n, 4, &ok);
        view.c = ir_allocate(output, n, 4, &ok);
        view.extra = error == IR_OK ? ir_allocate(output, extra_count, 4, &ok) : 0;
        int32_t** values = ir_allocate(output, table_count, sizeof(int32_t*), &ok);
        for (uint32_t k = 0; ok && k < table_count; k += 1)
        {
            values[k] = constants[k] != 0 ? ir_allocate(output, tables[k].count, 4, &ok) : 0;
        }
        error = error == IR_OK && !ok ? IR_ERROR_MEMORY : error;
        if (error == IR_OK)
        {
            memcpy(view.op, f->op, n);
            memcpy(view.type, f->type, n);
            memcpy(view.flags, f->flags, n);
            memcpy(view.alignment_log2, f->alignment_log2, n);
            memcpy(view.a, f->a, (uint64_t)n * 4);
            memcpy(view.b, f->b, (uint64_t)n * 4);
            memcpy(view.c, f->c, (uint64_t)n * 4);
            if (f->extra_count != 0)
            {
                memcpy(view.extra, f->extra, (uint64_t)f->extra_count * 4);
            }
            uint32_t extra = f->extra_count;
            for (uint32_t k = 0; k < table_count; k += 1)
            {
                if (constants[k] != 0)
                {
                    ir_switch_table_values(&tables[k], constants[k], values[k]);
                    ir_switch_table_rewrite(&view, &plans[k], &extra);
                }
            }
            view.extra_count = extra;
            view.arena_bytes = output->length - om;
            result->function = view;
            result->values = values;
            result->converted = converted;
        }
    }
    if (error != IR_OK)
    {
        arena_pop_to(output, om);
        *result = (IrSwitchTableResult){ .function = *f };
    }
    arena_pop_to(scratch, sm);
    const IrCheck check = ir_check(error, BUSTER_IR_NONE, 0);
    return check;
}

#endif
