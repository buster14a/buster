// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Generated programs for Tier 0's tests and benchmark, until the checker's typed streams (#133) and the random C
// programs of #142 exist: each program is a set of functions over globals, made from a seed, both as typed streams (what
// #133 will hand Tier 0) and as C (what clang compiles for the cross-check), so that the two can be run and compared.
//
// A function has parameters and locals of every class, address-taken locals and pointers to them and to global arrays,
// with the `objects` option local arrays and structs (frame objects, some aligned to 16) and pointers into them,
// and statements: assignments (also compound, x op= v, and through pointers and array elements), increments, calls,
// if and else, counted loops (while, for and do) with break and continue, and early returns. Expressions are trees of
// every operation of the stream over every class, with conversions, ?:, && and || as values, and compares. Everything
// is defined behavior in C (with -fwrapv): divisors are (v & 15) + 1, shift counts are masked to the width, loops count
// to small bounds, calls go to earlier functions only and within a budget of work, and every local is set before it is
// read. Only an out-of-range floating conversion to an integer is undefined in C; Tier 0 and clang -O0 both use cvttsd2si
// for it, and the tests' interpreter does too.
//
// The C text names functions and globals with a prefix (so that clang's build and Tier 0's can be linked together); the
// streams refer to globals by SymbolIds 1 to global_count and to functions by call targets global_count + 1 onward,
// which is the order the tests add them to a link in.
//
// Nothing here recurses: expressions are built top-down from a worklist into node arrays, and emitted (as stream and
// as C) by walks with explicit stacks of (node, phase); statements are generated with a stack of open constructs.
//
// Entry points: tier0_generate_program (the entry it makes last is the function the tests call: uint64_t
// entry(uint64_t, uint64_t, uint64_t)), and tier0_tables_open and tier0_program_functions (a program's functions as
// Tier 0 takes them, with abi_assign's locations). tier0_test.c and tier0_benchmark.c include it after tier0.c; every
// function in it is static.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

// ---------------------------------------------------------------------------------------------------------------------
// Programs

#define BUSTER_TIER0_GENERATE_MAX_FUNCTIONS 64
#define BUSTER_TIER0_GENERATE_MAX_GLOBALS 64
#define BUSTER_TIER0_GENERATE_MAX_PARAMETERS 12
#define BUSTER_TIER0_GENERATE_MAX_LOCALS 48
#define BUSTER_TIER0_GENERATE_ARRAY_ELEMENTS 16
#define BUSTER_TIER0_GENERATE_EXPRESSION_NODES 128
#define BUSTER_TIER0_GENERATE_NESTING 4
// A function's objects (local arrays and structs), at most, and a struct's members; and a pointer local's target that
// is object o's element or member `part`, among the targets past the locals.
#define BUSTER_TIER0_GENERATE_MAX_OBJECTS 3
#define BUSTER_TIER0_GENERATE_MAX_MEMBERS 5
// A function's locals that hold a function's address, at most.
#define BUSTER_TIER0_GENERATE_FUNCTION_POINTERS 3
#define BUSTER_TIER0_GENERATE_OBJECT_TARGET(o, part) (BUSTER_TIER0_GENERATE_MAX_LOCALS + (o) * 256u + (part))
// The work a call of a function may cost, at most (statements run, as the generator counts them).
#define BUSTER_TIER0_GENERATE_WORK 20000

// What a program may hold, as options: how many functions (the entry among them), statements per function, the
// largest expression, and whether to use calls, loops, floating values, pointers and objects (local arrays and
// structs, in the frame). A program without objects is the same as before they existed, seed for seed.
typedef struct Tier0ProgramOptions Tier0ProgramOptions;
struct Tier0ProgramOptions
{
    uint32_t functions;
    uint32_t statements;
    uint32_t expression_nodes;
    bool calls;
    bool loops;
    bool floating;
    bool pointers;
    // Only what C defines: a floating value converts to an integer only from a narrow integer's value, into int or
    // long, so that it is in range (clang folds an out-of-range constant conversion its own way; cvttsd2si, Tier 0's
    // and the tests' interpreter's, gives the indefinite integer).
    bool defined;
    bool objects;
    // Calls through pointers to functions: locals that hold a function's address, and a function's address itself.
    bool indirect;
};

typedef struct Tier0GeneratedFunction Tier0GeneratedFunction;
struct Tier0GeneratedFunction
{
    uint32_t first_entry;
    uint32_t entry_count;
    uint32_t first_local;
    uint32_t local_count;
    uint32_t parameter_count;
    uint32_t first_call;
    uint32_t call_count;
    uint8_t result_class;
    uint64_t work;
};

// A program: its functions' streams, locals and calls in shared columns (each function's a range of them), its globals
// (a class, an element count, 1 or BUSTER_TIER0_GENERATE_ARRAY_ELEMENTS, and their initial values) and its C text. An
// object local has class VOID and its size and alignment's log2 in `local_sizes` and `local_alignments` (0 for a value).
typedef struct Tier0Program Tier0Program;
struct Tier0Program
{
    uint8_t* kinds;
    uint8_t* classes;
    uint64_t* values;
    uint32_t entry_count;
    uint32_t entry_capacity;
    uint8_t* local_classes;
    uint8_t* local_flags;
    uint32_t* local_sizes;
    uint8_t* local_alignments;
    uint32_t local_count;
    uint32_t local_capacity;
    uint32_t* call_callees;
    uint32_t* call_first;
    uint32_t* call_argument_counts;
    // Whether each call is indirect: its callee's address pushed before its arguments.
    uint8_t* call_indirect;
    uint32_t call_count;
    uint32_t call_capacity;
    uint8_t* argument_classes;
    uint32_t argument_count;
    uint32_t argument_capacity;
    uint8_t global_classes[BUSTER_TIER0_GENERATE_MAX_GLOBALS];
    uint32_t global_elements[BUSTER_TIER0_GENERATE_MAX_GLOBALS];
    uint64_t* global_values;
    uint32_t global_value_count;
    uint32_t global_first_value[BUSTER_TIER0_GENERATE_MAX_GLOBALS];
    uint32_t global_count;
    Tier0GeneratedFunction functions[BUSTER_TIER0_GENERATE_MAX_FUNCTIONS];
    uint32_t function_count;
    char* text;
    uint64_t text_length;
    uint64_t text_capacity;
    bool overflow;
    bool defined;
};

// The C name of a class.
static const char* tier0_c_type(uint32_t class)
{
    static const char* const names[BUSTER_TYPED_CLASSES] = {
        "void", "signed char", "unsigned char", "short", "unsigned short", "int", "unsigned", "long", "unsigned long", "float", "double",
    };
    return names[class < BUSTER_TYPED_CLASSES ? class : 0];
}

static void tier0_text(Tier0Program* program, const char* format, ...) __attribute__((format(printf, 2, 3)));
static void tier0_text(Tier0Program* program, const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    const uint64_t room = program->text_capacity - program->text_length;
    const int written = vsnprintf(program->text + program->text_length, room, format, arguments);
    va_end(arguments);
    if (written < 0 || (uint64_t)written >= room)
    {
        program->overflow = true;
    }
    else
    {
        program->text_length += (uint64_t)written;
    }
}

// splitmix64.
static uint64_t tier0_generate_random(uint64_t* state)
{
    *state += 0x9e3779b97f4a7c15ull;
    uint64_t z = *state;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

static uint32_t tier0_generate_below(uint64_t* state, uint32_t bound)
{
    return bound == 0 ? 0 : (uint32_t)(tier0_generate_random(state) % bound);
}

static bool tier0_generate_chance(uint64_t* state, uint32_t percent)
{
    return tier0_generate_below(state, 100) < percent;
}

// An entry of the current function's stream: its index (for a target to patch), or UINT32_MAX when the columns are full.
static uint32_t tier0_emit(Tier0Program* program, uint32_t kind, uint32_t class, uint64_t value)
{
    uint32_t index = UINT32_MAX;
    if (program->entry_count < program->entry_capacity)
    {
        index = program->entry_count;
        program->kinds[index] = (uint8_t)kind;
        program->classes[index] = (uint8_t)class;
        program->values[index] = value;
        program->entry_count += 1;
    }
    else
    {
        program->overflow = true;
    }
    return index;
}

// The entry index the next entry will have, relative to the current function's first (targets are function-relative).
static uint32_t tier0_here(const Tier0Program* program, const Tier0GeneratedFunction* function)
{
    return program->entry_count - function->first_entry;
}

static void tier0_patch(Tier0Program* program, uint32_t entry, uint32_t target)
{
    if (entry != UINT32_MAX)
    {
        program->values[entry] = target;
    }
}

// A random value of a class, as the stream holds it (sign- or zero-extended; a float's or a double's bits): mostly
// small, now and then a limit or a random pattern.
static uint64_t tier0_generate_value(uint64_t* state, uint32_t class)
{
    const uint32_t shape = tier0_generate_below(state, 10);
    uint64_t bits = shape < 6 ? tier0_generate_below(state, 33) - 16ull : shape < 8 ? tier0_generate_random(state) : (shape == 8 ? 0 : ~0ull);
    uint64_t value = 0;
    switch (class)
    {
    case BUSTER_TYPED_CLASS_I8:
        value = (uint64_t)(int64_t)(int8_t)bits;
        break;
    case BUSTER_TYPED_CLASS_U8:
        value = (uint8_t)bits;
        break;
    case BUSTER_TYPED_CLASS_I16:
        value = (uint64_t)(int64_t)(int16_t)bits;
        break;
    case BUSTER_TYPED_CLASS_U16:
        value = (uint16_t)bits;
        break;
    case BUSTER_TYPED_CLASS_I32:
        value = (uint64_t)(int64_t)(int32_t)bits;
        break;
    case BUSTER_TYPED_CLASS_U32:
        value = (uint32_t)bits;
        break;
    case BUSTER_TYPED_CLASS_F32:
    {
        const float f = shape < 8 ? (float)(int32_t)(int64_t)bits * 0.5f : shape == 8 ? 0.0f : -1.0e30f;
        uint32_t f_bits = 0;
        memcpy(&f_bits, &f, sizeof(f_bits));
        value = f_bits;
        break;
    }
    case BUSTER_TYPED_CLASS_F64:
    {
        const double d = shape < 8 ? (double)(int32_t)(int64_t)bits * 0.25 : shape == 8 ? 0.0 : 1.0e300;
        memcpy(&value, &d, sizeof(value));
        break;
    }
    default:
        value = bits;
        break;
    }
    return value;
}

// A constant as C: an integer through a cast from unsigned long long (which wraps), a floating one in hexadecimal.
static void tier0_text_constant(Tier0Program* program, uint32_t class, uint64_t value)
{
    if (class == BUSTER_TYPED_CLASS_F32)
    {
        float f = 0.0f;
        uint32_t bits = (uint32_t)value;
        memcpy(&f, &bits, sizeof(f));
        tier0_text(program, "((float)%af)", (double)f);
    }
    else if (class == BUSTER_TYPED_CLASS_F64)
    {
        double d = 0.0;
        memcpy(&d, &value, sizeof(d));
        tier0_text(program, "((double)%a)", d);
    }
    else
    {
        tier0_text(program, "((%s)0x%llxull)", tier0_c_type(class), (unsigned long long)value);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Expressions

// An expression tree's node kinds.
#define BUSTER_TIER0_EXPRESSION_CONSTANT 1
#define BUSTER_TIER0_EXPRESSION_LOCAL 2
#define BUSTER_TIER0_EXPRESSION_GLOBAL 3
// An element of a global array: child 0 is the index (an I32), masked to the array.
#define BUSTER_TIER0_EXPRESSION_ELEMENT 4
// A load through a pointer local (value: the local).
#define BUSTER_TIER0_EXPRESSION_DEREFERENCE 5
// NEG or NOT (value: the kind), CONVERT (from child 0's class).
#define BUSTER_TIER0_EXPRESSION_UNARY 6
#define BUSTER_TIER0_EXPRESSION_CONVERT 7
// ADD, SUB, MUL, AND, OR, XOR, and DIV, MOD (the divisor made (v & 15) + 1), SHL, SHR (the count masked).
#define BUSTER_TIER0_EXPRESSION_BINARY 8
// EQ to GE (value: the kind) of two children of child 0's class.
#define BUSTER_TIER0_EXPRESSION_COMPARE 9
// ?: (children: the condition, an I32, and the two arms), && and || as values.
#define BUSTER_TIER0_EXPRESSION_TERNARY 10
#define BUSTER_TIER0_EXPRESSION_AND 11
#define BUSTER_TIER0_EXPRESSION_OR 12
// An element of a local array (value: the object), child 0 its index (an I32), masked to the array; a struct's member
// (value: the object times 256 plus the member); and an object's address modulo its alignment, 0 (value: the object).
#define BUSTER_TIER0_EXPRESSION_OBJECT_ELEMENT 13
#define BUSTER_TIER0_EXPRESSION_MEMBER 14
#define BUSTER_TIER0_EXPRESSION_ALIGNMENT 15

typedef struct Tier0Expression Tier0Expression;
struct Tier0Expression
{
    uint8_t kind;
    uint8_t class;
    uint8_t operation;
    uint8_t count;
    uint32_t children[3];
    uint64_t value;
};

// An object of a function: a local array (`elements` of class classes[0], a power of two) or a struct (`members`, at
// their offsets), and its alignment in bytes (`aligned` when it is _Alignas(16) in C: an array's declaration's, or a
// struct's member's).
typedef struct Tier0Object Tier0Object;
struct Tier0Object
{
    uint32_t local;
    uint32_t elements;
    uint32_t members;
    uint8_t classes[BUSTER_TIER0_GENERATE_MAX_MEMBERS];
    bool aligned[BUSTER_TIER0_GENERATE_MAX_MEMBERS];
    uint32_t offsets[BUSTER_TIER0_GENERATE_MAX_MEMBERS];
    uint32_t alignment;
    uint32_t size;
};

// What a function's expressions can read: its locals by class (pointer locals and their pointees apart), its objects,
// and the program's globals.
typedef struct Tier0Scope Tier0Scope;
struct Tier0Scope
{
    uint32_t locals[BUSTER_TYPED_CLASSES][BUSTER_TIER0_GENERATE_MAX_LOCALS];
    uint32_t local_counts[BUSTER_TYPED_CLASSES];
    // Pointer locals (U64) and the class each points to (an address-taken local's, a global array's element's or an
    // object's element's or member's).
    uint32_t pointers[BUSTER_TIER0_GENERATE_MAX_LOCALS];
    uint8_t pointees[BUSTER_TIER0_GENERATE_MAX_LOCALS];
    uint32_t pointer_count;
    Tier0Object objects[BUSTER_TIER0_GENERATE_MAX_OBJECTS];
    uint32_t object_count;
    // The function's local indices are relative to its first; first_local makes them the program's.
    uint32_t first_local;
};

typedef struct Tier0ExpressionTree Tier0ExpressionTree;
struct Tier0ExpressionTree
{
    Tier0Expression nodes[BUSTER_TIER0_GENERATE_EXPRESSION_NODES];
    uint32_t count;
};

static bool tier0_is_arithmetic(uint32_t class)
{
    return class == BUSTER_TYPED_CLASS_I32 || class == BUSTER_TYPED_CLASS_U32 || class == BUSTER_TYPED_CLASS_I64 || class == BUSTER_TYPED_CLASS_U64;
}

// A random class: integers more often than floating values (none without `floating`).
static uint32_t tier0_generate_class(uint64_t* state, bool floating)
{
    static const uint8_t classes[] = {
        BUSTER_TYPED_CLASS_I32, BUSTER_TYPED_CLASS_I32, BUSTER_TYPED_CLASS_I32, BUSTER_TYPED_CLASS_U32, BUSTER_TYPED_CLASS_I64, BUSTER_TYPED_CLASS_I64,
        BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_I8,  BUSTER_TYPED_CLASS_U8,  BUSTER_TYPED_CLASS_I16, BUSTER_TYPED_CLASS_U16, BUSTER_TYPED_CLASS_F64,
        BUSTER_TYPED_CLASS_F32, BUSTER_TYPED_CLASS_F64,
    };
    const uint32_t count = floating ? (uint32_t)BUSTER_ARRAY_LENGTH(classes) : (uint32_t)BUSTER_ARRAY_LENGTH(classes) - 3;
    return classes[tier0_generate_below(state, count)];
}

// A leaf of `class`: a constant, a local, a global, an array element, or a load through a pointer.
// An array element needs room for its index's subtree beside the array's address (`grow`).
static void tier0_generate_leaf(const Tier0Program* program, const Tier0Scope* scope, uint64_t* state, Tier0Expression* node, uint32_t class,
                                Tier0ExpressionTree* tree, uint32_t* work, uint32_t* work_count, bool grow)
{
    node->class = (uint8_t)class;
    node->kind = BUSTER_TIER0_EXPRESSION_CONSTANT;
    node->value = tier0_generate_value(state, class);
    const uint32_t choice = tier0_generate_below(state, 10);
    uint32_t globals[BUSTER_TIER0_GENERATE_MAX_GLOBALS];
    uint32_t arrays[BUSTER_TIER0_GENERATE_MAX_GLOBALS];
    uint32_t global_count = 0;
    uint32_t array_count = 0;
    for (uint32_t g = 0; g < program->global_count; g += 1)
    {
        if (program->global_classes[g] == class)
        {
            if (program->global_elements[g] == 1)
            {
                globals[global_count] = g;
                global_count += 1;
            }
            else
            {
                arrays[array_count] = g;
                array_count += 1;
            }
        }
    }
    uint32_t pointers[BUSTER_TIER0_GENERATE_MAX_LOCALS];
    uint32_t pointer_count = 0;
    for (uint32_t p = 0; p < scope->pointer_count; p += 1)
    {
        if (scope->pointees[p] == class)
        {
            pointers[pointer_count] = scope->pointers[p];
            pointer_count += 1;
        }
    }
    if (choice < 5 && scope->local_counts[class] != 0)
    {
        node->kind = BUSTER_TIER0_EXPRESSION_LOCAL;
        node->value = scope->locals[class][tier0_generate_below(state, scope->local_counts[class])];
    }
    else if (choice < 6 && global_count != 0)
    {
        node->kind = BUSTER_TIER0_EXPRESSION_GLOBAL;
        node->value = globals[tier0_generate_below(state, global_count)];
    }
    else if (choice < 7 && array_count != 0 && grow && tree->count < BUSTER_TIER0_GENERATE_EXPRESSION_NODES)
    {
        node->kind = BUSTER_TIER0_EXPRESSION_ELEMENT;
        node->value = arrays[tier0_generate_below(state, array_count)];
        node->count = 1;
        const uint32_t index = tree->count;
        tree->count += 1;
        memset(&tree->nodes[index], 0, sizeof(tree->nodes[index]));
        node->children[0] = index;
        // The index, while the array's address is on the stack: one slot less room.
        const uint32_t room = (work[*work_count] >> 8 & 0xff) > 1 ? (work[*work_count] >> 8 & 0xff) - 1 : 1;
        work[*work_count] = index << 16 | room << 8 | BUSTER_TYPED_CLASS_I32;
        *work_count += 1;
    }
    else if (choice < 8 && pointer_count != 0)
    {
        node->kind = BUSTER_TIER0_EXPRESSION_DEREFERENCE;
        node->value = pointers[tier0_generate_below(state, pointer_count)];
    }
    else if (choice >= 8 && scope->object_count != 0 && tier0_generate_chance(state, 60))
    {
        // An object's element or member of the class, or for a U64 an object's address modulo its alignment: each
        // candidate as the object times 256 plus the member, an element as member 255 and the address as 254.
        uint32_t candidates[BUSTER_TIER0_GENERATE_MAX_OBJECTS * (BUSTER_TIER0_GENERATE_MAX_MEMBERS + 2)];
        uint32_t candidate_count = 0;
        const bool index_room = grow && tree->count < BUSTER_TIER0_GENERATE_EXPRESSION_NODES;
        for (uint32_t o = 0; o < scope->object_count; o += 1)
        {
            const Tier0Object* object = &scope->objects[o];
            for (uint32_t m = 0; m < object->members; m += 1)
            {
                candidates[candidate_count] = o << 8 | m;
                candidate_count += object->classes[m] == class ? 1 : 0;
            }
            candidates[candidate_count] = o << 8 | 255;
            candidate_count += object->elements != 0 && object->classes[0] == class && index_room ? 1 : 0;
            candidates[candidate_count] = o << 8 | 254;
            candidate_count += class == BUSTER_TYPED_CLASS_U64 ? 1 : 0;
        }
        if (candidate_count != 0)
        {
            // The node's value says all its emission and its C need: the local, and an array's element count, a
            // member's offset and number, or the alignment.
            const uint32_t candidate = candidates[tier0_generate_below(state, candidate_count)];
            const uint32_t part = candidate & 0xff;
            const Tier0Object* object = &scope->objects[candidate >> 8];
            node->kind = part == 255 ? BUSTER_TIER0_EXPRESSION_OBJECT_ELEMENT : part == 254 ? BUSTER_TIER0_EXPRESSION_ALIGNMENT : BUSTER_TIER0_EXPRESSION_MEMBER;
            node->value = part == 255   ? object->local | (uint64_t)object->elements << 32
                          : part == 254 ? object->local | (uint64_t)object->alignment << 32
                                        : object->local | (uint64_t)object->offsets[part] << 16 | (uint64_t)part << 32;
            if (part == 255)
            {
                node->count = 1;
                const uint32_t index = tree->count;
                tree->count += 1;
                memset(&tree->nodes[index], 0, sizeof(tree->nodes[index]));
                node->children[0] = index;
                const uint32_t room = (work[*work_count] >> 8 & 0xff) > 1 ? (work[*work_count] >> 8 & 0xff) - 1 : 1;
                work[*work_count] = index << 16 | room << 8 | BUSTER_TYPED_CLASS_I32;
                *work_count += 1;
            }
        }
    }
}

// A tree of at most `budget` nodes, of `class`, into `tree` (its root is node 0), whose evaluation needs at most `room`
// slots of the value stack (the expressions a front end writes stay shallow; Tier 0 refuses one deeper than its depth
// tables, until deep values spill). Top-down from a worklist: each item is a node to fill, with its class and its
// room; an item becomes a leaf when the budget is spent or by chance, otherwise an operation whose children are new
// items, a binary operation's right child with one slot less room (its left value waits on the stack).
static void tier0_generate_expression(const Tier0Program* program, const Tier0Scope* scope, uint64_t* state, Tier0ExpressionTree* tree, uint32_t class,
                                      uint32_t budget, bool floating, uint32_t room_slots, bool defined)
{
    uint32_t work[BUSTER_TIER0_GENERATE_EXPRESSION_NODES * 2];
    uint32_t work_count = 0;
    tree->count = 1;
    memset(&tree->nodes[0], 0, sizeof(tree->nodes[0]));
    work[work_count] = 0u << 16 | (room_slots < 1 ? 1 : room_slots) << 8 | class;
    work_count += 1;
    while (work_count != 0)
    {
        work_count -= 1;
        const uint32_t index = work[work_count] >> 16;
        const uint32_t slots = work[work_count] >> 8 & 0xff;
        const uint32_t wanted = work[work_count] & 0xff;
        Tier0Expression* node = &tree->nodes[index];
        node->class = (uint8_t)wanted;
        const bool room = tree->count + 3 <= budget && tree->count + 3 <= BUSTER_TIER0_GENERATE_EXPRESSION_NODES;
        const uint32_t choice = room ? tier0_generate_below(state, 100) : 100;
        // A binary operation, a compare and ?: hold a value while they evaluate another: they need two slots.
        const bool two = slots >= 2;
        uint32_t children = 0;
        uint8_t child_classes[3] = { (uint8_t)wanted, (uint8_t)wanted, (uint8_t)wanted };
        const bool arithmetic = tier0_is_arithmetic(wanted);
        const bool is_floating = tier0_class_floating(wanted);
        if (choice < 30 && two && (arithmetic || is_floating))
        {
            static const uint8_t integer_operations[] = { BUSTER_TYPED_ADD, BUSTER_TYPED_SUB, BUSTER_TYPED_MUL, BUSTER_TYPED_AND, BUSTER_TYPED_OR,
                                                          BUSTER_TYPED_XOR, BUSTER_TYPED_DIV, BUSTER_TYPED_MOD, BUSTER_TYPED_SHL, BUSTER_TYPED_SHR };
            static const uint8_t floating_operations[] = { BUSTER_TYPED_ADD, BUSTER_TYPED_SUB, BUSTER_TYPED_MUL, BUSTER_TYPED_DIV };
            // Pointer arithmetic as a U64 value (a + b * size, modulo 2^64): element sizes for each of its forms, SIB
            // scales, imul's imm32 and sizes past it, with an index of either signedness.
            static const uint64_t sizes[] = { 1,          2,          3,           4,           8,           12,          24,         4096,
                                              0x7fffffff, 0x80000000, 0xffffffff, 0x100000000, 0x100000003, 0x8000000000000000, 0xfffffffffffffff8 };
            node->kind = BUSTER_TIER0_EXPRESSION_BINARY;
            node->operation = arithmetic ? integer_operations[tier0_generate_below(state, BUSTER_ARRAY_LENGTH(integer_operations))]
                                         : floating_operations[tier0_generate_below(state, BUSTER_ARRAY_LENGTH(floating_operations))];
            children = 2;
            if (node->operation == BUSTER_TYPED_SHL || node->operation == BUSTER_TYPED_SHR)
            {
                child_classes[1] = BUSTER_TYPED_CLASS_I32;
            }
            if (wanted == BUSTER_TYPED_CLASS_U64 && tier0_generate_chance(state, 25))
            {
                node->operation = BUSTER_TYPED_PTR_ADD;
                node->value = sizes[tier0_generate_below(state, BUSTER_ARRAY_LENGTH(sizes))];
                child_classes[1] = tier0_generate_chance(state, 50) ? BUSTER_TYPED_CLASS_I64 : BUSTER_TYPED_CLASS_U64;
            }
        }
        else if (choice < 38 && (arithmetic || is_floating))
        {
            node->kind = BUSTER_TIER0_EXPRESSION_UNARY;
            node->operation = is_floating || tier0_generate_chance(state, 50) ? BUSTER_TYPED_NEG : BUSTER_TYPED_NOT;
            children = 1;
        }
        else if (choice < 52)
        {
            // A conversion from another class (never between U64 and a floating class, which Tier 0 leaves out).
            uint32_t from = tier0_generate_class(state, floating);
            const bool unsupported = (from == BUSTER_TYPED_CLASS_U64 && is_floating) || (wanted == BUSTER_TYPED_CLASS_U64 && tier0_class_floating(from));
            // C's defined conversions to an integer from a floating value: of a short's value, into int or long.
            const bool into_wide = wanted == BUSTER_TYPED_CLASS_I32 || wanted == BUSTER_TYPED_CLASS_I64;
            const bool undefined = defined && tier0_class_floating(from) && !tier0_class_floating(wanted) && (!into_wide || tree->count + 2 > budget);
            if (from != wanted && !unsupported && !undefined)
            {
                node->kind = BUSTER_TIER0_EXPRESSION_CONVERT;
                child_classes[0] = (uint8_t)from;
                children = 1;
                if (defined && tier0_class_floating(from) && !tier0_class_floating(wanted))
                {
                    // The floating child is itself a conversion, of a short.
                    const uint32_t child = tree->count;
                    const uint32_t grandchild = tree->count + 1;
                    tree->count += 2;
                    memset(&tree->nodes[child], 0, sizeof(tree->nodes[child]));
                    memset(&tree->nodes[grandchild], 0, sizeof(tree->nodes[grandchild]));
                    node->count = 1;
                    node->children[0] = child;
                    tree->nodes[child].kind = BUSTER_TIER0_EXPRESSION_CONVERT;
                    tree->nodes[child].class = (uint8_t)from;
                    tree->nodes[child].count = 1;
                    tree->nodes[child].children[0] = grandchild;
                    work[work_count] = grandchild << 16 | slots << 8 | BUSTER_TYPED_CLASS_I16;
                    work_count += 1;
                    children = 0;
                    node->kind = BUSTER_TIER0_EXPRESSION_CONVERT;
                    continue;
                }
            }
        }
        else if (choice < 64 && two && wanted == BUSTER_TYPED_CLASS_I32)
        {
            node->kind = BUSTER_TIER0_EXPRESSION_COMPARE;
            node->operation = (uint8_t)(BUSTER_TYPED_EQ + tier0_generate_below(state, 6));
            uint32_t operand = tier0_generate_class(state, floating);
            operand = tier0_is_arithmetic(operand) || tier0_class_floating(operand) ? operand : BUSTER_TYPED_CLASS_I32;
            child_classes[0] = (uint8_t)operand;
            child_classes[1] = (uint8_t)operand;
            children = 2;
        }
        else if (choice < 70)
        {
            node->kind = BUSTER_TIER0_EXPRESSION_TERNARY;
            child_classes[0] = BUSTER_TYPED_CLASS_I32;
            children = 3;
        }
        else if (choice < 74 && two && wanted == BUSTER_TYPED_CLASS_I32)
        {
            node->kind = tier0_generate_chance(state, 50) ? BUSTER_TIER0_EXPRESSION_AND : BUSTER_TIER0_EXPRESSION_OR;
            child_classes[0] = BUSTER_TYPED_CLASS_I32;
            child_classes[1] = BUSTER_TYPED_CLASS_I32;
            children = 2;
        }
        if (children == 0)
        {
            // The leaf's own work item (an array element's index) takes the item's room, which it reads back.
            work[work_count] = slots << 8;
            tier0_generate_leaf(program, scope, state, node, wanted, tree, work, &work_count, room && two);
        }
        else
        {
            node->count = (uint8_t)children;
            for (uint32_t c = 0; c < children; c += 1)
            {
                const uint32_t child = tree->count;
                tree->count += 1;
                memset(&tree->nodes[child], 0, sizeof(tree->nodes[child]));
                node->children[c] = child;
                // The second operand of a binary operation or a compare waits on the first.
                const bool waits = c == 1 && (node->kind == BUSTER_TIER0_EXPRESSION_BINARY || node->kind == BUSTER_TIER0_EXPRESSION_COMPARE);
                work[work_count] = child << 16 | (waits ? slots - 1 : slots) << 8 | child_classes[c];
                work_count += 1;
            }
        }
    }
}

// A walk's frame: a node and how far its emission got.
typedef struct Tier0Walk Tier0Walk;
struct Tier0Walk
{
    uint32_t node;
    uint32_t phase;
    uint32_t patch;
    uint32_t patch2;
};

// The stream of an expression tree, in evaluation order, into the current function. Children in order, each node's own
// entries after its children; ?:, && and || with their branches between the children.
static void tier0_emit_expression(Tier0Program* program, const Tier0GeneratedFunction* function, const Tier0ExpressionTree* tree)
{
    Tier0Walk stack[BUSTER_TIER0_GENERATE_EXPRESSION_NODES];
    uint32_t depth = 1;
    stack[0] = (Tier0Walk){ 0, 0, UINT32_MAX, UINT32_MAX };
    while (depth != 0)
    {
        Tier0Walk* frame = &stack[depth - 1];
        const Tier0Expression* node = &tree->nodes[frame->node];
        const uint32_t phase = frame->phase;
        frame->phase += 1;
        const uint32_t class = node->class;
        const uint32_t child_class = node->count != 0 ? tree->nodes[node->children[0]].class : class;
        bool pushed = false;
        bool done = false;
        if (node->kind == BUSTER_TIER0_EXPRESSION_TERNARY || node->kind == BUSTER_TIER0_EXPRESSION_AND || node->kind == BUSTER_TIER0_EXPRESSION_OR)
        {
            if (phase == 1)
            {
                frame->patch = tier0_emit(program, node->kind == BUSTER_TIER0_EXPRESSION_OR ? BUSTER_TYPED_BRANCH_TRUE : BUSTER_TYPED_BRANCH_FALSE, BUSTER_TYPED_CLASS_I32, 0);
            }
            else if (phase == 2)
            {
                if (node->kind != BUSTER_TIER0_EXPRESSION_TERNARY)
                {
                    tier0_emit(program, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 0);
                    tier0_emit(program, BUSTER_TYPED_NE, BUSTER_TYPED_CLASS_I32, 0);
                }
                frame->patch2 = tier0_emit(program, BUSTER_TYPED_JUMP_VALUE, class, 0);
                tier0_patch(program, frame->patch, tier0_here(program, function));
                if (node->kind != BUSTER_TIER0_EXPRESSION_TERNARY)
                {
                    tier0_emit(program, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, node->kind == BUSTER_TIER0_EXPRESSION_OR ? 1 : 0);
                    tier0_patch(program, frame->patch2, tier0_here(program, function));
                    done = true;
                }
            }
            else if (phase == 3)
            {
                tier0_patch(program, frame->patch2, tier0_here(program, function));
                done = true;
            }
            if (!done && phase < node->count)
            {
                stack[depth] = (Tier0Walk){ node->children[phase], 0, UINT32_MAX, UINT32_MAX };
                depth += 1;
                pushed = true;
            }
        }
        else if (node->kind == BUSTER_TIER0_EXPRESSION_ELEMENT || node->kind == BUSTER_TIER0_EXPRESSION_OBJECT_ELEMENT)
        {
            const bool object = node->kind == BUSTER_TIER0_EXPRESSION_OBJECT_ELEMENT;
            if (phase == 0)
            {
                if (object)
                {
                    tier0_emit(program, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, (uint32_t)node->value);
                }
                else
                {
                    tier0_emit(program, BUSTER_TYPED_GLOBAL_ADDRESS, BUSTER_TYPED_CLASS_U64, node->value + 1);
                }
                stack[depth] = (Tier0Walk){ node->children[0], 0, UINT32_MAX, UINT32_MAX };
                depth += 1;
                pushed = true;
            }
            else
            {
                const uint64_t elements = object ? node->value >> 32 : BUSTER_TIER0_GENERATE_ARRAY_ELEMENTS;
                tier0_emit(program, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, elements - 1);
                tier0_emit(program, BUSTER_TYPED_AND, BUSTER_TYPED_CLASS_I32, 0);
                tier0_emit(program, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_I64, 0);
                tier0_emit(program, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, tier0_class_bytes(class));
                tier0_emit(program, BUSTER_TYPED_LOAD, class, 0);
                done = true;
            }
        }
        else if (phase < node->count)
        {
            stack[depth] = (Tier0Walk){ node->children[phase], 0, UINT32_MAX, UINT32_MAX };
            depth += 1;
            pushed = true;
        }
        else
        {
            switch (node->kind)
            {
            case BUSTER_TIER0_EXPRESSION_CONSTANT:
                tier0_emit(program, BUSTER_TYPED_CONST, class, node->value);
                break;
            case BUSTER_TIER0_EXPRESSION_LOCAL:
                tier0_emit(program, BUSTER_TYPED_LOCAL, class, node->value);
                break;
            case BUSTER_TIER0_EXPRESSION_GLOBAL:
                tier0_emit(program, BUSTER_TYPED_GLOBAL, class, node->value + 1);
                break;
            case BUSTER_TIER0_EXPRESSION_DEREFERENCE:
                tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, node->value);
                tier0_emit(program, BUSTER_TYPED_LOAD, class, 0);
                break;
            case BUSTER_TIER0_EXPRESSION_MEMBER:
            {
                // The object's address plus the member's offset (as a front end lowers s.m), loaded.
                const uint64_t offset = node->value >> 16 & 0xffff;
                tier0_emit(program, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, node->value & 0xffff);
                if (offset != 0)
                {
                    tier0_emit(program, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, offset);
                    tier0_emit(program, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, 1);
                }
                tier0_emit(program, BUSTER_TYPED_LOAD, class, 0);
                break;
            }
            case BUSTER_TIER0_EXPRESSION_ALIGNMENT:
                tier0_emit(program, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, node->value & 0xffff);
                tier0_emit(program, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, (node->value >> 32) - 1);
                tier0_emit(program, BUSTER_TYPED_AND, BUSTER_TYPED_CLASS_U64, 0);
                break;
            case BUSTER_TIER0_EXPRESSION_UNARY:
                tier0_emit(program, node->operation, class, 0);
                break;
            case BUSTER_TIER0_EXPRESSION_CONVERT:
                tier0_emit(program, BUSTER_TYPED_CONVERT, class, 0);
                break;
            case BUSTER_TIER0_EXPRESSION_COMPARE:
                tier0_emit(program, node->operation, BUSTER_TYPED_CLASS_I32, 0);
                break;
            case BUSTER_TIER0_EXPRESSION_BINARY:
                if ((node->operation == BUSTER_TYPED_DIV || node->operation == BUSTER_TYPED_MOD) && !tier0_class_floating(class))
                {
                    tier0_emit(program, BUSTER_TYPED_CONST, class, 15);
                    tier0_emit(program, BUSTER_TYPED_AND, class, 0);
                    tier0_emit(program, BUSTER_TYPED_CONST, class, 1);
                    tier0_emit(program, BUSTER_TYPED_ADD, class, 0);
                }
                else if (node->operation == BUSTER_TYPED_SHL || node->operation == BUSTER_TYPED_SHR)
                {
                    tier0_emit(program, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, tier0_class_bytes(class) * 8 - 1);
                    tier0_emit(program, BUSTER_TYPED_AND, BUSTER_TYPED_CLASS_I32, 0);
                }
                // A pointer addition's value is its element size (0 for the other operations).
                tier0_emit(program, node->operation, class, node->value);
                break;
            default:
                break;
            }
            (void)child_class;
            done = true;
        }
        if (done && !pushed)
        {
            depth -= 1;
        }
    }
}

// The C of an expression tree, by the same walk.
static void tier0_print_expression(Tier0Program* program, const char* prefix, const Tier0ExpressionTree* tree)
{
    static const char* const operators[BUSTER_TYPED_KINDS] = {
        [BUSTER_TYPED_ADD] = "+",  [BUSTER_TYPED_SUB] = "-",  [BUSTER_TYPED_MUL] = "*",  [BUSTER_TYPED_DIV] = "/",  [BUSTER_TYPED_MOD] = "%",
        [BUSTER_TYPED_AND] = "&",  [BUSTER_TYPED_OR] = "|",   [BUSTER_TYPED_XOR] = "^",  [BUSTER_TYPED_SHL] = "<<", [BUSTER_TYPED_SHR] = ">>",
        [BUSTER_TYPED_EQ] = "==",  [BUSTER_TYPED_NE] = "!=",  [BUSTER_TYPED_LT] = "<",   [BUSTER_TYPED_LE] = "<=",  [BUSTER_TYPED_GT] = ">",
        [BUSTER_TYPED_GE] = ">=",  [BUSTER_TYPED_NEG] = "-",  [BUSTER_TYPED_NOT] = "~",
    };
    Tier0Walk stack[BUSTER_TIER0_GENERATE_EXPRESSION_NODES];
    uint32_t depth = 1;
    stack[0] = (Tier0Walk){ 0, 0, 0, 0 };
    while (depth != 0)
    {
        Tier0Walk* frame = &stack[depth - 1];
        const Tier0Expression* node = &tree->nodes[frame->node];
        const uint32_t phase = frame->phase;
        frame->phase += 1;
        const char* type = tier0_c_type(node->class);
        // The text before child `phase`, or after the last.
        const bool first = phase == 0;
        const bool last = phase == node->count;
        switch (node->kind)
        {
        case BUSTER_TIER0_EXPRESSION_CONSTANT:
            tier0_text_constant(program, node->class, node->value);
            break;
        case BUSTER_TIER0_EXPRESSION_LOCAL:
            tier0_text(program, "v%llu", (unsigned long long)node->value);
            break;
        case BUSTER_TIER0_EXPRESSION_GLOBAL:
            tier0_text(program, "%sg%llu", prefix, (unsigned long long)node->value);
            break;
        case BUSTER_TIER0_EXPRESSION_DEREFERENCE:
            tier0_text(program, "(*(%s*)v%llu)", type, (unsigned long long)node->value);
            break;
        case BUSTER_TIER0_EXPRESSION_ELEMENT:
            if (first)
            {
                tier0_text(program, "%sg%llu[(", prefix, (unsigned long long)node->value);
            }
            else
            {
                tier0_text(program, ") & %u]", BUSTER_TIER0_GENERATE_ARRAY_ELEMENTS - 1);
            }
            break;
        case BUSTER_TIER0_EXPRESSION_OBJECT_ELEMENT:
            if (first)
            {
                tier0_text(program, "v%u[(", (uint32_t)node->value);
            }
            else
            {
                tier0_text(program, ") & %u]", (uint32_t)(node->value >> 32) - 1);
            }
            break;
        case BUSTER_TIER0_EXPRESSION_MEMBER:
            tier0_text(program, "v%u.m%u", (uint32_t)(node->value & 0xffff), (uint32_t)(node->value >> 32));
            break;
        case BUSTER_TIER0_EXPRESSION_ALIGNMENT:
            tier0_text(program, "((unsigned long)&v%u & %uul)", (uint32_t)(node->value & 0xffff), (uint32_t)(node->value >> 32) - 1);
            break;
        case BUSTER_TIER0_EXPRESSION_UNARY:
            if (first)
            {
                tier0_text(program, "((%s)%s(", type, operators[node->operation]);
            }
            else
            {
                tier0_text(program, "))");
            }
            break;
        case BUSTER_TIER0_EXPRESSION_CONVERT:
            if (first)
            {
                tier0_text(program, "((%s)(", type);
            }
            else
            {
                tier0_text(program, "))");
            }
            break;
        case BUSTER_TIER0_EXPRESSION_COMPARE:
            if (first)
            {
                tier0_text(program, "((");
            }
            else if (!last)
            {
                tier0_text(program, ") %s (", operators[node->operation]);
            }
            else
            {
                tier0_text(program, "))");
            }
            break;
        case BUSTER_TIER0_EXPRESSION_BINARY:
        {
            const uint32_t op = node->operation;
            const bool divide = (op == BUSTER_TYPED_DIV || op == BUSTER_TYPED_MOD) && !tier0_class_floating(node->class);
            const bool shift = op == BUSTER_TYPED_SHL || op == BUSTER_TYPED_SHR;
            const bool pointer_add = op == BUSTER_TYPED_PTR_ADD;
            if (first)
            {
                tier0_text(program, "((%s)((", type);
            }
            else if (!last && pointer_add)
            {
                tier0_text(program, ") + (unsigned long)(");
            }
            else if (pointer_add)
            {
                tier0_text(program, ") * %#llxul))", (unsigned long long)node->value);
            }
            else if (!last && divide)
            {
                tier0_text(program, ") %s (%s)(((", operators[op], type);
            }
            else if (!last && shift)
            {
                tier0_text(program, ") %s ((", operators[op]);
            }
            else if (!last)
            {
                tier0_text(program, ") %s (", operators[op]);
            }
            else if (divide)
            {
                tier0_text(program, ") & 15) + 1)))");
            }
            else if (shift)
            {
                tier0_text(program, ") & %u)))", tier0_class_bytes(node->class) * 8 - 1);
            }
            else
            {
                tier0_text(program, ")))");
            }
            break;
        }
        case BUSTER_TIER0_EXPRESSION_TERNARY:
            if (first)
            {
                tier0_text(program, "((%s)((", type);
            }
            else if (phase == 1)
            {
                tier0_text(program, ") ? (");
            }
            else if (phase == 2)
            {
                tier0_text(program, ") : (");
            }
            else
            {
                tier0_text(program, ")))");
            }
            break;
        case BUSTER_TIER0_EXPRESSION_AND:
        case BUSTER_TIER0_EXPRESSION_OR:
            if (first)
            {
                tier0_text(program, "((");
            }
            else if (!last)
            {
                tier0_text(program, ") %s (", node->kind == BUSTER_TIER0_EXPRESSION_AND ? "&&" : "||");
            }
            else
            {
                tier0_text(program, "))");
            }
            break;
        default:
            break;
        }
        if (phase < node->count)
        {
            stack[depth] = (Tier0Walk){ node->children[phase], 0, 0, 0 };
            depth += 1;
        }
        else
        {
            depth -= 1;
        }
    }
}

// The stack slots a statement's expression may use: the integer depth table's, less what statements and calls hold.
#define BUSTER_TIER0_GENERATE_ROOM 12

// An expression of `class` into the stream and the C text, with `held` values already waiting on the stack.
static void tier0_generate_value_of(Tier0Program* program, Tier0GeneratedFunction* function, const Tier0Scope* scope, uint64_t* state, uint32_t class,
                                    uint32_t budget, bool floating, const char* prefix, uint32_t held)
{
    Tier0ExpressionTree tree;
    tier0_generate_expression(program, scope, state, &tree, class, budget, floating, held < BUSTER_TIER0_GENERATE_ROOM ? BUSTER_TIER0_GENERATE_ROOM - held : 1,
                              program->defined);
    tier0_emit_expression(program, function, &tree);
    tier0_print_expression(program, prefix, &tree);
}

// ---------------------------------------------------------------------------------------------------------------------
// Statements

// The constructs a function's statements can be inside: an if's then-block (and its else-block), and counted loops.
#define BUSTER_TIER0_CONSTRUCT_THEN 1
#define BUSTER_TIER0_CONSTRUCT_ELSE 2
#define BUSTER_TIER0_CONSTRUCT_WHILE 3
#define BUSTER_TIER0_CONSTRUCT_FOR 4
#define BUSTER_TIER0_CONSTRUCT_DO 5
// A loop's bound, at most (its counter counts from 0 to it).
#define BUSTER_TIER0_GENERATE_BOUND 5
#define BUSTER_TIER0_GENERATE_JUMPS 256

typedef struct Tier0Construct Tier0Construct;
struct Tier0Construct
{
    uint8_t kind;
    uint32_t statements;
    // An if's branch to its else-block or end, and its jump over the else-block; a loop's exit branch and its top.
    uint32_t branch;
    uint32_t jump;
    uint32_t top;
    uint32_t counter;
    uint32_t bound;
    // How many times the construct's statements run per run of the function (the loops' bounds multiplied).
    uint64_t multiplier;
};

// A function being generated: its record, its scope, its constructs, and the breaks and continues waiting for their
// loop's labels (each with the depth of its loop).
typedef struct Tier0FunctionState Tier0FunctionState;
struct Tier0FunctionState
{
    Tier0GeneratedFunction* function;
    uint32_t index;
    Tier0Scope scope;
    Tier0Construct constructs[BUSTER_TIER0_GENERATE_NESTING + 1];
    uint32_t depth;
    uint32_t jumps[BUSTER_TIER0_GENERATE_JUMPS];
    uint8_t jump_loops[BUSTER_TIER0_GENERATE_JUMPS];
    bool jump_continues[BUSTER_TIER0_GENERATE_JUMPS];
    uint32_t jump_count;
    uint32_t counters[BUSTER_TIER0_GENERATE_NESTING + 1];
    // The locals statements may assign (not the loop counters or the pointers).
    uint32_t assignable[BUSTER_TIER0_GENERATE_MAX_LOCALS];
    uint32_t assignable_count;
    // The locals that hold an earlier function's address (U64s, apart from the scope like the pointers), each one's
    // function.
    uint32_t function_pointers[BUSTER_TIER0_GENERATE_FUNCTION_POINTERS];
    uint32_t function_pointer_targets[BUSTER_TIER0_GENERATE_FUNCTION_POINTERS];
    uint32_t function_pointer_count;
    const char* prefix;
};

// A new local of the current function: its index (function-relative), or UINT32_MAX when the columns are full.
static uint32_t tier0_generate_local(Tier0Program* program, Tier0FunctionState* state, uint32_t class, uint8_t flags)
{
    uint32_t index = UINT32_MAX;
    if (program->local_count < program->local_capacity && state->function->local_count < BUSTER_TIER0_GENERATE_MAX_LOCALS)
    {
        program->local_classes[program->local_count] = (uint8_t)class;
        program->local_flags[program->local_count] = flags;
        program->local_sizes[program->local_count] = 0;
        program->local_alignments[program->local_count] = 0;
        program->local_count += 1;
        index = state->function->local_count;
        state->function->local_count += 1;
    }
    else
    {
        program->overflow = true;
    }
    return index;
}

static void tier0_scope_add(Tier0Scope* scope, uint32_t local, uint32_t class)
{
    if (scope->local_counts[class] < BUSTER_TIER0_GENERATE_MAX_LOCALS)
    {
        scope->locals[class][scope->local_counts[class]] = local;
        scope->local_counts[class] += 1;
    }
}

// A new object of the current function (which has fewer than BUSTER_TIER0_GENERATE_MAX_OBJECTS): a local array of 2 to
// 16 elements of a class, or a struct of 2 to 5 members laid out as C lays them out, each now and then aligned to 16
// (an array's declaration, a struct's member). Its local is of class VOID, address-taken, with its size and alignment.
// Returns false when the columns are full.
static bool tier0_generate_object(Tier0Program* program, Tier0FunctionState* state, uint64_t* random, bool floating)
{
    Tier0Object object;
    memset(&object, 0, sizeof(object));
    const bool array = tier0_generate_chance(random, 55);
    if (array)
    {
        object.elements = 2u << tier0_generate_below(random, 4);
        object.members = 0;
        object.classes[0] = (uint8_t)tier0_generate_class(random, floating);
        object.aligned[0] = tier0_generate_chance(random, 30);
        object.alignment = object.aligned[0] ? 16 : tier0_class_bytes(object.classes[0]);
        object.size = object.elements * tier0_class_bytes(object.classes[0]);
    }
    else
    {
        object.members = 2 + tier0_generate_below(random, BUSTER_TIER0_GENERATE_MAX_MEMBERS - 1);
        object.alignment = 1;
        uint32_t offset = 0;
        for (uint32_t m = 0; m < object.members; m += 1)
        {
            object.classes[m] = (uint8_t)tier0_generate_class(random, floating);
            object.aligned[m] = tier0_generate_chance(random, 15);
            const uint32_t alignment = object.aligned[m] ? 16 : tier0_class_bytes(object.classes[m]);
            offset = (offset + alignment - 1) & ~(alignment - 1);
            object.offsets[m] = offset;
            offset += tier0_class_bytes(object.classes[m]);
            object.alignment = alignment > object.alignment ? alignment : object.alignment;
        }
        object.size = (offset + object.alignment - 1) & ~(object.alignment - 1);
    }
    const uint32_t local = tier0_generate_local(program, state, BUSTER_TYPED_CLASS_VOID, BUSTER_TYPED_LOCAL_ADDRESS_TAKEN);
    const bool made = local != UINT32_MAX;
    if (made)
    {
        const uint32_t program_local = state->function->first_local + local;
        program->local_sizes[program_local] = object.size;
        program->local_alignments[program_local] = (uint8_t)__builtin_ctz(object.alignment);
        object.local = local;
        state->scope.objects[state->scope.object_count] = object;
        state->scope.object_count += 1;
    }
    return made;
}

// An object's declaration, as C.
static void tier0_declare_object(Tier0Program* program, const Tier0Object* object)
{
    if (object->elements != 0)
    {
        tier0_text(program, "    %s%s v%u[%u];\n", object->aligned[0] ? "_Alignas(16) " : "", tier0_c_type(object->classes[0]), object->local, object->elements);
    }
    else
    {
        tier0_text(program, "    struct {");
        for (uint32_t m = 0; m < object->members; m += 1)
        {
            tier0_text(program, " %s%s m%u;", object->aligned[m] ? "_Alignas(16) " : "", tier0_c_type(object->classes[m]), m);
        }
        tier0_text(program, " } v%u;\n", object->local);
    }
}

// An object's element (a constant index) or member: its address into the stream (LOCAL_ADDRESS, and the offset as a
// front end lowers it: an index times the element's size, a member's offset in bytes), and the lvalue as C.
static void tier0_object_address(Tier0Program* program, const Tier0Object* object, uint32_t part)
{
    tier0_emit(program, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, object->local);
    const uint64_t index = object->elements != 0 ? part : object->offsets[part];
    if (index != 0)
    {
        tier0_emit(program, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, index);
        tier0_emit(program, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, object->elements != 0 ? tier0_class_bytes(object->classes[0]) : 1);
    }
    if (object->elements != 0)
    {
        tier0_text(program, "v%u[%u]", object->local, part);
    }
    else
    {
        tier0_text(program, "v%u.m%u", object->local, part);
    }
}

// The innermost loop's construct depth, or 0 outside every loop.
static uint32_t tier0_innermost_loop(const Tier0FunctionState* state)
{
    uint32_t loop = 0;
    for (uint32_t d = 1; d <= state->depth; d += 1)
    {
        loop = state->constructs[d].kind >= BUSTER_TIER0_CONSTRUCT_WHILE ? d : loop;
    }
    return loop;
}

static void tier0_indent(Tier0Program* program, const Tier0FunctionState* state)
{
    tier0_text(program, "%*s", (int)(4 * (state->depth + 1)), "");
}

// A loop's counter at a nesting depth: one local per depth, made the first time a loop needs it.
static uint32_t tier0_counter(Tier0Program* program, Tier0FunctionState* state, uint32_t depth)
{
    if (state->counters[depth] == UINT32_MAX)
    {
        state->counters[depth] = tier0_generate_local(program, state, BUSTER_TYPED_CLASS_I32, 0);
    }
    return state->counters[depth];
}

// x = (int)v; into the stream: CONST v, SET_LOCAL x (counters and the locals' first values).
static void tier0_set_constant(Tier0Program* program, Tier0FunctionState* state, uint32_t local, uint32_t class, uint64_t value)
{
    tier0_emit(program, BUSTER_TYPED_CONST, class, value);
    tier0_emit(program, BUSTER_TYPED_SET_LOCAL, class, local);
    tier0_indent(program, state);
    tier0_text(program, "v%u = ", local);
    tier0_text_constant(program, class, value);
    tier0_text(program, ";\n");
}

// x = x + 1 for a counter.
static void tier0_increment(Tier0Program* program, uint32_t local)
{
    tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_I32, local);
    tier0_emit(program, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 1);
    tier0_emit(program, BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_I32, 0);
    tier0_emit(program, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_I32, local);
}

// The counter's test: counter < bound.
static void tier0_counter_test(Tier0Program* program, uint32_t local, uint32_t bound)
{
    tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_I32, local);
    tier0_emit(program, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, bound);
    tier0_emit(program, BUSTER_TYPED_LT, BUSTER_TYPED_CLASS_I32, 0);
}

// A jump to a loop's label, patched when the loop closes.
static void tier0_loop_jump(Tier0Program* program, Tier0FunctionState* state, uint32_t loop, bool continues)
{
    const uint32_t jump = tier0_emit(program, BUSTER_TYPED_JUMP, BUSTER_TYPED_CLASS_VOID, 0);
    if (state->jump_count < BUSTER_TIER0_GENERATE_JUMPS)
    {
        state->jumps[state->jump_count] = jump;
        state->jump_loops[state->jump_count] = (uint8_t)loop;
        state->jump_continues[state->jump_count] = continues;
        state->jump_count += 1;
    }
    else
    {
        program->overflow = true;
    }
}

// Patches the breaks and continues of the loop at `loop` (its break target and continue target), and forgets them.
static void tier0_patch_loop_jumps(Tier0Program* program, Tier0FunctionState* state, uint32_t loop, uint32_t exit, uint32_t again)
{
    uint32_t kept = 0;
    for (uint32_t j = 0; j < state->jump_count; j += 1)
    {
        if (state->jump_loops[j] == loop)
        {
            tier0_patch(program, state->jumps[j], state->jump_continues[j] ? again : exit);
        }
        else
        {
            state->jumps[kept] = state->jumps[j];
            state->jump_loops[kept] = state->jump_loops[j];
            state->jump_continues[kept] = state->jump_continues[j];
            kept += 1;
        }
    }
    state->jump_count = kept;
}

// Closes the innermost construct.
static void tier0_close(Tier0Program* program, Tier0FunctionState* state, uint64_t* random)
{
    Tier0GeneratedFunction* function = state->function;
    Tier0Construct* construct = &state->constructs[state->depth];
    const uint32_t depth = state->depth;
    state->depth -= 1;
    switch (construct->kind)
    {
    case BUSTER_TIER0_CONSTRUCT_THEN:
        if (tier0_generate_chance(random, 40))
        {
            construct->jump = tier0_emit(program, BUSTER_TYPED_JUMP, BUSTER_TYPED_CLASS_VOID, 0);
            tier0_patch(program, construct->branch, tier0_here(program, function));
            tier0_indent(program, state);
            tier0_text(program, "}\n");
            tier0_indent(program, state);
            tier0_text(program, "else\n");
            tier0_indent(program, state);
            tier0_text(program, "{\n");
            construct->kind = BUSTER_TIER0_CONSTRUCT_ELSE;
            construct->statements = 1 + tier0_generate_below(random, 3);
            state->depth += 1;
        }
        else
        {
            tier0_patch(program, construct->branch, tier0_here(program, function));
            tier0_indent(program, state);
            tier0_text(program, "}\n");
        }
        break;
    case BUSTER_TIER0_CONSTRUCT_ELSE:
        tier0_patch(program, construct->jump, tier0_here(program, function));
        tier0_indent(program, state);
        tier0_text(program, "}\n");
        break;
    case BUSTER_TIER0_CONSTRUCT_WHILE:
    {
        tier0_emit(program, BUSTER_TYPED_JUMP, BUSTER_TYPED_CLASS_VOID, construct->top);
        const uint32_t exit = tier0_here(program, function);
        tier0_patch(program, construct->branch, exit);
        tier0_patch_loop_jumps(program, state, depth, exit, construct->top);
        tier0_indent(program, state);
        tier0_text(program, "}\n");
        break;
    }
    case BUSTER_TIER0_CONSTRUCT_FOR:
    {
        const uint32_t step = tier0_here(program, function);
        tier0_increment(program, construct->counter);
        tier0_emit(program, BUSTER_TYPED_JUMP, BUSTER_TYPED_CLASS_VOID, construct->top);
        const uint32_t exit = tier0_here(program, function);
        tier0_patch(program, construct->branch, exit);
        tier0_patch_loop_jumps(program, state, depth, exit, step);
        tier0_indent(program, state);
        tier0_text(program, "}\n");
        break;
    }
    default:
    {
        const uint32_t test = tier0_here(program, function);
        tier0_counter_test(program, construct->counter, construct->bound);
        tier0_emit(program, BUSTER_TYPED_BRANCH_TRUE, BUSTER_TYPED_CLASS_I32, construct->top);
        const uint32_t exit = tier0_here(program, function);
        tier0_patch_loop_jumps(program, state, depth, exit, test);
        tier0_indent(program, state);
        tier0_text(program, "} while (v%u < %u);\n", construct->counter, construct->bound);
        break;
    }
    }
}

// A call of a random earlier function that fits the work budget, with random arguments, its result assigned to a
// local of its class or discarded; with the `indirect` option now and then through a pointer to it, a local that holds
// its address or the address itself. Returns false if no function fits.
static bool tier0_generate_call(Tier0Program* program, Tier0FunctionState* state, uint64_t* random, uint64_t multiplier, const Tier0ProgramOptions* options)
{
    Tier0GeneratedFunction* function = state->function;
    bool made = false;
    if (state->index != 0 && program->call_count < program->call_capacity)
    {
        const uint32_t callee = tier0_generate_below(random, state->index);
        const Tier0GeneratedFunction* target = &program->functions[callee];
        const uint64_t work = multiplier * (target->work + 1);
        if (function->work + work <= BUSTER_TIER0_GENERATE_WORK && program->argument_count + target->parameter_count <= program->argument_capacity)
        {
            const bool indirect = options->indirect && tier0_generate_chance(random, 35);
            uint32_t pointer = UINT32_MAX;
            for (uint32_t p = 0; p < state->function_pointer_count && indirect; p += 1)
            {
                pointer = state->function_pointer_targets[p] == callee ? state->function_pointers[p] : pointer;
            }
            function->work += work;
            const uint32_t call = program->call_count - function->first_call;
            program->call_callees[program->call_count] = callee;
            program->call_first[program->call_count] = program->argument_count;
            program->call_argument_counts[program->call_count] = target->parameter_count;
            program->call_indirect[program->call_count] = indirect ? 1 : 0;
            program->call_count += 1;
            function->call_count += 1;
            const uint32_t result = target->result_class;
            uint32_t destination = UINT32_MAX;
            if (result != BUSTER_TYPED_CLASS_VOID && state->scope.local_counts[result] != 0 && tier0_generate_chance(random, 80))
            {
                // A destination among the assignable locals of the class.
                uint32_t candidates[BUSTER_TIER0_GENERATE_MAX_LOCALS];
                uint32_t candidate_count = 0;
                for (uint32_t a = 0; a < state->assignable_count; a += 1)
                {
                    if (program->local_classes[function->first_local + state->assignable[a]] == result)
                    {
                        candidates[candidate_count] = state->assignable[a];
                        candidate_count += 1;
                    }
                }
                destination = candidate_count != 0 ? candidates[tier0_generate_below(random, candidate_count)] : UINT32_MAX;
            }
            tier0_indent(program, state);
            if (destination != UINT32_MAX)
            {
                tier0_text(program, "v%u = ", destination);
            }
            // An indirect call's callee, before its arguments: the local that holds its address (C calls through it),
            // or its address (C names it; the call is the same).
            if (pointer != UINT32_MAX)
            {
                tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, pointer);
                tier0_text(program, "v%u(", pointer);
            }
            else
            {
                if (indirect)
                {
                    tier0_emit(program, BUSTER_TYPED_GLOBAL_ADDRESS, BUSTER_TYPED_CLASS_U64, program->global_count + 1 + callee);
                }
                tier0_text(program, "%sf%u(", state->prefix, callee);
            }
            for (uint32_t p = 0; p < target->parameter_count; p += 1)
            {
                const uint32_t class = program->local_classes[target->first_local + p];
                program->argument_classes[program->argument_count] = (uint8_t)class;
                program->argument_count += 1;
                tier0_text(program, p == 0 ? "" : ", ");
                tier0_generate_value_of(program, function, &state->scope, random, class, 1 + tier0_generate_below(random, options->expression_nodes / 2 + 1),
                                        options->floating, state->prefix, p + (indirect ? 1 : 0));
            }
            tier0_text(program, ");\n");
            tier0_emit(program, BUSTER_TYPED_CALL, result, call);
            if (destination != UINT32_MAX)
            {
                tier0_emit(program, BUSTER_TYPED_SET_LOCAL, result, destination);
            }
            else if (result != BUSTER_TYPED_CLASS_VOID)
            {
                tier0_emit(program, BUSTER_TYPED_DISCARD, result, 0);
            }
            made = true;
        }
    }
    return made;
}

// One statement, or the opening of a construct, at the current depth.
static void tier0_generate_statement(Tier0Program* program, Tier0FunctionState* state, uint64_t* random, const Tier0ProgramOptions* options, uint32_t* left)
{
    Tier0GeneratedFunction* function = state->function;
    Tier0Construct* current = &state->constructs[state->depth];
    const uint64_t multiplier = current->multiplier;
    const uint32_t budget = 1 + tier0_generate_below(random, options->expression_nodes);
    const uint32_t choice = tier0_generate_below(random, 100);
    const uint32_t loop = tier0_innermost_loop(state);
    const bool nest = state->depth < BUSTER_TIER0_GENERATE_NESTING && *left > 2;
    const char* prefix = state->prefix;
    *left -= 1;
    function->work += multiplier;
    if (choice < 10 && nest)
    {
        // if (c) {
        tier0_indent(program, state);
        tier0_text(program, "if (");
        tier0_generate_value_of(program, function, &state->scope, random, BUSTER_TYPED_CLASS_I32, budget, options->floating, prefix, 0);
        tier0_text(program, ")\n");
        tier0_indent(program, state);
        tier0_text(program, "{\n");
        const uint32_t branch = tier0_emit(program, BUSTER_TYPED_BRANCH_FALSE, BUSTER_TYPED_CLASS_I32, 0);
        state->depth += 1;
        state->constructs[state->depth] = (Tier0Construct){ BUSTER_TIER0_CONSTRUCT_THEN, 1 + tier0_generate_below(random, 4), branch, UINT32_MAX, 0, 0, 0, multiplier };
    }
    else if (choice < 18 && nest && options->loops && multiplier * BUSTER_TIER0_GENERATE_BOUND <= 64)
    {
        const uint32_t kind = BUSTER_TIER0_CONSTRUCT_WHILE + tier0_generate_below(random, 3);
        const uint32_t counter = tier0_counter(program, state, state->depth + 1);
        const uint32_t bound = tier0_generate_below(random, BUSTER_TIER0_GENERATE_BOUND + 1);
        tier0_set_constant(program, state, counter, BUSTER_TYPED_CLASS_I32, 0);
        const uint32_t top = tier0_here(program, function);
        uint32_t branch = UINT32_MAX;
        tier0_indent(program, state);
        if (kind == BUSTER_TIER0_CONSTRUCT_DO)
        {
            tier0_text(program, "do\n");
            tier0_indent(program, state);
            tier0_text(program, "{\n");
            tier0_increment(program, counter);
        }
        else
        {
            tier0_counter_test(program, counter, bound);
            branch = tier0_emit(program, BUSTER_TYPED_BRANCH_FALSE, BUSTER_TYPED_CLASS_I32, 0);
            if (kind == BUSTER_TIER0_CONSTRUCT_WHILE)
            {
                tier0_text(program, "while (v%u < %u)\n", counter, bound);
                tier0_indent(program, state);
                tier0_text(program, "{\n");
                tier0_increment(program, counter);
            }
            else
            {
                tier0_text(program, "for (; v%u < %u; v%u++)\n", counter, bound, counter);
                tier0_indent(program, state);
                tier0_text(program, "{\n");
            }
        }
        state->depth += 1;
        state->constructs[state->depth] = (Tier0Construct){ (uint8_t)kind, 1 + tier0_generate_below(random, 4), branch, UINT32_MAX, top, counter, bound,
                                                            multiplier * (bound + 1) };
        if (kind != BUSTER_TIER0_CONSTRUCT_FOR)
        {
            tier0_indent(program, state);
            tier0_text(program, "v%u++;\n", counter);
        }
    }
    else if (choice < 24 && loop != 0)
    {
        // if (c) break; or if (c) continue;
        const bool continues = tier0_generate_chance(random, 50);
        tier0_indent(program, state);
        tier0_text(program, "if (");
        tier0_generate_value_of(program, function, &state->scope, random, BUSTER_TYPED_CLASS_I32, budget, options->floating, prefix, 0);
        tier0_text(program, ") %s;\n", continues ? "continue" : "break");
        const uint32_t skip = tier0_emit(program, BUSTER_TYPED_BRANCH_FALSE, BUSTER_TYPED_CLASS_I32, 0);
        tier0_loop_jump(program, state, loop, continues);
        tier0_patch(program, skip, tier0_here(program, function));
    }
    else if (choice < 27 && state->depth != 0 && state->index + 1 < program->function_count)
    {
        // An early return.
        const uint32_t result = function->result_class;
        tier0_indent(program, state);
        tier0_text(program, "if (");
        tier0_generate_value_of(program, function, &state->scope, random, BUSTER_TYPED_CLASS_I32, budget, options->floating, prefix, 0);
        tier0_text(program, ") return");
        const uint32_t skip = tier0_emit(program, BUSTER_TYPED_BRANCH_FALSE, BUSTER_TYPED_CLASS_I32, 0);
        if (result != BUSTER_TYPED_CLASS_VOID)
        {
            tier0_text(program, " ");
            tier0_generate_value_of(program, function, &state->scope, random, result, budget, options->floating, prefix, 0);
        }
        tier0_text(program, ";\n");
        tier0_emit(program, BUSTER_TYPED_RETURN, result, 0);
        tier0_patch(program, skip, tier0_here(program, function));
    }
    else if (choice < 42 && options->calls && tier0_generate_call(program, state, random, multiplier, options))
    {
    }
    else if (choice < 50 && state->scope.pointer_count != 0)
    {
        // *(T*)p = v;
        const uint32_t p = tier0_generate_below(random, state->scope.pointer_count);
        const uint32_t pointer = state->scope.pointers[p];
        const uint32_t class = state->scope.pointees[p];
        tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, pointer);
        tier0_indent(program, state);
        tier0_text(program, "*(%s*)v%u = ", tier0_c_type(class), pointer);
        tier0_generate_value_of(program, function, &state->scope, random, class, budget, options->floating, prefix, 1);
        tier0_text(program, ";\n");
        tier0_emit(program, BUSTER_TYPED_SET, class, 0);
    }
    else if (choice < 54 && state->scope.object_count != 0)
    {
        // An object's element (plainly or op=, the address computed once) or member.
        const Tier0Object* object = &state->scope.objects[tier0_generate_below(random, state->scope.object_count)];
        tier0_indent(program, state);
        if (object->elements != 0)
        {
            const uint32_t class = object->classes[0];
            const bool compound = tier0_is_arithmetic(class) && tier0_generate_chance(random, 40);
            tier0_emit(program, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, object->local);
            tier0_text(program, "v%u[(", object->local);
            tier0_generate_value_of(program, function, &state->scope, random, BUSTER_TYPED_CLASS_I32, budget / 2 + 1, options->floating, prefix, 1);
            tier0_text(program, ") & %u] %s ", object->elements - 1, compound ? "+=" : "=");
            tier0_emit(program, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, object->elements - 1);
            tier0_emit(program, BUSTER_TYPED_AND, BUSTER_TYPED_CLASS_I32, 0);
            tier0_emit(program, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_I64, 0);
            tier0_emit(program, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, tier0_class_bytes(class));
            if (compound)
            {
                tier0_emit(program, BUSTER_TYPED_DUP, BUSTER_TYPED_CLASS_U64, 0);
                tier0_emit(program, BUSTER_TYPED_LOAD, class, 0);
            }
            tier0_generate_value_of(program, function, &state->scope, random, class, budget, options->floating, prefix, compound ? 2 : 1);
            if (compound)
            {
                tier0_emit(program, BUSTER_TYPED_ADD, class, 0);
            }
            tier0_emit(program, BUSTER_TYPED_SET, class, 0);
        }
        else
        {
            const uint32_t member = tier0_generate_below(random, object->members);
            const uint32_t class = object->classes[member];
            tier0_object_address(program, object, member);
            tier0_text(program, " = ");
            tier0_generate_value_of(program, function, &state->scope, random, class, budget, options->floating, prefix, 1);
            tier0_emit(program, BUSTER_TYPED_SET, class, 0);
        }
        tier0_text(program, ";\n");
    }
    else if (choice < 58 && program->global_count != 0)
    {
        // A global, or an array's element (plainly or op=, the address computed once).
        const uint32_t g = tier0_generate_below(random, program->global_count);
        const uint32_t class = program->global_classes[g];
        tier0_indent(program, state);
        if (program->global_elements[g] == 1)
        {
            tier0_text(program, "%sg%u = ", prefix, g);
            tier0_generate_value_of(program, function, &state->scope, random, class, budget, options->floating, prefix, 0);
            tier0_emit(program, BUSTER_TYPED_SET_GLOBAL, class, g + 1);
        }
        else
        {
            const bool compound = tier0_is_arithmetic(class) && tier0_generate_chance(random, 40);
            tier0_emit(program, BUSTER_TYPED_GLOBAL_ADDRESS, BUSTER_TYPED_CLASS_U64, g + 1);
            tier0_text(program, "%sg%u[(", prefix, g);
            tier0_generate_value_of(program, function, &state->scope, random, BUSTER_TYPED_CLASS_I32, budget / 2 + 1, options->floating, prefix, 1);
            tier0_text(program, ") & %u] %s ", BUSTER_TIER0_GENERATE_ARRAY_ELEMENTS - 1, compound ? "+=" : "=");
            tier0_emit(program, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, BUSTER_TIER0_GENERATE_ARRAY_ELEMENTS - 1);
            tier0_emit(program, BUSTER_TYPED_AND, BUSTER_TYPED_CLASS_I32, 0);
            tier0_emit(program, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_I64, 0);
            tier0_emit(program, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, tier0_class_bytes(class));
            if (compound)
            {
                tier0_emit(program, BUSTER_TYPED_DUP, BUSTER_TYPED_CLASS_U64, 0);
                tier0_emit(program, BUSTER_TYPED_LOAD, class, 0);
            }
            tier0_generate_value_of(program, function, &state->scope, random, class, budget, options->floating, prefix, compound ? 2 : 1);
            if (compound)
            {
                tier0_emit(program, BUSTER_TYPED_ADD, class, 0);
            }
            tier0_emit(program, BUSTER_TYPED_SET, class, 0);
        }
        tier0_text(program, ";\n");
    }
    else if (state->assignable_count != 0)
    {
        // x = v; x op= v; x++; or x = y = v.
        const uint32_t x = state->assignable[tier0_generate_below(random, state->assignable_count)];
        const uint32_t class = program->local_classes[function->first_local + x];
        const uint32_t form = tier0_generate_below(random, 10);
        tier0_indent(program, state);
        if (form < 3 && tier0_is_arithmetic(class))
        {
            static const uint8_t operations[] = { BUSTER_TYPED_ADD, BUSTER_TYPED_SUB, BUSTER_TYPED_AND, BUSTER_TYPED_OR, BUSTER_TYPED_XOR, BUSTER_TYPED_SHL, BUSTER_TYPED_SHR };
            static const char* const spellings[] = { "+=", "-=", "&=", "|=", "^=", "<<=", ">>=" };
            const uint32_t o = tier0_generate_below(random, BUSTER_ARRAY_LENGTH(operations));
            const bool shift = operations[o] == BUSTER_TYPED_SHL || operations[o] == BUSTER_TYPED_SHR;
            tier0_emit(program, BUSTER_TYPED_LOCAL, class, x);
            tier0_text(program, "v%u %s ", x, spellings[o]);
            if (shift)
            {
                const uint32_t count = tier0_generate_below(random, tier0_class_bytes(class) * 8);
                tier0_emit(program, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, count);
                tier0_text(program, "%u", count);
            }
            else
            {
                tier0_generate_value_of(program, function, &state->scope, random, class, budget, options->floating, prefix, 1);
            }
            tier0_emit(program, operations[o], class, 0);
            tier0_emit(program, BUSTER_TYPED_SET_LOCAL, class, x);
            tier0_text(program, ";\n");
        }
        else if (form < 5 && tier0_is_arithmetic(class))
        {
            tier0_emit(program, BUSTER_TYPED_LOCAL, class, x);
            tier0_emit(program, BUSTER_TYPED_CONST, class, 1);
            tier0_emit(program, BUSTER_TYPED_ADD, class, 0);
            tier0_emit(program, BUSTER_TYPED_SET_LOCAL, class, x);
            tier0_text(program, "v%u++;\n", x);
        }
        else
        {
            // x = v, or x = y = v with another local of the class.
            uint32_t y = UINT32_MAX;
            for (uint32_t a = 0; a < state->assignable_count && form == 9; a += 1)
            {
                const uint32_t other = state->assignable[a];
                y = other != x && program->local_classes[function->first_local + other] == class ? other : y;
            }
            tier0_text(program, "v%u = ", x);
            if (y != UINT32_MAX)
            {
                tier0_text(program, "v%u = ", y);
            }
            tier0_generate_value_of(program, function, &state->scope, random, class, budget, options->floating, prefix, 0);
            tier0_text(program, ";\n");
            if (y != UINT32_MAX)
            {
                tier0_emit(program, BUSTER_TYPED_STORE_LOCAL, class, y);
            }
            tier0_emit(program, BUSTER_TYPED_SET_LOCAL, class, x);
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Functions and programs

// A function: its signature (the entry's fixed: three U64 parameters and a U64 result), its locals (first values set
// at the top, pointers aimed), its statements, and its final return.
static void tier0_generate_function(Tier0Program* program, uint32_t index, uint64_t* random, const Tier0ProgramOptions* options, const char* prefix)
{
    Tier0GeneratedFunction* function = &program->functions[index];
    const bool entry = index + 1 == program->function_count;
    memset(function, 0, sizeof(*function));
    function->first_entry = program->entry_count;
    function->first_local = program->local_count;
    function->first_call = program->call_count;
    Tier0FunctionState state;
    memset(&state, 0, sizeof(state));
    state.function = function;
    state.index = index;
    state.prefix = prefix;
    state.scope.first_local = function->first_local;
    for (uint32_t d = 0; d <= BUSTER_TIER0_GENERATE_NESTING; d += 1)
    {
        state.counters[d] = UINT32_MAX;
    }
    // The signature: up to 6 parameters mostly, and 7 to 12 one time in five, past the argument registers.
    const uint32_t parameters = entry ? 3 : tier0_generate_chance(random, 20) ? 7 + tier0_generate_below(random, 6) : tier0_generate_below(random, 7);
    function->result_class = (uint8_t)(entry ? BUSTER_TYPED_CLASS_U64 : tier0_generate_chance(random, 15) ? BUSTER_TYPED_CLASS_VOID : tier0_generate_class(random, options->floating));
    for (uint32_t p = 0; p < parameters; p += 1)
    {
        const uint32_t class = entry ? BUSTER_TYPED_CLASS_U64 : tier0_generate_class(random, options->floating);
        const uint32_t local = tier0_generate_local(program, &state, class, 0);
        if (local != UINT32_MAX)
        {
            tier0_scope_add(&state.scope, local, class);
            state.assignable[state.assignable_count] = local;
            state.assignable_count += 1;
        }
    }
    function->parameter_count = function->local_count;
    // The other locals, some with their addresses taken, and pointers to some of those and to global arrays.
    const uint32_t locals = 1 + tier0_generate_below(random, 8);
    uint32_t taken[BUSTER_TIER0_GENERATE_MAX_LOCALS];
    uint32_t taken_count = 0;
    for (uint32_t l = 0; l < locals; l += 1)
    {
        const uint32_t class = tier0_generate_class(random, options->floating);
        const bool address = options->pointers && tier0_generate_chance(random, 15);
        const uint32_t local = tier0_generate_local(program, &state, class, address ? BUSTER_TYPED_LOCAL_ADDRESS_TAKEN : 0);
        if (local != UINT32_MAX)
        {
            tier0_scope_add(&state.scope, local, class);
            state.assignable[state.assignable_count] = local;
            state.assignable_count += 1;
            if (address)
            {
                taken[taken_count] = local;
                taken_count += 1;
            }
        }
    }
    // The objects.
    const uint32_t objects = options->objects ? tier0_generate_below(random, BUSTER_TIER0_GENERATE_MAX_OBJECTS + 1) : 0;
    for (uint32_t o = 0; o < objects; o += 1)
    {
        tier0_generate_object(program, &state, random, options->floating);
    }
    // A pointer's target: an address-taken local (its index), a global array's element (UINT32_MAX less the global),
    // or an object's element or member (BUSTER_TIER0_GENERATE_OBJECT_TARGET).
    uint32_t pointers = 0;
    uint32_t pointer_targets[BUSTER_TIER0_GENERATE_MAX_LOCALS];
    for (uint32_t t = 0; t < taken_count; t += 1)
    {
        const uint32_t pointer = tier0_generate_local(program, &state, BUSTER_TYPED_CLASS_U64, 0);
        if (pointer != UINT32_MAX)
        {
            state.scope.pointers[state.scope.pointer_count] = pointer;
            state.scope.pointees[state.scope.pointer_count] = program->local_classes[function->first_local + taken[t]];
            pointer_targets[state.scope.pointer_count] = taken[t];
            state.scope.pointer_count += 1;
            pointers += 1;
        }
    }
    uint32_t array_pointers = 0;
    for (uint32_t g = 0; g < program->global_count && options->pointers; g += 1)
    {
        if (program->global_elements[g] != 1 && tier0_generate_chance(random, 30))
        {
            const uint32_t pointer = tier0_generate_local(program, &state, BUSTER_TYPED_CLASS_U64, 0);
            if (pointer != UINT32_MAX)
            {
                state.scope.pointers[state.scope.pointer_count] = pointer;
                state.scope.pointees[state.scope.pointer_count] = program->global_classes[g];
                pointer_targets[state.scope.pointer_count] = UINT32_MAX - g;
                state.scope.pointer_count += 1;
                array_pointers += 1;
            }
        }
    }
    for (uint32_t o = 0; o < state.scope.object_count && options->pointers; o += 1)
    {
        if (tier0_generate_chance(random, 40))
        {
            const Tier0Object* object = &state.scope.objects[o];
            const uint32_t part = tier0_generate_below(random, object->elements != 0 ? object->elements : object->members);
            const uint32_t pointer = tier0_generate_local(program, &state, BUSTER_TYPED_CLASS_U64, 0);
            if (pointer != UINT32_MAX)
            {
                state.scope.pointers[state.scope.pointer_count] = pointer;
                state.scope.pointees[state.scope.pointer_count] = object->classes[object->elements != 0 ? 0 : part];
                pointer_targets[state.scope.pointer_count] = BUSTER_TIER0_GENERATE_OBJECT_TARGET(o, part);
                state.scope.pointer_count += 1;
            }
        }
    }
    // Locals that hold an earlier function's address, for calls through them.
    const uint32_t function_pointers = options->indirect && index != 0 ? tier0_generate_below(random, BUSTER_TIER0_GENERATE_FUNCTION_POINTERS + 1) : 0;
    for (uint32_t k = 0; k < function_pointers; k += 1)
    {
        const uint32_t target = tier0_generate_below(random, index);
        const uint32_t local = tier0_generate_local(program, &state, BUSTER_TYPED_CLASS_U64, 0);
        if (local != UINT32_MAX)
        {
            state.function_pointers[state.function_pointer_count] = local;
            state.function_pointer_targets[state.function_pointer_count] = target;
            state.function_pointer_count += 1;
        }
    }
    // The header and the declarations.
    if (entry)
    {
        tier0_text(program, "%s %sentry(", tier0_c_type(function->result_class), prefix);
    }
    else
    {
        tier0_text(program, "%s %sf%u(", tier0_c_type(function->result_class), prefix, index);
    }
    for (uint32_t p = 0; p < function->parameter_count; p += 1)
    {
        tier0_text(program, "%s%s v%u", p == 0 ? "" : ", ", tier0_c_type(program->local_classes[function->first_local + p]), p);
    }
    tier0_text(program, "%s)\n{\n", function->parameter_count == 0 ? "void" : "");
    const uint32_t declared = function->local_count;
    // The counters are made as loops need them, so declare room for them: every nesting depth's.
    for (uint32_t d = 1; d <= BUSTER_TIER0_GENERATE_NESTING; d += 1)
    {
        state.counters[d] = options->loops ? tier0_generate_local(program, &state, BUSTER_TYPED_CLASS_I32, 0) : UINT32_MAX;
    }
    for (uint32_t l = function->parameter_count; l < function->local_count; l += 1)
    {
        uint32_t object = UINT32_MAX;
        for (uint32_t o = 0; o < state.scope.object_count; o += 1)
        {
            object = state.scope.objects[o].local == l ? o : object;
        }
        uint32_t pointee = UINT32_MAX;
        for (uint32_t p = 0; p < state.function_pointer_count; p += 1)
        {
            pointee = state.function_pointers[p] == l ? state.function_pointer_targets[p] : pointee;
        }
        if (object != UINT32_MAX)
        {
            tier0_declare_object(program, &state.scope.objects[object]);
        }
        else if (pointee != UINT32_MAX)
        {
            // A pointer to the function's type.
            const Tier0GeneratedFunction* target = &program->functions[pointee];
            tier0_text(program, "    %s (*v%u)(", tier0_c_type(target->result_class), l);
            for (uint32_t p = 0; p < target->parameter_count; p += 1)
            {
                tier0_text(program, "%s%s", p == 0 ? "" : ", ", tier0_c_type(program->local_classes[target->first_local + p]));
            }
            tier0_text(program, "%s);\n", target->parameter_count == 0 ? "void" : "");
        }
        else
        {
            tier0_text(program, "    %s v%u;\n", tier0_c_type(program->local_classes[function->first_local + l]), l);
        }
    }
    // The first values: constants (every element and member of the objects too), then the pointers' targets.
    for (uint32_t l = function->parameter_count; l < declared; l += 1)
    {
        const uint32_t class = program->local_classes[function->first_local + l];
        bool pointer = false;
        for (uint32_t p = 0; p < state.scope.pointer_count; p += 1)
        {
            pointer |= state.scope.pointers[p] == l;
        }
        for (uint32_t p = 0; p < state.function_pointer_count; p += 1)
        {
            pointer |= state.function_pointers[p] == l;
        }
        if (!pointer && class != BUSTER_TYPED_CLASS_VOID)
        {
            tier0_set_constant(program, &state, l, class, tier0_generate_value(random, class));
        }
    }
    for (uint32_t o = 0; o < state.scope.object_count; o += 1)
    {
        const Tier0Object* object = &state.scope.objects[o];
        const uint32_t parts = object->elements != 0 ? object->elements : object->members;
        for (uint32_t part = 0; part < parts; part += 1)
        {
            const uint32_t class = object->classes[object->elements != 0 ? 0 : part];
            const uint64_t value = tier0_generate_value(random, class);
            tier0_text(program, "    ");
            tier0_object_address(program, object, part);
            tier0_text(program, " = ");
            tier0_text_constant(program, class, value);
            tier0_text(program, ";\n");
            tier0_emit(program, BUSTER_TYPED_CONST, class, value);
            tier0_emit(program, BUSTER_TYPED_SET, class, 0);
        }
    }
    for (uint32_t p = 0; p < state.scope.pointer_count; p += 1)
    {
        const uint32_t pointer = state.scope.pointers[p];
        const uint32_t target = pointer_targets[p];
        if (target < BUSTER_TIER0_GENERATE_MAX_LOCALS)
        {
            tier0_emit(program, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, target);
            tier0_text(program, "    v%u = (unsigned long)&v%u;\n", pointer, target);
        }
        else if (target < BUSTER_TIER0_GENERATE_OBJECT_TARGET(BUSTER_TIER0_GENERATE_MAX_OBJECTS, 0))
        {
            const uint32_t at = target - BUSTER_TIER0_GENERATE_OBJECT_TARGET(0, 0);
            tier0_text(program, "    v%u = (unsigned long)&", pointer);
            tier0_object_address(program, &state.scope.objects[at >> 8], at & 0xff);
            tier0_text(program, ";\n");
        }
        else
        {
            const uint32_t g = UINT32_MAX - target;
            const uint32_t element = tier0_generate_below(random, BUSTER_TIER0_GENERATE_ARRAY_ELEMENTS);
            tier0_emit(program, BUSTER_TYPED_GLOBAL_ADDRESS, BUSTER_TYPED_CLASS_U64, g + 1);
            tier0_emit(program, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, element);
            tier0_emit(program, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, tier0_class_bytes(program->global_classes[g]));
            tier0_text(program, "    v%u = (unsigned long)&%sg%u[%u];\n", pointer, prefix, g, element);
        }
        tier0_emit(program, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, pointer);
    }
    for (uint32_t p = 0; p < state.function_pointer_count; p += 1)
    {
        tier0_emit(program, BUSTER_TYPED_GLOBAL_ADDRESS, BUSTER_TYPED_CLASS_U64, program->global_count + 1 + state.function_pointer_targets[p]);
        tier0_emit(program, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, state.function_pointers[p]);
        tier0_text(program, "    v%u = %sf%u;\n", state.function_pointers[p], prefix, state.function_pointer_targets[p]);
    }
    (void)pointers;
    (void)array_pointers;
    // The statements.
    state.constructs[0] = (Tier0Construct){ 0, UINT32_MAX, 0, 0, 0, 0, 0, 1 };
    uint32_t left = options->statements == 0 ? 0 : 1 + tier0_generate_below(random, options->statements);
    while ((left != 0 || state.depth != 0) && !program->overflow)
    {
        Tier0Construct* current = &state.constructs[state.depth];
        if (state.depth != 0 && (current->statements == 0 || left == 0))
        {
            tier0_close(program, &state, random);
        }
        else
        {
            current->statements -= state.depth != 0 ? 1 : 0;
            tier0_generate_statement(program, &state, random, options, &left);
        }
    }
    // The entry calls every function, folding their results into a local it returns with its own.
    if (entry)
    {
        const uint32_t sum = tier0_generate_local(program, &state, BUSTER_TYPED_CLASS_U64, 0);
        tier0_text(program, "    unsigned long v%u = 0;\n", sum);
        tier0_emit(program, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 0);
        tier0_emit(program, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, sum);
        for (uint32_t callee = 0; callee < index && options->calls; callee += 1)
        {
            const Tier0GeneratedFunction* target = &program->functions[callee];
            if (program->call_count >= program->call_capacity || program->argument_count + target->parameter_count > program->argument_capacity)
            {
                program->overflow = true;
                break;
            }
            const uint32_t call = program->call_count - function->first_call;
            program->call_callees[program->call_count] = callee;
            program->call_first[program->call_count] = program->argument_count;
            program->call_argument_counts[program->call_count] = target->parameter_count;
            program->call_indirect[program->call_count] = 0;
            program->call_count += 1;
            function->call_count += 1;
            const uint32_t result = target->result_class;
            if (result != BUSTER_TYPED_CLASS_VOID)
            {
                tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, sum);
                tier0_emit(program, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 31);
                tier0_emit(program, BUSTER_TYPED_MUL, BUSTER_TYPED_CLASS_U64, 0);
                tier0_text(program, "    v%u = v%u * 31 + (unsigned long)%s(", sum, sum, tier0_class_floating(result) ? "(long)" : "");
            }
            else
            {
                tier0_text(program, "    ");
            }
            tier0_text(program, "%sf%u(", prefix, callee);
            for (uint32_t p = 0; p < target->parameter_count; p += 1)
            {
                const uint32_t class = program->local_classes[target->first_local + p];
                program->argument_classes[program->argument_count] = (uint8_t)class;
                program->argument_count += 1;
                tier0_text(program, p == 0 ? "" : ", ");
                tier0_generate_value_of(program, function, &state.scope, random, class, 1 + tier0_generate_below(random, options->expression_nodes / 2 + 1),
                                        options->floating, prefix, 1 + p);
            }
            tier0_emit(program, BUSTER_TYPED_CALL, result, call);
            if (result != BUSTER_TYPED_CLASS_VOID)
            {
                if (tier0_class_floating(result))
                {
                    tier0_emit(program, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_I64, 0);
                }
                if (result != BUSTER_TYPED_CLASS_U64)
                {
                    tier0_emit(program, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
                }
                tier0_emit(program, BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0);
                tier0_emit(program, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, sum);
                tier0_text(program, "));\n");
            }
            else
            {
                tier0_text(program, ");\n");
            }
        }
        // The result: the sum with a value of the entry's own.
        tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, sum);
        tier0_text(program, "    return v%u + ", sum);
        tier0_generate_value_of(program, function, &state.scope, random, BUSTER_TYPED_CLASS_U64, options->expression_nodes, options->floating, prefix, 1);
        tier0_text(program, ";\n}\n\n");
        tier0_emit(program, BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0);
        tier0_emit(program, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_U64, 0);
    }
    else if (function->result_class != BUSTER_TYPED_CLASS_VOID)
    {
        tier0_text(program, "    return ");
        tier0_generate_value_of(program, function, &state.scope, random, function->result_class, 1 + tier0_generate_below(random, options->expression_nodes),
                                options->floating, prefix, 0);
        tier0_text(program, ";\n}\n\n");
        tier0_emit(program, BUSTER_TYPED_RETURN, function->result_class, 0);
    }
    else
    {
        tier0_text(program, "}\n\n");
    }
    function->entry_count = program->entry_count - function->first_entry;
}

// The program of a seed: its globals (scalars of every class, and arrays), then its functions, the entry last, into
// columns from `arena` (sized from the options), and its C text with every name prefixed by `prefix`. Returns false if
// something didn't fit, which a larger arena fixes.
static bool tier0_generate_program(Tier0Program* program, Arena* arena, uint64_t seed, Tier0ProgramOptions options, const char* prefix)
{
    memset(program, 0, sizeof(*program));
    uint64_t random = seed;
    const uint32_t functions = options.functions < 1 ? 1 : options.functions > BUSTER_TIER0_GENERATE_MAX_FUNCTIONS ? BUSTER_TIER0_GENERATE_MAX_FUNCTIONS : options.functions;
    program->function_count = functions;
    program->defined = options.defined;
    // Every statement can hold a few expressions and a call's arguments.
    const uint64_t per_function = (uint64_t)(options.statements + 16) * (4 * options.expression_nodes + 64) + 512;
    program->entry_capacity = (uint32_t)(per_function * functions);
    program->local_capacity = functions * BUSTER_TIER0_GENERATE_MAX_LOCALS;
    program->call_capacity = functions * (options.statements + functions + 1);
    program->argument_capacity = program->call_capacity * BUSTER_TIER0_GENERATE_MAX_PARAMETERS;
    program->text_capacity = program->entry_capacity * 24ull + 65536;
    program->kinds = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, program->entry_capacity);
    program->classes = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, program->entry_capacity);
    program->values = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, program->entry_capacity);
    program->local_classes = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, program->local_capacity);
    program->local_flags = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, program->local_capacity);
    program->local_sizes = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, program->local_capacity);
    program->local_alignments = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, program->local_capacity);
    program->call_callees = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, program->call_capacity);
    program->call_first = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, program->call_capacity);
    program->call_argument_counts = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, program->call_capacity);
    program->call_indirect = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, program->call_capacity);
    program->argument_classes = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, program->argument_capacity);
    program->global_values = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, BUSTER_TIER0_GENERATE_MAX_GLOBALS * BUSTER_TIER0_GENERATE_ARRAY_ELEMENTS);
    program->text = BUSTER_ARENA_PUSH_ARRAY(arena, char, program->text_capacity);
    bool result = program->kinds != 0 && program->classes != 0 && program->values != 0 && program->local_classes != 0 && program->local_flags != 0 &&
                  program->local_sizes != 0 && program->local_alignments != 0 && program->call_callees != 0 && program->call_first != 0 &&
                  program->call_argument_counts != 0 && program->call_indirect != 0 && program->argument_classes != 0 && program->global_values != 0 &&
                  program->text != 0;
    if (result)
    {
        program->text[0] = 0;
        // Globals: scalars and arrays of every class.
        program->global_count = 4 + tier0_generate_below(&random, 9);
        for (uint32_t g = 0; g < program->global_count; g += 1)
        {
            const uint32_t class = tier0_generate_class(&random, options.floating);
            const bool array = (tier0_is_arithmetic(class) || class == BUSTER_TYPED_CLASS_F64) && tier0_generate_chance(&random, 30);
            program->global_classes[g] = (uint8_t)class;
            program->global_elements[g] = array ? BUSTER_TIER0_GENERATE_ARRAY_ELEMENTS : 1;
            program->global_first_value[g] = program->global_value_count;
            tier0_text(program, "%s %sg%u", tier0_c_type(class), prefix, g);
            if (array)
            {
                tier0_text(program, "[%u] = { ", BUSTER_TIER0_GENERATE_ARRAY_ELEMENTS);
            }
            else
            {
                tier0_text(program, " = ");
            }
            for (uint32_t e = 0; e < program->global_elements[g]; e += 1)
            {
                const uint64_t value = tier0_generate_value(&random, class);
                program->global_values[program->global_value_count] = value;
                program->global_value_count += 1;
                tier0_text(program, e == 0 ? "" : ", ");
                tier0_text_constant(program, class, value);
            }
            tier0_text(program, array ? " };\n" : ";\n");
        }
        tier0_text(program, "\n");
        for (uint32_t f = 0; f < functions && !program->overflow; f += 1)
        {
            tier0_generate_function(program, f, &random, &options, prefix);
        }
        result = !program->overflow;
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// A program's functions as Tier 0 takes them

// A class's builtin type, for the calling convention.
static TypeId tier0_program_class_type(uint32_t class)
{
    static const TypeId types[BUSTER_TYPED_CLASSES] = {
        BUSTER_TYPE_VOID, BUSTER_TYPE_SCHAR, BUSTER_TYPE_UCHAR, BUSTER_TYPE_SHORT, BUSTER_TYPE_USHORT, BUSTER_TYPE_INT,
        BUSTER_TYPE_UINT, BUSTER_TYPE_LONG, BUSTER_TYPE_ULONG, BUSTER_TYPE_FLOAT, BUSTER_TYPE_DOUBLE,
    };
    return types[class < BUSTER_TYPED_CLASSES ? class : 0];
}

// The type table and the calling convention's, for the programs' signatures (abi_assign gives them the locations).
typedef struct Tier0Tables Tier0Tables;
struct Tier0Tables
{
    TypeTable types;
    Arena type_arena;
    Arena index_arena;
    AbiTable abi;
    Arena abi_arena;
};

static bool tier0_tables_open(Tier0Tables* tables)
{
    const TypeCapacity capacity = {
        .types = 256,
        .parameters = 1024,
        .tags = 16,
        .members = 64,
    };
    tables->type_arena = arena_reserve(type_capacity_bytes(capacity));
    tables->index_arena = arena_reserve(type_index_capacity_bytes(capacity));
    bool result = type_initialize(&tables->types, &tables->type_arena, &tables->index_arena, capacity);
    tables->abi_arena = arena_reserve(abi_capacity_bytes(tables->types.types.capacity));
    result = result && abi_initialize(&tables->abi, &tables->abi_arena, &tables->types, 16);
    return result;
}

static void tier0_tables_close(Tier0Tables* tables)
{
    arena_release(&tables->type_arena);
    arena_release(&tables->index_arena);
    arena_release(&tables->abi_arena);
}

// A program's functions as Tier 0 takes them, with every signature's locations from one abi_assign batch: each
// function's own (its parameters and result) and each call's (its callee's).
typedef struct Tier0Functions Tier0Functions;
struct Tier0Functions
{
    Tier0Function functions[BUSTER_TIER0_GENERATE_MAX_FUNCTIONS];
    uint32_t count;
    // SymbolIds: globals 1 to global_count, then the functions.
    SymbolId* call_targets;
    AbiLocation* parameter_locations;
    AbiLocation* argument_locations;
    AbiCall* entries;
    AbiCall* calls;
    uint8_t* call_variadic;
};

static bool tier0_program_functions(const Tier0Program* program, const Tier0Tables* tables, Arena* arena, Tier0Functions* functions)
{
    const uint32_t function_count = program->function_count;
    const uint32_t signatures = function_count + program->call_count;
    uint32_t parameters = 0;
    for (uint32_t f = 0; f < function_count; f += 1)
    {
        parameters += program->functions[f].parameter_count;
    }
    const uint32_t arguments = parameters + program->argument_count;
    TypeId* results = BUSTER_ARENA_PUSH_ARRAY(arena, TypeId, signatures + 1);
    uint32_t* counts = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, signatures + 1);
    uint8_t* variadic = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, signatures + 1);
    TypeId* types = BUSTER_ARENA_PUSH_ARRAY(arena, TypeId, arguments + 1);
    AbiLocation* locations = BUSTER_ARENA_PUSH_ARRAY(arena, AbiLocation, arguments + 1);
    AbiCall* calls = BUSTER_ARENA_PUSH_ARRAY(arena, AbiCall, signatures + 1);
    functions->call_targets = BUSTER_ARENA_PUSH_ARRAY(arena, SymbolId, program->call_count + 1);
    functions->call_variadic = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, program->call_count + 1);
    bool result = results != 0 && counts != 0 && variadic != 0 && types != 0 && locations != 0 && calls != 0 && functions->call_targets != 0 &&
                  functions->call_variadic != 0;
    if (result)
    {
        uint32_t argument = 0;
        for (uint32_t f = 0; f < function_count; f += 1)
        {
            const Tier0GeneratedFunction* function = &program->functions[f];
            results[f] = tier0_program_class_type(function->result_class);
            counts[f] = function->parameter_count;
            variadic[f] = 0;
            for (uint32_t p = 0; p < function->parameter_count; p += 1)
            {
                types[argument] = tier0_program_class_type(program->local_classes[function->first_local + p]);
                argument += 1;
            }
        }
        for (uint32_t c = 0; c < program->call_count; c += 1)
        {
            const Tier0GeneratedFunction* callee = &program->functions[program->call_callees[c]];
            results[function_count + c] = tier0_program_class_type(callee->result_class);
            counts[function_count + c] = program->call_argument_counts[c];
            variadic[function_count + c] = 0;
            for (uint32_t a = 0; a < program->call_argument_counts[c]; a += 1)
            {
                types[argument] = tier0_program_class_type(program->argument_classes[program->call_first[c] + a]);
                argument += 1;
            }
            functions->call_targets[c] = program->call_indirect[c] != 0 ? 0 : program->global_count + 1 + program->call_callees[c];
            functions->call_variadic[c] = 0;
        }
        const AbiSignatures batch = {
            .results = results,
            .counts = counts,
            .named = counts,
            .variadic = variadic,
            .count = signatures,
            .arguments = types,
        };
        abi_assign(&tables->abi, batch, locations, calls);
        // Each function's columns are its ranges of the program's; its calls' arguments index the program's argument
        // columns, which start after every function's parameters in the batch.
        functions->argument_locations = locations + parameters;
        functions->calls = calls + function_count;
        uint32_t parameter = 0;
        for (uint32_t f = 0; f < function_count; f += 1)
        {
            const Tier0GeneratedFunction* function = &program->functions[f];
            Tier0Function* t = &functions->functions[f];
            memset(t, 0, sizeof(*t));
            t->kinds = program->kinds + function->first_entry;
            t->classes = program->classes + function->first_entry;
            t->values = program->values + function->first_entry;
            t->count = function->entry_count;
            t->local_classes = program->local_classes + function->first_local;
            t->local_flags = program->local_flags + function->first_local;
            // The objects' columns for a function that has objects, as a front end would leave them out otherwise.
            bool objects = false;
            for (uint32_t l = 0; l < function->local_count; l += 1)
            {
                objects |= program->local_sizes[function->first_local + l] != 0;
            }
            t->local_sizes = objects ? program->local_sizes + function->first_local : 0;
            t->local_alignments = objects ? program->local_alignments + function->first_local : 0;
            t->local_count = function->local_count;
            t->parameter_count = function->parameter_count;
            t->parameters = locations + parameter;
            t->result = calls[f].result;
            parameter += function->parameter_count;
            t->call_targets = functions->call_targets + function->first_call;
            t->call_first = program->call_first + function->first_call;
            t->call_argument_counts = program->call_argument_counts + function->first_call;
            t->call_variadic = functions->call_variadic + function->first_call;
            t->calls = functions->calls + function->first_call;
            t->argument_locations = functions->argument_locations;
            t->argument_classes = program->argument_classes;
            t->call_count = function->call_count;
            t->argument_count = program->argument_count;
        }
        functions->count = function_count;
    }
    return result;
}
