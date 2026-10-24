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
// which is the order the tests add them to a link in. With the `access` option, each global is reached directly, through
// the GOT, or as a thread-local (_Thread_local in C) of the image's or through the GOT, and each function's address
// directly or through the GOT: the access column of Tier0Function (tier0_program_functions builds it), which changes how
// a symbol is reached and nothing it means.
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
#define BUSTER_TIER0_GENERATE_MAX_LOCALS 64
#define BUSTER_TIER0_GENERATE_ARRAY_ELEMENTS 16
#define BUSTER_TIER0_GENERATE_EXPRESSION_NODES 128
#define BUSTER_TIER0_GENERATE_NESTING 4
// A function's objects (local arrays and structs), at most, and a struct's members; and a pointer local's target that
// is object o's element or member `part`, among the targets past the locals.
#define BUSTER_TIER0_GENERATE_MAX_OBJECTS 3
#define BUSTER_TIER0_GENERATE_MAX_MEMBERS 5
// The variable-length arrays a function has in scope at once, at most, besides its objects.
#define BUSTER_TIER0_GENERATE_MAX_VLAS 4
#define BUSTER_TIER0_GENERATE_OBJECT_CAPACITY (BUSTER_TIER0_GENERATE_MAX_OBJECTS + BUSTER_TIER0_GENERATE_MAX_VLAS)
// A function's locals that hold a function's address, at most.
#define BUSTER_TIER0_GENERATE_FUNCTION_POINTERS 3
#define BUSTER_TIER0_GENERATE_OBJECT_TARGET(o, part) (BUSTER_TIER0_GENERATE_MAX_LOCALS + (o) * 256u + (part))
// The work a call of a function may cost, at most (statements run, as the generator counts them).
#define BUSTER_TIER0_GENERATE_WORK 20000
// A scope's objects, at most: the function's own (BUSTER_TIER0_GENERATE_MAX_OBJECTS), its records' (with
// Tier0ProgramOptions.records: its parameters that are records, and those it passes and returns), and its
// variable-length arrays, which a statement declares only while the scope has fewer than
// BUSTER_TIER0_GENERATE_OBJECT_CAPACITY objects.
#define BUSTER_TIER0_GENERATE_SCOPE_OBJECTS 16
_Static_assert(BUSTER_TIER0_GENERATE_SCOPE_OBJECTS >= BUSTER_TIER0_GENERATE_OBJECT_CAPACITY, "a scope holds its variable-length arrays");
// A function's parameters that are records, and the objects it has of records to pass and return, at most.
#define BUSTER_TIER0_GENERATE_RECORD_PARAMETERS 4
#define BUSTER_TIER0_GENERATE_RECORD_OBJECTS 4

// ---------------------------------------------------------------------------------------------------------------------
// Records

// The structs and unions programs pass and return by value (#137), a catalog that tier0_tables_open builds in the type
// table and classifies: byte arrays of 1 to 17 bytes, whose parts load in every way (exactly, overlapping, through a
// temporary) and the largest of which goes on the stack; floats and doubles (in XMM registers), mixed with integers
// both ways round, unions, a member aligned to 16 (one part of 16 bytes), the empty struct, and records past 16 bytes
// (MEMORY: on the stack, returned through a hidden pointer). Each is a shape: a member per letter, of a class (b u8,
// c i8, s i16, S u16, i i32, I u32, l i64, L u64, f f32, d f64), a union after "u:", its first member _Alignas(16)
// after "a:". A record's C name is "struct rN" or "union rN".
#define BUSTER_TIER0_GENERATE_RECORDS 64
#define BUSTER_TIER0_GENERATE_RECORD_MEMBERS 17
#define BUSTER_TIER0_GENERATE_NO_RECORD 0xff
static const char* const tier0_record_shapes[] = {
    "",
    "b",
    "bb",
    "bbb",
    "bbbb",
    "bbbbb",
    "bbbbbb",
    "bbbbbbb",
    "bbbbbbbb",
    "bbbbbbbbb",
    "bbbbbbbbbb",
    "bbbbbbbbbbb",
    "bbbbbbbbbbbb",
    "bbbbbbbbbbbbb",
    "bbbbbbbbbbbbbb",
    "bbbbbbbbbbbbbbb",
    "bbbbbbbbbbbbbbbb",
    "bbbbbbbbbbbbbbbbb",
    "f",
    "ff",
    "fff",
    "ffff",
    "d",
    "dd",
    "ddd",
    "dddd",
    "di",
    "id",
    "fi",
    "if",
    "ifd",
    "lf",
    "ffl",
    "cd",
    "sss",
    "Ss",
    "ic",
    "fc",
    "df",
    "fd",
    "cdc",
    "ll",
    "lll",
    "lc",
    "bL",
    "IcS",
    "u:if",
    "u:dl",
    "u:fdi",
    "u:dc",
    "u:ff",
    "u:dd",
    "u:bs",
    "a:d",
    "a:i",
    "a:f",
    "LLLLLLLLLLLLLLLLL",
    "ddddddddd",
    "iiiiiiiiiiiiiiiii",
};

// A record of the catalog: its members' classes and offsets, its size and alignment, and from the calling convention
// its type and how it is returned.
typedef struct Tier0Record Tier0Record;
struct Tier0Record
{
    uint8_t classes[BUSTER_TIER0_GENERATE_RECORD_MEMBERS];
    uint32_t offsets[BUSTER_TIER0_GENERATE_RECORD_MEMBERS];
    uint32_t members;
    uint32_t size;
    uint32_t alignment;
    bool is_union;
    bool aligned;
    TypeId type;
    AbiReturn result;
};

typedef struct Tier0Records Tier0Records;
struct Tier0Records
{
    Tier0Record records[BUSTER_TIER0_GENERATE_RECORDS];
    uint32_t count;
};

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
    // With `objects`, the catalog's records passed and returned by value (Tier0Tables.records), or 0 for none: a
    // program without them is the one it was before them, seed for seed.
    const Tier0Records* records;
    // Symbols reached otherwise than directly (Tier0Function.symbol_access): globals through the GOT and thread-locals
    // of both TLS accesses, and functions' addresses through the GOT.
    bool access;
    // Variable-length arrays: arrays declared in a block, of a length the program computes, on the stack (ALLOCA)
    // until their scope ends (STACK_SAVE and STACK_RESTORE). A program without them is the same as before they
    // existed, seed for seed.
    bool vlas;
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
    // A record the function returns (result_class VOID), or BUSTER_TIER0_GENERATE_NO_RECORD; returned through a hidden
    // pointer (MEMORY), the pointer is its parameter 0 (`hidden`), which C doesn't spell.
    uint8_t result_record;
    bool hidden;
    uint64_t work;
};

// A program: its functions' streams, locals and calls in shared columns (each function's a range of them), its globals
// (a class, an element count, 1 or BUSTER_TIER0_GENERATE_ARRAY_ELEMENTS, and their initial values) and its C text. An
// object local has class VOID and its size and alignment's log2 in `local_sizes` and `local_alignments` (0 for a value).
// With records, `local_records` says which locals are records' objects (a parameter's, or one passed and returned),
// `argument_records` which arguments are (their addresses), and `call_result_objects` which object receives a call's
// record (1 + the function's local, or 0). A program a test makes by hand may leave out `local_sizes`,
// `local_alignments`, `call_indirect` and the records' columns (0): no objects, no indirect calls and no records.
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
    uint8_t* local_records;
    uint32_t local_count;
    uint32_t local_capacity;
    uint32_t* call_callees;
    uint32_t* call_first;
    uint32_t* call_argument_counts;
    // Whether each call is indirect: its callee's address pushed before its arguments.
    uint8_t* call_indirect;
    uint32_t* call_result_objects;
    uint32_t call_count;
    uint32_t call_capacity;
    uint8_t* argument_classes;
    uint8_t* argument_records;
    uint32_t argument_count;
    uint32_t argument_capacity;
    uint8_t global_classes[BUSTER_TIER0_GENERATE_MAX_GLOBALS];
    uint32_t global_elements[BUSTER_TIER0_GENERATE_MAX_GLOBALS];
    uint64_t* global_values;
    uint32_t global_value_count;
    uint32_t global_first_value[BUSTER_TIER0_GENERATE_MAX_GLOBALS];
    uint32_t global_count;
    // The global the objects' bytes are folded into (UINT32_MAX without objects).
    uint32_t sink;
    // With the `access` option, how each global and each function's address is reached (BUSTER_TIER0_ACCESS_*: a
    // thread-local global, _Thread_local in C, by one of the TLS accesses); all direct without it.
    bool access;
    uint8_t global_access[BUSTER_TIER0_GENERATE_MAX_GLOBALS];
    uint8_t function_access[BUSTER_TIER0_GENERATE_MAX_FUNCTIONS];
    Tier0GeneratedFunction functions[BUSTER_TIER0_GENERATE_MAX_FUNCTIONS];
    uint32_t function_count;
    char* text;
    uint64_t text_length;
    uint64_t text_capacity;
    const Tier0Records* records;
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

// A record's C name (a struct or a union of the catalog's), or a class's for NO_RECORD and in a program without the
// catalog, whose zeroed columns hold no records.
static const char* tier0_c_record(const Tier0Records* records, uint32_t record, uint32_t class, char* buffer, uint32_t capacity)
{
    const char* name = tier0_c_type(class);
    if (records != 0 && record != BUSTER_TIER0_GENERATE_NO_RECORD)
    {
        snprintf(buffer, capacity, "%s r%u", records->records[record].is_union ? "union" : "struct", record);
        name = buffer;
    }
    return name;
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

// A global's access with the `access` option: two in five direct, one through the GOT, one a thread-local of the image's
// and one a thread-local through the GOT (BUSTER_TIER0_ACCESS_*).
static uint32_t tier0_generate_access(uint64_t* state)
{
    static const uint8_t accesses[5] = {
        BUSTER_TIER0_ACCESS_DIRECT, BUSTER_TIER0_ACCESS_DIRECT, BUSTER_TIER0_ACCESS_GOT, BUSTER_TIER0_ACCESS_LOCAL_EXEC, BUSTER_TIER0_ACCESS_INITIAL_EXEC,
    };
    return accesses[tier0_generate_below(state, 5)];
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
// A variable-length array's size in bytes (value: the array's local, and its size's local times 2^16), as C's sizeof.
#define BUSTER_TIER0_EXPRESSION_SIZE 16
// In an OBJECT_ELEMENT's and an ALIGNMENT's value: the object is a variable-length array, its address its local's value.
#define BUSTER_TIER0_EXPRESSION_VLA (1ull << 62)

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
// struct's member's). Objects of one `layout` (a twin and the object it copies) can be copied into one another. A
// record's object is of the catalog's `record` (BUSTER_TIER0_GENERATE_NO_RECORD for the function's own), whose layout
// is its record's, and a union's members aren't read apart (only their bits, by the checksum).
// A variable-length array (`vla`) is an array whose `local` is a U64 that holds its address (ALLOCA's), not a frame
// object, of `elements` or up to 3 more, which the program computes; `bytes` is the local that holds its size.
typedef struct Tier0Object Tier0Object;
struct Tier0Object
{
    uint32_t local;
    uint32_t elements;
    uint32_t members;
    uint8_t classes[BUSTER_TIER0_GENERATE_RECORD_MEMBERS];
    bool aligned[BUSTER_TIER0_GENERATE_RECORD_MEMBERS];
    uint32_t offsets[BUSTER_TIER0_GENERATE_RECORD_MEMBERS];
    uint32_t alignment;
    uint32_t size;
    uint32_t layout;
    uint8_t record;
    bool is_union;
    bool vla;
    uint32_t bytes;
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
    Tier0Object objects[BUSTER_TIER0_GENERATE_SCOPE_OBJECTS];
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
        // An object's element or member of the class, or for a U64 an object's address modulo its alignment, or a
        // variable-length array's size: each candidate as the object times 256 plus the member, an element as member
        // 255, the address as 254 and the size as 253.
        uint32_t candidates[BUSTER_TIER0_GENERATE_SCOPE_OBJECTS * (BUSTER_TIER0_GENERATE_RECORD_MEMBERS + 3)];
        uint32_t candidate_count = 0;
        const bool index_room = grow && tree->count < BUSTER_TIER0_GENERATE_EXPRESSION_NODES;
        for (uint32_t o = 0; o < scope->object_count; o += 1)
        {
            const Tier0Object* object = &scope->objects[o];
            for (uint32_t m = 0; m < object->members; m += 1)
            {
                candidates[candidate_count] = o << 8 | m;
                candidate_count += object->classes[m] == class && !object->is_union ? 1 : 0;
            }
            candidates[candidate_count] = o << 8 | 255;
            candidate_count += object->elements != 0 && object->classes[0] == class && index_room ? 1 : 0;
            candidates[candidate_count] = o << 8 | 254;
            candidate_count += class == BUSTER_TYPED_CLASS_U64 ? 1 : 0;
            candidates[candidate_count] = o << 8 | 253;
            candidate_count += object->vla && class == BUSTER_TYPED_CLASS_U64 ? 1 : 0;
        }
        if (candidate_count != 0)
        {
            // The node's value says all its emission and its C need: the local, and an array's element count, a
            // member's offset and number, or the alignment.
            const uint32_t candidate = candidates[tier0_generate_below(state, candidate_count)];
            const uint32_t part = candidate & 0xff;
            const Tier0Object* object = &scope->objects[candidate >> 8];
            const uint64_t vla = object->vla ? BUSTER_TIER0_EXPRESSION_VLA : 0;
            node->kind = part == 255   ? BUSTER_TIER0_EXPRESSION_OBJECT_ELEMENT
                         : part == 254 ? BUSTER_TIER0_EXPRESSION_ALIGNMENT
                         : part == 253 ? BUSTER_TIER0_EXPRESSION_SIZE
                                       : BUSTER_TIER0_EXPRESSION_MEMBER;
            node->value = part == 255   ? object->local | (uint64_t)object->elements << 32 | vla
                          : part == 254 ? object->local | (uint64_t)object->alignment << 32 | vla
                          : part == 253 ? object->local | (uint64_t)object->bytes << 16
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
                    const bool vla = (node->value & BUSTER_TIER0_EXPRESSION_VLA) != 0;
                    tier0_emit(program, vla ? BUSTER_TYPED_LOCAL : BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, (uint32_t)node->value);
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
                const uint64_t elements = object ? (node->value & ~BUSTER_TIER0_EXPRESSION_VLA) >> 32 : BUSTER_TIER0_GENERATE_ARRAY_ELEMENTS;
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
                tier0_emit(program, (node->value & BUSTER_TIER0_EXPRESSION_VLA) != 0 ? BUSTER_TYPED_LOCAL : BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64,
                           node->value & 0xffff);
                tier0_emit(program, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, ((node->value & ~BUSTER_TIER0_EXPRESSION_VLA) >> 32) - 1);
                tier0_emit(program, BUSTER_TYPED_AND, BUSTER_TYPED_CLASS_U64, 0);
                break;
            case BUSTER_TIER0_EXPRESSION_SIZE:
                tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, node->value >> 16 & 0xffff);
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
                tier0_text(program, ") & %u]", (uint32_t)((node->value & ~BUSTER_TIER0_EXPRESSION_VLA) >> 32) - 1);
            }
            break;
        case BUSTER_TIER0_EXPRESSION_MEMBER:
            tier0_text(program, "v%u.m%u", (uint32_t)(node->value & 0xffff), (uint32_t)(node->value >> 32));
            break;
        case BUSTER_TIER0_EXPRESSION_ALIGNMENT:
            tier0_text(program, "((unsigned long)&v%u & %uul)", (uint32_t)(node->value & 0xffff), (uint32_t)((node->value & ~BUSTER_TIER0_EXPRESSION_VLA) >> 32) - 1);
            break;
        case BUSTER_TIER0_EXPRESSION_SIZE:
            tier0_text(program, "(unsigned long)sizeof(v%u)", (uint32_t)(node->value & 0xffff));
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
    // The variable-length arrays declared in its block (the scope's last objects), and the local plus one that holds
    // the stack's position from before the first of them (STACK_SAVE's), which its end and the jumps out of it restore;
    // 0 for none.
    uint32_t vlas;
    uint32_t saved;
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
    // first function, and the functions of its type it may come to hold (a bit per function).
    uint32_t function_pointers[BUSTER_TIER0_GENERATE_FUNCTION_POINTERS];
    uint32_t function_pointer_targets[BUSTER_TIER0_GENERATE_FUNCTION_POINTERS];
    uint64_t function_pointer_candidates[BUSTER_TIER0_GENERATE_FUNCTION_POINTERS];
    uint32_t function_pointer_count;
    // The object each record's calls return into (call_result_objects), made at the first such call; UINT32_MAX before.
    uint32_t record_temporaries[BUSTER_TIER0_GENERATE_RECORDS];
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
        program->local_records[program->local_count] = BUSTER_TIER0_GENERATE_NO_RECORD;
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
// (an array's declaration, a struct's member), or a twin of the object before. Its local is of class VOID,
// address-taken, with its size and alignment. Returns false when the columns are full.
static bool tier0_generate_object(Tier0Program* program, Tier0FunctionState* state, uint64_t* random, bool floating)
{
    Tier0Object object;
    memset(&object, 0, sizeof(object));
    object.record = BUSTER_TIER0_GENERATE_NO_RECORD;
    // A twin, now and then, of the object before: its layout, as a second object of one type (a record's never: its
    // twins are the objects of its record, whose layout is the record's).
    const bool twin = state->scope.object_count != 0 && tier0_generate_chance(random, 40) && state->scope.objects[state->scope.object_count - 1].record == BUSTER_TIER0_GENERATE_NO_RECORD;
    const bool array = !twin && tier0_generate_chance(random, 55);
    if (twin)
    {
        object = state->scope.objects[state->scope.object_count - 1];
    }
    else if (array)
    {
        object.elements = 2u << tier0_generate_below(random, 4);
        object.members = 0;
        object.classes[0] = (uint8_t)tier0_generate_class(random, floating);
        object.aligned[0] = tier0_generate_chance(random, 30);
        object.alignment = object.aligned[0] ? 16 : tier0_class_bytes(object.classes[0]);
        object.size = object.elements * tier0_class_bytes(object.classes[0]);
        object.layout = state->scope.object_count;
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
        object.layout = state->scope.object_count;
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

// An object of record `record` for the current function: a parameter's, or one it passes, returns or receives a call's
// record into, in its scope with `scoped` (unless the scope is full). Its local is of class VOID, address-taken, with
// the record's size (at least a byte: an empty record's object still takes one, as a front end gives it) and alignment.
// Returns its local, or UINT32_MAX when the columns are full.
static uint32_t tier0_generate_record_object(Tier0Program* program, Tier0FunctionState* state, uint32_t record, bool scoped)
{
    const Tier0Record* layout = &program->records->records[record];
    const uint32_t local = tier0_generate_local(program, state, BUSTER_TYPED_CLASS_VOID, BUSTER_TYPED_LOCAL_ADDRESS_TAKEN);
    if (local != UINT32_MAX)
    {
        const uint32_t program_local = state->function->first_local + local;
        program->local_sizes[program_local] = layout->size != 0 ? layout->size : 1;
        program->local_alignments[program_local] = (uint8_t)__builtin_ctz(layout->alignment);
        program->local_records[program_local] = (uint8_t)record;
        if (scoped && state->scope.object_count < BUSTER_TIER0_GENERATE_SCOPE_OBJECTS)
        {
            Tier0Object* object = &state->scope.objects[state->scope.object_count];
            memset(object, 0, sizeof(*object));
            object->local = local;
            object->members = layout->members;
            memcpy(object->classes, layout->classes, sizeof(object->classes));
            memcpy(object->offsets, layout->offsets, sizeof(object->offsets));
            object->aligned[0] = layout->aligned;
            object->alignment = layout->alignment;
            object->size = layout->size;
            object->layout = BUSTER_TIER0_GENERATE_SCOPE_OBJECTS + record;
            object->record = (uint8_t)record;
            object->is_union = layout->is_union;
            state->scope.object_count += 1;
        }
    }
    return local;
}

// A scope's object of record `record`, a random one of them, or UINT32_MAX without one.
static uint32_t tier0_record_object(const Tier0Scope* scope, uint32_t record, uint64_t* random)
{
    uint32_t candidates[BUSTER_TIER0_GENERATE_SCOPE_OBJECTS];
    uint32_t count = 0;
    for (uint32_t o = 0; o < scope->object_count; o += 1)
    {
        candidates[count] = scope->objects[o].local;
        count += scope->objects[o].record == record ? 1 : 0;
    }
    return count != 0 ? candidates[tier0_generate_below(random, count)] : UINT32_MAX;
}

// Whether the function has two objects of one layout.
static bool tier0_has_twins(const Tier0Scope* scope)
{
    bool twins = false;
    for (uint32_t o = 1; o < scope->object_count; o += 1)
    {
        twins |= scope->objects[o].layout == scope->objects[o - 1].layout && scope->objects[o].size != 0;
    }
    return twins;
}

// A copy between two objects of one layout, if the function has them: all of the bytes or a random prefix of them,
// into the stream (COPY) and as C (memcpy, as an expression). Returns false without such objects.
static bool tier0_generate_copy(Tier0Program* program, const Tier0Scope* scope, uint64_t* random)
{
    uint32_t pairs[BUSTER_TIER0_GENERATE_SCOPE_OBJECTS * BUSTER_TIER0_GENERATE_SCOPE_OBJECTS];
    uint32_t pair_count = 0;
    for (uint32_t a = 0; a < scope->object_count; a += 1)
    {
        for (uint32_t b = 0; b < scope->object_count; b += 1)
        {
            // (An empty record has no bytes to copy.)
            pairs[pair_count] = a << 8 | b;
            pair_count += a != b && scope->objects[a].layout == scope->objects[b].layout && scope->objects[a].size != 0 ? 1 : 0;
        }
    }
    const bool made = pair_count != 0;
    if (made)
    {
        const uint32_t pair = pairs[tier0_generate_below(random, pair_count)];
        const Tier0Object* destination = &scope->objects[pair >> 8];
        const Tier0Object* source = &scope->objects[pair & 0xff];
        const uint32_t size = tier0_generate_chance(random, 60) ? destination->size : 1 + tier0_generate_below(random, destination->size);
        tier0_emit(program, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, destination->local);
        tier0_emit(program, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, source->local);
        tier0_emit(program, BUSTER_TYPED_COPY, BUSTER_TYPED_CLASS_VOID, size);
        tier0_text(program, "__builtin_memcpy(&v%u, &v%u, %u)", destination->local, source->local, size);
    }
    return made;
}

// An object's declaration, as C.
static void tier0_declare_object(Tier0Program* program, const Tier0Object* object)
{
    if (object->record != BUSTER_TIER0_GENERATE_NO_RECORD)
    {
        char name[32];
        tier0_text(program, "    %s v%u;\n", tier0_c_record(program->records, object->record, 0, name, sizeof(name)), object->local);
    }
    else if (object->elements != 0)
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

// An object's element (a constant index) or member: its address into the stream (LOCAL_ADDRESS, or a variable-length
// array's LOCAL, and the offset as a front end lowers it: an index times the element's size, a member's offset in
// bytes), and the lvalue as C.
static void tier0_object_address(Tier0Program* program, const Tier0Object* object, uint32_t part)
{
    tier0_emit(program, object->vla ? BUSTER_TYPED_LOCAL : BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, object->local);
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

// An object's bytes folded into the program's sink, an element or a member at a time as the unsigned integer of its
// size (its bits, a floating one's too) XORed in: as C, sink ^= *(unsigned T*)&part, which the cross-check compiles
// with -fno-strict-aliasing, as this repository's build is compiled (a floating part's bits read through an integer
// lvalue), indented by `indent` spaces. A floating part's bits are masked by whether it holds a number,
// & -(unsigned long)(part == part), so that every NaN folds as 0: C leaves a NaN's sign and payload to the
// implementation (clang folds a + -b into a - b, whose NaN keeps b's sign where Tier 0's flips it), and the
// cross-check compares floating globals with any two NaNs the same too. Padding, which C leaves undefined, isn't read,
// nor a variable-length array's elements past `elements`, which the program never writes.
static void tier0_generate_checksum_object(Tier0Program* program, const Tier0Object* object, const char* prefix, uint32_t indent)
{
    static const uint8_t raw_classes[9] = {
        [1] = BUSTER_TYPED_CLASS_U8,
        [2] = BUSTER_TYPED_CLASS_U16,
        [4] = BUSTER_TYPED_CLASS_U32,
        [8] = BUSTER_TYPED_CLASS_U64,
    };
    const uint32_t parts = object->elements != 0 ? object->elements : object->members;
    for (uint32_t part = 0; part < parts && program->sink != UINT32_MAX; part += 1)
    {
        const uint32_t class = object->classes[object->elements != 0 ? 0 : part];
        const uint32_t raw = raw_classes[tier0_class_bytes(class)];
        tier0_emit(program, BUSTER_TYPED_GLOBAL, BUSTER_TYPED_CLASS_U64, program->sink + 1);
        tier0_text(program, "%*s%sg%u ^= (unsigned long)*(%s*)&", (int)indent, "", prefix, program->sink, tier0_c_type(raw));
        tier0_object_address(program, object, part);
        tier0_emit(program, BUSTER_TYPED_LOAD, raw, 0);
        if (raw != BUSTER_TYPED_CLASS_U64)
        {
            tier0_emit(program, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
        }
        if (class == BUSTER_TYPED_CLASS_F32 || class == BUSTER_TYPED_CLASS_F64)
        {
            tier0_text(program, " & -(unsigned long)(");
            tier0_object_address(program, object, part);
            tier0_emit(program, BUSTER_TYPED_LOAD, class, 0);
            tier0_text(program, " == ");
            tier0_object_address(program, object, part);
            tier0_emit(program, BUSTER_TYPED_LOAD, class, 0);
            tier0_text(program, ")");
            tier0_emit(program, BUSTER_TYPED_EQ, BUSTER_TYPED_CLASS_I32, 0);
            tier0_emit(program, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
            tier0_emit(program, BUSTER_TYPED_NEG, BUSTER_TYPED_CLASS_U64, 0);
            tier0_emit(program, BUSTER_TYPED_AND, BUSTER_TYPED_CLASS_U64, 0);
        }
        tier0_text(program, ";\n");
        tier0_emit(program, BUSTER_TYPED_XOR, BUSTER_TYPED_CLASS_U64, 0);
        tier0_emit(program, BUSTER_TYPED_SET_GLOBAL, BUSTER_TYPED_CLASS_U64, program->sink + 1);
    }
}

// Every object's bytes folded into the program's sink (tier0_generate_checksum_object), as the function ends.
static void tier0_generate_checksum(Tier0Program* program, const Tier0Scope* scope, const char* prefix)
{
    for (uint32_t o = 0; o < scope->object_count && program->sink != UINT32_MAX; o += 1)
    {
        tier0_generate_checksum_object(program, &scope->objects[o], prefix, 4);
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

// The stack's position restored from the outermost of the constructs from `outer` to the innermost that saved one: the
// variable-length arrays of every scope a jump out of them leaves released (STACK_RESTORE).
static void tier0_restore_from(Tier0Program* program, const Tier0FunctionState* state, uint32_t outer)
{
    uint32_t saved = 0;
    for (uint32_t d = state->depth + 1; d > outer; d -= 1)
    {
        saved = state->constructs[d - 1].saved != 0 ? state->constructs[d - 1].saved : saved;
    }
    if (saved != 0)
    {
        tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, saved - 1);
        tier0_emit(program, BUSTER_TYPED_STACK_RESTORE, BUSTER_TYPED_CLASS_U64, 0);
    }
}

// A jump to a loop's label, patched when the loop closes, after the arrays of the scopes it leaves are released (the
// loop's body's among them: a continue goes to its test, or to a for's step).
static void tier0_loop_jump(Tier0Program* program, Tier0FunctionState* state, uint32_t loop, bool continues)
{
    tier0_restore_from(program, state, loop);
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

// Closes the innermost construct: first, at its block's end, its variable-length arrays' bytes folded into the sink and
// their stack released, and them out of the scope.
static void tier0_close(Tier0Program* program, Tier0FunctionState* state, uint64_t* random)
{
    Tier0GeneratedFunction* function = state->function;
    Tier0Construct* construct = &state->constructs[state->depth];
    const uint32_t depth = state->depth;
    for (uint32_t v = 0; v < construct->vlas; v += 1)
    {
        tier0_generate_checksum_object(program, &state->scope.objects[state->scope.object_count - construct->vlas + v], state->prefix, 4 * (depth + 1));
    }
    tier0_restore_from(program, state, depth);
    state->scope.object_count -= construct->vlas;
    construct->vlas = 0;
    construct->saved = 0;
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

// Whether two of the program's functions have one type: their results' and parameters' classes and records.
static bool tier0_same_signature(const Tier0Program* program, uint32_t a, uint32_t b)
{
    const Tier0GeneratedFunction* x = &program->functions[a];
    const Tier0GeneratedFunction* y = &program->functions[b];
    bool same = x->result_class == y->result_class && x->result_record == y->result_record && x->parameter_count == y->parameter_count;
    for (uint32_t p = 0; p < x->parameter_count && same; p += 1)
    {
        same = program->local_classes[x->first_local + p] == program->local_classes[y->first_local + p] &&
               program->local_records[x->first_local + p] == program->local_records[y->first_local + p];
    }
    return same;
}

// A function's type as C spells a pointer to it: `result (*name)(parameters)`, its hidden pointer left out.
static void tier0_text_function_pointer(Tier0Program* program, const Tier0GeneratedFunction* target, uint32_t local)
{
    char name[32];
    tier0_text(program, "    %s (*v%u)(", tier0_c_record(program->records, target->result_record, target->result_class, name, sizeof(name)), local);
    const uint32_t first = target->hidden ? 1 : 0;
    for (uint32_t p = first; p < target->parameter_count; p += 1)
    {
        const uint32_t at = target->first_local + p;
        tier0_text(program, "%s%s", p == first ? "" : ", ", tier0_c_record(program->records, program->local_records[at], program->local_classes[at], name, sizeof(name)));
    }
    tier0_text(program, "%s);\n", target->parameter_count == first ? "void" : "");
}

// A return of the function's result, as the stream has it and as C (`return` written already): a value's (an
// expression), or a record's (one of the scope's objects of it): its address in registers (result_size), its bytes
// copied through the hidden pointer, which it returns (MEMORY), or nothing (an empty record's IGNORE).
static void tier0_generate_return(Tier0Program* program, Tier0FunctionState* state, uint64_t* random, const Tier0ProgramOptions* options, uint32_t budget)
{
    Tier0GeneratedFunction* function = state->function;
    const uint32_t result = function->result_class;
    if (function->result_record != BUSTER_TIER0_GENERATE_NO_RECORD)
    {
        const Tier0Record* record = &program->records->records[function->result_record];
        const uint32_t object = tier0_record_object(&state->scope, function->result_record, random);
        tier0_text(program, " v%u", object);
        if (record->result.kind == BUSTER_ABI_RESULT_MEMORY)
        {
            tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 0);
            tier0_emit(program, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, object);
            tier0_emit(program, BUSTER_TYPED_COPY, BUSTER_TYPED_CLASS_VOID, record->size);
            tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 0);
            tier0_emit(program, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_U64, 0);
        }
        else if (record->result.kind == BUSTER_ABI_RESULT_REGISTERS)
        {
            tier0_emit(program, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, object);
            tier0_emit(program, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_U64, 0);
        }
        else
        {
            tier0_emit(program, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
        }
    }
    else if (result != BUSTER_TYPED_CLASS_VOID)
    {
        tier0_text(program, " ");
        tier0_generate_value_of(program, function, &state->scope, random, result, budget, options->floating, state->prefix, 0);
        tier0_emit(program, BUSTER_TYPED_RETURN, result, 0);
    }
    else
    {
        tier0_emit(program, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
    }
}

// A call's head as C: the assignment to its destination, if it has one, and its callee, by name or through the local
// that holds its address.
static void tier0_text_call(Tier0Program* program, uint32_t destination, uint32_t pointer, const char* prefix, uint32_t callee)
{
    if (destination != UINT32_MAX)
    {
        tier0_text(program, "v%u = ", destination);
    }
    if (pointer != UINT32_MAX)
    {
        tier0_text(program, "v%u(", pointer);
    }
    else
    {
        tier0_text(program, "%sf%u(", prefix, callee);
    }
}

// A random one of a set's bits (which isn't empty).
static uint32_t tier0_generate_bit(uint64_t* random, uint64_t bits)
{
    for (uint32_t k = tier0_generate_below(random, (uint32_t)__builtin_popcountll(bits)); k > 0; k -= 1)
    {
        bits &= bits - 1;
    }
    return (uint32_t)__builtin_ctzll(bits);
}

// A call of a random earlier function that fits the work budget, with random arguments, its result assigned to a
// local of its class or discarded; with the `indirect` option now and then through a pointer to it, a local that holds
// its address (which may have become another function's of its type: the budget takes the costliest it may hold) or
// the address itself. Returns false if no function fits.
static bool tier0_generate_call(Tier0Program* program, Tier0FunctionState* state, uint64_t* random, uint64_t multiplier, const Tier0ProgramOptions* options)
{
    Tier0GeneratedFunction* function = state->function;
    bool made = false;
    if (state->index != 0 && program->call_count < program->call_capacity)
    {
        const uint32_t callee = tier0_generate_below(random, state->index);
        const Tier0GeneratedFunction* target = &program->functions[callee];
        const bool indirect = options->indirect && tier0_generate_chance(random, 35);
        uint32_t pointer = UINT32_MAX;
        uint64_t candidates = 1ull << callee;
        for (uint32_t p = 0; p < state->function_pointer_count && indirect; p += 1)
        {
            const bool holds = (state->function_pointer_candidates[p] >> callee & 1) != 0;
            pointer = holds ? state->function_pointers[p] : pointer;
            candidates = holds ? state->function_pointer_candidates[p] : candidates;
        }
        uint64_t costliest = 0;
        for (uint64_t rest = candidates; rest != 0; rest &= rest - 1)
        {
            const uint64_t cost = program->functions[__builtin_ctzll(rest)].work;
            costliest = cost > costliest ? cost : costliest;
        }
        const uint64_t work = multiplier * (costliest + 1);
        // The callee's parameters that are records take objects of theirs from the scope: a call needs one of each. Its
        // hidden pointer, if it returns through one, isn't an argument: the call's result object is.
        const uint32_t spelled = target->hidden ? 1 : 0;
        const uint32_t arguments = target->parameter_count - spelled;
        uint32_t argument_objects[BUSTER_TIER0_GENERATE_MAX_PARAMETERS + 1];
        bool passable = true;
        for (uint32_t p = 0; p < arguments; p += 1)
        {
            const uint32_t record = program->local_records[target->first_local + spelled + p];
            argument_objects[p] = record != BUSTER_TIER0_GENERATE_NO_RECORD ? tier0_record_object(&state->scope, record, random) : UINT32_MAX;
            passable &= record == BUSTER_TIER0_GENERATE_NO_RECORD || argument_objects[p] != UINT32_MAX;
        }
        if (passable && function->work + work <= BUSTER_TIER0_GENERATE_WORK && program->argument_count + arguments <= program->argument_capacity)
        {
            function->work += work;
            const uint32_t call = program->call_count - function->first_call;
            program->call_callees[program->call_count] = callee;
            program->call_first[program->call_count] = program->argument_count;
            program->call_argument_counts[program->call_count] = arguments;
            program->call_indirect[program->call_count] = indirect ? 1 : 0;
            program->call_result_objects[program->call_count] = 0;
            const uint32_t program_call = program->call_count;
            program->call_count += 1;
            function->call_count += 1;
            const uint32_t result = target->result_class;
            uint32_t destination = UINT32_MAX;
            // A record's result: into the function's object for the record's calls, then copied to an object of the
            // record in the scope, or left there.
            const uint32_t result_record = target->result_record;
            if (result_record != BUSTER_TIER0_GENERATE_NO_RECORD)
            {
                if (state->record_temporaries[result_record] == UINT32_MAX)
                {
                    state->record_temporaries[result_record] = tier0_generate_record_object(program, state, result_record, false);
                }
                const uint32_t temporary = state->record_temporaries[result_record];
                program->call_result_objects[program_call] = temporary != UINT32_MAX ? temporary + 1 : 0;
                destination = tier0_generate_chance(random, 80) ? tier0_record_object(&state->scope, result_record, random) : UINT32_MAX;
            }
            else if (result != BUSTER_TYPED_CLASS_VOID && state->scope.local_counts[result] != 0 && tier0_generate_chance(random, 80))
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
            // Now and then a copy between twins before an argument: a COPY above the values before it. C leaves the
            // order of a call's arguments unspecified, and another of them may read the copy's destination, so a call
            // with a copy is written in C as a block of its own, its arguments into temporaries in the stream's order
            // and each copy a statement at its place there, then the call of the temporaries.
            bool copies[BUSTER_TIER0_GENERATE_MAX_PARAMETERS];
            bool sequenced = false;
            for (uint32_t p = 0; p < arguments; p += 1)
            {
                copies[p] = state->scope.object_count > 1 && tier0_generate_chance(random, 10) && tier0_has_twins(&state->scope);
                sequenced |= copies[p];
            }
            tier0_indent(program, state);
            tier0_text(program, sequenced ? "{\n" : "");
            // An indirect call's callee, before its arguments: the local that holds its address (C calls through it),
            // or its address (C names it; the call is the same).
            if (pointer != UINT32_MAX)
            {
                tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, pointer);
            }
            else if (indirect)
            {
                tier0_emit(program, BUSTER_TYPED_GLOBAL_ADDRESS, BUSTER_TYPED_CLASS_U64, program->global_count + 1 + callee);
            }
            if (!sequenced)
            {
                tier0_text_call(program, destination, pointer, state->prefix, callee);
            }
            for (uint32_t p = 0; p < arguments; p += 1)
            {
                // A record's argument is its object's address; C reads its bytes at the call, as Tier 0 does, after the
                // copies before it.
                const uint32_t record = program->local_records[target->first_local + spelled + p];
                const uint32_t class = record != BUSTER_TIER0_GENERATE_NO_RECORD ? BUSTER_TYPED_CLASS_U64 : program->local_classes[target->first_local + spelled + p];
                program->argument_classes[program->argument_count] = (uint8_t)class;
                program->argument_records[program->argument_count] = (uint8_t)record;
                program->argument_count += 1;
                if (copies[p])
                {
                    tier0_text(program, "    ");
                    tier0_generate_copy(program, &state->scope, random);
                    tier0_text(program, ";\n");
                }
                if (record != BUSTER_TIER0_GENERATE_NO_RECORD)
                {
                    tier0_emit(program, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, argument_objects[p]);
                    if (!sequenced)
                    {
                        tier0_text(program, p == 0 ? "v%u" : ", v%u", argument_objects[p]);
                    }
                }
                else
                {
                    if (sequenced)
                    {
                        tier0_text(program, "    %s t%u = ", tier0_c_type(class), p);
                    }
                    else
                    {
                        tier0_text(program, p == 0 ? "" : ", ");
                    }
                    tier0_generate_value_of(program, function, &state->scope, random, class, 1 + tier0_generate_below(random, options->expression_nodes / 2 + 1),
                                            options->floating, state->prefix, p + (indirect ? 1 : 0));
                    tier0_text(program, sequenced ? ";\n" : "");
                }
            }
            if (sequenced)
            {
                tier0_text(program, "    ");
                tier0_text_call(program, destination, pointer, state->prefix, callee);
                for (uint32_t p = 0; p < arguments; p += 1)
                {
                    const bool record = argument_objects[p] != UINT32_MAX;
                    tier0_text(program, p == 0 ? "%c%u" : ", %c%u", record ? 'v' : 't', record ? argument_objects[p] : p);
                }
                tier0_text(program, ");\n");
                tier0_indent(program, state);
                tier0_text(program, "}\n");
            }
            else
            {
                tier0_text(program, ");\n");
            }
            tier0_emit(program, BUSTER_TYPED_CALL, result, call);
            if (result_record != BUSTER_TIER0_GENERATE_NO_RECORD)
            {
                const uint32_t size = program->records->records[result_record].size;
                if (destination != UINT32_MAX && size != 0 && program->call_result_objects[program_call] != 0)
                {
                    tier0_emit(program, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, destination);
                    tier0_emit(program, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, program->call_result_objects[program_call] - 1);
                    tier0_emit(program, BUSTER_TYPED_COPY, BUSTER_TYPED_CLASS_VOID, size);
                }
            }
            else if (destination != UINT32_MAX)
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

// The locals a variable-length array takes (its address, its size and its block's saved position), with room left for
// the entry's sum.
#define BUSTER_TIER0_GENERATE_VLA_LOCALS 4

// A variable-length array declared in the innermost block: T v[((c) & 3) + n], of n = 2, 4 or 8 elements the program
// reads and writes (their indices masked to them) and up to 3 more, its size computed into a local (sizeof's) and taken
// from the stack (ALLOCA), after the stack's position is saved when it is the first of a block inside the function's
// (its end releases them all); then its n elements set. Nothing else can point into it: its scope ends with its block.
static void tier0_generate_vla(Tier0Program* program, Tier0FunctionState* state, uint64_t* random, const Tier0ProgramOptions* options, uint32_t budget)
{
    Tier0Construct* construct = &state->constructs[state->depth];
    Tier0Object object;
    memset(&object, 0, sizeof(object));
    object.vla = true;
    // Not a record's object (#137), which tier0_record_object would otherwise take it for: record 0's.
    object.record = BUSTER_TIER0_GENERATE_NO_RECORD;
    object.elements = 2u << tier0_generate_below(random, 3);
    object.classes[0] = (uint8_t)tier0_generate_class(random, options->floating);
    object.alignment = tier0_class_bytes(object.classes[0]);
    object.size = object.elements * object.alignment;
    // A layout no other object has: never a twin's.
    object.layout = UINT32_MAX - state->scope.object_count;
    const bool save = state->depth != 0 && construct->saved == 0;
    const uint32_t saved = save ? tier0_generate_local(program, state, BUSTER_TYPED_CLASS_U64, 0) : UINT32_MAX;
    object.bytes = tier0_generate_local(program, state, BUSTER_TYPED_CLASS_U64, 0);
    object.local = tier0_generate_local(program, state, BUSTER_TYPED_CLASS_U64, 0);
    if (!program->overflow)
    {
        if (save)
        {
            tier0_emit(program, BUSTER_TYPED_STACK_SAVE, BUSTER_TYPED_CLASS_U64, 0);
            tier0_emit(program, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, saved);
            construct->saved = saved + 1;
        }
        const uint32_t class = object.classes[0];
        tier0_indent(program, state);
        tier0_text(program, "%s v%u[((", tier0_c_type(class), object.local);
        tier0_generate_value_of(program, state->function, &state->scope, random, BUSTER_TYPED_CLASS_I32, budget, options->floating, state->prefix, 0);
        tier0_text(program, ") & 3) + %u];\n", object.elements);
        tier0_emit(program, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 3);
        tier0_emit(program, BUSTER_TYPED_AND, BUSTER_TYPED_CLASS_I32, 0);
        tier0_emit(program, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, object.elements);
        tier0_emit(program, BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_I32, 0);
        tier0_emit(program, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
        tier0_emit(program, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, tier0_class_bytes(class));
        tier0_emit(program, BUSTER_TYPED_MUL, BUSTER_TYPED_CLASS_U64, 0);
        tier0_emit(program, BUSTER_TYPED_STORE_LOCAL, BUSTER_TYPED_CLASS_U64, object.bytes);
        tier0_emit(program, BUSTER_TYPED_ALLOCA, BUSTER_TYPED_CLASS_U64, 0);
        tier0_emit(program, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, object.local);
        for (uint32_t e = 0; e < object.elements; e += 1)
        {
            const uint64_t value = tier0_generate_value(random, class);
            tier0_indent(program, state);
            tier0_object_address(program, &object, e);
            tier0_text(program, " = ");
            tier0_text_constant(program, class, value);
            tier0_text(program, ";\n");
            tier0_emit(program, BUSTER_TYPED_CONST, class, value);
            tier0_emit(program, BUSTER_TYPED_SET, class, 0);
        }
        state->scope.objects[state->scope.object_count] = object;
        state->scope.object_count += 1;
        construct->vlas += 1;
    }
}

// One statement, or the opening of a construct, at the current depth: with the `vlas` option, now and then a
// variable-length array's declaration (tier0_generate_vla).
static void tier0_generate_statement(Tier0Program* program, Tier0FunctionState* state, uint64_t* random, const Tier0ProgramOptions* options, uint32_t* left)
{
    Tier0GeneratedFunction* function = state->function;
    Tier0Construct* current = &state->constructs[state->depth];
    const uint64_t multiplier = current->multiplier;
    // Drawn only with the option, so that a program without arrays is the one the same seed gave before them.
    const bool vla = options->vlas && state->scope.object_count < BUSTER_TIER0_GENERATE_OBJECT_CAPACITY &&
                     function->local_count + BUSTER_TIER0_GENERATE_VLA_LOCALS <= BUSTER_TIER0_GENERATE_MAX_LOCALS && tier0_generate_chance(random, 8);
    const uint32_t budget = 1 + tier0_generate_below(random, options->expression_nodes);
    const uint32_t choice = tier0_generate_below(random, 100);
    const uint32_t loop = tier0_innermost_loop(state);
    const bool nest = state->depth < BUSTER_TIER0_GENERATE_NESTING && *left > 2;
    const char* prefix = state->prefix;
    *left -= 1;
    function->work += multiplier;
    if (vla)
    {
        tier0_generate_vla(program, state, random, options, budget);
    }
    else if (choice < 10 && nest)
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
        tier0_indent(program, state);
        tier0_text(program, "if (");
        tier0_generate_value_of(program, function, &state->scope, random, BUSTER_TYPED_CLASS_I32, budget, options->floating, prefix, 0);
        tier0_text(program, ") return");
        const uint32_t skip = tier0_emit(program, BUSTER_TYPED_BRANCH_FALSE, BUSTER_TYPED_CLASS_I32, 0);
        tier0_generate_return(program, state, random, options, budget);
        tier0_text(program, ";\n");
        tier0_patch(program, skip, tier0_here(program, function));
    }
    else if (choice < 30 && state->function_pointer_count != 0)
    {
        // A pointer to a function given one of its type or another, by a condition: p = c ? f : g, so that a call
        // through it reaches the function it holds when it runs.
        const uint32_t p = tier0_generate_below(random, state->function_pointer_count);
        const uint64_t candidates = state->function_pointer_candidates[p];
        const uint32_t when_true = tier0_generate_bit(random, candidates);
        const uint32_t when_false = tier0_generate_bit(random, candidates);
        tier0_indent(program, state);
        tier0_text(program, "v%u = (", state->function_pointers[p]);
        tier0_generate_value_of(program, function, &state->scope, random, BUSTER_TYPED_CLASS_I32, budget, options->floating, prefix, 0);
        tier0_text(program, ") ? %sf%u : %sf%u;\n", prefix, when_true, prefix, when_false);
        const uint32_t branch = tier0_emit(program, BUSTER_TYPED_BRANCH_FALSE, BUSTER_TYPED_CLASS_I32, 0);
        tier0_emit(program, BUSTER_TYPED_GLOBAL_ADDRESS, BUSTER_TYPED_CLASS_U64, program->global_count + 1 + when_true);
        const uint32_t jump = tier0_emit(program, BUSTER_TYPED_JUMP_VALUE, BUSTER_TYPED_CLASS_U64, 0);
        tier0_patch(program, branch, tier0_here(program, function));
        tier0_emit(program, BUSTER_TYPED_GLOBAL_ADDRESS, BUSTER_TYPED_CLASS_U64, program->global_count + 1 + when_false);
        tier0_patch(program, jump, tier0_here(program, function));
        tier0_emit(program, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, state->function_pointers[p]);
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
        // A copy between twins, or an object's element (plainly or op=, the address computed once) or member.
        const Tier0Object* object = &state->scope.objects[tier0_generate_below(random, state->scope.object_count)];
        tier0_indent(program, state);
        if (tier0_generate_chance(random, 25) && tier0_generate_copy(program, &state->scope, random))
        {
        }
        else if (object->elements != 0)
        {
            const uint32_t class = object->classes[0];
            const bool compound = tier0_is_arithmetic(class) && tier0_generate_chance(random, 40);
            tier0_emit(program, object->vla ? BUSTER_TYPED_LOCAL : BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, object->local);
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
        else if (object->members != 0)
        {
            const uint32_t member = tier0_generate_below(random, object->members);
            const uint32_t class = object->classes[member];
            // A union's floating member takes an integer's value, converted, so that it never holds a NaN: the
            // checksum masks a NaN's bits in the floating parts alone, and the union's other members would read them.
            const bool converted = object->is_union && (class == BUSTER_TYPED_CLASS_F32 || class == BUSTER_TYPED_CLASS_F64);
            tier0_object_address(program, object, member);
            if (converted)
            {
                tier0_text(program, " = (%s)(", tier0_c_type(class));
            }
            else
            {
                tier0_text(program, " = ");
            }
            tier0_generate_value_of(program, function, &state->scope, random, converted ? BUSTER_TYPED_CLASS_I32 : class, budget, options->floating, prefix, 1);
            if (converted)
            {
                tier0_text(program, ")");
                tier0_emit(program, BUSTER_TYPED_CONVERT, class, 0);
            }
            tier0_emit(program, BUSTER_TYPED_SET, class, 0);
        }
        // (An empty record's object has nothing to write: the statement is empty.)
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
    for (uint32_t r = 0; r < BUSTER_TIER0_GENERATE_RECORDS; r += 1)
    {
        state.record_temporaries[r] = UINT32_MAX;
    }
    // The signature: up to 6 parameters mostly, and 7 to 12 one time in five, past the argument registers. With the
    // `indirect` option, now and then an earlier function's, so that a pointer to a function can hold either. With
    // records (but for the entry), now and then a record's result, and parameters that are records, up to
    // BUSTER_TIER0_GENERATE_RECORD_PARAMETERS: objects in the scope. A record returned through a hidden pointer makes
    // that pointer parameter 0, a U64 that C doesn't spell.
    const Tier0Records* records = options->objects && !entry ? options->records : 0;
    const uint32_t copied = options->indirect && !entry && index != 0 && tier0_generate_chance(random, 40) ? tier0_generate_below(random, index) : UINT32_MAX;
    const Tier0GeneratedFunction* model = copied != UINT32_MAX ? &program->functions[copied] : 0;
    const uint32_t model_first = model != 0 ? model->first_local + (model->hidden ? 1u : 0u) : 0;
    const uint32_t parameters = model != 0 ? model->parameter_count - (model->hidden ? 1u : 0u)
                                : entry   ? 3
                                : tier0_generate_chance(random, 20) ? 7 + tier0_generate_below(random, 6)
                                                                    : tier0_generate_below(random, 7);
    function->result_class = (uint8_t)(model != 0 ? model->result_class
                                       : entry   ? BUSTER_TYPED_CLASS_U64
                                       : tier0_generate_chance(random, 15) ? BUSTER_TYPED_CLASS_VOID
                                                                           : tier0_generate_class(random, options->floating));
    function->result_record = BUSTER_TIER0_GENERATE_NO_RECORD;
    if (model != 0)
    {
        function->result_record = model->result_record;
    }
    else if (records != 0 && tier0_generate_chance(random, 25))
    {
        function->result_record = (uint8_t)tier0_generate_below(random, records->count);
        function->result_class = BUSTER_TYPED_CLASS_VOID;
    }
    function->hidden = function->result_record != BUSTER_TIER0_GENERATE_NO_RECORD && records->records[function->result_record].result.kind == BUSTER_ABI_RESULT_MEMORY;
    if (function->hidden)
    {
        tier0_generate_local(program, &state, BUSTER_TYPED_CLASS_U64, 0);
    }
    uint32_t record_parameters = 0;
    for (uint32_t p = 0; p < parameters; p += 1)
    {
        const uint32_t record = model != 0                                                                               ? program->local_records[model_first + p]
                                : records != 0 && record_parameters < BUSTER_TIER0_GENERATE_RECORD_PARAMETERS && tier0_generate_chance(random, 25)
                                    ? tier0_generate_below(random, records->count)
                                    : BUSTER_TIER0_GENERATE_NO_RECORD;
        if (record != BUSTER_TIER0_GENERATE_NO_RECORD)
        {
            tier0_generate_record_object(program, &state, record, true);
            record_parameters += 1;
        }
        else
        {
            const uint32_t class = model != 0 ? program->local_classes[model_first + p]
                                   : entry   ? BUSTER_TYPED_CLASS_U64
                                             : tier0_generate_class(random, options->floating);
            const uint32_t local = tier0_generate_local(program, &state, class, 0);
            if (local != UINT32_MAX)
            {
                tier0_scope_add(&state.scope, local, class);
                state.assignable[state.assignable_count] = local;
                state.assignable_count += 1;
            }
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
    // The records' objects, for what the function passes and returns: one of its result's, and of a few of the
    // records its earlier functions take (or any); the entry's, of each record its callees take, as far as they go.
    const Tier0Records* catalog = options->objects ? options->records : 0;
    if (catalog != 0 && function->result_record != BUSTER_TIER0_GENERATE_NO_RECORD)
    {
        tier0_generate_record_object(program, &state, function->result_record, true);
    }
    for (uint32_t f = 0; catalog != 0 && entry && f < index && state.scope.object_count < BUSTER_TIER0_GENERATE_SCOPE_OBJECTS - 2; f += 1)
    {
        const Tier0GeneratedFunction* callee = &program->functions[f];
        for (uint32_t p = 0; p < callee->parameter_count; p += 1)
        {
            const uint32_t record = program->local_records[callee->first_local + p];
            if (record != BUSTER_TIER0_GENERATE_NO_RECORD && tier0_record_object(&state.scope, record, random) == UINT32_MAX &&
                state.scope.object_count < BUSTER_TIER0_GENERATE_SCOPE_OBJECTS - 2)
            {
                tier0_generate_record_object(program, &state, record, true);
            }
        }
    }
    const uint32_t record_objects = catalog != 0 && !entry ? tier0_generate_below(random, BUSTER_TIER0_GENERATE_RECORD_OBJECTS) : 0;
    for (uint32_t k = 0; k < record_objects; k += 1)
    {
        uint32_t record = tier0_generate_below(random, catalog->count);
        if (index != 0 && tier0_generate_chance(random, 70))
        {
            const Tier0GeneratedFunction* callee = &program->functions[tier0_generate_below(random, index)];
            const uint32_t p = callee->parameter_count != 0 ? tier0_generate_below(random, callee->parameter_count) : 0;
            const uint32_t taken = callee->parameter_count != 0 ? program->local_records[callee->first_local + p] : BUSTER_TIER0_GENERATE_NO_RECORD;
            record = taken != BUSTER_TIER0_GENERATE_NO_RECORD ? taken : callee->result_record != BUSTER_TIER0_GENERATE_NO_RECORD ? callee->result_record : record;
        }
        tier0_generate_record_object(program, &state, record, true);
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
        if (tier0_generate_chance(random, 40) && !state.scope.objects[o].is_union && state.scope.objects[o].size != 0)
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
            uint64_t candidates = 0;
            for (uint32_t f = 0; f < index; f += 1)
            {
                candidates |= tier0_same_signature(program, f, target) ? 1ull << f : 0;
            }
            state.function_pointers[state.function_pointer_count] = local;
            state.function_pointer_targets[state.function_pointer_count] = target;
            state.function_pointer_candidates[state.function_pointer_count] = candidates;
            state.function_pointer_count += 1;
        }
    }
    // The header (without a hidden pointer, which C doesn't spell) and the declarations.
    char type_name[32];
    const char* result_name = tier0_c_record(program->records, function->result_record, function->result_class, type_name, sizeof(type_name));
    if (entry)
    {
        tier0_text(program, "%s %sentry(", result_name, prefix);
    }
    else
    {
        tier0_text(program, "%s %sf%u(", result_name, prefix, index);
    }
    const uint32_t spelled = function->hidden ? 1 : 0;
    for (uint32_t p = spelled; p < function->parameter_count; p += 1)
    {
        const uint32_t at = function->first_local + p;
        tier0_text(program, "%s%s v%u", p == spelled ? "" : ", ", tier0_c_record(program->records, program->local_records[at], program->local_classes[at], type_name, sizeof(type_name)),
                   p);
    }
    tier0_text(program, "%s)\n{\n", function->parameter_count == spelled ? "void" : "");
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
            tier0_text_function_pointer(program, &program->functions[pointee], l);
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
        // (A parameter's bytes are its argument's.)
        const Tier0Object* object = &state.scope.objects[o];
        const uint32_t parts = object->local < function->parameter_count ? 0 : object->elements != 0 ? object->elements : object->members;
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
        else if (target < BUSTER_TIER0_GENERATE_OBJECT_TARGET(BUSTER_TIER0_GENERATE_SCOPE_OBJECTS, 0))
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
    // The objects' bytes, into the sink, as the function ends.
    tier0_generate_checksum(program, &state.scope, prefix);
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
            // A callee that takes a record the entry has no object of isn't called here, and one that returns a record
            // returns it into the entry's object for its record's calls, then an object of the record, if it has one.
            const uint32_t spelled = target->hidden ? 1 : 0;
            const uint32_t arguments = target->parameter_count - spelled;
            uint32_t argument_objects[BUSTER_TIER0_GENERATE_MAX_PARAMETERS + 1];
            bool passable = true;
            for (uint32_t p = 0; p < arguments; p += 1)
            {
                const uint32_t record = program->local_records[target->first_local + spelled + p];
                argument_objects[p] = record != BUSTER_TIER0_GENERATE_NO_RECORD ? tier0_record_object(&state.scope, record, random) : UINT32_MAX;
                passable &= record == BUSTER_TIER0_GENERATE_NO_RECORD || argument_objects[p] != UINT32_MAX;
            }
            if (!passable)
            {
                continue;
            }
            if (program->call_count >= program->call_capacity || program->argument_count + arguments > program->argument_capacity)
            {
                program->overflow = true;
                break;
            }
            const uint32_t call = program->call_count - function->first_call;
            const uint32_t program_call = program->call_count;
            program->call_callees[program->call_count] = callee;
            program->call_first[program->call_count] = program->argument_count;
            program->call_argument_counts[program->call_count] = arguments;
            program->call_indirect[program->call_count] = 0;
            program->call_result_objects[program->call_count] = 0;
            program->call_count += 1;
            function->call_count += 1;
            const uint32_t result = target->result_class;
            const uint32_t result_record = target->result_record;
            uint32_t destination = UINT32_MAX;
            if (result_record != BUSTER_TIER0_GENERATE_NO_RECORD)
            {
                if (state.record_temporaries[result_record] == UINT32_MAX)
                {
                    state.record_temporaries[result_record] = tier0_generate_record_object(program, &state, result_record, false);
                }
                const uint32_t temporary = state.record_temporaries[result_record];
                program->call_result_objects[program_call] = temporary != UINT32_MAX ? temporary + 1 : 0;
                destination = tier0_record_object(&state.scope, result_record, random);
                if (destination != UINT32_MAX)
                {
                    tier0_text(program, "    v%u = ", destination);
                }
                else
                {
                    tier0_text(program, "    ");
                }
            }
            else if (result != BUSTER_TYPED_CLASS_VOID)
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
            for (uint32_t p = 0; p < arguments; p += 1)
            {
                const uint32_t record = program->local_records[target->first_local + spelled + p];
                const uint32_t class = record != BUSTER_TIER0_GENERATE_NO_RECORD ? BUSTER_TYPED_CLASS_U64 : program->local_classes[target->first_local + spelled + p];
                program->argument_classes[program->argument_count] = (uint8_t)class;
                program->argument_records[program->argument_count] = (uint8_t)record;
                program->argument_count += 1;
                tier0_text(program, p == 0 ? "" : ", ");
                if (record != BUSTER_TIER0_GENERATE_NO_RECORD)
                {
                    tier0_emit(program, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, argument_objects[p]);
                    tier0_text(program, "v%u", argument_objects[p]);
                }
                else
                {
                    tier0_generate_value_of(program, function, &state.scope, random, class, 1 + tier0_generate_below(random, options->expression_nodes / 2 + 1),
                                            options->floating, prefix, 1 + p);
                }
            }
            tier0_emit(program, BUSTER_TYPED_CALL, result, call);
            if (result_record != BUSTER_TIER0_GENERATE_NO_RECORD)
            {
                const uint32_t size = program->records->records[result_record].size;
                if (destination != UINT32_MAX && size != 0 && program->call_result_objects[program_call] != 0)
                {
                    tier0_emit(program, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, destination);
                    tier0_emit(program, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, program->call_result_objects[program_call] - 1);
                    tier0_emit(program, BUSTER_TYPED_COPY, BUSTER_TYPED_CLASS_VOID, size);
                }
                tier0_text(program, ");\n");
            }
            else if (result != BUSTER_TYPED_CLASS_VOID)
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
    else if (function->result_record != BUSTER_TIER0_GENERATE_NO_RECORD)
    {
        tier0_text(program, "    return");
        tier0_generate_return(program, &state, random, options, 0);
        tier0_text(program, ";\n}\n\n");
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
    program->call_result_objects = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, program->call_capacity);
    program->argument_classes = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, program->argument_capacity);
    program->argument_records = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, program->argument_capacity);
    program->local_records = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, program->local_capacity);
    program->global_values = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, BUSTER_TIER0_GENERATE_MAX_GLOBALS * BUSTER_TIER0_GENERATE_ARRAY_ELEMENTS);
    program->text = BUSTER_ARENA_PUSH_ARRAY(arena, char, program->text_capacity);
    bool result = program->kinds != 0 && program->classes != 0 && program->values != 0 && program->local_classes != 0 && program->local_flags != 0 &&
                  program->local_sizes != 0 && program->local_alignments != 0 && program->call_callees != 0 && program->call_first != 0 &&
                  program->call_argument_counts != 0 && program->call_indirect != 0 && program->argument_classes != 0 && program->global_values != 0 &&
                  program->text != 0 && program->call_result_objects != 0 && program->argument_records != 0 && program->local_records != 0;
    if (result)
    {
        program->text[0] = 0;
        program->records = options.objects ? options.records : 0;
        program->access = options.access;
        // The records' declarations, every one the catalog has.
        for (uint32_t r = 0; program->records != 0 && r < program->records->count; r += 1)
        {
            const Tier0Record* record = &program->records->records[r];
            tier0_text(program, "%s r%u {", record->is_union ? "union" : "struct", r);
            for (uint32_t m = 0; m < record->members; m += 1)
            {
                tier0_text(program, " %s%s m%u;", record->aligned && m == 0 ? "_Alignas(16) " : "", tier0_c_type(record->classes[m]), m);
            }
            tier0_text(program, " };\n");
        }
        // Globals: scalars and arrays of every class, and with `access` two in five direct and the rest through the GOT,
        // or thread-locals of the image's or through the GOT.
        program->global_count = 4 + tier0_generate_below(&random, 9);
        for (uint32_t g = 0; g < program->global_count; g += 1)
        {
            const uint32_t class = tier0_generate_class(&random, options.floating);
            const bool array = (tier0_is_arithmetic(class) || class == BUSTER_TYPED_CLASS_F64) && tier0_generate_chance(&random, 30);
            program->global_classes[g] = (uint8_t)class;
            program->global_elements[g] = array ? BUSTER_TIER0_GENERATE_ARRAY_ELEMENTS : 1;
            program->global_first_value[g] = program->global_value_count;
            program->global_access[g] = (uint8_t)(options.access ? tier0_generate_access(&random) : BUSTER_TIER0_ACCESS_DIRECT);
            tier0_text(program, "%s%s %sg%u", program->global_access[g] >= BUSTER_TIER0_ACCESS_LOCAL_EXEC ? "_Thread_local " : "", tier0_c_type(class), prefix, g);
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
        // With objects, a global the functions fold their objects' bytes into before they return, so that a byte a
        // copy or a store puts wrong anywhere in them shows in the globals.
        program->sink = UINT32_MAX;
        if (options.objects && program->global_count < BUSTER_TIER0_GENERATE_MAX_GLOBALS)
        {
            program->sink = program->global_count;
            program->global_classes[program->sink] = BUSTER_TYPED_CLASS_U64;
            program->global_elements[program->sink] = 1;
            program->global_first_value[program->sink] = program->global_value_count;
            program->global_values[program->global_value_count] = 0;
            program->global_value_count += 1;
            program->global_access[program->sink] = (uint8_t)(options.access ? tier0_generate_access(&random) : BUSTER_TIER0_ACCESS_DIRECT);
            program->global_count += 1;
            tier0_text(program, "%sunsigned long %sg%u = 0;\n", program->global_access[program->sink] >= BUSTER_TIER0_ACCESS_LOCAL_EXEC ? "_Thread_local " : "",
                       prefix, program->sink);
        }
        // With `access`, half the functions' addresses through the GOT.
        for (uint32_t f = 0; f < functions && options.access; f += 1)
        {
            program->function_access[f] = (uint8_t)(tier0_generate_chance(&random, 50) ? BUSTER_TIER0_ACCESS_GOT : BUSTER_TIER0_ACCESS_DIRECT);
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

// The type table and the calling convention's, for the programs' signatures (abi_assign gives them the locations), and
// the records' catalog, in both.
typedef struct Tier0Tables Tier0Tables;
struct Tier0Tables
{
    TypeTable types;
    Arena type_arena;
    Arena index_arena;
    AbiTable abi;
    Arena abi_arena;
    Tier0Records records;
};

// A record shape's member class (tier0_record_shapes' letters).
static uint32_t tier0_record_class(char letter)
{
    const uint32_t class = letter == 'b' ? BUSTER_TYPED_CLASS_U8
                           : letter == 'c' ? BUSTER_TYPED_CLASS_I8
                           : letter == 's' ? BUSTER_TYPED_CLASS_I16
                           : letter == 'S' ? BUSTER_TYPED_CLASS_U16
                           : letter == 'i' ? BUSTER_TYPED_CLASS_I32
                           : letter == 'I' ? BUSTER_TYPED_CLASS_U32
                           : letter == 'l' ? BUSTER_TYPED_CLASS_I64
                           : letter == 'L' ? BUSTER_TYPED_CLASS_U64
                           : letter == 'f' ? BUSTER_TYPED_CLASS_F32
                                           : BUSTER_TYPED_CLASS_F64;
    return class;
}

// The catalog's records: each laid out as C lays it out (the generator's offsets), made in the type table, which must
// lay it out alike, and classified, for how functions return it.
static bool tier0_records_build(Tier0Tables* tables)
{
    Tier0Records* records = &tables->records;
    memset(records, 0, sizeof(*records));
    _Static_assert(BUSTER_ARRAY_LENGTH(tier0_record_shapes) <= BUSTER_TIER0_GENERATE_RECORDS, "the catalog's records fit");
    bool result = true;
    for (uint32_t r = 0; r < BUSTER_ARRAY_LENGTH(tier0_record_shapes) && result; r += 1)
    {
        const char* shape = tier0_record_shapes[r];
        Tier0Record* record = &records->records[r];
        record->is_union = shape[0] == 'u' && shape[1] == ':';
        record->aligned = shape[0] == 'a' && shape[1] == ':';
        const char* letters = record->is_union || record->aligned ? shape + 2 : shape;
        TypeMember members[BUSTER_TIER0_GENERATE_RECORD_MEMBERS];
        uint32_t offset = 0;
        uint32_t end = 0;
        record->alignment = 1;
        while (letters[record->members] != 0 && record->members < BUSTER_TIER0_GENERATE_RECORD_MEMBERS)
        {
            const uint32_t m = record->members;
            const uint32_t class = tier0_record_class(letters[m]);
            const uint32_t bytes = tier0_class_bytes(class);
            const uint32_t alignment = record->aligned && m == 0 ? 16 : bytes;
            offset = record->is_union ? 0 : (offset + alignment - 1) & ~(alignment - 1);
            record->classes[m] = (uint8_t)class;
            record->offsets[m] = offset;
            offset += bytes;
            end = offset > end ? offset : end;
            record->alignment = alignment > record->alignment ? alignment : record->alignment;
            members[m] = (TypeMember){
                .name = m + 1,
                .type = tier0_program_class_type(class),
                .bit_width = BUSTER_TYPE_NOT_BIT_FIELD,
                .alignment_log2 = (uint8_t)(record->aligned && m == 0 ? 5 : 0),
                .packed = false,
            };
            record->members += 1;
        }
        record->size = (end + record->alignment - 1) & ~(record->alignment - 1);
        const TypeRecordAttributes attributes = {
            .packed = false,
            .pack_log2 = 0,
            .alignment_log2 = 0,
        };
        record->type = type_tag(&tables->types, record->is_union ? BUSTER_TYPE_KIND_UNION : BUSTER_TYPE_KIND_STRUCT, 1000 + r);
        result = record->type != 0 && type_complete_record(&tables->types, record->type, members, record->members, attributes) &&
                 type_size(&tables->types, record->type) == record->size && (1u << type_alignment_log2(&tables->types, record->type)) == record->alignment;
        records->count += 1;
    }
    if (result)
    {
        abi_classify_all(&tables->abi, &tables->types);
        for (uint32_t r = 0; r < records->count; r += 1)
        {
            const uint32_t index = type_index(records->records[r].type);
            records->records[r].result = tables->abi.results[index];
            result = result && (tables->abi.pass[index] & BUSTER_ABI_PASS_CLASSIFIED) != 0 && (tables->abi.pass[index] & BUSTER_ABI_PASS_UNSUPPORTED) == 0;
        }
    }
    return result;
}

static bool tier0_tables_open(Tier0Tables* tables)
{
    const TypeCapacity capacity = {
        .types = 512,
        .parameters = 1024,
        .tags = 128,
        .members = 512,
    };
    tables->type_arena = arena_reserve(type_capacity_bytes(capacity));
    tables->index_arena = arena_reserve(type_index_capacity_bytes(capacity));
    bool result = type_initialize(&tables->types, &tables->type_arena, &tables->index_arena, capacity);
    tables->abi_arena = arena_reserve(abi_capacity_bytes(tables->types.types.capacity));
    result = result && abi_initialize(&tables->abi, &tables->abi_arena, &tables->types, 16);
    result = result && tier0_records_build(tables);
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
    // With the `access` option, the program's access column by SymbolId (every function's), and 0 without it.
    uint8_t* symbol_access;
    uint32_t symbol_count;
    AbiLocation* parameter_locations;
    AbiLocation* argument_locations;
    AbiCall* entries;
    AbiCall* calls;
    uint8_t* call_variadic;
    // With records: each argument's bytes (0 for a value's), and each function's parameters' locations when it returns
    // through a hidden pointer (rdi's first, then its signature's).
    uint32_t* argument_sizes;
    AbiLocation* hidden_parameters;
};

static bool tier0_program_functions(const Tier0Program* program, const Tier0Tables* tables, Arena* arena, Tier0Functions* functions)
{
    const uint32_t function_count = program->function_count;
    const uint32_t signatures = function_count + program->call_count;
    // A record's type, or a class's. A program without the catalog (a hand-made one, whose columns are zeroed, so that
    // a function's result_record reads as record 0) has no records.
    const Tier0Records* records = program->records;
    uint32_t parameters = 0;
    uint32_t hidden = 0;
    for (uint32_t f = 0; f < function_count; f += 1)
    {
        parameters += program->functions[f].parameter_count - (program->functions[f].hidden ? 1 : 0);
        hidden += program->functions[f].hidden ? program->functions[f].parameter_count : 0;
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
    functions->argument_sizes = records != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, program->argument_count + 1) : 0;
    functions->hidden_parameters = records != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, AbiLocation, hidden + 1) : 0;
    functions->symbol_count = program->access ? program->global_count + function_count + 1 : 0;
    functions->symbol_access = program->access ? BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, functions->symbol_count) : 0;
    bool result = results != 0 && counts != 0 && variadic != 0 && types != 0 && locations != 0 && calls != 0 && functions->call_targets != 0 &&
                  functions->call_variadic != 0 && (records == 0 || (functions->argument_sizes != 0 && functions->hidden_parameters != 0)) &&
                  (!program->access || functions->symbol_access != 0);
    if (result && program->access)
    {
        functions->symbol_access[0] = BUSTER_TIER0_ACCESS_DIRECT;
        memcpy(functions->symbol_access + 1, program->global_access, program->global_count);
        memcpy(functions->symbol_access + 1 + program->global_count, program->function_access, function_count);
    }
    if (result)
    {
        uint32_t argument = 0;
        for (uint32_t f = 0; f < function_count; f += 1)
        {
            const Tier0GeneratedFunction* function = &program->functions[f];
            const uint32_t spelled = function->hidden ? 1 : 0;
            const uint32_t result_record = records != 0 ? function->result_record : BUSTER_TIER0_GENERATE_NO_RECORD;
            results[f] = result_record != BUSTER_TIER0_GENERATE_NO_RECORD ? records->records[result_record].type : tier0_program_class_type(function->result_class);
            counts[f] = function->parameter_count - spelled;
            variadic[f] = 0;
            for (uint32_t p = spelled; p < function->parameter_count; p += 1)
            {
                const uint32_t at = function->first_local + p;
                const uint32_t record = records != 0 ? program->local_records[at] : BUSTER_TIER0_GENERATE_NO_RECORD;
                types[argument] = record != BUSTER_TIER0_GENERATE_NO_RECORD ? records->records[record].type : tier0_program_class_type(program->local_classes[at]);
                argument += 1;
            }
        }
        for (uint32_t c = 0; c < program->call_count; c += 1)
        {
            const Tier0GeneratedFunction* callee = &program->functions[program->call_callees[c]];
            const uint32_t result_record = records != 0 ? callee->result_record : BUSTER_TIER0_GENERATE_NO_RECORD;
            results[function_count + c] = result_record != BUSTER_TIER0_GENERATE_NO_RECORD ? records->records[result_record].type
                                                                                           : tier0_program_class_type(callee->result_class);
            counts[function_count + c] = program->call_argument_counts[c];
            variadic[function_count + c] = 0;
            for (uint32_t a = 0; a < program->call_argument_counts[c]; a += 1)
            {
                const uint32_t at = program->call_first[c] + a;
                const uint32_t record = records != 0 ? program->argument_records[at] : BUSTER_TIER0_GENERATE_NO_RECORD;
                types[argument] = record != BUSTER_TIER0_GENERATE_NO_RECORD ? records->records[record].type : tier0_program_class_type(program->argument_classes[at]);
                if (records != 0)
                {
                    functions->argument_sizes[at] = record != BUSTER_TIER0_GENERATE_NO_RECORD ? records->records[record].size : 0;
                }
                argument += 1;
            }
            const bool indirect = program->call_indirect != 0 && program->call_indirect[c] != 0;
            functions->call_targets[c] = indirect ? 0 : program->global_count + 1 + program->call_callees[c];
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
        uint32_t hidden_parameter = 0;
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
                objects |= program->local_sizes != 0 && program->local_sizes[function->first_local + l] != 0;
            }
            t->local_sizes = objects ? program->local_sizes + function->first_local : 0;
            t->local_alignments = objects ? program->local_alignments + function->first_local : 0;
            t->local_count = function->local_count;
            t->parameter_count = function->parameter_count;
            t->parameters = locations + parameter;
            t->result = calls[f].result;
            if (function->hidden)
            {
                // The hidden pointer in rdi, which the signature's locations leave free, then the parameters.
                AbiLocation* list = functions->hidden_parameters + hidden_parameter;
                list[0] = (AbiLocation){ .kind = BUSTER_ABI_LOCATION_REGISTERS, .registers = { BUSTER_ABI_RDI, BUSTER_ABI_REGISTER_NONE } };
                memcpy(list + 1, locations + parameter, (function->parameter_count - 1) * sizeof(AbiLocation));
                t->parameters = list;
                hidden_parameter += function->parameter_count;
            }
            parameter += function->parameter_count - (function->hidden ? 1 : 0);
            const bool registers = records != 0 && function->result_record != BUSTER_TIER0_GENERATE_NO_RECORD && calls[f].result.kind == BUSTER_ABI_RESULT_REGISTERS;
            t->result_size = registers ? records->records[function->result_record].size : 0;
            t->argument_sizes = functions->argument_sizes;
            t->call_result_objects = records != 0 ? program->call_result_objects + function->first_call : 0;
            t->call_targets = functions->call_targets + function->first_call;
            t->call_first = program->call_first + function->first_call;
            t->call_argument_counts = program->call_argument_counts + function->first_call;
            t->call_variadic = functions->call_variadic + function->first_call;
            t->calls = functions->calls + function->first_call;
            t->argument_locations = functions->argument_locations;
            t->argument_classes = program->argument_classes;
            t->call_count = function->call_count;
            t->argument_count = program->argument_count;
            t->symbol_access = functions->symbol_access;
            t->symbol_count = functions->symbol_count;
        }
        functions->count = function_count;
    }
    return result;
}
