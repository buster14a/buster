// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// test-types' check of #113's deferred-body support in the type table (c_types.c, "Deferred bodies"). A unit here is
// a random list of type operations at source positions, some at the top level and some in function bodies, which are
// disjoint ranges of positions. Each unit is run twice:
//   - in source order, on one table, as source-order checking makes its types;
//   - deferred: the top level alone first, recording its changes; then every body, in a random order, on a lane's copy
//     of the top level's table (type_copy) that hides the top level's later types and withdraws the definitions it
//     made later (type_tag_withdraw), recording the body's changes and then taking the copy back (type_view_reset);
//     then type_replay of every record into a new table.
// The replayed table must be the source-order one, column by column with its index, tags, members and last_error, and
// every operation's result, mapped from its own table's ids (type_replay_type), the source-order run's, as must every
// question a body asks of the types it sees (complete? size? transparent?). Units vary the lanes (1, 2, 3 and 8) and
// the bodies' order. Fixtures check that a view's batch requests pass its hidden types over, that a replay refuses an
// incomplete record, and that a view whose index grew is taken back as it was.
//
// Include in c_types_test.c, before run_type_tests.

#define BUSTER_TYPE_DEFERRED_NONE UINT32_MAX
// The unit's builtin operands, which every table has: their values are fixed.
#define BUSTER_TYPE_DEFERRED_BUILTINS 6
// How many operands an operation takes at most (a record's members, a function's return type and parameters).
#define BUSTER_TYPE_DEFERRED_OPERANDS 3

typedef enum TypeDeferredKind
{
    TYPE_DEFERRED_POINTER,
    TYPE_DEFERRED_ARRAY,
    TYPE_DEFERRED_FUNCTION,
    TYPE_DEFERRED_ALIGNED,
    TYPE_DEFERRED_ATOMIC,
    TYPE_DEFERRED_RECORD,
    TYPE_DEFERRED_FORWARD,
    TYPE_DEFERRED_COMPLETE,
    TYPE_DEFERRED_ENUM,
    TYPE_DEFERRED_TRANSPARENT,
    TYPE_DEFERRED_VA_LIST,
    TYPE_DEFERRED_QUERY,
    TYPE_DEFERRED_KINDS,
} TypeDeferredKind;

// An operation, in source order: at `position`, of the top level (body 0) or of body - 1. Operands are earlier
// operations' indices that it may see (its body's, or the top level's at its body's opening or before), or builtin
// operands (BUSTER_TYPE_DEFERRED_NONE - 1 - i), or none.
typedef struct TypeDeferredOp TypeDeferredOp;
struct TypeDeferredOp
{
    uint32_t position;
    uint32_t body;
    uint8_t kind;
    uint8_t tag_kind;
    uint32_t extent;
    uint32_t operands[BUSTER_TYPE_DEFERRED_OPERANDS];
};

typedef struct TypeDeferredBody TypeDeferredBody;
struct TypeDeferredBody
{
    uint32_t open;
    uint32_t close;
    // Its operations, from first, count of them, in the unit's list.
    uint32_t first;
    uint32_t count;
};

typedef struct TypeDeferredUnit TypeDeferredUnit;
struct TypeDeferredUnit
{
    TypeDeferredOp* ops;
    uint32_t op_count;
    TypeDeferredBody* bodies;
    uint32_t body_count;
};

// What the units exercised, for the report.
typedef struct TypeDeferredCoverage TypeDeferredCoverage;
struct TypeDeferredCoverage
{
    uint64_t units;
    uint64_t bodies;
    uint64_t changes;
    // Types a body made again because its view hid the top level's, definitions and transparency withdrawn from a
    // body's view, predefined tags the replay merged, and failed requests (last_error's writes).
    uint64_t remade;
    uint64_t withdrawn;
    uint64_t merged;
    uint64_t errors;
};

// What a run made of the unit: each operation's type in its own table's ids, and a QUERY's answers.
typedef struct TypeDeferredResults TypeDeferredResults;
struct TypeDeferredResults
{
    TypeId* values;
    uint64_t* answers;
};

static TypeId type_deferred_builtin(uint32_t i)
{
    static const TypeId builtins[BUSTER_TYPE_DEFERRED_BUILTINS] = {
        BUSTER_TYPE_INT, BUSTER_TYPE_CHAR, BUSTER_TYPE_DOUBLE, BUSTER_TYPE_VOID, BUSTER_TYPE_LONG, BUSTER_TYPE_UINT,
    };
    return builtins[i];
}

static uint32_t type_deferred_builtin_operand(uint32_t i)
{
    return BUSTER_TYPE_DEFERRED_NONE - 1 - i;
}

// The value of an operand in a run: a builtin's, or an operation's.
static TypeId type_deferred_value(const TypeDeferredResults* results, uint32_t operand)
{
    TypeId value = 0;
    if (operand != BUSTER_TYPE_DEFERRED_NONE && operand >= BUSTER_TYPE_DEFERRED_NONE - BUSTER_TYPE_DEFERRED_BUILTINS)
    {
        value = type_deferred_builtin(BUSTER_TYPE_DEFERRED_NONE - 1 - operand);
    }
    else if (operand != BUSTER_TYPE_DEFERRED_NONE)
    {
        value = results->values[operand];
    }
    return value;
}

// A random operand an operation of `body` at index `op` may see: a builtin, or an earlier operation of its body or of
// the top level at or before the body's opening (for the top level, any earlier top-level operation; with `own`, its
// body's alone), of `kind` if it isn't TYPE_DEFERRED_KINDS. Kinds that make no value (QUERY) are never operands.
static uint32_t type_deferred_pick(const TypeDeferredUnit* unit, uint64_t* random, uint32_t op, uint32_t body, uint32_t open, uint8_t kind, bool own)
{
    uint32_t result = kind == TYPE_DEFERRED_KINDS ? type_deferred_builtin_operand((uint32_t)type_test_below(random, BUSTER_TYPE_DEFERRED_BUILTINS)) :
                                                     BUSTER_TYPE_DEFERRED_NONE;
    const uint32_t tries = op < 8 ? op : 8;
    for (uint32_t t = 0; t < tries; t += 1)
    {
        const uint32_t candidate = (uint32_t)type_test_below(random, op);
        const TypeDeferredOp* other = unit->ops + candidate;
        const bool visible = other->body == body || (!own && other->body == 0 && (body == 0 || other->position <= open));
        const bool fits = kind == TYPE_DEFERRED_KINDS ? other->kind != TYPE_DEFERRED_QUERY : other->kind == kind;
        result = visible && fits && type_test_below(random, 4) != 0 ? candidate : result;
    }
    return result;
}

// Fills a unit with `count` positions' worth of top-level operations and bodies, from `random`.
static bool type_deferred_generate(TypeDeferredUnit* unit, uint64_t* random, uint32_t count, Arena* arena)
{
    unit->ops = BUSTER_ARENA_PUSH_ARRAY(arena, TypeDeferredOp, (uint64_t)count * 2 + 1);
    unit->bodies = BUSTER_ARENA_PUSH_ARRAY(arena, TypeDeferredBody, (uint64_t)count + 1);
    unit->op_count = 0;
    unit->body_count = 0;
    const bool valid = unit->ops != 0 && unit->bodies != 0;
    uint32_t position = 0;
    uint32_t body = 0;
    uint32_t open = 0;
    uint32_t remaining = 0;
    while (valid && position < count)
    {
        if (body == 0 && type_test_below(random, 16) == 0 && position + 4 < count)
        {
            // A body opens here, with the top level's operations at its opening first, as a function's own
            // declaration is bound at its body's brace.
            TypeDeferredBody* opened = unit->bodies + unit->body_count;
            opened->open = position;
            const uint32_t at_open = (uint32_t)type_test_below(random, 3);
            for (uint32_t i = 0; i < at_open; i += 1)
            {
                TypeDeferredOp* op = unit->ops + unit->op_count;
                *op = (TypeDeferredOp){ .position = position, .body = 0, .kind = TYPE_DEFERRED_POINTER };
                op->operands[0] = type_deferred_pick(unit, random, unit->op_count, 0, 0, TYPE_DEFERRED_KINDS, false);
                op->operands[1] = BUSTER_TYPE_DEFERRED_NONE;
                op->operands[2] = BUSTER_TYPE_DEFERRED_NONE;
                unit->op_count += 1;
            }
            opened->first = unit->op_count;
            opened->count = 0;
            unit->body_count += 1;
            body = unit->body_count;
            open = position;
            remaining = 1 + (uint32_t)type_test_below(random, 24);
            // The body's first operations may come at its opening too, after the top level's there, as a body binds
            // its parameters at its brace.
            position += type_test_below(random, 2);
        }
        else if (body != 0 && remaining == 0)
        {
            unit->bodies[body - 1].close = position;
            body = 0;
            position += 1;
        }
        else
        {
            TypeDeferredOp* op = unit->ops + unit->op_count;
            const uint32_t index = unit->op_count;
            uint8_t kind = (uint8_t)type_test_below(random, TYPE_DEFERRED_KINDS);
            // Transparency is marked at the top level only, as a block's typedef seldom does it; the checker's
            // deferred reader takes such a unit in source order.
            kind = kind == TYPE_DEFERRED_TRANSPARENT && body != 0 ? TYPE_DEFERRED_QUERY : kind;
            *op = (TypeDeferredOp){
                .position = position,
                .body = body,
                .kind = kind,
                .tag_kind = type_test_below(random, 2) == 0 ? BUSTER_TYPE_KIND_STRUCT : BUSTER_TYPE_KIND_UNION,
                .extent = 1 + (uint32_t)type_test_below(random, 4),
            };
            for (uint32_t i = 0; i < BUSTER_TYPE_DEFERRED_OPERANDS; i += 1)
            {
                op->operands[i] = type_deferred_pick(unit, random, index, body, open, TYPE_DEFERRED_KINDS, false);
            }
            if (kind == TYPE_DEFERRED_COMPLETE)
            {
                // A body completes its own tags alone: an outer tag's definition in a block is a new tag.
                op->operands[0] = type_deferred_pick(unit, random, index, body, open, TYPE_DEFERRED_FORWARD, true);
            }
            else if (kind == TYPE_DEFERRED_TRANSPARENT)
            {
                op->operands[0] = type_deferred_pick(unit, random, index, body, open, TYPE_DEFERRED_RECORD, false);
            }
            else if (kind == TYPE_DEFERRED_QUERY && type_test_below(random, 2) == 0)
            {
                // Tags of the top level that may be completed, or made transparent, after the body.
                op->operands[0] = type_deferred_pick(unit, random, index, body, open, type_test_below(random, 2) == 0 ? TYPE_DEFERRED_FORWARD : TYPE_DEFERRED_RECORD,
                                                     false);
            }
            unit->op_count += 1;
            unit->bodies[body != 0 ? body - 1 : 0].count += body != 0;
            remaining -= body != 0;
            position += 1;
        }
    }
    if (body != 0)
    {
        unit->bodies[body - 1].close = position;
    }
    return valid;
}

// The va_list's names: the tag's and its members'.
static const uint32_t type_deferred_va_names[5] = { 9001, 9002, 9003, 9004, 9005 };

// Runs one operation on `table`, its operands' values from `results`; `va_list` is the run's (or the body's) cache of
// the predefined type, which the record marks with 1 when it makes it.
static void type_deferred_run_op(TypeTable* table, const TypeDeferredOp* op, uint32_t index, TypeDeferredResults* results, TypeId* va_list)
{
    const TypeId a = type_deferred_value(results, op->operands[0]);
    const TypeId b = type_deferred_value(results, op->operands[1]);
    const TypeId c = type_deferred_value(results, op->operands[2]);
    TypeId value = 0;
    if (op->kind == TYPE_DEFERRED_POINTER && a != 0)
    {
        value = type_pointer(table, a);
    }
    else if (op->kind == TYPE_DEFERRED_ARRAY && a != 0)
    {
        value = type_array(table, a, op->extent);
    }
    else if (op->kind == TYPE_DEFERRED_FUNCTION && a != 0)
    {
        TypeId parameters[2] = { b != 0 ? type_adjust_parameter(table, b) : 0, c != 0 ? type_adjust_parameter(table, c) : 0 };
        const uint32_t count = parameters[0] == 0 ? 0 : parameters[1] == 0 ? 1 : 2;
        value = type_function(table, a, parameters, count, BUSTER_TYPE_FLAG_PROTOTYPE);
    }
    else if (op->kind == TYPE_DEFERRED_ALIGNED && a != 0)
    {
        value = type_aligned(table, a, op->extent);
    }
    else if (op->kind == TYPE_DEFERRED_ATOMIC && a != 0)
    {
        value = type_atomic(table, a);
    }
    else if (op->kind == TYPE_DEFERRED_RECORD || op->kind == TYPE_DEFERRED_FORWARD || op->kind == TYPE_DEFERRED_ENUM)
    {
        const uint8_t tag_kind = op->kind == TYPE_DEFERRED_ENUM ? BUSTER_TYPE_KIND_ENUM : op->tag_kind;
        value = type_tag(table, tag_kind, 100 + index);
        if (value != 0 && op->kind == TYPE_DEFERRED_RECORD)
        {
            // A union of two members of one size can be made transparent.
            const TypeId first = a != 0 ? a : BUSTER_TYPE_INT;
            const TypeMember members[2] = { type_test_member(1, first, BUSTER_TYPE_NOT_BIT_FIELD), type_test_member(2, op->tag_kind == BUSTER_TYPE_KIND_UNION ? first : (b != 0 ? b : BUSTER_TYPE_CHAR), BUSTER_TYPE_NOT_BIT_FIELD) };
            type_complete_record(table, value, members, 2, (TypeRecordAttributes){ 0 });
        }
        else if (value != 0 && op->kind == TYPE_DEFERRED_ENUM)
        {
            type_enum_complete(table, value, op->extent % 2 == 0 ? BUSTER_TYPE_INT : BUSTER_TYPE_UINT, 0);
        }
    }
    else if (op->kind == TYPE_DEFERRED_COMPLETE && a != 0)
    {
        const TypeMember members[2] = { type_test_member(1, b != 0 ? b : BUSTER_TYPE_INT, BUSTER_TYPE_NOT_BIT_FIELD), type_test_member(2, c != 0 ? c : BUSTER_TYPE_LONG, BUSTER_TYPE_NOT_BIT_FIELD) };
        if (type_kind(table, a) != BUSTER_TYPE_KIND_ENUM)
        {
            type_complete_record(table, a, members, 2, (TypeRecordAttributes){ 0 });
        }
        value = a;
    }
    else if (op->kind == TYPE_DEFERRED_TRANSPARENT && a != 0)
    {
        type_mark_transparent_union(table, a);
        value = a;
    }
    else if (op->kind == TYPE_DEFERRED_VA_LIST)
    {
        if (*va_list == 0)
        {
            if (table->changes != 0)
            {
                table->changes->predefined = 1;
            }
            *va_list = type_builtin_va_list(table, type_deferred_va_names);
            if (table->changes != 0)
            {
                table->changes->predefined = 0;
            }
        }
        value = *va_list;
    }
    else if (op->kind == TYPE_DEFERRED_QUERY && a != 0)
    {
        results->answers[index] = (uint64_t)type_is_complete(table, a) | (uint64_t)type_is_transparent_union(table, a) << 1 | type_size(table, a) << 2 |
                                  (uint64_t)type_alignment_log2(table, a) << 40;
    }
    results->values[index] = value;
}

static bool type_deferred_same_tables(const TypeTable* a, const TypeTable* b)
{
    bool same = type_test_same_tables(a, b) && a->tags.count == b->tags.count && a->tags.completions == b->tags.completions &&
                a->members.count == b->members.count && a->lookup.count == b->lookup.count && a->last_error == b->last_error;
    const uint64_t tags = a->tags.count;
    same = same && memcmp(a->tags.type, b->tags.type, tags * sizeof(TypeId)) == 0 && memcmp(a->tags.name, b->tags.name, tags * sizeof(uint32_t)) == 0 &&
           memcmp(a->tags.member_begin, b->tags.member_begin, tags * sizeof(uint32_t)) == 0 &&
           memcmp(a->tags.member_count, b->tags.member_count, tags * sizeof(uint32_t)) == 0 &&
           memcmp(a->tags.completion, b->tags.completion, tags * sizeof(uint32_t)) == 0 && memcmp(a->tags.packed, b->tags.packed, tags) == 0 &&
           memcmp(a->tags.pack_log2, b->tags.pack_log2, tags) == 0 && memcmp(a->tags.alignment_log2, b->tags.alignment_log2, tags) == 0 &&
           memcmp(a->tags.lookup, b->tags.lookup, tags * sizeof(uint32_t)) == 0 && memcmp(a->tags.lookup_log2, b->tags.lookup_log2, tags) == 0;
    const uint64_t members = a->members.count;
    same = same && memcmp(a->members.name, b->members.name, members * sizeof(uint32_t)) == 0 &&
           memcmp(a->members.type, b->members.type, members * sizeof(TypeId)) == 0 &&
           memcmp(a->members.bit_width, b->members.bit_width, members * sizeof(uint32_t)) == 0 &&
           memcmp(a->members.alignment_log2, b->members.alignment_log2, members) == 0 && memcmp(a->members.flags, b->members.flags, members) == 0 &&
           memcmp(a->members.offset, b->members.offset, members * sizeof(uint64_t)) == 0;
    same = same && memcmp(a->lookup.buckets, b->lookup.buckets, (uint64_t)a->lookup.count * sizeof(TypeLookupBucket)) == 0;
    return same;
}

// One unit in source order and deferred on `lanes` lanes, its bodies in an order from `random`: whether they agree.
static bool type_deferred_unit(TypeTestTally* tally, TypeDeferredCoverage* coverage, const TypeDeferredUnit* unit, uint32_t lanes, uint64_t* random,
                               Arena* arena)
{
    const uint64_t mark = arena->length;
    const uint32_t op_count = unit->op_count;
    TypeDeferredResults sequential = {
        .values = BUSTER_ARENA_PUSH_ARRAY(arena, TypeId, (uint64_t)op_count + 1),
        .answers = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, (uint64_t)op_count + 1),
    };
    TypeDeferredResults deferred = {
        .values = BUSTER_ARENA_PUSH_ARRAY(arena, TypeId, (uint64_t)op_count + 1),
        .answers = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, (uint64_t)op_count + 1),
    };
    uint32_t* order = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)unit->body_count + 1);
    TypeChanges* records = BUSTER_ARENA_PUSH_ARRAY(arena, TypeChanges, (uint64_t)unit->body_count + 1);
    TypeReplayPart* parts = BUSTER_ARENA_PUSH_ARRAY(arena, TypeReplayPart, (uint64_t)unit->body_count + 1);
    uint32_t* hidden = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)unit->body_count + 1);
    bool valid = sequential.values != 0 && sequential.answers != 0 && deferred.values != 0 && deferred.answers != 0 && order != 0 && records != 0 &&
                 parts != 0 && hidden != 0;
    TypeTestTable source = { 0 };
    TypeTestTable top = { 0 };
    TypeTestTable replayed = { 0 };
    TypeTestTable lane_tables[8] = { 0 };
    bool lane_open[8] = { false };
    const uint32_t capacity = op_count * 4 + 64;
    valid = valid && type_test_table_open(&source, capacity) && type_test_table_open(&top, capacity) && type_test_table_open(&replayed, capacity);
    if (valid)
    {
        memset(sequential.answers, 0, ((uint64_t)op_count + 1) * sizeof(uint64_t));
        memset(deferred.answers, 0, ((uint64_t)op_count + 1) * sizeof(uint64_t));
        // In source order: the list's order.
        TypeId va_list = 0;
        for (uint32_t i = 0; i < op_count; i += 1)
        {
            type_deferred_run_op(&source.table, unit->ops + i, i, &sequential, &va_list);
        }
        // The top level alone, its record kept, with the count of types it has made by each body's opening.
        TypeChanges top_changes = { 0 };
        valid = type_changes_initialize(&top_changes, arena, capacity * 2, capacity * 2, capacity * 2);
        top.table.changes = valid ? &top_changes : 0;
        TypeId top_va_list = 0;
        uint32_t top_va_position = BUSTER_TYPE_DEFERRED_NONE;
        uint32_t body = 0;
        for (uint32_t i = 0; valid && i < op_count; i += 1)
        {
            const TypeDeferredOp* op = unit->ops + i;
            while (body < unit->body_count && unit->bodies[body].open < op->position)
            {
                hidden[body] = top.table.types.count;
                body += 1;
            }
            if (op->body == 0)
            {
                type_changes_at(&top.table, op->position);
                const bool made_va_list = op->kind == TYPE_DEFERRED_VA_LIST && top_va_list == 0;
                type_deferred_run_op(&top.table, op, i, &deferred, &top_va_list);
                top_va_position = made_va_list ? op->position : top_va_position;
            }
        }
        while (body < unit->body_count)
        {
            hidden[body] = top.table.types.count;
            body += 1;
        }
        top.table.changes = 0;
        valid = valid && !top_changes.full;
        // The bodies in a random order, each on its lane's copy.
        for (uint32_t i = 0; i < unit->body_count; i += 1)
        {
            order[i] = i;
        }
        for (uint32_t i = unit->body_count; i > 1; i -= 1)
        {
            const uint32_t j = (uint32_t)type_test_below(random, i);
            const uint32_t swap = order[i - 1];
            order[i - 1] = order[j];
            order[j] = swap;
        }
        for (uint32_t k = 0; valid && k < unit->body_count; k += 1)
        {
            const uint32_t b = order[k];
            const TypeDeferredBody* current = unit->bodies + b;
            const uint32_t lane = k % lanes;
            TypeTable* table = &lane_tables[lane].table;
            if (!lane_open[lane])
            {
                lane_tables[lane].arena = arena_reserve(UINT64_C(1) << 28);
                lane_tables[lane].index_arena = arena_reserve(UINT64_C(1) << 26);
                const TypeCapacity extra = { .types = capacity, .parameters = capacity, .tags = capacity, .members = capacity * 2 };
                valid = type_copy(table, &top.table, &lane_tables[lane].arena, &lane_tables[lane].index_arena, extra);
                lane_open[lane] = valid;
            }
            const TypeViewMark view = type_view_mark(table);
            table->hidden_begin = hidden[b];
            table->hidden_end = top.table.types.count;
            // The top level's definitions after the body's opening, withdrawn.
            for (uint32_t c = 0; valid && c < top_changes.count; c += 1)
            {
                const TypeChange* change = top_changes.items + c;
                if (change->position > current->open && (change->operation == BUSTER_TYPE_CHANGE_LAYOUT || change->operation == BUSTER_TYPE_CHANGE_ENUM ||
                                                         change->operation == BUSTER_TYPE_CHANGE_DEFINE))
                {
                    type_tag_withdraw(table, change->tag, false);
                    coverage->withdrawn += change->operation != BUSTER_TYPE_CHANGE_DEFINE;
                }
                else if (change->position > current->open && change->operation == BUSTER_TYPE_CHANGE_TRANSPARENT)
                {
                    type_tag_withdraw(table, change->tag, true);
                    coverage->withdrawn += 1;
                }
            }
            valid = valid && type_changes_initialize(records + b, arena, capacity, capacity, capacity * 2);
            table->changes = valid ? records + b : 0;
            TypeId body_va_list = top_va_position <= current->open ? top_va_list : 0;
            for (uint32_t o = current->first; valid && o < current->first + current->count; o += 1)
            {
                type_changes_at(table, unit->ops[o].position);
                type_deferred_run_op(table, unit->ops + o, o, &deferred, &body_va_list);
            }
            table->changes = 0;
            for (uint32_t c = 0; c < records[b].count; c += 1)
            {
                // A body's derived type of the top level's types alone that the top level has made too, later.
                const TypeChange* change = records[b].items + c;
                const bool top_operands = type_index(change->base) < view.types;
                for (uint32_t t = hidden[b]; change->operation == BUSTER_TYPE_CHANGE_DERIVE && top_operands && t < top.table.types.count; t += 1)
                {
                    const bool same = top.table.types.kind[t] == change->kind && top.table.types.base[t] == change->base &&
                                      top.table.types.extent[t] == change->extent && change->kind != BUSTER_TYPE_KIND_FUNCTION;
                    coverage->remade += same;
                    t = same ? top.table.types.count : t;
                }
            }
            coverage->changes += records[b].count;
            coverage->errors += records[b].error_written;
            const uint32_t local_types = table->types.count - view.types;
            const uint32_t local_tags = table->tags.count - view.tags;
            parts[1 + b] = (TypeReplayPart){
                .changes = records + b,
                .parent = &parts[0],
                .base = view.types,
                .tag_base = view.tags,
                .types = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)local_types + 1),
                .tags = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)local_tags + 1),
                .merged = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, (uint64_t)local_tags + 1),
            };
            valid = valid && parts[1 + b].types != 0 && parts[1 + b].tags != 0 && parts[1 + b].merged != 0 && !records[b].full && type_view_reset(table, view);
            for (uint32_t c = 0; valid && c < top_changes.count; c += 1)
            {
                const TypeChange* change = top_changes.items + c;
                if (change->position > current->open && change->operation >= BUSTER_TYPE_CHANGE_DEFINE)
                {
                    type_tag_restore(table, &top.table, change->tag);
                }
            }
        }
        type_test_expect(tally, valid, "deferred", "a run couldn't be made");
        // The replay, the bodies in source order.
        parts[0] = (TypeReplayPart){
            .changes = &top_changes,
            .base = BUSTER_TYPE_BUILTIN_COUNT,
            .tag_base = 1,
            .types = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, top.table.types.count),
            .tags = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, top.table.tags.count),
            .merged = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, top.table.tags.count),
        };
        valid = valid && parts[0].types != 0 && parts[0].tags != 0 && parts[0].merged != 0;
        const bool replay = valid && type_replay(&replayed.table, parts, unit->body_count + 1, arena);
        for (uint32_t p = 1; replay && p <= unit->body_count; p += 1)
        {
            for (uint32_t c = 0; c < records[p - 1].count; c += 1)
            {
                const TypeChange* change = records[p - 1].items + c;
                coverage->merged += change->operation == BUSTER_TYPE_CHANGE_TAG && parts[p].merged[change->tag - parts[p].tag_base] != 0;
            }
        }
        for (uint32_t c = 0; replay && c < top_changes.count; c += 1)
        {
            const TypeChange* change = top_changes.items + c;
            coverage->merged += change->operation == BUSTER_TYPE_CHANGE_TAG && parts[0].merged[change->tag - parts[0].tag_base] != 0;
        }
        coverage->units += 1;
        coverage->bodies += unit->body_count;
        coverage->changes += top_changes.count;
        coverage->errors += top_changes.error_written;
        type_test_expect(tally, !valid || replay, "deferred", "the replay failed");
        valid = valid && replay;
        if (valid)
        {
            char detail[160];
            snprintf(detail, sizeof(detail), "the replayed table isn't source order's (%u types against %u, %u tags against %u)", replayed.table.types.count,
                     source.table.types.count, replayed.table.tags.count, source.table.tags.count);
            type_test_expect(tally, type_deferred_same_tables(&replayed.table, &source.table), "deferred", detail);
            uint32_t different = 0;
            uint32_t first = BUSTER_TYPE_DEFERRED_NONE;
            for (uint32_t i = 0; i < op_count; i += 1)
            {
                const TypeDeferredOp* op = unit->ops + i;
                const TypeReplayPart* part = op->body == 0 ? &parts[0] : &parts[op->body];
                const TypeId mapped = deferred.values[i] != 0 ? type_replay_type(part, deferred.values[i]) : 0;
                const bool same = mapped == sequential.values[i] && deferred.answers[i] == sequential.answers[i];
                different += !same;
                first = !same && first == BUSTER_TYPE_DEFERRED_NONE ? i : first;
            }
            const TypeDeferredOp* wrong = first != BUSTER_TYPE_DEFERRED_NONE ? unit->ops + first : unit->ops;
            snprintf(detail, sizeof(detail), "%u operations' results aren't source order's, the first #%u (kind %u, body %u, position %u)", different, first,
                     wrong->kind, wrong->body, wrong->position);
            type_test_expect(tally, different == 0, "deferred", detail);
        }
    }
    for (uint32_t lane = 0; lane < 8; lane += 1)
    {
        if (lane_tables[lane].arena.base != 0)
        {
            type_test_table_close(&lane_tables[lane]);
        }
    }
    type_test_table_close(&replayed);
    type_test_table_close(&top);
    type_test_table_close(&source);
    arena_pop_to(arena, mark);
    return valid;
}

// A copy's index (type_copy): the source's slots copied as they are where its index has room for the copy's slack (no
// more than BUSTER_TYPE_COPY_INDEX_SLACK and than the source's own types, BUSTER_TYPE_COPY_INDEX_MINIMUM at least), and
// the source's types placed again with room for the slack otherwise. Chains of 100 pointers (their slack the minimum,
// past the 64 buckets their index has), 7,000 (BUSTER_TYPE_COPY_INDEX_SLACK, which the 4,096 buckets theirs grew to
// have room for) and 9,000 (whose don't). The copy finds every type of the source's at its id, and grows past its room
// as any index does.
static void type_deferred_index_copies(TypeTestTally* tally)
{
    static const uint32_t lengths[] = { 100, 7000, 9000 };
    static const bool copied[] = { false, true, false };
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(lengths); c += 1)
    {
        TypeTestTable source = { 0 };
        TypeTestTable copy = { 0 };
        bool valid = type_test_table_open(&source, lengths[c] + 64);
        TypeId last = BUSTER_TYPE_INT;
        for (uint32_t i = 0; valid && i < lengths[c]; i += 1)
        {
            last = type_pointer(&source.table, last);
            valid = last != 0;
        }
        copy.arena = arena_reserve(UINT64_C(1) << 28);
        copy.index_arena = arena_reserve(UINT64_C(1) << 26);
        const TypeCapacity extra = { .types = 16384, .parameters = 64, .tags = 64, .members = 64 };
        valid = valid && type_copy(&copy.table, &source.table, &copy.arena, &copy.index_arena, extra);
        type_test_expect(tally, valid, "deferred index copies", "a copy couldn't be made");
        if (valid)
        {
            const TypeIndex* index = &source.table.index;
            const uint64_t own = index->count > BUSTER_TYPE_COPY_INDEX_MINIMUM ? index->count : BUSTER_TYPE_COPY_INDEX_MINIMUM;
            const uint64_t slack = own < BUSTER_TYPE_COPY_INDEX_SLACK ? own : BUSTER_TYPE_COPY_INDEX_SLACK;
            uint64_t buckets = BUSTER_TYPE_INITIAL_BUCKETS;
            while (buckets * 3 < (uint64_t)source.table.types.count + slack + 1)
            {
                buckets *= 2;
            }
            const bool same = copied[c] ? copy.table.index.bucket_count == index->bucket_count &&
                                              memcmp(copy.table.index.slots, index->slots, (uint64_t)index->bucket_count * BUSTER_TYPE_BUCKET_BYTES) == 0 :
                                          copy.table.index.bucket_count == buckets && buckets > index->bucket_count;
            type_test_expect(tally, same && copy.table.index.count == index->count, "deferred index copies",
                             copied[c] ? "the source's slots weren't copied as they are" : "the index wasn't made again with room for the slack");
            // Every type of the source's at its id, none made again.
            TypeId found = BUSTER_TYPE_INT;
            for (uint32_t i = 0; found != 0 && i < lengths[c]; i += 1)
            {
                const TypeId next = type_pointer(&copy.table, found);
                found = next != 0 && type_index(next) < source.table.types.count && source.table.types.base[type_index(next)] == found ? next : 0;
            }
            type_test_expect(tally, found == last && copy.table.types.count == source.table.types.count, "deferred index copies",
                             "the copy doesn't find the source's types at their ids");
            // Past its room, one type more than it holds: the index doubled, and the source's types still found.
            const uint32_t room = copy.table.index.bucket_count;
            TypeId more = last;
            for (uint64_t i = (uint64_t)copy.table.index.count; more != 0 && i <= 3ull * room; i += 1)
            {
                more = type_pointer(&copy.table, more);
            }
            type_test_expect(tally, more != 0 && copy.table.index.bucket_count == room * 2 && type_pointer(&copy.table, BUSTER_TYPE_INT) == type_pointer(&source.table, BUSTER_TYPE_INT),
                             "deferred index copies", "the copy's index didn't grow past its room");
        }
        type_test_table_close(&copy);
        type_test_table_close(&source);
    }
}

// A view's batch requests pass its hidden types over, as one at a time; a replay refuses an incomplete record; and a
// view whose index grew is taken back as it was.
static void type_deferred_fixtures(TypeTestTally* tally, Arena* arena)
{
    TypeTestTable top = { 0 };
    TypeTestTable lane = { 0 };
    bool valid = type_test_table_open(&top, 256);
    const TypeId pointer = valid ? type_pointer(&top.table, BUSTER_TYPE_INT) : 0;
    const TypeId array = valid ? type_array(&top.table, BUSTER_TYPE_CHAR, 4) : 0;
    lane.arena = arena_reserve(UINT64_C(1) << 24);
    lane.index_arena = arena_reserve(UINT64_C(1) << 24);
    const TypeCapacity extra = { .types = 4096, .parameters = 64, .tags = 64, .members = 64 };
    valid = valid && pointer != 0 && array != 0 && type_copy(&lane.table, &top.table, &lane.arena, &lane.index_arena, extra);
    type_test_expect(tally, valid, "deferred fixtures", "the copy couldn't be made");
    if (valid)
    {
        // The pointer hidden, the array not: the batch makes the pointer again, as type_derive does.
        const TypeViewMark view = type_view_mark(&lane.table);
        lane.table.hidden_begin = type_index(pointer);
        lane.table.hidden_end = type_index(pointer) + 1;
        const uint8_t kinds[2] = { BUSTER_TYPE_KIND_POINTER, BUSTER_TYPE_KIND_ARRAY };
        const uint8_t flags[2] = { 0, 0 };
        const TypeId bases[2] = { BUSTER_TYPE_INT, BUSTER_TYPE_CHAR };
        const uint64_t extents[2] = { 0, 4 };
        TypeId results[2] = { 0 };
        type_derive_batch(&lane.table, (TypeKeys){ .kinds = kinds, .flags = flags, .bases = bases, .extents = extents }, 2, results);
        type_test_expect(tally, results[0] != 0 && results[0] != pointer && type_index(results[0]) >= view.types && results[1] == array, "deferred fixtures",
                         "a view's batch didn't pass its hidden type over");
        type_test_expect(tally, type_pointer(&lane.table, BUSTER_TYPE_INT) == results[0], "deferred fixtures", "the type made again isn't found again");
        // More types, and the index grown past the copy's: the reset rebuilds it at the mark's size.
        for (uint32_t i = 0; i < 200; i += 1)
        {
            type_array(&lane.table, BUSTER_TYPE_INT, 10 + i);
        }
        const bool grew = type_index_grow(&lane.table) && lane.table.index.bucket_count == view.buckets * 2;
        type_test_expect(tally, grew, "deferred fixtures", "the copy's index didn't grow");
        type_test_expect(tally, type_view_reset(&lane.table, view) && lane.table.types.count == view.types && lane.table.index.bucket_count == view.buckets &&
                               lane.table.index.count == top.table.index.count,
                         "deferred fixtures", "a view isn't taken back to its mark");
        type_test_expect(tally, type_pointer(&lane.table, BUSTER_TYPE_INT) == pointer && type_array(&lane.table, BUSTER_TYPE_CHAR, 4) == array &&
                               lane.table.types.count == view.types,
                         "deferred fixtures", "the copy's own types aren't found after the reset");
    }
    // A record that ran out of room can't be replayed.
    TypeTestTable replayed = { 0 };
    TypeChanges small = { 0 };
    valid = type_test_table_open(&replayed, 64) && type_changes_initialize(&small, arena, 1, 0, 0);
    if (valid)
    {
        TypeTestTable recording = { 0 };
        valid = type_test_table_open(&recording, 64);
        recording.table.changes = valid ? &small : 0;
        type_pointer(&recording.table, BUSTER_TYPE_INT);
        type_pointer(&recording.table, BUSTER_TYPE_CHAR);
        recording.table.changes = 0;
        uint32_t types[8];
        uint32_t tags[2];
        uint8_t merged[2];
        TypeReplayPart part = { .changes = &small, .base = BUSTER_TYPE_BUILTIN_COUNT, .tag_base = 1, .types = types, .tags = tags, .merged = merged };
        type_test_expect(tally, small.full && !type_replay(&replayed.table, &part, 1, arena), "deferred fixtures", "a full record was replayed");
        type_test_table_close(&recording);
    }
    type_test_table_close(&replayed);
    type_test_table_close(&lane);
    type_test_table_close(&top);
}

static bool type_test_deferred(uint64_t seed, uint32_t count)
{
    TypeTestTally tally = { 0 };
    TypeDeferredCoverage coverage = { 0 };
    Arena arena = arena_reserve(UINT64_C(1) << 32);
    uint64_t random = seed * 0x9E3779B97F4A7C15ull + 113;
    type_deferred_fixtures(&tally, &arena);
    type_deferred_index_copies(&tally);
    static const uint32_t lane_counts[4] = { 1, 2, 3, 8 };
    for (uint32_t u = 0; u < count && tally.failures == 0; u += 1)
    {
        const uint64_t mark = arena.length;
        TypeDeferredUnit unit = { 0 };
        const uint32_t positions = 32 + (uint32_t)type_test_below(&random, 1500);
        const bool generated = type_deferred_generate(&unit, &random, positions, &arena);
        type_test_expect(&tally, generated, "deferred", "a unit couldn't be generated");
        for (uint32_t l = 0; generated && l < 4; l += 1)
        {
            type_deferred_unit(&tally, &coverage, &unit, lane_counts[l], &random, &arena);
        }
        arena_pop_to(&arena, mark);
    }
    arena_release(&arena);
    printf("%-34s %10" PRIu64 " runs, %" PRIu64 " bodies, %" PRIu64 " changes, %" PRIu64 " made again, %" PRIu64 " withdrawn, %" PRIu64 " merged, %" PRIu64
           " with errors\n", "deferred bodies", coverage.units, coverage.bodies, coverage.changes, coverage.remade, coverage.withdrawn, coverage.merged, coverage.errors);
    type_test_report("deferred bodies", tally);
    return tally.failures == 0;
}
