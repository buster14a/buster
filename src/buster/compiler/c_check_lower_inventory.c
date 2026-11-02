// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #1048's inventory: how much of the lowering a table of node templates would cover. Include after
// c_lowering_source_benchmark.c.
//
//   build/compiler measure-lowering-templates [--top N] [--keys N] [--abstracts N] [--ignore-tracking]
//                                             [--allow CATEGORY]... [preprocessor options] file...
//
// Each file is checked, its static data and checked program prepared and its definitions selected as -c selects them
// (C_LOWERING_REACHABLE), then lowered by the tasks' loop with each entry's owner (check_program_lower_owners): the
// expression node whose task wrote it, the node whose value a conversion task converts for its consumer (marked), or a
// control or a token outside the expressions; and lowered again without the owners, which must give every function the
// same entries. Every EXPRESSION task the loop popped is a node occurrence, with its context (the task's address,
// initialized, discard and bit_int flags), its conversion's target and the node that pushed it.
//
// A node's key is what a classifier would read from the columns: its op and context, its type's class and facts
// (check_lower_type_class, check_lower_type_facts: #570's table), whether it is a folded constant and of which kind, its
// category, its value's flags that change the lowering (a bit-field, a vector element, an overload), a VALUE's or a
// constant address's object's kind (a symbol, a compound literal, static) and whether the constant's offset is past the
// object's start, whether a member's offset is past its first byte, its first two operands' types, its conversion's
// target, and a store's destination's kind. A node's pattern is its own entries' kinds and classes, its conversion's
// marked apart, interleaved with its children's subtrees ('C'): the template a key must give. A key is deterministic
// where every node of it has one pattern; its abstract pattern writes each class as the node's, an operand's or the
// conversion's target's, as a table whose nodes take their classes from their columns would hold it.
//
// A node is an exception, by the issue's list, where its body tracks its stack, it was popped more than once, or it is a
// call, a builtin or va_arg, a logical operator or a conditional, a statement expression, a variable-length array's
// bound, extent or size, a bit-field's access, or of a complex, _BitInt, atomic, vector or half-precision type. The other
// nodes are ordinary: the table's candidates, by their keys' node counts. --allow CATEGORY counts a category as
// ordinary, and --ignore-tracking classifies a tracked body's nodes by the other rules.
//
// The report gives the maximal subtrees of nodes in deterministic keys (how many, their nodes, by size, and the
// statements' expressions they cover whole), the abstract patterns' coverage in nodes and entries, the totals (bodies,
// node occurrences, entries by owner), each exception category's nodes, own entries and the most of one node, the
// coverage of the first N deterministic keys for N up to --keys (64 by default) with the covered nodes whose own entries
// come before a child's (not plain postfix), the bodies without an exception and their share of the entries; then the
// --top (40) largest keys with their patterns, the largest undetermined keys with theirs, and with --abstracts N the
// first N abstract patterns.
//
// This is a measurement outside the compile path: it runs the scalar task loop, and has no vector path of its own.
#ifndef BUSTER_C_CHECK_LOWER_INVENTORY_INCLUDED
#define BUSTER_C_CHECK_LOWER_INVENTORY_INCLUDED

#define BUSTER_LOWER_INVENTORY_KEY_BYTES 32u
#define BUSTER_LOWER_INVENTORY_PATTERNS 4u
#define BUSTER_LOWER_INVENTORY_PATTERN_BYTES 96u
#define BUSTER_LOWER_INVENTORY_TABLE (UINT32_C(1) << 16)

typedef enum LowerInventoryCategory
{
    LOWER_INVENTORY_ORDINARY,
    LOWER_INVENTORY_TRACKED,
    LOWER_INVENTORY_REPOPPED,
    LOWER_INVENTORY_CALL,
    LOWER_INVENTORY_BUILTIN,
    LOWER_INVENTORY_LOGICAL,
    LOWER_INVENTORY_STATEMENT,
    LOWER_INVENTORY_VLA,
    LOWER_INVENTORY_BIT_FIELD,
    LOWER_INVENTORY_TYPE,
    LOWER_INVENTORY_CATEGORIES,
} LowerInventoryCategory;

static const char* const lower_inventory_category_names[LOWER_INVENTORY_CATEGORIES] = {
    [LOWER_INVENTORY_ORDINARY] = "ordinary",
    [LOWER_INVENTORY_TRACKED] = "tracked-body",
    [LOWER_INVENTORY_REPOPPED] = "popped-again",
    [LOWER_INVENTORY_CALL] = "call",
    [LOWER_INVENTORY_BUILTIN] = "builtin-or-va_arg",
    [LOWER_INVENTORY_LOGICAL] = "&&-||-?:",
    [LOWER_INVENTORY_STATEMENT] = "statement-expression",
    [LOWER_INVENTORY_VLA] = "vla",
    [LOWER_INVENTORY_BIT_FIELD] = "bit-field",
    [LOWER_INVENTORY_TYPE] = "complex-bitint-atomic-vector-half",
};

static const char* const lower_inventory_op_names[CHECK_OP_COUNT] = {
    [CHECK_OP_VALUE] = "value", [CHECK_OP_POSITIVE] = "+x", [CHECK_OP_NEGATIVE] = "-x", [CHECK_OP_NOT] = "!", [CHECK_OP_BIT_NOT] = "~",
    [CHECK_OP_ADD] = "+", [CHECK_OP_SUBTRACT] = "-", [CHECK_OP_MULTIPLY] = "*", [CHECK_OP_DIVIDE] = "/", [CHECK_OP_REMAINDER] = "%",
    [CHECK_OP_SHIFT_LEFT] = "<<", [CHECK_OP_SHIFT_RIGHT] = ">>", [CHECK_OP_LESS] = "<", [CHECK_OP_LESS_EQUAL] = "<=", [CHECK_OP_GREATER] = ">",
    [CHECK_OP_GREATER_EQUAL] = ">=", [CHECK_OP_EQUAL] = "==", [CHECK_OP_NOT_EQUAL] = "!=", [CHECK_OP_BIT_AND] = "&", [CHECK_OP_BIT_OR] = "|",
    [CHECK_OP_BIT_XOR] = "^", [CHECK_OP_LOGICAL_AND] = "&&", [CHECK_OP_LOGICAL_OR] = "||", [CHECK_OP_CONDITIONAL] = "?:", [CHECK_OP_CAST] = "cast",
    [CHECK_OP_ADDRESS] = "&x", [CHECK_OP_DEREFERENCE] = "*x", [CHECK_OP_ASSIGN] = "=", [CHECK_OP_COMMA] = ",", [CHECK_OP_SUBSCRIPT] = "[]",
    [CHECK_OP_SIZEOF] = "sizeof", [CHECK_OP_ALIGNOF] = "alignof", [CHECK_OP_MEMBER] = ".", [CHECK_OP_ARROW] = "->", [CHECK_OP_CALL] = "call",
    [CHECK_OP_INITIALIZE] = "initialize", [CHECK_OP_CONVERT] = "convert", [CHECK_OP_CONDITION] = "condition", [CHECK_OP_PRE_INCREMENT] = "++x",
    [CHECK_OP_PRE_DECREMENT] = "--x", [CHECK_OP_POST_INCREMENT] = "x++", [CHECK_OP_POST_DECREMENT] = "x--", [CHECK_OP_ADD_ASSIGN] = "+=",
    [CHECK_OP_SUBTRACT_ASSIGN] = "-=", [CHECK_OP_MULTIPLY_ASSIGN] = "*=", [CHECK_OP_DIVIDE_ASSIGN] = "/=", [CHECK_OP_REMAINDER_ASSIGN] = "%=",
    [CHECK_OP_SHIFT_LEFT_ASSIGN] = "<<=", [CHECK_OP_SHIFT_RIGHT_ASSIGN] = ">>=", [CHECK_OP_BIT_AND_ASSIGN] = "&=", [CHECK_OP_BIT_OR_ASSIGN] = "|=",
    [CHECK_OP_BIT_XOR_ASSIGN] = "^=", [CHECK_OP_SELECT] = "select", [CHECK_OP_BUILTIN_UNARY] = "builtin-unary",
    [CHECK_OP_BUILTIN_OVERFLOW] = "builtin-overflow", [CHECK_OP_STATEMENT_VALUE] = "statement-value", [CHECK_OP_VARARGS] = "va_arg",
    [CHECK_OP_VLA_BOUND] = "vla-bound", [CHECK_OP_VLA_EXTENT] = "vla-extent", [CHECK_OP_SIZEOF_VLA] = "sizeof-vla",
    [CHECK_OP_BUILTIN_MISC] = "builtin-misc", [CHECK_OP_BUILTIN_FLOAT] = "builtin-float", [CHECK_OP_BUILTIN_QUERY] = "builtin-query",
    [CHECK_OP_BUILTIN_ATOMIC] = "builtin-atomic", [CHECK_OP_REAL] = "__real", [CHECK_OP_IMAGINARY] = "__imag",
    [CHECK_OP_BUILTIN_VECTOR] = "builtin-vector", [CHECK_OP_BUILTIN_X86] = "builtin-x86", [CHECK_OP_BUILTIN_BIT_CAST] = "builtin-bit-cast",
};

static const char* const lower_inventory_kind_names[BUSTER_TYPED_KINDS] = {
    "none", "const", "local", "local-address", "global", "global-address", "dup", "neg", "not", "convert", "load", "add", "sub", "mul",
    "div", "mod", "and", "or", "xor", "shl", "shr", "ptr-add", "eq", "ne", "lt", "le", "gt", "ge", "store", "set", "store-local",
    "set-local", "store-global", "set-global", "call", "discard", "branch-false", "branch-true", "jump", "jump-value", "return", "copy",
    "alloca", "stack-save", "stack-restore", "frame-address", "jump-indirect", "tail-call", "x86",
};

static const char* const lower_inventory_class_names[BUSTER_TYPED_CLASS_LIMIT] = {
    "void", "i8", "u8", "i16", "u16", "i32", "u32", "i64", "u64", "f32", "f64", "f80", "i128", "u128", "vector", "mask",
};

// One key's nodes: their count, own entries (the conversions they asked of their operands among them), the patterns
// they had (up to BUSTER_LOWER_INVENTORY_PATTERNS of them, each with its nodes, and how many nodes had another), and
// the key's bytes and its first node's pattern, for the report.
typedef struct LowerInventoryKey LowerInventoryKey;
struct LowerInventoryKey
{
    uint64_t hash;
    uint64_t nodes;
    uint64_t entries;
    uint64_t pattern_hashes[BUSTER_LOWER_INVENTORY_PATTERNS];
    uint64_t pattern_nodes[BUSTER_LOWER_INVENTORY_PATTERNS];
    uint64_t other_patterns;
    uint64_t prefixed;
    uint32_t pattern_count;
    uint8_t key[BUSTER_LOWER_INVENTORY_KEY_BYTES];
    uint8_t patterns[BUSTER_LOWER_INVENTORY_PATTERNS][BUSTER_LOWER_INVENTORY_PATTERN_BYTES];
    uint32_t pattern_lengths[BUSTER_LOWER_INVENTORY_PATTERNS];
    bool used;
};

typedef struct LowerInventory LowerInventory;
struct LowerInventory
{
    LowerInventoryKey* keys;
    uint32_t key_count;
    uint64_t files;
    uint64_t bodies;
    uint64_t pops;
    uint64_t entries;
    uint64_t entries_nodes;
    uint64_t entries_conversions;
    uint64_t entries_controls;
    uint64_t entries_tokens;
    uint64_t category_nodes[LOWER_INVENTORY_CATEGORIES];
    uint64_t category_entries[LOWER_INVENTORY_CATEGORIES];
    uint64_t category_most[LOWER_INVENTORY_CATEGORIES];
    uint64_t tracked_nodes;
    uint64_t clean_bodies;
    uint64_t clean_entries;
    uint64_t clean_nodes;
    uint64_t long_patterns;
    uint64_t strays;
    uint64_t failures;
    // Every node's first pop, over the files, in pop order: its key's slot (BUSTER_CHECK_NONE for an exception), its
    // parent's index here (BUSTER_CHECK_NONE for a statement's expression), and its own entries.
    uint32_t* node_keys;
    uint32_t* node_parents;
    uint32_t* node_own;
    uint64_t node_count;
    uint64_t node_capacity;
};

// Room for `more` nodes in the inventory's columns: whether there is.
static bool lower_inventory_room(LowerInventory* inventory, uint64_t more)
{
    bool valid = true;
    if (inventory->node_count + more > inventory->node_capacity)
    {
        const uint64_t capacity = 2 * (inventory->node_count + more);
        uint32_t* keys = realloc(inventory->node_keys, capacity * sizeof(uint32_t));
        inventory->node_keys = keys != NULL ? keys : inventory->node_keys;
        uint32_t* parents = realloc(inventory->node_parents, capacity * sizeof(uint32_t));
        inventory->node_parents = parents != NULL ? parents : inventory->node_parents;
        uint32_t* own = realloc(inventory->node_own, capacity * sizeof(uint32_t));
        inventory->node_own = own != NULL ? own : inventory->node_own;
        valid = keys != NULL && parents != NULL && own != NULL;
        inventory->node_capacity = valid ? capacity : inventory->node_capacity;
    }
    return valid;
}

static uint64_t lower_inventory_hash(const uint8_t* bytes, uint32_t length)
{
    uint64_t hash = UINT64_C(0xcbf29ce484222325);
    for (uint32_t i = 0; i < length; i += 1)
    {
        hash = (hash ^ bytes[i]) * UINT64_C(0x100000001b3);
    }
    return hash ^ (hash >> 29);
}

// The class and facts of a type, as the classifier's table (#570) would give them, in two bytes.
static void lower_inventory_type(uint8_t* key, const TypeTable* types, TypeId type)
{
    key[0] = type != 0 ? check_lower_type_class(types, type) : UINT8_MAX;
    key[1] = type != 0 ? check_lower_type_facts(types, type) : 0;
}

// The key of pop `pop` (its node's), in `key`'s BUSTER_LOWER_INVENTORY_KEY_BYTES: its op, context, type, constant kind,
// category and flags; a VALUE's or a constant address's object's kind and whether the constant's offset is past its
// object's start; whether a member's offset is past its record's start; its first two operands' types; the type the
// conversion its consumer asked of it converts to (its pop's target); and a store's destination.
static void lower_inventory_key(uint8_t* key, const CheckProgram* program, const CheckLowerOwnerPop* pop)
{
    const uint32_t node = pop->node;
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const TypeTable* types = program->types;
    const CheckValue value = check_stream_value(stream, node);
    const CheckOp op = graph->ops[node];
    const bool constant = value.constant_kind != CHECK_CONSTANT_NONE && value.fault == CHECK_FAULT_NONE &&
                          (value.flags & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_FP_ENVIRONMENT)) == 0;
    const uint32_t object = value.object;
    const bool known = object != 0 && object < program->object_count;
    const bool addressed = op == CHECK_OP_VALUE || (constant && value.constant_kind == CHECK_CONSTANT_ADDRESS);
    memset(key, 0, BUSTER_LOWER_INVENTORY_KEY_BYTES);
    key[0] = (uint8_t)op;
    key[1] = pop->flags & 0x17u;
    lower_inventory_type(key + 2, types, value.type);
    key[4] = constant ? (uint8_t)value.constant_kind : 0;
    key[5] = (uint8_t)value.category;
    key[6] = (uint8_t)(((value.flags & BUSTER_CHECK_BIT_FIELD) != 0 ? 1u : 0u) | ((value.flags & BUSTER_CHECK_VECTOR_ELEMENT) != 0 ? 2u : 0u) |
                       (value.overload_head != 0 ? 4u : 0u));
    key[7] = (uint8_t)(!addressed || !known ? 0u :
                       (program->object_symbols[object] != 0 ? 1u : 2u) | (program->input->compound_objects[object - 1] ? 4u : 0u) |
                           (program->input->static_objects[object - 1] ? 8u : 0u) | (constant && value.offset != 0 ? 16u : 0u));
    key[8] = (uint8_t)((op == CHECK_OP_MEMBER || op == CHECK_OP_ARROW) && stream->member_offsets[node] / 8 != 0 ? 1u : 0u);
    const uint32_t operands[2] = { graph->operand0[node], graph->operand1[node] };
    for (uint32_t i = 0; op != CHECK_OP_VALUE && i < 2; i += 1)
    {
        lower_inventory_type(key + 9 + 2 * i, types, operands[i] < stream->count ? check_stream_type(stream, operands[i]) : 0);
    }
    lower_inventory_type(key + 13, types, pop->target);
    // A store's destination: a VALUE's object's kind, which decides STORE_LOCAL, STORE_GLOBAL or a store through its
    // address, and 0xff for any other destination.
    const bool store = op == CHECK_OP_ASSIGN || (op >= CHECK_OP_PRE_INCREMENT && op <= CHECK_OP_BIT_XOR_ASSIGN);
    const uint32_t destination = store && operands[0] < stream->count && graph->ops[operands[0]] == CHECK_OP_VALUE ? check_stream_object(stream, operands[0]) : 0;
    key[17] = (uint8_t)(!store ? 0u : destination == 0 || destination >= program->object_count ? 0xffu :
                                       (program->object_symbols[destination] != 0 ? 1u : 2u) | (program->input->static_objects[destination - 1] ? 8u : 0u));
}

// Whether a body that tracks its stack (CheckLowerWriter.track) is classified by its nodes as any other is
// (--ignore-tracking): every body of a program with a terminal helper tracks it, whether or not it calls one.
static bool lower_inventory_ignore_tracking;
// The exception categories classified as ordinary nodes (--allow CATEGORY), by their bits.
static uint32_t lower_inventory_allowed;
// How many abstract patterns the report lists (--abstracts N).
static uint32_t lower_inventory_print_abstracts;

// The exception category of `node`, popped with `flags` (BUSTER_CHECK_LOWER_OWNER_TRACKED among them), `repopped` when
// the body's loop popped it more than once.
static LowerInventoryCategory lower_inventory_category(const CheckProgram* program, uint32_t node, uint8_t flags, bool repopped)
{
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const TypeTable* types = program->types;
    const CheckOp op = graph->ops[node];
    const TypeId type = check_stream_type(stream, node);
    const uint8_t class = check_lower_type_class(types, type);
    const uint8_t facts = check_lower_type_facts(types, type);
    const uint32_t a = graph->operand0[node];
    const bool update = op == CHECK_OP_ASSIGN || (op >= CHECK_OP_PRE_INCREMENT && op <= CHECK_OP_BIT_XOR_ASSIGN);
    const bool bit_field = (check_stream_flags(stream, node) & BUSTER_CHECK_BIT_FIELD) != 0 ||
                           (update && a < stream->count && (check_stream_flags(stream, a) & BUSTER_CHECK_BIT_FIELD) != 0);
    const TypeId operand_type = a < stream->count && op != CHECK_OP_VALUE ? check_stream_type(stream, a) : 0;
    const uint8_t operand_facts = operand_type != 0 ? check_lower_type_facts(types, operand_type) : 0;
    const uint8_t operand_class = operand_type != 0 ? check_lower_type_class(types, operand_type) : BUSTER_TYPED_CLASS_VOID;
    const uint8_t special = BUSTER_CHECK_LOWER_TYPE_COMPLEX | BUSTER_CHECK_LOWER_TYPE_ATOMIC | BUSTER_CHECK_LOWER_TYPE_BIT_INT;
    LowerInventoryCategory category = LOWER_INVENTORY_ORDINARY;
    if ((flags & BUSTER_CHECK_LOWER_OWNER_TRACKED) != 0 && !lower_inventory_ignore_tracking)
    {
        category = LOWER_INVENTORY_TRACKED;
    }
    else if (repopped)
    {
        category = LOWER_INVENTORY_REPOPPED;
    }
    else if (op == CHECK_OP_CALL)
    {
        category = LOWER_INVENTORY_CALL;
    }
    else if (op == CHECK_OP_BUILTIN_UNARY || op == CHECK_OP_BUILTIN_OVERFLOW || op == CHECK_OP_VARARGS || (op >= CHECK_OP_BUILTIN_MISC && op <= CHECK_OP_BUILTIN_ATOMIC) ||
             op >= CHECK_OP_BUILTIN_VECTOR)
    {
        category = LOWER_INVENTORY_BUILTIN;
    }
    else if (op == CHECK_OP_LOGICAL_AND || op == CHECK_OP_LOGICAL_OR || op == CHECK_OP_CONDITIONAL)
    {
        category = LOWER_INVENTORY_LOGICAL;
    }
    else if (op == CHECK_OP_STATEMENT_VALUE)
    {
        category = LOWER_INVENTORY_STATEMENT;
    }
    else if (op == CHECK_OP_VLA_BOUND || op == CHECK_OP_VLA_EXTENT || op == CHECK_OP_SIZEOF_VLA)
    {
        category = LOWER_INVENTORY_VLA;
    }
    else if (bit_field)
    {
        category = LOWER_INVENTORY_BIT_FIELD;
    }
    else if ((facts & special) != 0 || (operand_facts & special) != 0 || (class >= BUSTER_TYPED_CLASS_VECTOR && class != UINT8_MAX) ||
             (operand_class >= BUSTER_TYPED_CLASS_VECTOR && operand_class != UINT8_MAX))
    {
        category = LOWER_INVENTORY_TYPE;
    }
    return (lower_inventory_allowed & (1u << category)) != 0 ? LOWER_INVENTORY_ORDINARY : category;
}

static LowerInventoryKey* lower_inventory_find(LowerInventory* inventory, const uint8_t* key)
{
    const uint64_t hash = lower_inventory_hash(key, BUSTER_LOWER_INVENTORY_KEY_BYTES);
    uint32_t slot = (uint32_t)hash & (BUSTER_LOWER_INVENTORY_TABLE - 1);
    LowerInventoryKey* found = NULL;
    for (uint32_t probe = 0; found == NULL && probe < BUSTER_LOWER_INVENTORY_TABLE; probe += 1)
    {
        LowerInventoryKey* entry = inventory->keys + slot;
        if (!entry->used)
        {
            *entry = (LowerInventoryKey){ .hash = hash, .used = true };
            memcpy(entry->key, key, BUSTER_LOWER_INVENTORY_KEY_BYTES);
            inventory->key_count += 1;
            found = entry;
        }
        else if (entry->hash == hash && memcmp(entry->key, key, BUSTER_LOWER_INVENTORY_KEY_BYTES) == 0)
        {
            found = entry;
        }
        slot = (slot + 1) & (BUSTER_LOWER_INVENTORY_TABLE - 1);
    }
    return found;
}

// A key's node with `pattern`: its counts, and the pattern among the key's.
static void lower_inventory_count(LowerInventoryKey* entry, const uint8_t* pattern, uint32_t length, uint64_t own, bool prefixed)
{
    const uint64_t hash = lower_inventory_hash(pattern, length) ^ length;
    uint32_t found = BUSTER_LOWER_INVENTORY_PATTERNS;
    for (uint32_t i = 0; i < entry->pattern_count; i += 1)
    {
        found = entry->pattern_hashes[i] == hash ? i : found;
    }
    if (found == BUSTER_LOWER_INVENTORY_PATTERNS && entry->pattern_count < BUSTER_LOWER_INVENTORY_PATTERNS)
    {
        found = entry->pattern_count++;
        entry->pattern_hashes[found] = hash;
        memcpy(entry->patterns[found], pattern, length);
        entry->pattern_lengths[found] = length;
    }
    if (found != BUSTER_LOWER_INVENTORY_PATTERNS)
    {
        entry->pattern_nodes[found] += 1;
    }
    else
    {
        entry->other_patterns += 1;
    }
    entry->nodes += 1;
    entry->entries += own;
    entry->prefixed += prefixed;
}

// Per body: each node's first pop (`node_pop`, every entry BUSTER_CHECK_NONE between bodies), its subtree's range of
// entries (`low`, `high`), its own entries, how often it was popped, its parent pop, and its first child and next
// sibling in pop order.
typedef struct LowerInventoryScratch LowerInventoryScratch;
struct LowerInventoryScratch
{
    uint32_t* node_pop;
    uint32_t* low;
    uint32_t* high;
    uint32_t* own;
    uint32_t* times;
    uint32_t* parent;
    uint32_t* first_child;
    uint32_t* last_child;
    uint32_t* next_sibling;
    // Each first pop's index in the inventory's node columns.
    uint32_t* global;
};

// Pop `p`'s pattern (node `node`'s) into `pattern`, BUSTER_LOWER_INVENTORY_PATTERN_BYTES at most: its own entries ('K',
// kind, class), the conversions it asked of its operands ('V', kind, class), and each child's subtree once ('C'), in the
// entries' order; '?' for an entry of the subtree that no child's holds. Its length.
static uint32_t lower_inventory_pattern(LowerInventory* inventory, const LowerInventoryScratch* s, const Tier0Function* function, const uint32_t* owners,
                                        uint32_t p, uint32_t node, uint8_t* pattern)
{
    const uint32_t none = BUSTER_CHECK_NONE;
    uint32_t length = 0;
    bool fits = true;
    uint32_t child = s->first_child[p];
    for (uint32_t e = s->low[p]; s->low[p] != none && e <= s->high[p] && fits; e += 1)
    {
        const uint32_t owner = owners[e];
        const bool mine = (owner & BUSTER_CHECK_LOWER_OWNER_CONTROL) == 0 && (owner & ~BUSTER_CHECK_LOWER_OWNER_CONVERSION) == node;
        if (mine)
        {
            fits = length + 3 <= BUSTER_LOWER_INVENTORY_PATTERN_BYTES;
            if (fits)
            {
                pattern[length++] = (owner & BUSTER_CHECK_LOWER_OWNER_CONVERSION) != 0 ? 'V' : 'K';
                pattern[length++] = function->kinds[e];
                pattern[length++] = function->classes[e];
            }
        }
        else
        {
            while (child != none && (s->low[child] == none || s->high[child] < e))
            {
                child = s->next_sibling[child];
            }
            const bool held = child != none && s->low[child] <= e;
            fits = length + 1 <= BUSTER_LOWER_INVENTORY_PATTERN_BYTES;
            if (fits)
            {
                pattern[length++] = held ? 'C' : '?';
            }
            if (held)
            {
                e = s->high[child];
                child = s->next_sibling[child];
            }
            else
            {
                inventory->strays += 1;
            }
        }
    }
    if (!fits)
    {
        inventory->long_patterns += 1;
        length = BUSTER_LOWER_INVENTORY_PATTERN_BYTES;
    }
    return length;
}

// Whether a pattern has an own entry ('K', not an operand's conversion) before a child's subtree: a node whose entries
// aren't all after its operands', as a postfix's are.
static bool lower_inventory_prefixed(const uint8_t* pattern, uint32_t length)
{
    bool own = false;
    bool prefixed = false;
    uint32_t i = 0;
    while (i < length)
    {
        prefixed = prefixed || (own && pattern[i] == 'C');
        own = own || pattern[i] == 'K';
        i += pattern[i] == 'C' || pattern[i] == '?' ? 1 : 3;
    }
    return prefixed;
}

// Body `b`'s pops (`pops`, `count` of them) and entries, counted into `inventory`.
static void lower_inventory_body(LowerInventory* inventory, LowerInventoryScratch* s, const CheckProgram* program, const Tier0Function* function,
                                 const uint32_t* owners, const CheckLowerOwnerPop* pops, uint32_t count)
{
    const uint32_t none = BUSTER_CHECK_NONE;
    for (uint32_t p = 0; p < count; p += 1)
    {
        const uint32_t node = pops[p].node;
        s->times[p] = 0;
        s->own[p] = 0;
        s->low[p] = none;
        s->high[p] = 0;
        s->first_child[p] = none;
        s->last_child[p] = none;
        s->next_sibling[p] = none;
        s->parent[p] = none;
        if (s->node_pop[node] == none)
        {
            s->node_pop[node] = p;
        }
        s->times[s->node_pop[node]] += 1;
        const uint32_t pusher = pops[p].pusher;
        const uint32_t parent = (pusher & BUSTER_CHECK_LOWER_OWNER_CONTROL) == 0 ? s->node_pop[pusher & ~BUSTER_CHECK_LOWER_OWNER_CONVERSION] : none;
        // A pop again of a node is its first's: not a child of its own.
        if (s->node_pop[node] == p && parent != none && parent < p)
        {
            s->parent[p] = parent;
            if (s->last_child[parent] == none)
            {
                s->first_child[parent] = p;
            }
            else
            {
                s->next_sibling[s->last_child[parent]] = p;
            }
            s->last_child[parent] = p;
        }
    }
    uint64_t conversions = 0;
    uint64_t controls = 0;
    uint64_t tokens = 0;
    uint64_t nodes = 0;
    for (uint32_t e = 0; e < function->count; e += 1)
    {
        const uint32_t owner = owners[e];
        if ((owner & BUSTER_CHECK_LOWER_OWNER_TOKEN) == BUSTER_CHECK_LOWER_OWNER_TOKEN)
        {
            tokens += 1;
        }
        else if ((owner & BUSTER_CHECK_LOWER_OWNER_CONTROL) != 0)
        {
            controls += 1;
        }
        else
        {
            const uint32_t node = owner & ~BUSTER_CHECK_LOWER_OWNER_CONVERSION;
            const uint32_t p = node < program->stream->count ? s->node_pop[node] : none;
            conversions += (owner & BUSTER_CHECK_LOWER_OWNER_CONVERSION) != 0;
            nodes += (owner & BUSTER_CHECK_LOWER_OWNER_CONVERSION) == 0;
            if (p != none)
            {
                s->own[p] += 1;
                s->low[p] = e < s->low[p] ? e : s->low[p];
                s->high[p] = e > s->high[p] ? e : s->high[p];
            }
            else
            {
                inventory->strays += 1;
            }
        }
    }
    // Each subtree's range, children before their parents (a child is popped after its parent).
    for (uint32_t p = count; p-- > 0;)
    {
        const uint32_t parent = s->parent[p];
        if (parent != none && s->low[p] != none)
        {
            s->low[parent] = s->low[p] < s->low[parent] ? s->low[p] : s->low[parent];
            s->high[parent] = s->high[p] > s->high[parent] ? s->high[p] : s->high[parent];
        }
    }
    bool clean = true;
    uint64_t body_nodes = 0;
    const bool room = lower_inventory_room(inventory, count);
    inventory->failures += !room;
    for (uint32_t p = 0; room && p < count; p += 1)
    {
        const uint32_t node = pops[p].node;
        const LowerInventoryCategory category = s->node_pop[node] == p ? lower_inventory_category(program, node, pops[p].flags, s->times[p] > 1) :
                                                                        LOWER_INVENTORY_CATEGORIES;
        if (category != LOWER_INVENTORY_CATEGORIES)
        {
            const uint64_t g = inventory->node_count++;
            s->global[p] = (uint32_t)g;
            inventory->node_keys[g] = BUSTER_CHECK_NONE;
            inventory->node_parents[g] = s->parent[p] != none ? s->global[s->parent[p]] : none;
            inventory->node_own[g] = s->own[p];
            inventory->tracked_nodes += (pops[p].flags & BUSTER_CHECK_LOWER_OWNER_TRACKED) != 0;
            body_nodes += 1;
            inventory->category_nodes[category] += 1;
            inventory->category_entries[category] += s->own[p];
            inventory->category_most[category] = s->own[p] > inventory->category_most[category] ? s->own[p] : inventory->category_most[category];
            clean = clean && category == LOWER_INVENTORY_ORDINARY;
        }
        if (category == LOWER_INVENTORY_ORDINARY)
        {
            uint8_t pattern[BUSTER_LOWER_INVENTORY_PATTERN_BYTES];
            const uint32_t length = lower_inventory_pattern(inventory, s, function, owners, p, node, pattern);
            uint8_t key[BUSTER_LOWER_INVENTORY_KEY_BYTES];
            lower_inventory_key(key, program, pops + p);
            LowerInventoryKey* entry = lower_inventory_find(inventory, key);
            if (entry != NULL)
            {
                lower_inventory_count(entry, pattern, length, s->own[p], lower_inventory_prefixed(pattern, length));
                inventory->node_keys[s->global[p]] = (uint32_t)(entry - inventory->keys);
            }
            else
            {
                inventory->failures += 1;
            }
        }
    }
    for (uint32_t p = 0; p < count; p += 1)
    {
        s->node_pop[pops[p].node] = none;
    }
    inventory->bodies += 1;
    inventory->pops += count;
    inventory->entries += function->count;
    inventory->entries_nodes += nodes;
    inventory->entries_conversions += conversions;
    inventory->entries_controls += controls;
    inventory->entries_tokens += tokens;
    if (clean)
    {
        inventory->clean_bodies += 1;
        inventory->clean_entries += function->count;
        inventory->clean_nodes += body_nodes;
    }
}

// File `file`, read with `options`, prepared and lowered as -c lowers it, counted into `inventory`: whether it was.
static bool lower_inventory_file(LowerInventory* inventory, const char* file, char** options, uint32_t option_count)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    Arena option_arena = arena_reserve(UINT64_C(1) << 24);
    char** own = BUSTER_ARENA_PUSH_ARRAY(&option_arena, char*, (uint64_t)option_count + 2);
    bool valid = own != 0;
    for (uint32_t i = 0; valid && i < option_count; i += 1)
    {
        own[i] = options[i];
    }
    PPOptions pp_options;
    const char* input = 0;
    const char* output_file = 0;
    bool markers = false;
    if (valid)
    {
        own[option_count] = (char*)file;
    }
    valid = valid && pp_parse_options((int)option_count + 1, own, &pp_options, &option_arena, &input, &output_file, &markers) && output_file == 0 && input != 0;
    pp_options.semantic_pragmas = true;
    CheckCorpusPath path = { 0 };
    const bool preprocessed = valid && intern_initialize(&identifiers) && pp_initialize(&pp, &pp_options, &identifiers) && pp_run(&pp, input);
    const bool accepted = preprocessed && check_corpus_check_policy(&path, &parser, &pp, false, pp_options.quiet);
    CheckDataPrepared data = { 0 };
    CheckProgram program = { 0 };
    bool prepared = accepted && check_data_prepare(&data, &path.input, &path.stream, &path.initializers, &path.types, parser.options.scalar, &path.output_arena, &path.scratch);
    const CheckProgramOptions program_options = {
        .initializers = &path.initializers,
        .asms = &path.asms,
        .identifiers = pp.table,
        .initializer_references = data.references,
        .initializer_reference_count = data.reference_count,
        .target = pp.options.target,
        .predefines = &pp.options.predefines,
    };
    prepared = prepared && check_program(&program, &parser, &path.input, &path.stream, &path.types, program_options, &path.output_arena, &path.scratch);
    CheckProgramSelection selected = { 0 };
    prepared = prepared && check_program_select_mode(&selected, &program, &data, C_LOWERING_REACHABLE, false, &path.output_arena, &path.scratch);
    prepared = prepared && (tier0_initialized || (x86_encoder_initialize() && x86_semantics_initialize() && tier0_initialize()));
    // The log of the tasks' pops: a node is popped again only for what its first pop finds (a compound literal's
    // initializer, a _BitInt's operation), so four per node of the program's stream leaves room; a fuller log fails.
    const uint64_t log_capacity = prepared ? 4 * (uint64_t)selected.program.stream->count + 65536 : 0;
    Arena log_arena = arena_reserve(log_capacity * sizeof(CheckLowerOwnerPop) + 4096);
    CheckLowerOwners owners = { .capacity = log_capacity };
    owners.pops = prepared ? BUSTER_ARENA_PUSH_ARRAY(&log_arena, CheckLowerOwnerPop, owners.capacity) : 0;
    CheckProgramLowered lowered = { 0 };
    const bool lowering = prepared && owners.pops != 0 && check_program_lower_owners(&lowered, &selected.program, &owners, &path.output_arena, &path.scratch);
    // The owners change no entry: the plain lowering's, function for function, are the same.
    CheckProgramLowered plain = { 0 };
    const bool lowered_plain = lowering && check_program_lower(&plain, &selected.program, true, &path.output_arena, &path.scratch);
    uint32_t differing = lowered_plain ? 0 : 1;
    for (uint32_t b = 0; lowered_plain && b < selected.program.body_count; b += 1)
    {
        const Tier0Function* a = lowered.functions + b;
        const Tier0Function* c = plain.functions + b;
        const bool same = a->count == c->count && (a->count == 0 || (memcmp(a->kinds, c->kinds, a->count) == 0 && memcmp(a->classes, c->classes, a->count) == 0 &&
                                                                      memcmp(a->values, c->values, (uint64_t)a->count * sizeof(uint64_t)) == 0));
        differing += !same;
    }
    if (lowering && differing != 0)
    {
        printf("lowering-templates: file=%s failed=\"the owners' lowering differs from the plain one in %u functions\"\n", file, differing);
    }
    const bool complete = lowering && !owners.overflow && differing == 0;
    if (complete)
    {
        const uint32_t nodes = selected.program.stream->count;
        Arena scratch = arena_reserve(UINT64_C(1) << 34);
        LowerInventoryScratch s = { 0 };
        uint32_t widest = 0;
        uint64_t first = 0;
        // The log's bodies come in order, each one's pops together.
        for (uint64_t i = 0; i <= owners.count; i += 1)
        {
            if (i == owners.count || owners.pops[i].body != owners.pops[first].body)
            {
                widest = (uint32_t)(i - first) > widest ? (uint32_t)(i - first) : widest;
                first = i;
            }
        }
        s.node_pop = BUSTER_ARENA_PUSH_ARRAY(&scratch, uint32_t, (uint64_t)nodes + 1);
        uint32_t** columns[] = { &s.low, &s.high, &s.own, &s.times, &s.parent, &s.first_child, &s.last_child, &s.next_sibling, &s.global };
        bool room = s.node_pop != 0;
        for (uint32_t c = 0; room && c < sizeof(columns) / sizeof(columns[0]); c += 1)
        {
            *columns[c] = BUSTER_ARENA_PUSH_ARRAY(&scratch, uint32_t, (uint64_t)widest + 1);
            room = *columns[c] != 0;
        }
        if (room)
        {
            memset(s.node_pop, 0xff, ((uint64_t)nodes + 1) * sizeof(uint32_t));
            first = 0;
            for (uint32_t b = 0; b < selected.program.body_count; b += 1)
            {
                uint64_t end = first;
                while (end < owners.count && owners.pops[end].body == b)
                {
                    end += 1;
                }
                if (lowered.entry_tokens[b] != 0)
                {
                    lower_inventory_body(inventory, &s, &selected.program, lowered.functions + b, lowered.entry_tokens[b], owners.pops + first, (uint32_t)(end - first));
                }
                first = end;
            }
            inventory->files += 1;
        }
        else
        {
            inventory->failures += 1;
        }
        arena_release(&scratch);
    }
    inventory->failures += valid && !complete;
    if (valid && !complete && (!lowering || differing == 0))
    {
        printf("lowering-templates: file=%s failed=\"%s\"\n", file,
               !preprocessed ? "it couldn't be preprocessed" : !accepted ? "the checker rejected it" : !prepared ? "its program couldn't be prepared" :
               owners.overflow ? "the log of pops filled" : "it couldn't be lowered");
        if (prepared && !lowering)
        {
            printf("lowering-templates: status=%u reason=\"%s\" token=%u\n", (uint32_t)lowered.status, lowered.reason != NULL ? lowered.reason : "", lowered.token);
        }
    }
    arena_release(&log_arena);
    check_corpus_close(&path);
    parse_release(&parser);
    pp_release(&pp);
    intern_release(&identifiers);
    arena_release(&option_arena);
    return valid;
}

static double lower_inventory_share(uint64_t part, uint64_t whole)
{
    return whole != 0 ? 100.0 * (double)part / (double)whole : 0.0;
}

// A key's description: op, context and its node's class, for the report.
static void lower_inventory_print_key(const LowerInventoryKey* entry)
{
    const uint8_t* k = entry->key;
    const uint8_t class = k[2];
    printf("op=%s ctx=%s%s%s class=%s facts=%#x const=%u cat=%u flags=%u object=%#x offset=%u dest=%#x a=%s b=%s to=%s",
           k[0] < CHECK_OP_COUNT ? lower_inventory_op_names[k[0]] : "?",
           (k[1] & 1) != 0 ? "A" : "-", (k[1] & 2) != 0 ? "I" : "-", (k[1] & 4) != 0 ? "D" : "-",
           class < BUSTER_TYPED_CLASS_LIMIT ? lower_inventory_class_names[class] : "none", k[3], k[4], k[5], k[6], k[7], k[8], k[17],
           k[9] < BUSTER_TYPED_CLASS_LIMIT ? lower_inventory_class_names[k[9]] : "-", k[11] < BUSTER_TYPED_CLASS_LIMIT ? lower_inventory_class_names[k[11]] : "-",
           k[13] < BUSTER_TYPED_CLASS_LIMIT ? lower_inventory_class_names[k[13]] : "-");
}

static void lower_inventory_print_pattern(const uint8_t* pattern, uint32_t length)
{
    uint32_t i = 0;
    while (i < length)
    {
        if (pattern[i] == 'C' || pattern[i] == '?')
        {
            printf(" %c", pattern[i]);
            i += 1;
        }
        else
        {
            const uint8_t kind = i + 1 < length ? pattern[i + 1] : 0;
            const uint8_t class = i + 2 < length ? pattern[i + 2] : 0;
            printf(" %s%s.%s", pattern[i] == 'V' ? "v:" : "", kind < BUSTER_TYPED_KINDS ? lower_inventory_kind_names[kind] : "?",
                   class < BUSTER_TYPED_CLASS_LIMIT ? lower_inventory_class_names[class] : "?");
            i += 3;
        }
    }
}

// Orders keys by nodes, the most first, then by their bytes.
static int lower_inventory_compare(const void* left, const void* right)
{
    const LowerInventoryKey* a = left;
    const LowerInventoryKey* b = right;
    const int by_nodes = a->nodes < b->nodes ? 1 : a->nodes > b->nodes ? -1 : 0;
    return by_nodes != 0 ? by_nodes : memcmp(a->key, b->key, BUSTER_LOWER_INVENTORY_KEY_BYTES);
}

// A deterministic key's pattern with each class written as what gives it: the node's own class ('n'), its first or second
// operand's ('a', 'b'), its conversion's target's ('t'), or the class itself where none of them is it. A template table
// holds these, each node taking its classes from its own columns.
static uint32_t lower_inventory_abstract(const LowerInventoryKey* entry, uint8_t* abstract)
{
    const uint8_t* pattern = entry->patterns[0];
    const uint32_t length = entry->pattern_lengths[0];
    const uint8_t* k = entry->key;
    uint32_t i = 0;
    while (i < length)
    {
        abstract[i] = pattern[i];
        if (pattern[i] == 'C' || pattern[i] == '?')
        {
            i += 1;
        }
        else
        {
            const uint8_t class = pattern[i + 2];
            abstract[i + 1] = pattern[i + 1];
            abstract[i + 2] = class == k[2] ? 'n' : class == k[13] ? 't' : class == k[9] ? 'a' : class == k[11] ? 'b' : (uint8_t)(0x80u | class);
            i += 3;
        }
    }
    return length;
}

// Orders counts, the largest first.
static int lower_inventory_compare_counts(const void* left, const void* right)
{
    const uint64_t a = *(const uint64_t*)left;
    const uint64_t b = *(const uint64_t*)right;
    return a < b ? 1 : a > b ? -1 : 0;
}

// How many abstract patterns (lower_inventory_abstract) the deterministic keys have, and how many nodes and own entries
// the first N of them, the largest first, cover.
static void lower_inventory_abstracts(const LowerInventory* inventory, uint64_t nodes, uint64_t expression_entries)
{
    const uint32_t capacity = BUSTER_LOWER_INVENTORY_TABLE;
    uint64_t* hashes = calloc(capacity, sizeof(uint64_t));
    uint64_t* counts = calloc(capacity, 2 * sizeof(uint64_t));
    uint32_t distinct = 0;
    if (hashes != NULL && counts != NULL)
    {
        for (uint32_t i = 0; i < capacity; i += 1)
        {
            const LowerInventoryKey* entry = inventory->keys + i;
            if (entry->used && entry->pattern_count == 1 && entry->other_patterns == 0)
            {
                uint8_t abstract[BUSTER_LOWER_INVENTORY_PATTERN_BYTES];
                const uint32_t length = lower_inventory_abstract(entry, abstract);
                const uint64_t hash = (lower_inventory_hash(abstract, length) ^ length) | 1;
                uint32_t slot = (uint32_t)hash & (capacity - 1);
                while (hashes[slot] != 0 && hashes[slot] != hash)
                {
                    slot = (slot + 1) & (capacity - 1);
                }
                distinct += hashes[slot] == 0;
                hashes[slot] = hash;
                counts[2 * slot] += entry->nodes;
                counts[2 * slot + 1] += entry->entries;
            }
        }
        uint32_t used = 0;
        // With --abstracts N, the first N abstract patterns, by their nodes, each with a key of it.
        uint8_t* taken = calloc(capacity, 1);
        for (uint32_t printed = 0; taken != NULL && printed < lower_inventory_print_abstracts; printed += 1)
        {
            uint32_t best = capacity;
            for (uint32_t i = 0; i < capacity; i += 1)
            {
                best = hashes[i] != 0 && taken[i] == 0 && (best == capacity || counts[2 * i] > counts[2 * best]) ? i : best;
            }
            if (best != capacity)
            {
                const LowerInventoryKey* example = NULL;
                for (uint32_t i = 0; example == NULL && i < capacity; i += 1)
                {
                    const LowerInventoryKey* entry = inventory->keys + i;
                    if (entry->used && entry->pattern_count == 1 && entry->other_patterns == 0)
                    {
                        uint8_t abstract[BUSTER_LOWER_INVENTORY_PATTERN_BYTES];
                        const uint32_t length = lower_inventory_abstract(entry, abstract);
                        example = ((lower_inventory_hash(abstract, length) ^ length) | 1) == hashes[best] ? entry : NULL;
                    }
                }
                printf("lowering-templates: abstract %u nodes=%" PRIu64 " entries=%" PRIu64 " :", printed + 1, counts[2 * best], counts[2 * best + 1]);
                if (example != NULL)
                {
                    uint8_t abstract[BUSTER_LOWER_INVENTORY_PATTERN_BYTES];
                    const uint32_t length = lower_inventory_abstract(example, abstract);
                    for (uint32_t i = 0; i < length;)
                    {
                        if (abstract[i] == 'C' || abstract[i] == '?')
                        {
                            printf(" %c", abstract[i]);
                            i += 1;
                        }
                        else
                        {
                            const uint8_t kind = abstract[i + 1];
                            const uint8_t class = abstract[i + 2];
                            printf(" %s%s.", abstract[i] == 'V' ? "v:" : "", kind < BUSTER_TYPED_KINDS ? lower_inventory_kind_names[kind] : "?");
                            if (class >= 0x80u)
                            {
                                printf("%s", (class & 0x7fu) < BUSTER_TYPED_CLASS_LIMIT ? lower_inventory_class_names[class & 0x7fu] : "?");
                            }
                            else
                            {
                                printf("%c", class);
                            }
                            i += 3;
                        }
                    }
                    printf("   e.g. ");
                    lower_inventory_print_key(example);
                }
                printf("\n");
                taken[best] = 1;
            }
        }
        free(taken);
        // The (nodes, entries) pairs packed, then sorted by nodes.
        used = 0;
        for (uint32_t i = 0; i < capacity; i += 1)
        {
            if (hashes[i] != 0)
            {
                counts[2 * used] = counts[2 * i];
                counts[2 * used + 1] = counts[2 * i + 1];
                used += 1;
            }
        }
        qsort(counts, used, 2 * sizeof(uint64_t), lower_inventory_compare_counts);
        uint64_t covered_nodes = 0;
        uint64_t covered_entries = 0;
        uint32_t next = 8;
        printf("lowering-templates: abstract patterns=%u (classes as the node's, its operands' or its target's)\n", distinct);
        for (uint32_t i = 0; i < used; i += 1)
        {
            covered_nodes += counts[2 * i];
            covered_entries += counts[2 * i + 1];
            if (i + 1 == next || i + 1 == used)
            {
                printf("lowering-templates: abstract templates=%u nodes=%" PRIu64 " (%.2f%%) own-entries=%" PRIu64 " (%.2f%% of expression entries)\n", i + 1,
                       covered_nodes, lower_inventory_share(covered_nodes, nodes), covered_entries, lower_inventory_share(covered_entries, expression_entries));
                next *= 2;
            }
        }
    }
    free(counts);
    free(hashes);
}

// The maximal subtrees whose every node is in a deterministic key: how many, their nodes and own entries, by size; and the
// statements' expressions whose whole tree is.
static void lower_inventory_subtrees(const LowerInventory* inventory, uint64_t nodes, uint64_t expression_entries)
{
    const uint64_t count = inventory->node_count;
    uint8_t* full = calloc(count + 1, 1);
    uint64_t* subtree_nodes = calloc(count + 1, sizeof(uint64_t));
    uint64_t* subtree_entries = calloc(count + 1, sizeof(uint64_t));
    if (full != NULL && subtree_nodes != NULL && subtree_entries != NULL)
    {
        for (uint64_t g = 0; g < count; g += 1)
        {
            const uint32_t key = inventory->node_keys[g];
            full[g] = key != BUSTER_CHECK_NONE && inventory->keys[key].pattern_count == 1 && inventory->keys[key].other_patterns == 0;
            subtree_nodes[g] = 1;
            subtree_entries[g] = inventory->node_own[g];
        }
        for (uint64_t g = count; g-- > 0;)
        {
            const uint32_t parent = inventory->node_parents[g];
            if (parent != BUSTER_CHECK_NONE)
            {
                full[parent] = full[parent] && full[g];
                subtree_nodes[parent] += subtree_nodes[g];
                subtree_entries[parent] += subtree_entries[g];
            }
        }
        uint64_t maximal = 0;
        uint64_t covered_nodes = 0;
        uint64_t covered_entries = 0;
        uint64_t roots = 0;
        uint64_t full_roots = 0;
        uint64_t sizes[6] = { 0 };
        uint64_t size_nodes[6] = { 0 };
        for (uint64_t g = 0; g < count; g += 1)
        {
            const uint32_t parent = inventory->node_parents[g];
            roots += parent == BUSTER_CHECK_NONE;
            full_roots += parent == BUSTER_CHECK_NONE && full[g];
            if (full[g] && (parent == BUSTER_CHECK_NONE || !full[parent]))
            {
                const uint64_t size = subtree_nodes[g];
                const uint32_t bucket = size == 1 ? 0 : size <= 3 ? 1 : size <= 7 ? 2 : size <= 15 ? 3 : size <= 63 ? 4 : 5;
                maximal += 1;
                covered_nodes += size;
                covered_entries += subtree_entries[g];
                sizes[bucket] += 1;
                size_nodes[bucket] += size;
            }
        }
        printf("lowering-templates: maximal covered subtrees=%" PRIu64 " nodes=%" PRIu64 " (%.2f%%) own-entries=%" PRIu64 " (%.2f%% of expression entries) "
               "nodes-per-subtree=%.2f statements' expressions=%" PRIu64 " whole=%" PRIu64 " (%.2f%%)\n",
               maximal, covered_nodes, lower_inventory_share(covered_nodes, nodes), covered_entries, lower_inventory_share(covered_entries, expression_entries),
               maximal != 0 ? (double)covered_nodes / (double)maximal : 0.0, roots, full_roots, lower_inventory_share(full_roots, roots));
        static const char* const names[6] = { "1", "2-3", "4-7", "8-15", "16-63", "64+" };
        for (uint32_t b = 0; b < 6; b += 1)
        {
            printf("lowering-templates: covered subtrees of %s nodes: %" PRIu64 " with %" PRIu64 " nodes (%.2f%%)\n", names[b], sizes[b], size_nodes[b],
                   lower_inventory_share(size_nodes[b], nodes));
        }
    }
    free(subtree_entries);
    free(subtree_nodes);
    free(full);
}

static void lower_inventory_report(LowerInventory* inventory, uint32_t top, uint32_t key_limit)
{
    {
        uint64_t all_nodes = 0;
        for (uint32_t c = 0; c < LOWER_INVENTORY_CATEGORIES; c += 1)
        {
            all_nodes += inventory->category_nodes[c];
        }
        lower_inventory_subtrees(inventory, all_nodes, inventory->entries_nodes + inventory->entries_conversions);
        lower_inventory_abstracts(inventory, all_nodes, inventory->entries_nodes + inventory->entries_conversions);
    }
    // The used keys, packed and sorted.
    uint32_t used = 0;
    for (uint32_t i = 0; i < BUSTER_LOWER_INVENTORY_TABLE; i += 1)
    {
        if (inventory->keys[i].used)
        {
            inventory->keys[used++] = inventory->keys[i];
        }
    }
    qsort(inventory->keys, used, sizeof(LowerInventoryKey), lower_inventory_compare);
    uint64_t nodes = 0;
    for (uint32_t c = 0; c < LOWER_INVENTORY_CATEGORIES; c += 1)
    {
        nodes += inventory->category_nodes[c];
    }
    const uint64_t expression_entries = inventory->entries_nodes + inventory->entries_conversions;
    printf("lowering-templates: files=%" PRIu64 " bodies=%" PRIu64 " pops=%" PRIu64 " nodes=%" PRIu64 " entries=%" PRIu64 " nodes-in-tracked-bodies=%" PRIu64
           " (%.2f%%)%s\n",
           inventory->files, inventory->bodies, inventory->pops, nodes, inventory->entries, inventory->tracked_nodes,
           lower_inventory_share(inventory->tracked_nodes, nodes), lower_inventory_ignore_tracking ? " (classified as untracked)" : "");
    printf("lowering-templates: entries by owner: nodes=%" PRIu64 " (%.2f%%) operand-conversions=%" PRIu64 " (%.2f%%) nested-controls=%" PRIu64
           " (%.2f%%) statements-and-frames=%" PRIu64 " (%.2f%%)\n",
           inventory->entries_nodes, lower_inventory_share(inventory->entries_nodes, inventory->entries), inventory->entries_conversions,
           lower_inventory_share(inventory->entries_conversions, inventory->entries), inventory->entries_controls,
           lower_inventory_share(inventory->entries_controls, inventory->entries), inventory->entries_tokens, lower_inventory_share(inventory->entries_tokens, inventory->entries));
    for (uint32_t c = 0; c < LOWER_INVENTORY_CATEGORIES; c += 1)
    {
        printf("lowering-templates: category=%s nodes=%" PRIu64 " (%.2f%%) own-entries=%" PRIu64 " (%.2f%% of expression entries) per-node=%.2f most=%" PRIu64 "\n",
               lower_inventory_category_names[c], inventory->category_nodes[c], lower_inventory_share(inventory->category_nodes[c], nodes),
               inventory->category_entries[c], lower_inventory_share(inventory->category_entries[c], expression_entries),
               inventory->category_nodes[c] != 0 ? (double)inventory->category_entries[c] / (double)inventory->category_nodes[c] : 0.0, inventory->category_most[c]);
    }
    uint64_t deterministic_keys = 0;
    uint64_t deterministic_nodes = 0;
    uint64_t deterministic_entries = 0;
    for (uint32_t i = 0; i < used; i += 1)
    {
        const LowerInventoryKey* entry = inventory->keys + i;
        const bool deterministic = entry->pattern_count == 1 && entry->other_patterns == 0;
        deterministic_keys += deterministic;
        deterministic_nodes += deterministic ? entry->nodes : 0;
        deterministic_entries += deterministic ? entry->entries : 0;
    }
    printf("lowering-templates: ordinary keys=%u deterministic=%" PRIu64 " their nodes=%" PRIu64 " (%.2f%% of nodes) their own entries=%" PRIu64
           " (%.2f%% of expression entries, %.2f%% of all) undetermined keys' nodes=%" PRIu64 "\n",
           used, deterministic_keys, deterministic_nodes, lower_inventory_share(deterministic_nodes, nodes), deterministic_entries,
           lower_inventory_share(deterministic_entries, expression_entries), lower_inventory_share(deterministic_entries, inventory->entries),
           inventory->category_nodes[LOWER_INVENTORY_ORDINARY] - deterministic_nodes);
    // Coverage by the first N deterministic keys, the largest first.
    uint64_t covered_nodes = 0;
    uint64_t covered_entries = 0;
    uint64_t covered_prefixed = 0;
    uint32_t taken = 0;
    uint32_t next = 1;
    for (uint32_t i = 0; i < used && taken < key_limit; i += 1)
    {
        const LowerInventoryKey* entry = inventory->keys + i;
        if (entry->pattern_count == 1 && entry->other_patterns == 0)
        {
            taken += 1;
            covered_nodes += entry->nodes;
            covered_entries += entry->entries;
            covered_prefixed += entry->prefixed;
            if (taken == next || taken == key_limit)
            {
                printf("lowering-templates: templates=%u nodes=%" PRIu64 " (%.2f%%) own-entries=%" PRIu64 " (%.2f%% of expression entries, %.2f%% of all) "
                       "not-postfix-nodes=%" PRIu64 "\n",
                       taken, covered_nodes, lower_inventory_share(covered_nodes, nodes), covered_entries, lower_inventory_share(covered_entries, expression_entries),
                       lower_inventory_share(covered_entries, inventory->entries), covered_prefixed);
                next *= 2;
            }
        }
    }
    printf("lowering-templates: bodies without an exception=%" PRIu64 " (%.2f%%) their nodes=%" PRIu64 " (%.2f%%) their entries=%" PRIu64 " (%.2f%%)\n",
           inventory->clean_bodies, lower_inventory_share(inventory->clean_bodies, inventory->bodies), inventory->clean_nodes,
           lower_inventory_share(inventory->clean_nodes, nodes), inventory->clean_entries, lower_inventory_share(inventory->clean_entries, inventory->entries));
    printf("lowering-templates: patterns past %u bytes=%" PRIu64 " strays=%" PRIu64 " failures=%" PRIu64 "\n", BUSTER_LOWER_INVENTORY_PATTERN_BYTES,
           inventory->long_patterns, inventory->strays, inventory->failures);
    for (uint32_t i = 0; i < used && i < top; i += 1)
    {
        const LowerInventoryKey* entry = inventory->keys + i;
        printf("lowering-templates: key %u nodes=%" PRIu64 " entries=%" PRIu64 " patterns=%u%s ", i + 1, entry->nodes, entry->entries, entry->pattern_count,
               entry->other_patterns != 0 ? "+" : "");
        lower_inventory_print_key(entry);
        printf(" :");
        lower_inventory_print_pattern(entry->patterns[0], entry->pattern_lengths[0]);
        printf("\n");
    }
    uint32_t printed = 0;
    for (uint32_t i = 0; i < used && printed < top / 2; i += 1)
    {
        const LowerInventoryKey* entry = inventory->keys + i;
        if (entry->pattern_count != 1 || entry->other_patterns != 0)
        {
            printed += 1;
            printf("lowering-templates: undetermined %u nodes=%" PRIu64 " first-pattern-nodes=%" PRIu64 " patterns=%u%s ", i + 1, entry->nodes, entry->pattern_nodes[0],
                   entry->pattern_count, entry->other_patterns != 0 ? "+" : "");
            lower_inventory_print_key(entry);
            printf("\n");
            for (uint32_t k = 0; k < entry->pattern_count; k += 1)
            {
                printf("lowering-templates:     %" PRIu64 " :", entry->pattern_nodes[k]);
                lower_inventory_print_pattern(entry->patterns[k], entry->pattern_lengths[k]);
                printf("\n");
            }
        }
    }
}

static int run_lowering_inventory(int count, char** arguments)
{
    uint64_t top = 40;
    uint64_t key_limit = 64;
    char** options = calloc((size_t)count + 1, sizeof(char*));
    char** files = calloc((size_t)count + 1, sizeof(char*));
    uint32_t option_count = 0;
    uint32_t file_count = 0;
    bool valid = options != 0 && files != 0;
    for (int i = 0; valid && i < count; i += 1)
    {
        if (strcmp(arguments[i], "--top") == 0)
        {
            valid = i + 1 < count && check_corpus_number(arguments[++i], &top);
        }
        else if (strcmp(arguments[i], "--abstracts") == 0)
        {
            uint64_t number = 0;
            valid = i + 1 < count && check_corpus_number(arguments[++i], &number) && number <= 4096;
            lower_inventory_print_abstracts = (uint32_t)number;
        }
        else if (strcmp(arguments[i], "--ignore-tracking") == 0)
        {
            lower_inventory_ignore_tracking = true;
        }
        else if (strcmp(arguments[i], "--allow") == 0)
        {
            valid = i + 1 < count;
            const char* name = valid ? arguments[++i] : "";
            uint32_t found = LOWER_INVENTORY_CATEGORIES;
            for (uint32_t c = 1; c < LOWER_INVENTORY_CATEGORIES; c += 1)
            {
                found = strcmp(name, lower_inventory_category_names[c]) == 0 ? c : found;
            }
            valid = valid && found != LOWER_INVENTORY_CATEGORIES;
            lower_inventory_allowed |= valid ? 1u << found : 0u;
        }
        else if (strcmp(arguments[i], "--keys") == 0)
        {
            valid = i + 1 < count && check_corpus_number(arguments[++i], &key_limit) && key_limit != 0 && key_limit <= UINT32_MAX;
        }
        else if (arguments[i][0] != '-')
        {
            files[file_count++] = arguments[i];
        }
        else
        {
            options[option_count++] = arguments[i];
            if (pp_option_takes_value(arguments[i]))
            {
                valid = i + 1 < count;
                options[option_count++] = valid ? arguments[++i] : 0;
            }
        }
    }
    valid = valid && file_count != 0;
    LowerInventory inventory = { .keys = valid ? calloc(BUSTER_LOWER_INVENTORY_TABLE, sizeof(LowerInventoryKey)) : 0 };
    valid = valid && inventory.keys != 0;
    for (uint32_t i = 0; valid && i < file_count; i += 1)
    {
        valid = lower_inventory_file(&inventory, files[i], options, option_count);
        fflush(stdout);
    }
    if (valid)
    {
        lower_inventory_report(&inventory, (uint32_t)(top < UINT32_MAX ? top : UINT32_MAX), (uint32_t)key_limit);
    }
    else
    {
        fprintf(stderr, "usage: compiler measure-lowering-templates [--top N] [--keys N] [--abstracts N] [--ignore-tracking] [--allow CATEGORY]... [preprocessor options] file...\n");
    }
    free(inventory.node_own);
    free(inventory.node_parents);
    free(inventory.node_keys);
    free(inventory.keys);
    free(files);
    free(options);
    return valid && inventory.failures == 0 && inventory.files != 0 ? 0 : 1;
}

#endif
