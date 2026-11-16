// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #2850's step 1, in #2524's shadow (measure-node-machine --shadow --facts; docs/one-core-path.md): a leaf's and a
// lookup's record from one load each. T (c_check_node_machine_type.c) types a leaf from the graph's leaf record, which
// the reader made from seven columns at the token and at its declaration, and a member, a dereference, a subscript and
// a call by a dozen queries of the type table, a column each. Here the same records are read from three kinds of words:
//
// - A declaration's line (32 bytes, by the line's number): its leaf's record's first half, as the record has it: its
//   type and that type's word, its flags and its object, its payload (an enumerator's value) and its standing. Made from
//   the declaration's own columns as the reader had them at a use (CheckInput.declared_types at the use's token, where
//   the reader copied the declaration's; declaration_kinds, declaration_bits, objects and enum_values at the
//   declaration; its weak attribute and the unit's weak pragmas before the use), never from the use's leaf. A
//   declaration whose line would differ from its last (a type completed since, a weak pragma since) gets a new line, as
//   a compile would write its line again: counted.
// - A type's row (NodeFactsRow, 64 bytes, by type index): its own word, where it is complete, its value type and its
//   promotion; its element (a pointer's pointee, an array's element) with the element's word and completeness; an array's
//   or a function's pointer type; a function's or a function pointer's return type, with its word, its completeness
//   and its prototype's flags and parameter count; and, as flags, what each lookup's rule asks of the type.
// - A member's slot (NodeFactsMember, 32 bytes, by a hash of the record's type and the name): the member's type as the
//   access qualifies it, that type's word, its offset and its flags. A memo of type_lookup_member's answers.
//
// The kernel's own record of a node is one line of the cache (NodeFactsRecord), T's record's fields together, where T
// has a column a field: a store a column a record costs Zen 5 about a cycle each, nine a record, and a line's stores
// don't. A leaf is then one load of its declaration's line and one store.
//
// The experiment's unit of work is a window: the unit's machine-taken expressions, their records one after another with
// their operands as distances back (node_facts_append, from T's columns as the shadow compares each expression), cut
// with their body and where the next expression would take it past the window's records. The records of the operators
// that are no lookup are given, as T made them, and so is a literal's leaf, the reader's entry. The kernel
// (node_facts_window) lists a window's identifiers' leaves and its lookups, sixteen records a step; makes the leaves; and
// makes the lookups (`->`, `.`, `*`, a subscript, a call's value and a discard) a class at a time, one loop an operator
// and a level of operands, each from its operands' records, its operand's type's row and, for a member, its slot. Each
// lookup has a short path for the shapes nearly all have, straight code over the row's flags, and behind it its rule:
// T's own rule's steps with every query of the type table replaced by a field of a row or a slot. A lookup whose guards
// the words don't cover (a wrapped type, a wide value, an element whose size the row doesn't hold under a constant index
// of an address constant) is left: it takes T's record, and is counted. T's columns are the oracle: node_facts_same
// compares every field T writes of a record, over records the kernel finds poisoned.
//
// No compile takes any of it.
#ifndef BUSTER_C_CHECK_NODE_MACHINE_FACTS_INCLUDED
#define BUSTER_C_CHECK_NODE_MACHINE_FACTS_INCLUDED

// A window's record's kind: another operator's or a lookup this step doesn't model (T's record, given), a literal's leaf
// (the reader's, given), an identifier's leaf, and a lookup.
#define BUSTER_NODE_FACTS_GIVEN 0u
#define BUSTER_NODE_FACTS_LITERAL 1u
#define BUSTER_NODE_FACTS_IDENTIFIER 2u
#define BUSTER_NODE_FACTS_LOOKUP 3u
// A discard where the kernel makes none (the lean kernels): its operand's context, and its record T's, given.
#define BUSTER_NODE_FACTS_DISCARD 4u

// The kernel's variants (docs/one-core-path.md, "Step 1's follow-up").
// - Whole: a leaf's and a lookup's record whole, T's every field. Step 1's kernel.
// - Lean: what later rules decide on. A leaf is its declaration's line alone; a lookup writes its record's first half
//   and a member's offset, and no conversion's entry (C's, from the operator, its operands' records and their types'
//   rows); a discard is not made (a context of its operand's).
// - Loads: what a lookup's load gives and nothing else: its type, that type's word and a member's offset. Its standing,
//   flags, object and payload are given, as lanes over its operands' bytes would give them, and so is whether its
//   operand is fit for the short path (`fits`, the lean kernel's own answer). No compile could run it alone: it says
//   what the load costs.
#define BUSTER_NODE_FACTS_WHOLE 0u
#define BUSTER_NODE_FACTS_LEAN 1u
#define BUSTER_NODE_FACTS_LOADS 2u
#define BUSTER_NODE_FACTS_MODES 3u

// How a record's `taken` says the kernel made it: left to T, by a short path (or a leaf's line), by its rule.
#define BUSTER_NODE_FACTS_LEFT 0u
#define BUSTER_NODE_FACTS_SHORT 1u
#define BUSTER_NODE_FACTS_RULED 2u

// What node_facts_given poisons of the records the kernel is to make: nothing, each whole, or of a lookup only what
// the loads' kernel writes (its type, its word and its member's offset; a leaf whole).
#define BUSTER_NODE_FACTS_POISON_NONE 0u
#define BUSTER_NODE_FACTS_POISON_RECORDS 1u
#define BUSTER_NODE_FACTS_POISON_LOADS 2u

// What makes a declaration's leaf the rule's, in its line's `unfit`: a register object, whose leaf's flags follow its
// use, and an enumerator whose value the line doesn't hold.
#define BUSTER_NODE_FACTS_REGISTER 0x01u
#define BUSTER_NODE_FACTS_WIDE 0x02u

// A row's `function` field: whether the type is a function or a pointer to one, whether it is the function itself (a
// designator's type), and whether its return type is void or complete somewhere in the unit; then its
// BUSTER_TYPE_FLAG_* in bits 8 to 15 and its parameter count from bit 16.
#define BUSTER_NODE_FACTS_FUNCTION 0x01u
#define BUSTER_NODE_FACTS_DESIGNATOR 0x02u
#define BUSTER_NODE_FACTS_RETURNS 0x04u

// A row's `flags`: whether check_action_is_integer takes the type; a builtin integer type's sign and, in bits 8 to 15,
// its width (node_machine_operand_width's, 0 for another type); and whether `stride` is its element's size (an element
// of a size known when the unit is compiled, under 4 GB). Then what the lookups' short paths ask of a type in one test:
// an integer, floating or pointer type with no wrapper (node_machine_plain_kind's, whose lvalue converts to its value
// type); a pointer, an array, each with no wrapper; a struct or union with no wrapper that is complete somewhere in the
// unit; a pointer to one; and a pointer or array whose element is an object type complete somewhere.
#define BUSTER_NODE_FACTS_INTEGER 0x01u
#define BUSTER_NODE_FACTS_SIGNED 0x02u
#define BUSTER_NODE_FACTS_STRIDE 0x04u
#define BUSTER_NODE_FACTS_SCALAR 0x10000u
#define BUSTER_NODE_FACTS_POINTER 0x20000u
#define BUSTER_NODE_FACTS_ARRAY 0x40000u
#define BUSTER_NODE_FACTS_RECORD 0x80000u
#define BUSTER_NODE_FACTS_ARROW 0x100000u
#define BUSTER_NODE_FACTS_OBJECTS 0x200000u

// A member slot's flags: found (and no unknown member), a bit-field, of an incomplete array type, and in use (every
// slot that holds an answer, a member found or not).
#define BUSTER_NODE_FACTS_MEMBER_FOUND 0x01u
#define BUSTER_NODE_FACTS_MEMBER_BIT_FIELD 0x02u
#define BUSTER_NODE_FACTS_MEMBER_FLEXIBLE 0x04u
#define BUSTER_NODE_FACTS_MEMBER_USED 0x08u

// The members' slots: a power of two of them, at least four a member of the unit's type table, between these.
#define BUSTER_NODE_FACTS_MEMBER_LEAST_BITS 10u
#define BUSTER_NODE_FACTS_MEMBER_MOST_BITS 20u

// The lookups' operators, for the counts: `->`, `.`, `*`, a subscript, a call and a discard.
#define BUSTER_NODE_FACTS_LOOKUPS 6u
static const char* const node_facts_lookup_names[BUSTER_NODE_FACTS_LOOKUPS] = { "->", ".", "*", "[]", "call", "discard" };

// The most records a window has: the machine's most for an expression, so that a window holds at least one. And where a
// window is cut unless the command says otherwise: where the next expression would take it past this many records,
// whose lines are half the first-level cache.
#define BUSTER_NODE_FACTS_WINDOW BUSTER_NODE_MACHINE_MOST
#define BUSTER_NODE_FACTS_CUT 384u

// The lookups' classes. A record's level is 0 where it is no lookup, and else one more than the highest of its
// operands': a lookup reads only records of lower levels, so the lookups of one level may be made in any order, and
// those of one operator together. A lookup's class is its level and its operator's place, BUSTER_NODE_FACTS_LEVELS
// levels each its own class an operator, and one class for the deeper. It comes with the record, as the operator and
// the operands' distances do: whatever emits the postorder has each record's operands at hand (node_facts_append here).
#define BUSTER_NODE_FACTS_LEVELS 7u
#define BUSTER_NODE_FACTS_DEEP 63u
#define BUSTER_NODE_FACTS_NO_CLASS 0xffu

// The kernel's record of a node: T's record's fields (NodeMachineColumns' value_types, words, value_flags, objects,
// payloads, standings, exponents, converted, conversion_flags and member_offsets) in one line of the cache. T's columns
// take a store a field, each to its own line, and Zen 5 lands about one such store a cycle: nine cycles a record
// (stores.c in the measurement record). One line's stores land two a cycle, and a leaf's line is one vector's store; an
// operand's type, word, flags and standing are one line's loads. `taken`: whether a rule of the kernel's made the record
// (the experiment's count); `unfit`: zero in a record, and in a declaration's line whether the leaf is the rule's.
typedef struct NodeFactsRecord NodeFactsRecord;
struct NodeFactsRecord
{
    TypeId type;
    uint32_t word;
    uint32_t flags;
    uint32_t object;
    uint64_t payload;
    uint8_t standing;
    uint8_t taken;
    uint16_t exponent;
    uint32_t unfit;
    TypeId converted[2];
    uint32_t conversion_flags[2];
    uint64_t member_offset;
    uint64_t spare;
};

_Static_assert(sizeof(NodeFactsRecord) == 64 && offsetof(NodeFactsRecord, payload) == 16 && offsetof(NodeFactsRecord, standing) == 24 &&
               offsetof(NodeFactsRecord, converted) == 32 && offsetof(NodeFactsRecord, member_offset) == 48, "a record is one line of the cache, in quadwords");

typedef struct NodeFactsRow NodeFactsRow;
struct NodeFactsRow
{
    uint32_t word;
    uint32_t complete_at;
    TypeId value;
    TypeId promoted;
    TypeId element;
    uint32_t element_word;
    uint32_t element_complete_at;
    TypeId decayed[2];
    TypeId returned;
    uint32_t returned_word;
    uint32_t returned_complete_at;
    uint32_t function;
    uint32_t flags;
    uint32_t stride;
    // Where a call of the type has its value's type: 0 for void, else returned_complete_at.
    uint32_t returned_ready_at;
};

_Static_assert(sizeof(NodeFactsRow) == 64, "a type's row is one line of the cache");

typedef struct NodeFactsMember NodeFactsMember;
struct NodeFactsMember
{
    uint64_t key;
    TypeId type;
    uint32_t word;
    uint64_t offset;
    uint32_t flags;
    // What an access of the member adds to its value's flags: BUSTER_CHECK_CONST_READ_RESTRICTED for a flexible array.
    uint32_t result_flags;
};

_Static_assert(sizeof(NodeFactsMember) == 32, "a member's slot is half a line of the cache");

// An operand as its operator reads it (node_facts_operand): NodeMachineOperand's fields, and its type's kind after the
// conversion.
typedef struct NodeFactsOperand NodeFactsOperand;
struct NodeFactsOperand
{
    TypeId type;
    uint32_t flags;
    uint64_t integer;
    uint8_t category;
    uint8_t constant_kind;
    uint8_t kind;
};

typedef struct NodeFacts NodeFacts;
struct NodeFacts
{
    NodeMachine* machine;
    // The words.
    NodeFactsRow* rows;
    uint64_t* lines;
    uint32_t* numbers;
    NodeFactsMember* members;
    NodeFactsMember no_member;
    uint32_t row_count;
    uint32_t line_count;
    uint32_t line_capacity;
    uint32_t member_count;
    uint32_t member_bits;
    uint32_t void_word;
    // The windows' records: each record's operator, kind, a lookup's class and every record's level (below), whether a lookup's operand is fit for its short path (the
    // loads' kernel's given, the lean kernel's answer), operands as distances back (0 for none), input (a leaf's
    // declaration's number, a member's name, a cast's type, a call's first argument in `arguments`), a call's argument
    // count, its node's source position and its node; a call's arguments as distances back; and each window's first
    // record, `window_count` + 1 of them.
    uint8_t* ops;
    uint8_t* kinds;
    uint8_t* classes;
    uint8_t* levels;
    uint8_t* fits;
    uint16_t* left;
    uint16_t* right;
    uint32_t* inputs;
    uint16_t* counts;
    uint32_t* positions;
    uint32_t* nodes;
    uint16_t* arguments;
    uint32_t* windows;
    uint32_t record_count;
    uint32_t record_capacity;
    uint32_t argument_count;
    uint32_t window_count;
    uint32_t window_body;
    uint32_t widest;
    // T's record of each (the oracle's, and what is given), by its place in the unit's windows; and the kernel's records
    // of the window at hand (NodeFactsRecord), by their places in the window.
    TypeId* expected_types;
    uint32_t* expected_flags;
    uint32_t* expected_objects;
    uint64_t* expected_payloads;
    uint8_t* expected_standings;
    uint16_t* expected_exponents;
    uint32_t* expected_words;
    TypeId* expected_converted;
    uint32_t* expected_conversion_flags;
    uint64_t* expected_member_offsets;
    NodeFactsRecord* records;
    // The kernel's lists for the window at hand: its identifiers' leaves and its lookups by their places; each lookup's
    // class beside its place; the lookups of the class at hand; and those of them no short path took. Each has a
    // vector's entries past a window's most, which the listing stores and loads whole.
    uint32_t* leaf_list;
    uint32_t* lookup_list;
    uint8_t* keys;
    uint32_t* batch;
    uint32_t* rare;
    // The counts: the records by kind, the identifiers' leaves by what they name, the lookups by operator (each one's and
    // those the kernel left), the given records by operator, and the members' slots filled.
    uint64_t record_total;
    uint64_t literals;
    uint64_t identifiers;
    uint64_t enumerators;
    uint64_t functions;
    uint64_t static_objects;
    uint64_t automatic_objects;
    uint64_t left_identifiers;
    uint64_t lookups[BUSTER_NODE_FACTS_LOOKUPS];
    uint64_t left_lookups[BUSTER_NODE_FACTS_LOOKUPS];
    uint64_t given[CHECK_OP_COUNT];
    uint64_t member_fills;
    uint64_t line_versions;
    uint64_t differences;
    // The kernel's TSC ticks in its two loops, summed over the windows it ran; and the words' making: the rows' and the
    // declarations' lines'.
    uint64_t leaf_ticks;
    uint64_t lookup_ticks;
    uint64_t row_ticks;
    uint64_t line_ticks;
    // The kernel's lists' lengths for the window it last ran.
    uint32_t leaf_count;
    uint32_t lookup_count;
    // Where a window is cut, in records, and whether it holds expressions of several bodies (else a body's alone).
    uint32_t cut;
    bool packed;
    // Whether the windows are a lean kernel's: their discards are no lookups.
    bool lean;
    bool room;
};

// A lookup's place in the counts, or BUSTER_NODE_FACTS_LOOKUPS for an operator that is none.
static inline uint32_t node_facts_lookup(CheckOp op)
{
    return op == CHECK_OP_ARROW ? 0 : op == CHECK_OP_MEMBER ? 1 : op == CHECK_OP_DEREFERENCE ? 2 : op == CHECK_OP_SUBSCRIPT ? 3 : op == CHECK_OP_CALL ? 4 :
           op == CHECK_OP_CAST ? 5 : BUSTER_NODE_FACTS_LOOKUPS;
}

// Type `type`'s row's fields, from the type table and the machine's words: what every lookup of the unit asks of it.
static void node_facts_row(const NodeMachine* machine, uint32_t index, NodeFactsRow* row)
{
    const TypeTable* types = machine->types;
    const TypeId type = (TypeId)index << BUSTER_TYPE_INDEX_SHIFT;
    const uint32_t word = machine->type_words[index];
    const bool valid = (word & BUSTER_NODE_MACHINE_WORD_VALID) != 0;
    const bool plain = (word & BUSTER_NODE_MACHINE_WORD_PLAIN) != 0;
    const uint8_t kind = (uint8_t)(word >> 16);
    *row = (NodeFactsRow){ .word = word, .complete_at = machine->type_complete_at[index], .returned_ready_at = UINT32_MAX };
    if (valid)
    {
        row->value = type_conversion_operand(types, type);
        row->promoted = type_promote(types, type);
        const uint32_t width = index < BUSTER_NODE_MACHINE_KINDS ? node_machine_integer_widths[index] : 0;
        const bool record = plain && (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION) && row->complete_at != UINT32_MAX;
        row->flags = (check_action_is_integer(types, type) ? BUSTER_NODE_FACTS_INTEGER : 0) | width << 8 |
                     (width != 0 && node_machine_integer_sign(machine, index) ? BUSTER_NODE_FACTS_SIGNED : 0) |
                     (plain && node_machine_plain_kind(kind) ? BUSTER_NODE_FACTS_SCALAR : 0) | (plain && kind == BUSTER_TYPE_KIND_POINTER ? BUSTER_NODE_FACTS_POINTER : 0) |
                     (plain && type_kind_is_array(kind) ? BUSTER_NODE_FACTS_ARRAY : 0) | (record ? BUSTER_NODE_FACTS_RECORD : 0);
    }
    TypeId function = valid && plain && kind == BUSTER_TYPE_KIND_FUNCTION ? type : 0;
    if (valid && plain && (kind == BUSTER_TYPE_KIND_POINTER || type_kind_is_array(kind)))
    {
        const TypeId element = type_element(types, type);
        const uint32_t element_index = type_index(element);
        const uint32_t element_word = node_machine_word(machine, element);
        row->element = element;
        row->element_word = element_word;
        row->element_complete_at = element_index < machine->type_count ? machine->type_complete_at[element_index] : UINT32_MAX;
        // check_action_pointer_stride's, where a constant index's address may be folded with it.
        const uint8_t element_kind = (uint8_t)(element_word >> 16);
        const bool byte = element_kind == BUSTER_TYPE_KIND_VOID || element_kind == BUSTER_TYPE_KIND_FUNCTION;
        const bool sized = (element_word & BUSTER_NODE_MACHINE_WORD_VALID) != 0 && (byte || (type_is_complete(types, element) && !check_action_runtime_size(types, element)));
        const uint64_t size = sized ? (byte ? 1 : type_size(types, element)) : UINT64_MAX;
        row->stride = size <= UINT32_MAX ? (uint32_t)size : 0;
        row->flags |= size <= UINT32_MAX ? BUSTER_NODE_FACTS_STRIDE : 0;
        const bool complete = row->element_complete_at != UINT32_MAX;
        const bool pointed_record = (element_word & BUSTER_NODE_MACHINE_WORD_PLAIN) != 0 && (element_kind == BUSTER_TYPE_KIND_STRUCT || element_kind == BUSTER_TYPE_KIND_UNION);
        row->flags |= kind == BUSTER_TYPE_KIND_POINTER && pointed_record && complete ? BUSTER_NODE_FACTS_ARROW : 0;
        row->flags |= !byte && complete ? BUSTER_NODE_FACTS_OBJECTS : 0;
        const bool pointed = kind == BUSTER_TYPE_KIND_POINTER && (element_word & (BUSTER_NODE_MACHINE_WORD_VALID | BUSTER_NODE_MACHINE_WORD_PLAIN)) ==
                                                                  (BUSTER_NODE_MACHINE_WORD_VALID | BUSTER_NODE_MACHINE_WORD_PLAIN) &&
                             (uint8_t)(element_word >> 16) == BUSTER_TYPE_KIND_FUNCTION;
        function = pointed ? type_unqualified(element) : 0;
    }
    if (function != 0)
    {
        const uint32_t function_index = type_index(function);
        const TypeId returned = type_return(types, function);
        const uint32_t returned_index = type_index(returned);
        uint32_t parameters = 0;
        (void)type_parameters(types, function, &parameters);
        row->returned = returned;
        row->returned_word = node_machine_word(machine, returned);
        row->returned_complete_at = returned_index < machine->type_count ? machine->type_complete_at[returned_index] : UINT32_MAX;
        row->returned_ready_at = (uint8_t)(row->returned_word >> 16) == BUSTER_TYPE_KIND_VOID ? 0 : row->returned_complete_at;
        row->function = BUSTER_NODE_FACTS_FUNCTION | (kind == BUSTER_TYPE_KIND_FUNCTION ? BUSTER_NODE_FACTS_DESIGNATOR : 0) |
                        (row->returned_ready_at != UINT32_MAX ? BUSTER_NODE_FACTS_RETURNS : 0) | (uint32_t)types->types.flags[function_index] << 8 |
                        (parameters < 0xffffu ? parameters : 0xffffu) << 16;
    }
}

// The experiment's words and windows for the machine's unit, from `arena`, for at most `capacity` records, a window cut
// at `cut` records (0 for BUSTER_NODE_FACTS_CUT) and holding several bodies' expressions with `packed`, its discards no
// lookups with `lean`: whether there was room.
static bool node_facts_begin(NodeFacts* facts, NodeMachine* machine, uint32_t capacity, uint32_t cut, bool packed, bool lean, Arena* arena)
{
    const CheckInput* input = machine->program->input;
    const uint64_t records = (uint64_t)capacity + 64;
    // The members' slots: four a member of the unit's type table, so that the accesses' record types and names, each one
    // slot, leave it under half full but in a unit that reaches most members through several types.
    uint32_t member_bits = BUSTER_NODE_FACTS_MEMBER_LEAST_BITS;
    while (member_bits < BUSTER_NODE_FACTS_MEMBER_MOST_BITS && (UINT64_C(1) << member_bits) < 4 * (uint64_t)machine->types->members.count)
    {
        member_bits += 1;
    }
    *facts = (NodeFacts){
        .machine = machine,
        .row_count = machine->type_count,
        .line_capacity = input->token_count / 2 + 64,
        .member_bits = member_bits,
        .record_capacity = capacity,
        .window_body = UINT32_MAX,
        .cut = cut != 0 && cut <= BUSTER_NODE_FACTS_WINDOW ? cut : BUSTER_NODE_FACTS_CUT,
        .packed = packed,
        .lean = lean,
    };
    facts->rows = arena_push_array(arena, (uint64_t)facts->row_count + 1, sizeof(NodeFactsRow), 64);
    facts->lines = arena_push_array(arena, 4 * (uint64_t)facts->line_capacity, sizeof(uint64_t), 64);
    facts->numbers = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)input->token_count + 1);
    facts->members = arena_push_array(arena, UINT64_C(1) << member_bits, sizeof(NodeFactsMember), 64);
    facts->ops = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, records);
    facts->kinds = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, records);
    facts->classes = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, records);
    facts->levels = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, records);
    facts->fits = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, records);
    facts->left = BUSTER_ARENA_PUSH_ARRAY(arena, uint16_t, records);
    facts->right = BUSTER_ARENA_PUSH_ARRAY(arena, uint16_t, records);
    facts->inputs = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, records);
    facts->counts = BUSTER_ARENA_PUSH_ARRAY(arena, uint16_t, records);
    facts->positions = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, records);
    facts->nodes = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, records);
    facts->arguments = BUSTER_ARENA_PUSH_ARRAY(arena, uint16_t, records);
    facts->windows = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, records);
    facts->expected_types = BUSTER_ARENA_PUSH_ARRAY(arena, TypeId, records);
    facts->expected_flags = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, records);
    facts->expected_objects = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, records);
    facts->expected_payloads = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, records);
    facts->expected_standings = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, records);
    facts->expected_exponents = BUSTER_ARENA_PUSH_ARRAY(arena, uint16_t, records);
    facts->expected_words = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, records);
    facts->expected_converted = BUSTER_ARENA_PUSH_ARRAY(arena, TypeId, 2 * records);
    facts->expected_conversion_flags = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, 2 * records);
    facts->expected_member_offsets = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, records);
    facts->records = arena_push_array(arena, BUSTER_NODE_FACTS_WINDOW, sizeof(NodeFactsRecord), 64);
    facts->leaf_list = arena_push_array(arena, BUSTER_NODE_FACTS_WINDOW + 16, sizeof(uint32_t), 64);
    facts->lookup_list = arena_push_array(arena, BUSTER_NODE_FACTS_WINDOW + 16, sizeof(uint32_t), 64);
    facts->keys = arena_push_array(arena, BUSTER_NODE_FACTS_WINDOW + 64, sizeof(uint8_t), 64);
    facts->batch = arena_push_array(arena, BUSTER_NODE_FACTS_WINDOW + 16, sizeof(uint32_t), 64);
    facts->rare = arena_push_array(arena, BUSTER_NODE_FACTS_WINDOW + 16, sizeof(uint32_t), 64);
    facts->room = facts->rows != NULL && facts->lines != NULL && facts->numbers != NULL && facts->members != NULL && facts->ops != NULL && facts->kinds != NULL &&
                  facts->left != NULL && facts->right != NULL && facts->inputs != NULL && facts->counts != NULL && facts->positions != NULL && facts->nodes != NULL &&
                  facts->arguments != NULL && facts->windows != NULL && facts->expected_types != NULL && facts->expected_flags != NULL &&
                  facts->expected_objects != NULL && facts->expected_payloads != NULL && facts->expected_standings != NULL && facts->expected_exponents != NULL &&
                  facts->expected_words != NULL && facts->expected_converted != NULL && facts->expected_conversion_flags != NULL &&
                  facts->expected_member_offsets != NULL && facts->records != NULL && facts->classes != NULL && facts->levels != NULL && facts->fits != NULL && facts->leaf_list != NULL &&
                  facts->lookup_list != NULL && facts->keys != NULL && facts->batch != NULL && facts->rare != NULL;
    if (facts->room)
    {
        memset(facts->numbers, 0, ((uint64_t)input->token_count + 1) * sizeof(uint32_t));
        memset(facts->members, 0, (UINT64_C(1) << member_bits) * sizeof(NodeFactsMember));
        memset(facts->rows + facts->row_count, 0, sizeof(NodeFactsRow));
        const uint64_t t0 = __rdtsc();
        for (uint32_t index = 0; index < facts->row_count; index += 1)
        {
            node_facts_row(machine, index, facts->rows + index);
        }
        facts->row_ticks = __rdtsc() - t0;
        facts->void_word = node_machine_word(machine, BUSTER_TYPE_VOID);
        facts->windows[0] = 0;
    }
    return facts->room;
}

// A miss of the members' slots at slot `at`: the type table's own lookup of member `name` of record type `record`, once a
// record type and name, kept there; with the table half full, no answer, and the access is the rule's.
static __attribute__((noinline, cold)) const NodeFactsMember* node_facts_member_fill(NodeFacts* facts, uint32_t at, TypeId record, uint32_t name)
{
    const NodeMachine* machine = facts->machine;
    const TypeTable* types = machine->types;
    const bool room = facts->member_count < (1u << facts->member_bits) / 2;
    uint64_t offset = 0;
    uint32_t qualifiers = 0;
    const uint32_t member = room ? type_lookup_member(types, record, name, &offset, &qualifiers) : 0;
    const bool found = member != 0 && member != BUSTER_TYPE_MEMBER_UNKNOWN;
    const TypeId type = found ? type_qualify(types, types->members.type[member], qualifiers) : 0;
    const bool flexible = found && type_kind(types, type_strip_aligned(types, type)) == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY;
    NodeFactsMember* slot = room ? facts->members + at : &facts->no_member;
    *slot = (NodeFactsMember){
        .key = (uint64_t)record << 32 | name,
        .type = type,
        .word = found ? node_machine_word(machine, type) : 0,
        .offset = offset,
        .flags = BUSTER_NODE_FACTS_MEMBER_USED | (found ? BUSTER_NODE_FACTS_MEMBER_FOUND : 0) |
                 (found && (types->members.flags[member] & BUSTER_TYPE_MEMBER_BIT_FIELD) != 0 ? BUSTER_NODE_FACTS_MEMBER_BIT_FIELD : 0) |
                 (flexible ? BUSTER_NODE_FACTS_MEMBER_FLEXIBLE : 0),
        .result_flags = flexible ? BUSTER_CHECK_CONST_READ_RESTRICTED : 0,
    };
    facts->member_count += room;
    facts->member_fills += room;
    return slot;
}

// The line of declaration `declaration` as the reader had it at the use `use`, its number (from 1), or 0 where the lines
// are full: the declaration's last line where that is still its line, and else a new one.
static uint32_t node_facts_declaration(NodeFacts* facts, uint32_t declaration, uint32_t use)
{
    const NodeMachine* machine = facts->machine;
    const CheckProgram* program = machine->program;
    const CheckInput* input = program->input;
    const uint64_t t0 = __rdtsc();
    const TypeId type = input->declared_types[use];
    const bool enumerator = input->declaration_kinds[declaration] == C_BINDING_ENUMERATOR;
    const uint32_t object = input->objects[declaration];
    // An enumerator's value: its row of the unit's enumerators, which are in source order.
    uint32_t low = 0;
    uint32_t high = enumerator ? input->enum_value_count : 0;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        if (input->enum_values[middle].token < declaration)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    const bool valued = enumerator && low < input->enum_value_count && input->enum_values[low].token == declaration;
    const unsigned __int128 value = valued ? input->enum_values[low].value : 0;
    const bool wide = enumerator && (!valued || (uint64_t)(value >> 64) != 0 || object != 0);
    // An address that may be null: the declaration's weak attribute, or a weak pragma of its name before the use
    // (check_input_address_is_weak's, whose weak names the reader doesn't keep).
    const IdentId name = declaration < program->parser->tokens.count && program->parser->tokens.names != NULL ? program->parser->tokens.names[declaration] : 0;
    const bool weak = check_input_attribute_of(check_input_attributes_at(input, declaration), CHECK_ATTRIBUTE_WEAK) != NULL || check_input_pragma_weak(input, name, use);
    const uint8_t bits = input->declaration_bits[declaration];
    const CheckCategory category = enumerator ? CHECK_RVALUE : type_kind(machine->types, type) == BUSTER_TYPE_KIND_FUNCTION ? CHECK_FUNCTION : CHECK_LVALUE;
    // The line: the leaf's record's first half, a NodeFactsRecord's first four quadwords.
    const NodeFactsRecord made = {
        .type = type,
        .word = node_machine_word(machine, type),
        .flags = (enumerator ? BUSTER_CHECK_ICE : 0) |
                 ((bits & BUSTER_CHECK_BITS_STATIC) != 0 && (bits & BUSTER_CHECK_BITS_THREAD_LOCAL) == 0 ? BUSTER_CHECK_STATIC_OBJECT : 0) |
                 (object != 0 && !enumerator && !weak ? BUSTER_CHECK_NONNULL_ADDRESS : 0),
        .object = enumerator ? 0 : object,
        .payload = enumerator ? (uint64_t)value : 0,
        .standing = BUSTER_NODE_MACHINE_STANDING(category, enumerator ? CHECK_CONSTANT_INTEGER : CHECK_CONSTANT_ADDRESS),
        .taken = 1,
        .unfit = ((bits & BUSTER_CHECK_BITS_REGISTER) != 0 ? BUSTER_NODE_FACTS_REGISTER : 0) | (wide ? BUSTER_NODE_FACTS_WIDE : 0),
    };
    uint64_t line[4];
    memcpy(line, &made, sizeof(line));
    uint32_t number = facts->numbers[declaration];
    const bool same = number != 0 && memcmp(facts->lines + 4 * (uint64_t)number, line, sizeof(line)) == 0;
    if (!same)
    {
        facts->line_versions += number != 0;
        number = facts->line_count + 1 < facts->line_capacity ? facts->line_count + 1 : 0;
        if (number != 0)
        {
            facts->line_count = number;
            memcpy(facts->lines + 4 * (uint64_t)number, line, sizeof(line));
            facts->numbers[declaration] = number;
        }
        facts->line_ticks += __rdtsc() - t0;
    }
    return number;
}

// An array's or a function's row's pointer types, from the machine's own memo (node_machine_pointer), which T fills as it
// converts one: type_pointer's answers, which a row holds for its type unqualified and const.
static void node_facts_decays(NodeFacts* facts, TypeId type)
{
    const NodeMachine* machine = facts->machine;
    const uint32_t index = type_index(type);
    NodeFactsRow* row = index < facts->row_count ? facts->rows + index : NULL;
    const uint8_t kind = row != NULL ? (uint8_t)(row->word >> 16) : BUSTER_TYPE_KIND_NONE;
    if (row != NULL && (row->word & BUSTER_NODE_MACHINE_WORD_PLAIN) != 0 && (type_kind_is_array(kind) || kind == BUSTER_TYPE_KIND_FUNCTION))
    {
        for (uint32_t qualifiers = 0; qualifiers <= BUSTER_TYPE_CONST; qualifiers += 1)
        {
            // An array's is its element's pointer, the element taking the array's qualifiers; a function's is its own.
            const TypeId pointee = kind == BUSTER_TYPE_KIND_FUNCTION ? type_unqualified(type) : row->element | qualifiers;
            const uint32_t pointee_index = type_index(pointee);
            const uint32_t pointee_qualifiers = type_qualifiers(pointee);
            if (pointee_index < machine->type_count && pointee_qualifiers <= BUSTER_TYPE_CONST)
            {
                const TypeId pointer = (pointee_qualifiers == 0 ? machine->pointers : machine->const_pointers)[pointee_index];
                row->decayed[qualifiers] = pointer != 0 ? pointer : row->decayed[qualifiers];
            }
        }
    }
}

// The machine's current expression, which T has typed, appended to the window at hand: its records' inputs and T's
// record of each. A new window starts where the expression would take the window past its cut and, unless the windows
// are packed, with a new body (`body`).
static void node_facts_append(NodeFacts* facts, uint32_t body)
{
    const NodeMachine* machine = facts->machine;
    const NodeMachineColumns* columns = machine->columns;
    const CheckProgram* program = machine->program;
    const CheckInput* input = program->input;
    const CheckGraph* graph = machine->graph;
    const uint32_t count = machine->count;
    const uint32_t base = facts->record_count;
    if (facts->room && count <= BUSTER_NODE_FACTS_WINDOW && (uint64_t)base + count <= facts->record_capacity &&
        (uint64_t)facts->argument_count + machine->argument_total <= facts->record_capacity)
    {
        const bool first = facts->window_body == UINT32_MAX;
        const bool full = base - facts->windows[facts->window_count] + count > facts->cut;
        if (first || full || (!facts->packed && body != facts->window_body))
        {
            facts->window_count += !first;
            facts->windows[facts->window_count] = base;
        }
        facts->window_body = body;
        for (uint32_t r = 0; r < count; r += 1)
        {
            const uint32_t w = base + r;
            const uint32_t node = node_machine_node(machine, r);
            const CheckOp op = graph->ops[node];
            const uint32_t a = columns->left[r];
            const uint32_t b = columns->right[r];
            const uint8_t standing = columns->standings[r];
            uint8_t kind = BUSTER_NODE_FACTS_GIVEN;
            uint32_t entry = 0;
            uint16_t arguments = 0;
            if (op == CHECK_OP_VALUE)
            {
                const uint32_t token = graph->tokens[node];
                const bool named = token < input->token_count && token < program->parser->tokens.count && program->parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER &&
                                   input->declared_types[token] != 0 && input->declarations[token] < input->token_count;
                const uint32_t number = named && (standing & BUSTER_NODE_MACHINE_WIDE) == 0 ? node_facts_declaration(facts, input->declarations[token], token) : 0;
                kind = number != 0 ? BUSTER_NODE_FACTS_IDENTIFIER : BUSTER_NODE_FACTS_LITERAL;
                entry = number;
                facts->literals += number == 0;
                facts->identifiers += number != 0;
                if (number != 0)
                {
                    const uint32_t flags = columns->value_flags[r];
                    const CheckCategory category = (CheckCategory)(standing & 0x03u);
                    facts->enumerators += (flags & BUSTER_CHECK_ICE) != 0;
                    facts->functions += (flags & BUSTER_CHECK_ICE) == 0 && category == CHECK_FUNCTION;
                    facts->static_objects += (flags & BUSTER_CHECK_ICE) == 0 && category != CHECK_FUNCTION && (flags & BUSTER_CHECK_STATIC_OBJECT) != 0;
                    facts->automatic_objects += (flags & BUSTER_CHECK_ICE) == 0 && category != CHECK_FUNCTION && (flags & BUSTER_CHECK_STATIC_OBJECT) == 0;
                }
            }
            else
            {
                const uint32_t lookup = node_facts_lookup(op);
                const bool discard = op == CHECK_OP_CAST && graph->types != NULL && graph->types[node] == BUSTER_TYPE_VOID;
                const bool modelled = lookup < BUSTER_NODE_FACTS_LOOKUPS && (op != CHECK_OP_CAST || discard) && a < r && (b == UINT32_MAX || b < r);
                kind = !modelled ? BUSTER_NODE_FACTS_GIVEN : discard && facts->lean ? BUSTER_NODE_FACTS_DISCARD : BUSTER_NODE_FACTS_LOOKUP;
                entry = op == CHECK_OP_ARROW || op == CHECK_OP_MEMBER ? graph->members[node] : op == CHECK_OP_CAST && graph->types != NULL ? graph->types[node] : 0;
                facts->lookups[modelled ? lookup : 0] += modelled;
                facts->given[op] += !modelled;
                if (modelled && op == CHECK_OP_CALL)
                {
                    const uint32_t first = columns->argument_first[r];
                    const uint32_t argument_count = columns->argument_counts[r];
                    bool before = argument_count <= UINT16_MAX;
                    for (uint32_t i = 0; before && i < argument_count; i += 1)
                    {
                        before = machine->arguments[first + i] < r;
                    }
                    entry = facts->argument_count;
                    arguments = before ? (uint16_t)argument_count : 0;
                    kind = before ? BUSTER_NODE_FACTS_LOOKUP : BUSTER_NODE_FACTS_GIVEN;
                    facts->lookups[lookup] -= !before;
                    facts->given[op] += !before;
                    for (uint32_t i = 0; before && i < argument_count; i += 1)
                    {
                        facts->arguments[facts->argument_count + i] = (uint16_t)(r - machine->arguments[first + i]);
                    }
                    facts->argument_count += before ? argument_count : 0;
                }
                // The pointer types T interned for its operands' arrays and functions, into their rows.
                if (a < r)
                {
                    node_facts_decays(facts, columns->value_types[a]);
                }
                if (b < r)
                {
                    node_facts_decays(facts, columns->value_types[b]);
                }
            }
            // Its level and, a lookup's, its class: one more than the highest of its operands' levels (a call's
            // arguments among them), which are before it.
            uint32_t level = 0;
            if (kind == BUSTER_NODE_FACTS_LOOKUP)
            {
                level = facts->levels[w - (r - a)];
                level = b < r && facts->levels[w - (r - b)] > level ? facts->levels[w - (r - b)] : level;
                for (uint32_t i = 0; i < arguments; i += 1)
                {
                    const uint32_t argument = facts->levels[w - facts->arguments[entry + i]];
                    level = argument > level ? argument : level;
                }
            }
            const bool deep = level >= BUSTER_NODE_FACTS_LEVELS;
            facts->levels[w] = (uint8_t)(kind != BUSTER_NODE_FACTS_LOOKUP ? 0 : deep ? BUSTER_NODE_FACTS_LEVELS : level + 1);
            facts->classes[w] = (uint8_t)(kind != BUSTER_NODE_FACTS_LOOKUP ? BUSTER_NODE_FACTS_NO_CLASS : deep ? BUSTER_NODE_FACTS_DEEP : level * 8 + node_facts_lookup(op));
            facts->ops[w] = (uint8_t)op;
            facts->kinds[w] = kind;
            facts->fits[w] = 0;
            facts->left[w] = a < r ? (uint16_t)(r - a) : 0;
            facts->right[w] = b < r ? (uint16_t)(r - b) : 0;
            facts->inputs[w] = entry;
            facts->counts[w] = arguments;
            facts->positions[w] = graph->source_positions != NULL ? graph->source_positions[node] : BUSTER_CHECK_NONE;
            facts->nodes[w] = node;
            facts->expected_types[w] = columns->value_types[r];
            facts->expected_flags[w] = columns->value_flags[r];
            facts->expected_objects[w] = columns->objects[r];
            facts->expected_payloads[w] = columns->payloads[r];
            facts->expected_standings[w] = standing;
            facts->expected_exponents[w] = columns->exponents[r];
            facts->expected_words[w] = columns->words[r];
            facts->expected_converted[2 * (uint64_t)w] = columns->converted[2 * r];
            facts->expected_converted[2 * (uint64_t)w + 1] = columns->converted[2 * r + 1];
            facts->expected_conversion_flags[2 * (uint64_t)w] = columns->conversion_flags[2 * r];
            facts->expected_conversion_flags[2 * (uint64_t)w + 1] = columns->conversion_flags[2 * r + 1];
            facts->expected_member_offsets[w] = columns->member_offsets[r];
        }
        facts->record_total += count;
        facts->record_count = base + count;
        facts->windows[facts->window_count + 1] = facts->record_count;
        const uint32_t width = facts->record_count - facts->windows[facts->window_count];
        facts->widest = width > facts->widest ? width : facts->widest;
    }
    else
    {
        facts->room = false;
    }
}

// T's record of the unit's record `at`, as a line.
static inline NodeFactsRecord node_facts_expected(const NodeFacts* facts, uint64_t at)
{
    return (NodeFactsRecord){
        .type = facts->expected_types[at],
        .word = facts->expected_words[at],
        .flags = facts->expected_flags[at],
        .object = facts->expected_objects[at],
        .payload = facts->expected_payloads[at],
        .standing = facts->expected_standings[at],
        .taken = 1,
        .exponent = facts->expected_exponents[at],
        .converted = { facts->expected_converted[2 * at], facts->expected_converted[2 * at + 1] },
        .conversion_flags = { facts->expected_conversion_flags[2 * at], facts->expected_conversion_flags[2 * at + 1] },
        .member_offset = facts->expected_member_offsets[at],
    };
}

// Window `window`'s records as the kernel finds them: T's, the given ones'. What the kernel is to make is poisoned as
// `poison` says, with bytes no record has, so that what it doesn't write shows in the comparison: nothing (T's
// records, which the kernel writes over), an identifier's leaf and a lookup whole, or a leaf whole and of a lookup only
// what the loads' kernel writes.
static void node_facts_given(NodeFacts* facts, uint32_t window, uint32_t poison)
{
    NodeFactsRecord* records = facts->records;
    const uint32_t first = facts->windows[window];
    const uint32_t count = facts->windows[window + 1] - first;
    for (uint32_t i = 0; i < count; i += 1)
    {
        const uint8_t kind = facts->kinds[first + i];
        const bool made = kind == BUSTER_NODE_FACTS_IDENTIFIER || kind == BUSTER_NODE_FACTS_LOOKUP;
        if (made && (poison == BUSTER_NODE_FACTS_POISON_RECORDS || (poison == BUSTER_NODE_FACTS_POISON_LOADS && kind == BUSTER_NODE_FACTS_IDENTIFIER)))
        {
            memset(records + i, 0xa5, sizeof(NodeFactsRecord));
        }
        else
        {
            records[i] = node_facts_expected(facts, (uint64_t)first + i);
            if (made && poison == BUSTER_NODE_FACTS_POISON_LOADS)
            {
                records[i].type = 0xa5a5a5a5u;
                records[i].word = 0xa5a5a5a5u;
                records[i].member_offset = UINT64_C(0xa5a5a5a5a5a5a5a5);
            }
        }
    }
}

// What the kernel reads of a window beside its records: the window's records' inputs, from the window's first; the
// unit's call arguments; and the words. A local of node_facts_window's, so that the kernel's stores change none of it.
typedef struct NodeFactsView NodeFactsView;
struct NodeFactsView
{
    const uint32_t* inputs;
    const uint16_t* counts;
    const uint32_t* positions;
    const uint16_t* arguments;
    const uint64_t* lines;
    const NodeFactsRow* rows;
    NodeFactsMember* members;
    uint32_t row_count;
    uint32_t member_bits;
    uint32_t void_word;
};

// A quadword of record `record`, the one at byte `offset`: a record's fields are written two a store, to their one line.
static inline __attribute__((always_inline)) void node_facts_store(NodeFactsRecord* record, uint32_t offset, uint64_t value)
{
    memcpy((uint8_t*)record + offset, &value, sizeof(value));
}

// Record `w`'s value, node_machine_record's columns, with its type's word: its first four quadwords, the last its
// standing, that a short path made it (BUSTER_NODE_FACTS_SHORT in its `taken`; node_facts_slow marks its own) and no
// exponent.
static inline __attribute__((always_inline)) void node_facts_record(NodeFactsRecord* records, uint32_t w, TypeId type, uint32_t word, uint8_t standing, uint32_t flags,
                                                                     uint32_t object, uint64_t payload)
{
    NodeFactsRecord* record = records + w;
    node_facts_store(record, 0, (uint64_t)type | (uint64_t)word << 32);
    node_facts_store(record, 8, (uint64_t)flags | (uint64_t)object << 32);
    node_facts_store(record, 16, payload);
    node_facts_store(record, 24, (uint64_t)standing | 0x100u);
}

static inline __attribute__((always_inline)) void node_facts_conversions(NodeFactsRecord* records, uint32_t w, TypeId first, uint32_t first_flags, TypeId second,
                                                                          uint32_t second_flags, uint64_t member_offset)
{
    NodeFactsRecord* record = records + w;
    node_facts_store(record, 32, (uint64_t)first | (uint64_t)second << 32);
    node_facts_store(record, 40, (uint64_t)first_flags | (uint64_t)second_flags << 32);
    node_facts_store(record, 48, member_offset);
}

// An identifier's leaf `w`, node_machine_leaf's record of it, from its declaration's line: the line is the record's
// first half, and the second is its type alone (its own conversion's entry), which a lean kernel leaves out. One load
// of a vector and one store; a host without the vectors copies the line and stores the rest. Whether the line gives it
// (no register object's, whose flags follow its use, and no enumerator's value the line doesn't hold).
static inline __attribute__((always_inline)) bool node_facts_leaf(NodeFactsRecord* records, const NodeFactsView* view, uint32_t w, bool lean)
{
    const uint64_t* line = view->lines + 4 * (uint64_t)view->inputs[w];
    const bool taken = (uint32_t)(line[3] >> 32) == 0;
    if (BUSTER_SCALAR_PATH(false))
    {
        if (taken)
        {
            memcpy(records + w, line, 4 * sizeof(uint64_t));
            if (!lean)
            {
                node_facts_conversions(records, w, (TypeId)line[0], 0, 0, 0, 0);
                node_facts_store(records + w, 56, 0);
            }
        }
    }
    else
    {
        const __m256i half = _mm256_loadu_si256((const __m256i*)(const void*)line);
        if (taken && lean)
        {
            _mm256_storeu_si256((__m256i*)(void*)(records + w), half);
        }
        else if (taken)
        {
            _mm512_storeu_si512(records + w, _mm512_inserti64x4(_mm512_castsi256_si512(half), _mm256_zextsi128_si256(_mm_cvtsi32_si128((int)(uint32_t)line[0])), 1));
        }
    }
    return taken;
}

static inline __attribute__((always_inline)) const NodeFactsRow* node_facts_row_of(const NodeFactsView* view, TypeId type)
{
    return view->rows + (type_index(type) < view->row_count ? type_index(type) : view->row_count);
}

// Record `a`'s value as an operator's operand reads it, node_machine_operand_value's, from the kernel's records and the
// operand's type's row `row`: its lvalue's value type, an array's or a function's pointer. Whether the common cases took
// it, with a type that has no wrapper and a value the columns hold whole (the rule's otherwise).
static inline __attribute__((always_inline)) bool node_facts_operand(const NodeFactsRecord* records, const NodeFactsRow* row, uint32_t a, NodeFactsOperand* value,
                                                                      TypeId* converted, uint32_t* conversion_flags)
{
    const uint8_t standing = records[a].standing;
    const TypeId type = records[a].type;
    const uint32_t word = records[a].word;
    uint8_t kind = (uint8_t)(word >> 16);
    uint8_t category = standing & 0x03u;
    uint8_t constant_kind = (standing >> 2) & 0x07u;
    uint32_t flags = records[a].flags;
    TypeId result = type;
    bool taken = (word & BUSTER_NODE_MACHINE_WORD_PLAIN) != 0 && (standing & BUSTER_NODE_MACHINE_WIDE) == 0;
    *converted = type;
    *conversion_flags = 0;
    if (type_kind_is_array(kind))
    {
        const uint32_t qualifiers = type_qualifiers(type);
        const TypeId pointer = (flags & BUSTER_CHECK_REGISTER_NAME) == 0 && qualifiers <= BUSTER_TYPE_CONST ? row->decayed[qualifiers] : 0;
        taken = taken && pointer != 0;
        result = pointer;
        category = CHECK_RVALUE;
        flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_BIT_FIELD | BUSTER_CHECK_REGISTER | BUSTER_CHECK_REGISTER_NAME);
        constant_kind = (flags & BUSTER_CHECK_STATIC_OBJECT) == 0 && constant_kind == CHECK_CONSTANT_ADDRESS ? CHECK_CONSTANT_NONE : constant_kind;
        kind = BUSTER_TYPE_KIND_POINTER;
        *converted = pointer;
        *conversion_flags = BUSTER_CHECK_CONVERT_ARRAY;
    }
    else if (kind == BUSTER_TYPE_KIND_FUNCTION)
    {
        const TypeId pointer = row->decayed[0];
        taken = taken && pointer != 0;
        result = pointer;
        category = CHECK_RVALUE;
        flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_FLOAT_LITERAL);
        kind = BUSTER_TYPE_KIND_POINTER;
        *converted = pointer;
        *conversion_flags = BUSTER_CHECK_CONVERT_FUNCTION;
    }
    else if (category == CHECK_LVALUE)
    {
        taken = taken && node_machine_plain_kind(kind) && (type_qualifiers(type) & BUSTER_TYPE_VOLATILE) == 0 && (flags & BUSTER_CHECK_VOLATILE_PART) == 0;
        constant_kind = constant_kind == CHECK_CONSTANT_ADDRESS || (flags & BUSTER_CHECK_ICE) == 0 ? CHECK_CONSTANT_NONE : constant_kind;
        result = row->value;
        category = CHECK_RVALUE;
        flags &= ~(BUSTER_CHECK_REGISTER | BUSTER_CHECK_REGISTER_NAME | BUSTER_CHECK_VOLATILE_PART);
        *converted = result;
        *conversion_flags = BUSTER_CHECK_CONVERT_LVALUE;
    }
    *value = (NodeFactsOperand){
        .type = result,
        .flags = flags,
        .integer = (standing & BUSTER_NODE_MACHINE_FIELD) == 0 ? records[a].payload : 0,
        .category = category,
        .constant_kind = constant_kind,
        .kind = kind,
    };
    return taken;
}

// What record `a`'s pointer, as node_facts_operand converted it, points at: its row's element, an array's with the array's
// qualifiers (type_element's).
static inline __attribute__((always_inline)) TypeId node_facts_pointee(const NodeFactsRecord* records, uint32_t a, const NodeFactsRow* row)
{
    return row->element | (type_kind_is_array((uint8_t)(records[a].word >> 16)) ? type_qualifiers(records[a].type) : 0);
}

// The member `name` of record type `record`, its slot: type_lookup_member's answer, kept the first time it is asked for.
static inline __attribute__((always_inline)) const NodeFactsMember* node_facts_member_slot(NodeFacts* facts, const NodeFactsView* view, TypeId record, uint32_t name)
{
    const NodeFactsMember* members = view->members;
    const uint64_t key = (uint64_t)record << 32 | name;
    uint32_t at = (uint32_t)((key * UINT64_C(0x9e3779b97f4a7c15)) >> (64 - view->member_bits));
    const NodeFactsMember* slot = members + at;
    while (slot->key != key && slot->flags != 0)
    {
        at = (at + 1) & ((1u << view->member_bits) - 1);
        slot = members + at;
    }
    if (__builtin_expect(slot->key != key, 0))
    {
        slot = node_facts_member_fill(facts, at, record, name);
    }
    return slot;
}

// The same slot for a short path, which fills none: the slot where the member's answer is kept, or one that holds no
// answer, which sends the access to its rule (and the rule fills the slot). No call is left in a short path's loop.
static const NodeFactsMember node_facts_no_member;

static inline __attribute__((always_inline)) const NodeFactsMember* node_facts_member_kept(const NodeFactsView* view, TypeId record, uint32_t name)
{
    const NodeFactsMember* members = view->members;
    const uint64_t key = (uint64_t)record << 32 | name;
    uint32_t at = (uint32_t)((key * UINT64_C(0x9e3779b97f4a7c15)) >> (64 - view->member_bits));
    const NodeFactsMember* slot = members + at;
    while (slot->key != key && slot->flags != 0)
    {
        at = (at + 1) & ((1u << view->member_bits) - 1);
        slot = members + at;
    }
    return slot->key == key ? slot : &node_facts_no_member;
}

// A member access `w` of record `a`, node_machine_member's steps: `->` of a pointer to a struct or union that has no
// wrapper, `.` of such a record itself, complete at the node, of a member that is no bit-field.
static inline __attribute__((always_inline)) bool node_facts_member(NodeFacts* facts, NodeFactsRecord* records, const NodeFactsView* view, uint32_t w, uint32_t a,
                                                                     bool arrow)
{
    const TypeId base_type = records[a].type;
    const uint8_t base_standing = records[a].standing;
    const NodeFactsRow* row = node_facts_row_of(view, base_type);
    NodeFactsOperand base = {
        .type = base_type,
        .flags = records[a].flags,
        .category = base_standing & 0x03u,
        .constant_kind = (base_standing >> 2) & 0x07u,
        .kind = (uint8_t)(records[a].word >> 16),
    };
    TypeId converted = base_type;
    uint32_t conversion_flags = 0;
    bool taken = (arrow ? node_facts_operand(records, row, a, &base, &converted, &conversion_flags) : (records[a].word & BUSTER_NODE_MACHINE_WORD_PLAIN) != 0) &&
                 (base_standing & BUSTER_NODE_MACHINE_WIDE) == 0;
    // The record: a pointer's pointee, or the base itself; its word says whether it is a struct or a union with no wrapper.
    const TypeId record = arrow ? node_facts_pointee(records, a, row) : base_type;
    const uint32_t record_word = arrow ? row->element_word : row->word;
    const uint32_t complete_at = arrow ? row->element_complete_at : row->complete_at;
    const uint8_t record_kind = (uint8_t)(record_word >> 16);
    const bool address = !arrow && base.constant_kind == CHECK_CONSTANT_ADDRESS;
    taken = taken && (!arrow || base.kind == BUSTER_TYPE_KIND_POINTER) && (record_word & BUSTER_NODE_MACHINE_WORD_PLAIN) != 0 &&
            (record_kind == BUSTER_TYPE_KIND_STRUCT || record_kind == BUSTER_TYPE_KIND_UNION) && (base.constant_kind == CHECK_CONSTANT_NONE || address) &&
            complete_at != UINT32_MAX && complete_at <= view->positions[w];
    if (taken)
    {
        const NodeFactsMember* slot = node_facts_member_slot(facts, view, record, view->inputs[w]);
        const uint64_t offset = slot->offset;
        const int64_t base_offset = (base_standing & BUSTER_NODE_MACHINE_FIELD) == BUSTER_NODE_MACHINE_OFFSET ? (int64_t)records[a].payload : 0;
        int64_t addend = 0;
        taken = (slot->flags & (BUSTER_NODE_FACTS_MEMBER_FOUND | BUSTER_NODE_FACTS_MEMBER_BIT_FIELD)) == BUSTER_NODE_FACTS_MEMBER_FOUND &&
                (!address || (offset % 8 == 0 && offset / 8 <= INT64_MAX && !__builtin_add_overflow(base_offset, (int64_t)(offset / 8), &addend)));
        if (taken)
        {
            uint32_t flags = (base.flags & BUSTER_CHECK_INHERITED) | (base.flags & (BUSTER_CHECK_STATIC_OBJECT | BUSTER_CHECK_CONST_READ_RESTRICTED | BUSTER_CHECK_NONNULL_ADDRESS));
            flags |= (slot->flags & BUSTER_NODE_FACTS_MEMBER_FLEXIBLE) != 0 ? BUSTER_CHECK_CONST_READ_RESTRICTED : 0;
            flags |= arrow && (base.flags & BUSTER_CHECK_INACTIVE_UNION_ADDRESS) != 0 ? BUSTER_CHECK_INACTIVE_UNION_READ : 0;
            flags |= !arrow ? base.flags & BUSTER_CHECK_REGISTER : 0;
            const CheckCategory category = arrow || base.category == CHECK_LVALUE ? CHECK_LVALUE : CHECK_RVALUE;
            const uint8_t standing = BUSTER_NODE_MACHINE_STANDING(category, address ? CHECK_CONSTANT_ADDRESS : CHECK_CONSTANT_NONE) |
                                     (addend != 0 ? BUSTER_NODE_MACHINE_OFFSET : 0);
            node_facts_record(records, w, slot->type, slot->word, standing, check_value_source_flags(arrow ? CHECK_OP_ARROW : CHECK_OP_MEMBER, flags),
                              address ? records[a].object : 0, (uint64_t)addend);
            node_facts_conversions(records, w, converted, conversion_flags, 0, 0, offset);
        }
    }
    return taken;
}

// Record `w`'s value: record `a`'s with its type, category, flags and constant kind replaced, node_machine_record_from's
// with no integer replaced, of a record the columns hold whole.
static inline __attribute__((always_inline)) void node_facts_from(NodeFactsRecord* records, uint32_t w, uint32_t a, TypeId type, uint32_t word, CheckCategory category,
                                                                   uint32_t flags, uint8_t constant_kind)
{
    const uint8_t standing = BUSTER_NODE_MACHINE_STANDING(category, constant_kind) | (records[a].standing & BUSTER_NODE_MACHINE_FIELD);
    NodeFactsRecord* record = records + w;
    node_facts_store(record, 0, (uint64_t)type | (uint64_t)word << 32);
    node_facts_store(record, 8, (uint64_t)flags | (uint64_t)records[a].object << 32);
    node_facts_store(record, 16, records[a].payload);
    node_facts_store(record, 24, (uint64_t)standing | 0x100u | (uint64_t)records[a].exponent << 16);
}

// A dereference `w` of record `a`, node_machine_dereference's steps.
static inline __attribute__((always_inline)) bool node_facts_dereference(NodeFactsRecord* records, const NodeFactsView* view, uint32_t w, uint32_t a)
{
    const NodeFactsRow* row = node_facts_row_of(view, records[a].type);
    NodeFactsOperand pointer;
    TypeId converted = 0;
    uint32_t conversion_flags = 0;
    const bool taken = node_facts_operand(records, row, a, &pointer, &converted, &conversion_flags) && pointer.kind == BUSTER_TYPE_KIND_POINTER;
    if (taken)
    {
        const TypeId type = node_facts_pointee(records, a, row);
        const CheckCategory category = (uint8_t)(row->element_word >> 16) == BUSTER_TYPE_KIND_FUNCTION ? CHECK_FUNCTION : CHECK_LVALUE;
        uint32_t flags = pointer.flags;
        flags |= (flags & BUSTER_CHECK_INACTIVE_UNION_ADDRESS) != 0 ? BUSTER_CHECK_INACTIVE_UNION_READ : 0;
        flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_NULL_POINTER | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_ICE_UNEVALUATED);
        node_facts_from(records, w, a, type, row->element_word, category, check_value_source_flags(CHECK_OP_DEREFERENCE, flags), pointer.constant_kind);
        node_facts_conversions(records, w, converted, conversion_flags, 0, 0, 0);
    }
    return taken;
}

// A subscript `w` of records `a` and `b`, node_machine_pointer_rule's steps for one: a pointer to a complete object type
// and an integer, the pointer no constant or an address constant.
static inline __attribute__((always_inline)) bool node_facts_subscript(NodeFactsRecord* records, const NodeFactsView* view, uint32_t w, uint32_t a, uint32_t b)
{
    const NodeFactsRow* rows[2] = { node_facts_row_of(view, records[a].type), node_facts_row_of(view, records[b].type) };
    NodeFactsOperand operands[2];
    TypeId converted[2] = { 0, 0 };
    uint32_t conversion_flags[2] = { 0, 0 };
    bool taken = node_facts_operand(records, rows[0], a, operands, converted, conversion_flags) &&
                 node_facts_operand(records, rows[1], b, operands + 1, converted + 1, conversion_flags + 1);
    const bool left_pointer = taken && operands[0].kind == BUSTER_TYPE_KIND_POINTER;
    const bool right_pointer = taken && operands[1].kind == BUSTER_TYPE_KIND_POINTER;
    const bool reverse = !left_pointer && right_pointer;
    const NodeFactsOperand* pointer = operands + (reverse ? 1 : 0);
    const NodeFactsOperand* index = operands + (reverse ? 0 : 1);
    const uint32_t slot = reverse ? 0 : 1;
    const uint32_t raw = reverse ? b : a;
    const NodeFactsRow* row = rows[reverse ? 1 : 0];
    const NodeFactsRow* index_row = rows[reverse ? 0 : 1];
    const uint8_t element_kind = (uint8_t)(row->element_word >> 16);
    // An address constant's element: at an index that has no constant, or at an integer constant's of a builtin type,
    // unqualified, of at most 64 bits (node_machine_operand_width's), where the row has the element's size.
    const uint32_t width = (index->type & BUSTER_TYPE_QUALIFIERS) == 0 ? index_row->flags >> 8 & 0xffu : 0;
    const bool address = pointer->constant_kind == CHECK_CONSTANT_ADDRESS &&
                         (index->constant_kind == CHECK_CONSTANT_NONE || (index->constant_kind == CHECK_CONSTANT_INTEGER && width != 0));
    const bool constant = address && index->constant_kind == CHECK_CONSTANT_INTEGER;
    taken = taken && left_pointer != right_pointer && (index_row->flags & BUSTER_NODE_FACTS_INTEGER) != 0 && (index->flags & BUSTER_CHECK_BIT_FIELD) == 0 &&
            (pointer->constant_kind == CHECK_CONSTANT_NONE || address) && (!constant || (row->flags & BUSTER_NODE_FACTS_STRIDE) != 0) &&
            element_kind != BUSTER_TYPE_KIND_VOID && element_kind != BUSTER_TYPE_KIND_FUNCTION && row->element_complete_at != UINT32_MAX &&
            row->element_complete_at <= view->positions[w];
    if (taken)
    {
        // The index's promotion, check_action_promote's conversion.
        const TypeId promoted = index_row->promoted;
        conversion_flags[slot] |= promoted != index->type ? BUSTER_CHECK_CONVERT_PROMOTION : 0;
        converted[slot] = promoted != index->type ? promoted : converted[slot];
        uint32_t flags = (operands[0].flags | operands[1].flags) & BUSTER_CHECK_INHERITED;
        flags |= pointer->flags & (BUSTER_CHECK_STATIC_OBJECT | BUSTER_CHECK_INACTIVE_UNION_ADDRESS | BUSTER_CHECK_CONST_READ_RESTRICTED | BUSTER_CHECK_NONNULL_ADDRESS);
        flags |= (pointer->flags & BUSTER_CHECK_INACTIVE_UNION_ADDRESS) != 0 ? BUSTER_CHECK_INACTIVE_UNION_READ : 0;
        flags |= (records[raw].flags & (BUSTER_CHECK_REGISTER | BUSTER_CHECK_REGISTER_NAME)) == BUSTER_CHECK_REGISTER &&
                 type_kind_is_array((uint8_t)(records[raw].word >> 16)) ? BUSTER_CHECK_REGISTER : 0;
        // A constant index's element: the address constant's object, its offset moved by the index's elements.
        uint64_t addend = 0;
        if (constant)
        {
            const uint64_t mask = width == 64 ? UINT64_MAX : (UINT64_C(1) << width) - 1;
            const uint64_t stored = index->integer & mask;
            const bool negative = (index_row->flags & BUSTER_NODE_FACTS_SIGNED) != 0 && width < 64 && ((stored >> (width - 1)) & 1) != 0;
            const uint64_t offset = (records[raw].standing & BUSTER_NODE_MACHINE_FIELD) == BUSTER_NODE_MACHINE_OFFSET ? records[raw].payload : 0;
            addend = offset + (negative ? stored | ~mask : stored) * row->stride;
        }
        node_facts_record(records, w, node_facts_pointee(records, raw, row), row->element_word,
                          BUSTER_NODE_MACHINE_STANDING(CHECK_LVALUE, constant ? CHECK_CONSTANT_ADDRESS : CHECK_CONSTANT_NONE) | (addend != 0 ? BUSTER_NODE_MACHINE_OFFSET : 0),
                          check_value_source_flags(CHECK_OP_SUBSCRIPT, flags), constant ? records[raw].object : 0, addend);
        node_facts_conversions(records, w, converted[0], conversion_flags[0], converted[1], conversion_flags[1], 0);
    }
    return taken;
}

// A call `w` of callee `a`: node_machine_call_rule's value, its callee's prototype's return type's rvalue with its
// callee's and its arguments' inherited flags, where the prototype takes the arguments' count and no argument's value is
// wide. Its arguments' conversions are the rule's.
static inline __attribute__((always_inline)) bool node_facts_call(NodeFactsRecord* records, const NodeFactsView* view, uint32_t w, uint32_t a)
{
    const NodeFactsRow* row = node_facts_row_of(view, records[a].type);
    const uint32_t function = row->function;
    const uint32_t parameters = function >> 16;
    const uint32_t count = view->counts[w];
    const uint16_t* arguments = view->arguments + view->inputs[w];
    NodeFactsOperand callee;
    TypeId converted = 0;
    uint32_t conversion_flags = 0;
    bool taken = (function & BUSTER_NODE_FACTS_FUNCTION) != 0 && node_facts_operand(records, row, a, &callee, &converted, &conversion_flags) &&
                 (function >> 8 & BUSTER_TYPE_FLAG_PROTOTYPE) != 0 && count >= parameters && ((function >> 8 & BUSTER_TYPE_FLAG_VARIADIC) != 0 || count == parameters) &&
                 ((uint8_t)(row->returned_word >> 16) == BUSTER_TYPE_KIND_VOID ||
                  (row->returned_complete_at != UINT32_MAX && row->returned_complete_at <= view->positions[w]));
    uint32_t flags = taken ? BUSTER_CHECK_SIDE_EFFECT | (callee.flags & BUSTER_CHECK_INHERITED) : 0;
#pragma clang loop vectorize(disable) interleave(disable) unroll(disable)
    for (uint32_t i = 0; taken && i < count; i += 1)
    {
        const uint32_t argument = w - arguments[i];
        taken = (records[argument].standing & BUSTER_NODE_MACHINE_WIDE) == 0;
        flags |= records[argument].flags & BUSTER_CHECK_INHERITED;
    }
    if (taken)
    {
        node_facts_record(records, w, row->returned, row->returned_word, BUSTER_NODE_MACHINE_STANDING(CHECK_RVALUE, CHECK_CONSTANT_NONE),
                          check_value_source_flags(CHECK_OP_CALL, flags), 0, 0);
        node_facts_conversions(records, w, converted, conversion_flags, 0, 0, 0);
    }
    return taken;
}

// A discard `w` of record `a`, node_machine_discard's steps on the columns: a cast to void of an rvalue the columns hold
// whole, of no array, function, vector or complex type and no _BitInt constant, in a unit with no overload candidates
// (`plain`).
static inline __attribute__((always_inline)) bool node_facts_discard(NodeFactsRecord* records, const NodeFactsView* view, bool plain, uint32_t w, uint32_t a)
{
    const uint8_t standing = records[a].standing;
    const uint8_t kind = (uint8_t)(records[a].word >> 16);
    const uint8_t constant_kind = (standing >> 2) & 0x07u;
    const bool taken = (standing & BUSTER_NODE_MACHINE_WIDE) == 0 && plain && (standing & 0x03u) == CHECK_RVALUE && !type_kind_is_array(kind) &&
                       kind != BUSTER_TYPE_KIND_FUNCTION && kind != BUSTER_TYPE_KIND_VECTOR && kind != BUSTER_TYPE_KIND_EXT_VECTOR && kind != BUSTER_TYPE_KIND_COMPLEX &&
                       (kind != BUSTER_TYPE_KIND_BIT_INT || constant_kind == CHECK_CONSTANT_NONE);
    if (taken)
    {
        const uint32_t restricted = kind == BUSTER_TYPE_KIND_POINTER ? BUSTER_CHECK_CONST_READ_RESTRICTED : 0;
        const uint32_t dropped = BUSTER_CHECK_BIT_FIELD | BUSTER_CHECK_REGISTER | BUSTER_CHECK_REGISTER_NAME | BUSTER_CHECK_NULL_POINTER | BUSTER_CHECK_ICE |
                                 BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_ICE_UNEVALUATED | BUSTER_CHECK_MODIFICATION |
                                 BUSTER_CHECK_LITERAL_ZERO;
        node_facts_from(records, w, a, BUSTER_TYPE_VOID, view->void_word, CHECK_RVALUE, (records[a].flags | restricted) & ~dropped, CHECK_CONSTANT_NONE);
        node_facts_conversions(records, w, BUSTER_TYPE_VOID, BUSTER_CHECK_CONVERT_CAST, 0, 0, 0);
    }
    return taken;
}

// The places of a window's identifiers' leaves and of its lookups, from its `count` records' kinds, as two lists, and
// the lookups' classes beside their places (`keys`, from the records' `classes`): sixteen records a step, each kind's
// places compressed to the front of a vector and the vector stored whole at the list's end (compress to a register and
// a plain store: Zen 5's compress to memory is 8 uops), with no branch a record. On a host without the vectors, a
// record at a time: the same lists (node_facts_lists_same is the twin's check on either).
static inline __attribute__((always_inline)) void node_facts_list(uint32_t* leaf_list, uint32_t* lookup_list, uint8_t* keys, const uint8_t* kinds, const uint8_t* classes,
                                                                   uint32_t count, uint32_t* leaf_count, uint32_t* lookup_count)
{
    uint32_t leaves = 0;
    uint32_t lookups = 0;
    if (BUSTER_SCALAR_PATH(false))
    {
        for (uint32_t w = 0; w < count; w += 1)
        {
            const uint8_t kind = kinds[w];
            leaf_list[leaves] = w;
            lookup_list[lookups] = w;
            keys[lookups] = classes[w];
            leaves += kind == BUSTER_NODE_FACTS_IDENTIFIER;
            lookups += kind == BUSTER_NODE_FACTS_LOOKUP;
        }
    }
    else
    {
        const __m512i step = _mm512_set1_epi32(16);
        __m512i places = _mm512_set_epi32(15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
        for (uint32_t w = 0; w < count; w += 16)
        {
            const uint32_t live = count - w < 16 ? count - w : 16;
            const __mmask16 mask = (__mmask16)((1u << live) - 1);
            const __m128i block = _mm_maskz_loadu_epi8(mask, kinds + w);
            const __mmask16 leaf = _mm_mask_cmpeq_epi8_mask(mask, block, _mm_set1_epi8(BUSTER_NODE_FACTS_IDENTIFIER));
            const __mmask16 lookup = _mm_mask_cmpeq_epi8_mask(mask, block, _mm_set1_epi8(BUSTER_NODE_FACTS_LOOKUP));
            _mm512_storeu_si512(leaf_list + leaves, _mm512_maskz_compress_epi32(leaf, places));
            _mm512_storeu_si512(lookup_list + lookups, _mm512_maskz_compress_epi32(lookup, places));
            _mm_storeu_si128((__m128i*)(void*)(keys + lookups), _mm_maskz_compress_epi8(lookup, _mm_maskz_loadu_epi8(lookup, classes + w)));
            leaves += (uint32_t)__builtin_popcount((uint32_t)leaf);
            lookups += (uint32_t)__builtin_popcount((uint32_t)lookup);
            places = _mm512_add_epi32(places, step);
        }
    }
    *leaf_count = leaves;
    *lookup_count = lookups;
}

// The lookups of class `key` among the window's `lookups`, whose classes are `keys`: their places, in their order, as
// `batch`; how many. Sixteen lookups a step, compressed as the listing's; on a host without the vectors, one at a time.
static inline __attribute__((always_inline)) uint32_t node_facts_batch(uint32_t* batch, const uint32_t* lookup_list, const uint8_t* keys, uint32_t lookups, uint8_t key)
{
    uint32_t count = 0;
    if (BUSTER_SCALAR_PATH(false))
    {
        for (uint32_t i = 0; i < lookups; i += 1)
        {
            batch[count] = lookup_list[i];
            count += keys[i] == key;
        }
    }
    else
    {
        for (uint32_t i = 0; i < lookups; i += 16)
        {
            const uint32_t live = lookups - i < 16 ? lookups - i : 16;
            const __mmask16 mask = (__mmask16)((1u << live) - 1);
            const __mmask16 chosen = _mm_mask_cmpeq_epi8_mask(mask, _mm_maskz_loadu_epi8(mask, keys + i), _mm_set1_epi8((char)key));
            _mm512_storeu_si512(batch + count, _mm512_maskz_compress_epi32(chosen, _mm512_maskz_loadu_epi32(mask, lookup_list + i)));
            count += (uint32_t)__builtin_popcount((uint32_t)chosen);
        }
    }
    return count;
}

// A record the kernel leaves, the window's record `w` (the unit's `at`): T's record of it, as the rule would make it.
static __attribute__((noinline)) void node_facts_leave(NodeFacts* facts, uint64_t at, uint32_t w)
{
    facts->records[w] = node_facts_expected(facts, at);
    facts->records[w].taken = BUSTER_NODE_FACTS_LEFT;
}

// The lookups' short paths. Each is its rule above for the shapes nearly every lookup has, as straight code: the
// operand's type's row says in one test of its flags what the rule asks the type table in several, what makes the
// operand unfit for the path is gathered into one word and tested once, and the few values that depend on the operand
// are selected, not branched on. A path takes only what its rule takes, with the rule's record; a lookup it doesn't
// take goes to the rule (node_facts_slow), so the two together make what the rule alone does.

// What keeps a record of standing `standing`, flags `flags` and type `type` from being an operand's value by the short
// path, as bits (0 where it is fit): a wide value or a function designator; of an lvalue, which converts to its value
// (node_facts_operand's third case), a volatile one or, with `constant`, one of an integer constant expression (whose
// value keeps its constant); of an rvalue, with `constant`, a constant. `lvalue` is all ones for an lvalue.
static inline __attribute__((always_inline)) uint32_t node_facts_unfit(TypeId type, uint32_t standing, uint32_t flags, uint32_t lvalue, bool constant)
{
    _Static_assert(CHECK_RVALUE == 0 && CHECK_LVALUE == 1 && CHECK_FUNCTION == 2 && BUSTER_CHECK_CONVERT_LVALUE == 1, "an lvalue's category is its conversion's flag");
    const uint32_t converted = (flags & (BUSTER_CHECK_VOLATILE_PART | (constant ? BUSTER_CHECK_ICE : 0))) | (type & BUSTER_TYPE_VOLATILE);
    const uint32_t kept = constant ? standing & 0x1cu : 0;
    return (standing & (BUSTER_NODE_MACHINE_WIDE | CHECK_FUNCTION)) | (lvalue & converted) | (~lvalue & kept);
}

// `->`'s short path: a pointer value, no constant, to a struct or union complete at the node, and a member that is no
// bit-field.
static inline __attribute__((always_inline)) bool node_facts_arrow_fast(NodeFactsRecord* records, const NodeFactsView* view, uint32_t w, uint32_t a, bool lean)
{
    const TypeId type = records[a].type;
    const uint32_t standing = records[a].standing;
    const uint32_t flags = records[a].flags;
    const NodeFactsRow* row = node_facts_row_of(view, type);
    const uint32_t lvalue = 0u - (standing & 0x01u);
    const uint32_t unfit = node_facts_unfit(type, standing, flags, lvalue, true) | (~row->flags & BUSTER_NODE_FACTS_ARROW);
    bool fast = unfit == 0 && row->element_complete_at <= view->positions[w];
    if (fast)
    {
        const NodeFactsMember* slot = node_facts_member_kept(view, row->element, view->inputs[w]);
        fast = (slot->flags & (BUSTER_NODE_FACTS_MEMBER_FOUND | BUSTER_NODE_FACTS_MEMBER_BIT_FIELD)) == BUSTER_NODE_FACTS_MEMBER_FOUND;
        if (fast)
        {
            // Its base's inherited flags and its object's; a read through an inactive union's address is one of an
            // inactive union (the flag below the address's).
            _Static_assert(BUSTER_CHECK_INACTIVE_UNION_ADDRESS >> 1 == BUSTER_CHECK_INACTIVE_UNION_READ, "an inactive union's address's flag shifts to its read's");
            const uint32_t kept = BUSTER_CHECK_INHERITED | BUSTER_CHECK_STATIC_OBJECT | BUSTER_CHECK_CONST_READ_RESTRICTED | BUSTER_CHECK_NONNULL_ADDRESS;
            const uint32_t result = (flags & kept) | ((flags >> 1) & BUSTER_CHECK_INACTIVE_UNION_READ) | slot->result_flags;
            node_facts_record(records, w, slot->type, slot->word, BUSTER_NODE_MACHINE_STANDING(CHECK_LVALUE, CHECK_CONSTANT_NONE), result, 0, 0);
            if (lean)
            {
                node_facts_store(records + w, 48, slot->offset);
            }
            else
            {
                node_facts_conversions(records, w, lvalue != 0 ? row->value : type, lvalue & 1u, 0, 0, slot->offset);
            }
        }
    }
    return fast;
}

// `.`'s short path: a struct or union complete at the node, no constant or an address constant, and a member that is no
// bit-field, an address constant's at a whole byte.
static inline __attribute__((always_inline)) bool node_facts_dot_fast(NodeFactsRecord* records, const NodeFactsView* view, uint32_t w, uint32_t a, bool lean)
{
    const TypeId type = records[a].type;
    const uint32_t standing = records[a].standing;
    const uint32_t flags = records[a].flags;
    const NodeFactsRow* row = node_facts_row_of(view, type);
    const uint32_t kind = (standing >> 2) & 0x07u;
    const bool address = kind == CHECK_CONSTANT_ADDRESS;
    const uint32_t unfit = (standing & BUSTER_NODE_MACHINE_WIDE) | (~row->flags & BUSTER_NODE_FACTS_RECORD) | (kind != CHECK_CONSTANT_NONE && !address ? 1u : 0u);
    bool fast = unfit == 0 && row->complete_at <= view->positions[w];
    if (fast)
    {
        // The operand's payload and object are read whether it is an address constant or not, and chosen by masks: a
        // choice with a load behind it is compiled as a branch, and whether a record is an address constant's (a static
        // object's member) is one the predictor misses.
        const NodeFactsMember* slot = node_facts_member_kept(view, type, view->inputs[w]);
        const uint64_t offset = slot->offset;
        const uint64_t payload = records[a].payload;
        const uint32_t object = records[a].object;
        const uint64_t addressed = 0 - (uint64_t)(address ? 1u : 0u);
        const uint64_t based = 0 - (uint64_t)((standing & BUSTER_NODE_MACHINE_FIELD) == BUSTER_NODE_MACHINE_OFFSET ? 1u : 0u);
        int64_t moved = 0;
        const bool overflow = __builtin_add_overflow((int64_t)(payload & based), (int64_t)(offset / 8), &moved);
        const uint32_t misplaced = (uint32_t)addressed & ((offset % 8 != 0 ? 1u : 0u) | (overflow ? 1u : 0u));
        fast = (((slot->flags ^ BUSTER_NODE_FACTS_MEMBER_FOUND) & (BUSTER_NODE_FACTS_MEMBER_FOUND | BUSTER_NODE_FACTS_MEMBER_BIT_FIELD)) | misplaced) == 0;
        if (fast)
        {
            const int64_t addend = (int64_t)((uint64_t)moved & addressed);
            const uint32_t kept = BUSTER_CHECK_INHERITED | BUSTER_CHECK_STATIC_OBJECT | BUSTER_CHECK_CONST_READ_RESTRICTED | BUSTER_CHECK_NONNULL_ADDRESS | BUSTER_CHECK_REGISTER;
            const uint32_t category = (standing & 0x03u) == CHECK_LVALUE ? CHECK_LVALUE : CHECK_RVALUE;
            _Static_assert(CHECK_CONSTANT_NONE == 0, "a standing with no constant is its category alone");
            const uint8_t made = (uint8_t)(BUSTER_NODE_MACHINE_STANDING(category, CHECK_CONSTANT_NONE) | ((uint32_t)addressed & BUSTER_NODE_MACHINE_STANDING(0, CHECK_CONSTANT_ADDRESS)) |
                                           (addend != 0 ? BUSTER_NODE_MACHINE_OFFSET : 0));
            node_facts_record(records, w, slot->type, slot->word, made, (flags & kept) | slot->result_flags, object & (uint32_t)addressed, (uint64_t)addend);
            if (lean)
            {
                node_facts_store(records + w, 48, offset);
            }
            else
            {
                node_facts_conversions(records, w, type, 0, 0, 0, offset);
            }
        }
    }
    return fast;
}

// A dereference's short path: a pointer's value. Its record is its operand's with the pointee's type, an lvalue or a
// function's designator (node_facts_dereference's steps).
static inline __attribute__((always_inline)) bool node_facts_dereference_fast(NodeFactsRecord* records, const NodeFactsView* view, uint32_t w, uint32_t a, bool lean)
{
    const TypeId type = records[a].type;
    const uint32_t standing = records[a].standing;
    const uint32_t flags = records[a].flags;
    const NodeFactsRow* row = node_facts_row_of(view, type);
    const uint32_t lvalue = 0u - (standing & 0x01u);
    const uint32_t kind = (standing >> 2) & 0x07u;
    const uint32_t unfit = node_facts_unfit(type, standing, flags, lvalue, false) | (~row->flags & BUSTER_NODE_FACTS_POINTER);
    const bool fast = unfit == 0;
    if (fast)
    {
        // The operand's value's flags (an lvalue's without its register's), then the dereference's.
        const uint32_t converted = lvalue != 0 ? flags & ~(BUSTER_CHECK_REGISTER | BUSTER_CHECK_REGISTER_NAME | BUSTER_CHECK_VOLATILE_PART) : flags;
        const uint32_t dropped = BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_NULL_POINTER | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_ICE_UNEVALUATED |
                                 BUSTER_CHECK_LITERAL_ZERO;
        const uint32_t result = (converted | ((converted >> 1) & BUSTER_CHECK_INACTIVE_UNION_READ)) & ~dropped;
        const uint32_t constant_kind = lvalue != 0 && (kind == CHECK_CONSTANT_ADDRESS || (flags & BUSTER_CHECK_ICE) == 0) ? CHECK_CONSTANT_NONE : kind;
        const CheckCategory category = (uint8_t)(row->element_word >> 16) == BUSTER_TYPE_KIND_FUNCTION ? CHECK_FUNCTION : CHECK_LVALUE;
        node_facts_from(records, w, a, row->element, row->element_word, category, result, (uint8_t)constant_kind);
        if (!lean)
        {
            node_facts_conversions(records, w, lvalue != 0 ? row->value : type, lvalue & 1u, 0, 0, 0);
        }
    }
    return fast;
}

// A subscript's short path: a pointer value or an array at its left, an integer value at its right, the element an
// object type complete at the node, and no constant index of an address constant (the rule folds that one).
static inline __attribute__((always_inline)) bool node_facts_subscript_fast(NodeFactsRecord* records, const NodeFactsView* view, uint32_t w, uint32_t a, uint32_t b,
                                                                             bool lean)
{
    const TypeId pointer_type = records[a].type;
    const uint32_t pointer_standing = records[a].standing;
    const uint32_t pointer_flags = records[a].flags;
    const NodeFactsRow* row = node_facts_row_of(view, pointer_type);
    const TypeId index_type = records[b].type;
    const uint32_t index_standing = records[b].standing;
    const uint32_t index_flags = records[b].flags;
    const NodeFactsRow* index_row = node_facts_row_of(view, index_type);
    const uint32_t pointer_lvalue = 0u - (pointer_standing & 0x01u);
    const uint32_t index_lvalue = 0u - (index_standing & 0x01u);
    const uint32_t pointer_kind = (pointer_standing >> 2) & 0x07u;
    const uint32_t index_kind = (index_standing >> 2) & 0x07u;
    // The pointer: a pointer's value, or an array's pointer to its element (node_facts_operand's first case), and what
    // constant is left of it. Every test below is a word of 0 or 1 and every choice a mask: whether the pointer is an
    // array, an lvalue or a constant, and the index a constant, differ from one subscript to the next, and a branch on
    // any of them is one the predictor misses. What a choice would load is loaded whichever way it goes.
    const uint32_t array = (row->flags & BUSTER_NODE_FACTS_ARRAY) != 0 ? 1u : 0u;
    const uint32_t arrayed = 0u - array;
    const uint32_t qualifiers = type_qualifiers(pointer_type);
    const TypeId pointer_of = row->decayed[qualifiers & BUSTER_TYPE_CONST];
    const uint32_t decays = ((pointer_flags & BUSTER_CHECK_REGISTER_NAME) == 0 ? 1u : 0u) & (qualifiers <= BUSTER_TYPE_CONST ? 1u : 0u);
    const TypeId decayed = pointer_of & (0u - decays);
    const uint32_t as_array = (pointer_standing & BUSTER_NODE_MACHINE_WIDE) | (decayed == 0 ? 1u : 0u);
    const uint32_t as_value = node_facts_unfit(pointer_type, pointer_standing, pointer_flags, pointer_lvalue, false) | (~row->flags & BUSTER_NODE_FACTS_POINTER);
    const uint32_t pointer_none = pointer_kind == CHECK_CONSTANT_NONE ? 1u : 0u;
    const uint32_t pointer_address = pointer_kind == CHECK_CONSTANT_ADDRESS ? 1u : 0u;
    const uint32_t pointer_value = pointer_lvalue == 0 ? 1u : 0u;
    const uint32_t pointer_ice = (pointer_flags & BUSTER_CHECK_ICE) != 0 ? 1u : 0u;
    const uint32_t pointer_static = (pointer_flags & BUSTER_CHECK_STATIC_OBJECT) != 0 ? 1u : 0u;
    const uint32_t array_constant = (pointer_none ^ 1u) & ((pointer_address & (pointer_static ^ 1u)) ^ 1u);
    const uint32_t value_constant = (pointer_none ^ 1u) & (pointer_value | ((pointer_address ^ 1u) & pointer_ice));
    const uint32_t constant = (array & array_constant) | ((array ^ 1u) & value_constant);
    // An address constant's element at an index that has no constant is no constant either; the lvalue of a pointer is
    // never an address constant, an array's is its static object's.
    const uint32_t address = (array & pointer_address & array_constant) | ((array ^ 1u) & pointer_value & pointer_address);
    // The index: an integer's value, promoted.
    const uint32_t index_none = index_kind == CHECK_CONSTANT_NONE ? 1u : 0u;
    const uint32_t index_address = index_kind == CHECK_CONSTANT_ADDRESS ? 1u : 0u;
    const uint32_t index_rvalue = index_lvalue == 0 ? 1u : 0u;
    const uint32_t index_ice = (index_flags & BUSTER_CHECK_ICE) != 0 ? 1u : 0u;
    const uint32_t index_constant = (index_none ^ 1u) & (index_rvalue | ((index_address ^ 1u) & index_ice));
    const uint32_t wanted = BUSTER_NODE_FACTS_SCALAR | BUSTER_NODE_FACTS_INTEGER;
    const uint32_t unfit = (arrayed & as_array) | (~arrayed & as_value) | node_facts_unfit(index_type, index_standing, index_flags, index_lvalue, false) |
                           (~index_row->flags & wanted) | (index_flags & BUSTER_CHECK_BIT_FIELD) | (~row->flags & BUSTER_NODE_FACTS_OBJECTS) |
                           (constant & ((address & (index_constant ^ 1u)) ^ 1u));
    const bool fast = unfit == 0 && row->element_complete_at <= view->positions[w];
    if (fast)
    {
        const TypeId index_value = index_type ^ ((index_type ^ index_row->value) & index_lvalue);
        const TypeId promoted = index_row->promoted;
        const uint32_t promotes = promoted != index_value ? 1u : 0u;
        const uint32_t kept = BUSTER_CHECK_STATIC_OBJECT | BUSTER_CHECK_INACTIVE_UNION_ADDRESS | BUSTER_CHECK_CONST_READ_RESTRICTED | BUSTER_CHECK_NONNULL_ADDRESS;
        const uint32_t held = arrayed & ((pointer_flags & (BUSTER_CHECK_REGISTER | BUSTER_CHECK_REGISTER_NAME)) == BUSTER_CHECK_REGISTER ? BUSTER_CHECK_REGISTER : 0);
        const uint32_t result = ((pointer_flags | index_flags) & BUSTER_CHECK_INHERITED) | (pointer_flags & kept) | ((pointer_flags >> 1) & BUSTER_CHECK_INACTIVE_UNION_READ) | held;
        node_facts_record(records, w, row->element | (arrayed & qualifiers), row->element_word, BUSTER_NODE_MACHINE_STANDING(CHECK_LVALUE, CHECK_CONSTANT_NONE), result, 0, 0);
        if (!lean)
        {
            const TypeId pointer_value_type = pointer_type ^ ((pointer_type ^ row->value) & pointer_lvalue);
            node_facts_conversions(records, w, (arrayed & decayed) | (~arrayed & pointer_value_type), (arrayed & BUSTER_CHECK_CONVERT_ARRAY) | (~arrayed & pointer_lvalue & 1u),
                                   promoted, (index_lvalue & 1u) | ((0u - promotes) & BUSTER_CHECK_CONVERT_PROMOTION), 0);
        }
    }
    return fast;
}

// A call's short path: a function's designator, or a pointer to a function's value, of a prototype that takes the
// arguments' count and whose return type is void or complete at the node, with no wide argument.
static inline __attribute__((always_inline)) bool node_facts_call_fast(NodeFactsRecord* records, const NodeFactsView* view, uint32_t w, uint32_t a, bool lean)
{
    const TypeId type = records[a].type;
    const uint32_t standing = records[a].standing;
    const uint32_t flags = records[a].flags;
    const NodeFactsRow* row = node_facts_row_of(view, type);
    const uint32_t function = row->function;
    const uint32_t parameters = function >> 16;
    const uint32_t count = view->counts[w];
    const uint16_t* arguments = view->arguments + view->inputs[w];
    const bool designator = (function & BUSTER_NODE_FACTS_DESIGNATOR) != 0;
    const TypeId pointer = row->decayed[0];
    const uint32_t lvalue = 0u - (standing & 0x01u);
    const uint32_t as_designator = (standing & BUSTER_NODE_MACHINE_WIDE) | (pointer == 0 ? 1u : 0u);
    const uint32_t as_value = node_facts_unfit(type, standing, flags, lvalue, false) | (~row->flags & BUSTER_NODE_FACTS_SCALAR);
    const uint32_t wanted = BUSTER_NODE_FACTS_FUNCTION | BUSTER_NODE_FACTS_RETURNS | BUSTER_TYPE_FLAG_PROTOTYPE << 8;
    // Every argument's flags and standing, gathered whether the call is taken or not. A loop of the arguments' count
    // would end at a branch the predictor misses once a call; the first four are read with no branch, the callee's own
    // record standing for an argument past the count (its flags are gathered already, and a wide callee is unfit), and
    // the few calls of more have a loop for the rest. An argument's distance is read whether the call has the argument
    // or not (the list has entries to spare past its last call's), so that the choice is between two places in
    // registers: a choice with a load behind it is compiled as a branch on the count, which the predictor misses.
    uint32_t gathered = flags;
    uint32_t standings = 0;
#pragma clang loop vectorize(disable) interleave(disable) unroll(full)
    for (uint32_t i = 0; i < 4; i += 1)
    {
        const uint32_t distance = arguments[i];
        const uint32_t place = i < count ? w - distance : a;
        gathered |= records[place].flags;
        standings |= records[place].standing;
    }
#pragma clang loop vectorize(disable) interleave(disable) unroll(disable)
    for (uint32_t i = 4; i < count; i += 1)
    {
        const NodeFactsRecord* argument = records + (w - arguments[i]);
        gathered |= argument->flags;
        standings |= argument->standing;
    }
    const uint32_t unfit = (designator ? as_designator : as_value) | (~function & wanted) | (standings & BUSTER_NODE_MACHINE_WIDE) |
                           (count < parameters || ((function >> 8 & BUSTER_TYPE_FLAG_VARIADIC) == 0 && count != parameters) ? 1u : 0u);
    const bool fast = unfit == 0 && row->returned_ready_at <= view->positions[w];
    if (fast)
    {
        node_facts_record(records, w, row->returned, row->returned_word, BUSTER_NODE_MACHINE_STANDING(CHECK_RVALUE, CHECK_CONSTANT_NONE),
                          BUSTER_CHECK_SIDE_EFFECT | (gathered & BUSTER_CHECK_INHERITED), 0, 0);
        if (!lean)
        {
            node_facts_conversions(records, w, designator ? pointer : lvalue != 0 ? row->value : type, designator ? BUSTER_CHECK_CONVERT_FUNCTION : lvalue & 1u, 0, 0, 0);
        }
    }
    return fast;
}

// A discard's short path, its rule with its tests as one word: a cast to void of an rvalue the record holds whole, of no
// array, function, vector or complex type and no _BitInt constant, in a unit with no overload candidates (`plain`).
static inline __attribute__((always_inline)) bool node_facts_discard_fast(NodeFactsRecord* records, const NodeFactsView* view, bool plain, uint32_t w, uint32_t a)
{
    _Static_assert(BUSTER_TYPE_KIND_COUNT <= 32, "a type's kind is a bit of a word");
    const uint32_t refused = 1u << BUSTER_TYPE_KIND_ARRAY | 1u << BUSTER_TYPE_KIND_INCOMPLETE_ARRAY | 1u << BUSTER_TYPE_KIND_VARIABLE_ARRAY | 1u << BUSTER_TYPE_KIND_FUNCTION |
                             1u << BUSTER_TYPE_KIND_VECTOR | 1u << BUSTER_TYPE_KIND_EXT_VECTOR | 1u << BUSTER_TYPE_KIND_COMPLEX;
    const uint32_t standing = records[a].standing;
    const uint32_t kind = (records[a].word >> 16) & 0xffu;
    const uint32_t constant_kind = (standing >> 2) & 0x07u;
    const uint32_t unfit = (standing & (BUSTER_NODE_MACHINE_WIDE | 0x03u)) | (kind >= 32 ? 1u : (refused >> kind) & 1u) |
                           (kind == BUSTER_TYPE_KIND_BIT_INT && constant_kind != CHECK_CONSTANT_NONE ? 1u : 0u) | (plain ? 0u : 1u);
    const bool fast = unfit == 0;
    if (fast)
    {
        const uint32_t restricted = kind == BUSTER_TYPE_KIND_POINTER ? BUSTER_CHECK_CONST_READ_RESTRICTED : 0;
        const uint32_t dropped = BUSTER_CHECK_BIT_FIELD | BUSTER_CHECK_REGISTER | BUSTER_CHECK_REGISTER_NAME | BUSTER_CHECK_NULL_POINTER | BUSTER_CHECK_ICE |
                                 BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_ICE_UNEVALUATED | BUSTER_CHECK_MODIFICATION |
                                 BUSTER_CHECK_LITERAL_ZERO;
        node_facts_from(records, w, a, BUSTER_TYPE_VOID, view->void_word, CHECK_RVALUE, (records[a].flags | restricted) & ~dropped, CHECK_CONSTANT_NONE);
        node_facts_conversions(records, w, BUSTER_TYPE_VOID, BUSTER_CHECK_CONVERT_CAST, 0, 0, 0);
    }
    return fast;
}

// What the kernel reads of the window whose first record is the unit's `first`, beside its records.
static inline __attribute__((always_inline)) NodeFactsView node_facts_view(const NodeFacts* facts, uint32_t first)
{
    return (NodeFactsView){
        .inputs = facts->inputs + first,
        .counts = facts->counts + first,
        .positions = facts->positions + first,
        .arguments = facts->arguments,
        .lines = facts->lines,
        .rows = facts->rows,
        .members = facts->members,
        .row_count = facts->row_count,
        .member_bits = facts->member_bits,
        .void_word = facts->void_word,
    };
}

// A lookup no short path took, record `w` of the window whose first record is the unit's `first`: its rule, and where
// the rule doesn't take it either, T's record.
static __attribute__((noinline)) void node_facts_slow(NodeFacts* facts, uint32_t first, uint32_t w)
{
    NodeFactsRecord* const records = facts->records;
    const NodeFactsView view = node_facts_view(facts, first);
    const CheckOp op = (CheckOp)facts->ops[first + w];
    const uint32_t a = w - facts->left[first + w];
    const uint32_t right = facts->right[first + w];
    bool taken = false;
    switch (op)
    {
        case CHECK_OP_ARROW: case CHECK_OP_MEMBER:
        {
            taken = node_facts_member(facts, records, &view, w, a, op == CHECK_OP_ARROW);
        } break;
        case CHECK_OP_DEREFERENCE:
        {
            taken = node_facts_dereference(records, &view, w, a);
        } break;
        case CHECK_OP_SUBSCRIPT:
        {
            taken = right != 0 && node_facts_subscript(records, &view, w, a, w - right);
        } break;
        case CHECK_OP_CALL:
        {
            taken = node_facts_call(records, &view, w, a);
        } break;
        case CHECK_OP_CAST:
        {
            taken = node_facts_discard(records, &view, facts->machine->graph->overload_candidate_count == 0, w, a);
        } break;
        default:
        {
        } break;
    }
    if (taken)
    {
        records[w].taken = BUSTER_NODE_FACTS_RULED;
    }
    else
    {
        node_facts_leave(facts, (uint64_t)first + w, w);
    }
}

// The loads' parts: of each lookup's short path, what its load gives (its type, that type's word, a member's offset)
// and what the load itself tests (the row's flags, completeness at the node, the member found, a call's prototype and
// count). The rest of the record is given, and so is that the operand is fit (the caller's test).
static inline __attribute__((always_inline)) void node_facts_loaded(NodeFactsRecord* record, TypeId type, uint32_t word)
{
    node_facts_store(record, 0, (uint64_t)type | (uint64_t)word << 32);
}

static inline __attribute__((always_inline)) bool node_facts_arrow_load(NodeFactsRecord* records, const NodeFactsView* view, uint32_t w, uint32_t a)
{
    const NodeFactsRow* row = node_facts_row_of(view, records[a].type);
    bool taken = (row->flags & BUSTER_NODE_FACTS_ARROW) != 0 && row->element_complete_at <= view->positions[w];
    if (taken)
    {
        const NodeFactsMember* slot = node_facts_member_kept(view, row->element, view->inputs[w]);
        taken = (slot->flags & (BUSTER_NODE_FACTS_MEMBER_FOUND | BUSTER_NODE_FACTS_MEMBER_BIT_FIELD)) == BUSTER_NODE_FACTS_MEMBER_FOUND;
        if (taken)
        {
            node_facts_loaded(records + w, slot->type, slot->word);
            node_facts_store(records + w, 48, slot->offset);
        }
    }
    return taken;
}

static inline __attribute__((always_inline)) bool node_facts_dot_load(NodeFactsRecord* records, const NodeFactsView* view, uint32_t w, uint32_t a)
{
    const TypeId type = records[a].type;
    const NodeFactsRow* row = node_facts_row_of(view, type);
    bool taken = (row->flags & BUSTER_NODE_FACTS_RECORD) != 0 && row->complete_at <= view->positions[w];
    if (taken)
    {
        const NodeFactsMember* slot = node_facts_member_kept(view, type, view->inputs[w]);
        taken = (slot->flags & (BUSTER_NODE_FACTS_MEMBER_FOUND | BUSTER_NODE_FACTS_MEMBER_BIT_FIELD)) == BUSTER_NODE_FACTS_MEMBER_FOUND;
        if (taken)
        {
            node_facts_loaded(records + w, slot->type, slot->word);
            node_facts_store(records + w, 48, slot->offset);
        }
    }
    return taken;
}

static inline __attribute__((always_inline)) bool node_facts_dereference_load(NodeFactsRecord* records, const NodeFactsView* view, uint32_t w, uint32_t a)
{
    const NodeFactsRow* row = node_facts_row_of(view, records[a].type);
    const bool taken = (row->flags & BUSTER_NODE_FACTS_POINTER) != 0;
    if (taken)
    {
        node_facts_loaded(records + w, row->element, row->element_word);
    }
    return taken;
}

static inline __attribute__((always_inline)) bool node_facts_subscript_load(NodeFactsRecord* records, const NodeFactsView* view, uint32_t w, uint32_t a)
{
    const TypeId type = records[a].type;
    const NodeFactsRow* row = node_facts_row_of(view, type);
    const bool array = (row->flags & BUSTER_NODE_FACTS_ARRAY) != 0;
    const bool taken = (row->flags & BUSTER_NODE_FACTS_OBJECTS) != 0 && row->element_complete_at <= view->positions[w];
    if (taken)
    {
        node_facts_loaded(records + w, row->element | (array ? type_qualifiers(type) : 0), row->element_word);
    }
    return taken;
}

static inline __attribute__((always_inline)) bool node_facts_call_load(NodeFactsRecord* records, const NodeFactsView* view, uint32_t w, uint32_t a)
{
    const NodeFactsRow* row = node_facts_row_of(view, records[a].type);
    const uint32_t function = row->function;
    const uint32_t parameters = function >> 16;
    const uint32_t count = view->counts[w];
    const uint32_t wanted = BUSTER_NODE_FACTS_FUNCTION | BUSTER_NODE_FACTS_RETURNS | BUSTER_TYPE_FLAG_PROTOTYPE << 8;
    const bool taken = (function & wanted) == wanted && count >= parameters && ((function >> 8 & BUSTER_TYPE_FLAG_VARIADIC) != 0 || count == parameters) &&
                       row->returned_ready_at <= view->positions[w];
    if (taken)
    {
        node_facts_loaded(records + w, row->returned, row->returned_word);
    }
    return taken;
}

// The lookups a loop's short path didn't take, `count` of them listed: each by its rule. A loop lists them and calls
// this once, after it: a call inside the loop would have every value the loop reads saved around it, a dozen loads a
// lookup for a call a few lookups in a hundred make.
static __attribute__((noinline)) void node_facts_slow_each(NodeFacts* facts, uint32_t first, uint32_t count)
{
    for (uint32_t i = 0; i < count; i += 1)
    {
        node_facts_slow(facts, first, facts->rare[i]);
    }
}

// The window's identifiers' leaves, `leaves` of them listed: each from its declaration's line. The kernel's parts are
// functions of their own, each with the few values it reads in registers, and one a variant, each variant's choice a
// constant in its own.
static inline __attribute__((always_inline)) void node_facts_leaves_of(NodeFacts* facts, uint32_t first, uint32_t leaves, bool lean)
{
    NodeFactsRecord* const records = facts->records;
    const uint32_t* const leaf_list = facts->leaf_list;
    const NodeFactsView view = node_facts_view(facts, first);
    for (uint32_t i = 0; i < leaves; i += 1)
    {
        const uint32_t w = leaf_list[i];
        if (__builtin_expect(!node_facts_leaf(records, &view, w, lean), 0))
        {
            node_facts_leave(facts, (uint64_t)first + w, w);
        }
    }
}

static __attribute__((noinline)) void node_facts_leaves(NodeFacts* facts, uint32_t first, uint32_t leaves)
{
    node_facts_leaves_of(facts, first, leaves, false);
}

static __attribute__((noinline)) void node_facts_leaves_lean(NodeFacts* facts, uint32_t first, uint32_t leaves)
{
    node_facts_leaves_of(facts, first, leaves, true);
}

// The classes that have a lookup among the window's `lookups`, as a mask of their classes (`keys`).
static inline __attribute__((always_inline)) uint64_t node_facts_present(const uint8_t* keys, uint32_t lookups)
{
    uint64_t present = 0;
    for (uint32_t i = 0; i < lookups; i += 1)
    {
        present |= UINT64_C(1) << (keys[i] & 63u);
    }
    return present;
}

// A class's lookups, `batched` of them listed: each by its operator's short path (the whole kernel's, the lean one's,
// or its load alone where the lean kernel found the operand fit), or its rule. One loop an operator and a variant.
static inline __attribute__((always_inline)) void node_facts_arrows_of(NodeFacts* facts, uint32_t first, uint32_t batched, uint32_t mode)
{
    NodeFactsRecord* const records = facts->records;
    const uint32_t* const batch = facts->batch;
    uint32_t* const rare = facts->rare;
    uint32_t rare_count = 0;
    const uint16_t* const left = facts->left + first;
    const uint8_t* const fits = facts->fits + first;
    const NodeFactsView view = node_facts_view(facts, first);
    for (uint32_t i = 0; i < batched; i += 1)
    {
        const uint32_t w = batch[i];
        const uint32_t a = w - left[w];
        const bool taken = mode == BUSTER_NODE_FACTS_LOADS ? fits[w] != 0 && node_facts_arrow_load(records, &view, w, a)
                                                           : node_facts_arrow_fast(records, &view, w, a, mode == BUSTER_NODE_FACTS_LEAN);
        if (__builtin_expect(!taken, 0))
        {
            rare[rare_count] = w;
            rare_count += 1;
        }
    }
    node_facts_slow_each(facts, first, rare_count);
}

static inline __attribute__((always_inline)) void node_facts_dots_of(NodeFacts* facts, uint32_t first, uint32_t batched, uint32_t mode)
{
    NodeFactsRecord* const records = facts->records;
    const uint32_t* const batch = facts->batch;
    uint32_t* const rare = facts->rare;
    uint32_t rare_count = 0;
    const uint16_t* const left = facts->left + first;
    const uint8_t* const fits = facts->fits + first;
    const NodeFactsView view = node_facts_view(facts, first);
    for (uint32_t i = 0; i < batched; i += 1)
    {
        const uint32_t w = batch[i];
        const uint32_t a = w - left[w];
        const bool taken = mode == BUSTER_NODE_FACTS_LOADS ? fits[w] != 0 && node_facts_dot_load(records, &view, w, a)
                                                           : node_facts_dot_fast(records, &view, w, a, mode == BUSTER_NODE_FACTS_LEAN);
        if (__builtin_expect(!taken, 0))
        {
            rare[rare_count] = w;
            rare_count += 1;
        }
    }
    node_facts_slow_each(facts, first, rare_count);
}

static inline __attribute__((always_inline)) void node_facts_dereferences_of(NodeFacts* facts, uint32_t first, uint32_t batched, uint32_t mode)
{
    NodeFactsRecord* const records = facts->records;
    const uint32_t* const batch = facts->batch;
    uint32_t* const rare = facts->rare;
    uint32_t rare_count = 0;
    const uint16_t* const left = facts->left + first;
    const uint8_t* const fits = facts->fits + first;
    const NodeFactsView view = node_facts_view(facts, first);
    for (uint32_t i = 0; i < batched; i += 1)
    {
        const uint32_t w = batch[i];
        const uint32_t a = w - left[w];
        const bool taken = mode == BUSTER_NODE_FACTS_LOADS ? fits[w] != 0 && node_facts_dereference_load(records, &view, w, a)
                                                           : node_facts_dereference_fast(records, &view, w, a, mode == BUSTER_NODE_FACTS_LEAN);
        if (__builtin_expect(!taken, 0))
        {
            rare[rare_count] = w;
            rare_count += 1;
        }
    }
    node_facts_slow_each(facts, first, rare_count);
}

static inline __attribute__((always_inline)) void node_facts_subscripts_of(NodeFacts* facts, uint32_t first, uint32_t batched, uint32_t mode)
{
    NodeFactsRecord* const records = facts->records;
    const uint32_t* const batch = facts->batch;
    uint32_t* const rare = facts->rare;
    uint32_t rare_count = 0;
    const uint16_t* const left = facts->left + first;
    const uint16_t* const right = facts->right + first;
    const uint8_t* const fits = facts->fits + first;
    const NodeFactsView view = node_facts_view(facts, first);
    for (uint32_t i = 0; i < batched; i += 1)
    {
        const uint32_t w = batch[i];
        const uint32_t a = w - left[w];
        const bool taken = right[w] != 0 && (mode == BUSTER_NODE_FACTS_LOADS ? fits[w] != 0 && node_facts_subscript_load(records, &view, w, a)
                                                                             : node_facts_subscript_fast(records, &view, w, a, w - right[w], mode == BUSTER_NODE_FACTS_LEAN));
        if (__builtin_expect(!taken, 0))
        {
            rare[rare_count] = w;
            rare_count += 1;
        }
    }
    node_facts_slow_each(facts, first, rare_count);
}

static inline __attribute__((always_inline)) void node_facts_calls_of(NodeFacts* facts, uint32_t first, uint32_t batched, uint32_t mode)
{
    NodeFactsRecord* const records = facts->records;
    const uint32_t* const batch = facts->batch;
    uint32_t* const rare = facts->rare;
    uint32_t rare_count = 0;
    const uint16_t* const left = facts->left + first;
    const uint8_t* const fits = facts->fits + first;
    const NodeFactsView view = node_facts_view(facts, first);
    for (uint32_t i = 0; i < batched; i += 1)
    {
        const uint32_t w = batch[i];
        const uint32_t a = w - left[w];
        const bool taken = mode == BUSTER_NODE_FACTS_LOADS ? fits[w] != 0 && node_facts_call_load(records, &view, w, a)
                                                           : node_facts_call_fast(records, &view, w, a, mode == BUSTER_NODE_FACTS_LEAN);
        if (__builtin_expect(!taken, 0))
        {
            rare[rare_count] = w;
            rare_count += 1;
        }
    }
    node_facts_slow_each(facts, first, rare_count);
}

static __attribute__((noinline)) void node_facts_arrows(NodeFacts* facts, uint32_t first, uint32_t batched)
{
    node_facts_arrows_of(facts, first, batched, BUSTER_NODE_FACTS_WHOLE);
}

static __attribute__((noinline)) void node_facts_arrows_lean(NodeFacts* facts, uint32_t first, uint32_t batched)
{
    node_facts_arrows_of(facts, first, batched, BUSTER_NODE_FACTS_LEAN);
}

static __attribute__((noinline)) void node_facts_arrows_loads(NodeFacts* facts, uint32_t first, uint32_t batched)
{
    node_facts_arrows_of(facts, first, batched, BUSTER_NODE_FACTS_LOADS);
}

static __attribute__((noinline)) void node_facts_dots(NodeFacts* facts, uint32_t first, uint32_t batched)
{
    node_facts_dots_of(facts, first, batched, BUSTER_NODE_FACTS_WHOLE);
}

static __attribute__((noinline)) void node_facts_dots_lean(NodeFacts* facts, uint32_t first, uint32_t batched)
{
    node_facts_dots_of(facts, first, batched, BUSTER_NODE_FACTS_LEAN);
}

static __attribute__((noinline)) void node_facts_dots_loads(NodeFacts* facts, uint32_t first, uint32_t batched)
{
    node_facts_dots_of(facts, first, batched, BUSTER_NODE_FACTS_LOADS);
}

static __attribute__((noinline)) void node_facts_dereferences(NodeFacts* facts, uint32_t first, uint32_t batched)
{
    node_facts_dereferences_of(facts, first, batched, BUSTER_NODE_FACTS_WHOLE);
}

static __attribute__((noinline)) void node_facts_dereferences_lean(NodeFacts* facts, uint32_t first, uint32_t batched)
{
    node_facts_dereferences_of(facts, first, batched, BUSTER_NODE_FACTS_LEAN);
}

static __attribute__((noinline)) void node_facts_dereferences_loads(NodeFacts* facts, uint32_t first, uint32_t batched)
{
    node_facts_dereferences_of(facts, first, batched, BUSTER_NODE_FACTS_LOADS);
}

static __attribute__((noinline)) void node_facts_subscripts(NodeFacts* facts, uint32_t first, uint32_t batched)
{
    node_facts_subscripts_of(facts, first, batched, BUSTER_NODE_FACTS_WHOLE);
}

static __attribute__((noinline)) void node_facts_subscripts_lean(NodeFacts* facts, uint32_t first, uint32_t batched)
{
    node_facts_subscripts_of(facts, first, batched, BUSTER_NODE_FACTS_LEAN);
}

static __attribute__((noinline)) void node_facts_subscripts_loads(NodeFacts* facts, uint32_t first, uint32_t batched)
{
    node_facts_subscripts_of(facts, first, batched, BUSTER_NODE_FACTS_LOADS);
}

static __attribute__((noinline)) void node_facts_calls(NodeFacts* facts, uint32_t first, uint32_t batched)
{
    node_facts_calls_of(facts, first, batched, BUSTER_NODE_FACTS_WHOLE);
}

static __attribute__((noinline)) void node_facts_calls_lean(NodeFacts* facts, uint32_t first, uint32_t batched)
{
    node_facts_calls_of(facts, first, batched, BUSTER_NODE_FACTS_LEAN);
}

static __attribute__((noinline)) void node_facts_calls_loads(NodeFacts* facts, uint32_t first, uint32_t batched)
{
    node_facts_calls_of(facts, first, batched, BUSTER_NODE_FACTS_LOADS);
}

static __attribute__((noinline)) void node_facts_discards(NodeFacts* facts, uint32_t first, uint32_t batched)
{
    NodeFactsRecord* const records = facts->records;
    const uint32_t* const batch = facts->batch;
    uint32_t* const rare = facts->rare;
    uint32_t rare_count = 0;
    const uint16_t* const left = facts->left + first;
    const bool plain = facts->machine->graph->overload_candidate_count == 0;
    const NodeFactsView view = node_facts_view(facts, first);
    for (uint32_t i = 0; i < batched; i += 1)
    {
        const uint32_t w = batch[i];
        if (__builtin_expect(!node_facts_discard_fast(records, &view, plain, w, w - left[w]), 0))
        {
            rare[rare_count] = w;
            rare_count += 1;
        }
    }
    node_facts_slow_each(facts, first, rare_count);
}

// The kernel over window `window`, whose given records node_facts_given has put in place, as variant `mode` (WHOLE,
// LEAN or LOADS): its identifiers' leaves and its lookups listed (node_facts_list); the leaves, each from its
// declaration's line; then the lookups a class at a time, the lower levels first: each class's lookups listed
// (node_facts_batch) and made by its operator's short path, or its rule. A class is one loop over one operator's
// lookups, where a dispatch a lookup would be a branch the predictor misses most of the time. With `split`, the two
// parts' TSC ticks are added to the counts (three reads of the counter a window, which a pass that is counted whole
// leaves out).
static __attribute__((noinline)) void node_facts_window(NodeFacts* facts, uint32_t window, uint32_t mode, bool split)
{
    const uint32_t first = facts->windows[window];
    const uint32_t count = facts->windows[window + 1] - first;
    const bool whole = mode == BUSTER_NODE_FACTS_WHOLE;
    const bool lean = mode == BUSTER_NODE_FACTS_LEAN;
    uint32_t leaves = 0;
    uint32_t lookups = 0;
    const uint64_t t0 = split ? __rdtsc() : 0;
    node_facts_list(facts->leaf_list, facts->lookup_list, facts->keys, facts->kinds + first, facts->classes + first, count, &leaves, &lookups);
    if (whole)
    {
        node_facts_leaves(facts, first, leaves);
    }
    else
    {
        node_facts_leaves_lean(facts, first, leaves);
    }
    const uint64_t t1 = split ? __rdtsc() : 0;
    uint64_t present = node_facts_present(facts->keys, lookups);
    const bool deep = (present >> BUSTER_NODE_FACTS_DEEP & 1) != 0;
    present &= ~(UINT64_C(1) << BUSTER_NODE_FACTS_DEEP);
    while (present != 0)
    {
        const uint32_t key = (uint32_t)__builtin_ctzll(present);
        const uint32_t batched = node_facts_batch(facts->batch, facts->lookup_list, facts->keys, lookups, (uint8_t)key);
        present &= present - 1;
        switch (key & 7u)
        {
            case 0:
            {
                if (whole)
                {
                    node_facts_arrows(facts, first, batched);
                }
                else if (lean)
                {
                    node_facts_arrows_lean(facts, first, batched);
                }
                else
                {
                    node_facts_arrows_loads(facts, first, batched);
                }
            } break;
            case 1:
            {
                if (whole)
                {
                    node_facts_dots(facts, first, batched);
                }
                else if (lean)
                {
                    node_facts_dots_lean(facts, first, batched);
                }
                else
                {
                    node_facts_dots_loads(facts, first, batched);
                }
            } break;
            case 2:
            {
                if (whole)
                {
                    node_facts_dereferences(facts, first, batched);
                }
                else if (lean)
                {
                    node_facts_dereferences_lean(facts, first, batched);
                }
                else
                {
                    node_facts_dereferences_loads(facts, first, batched);
                }
            } break;
            case 3:
            {
                if (whole)
                {
                    node_facts_subscripts(facts, first, batched);
                }
                else if (lean)
                {
                    node_facts_subscripts_lean(facts, first, batched);
                }
                else
                {
                    node_facts_subscripts_loads(facts, first, batched);
                }
            } break;
            case 4:
            {
                if (whole)
                {
                    node_facts_calls(facts, first, batched);
                }
                else if (lean)
                {
                    node_facts_calls_lean(facts, first, batched);
                }
                else
                {
                    node_facts_calls_loads(facts, first, batched);
                }
            } break;
            case 5:
            {
                node_facts_discards(facts, first, batched);
            } break;
            default:
            {
            } break;
        }
    }
    // The lookups past the levels that have classes, up the postorder: each by its rule.
    for (uint32_t i = 0; deep && i < lookups; i += 1)
    {
        if (facts->keys[i] == BUSTER_NODE_FACTS_DEEP)
        {
            node_facts_slow(facts, first, facts->lookup_list[i]);
        }
    }
    const uint64_t t2 = split ? __rdtsc() : 0;
    facts->leaf_ticks += t1 - t0;
    facts->lookup_ticks += t2 - t1;
    facts->leaf_count = leaves;
    facts->lookup_count = lookups;
}

// Whether the lists the kernel made of window `window`, which it ran last, are its records' of each kind in their order,
// and each lookup's class its level's and its operator's, the levels made again from the operands' here: the listing's
// twin, a record at a time.
static bool node_facts_lists_same(const NodeFacts* facts, uint32_t window)
{
    const uint32_t first = facts->windows[window];
    const uint32_t count = facts->windows[window + 1] - first;
    uint8_t levels[BUSTER_NODE_FACTS_WINDOW];
    uint32_t leaves = 0;
    uint32_t lookups = 0;
    bool same = count <= BUSTER_NODE_FACTS_WINDOW;
    for (uint32_t w = 0; same && w < count; w += 1)
    {
        const uint8_t kind = facts->kinds[first + w];
        levels[w] = 0;
        if (kind == BUSTER_NODE_FACTS_IDENTIFIER)
        {
            same = leaves < facts->leaf_count && facts->leaf_list[leaves] == w;
            leaves += 1;
        }
        else if (kind == BUSTER_NODE_FACTS_LOOKUP)
        {
            uint32_t level = levels[w - facts->left[first + w]];
            const uint32_t second = facts->right[first + w] != 0 ? levels[w - facts->right[first + w]] : 0;
            level = second > level ? second : level;
            for (uint32_t k = 0; facts->ops[first + w] == CHECK_OP_CALL && k < facts->counts[first + w]; k += 1)
            {
                const uint32_t argument = levels[w - facts->arguments[facts->inputs[first + w] + k]];
                level = argument > level ? argument : level;
            }
            const bool deep = level >= BUSTER_NODE_FACTS_LEVELS;
            const uint32_t key = deep ? BUSTER_NODE_FACTS_DEEP : level * 8 + node_facts_lookup((CheckOp)facts->ops[first + w]);
            levels[w] = (uint8_t)(deep ? BUSTER_NODE_FACTS_LEVELS : level + 1);
            same = lookups < facts->lookup_count && facts->lookup_list[lookups] == w && facts->keys[lookups] == key;
            lookups += 1;
        }
    }
    return same && leaves == facts->leaf_count && lookups == facts->lookup_count;
}

// Whether the kernel's record `w` of the window whose first record is `base` is T's, in what variant `mode` makes of
// it: every column T writes of a record (and its exponent where its payload is a floating value's) for the whole
// kernel, and for any record its rule made or T's own; the record's first half and a member's offset for the lean
// kernel; and of a lookup its type, its word and a member's offset for the loads' kernel.
static bool node_facts_same(const NodeFacts* facts, uint32_t base, uint32_t w, uint32_t mode)
{
    const NodeFactsRecord* record = facts->records + w;
    const NodeFactsRecord expected = node_facts_expected(facts, (uint64_t)base + w);
    const CheckOp op = (CheckOp)facts->ops[base + w];
    const bool lookup = facts->kinds[base + w] == BUSTER_NODE_FACTS_LOOKUP;
    const bool member = lookup && (op == CHECK_OP_ARROW || op == CHECK_OP_MEMBER);
    const bool floating = (expected.standing & BUSTER_NODE_MACHINE_FIELD) == BUSTER_NODE_MACHINE_FLOATING;
    const bool loaded = record->type == expected.type && record->word == expected.word;
    const bool half = loaded && record->flags == expected.flags && record->object == expected.object && record->payload == expected.payload &&
                      record->standing == expected.standing && (!floating || record->exponent == expected.exponent);
    const bool rest = record->converted[0] == expected.converted[0] && record->converted[1] == expected.converted[1] &&
                      record->conversion_flags[0] == expected.conversion_flags[0] && record->conversion_flags[1] == expected.conversion_flags[1];
    const bool offset = record->member_offset == expected.member_offset;
    const bool whole = mode == BUSTER_NODE_FACTS_WHOLE || record->taken != BUSTER_NODE_FACTS_SHORT;
    return whole ? half && rest && offset : mode == BUSTER_NODE_FACTS_LOADS && lookup ? loaded && (!member || offset) : half && (!member || offset);
}

// What a lean kernel leaves out of record `w` of the window it ran last, made here by the record's rule from the
// kernel's own records of its operands, for the comparison alone: a lookup's two conversions' entries (C's, a
// function of the operator, its operands' records and their types' rows), or with `discard` a discard's whole record
// (a function of its operand's). Whether the rule takes it and what it makes is T's. The record is left as it was.
static bool node_facts_derived(NodeFacts* facts, uint32_t first, uint32_t w, bool discard)
{
    const NodeFactsRecord kept = facts->records[w];
    const NodeFactsRecord expected = node_facts_expected(facts, (uint64_t)first + w);
    memset(facts->records + w, 0xa5, sizeof(NodeFactsRecord));
    node_facts_slow(facts, first, w);
    const NodeFactsRecord* made = facts->records + w;
    const bool conversions = made->converted[0] == expected.converted[0] && made->converted[1] == expected.converted[1] &&
                             made->conversion_flags[0] == expected.conversion_flags[0] && made->conversion_flags[1] == expected.conversion_flags[1];
    const bool derived = made->taken == BUSTER_NODE_FACTS_RULED && conversions && (!discard || node_facts_same(facts, first, w, BUSTER_NODE_FACTS_WHOLE));
    facts->records[w] = kept;
    return derived;
}

#endif
