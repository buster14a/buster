// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Checked executable roots to Tier0Function. This separate boundary may refuse a machine class while leaving
// CheckProgram valid. Explicit worklists preserve evaluation and side effects; graph postorder is never mistaken
// for executable order. ABI signatures from every body and call are assigned together after type classification.
// Source constant proofs remain intact; FP_ENVIRONMENT requires runtime evaluation. Math leaves transport raw
// IEEE bits, and errno-enabled leaves receive the target libc accessor's real int pointer. Runtime dependencies
// append symbols after the source/helper map and do not invent source declarations or change binding identities.
#include "buster/compiler/x86_target.c"

// The runtime's functions that the lowering calls of its own accord: the C library's errno accessor, and libgcc's, which
// clang -O0 calls where Tier 0 has no entry: the 128-bit integers' division and remainder, and their conversions to and
// from the floating types; and the complex types' multiplication and division, of each floating element.
typedef enum CheckLowerRuntime
{
    CHECK_LOWER_RUNTIME_ERRNO,
    CHECK_LOWER_RUNTIME_DIVTI3,
    CHECK_LOWER_RUNTIME_UDIVTI3,
    CHECK_LOWER_RUNTIME_MODTI3,
    CHECK_LOWER_RUNTIME_UMODTI3,
    CHECK_LOWER_RUNTIME_FLOATTISF,
    CHECK_LOWER_RUNTIME_FLOATTIDF,
    CHECK_LOWER_RUNTIME_FLOATTIXF,
    CHECK_LOWER_RUNTIME_FLOATUNTISF,
    CHECK_LOWER_RUNTIME_FLOATUNTIDF,
    CHECK_LOWER_RUNTIME_FLOATUNTIXF,
    CHECK_LOWER_RUNTIME_FIXSFTI,
    CHECK_LOWER_RUNTIME_FIXDFTI,
    CHECK_LOWER_RUNTIME_FIXXFTI,
    CHECK_LOWER_RUNTIME_FIXUNSSFTI,
    CHECK_LOWER_RUNTIME_FIXUNSDFTI,
    CHECK_LOWER_RUNTIME_FIXUNSXFTI,
    CHECK_LOWER_RUNTIME_MULSC3,
    CHECK_LOWER_RUNTIME_MULDC3,
    CHECK_LOWER_RUNTIME_MULXC3,
    CHECK_LOWER_RUNTIME_DIVSC3,
    CHECK_LOWER_RUNTIME_DIVDC3,
    CHECK_LOWER_RUNTIME_DIVXC3,
    CHECK_LOWER_RUNTIME_MEMMOVE,
    CHECK_LOWER_RUNTIME_MEMCPY,
    CHECK_LOWER_RUNTIME_ATOMIC_LOAD,
    CHECK_LOWER_RUNTIME_ATOMIC_STORE,
    CHECK_LOWER_RUNTIME_ATOMIC_EXCHANGE,
    CHECK_LOWER_RUNTIME_ATOMIC_COMPARE_EXCHANGE,
    CHECK_LOWER_RUNTIME_ATOMIC_IS_LOCK_FREE,
    CHECK_LOWER_RUNTIME_FMINF,
    CHECK_LOWER_RUNTIME_FMIN,
    CHECK_LOWER_RUNTIME_FMAXF,
    CHECK_LOWER_RUNTIME_FMAX,
    CHECK_LOWER_RUNTIMES,
} CheckLowerRuntime;

static const char* const check_lower_runtime_names[CHECK_LOWER_RUNTIMES] = {
    "__errno_location", "__divti3",    "__udivti3",   "__modti3",    "__umodti3",  "__floattisf",  "__floattidf",  "__floattixf",  "__floatuntisf",
    "__floatuntidf",    "__floatuntixf", "__fixsfti", "__fixdfti",   "__fixxfti",  "__fixunssfti", "__fixunsdfti", "__fixunsxfti", "__mulsc3",
    "__muldc3",         "__mulxc3",    "__divsc3",    "__divdc3",    "__divxc3", "memmove", "memcpy",
    "__atomic_load", "__atomic_store", "__atomic_exchange", "__atomic_compare_exchange", "__atomic_is_lock_free",
    "fminf", "fmin", "fmaxf", "fmax",
};

// A switch's jump table (#136's item 7, check_lower_switch_tables): its function and its SWITCH control's index in the
// program's controls, by which the lowering finds it (check_lower_table_of), and for each value from its cases' lowest up
// the index of the label it goes to among the tables' labels (its default's, or the switch's end, where no case has it).
// Its labels are `label_count` from `first_label` of the tables', `labels` their control entries: the default's or the
// end's first, then each run of neighboring cases' that go to one place, in value order.
typedef struct CheckLowerTable CheckLowerTable;
struct CheckLowerTable
{
    uint32_t* entries;
    uint32_t count;
    uint32_t function;
    uint32_t control;
    uint32_t* labels;
    uint32_t label_count;
    uint32_t first_label;
    // The dispatch's two GLOBAL_ADDRESS entries in the function, which check_lower_finish gives the table's symbol.
    uint32_t addresses[2];
    // The lowest value, in its class's bits, and the label of the switch's default or end (a control entry's index).
    uint64_t lower;
    uint32_t fallback;
};

// Immutable type facts shared by pool preparation and expression/body lowering.
#define BUSTER_CHECK_LOWER_TYPE_AGGREGATE 0x01u
#define BUSTER_CHECK_LOWER_TYPE_COMPLEX 0x02u
#define BUSTER_CHECK_LOWER_TYPE_ADDRESS 0x04u
#define BUSTER_CHECK_LOWER_TYPE_POINTER 0x08u
#define BUSTER_CHECK_LOWER_TYPE_BOOL 0x10u
#define BUSTER_CHECK_LOWER_TYPE_VOLATILE_COPY 0x20u
#define BUSTER_CHECK_LOWER_TYPE_ATOMIC 0x40u
#define BUSTER_CHECK_LOWER_TYPE_KIND 0x80u

#include "c_check_program_initializers.c"

typedef struct CheckProgramLowered CheckProgramLowered;
struct CheckProgramLowered
{
    Tier0Function* functions;
    // The builtin helpers, then the asm statements' (c_check_program_asm.c), whose plans `asms` has.
    CheckProgramHelpers helpers;
    CheckProgramAsms asms;
    CheckLowerInitializers initializers;
    SymbolId errno_symbol;
    TypeId errno_pointer_type;
    // The runtime's functions the lowering calls (CHECK_LOWER_RUNTIME_*): each one's SymbolId, 0 while nothing calls it,
    // a declaration's of the unit where its program has a symbol of the name with linkage, else one of the lowering's own
    // after the program's and the helpers': `runtime_symbol_count` of those, which `runtime_order` names in their order.
    SymbolId runtime_symbols[CHECK_LOWER_RUNTIMES];
    uint8_t runtime_order[CHECK_LOWER_RUNTIMES];
    // Each function's entries' source tokens (the parser's), when check_program_lower is asked for them: entry e's, the
    // token of the expression, statement or initializer whose lowering wrote it (a function's first entries the
    // declaration's, its last its closing brace's), for source rows (#395). Null otherwise.
    uint32_t** entry_tokens;
    uint32_t runtime_symbol_count;
    uint32_t count;
    CheckProgramStatus status;
    uint32_t token;
    uint32_t backend_node;
    // What a failure was about, for its diagnostic: the feature CHECK_PROGRAM_UNSUPPORTED names, or the rule of
    // tier0_validate's that a lowered function broke (CHECK_PROGRAM_INVALID), or an exhausted capacity's name
    // (CHECK_PROGRAM_RESOURCE). NULL when nothing names it.
    const char* reason;
    uint64_t entries;
    uint64_t output_bytes;
    // Evaluated expression occurrences, and those expanded by the straight-line templates (#624).
    uint64_t expression_nodes;
    uint64_t batch_nodes;
    uint64_t classified_nodes;
    // The symbols of the labels whose addresses the program takes (GNU C's &&label), each function's in the order of its
    // Tier0Function.label_entries, the functions' one after another: the emission defines each at its label's record
    // (Tier0Code.label_records). A jump table's labels are among them, at its dispatch (check_lower_switch_dispatch).
    SymbolId* label_symbols;
    uint32_t label_symbol_count;
    // The switches' jump tables, `table_count` of them in source order, and their labels, `case_symbol_count`. The tables'
    // symbols and their labels' are the lowering's own, local, after the runtime's functions': table t's is
    // case_symbol_base + t, and label k's case_symbol_base + table_count + k (the emission names them .Lbuster_switch_N and
    // .Lbuster_case_N). While the bodies compile, table t's address is the provisional symbol
    // CheckLowerShared.provisional_tables + t, which the access column has.
    CheckLowerTable* tables;
    uint32_t table_count;
    uint32_t case_symbol_count;
    SymbolId case_symbol_base;
};

typedef enum CheckLowerTaskKind
{
    CHECK_LOWER_EXPRESSION,
    CHECK_LOWER_INSTRUCTION,
    CHECK_LOWER_CONVERSION,
    CHECK_LOWER_LABEL,
    CHECK_LOWER_CONTROL,
    CHECK_LOWER_BUILTIN,
    CHECK_LOWER_BUILTIN_MISC,
    CHECK_LOWER_BUILTIN_FLOAT,
    CHECK_LOWER_BUILTIN_FLOAT_STRICT,
    CHECK_LOWER_CONSTANT,
    CHECK_LOWER_ADDRESS,
    CHECK_LOWER_QUERY,
    CHECK_LOWER_TERMINATE,
    // `value` bytes from the address on top to the one under it, which stays (`address`) or not.
    CHECK_LOWER_COPY,
    // An initializer's part of zeros or of bytes (CHECK_INIT_CLEAR or CHECK_INIT_BYTES): `node` its plan, `type` the
    // part, `value` the frame object it writes.
    CHECK_LOWER_FILL,
    // Frame object `value` cleared whole, `type` bytes, before an initializer's parts: what they don't write is zero.
    CHECK_LOWER_CLEAR,
    CHECK_LOWER_INITIALIZER_COPY,
    // A bit-field's value from the address of its first byte (check_lower_bit_read), `type` its packed place.
    CHECK_LOWER_BIT_READ,
    // The value on the stack stored into a bit-field (check_lower_bit_store): `type` its packed place, `value` the local
    // that holds the address of its first byte. The field's new value stays unless `discard`.
    CHECK_LOWER_BIT_STORE,
    // va_start, va_arg, va_copy or va_end's recipe (CBuiltinVaLowerCall `node`), `type` its value's.
    CHECK_LOWER_VARARGS,
    // The size of `type`, a variable-length array's, computed from its extents (an I64: a pointer's runtime stride).
    CHECK_LOWER_SIZE,
    CHECK_LOWER_ATOMIC,
    // Expression `node`'s operation on complex values (check_lower_complex_emit), its operands in the temporaries `value`
    // holds (the first's in its low 32 bits), or with `address` its compound assignment's (check_lower_complex_update_emit),
    // the value's and the destination's address's.
    CHECK_LOWER_COMPLEX,
    // The truth of the complex value of `type` whose address is on the stack, negated with `address`
    // (check_lower_complex_truth).
    CHECK_LOWER_COMPLEX_TRUTH,
} CheckLowerTaskKind;

typedef struct CheckLowerTask CheckLowerTask;
struct CheckLowerTask
{
    CheckLowerTaskKind kind;
    uint32_t node;
    TypeId type;
    uint64_t value;
    bool address;
    bool initialized;
    // An expression whose value nobody reads (a statement's): an aggregate's assignment then leaves no address.
    bool discard;
    // COPY retains volatile accesses through the existing inline pieces, including self-assignment.
    bool volatile_copy;
};

// The pending stack keeps two words instead of a padded 32-byte C record. Instructions, addresses,
// copies and complex operations use the full value word; their type is absent or an instruction class.
// Other tasks have a full TypeId and a 32-bit value (a local identity). The original task is decoded at pop.
typedef struct CheckLowerTaskPacked CheckLowerTaskPacked;
struct CheckLowerTaskPacked
{
    uint64_t control;
    uint64_t payload;
};

_Static_assert(sizeof(CheckLowerTaskPacked) == 16, "a pending lowering task takes two words");
_Static_assert(CHECK_LOWER_COMPLEX_TRUTH <= UINT8_MAX && sizeof(TypeId) == sizeof(uint32_t), "pending task tags and type identities fit their fields");

static inline __attribute__((always_inline)) CheckLowerTaskPacked check_lower_task_pack(CheckLowerTask task)
{
    const bool full = task.kind == CHECK_LOWER_INSTRUCTION || task.kind == CHECK_LOWER_ADDRESS || task.kind == CHECK_LOWER_COPY || task.kind == CHECK_LOWER_COMPLEX;
    return (CheckLowerTaskPacked){
        .control = task.node | (uint64_t)task.kind << 32 | (uint64_t)task.address << 40 | (uint64_t)task.initialized << 41 | (uint64_t)task.discard << 42 |
                   (uint64_t)task.volatile_copy << 43 |
                   (task.kind == CHECK_LOWER_INSTRUCTION ? (uint64_t)task.type << 48 : 0),
        .payload = full ? task.value : task.type | task.value << 32,
    };
}

static inline __attribute__((always_inline)) CheckLowerTask check_lower_task_unpack(CheckLowerTaskPacked packed)
{
    const CheckLowerTaskKind kind = (CheckLowerTaskKind)((packed.control >> 32) & UINT8_MAX);
    const bool full = kind == CHECK_LOWER_INSTRUCTION || kind == CHECK_LOWER_ADDRESS || kind == CHECK_LOWER_COPY || kind == CHECK_LOWER_COMPLEX;
    return (CheckLowerTask){
        .kind = kind,
        .node = (uint32_t)packed.control,
        .type = kind == CHECK_LOWER_INSTRUCTION ? (uint16_t)(packed.control >> 48) : full ? 0 : (uint32_t)packed.payload,
        .value = full ? packed.payload : packed.payload >> 32,
        .address = ((packed.control >> 40) & 1) != 0,
        .initialized = ((packed.control >> 41) & 1) != 0,
        .discard = ((packed.control >> 42) & 1) != 0,
        .volatile_copy = ((packed.control >> 43) & 1) != 0,
    };
}

typedef struct CheckLowerAbi CheckLowerAbi;
typedef struct CheckLowerAtomic CheckLowerAtomic;
struct CheckLowerAtomic
{
    uint32_t node;
    uint32_t address;
    uint32_t desired;
    uint32_t expected;
    uint32_t result;
    uint32_t arguments[6];
    TypeId operand_type;
    TypeId value_type;
    TypeId object_type;
    uint64_t size;
    CBuiltinId id;
    uint8_t operation;
    bool builtin;
    bool native;
    bool postfix;
    bool discard;
};

struct CheckLowerAbi
{
    TypeId* results;
    TypeId* arguments;
    uint32_t* counts;
    uint32_t* named;
    uint32_t* first;
    uint8_t* variadic;
    uint32_t count;
    uint32_t argument_count;
    uint32_t capacity;
    uint32_t argument_capacity;
};

// A jump table's most entries (check_lower_switch_table). clang -O0 builds one of any size (LLVM's limit is UINT_MAX
// entries); a range wider than this one would take more space than its cases' compares.
#define BUSTER_CHECK_LOWER_MAX_TABLE (UINT32_C(1) << 16)
// A jump table's label's symbol in label_symbols while the bodies compile: its index among the tables' labels with the
// flag, which check_lower_finish numbers after the tables' (CheckProgramLowered.case_symbol_base).
#define BUSTER_CHECK_LOWER_CASE_SYMBOL UINT32_C(0x40000000)

// One of a switch's cases while its jump table is decided: its first and last values as keys in unsigned order (a signed
// class's sign bit flipped), and its label (a control entry's index).
typedef struct CheckLowerCaseRange CheckLowerCaseRange;
struct CheckLowerCaseRange
{
    uint64_t lower;
    uint64_t upper;
    uint32_t label;
};

// A classification vector holds at most 16 operator/type words. Payloads belong to reached fragments;
// classification never follows an operand, reads a local map or makes a reachability decision.
#define BUSTER_CHECK_LOWER_BATCH 16u

_Static_assert(BUSTER_CHECK_LOWER_BATCH == 16, "the classification window is one vector of 32-bit node indices");

// At one active integer-builtin depth, each argument position keeps a slot for every value class it has used. Separate
// positions preserve equal-class operands; separate depths preserve outer arguments while a nested call is lowered.
typedef struct CheckLowerBuiltinOperands CheckLowerBuiltinOperands;
struct CheckLowerBuiltinOperands
{
    uint32_t locals[3][BUSTER_TYPED_CLASSES];
};

typedef struct CheckLowerWriter CheckLowerWriter;
struct CheckLowerWriter
{
    const CheckProgram* program;
    CheckProgramLowered* output;
    CheckLowerAbi* abi;
    CheckLowerTaskPacked* tasks;
    uint8_t* kinds;
    uint8_t* classes;
    uint64_t* values;
    // Each entry's source token (CheckProgramLowered.entry_tokens), when `sources` asks for them: the entries before
    // `filled` have theirs, and the ones after it take `source` when it next changes (check_lower_settle).
    uint32_t* tokens;
    // With `sources`, the token each task was pushed under, for the tasks before `settled`; the ones after it were pushed
    // under `source`.
    uint32_t* task_sources;
    uint32_t* labels;
    uint32_t* label_heads;
    uint32_t* stack_parents;
    uint8_t* stack_classes;
    uint32_t* local_map;
    uint32_t* switch_locals;
    // The switch whose CASE controls come next (the last SWITCH control's token), its jump table or BUSTER_CHECK_NONE
    // where its cases' compares dispatch it (check_lower_table_of, by its control), and whether its first CASE control is
    // still to write the table's dispatch (check_lower_switch_dispatch); the tables, `table_count` of them, and the
    // provisional symbols of their addresses.
    uint32_t table_switch;
    uint32_t table_current;
    bool table_pending;
    CheckLowerTable* tables;
    uint32_t table_count;
    SymbolId provisional_tables;
    uint32_t* extent_locals;
    // Each variable-length array's identity plus one, by its bound's first token (0 for none), for its extent's local;
    // each block's local that holds the stack's position its arrays came after (CHECK_CONTROL_STACK_SAVE's), by its
    // closing token; and each local's role as a variable-length array's address (BUSTER_CHECK_LOWER_VLA_*).
    uint32_t* vla_identities;
    uint32_t* save_locals;
    uint8_t* indirect;
    uint32_t* initializer_declarations;
    uint8_t* local_classes;
    uint8_t* local_flags;
    // The class of the value each temporary holds (check_lower_temporary's): its local's class, or a memory class's, whose
    // temporary is a frame object of class VOID.
    uint8_t* value_classes;
    // Tier0Function's optional object columns: a frame object's size (0 for a value's local) and alignment's log2.
    uint32_t* local_sizes;
    uint8_t* local_alignments;
    SymbolId* call_targets;
    uint32_t* call_first;
    uint32_t* call_counts;
    uint8_t* call_variadic;
    uint8_t* argument_classes;
    // Structs and unions by value (#137): each argument's bytes for an aggregate (0 for a value) and each call's result
    // object (1 + its local, or 0), Tier0Function's optional columns, which a function takes when it has one
    // (`aggregates`).
    uint32_t* argument_sizes;
    uint32_t* call_result_objects;
    CBuiltinLowerCall* builtins;
    CheckLowerBuiltinOperands* builtin_operands;
    CBuiltinLowerScratch builtin_scratch;
    uint32_t builtin_depth;
    uint32_t builtin_levels;
    CBuiltinMiscLowerCall* miscellaneous;
    CBuiltinFloatLowerCall* floating;
    CheckLowerAtomic* atomics;
    uint32_t atomic_count;
    uint32_t count;
    uint32_t depth;
    uint32_t capacity;
    uint32_t label_count;
    uint32_t local_count;
    uint32_t local_capacity;
    uint32_t call_count;
    uint32_t argument_count;
    uint32_t builtin_count;
    uint32_t miscellaneous_count;
    uint32_t floating_count;
    uint32_t token;
    // With `sources`, the token whose lowering writes the next entries: an expression's node's, a statement's, a call's
    // callee's or an initializer part's, and for any other task the one it was pushed under, so that an operation's entry,
    // written after its operands', takes its own node's token (check_lower_drain_sources). `filled` and `settled` count
    // the entries and the tasks that have theirs (check_lower_settle).
    uint32_t source;
    uint32_t filled;
    uint32_t settled;
    uint32_t stack_head;
    uint32_t stack_count;
    bool reachable;
    // Whether the function has a frame object, whose columns it then takes.
    bool objects;
    bool aggregates;
    const CheckProgramBody* body;
    TypeId result;
    // Every type's classification, made before the bodies, so that a function's own result (in registers, or through
    // the hidden pointer that is then local `hidden`, parameter 0) decides its returns.
    const AbiTable* abi_table;
    // Every type's class and facts, made before the bodies (CheckLowerShared's), `type_count` of each, by type index.
    const uint8_t* type_classes;
    const uint8_t* type_facts;
    const uint32_t* type_templates;
    uint32_t type_count;
    uint32_t hidden;
    // A variadic definition's (#137): va_start's offsets past the named registers, from the function's own call as
    // abi_assign gives it, and the anchors of its incoming frame (the register save area and the overflow area), made at
    // its first va_start.
    bool variadic;
    uint32_t gp_offset;
    uint32_t fp_offset;
    uint32_t save_anchor;
    uint32_t overflow_anchor;
    CBuiltinVaLowerCall* varargs;
    uint32_t varargs_count;
    // Long double's constants (Tier0Function.x87_constants), two words each: the first is +0.0, which a CONST F80 0 reads
    // (a truth test's, a conversion to bool's, a dead value's), and each other constant has a place of its own. The
    // function takes the table when it has long double values (`x87`, Tier0Function.x87's): an entry of class F80, or its
    // result.
    uint64_t* x87_constants;
    uint32_t x87_constant_count;
    uint32_t x87_constant_capacity;
    bool x87;
    // Whether the function has 128-bit integer values (Tier0Function.int128's): an entry of class I128 or U128; and vector
    // values or X86 entries (Tier0Function.vectors's): an entry of class VECTOR, or an x86 builtin's X86 entry, whose
    // values may all be integers (an opmask's).
    bool int128;
    bool vectors;
    // The function's machine operations (Tier0Function.operations), each X86 entry's row by its index: the vector
    // operators' (check_vector_lower_row), each distinct row once, and the room for them.
    Tier0Operation* operations;
    uint32_t operation_count;
    uint32_t operation_capacity;
    // The function's vector constants (Tier0Function.vector_constants), 64 bytes each and each one's bytes: a shuffle's
    // index vector (check_lower_vector_builtin) or a vector of zeros, each distinct constant once, and the room for them.
    uint64_t* vector_constants;
    uint8_t* vector_constant_bytes;
    uint32_t vector_constant_count;
    uint32_t vector_constant_capacity;
    // The classes check_lower_emit takes, those below this: BUSTER_TYPED_CLASSES, or BUSTER_TYPED_CLASS_VECTOR where the
    // unit's target or the function's has no AVX-512F and AVX-512VL, whose EVEX forms Tier 0's vector entries are
    // (check_lower_vector_target): the unit's decides the convention, as clang's x86-64 ABI does.
    uint32_t class_limit;
    // The function's instruction set (#180), by its target attribute (check_program_targets): which instructions the x86
    // builtins' rows and leaves and the vector builtins' and operators' rows use.
    X86Target target;
    // A function whose result is a 128-bit integer returns it from this frame object's 16 bytes (Tier0Function.result_size,
    // as a struct of two INTEGER eightbytes returns), BUSTER_CHECK_NONE for any other.
    uint32_t result_object;
    bool scalar;
    bool track;
    bool sources;
    uint64_t expression_nodes;
    uint64_t batch_nodes;
    uint64_t classified_nodes;
    // The function's labels whose addresses the program takes (CHECK_CONTROL_LABEL with a symbol): each one's control
    // entry, then its entry (Tier0Function.label_entries), and the room for them, one per control entry.
    uint32_t* label_entries;
    uint32_t label_entry_count;
    uint32_t label_symbol_capacity;
    // The writer's room for the program's label symbols (CheckProgramLowered.label_symbols), which its parts take one after
    // another (check_lower_part): `label_symbol_used` of it kept by the parts before the current one.
    SymbolId* label_symbol_room;
    uint32_t label_symbol_used;
    // The runtime's functions (check_lower_runtime): each one's symbol where the unit declares it, 0 for none
    // (CheckLowerShared.declared_runtimes); the first of the provisional symbols that a call of one of the lowering's own
    // takes; and the ones the writer's part calls first, in the order it does (`runtimes`, `runtime_mask` their bits),
    // which check_lower_finish numbers in source order's order (#151).
    const SymbolId* declared_runtimes;
    SymbolId provisional_runtimes;
    uint64_t runtime_mask;
    uint32_t runtime_count;
    uint8_t runtimes[CHECK_LOWER_RUNTIMES];
};

_Static_assert(CHECK_LOWER_RUNTIMES <= 64, "a part's runtime functions are a 64-bit mask");

// A local's role as a variable-length array's address (CheckLowerWriter.indirect): none; its array's, declared, which
// its object's references read where an array's take LOCAL_ADDRESS; and allocated, once its CHECK_CONTROL_ALLOCATE
// came, which every declared array's must have.
#define BUSTER_CHECK_LOWER_VLA_NONE 0
#define BUSTER_CHECK_LOWER_VLA_DECLARED 1
#define BUSTER_CHECK_LOWER_VLA_ALLOCATED 2

static void check_lower_fail(CheckLowerWriter* writer, CheckProgramStatus status)
{
    if (writer->output->status == CHECK_PROGRAM_OK)
    {
        writer->output->status = status;
        writer->output->token = writer->token;
    }
}

// Keep a hard limit distinct from the writer's allocated storage, and preserve the first failure through rollback.
static void check_lower_resource(CheckLowerWriter* writer, const char* reason)
{
    if (writer->output->status == CHECK_PROGRAM_OK)
    {
        writer->output->reason = reason;
    }
    check_lower_fail(writer, CHECK_PROGRAM_RESOURCE);
}

// A construct the backend doesn't take yet, named for the diagnostic (#145: a program outside the subset fails with
// the feature it needs, never with wrong code).
static void check_lower_unsupported(CheckLowerWriter* writer, const char* feature)
{
    if (writer->output->status == CHECK_PROGRAM_OK)
    {
        writer->output->reason = feature;
    }
    check_lower_fail(writer, CHECK_PROGRAM_UNSUPPORTED);
}

// A vector type's bytes, where Tier 0 takes its values (BUSTER_TYPED_CLASS_VECTOR): GNU C's and Clang's extended vectors
// of 16, 32 or 64 bytes, as the intrinsics' __m128 to __m512i, of elements other than bool (whose extended vectors pack
// a bit per element); 0 for any other type. Atomic wrappers use these value bytes; ABI rows retain their declared
// atomic type, storage size and alignment separately.
static uint32_t check_lower_vector_bytes(const TypeTable* types, TypeId type)
{
    const TypeId base = type_conversion_operand(types, type);
    const uint8_t kind = type_kind(types, base);
    const uint64_t size = kind == BUSTER_TYPE_KIND_VECTOR || kind == BUSTER_TYPE_KIND_EXT_VECTOR ? type_size(types, base) : 0;
    const bool boolean = size != 0 && type_unqualified(type_strip_aligned(types, type_element(types, base))) == BUSTER_TYPE_BOOL;
    return (size == 16 || size == 32 || size == 64) && !boolean ? (uint32_t)size : 0;
}

// The value of a LOAD, STORE or SET of a class's value of a type: a vector's bytes, which Tier 0 takes the width of a
// VECTOR entry that touches memory from (check_lower_vector_bytes); 0 for every other class.
static uint64_t check_lower_access_value(const TypeTable* types, uint8_t class, TypeId type)
{
    return class == BUSTER_TYPED_CLASS_VECTOR ? check_lower_vector_bytes(types, type) : 0;
}

// Whether the target takes Tier 0's vector values: their entries are EVEX forms through zmm16 (AVX-512F), of 16 and 32
// bytes too (AVX-512VL).
static bool check_lower_vector_target(uint64_t features)
{
    const uint64_t required = BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512VL);
    return (features & required) == required;
}

// The widest vector register the convention passes a vector in (abi_initialize's vector_bytes), as clang's x86-64 ABI
// takes it from the target's features (X86AVXABILevel): AVX-512F's 64 bytes, AVX's 32, or else 16.
static uint32_t check_lower_vector_register_bytes(uint64_t features)
{
    return (features & BUSTER_X86_FEATURE(AVX512F)) != 0 ? 64 : (features & BUSTER_X86_FEATURE(AVX)) != 0 ? 32 : 16;
}

// The most distinct machine operations a function may have (CheckLowerWriter.operations), and vector constants
// (CheckLowerWriter.vector_constants).
#define BUSTER_CHECK_LOWER_OPERATIONS 4096u
#define BUSTER_CHECK_LOWER_VECTOR_CONSTANTS 4096u

#include "c_check_program_vectors.c"
#include "c_check_program_x86.c"
#include "c_check_program_vector_builtins.c"

// A type's class, BUSTER_TYPED_CLASS_* or UINT8_MAX for a type no class holds: the rule that the table of every type's
// holds (check_lower_classify_types), which the bodies read (check_lower_class).
static uint8_t check_lower_type_class(const TypeTable* types, TypeId type)
{
    const TypeId base = type_conversion_operand(types, type);
    const uint8_t kind = type_kind(types, base);
    uint8_t result = UINT8_MAX;
    uint32_t bits = 0;
    bool sign = false;
    if (base == BUSTER_TYPE_VOID)
    {
        result = BUSTER_TYPED_CLASS_VOID;
    }
    else if (kind == BUSTER_TYPE_KIND_POINTER || kind == BUSTER_TYPE_KIND_NULLPTR || kind == BUSTER_TYPE_KIND_FUNCTION || type_kind_is_array(kind))
    {
        result = BUSTER_TYPED_CLASS_U64;
    }
    else if (base == BUSTER_TYPE_FLOAT || base == BUSTER_TYPE_DOUBLE)
    {
        result = base == BUSTER_TYPE_FLOAT ? BUSTER_TYPED_CLASS_F32 : BUSTER_TYPED_CLASS_F64;
    }
    else if (base == BUSTER_TYPE_LONG_DOUBLE)
    {
        // Tier 0 keeps a long double's values in memory (check_lower_x87): its locals and parameters are frame objects,
        // read and written through their addresses as its globals are.
        result = BUSTER_TYPED_CLASS_F80;
    }
    else if (check_action_integer_type(types, base, &bits, &sign))
    {
        // An enum's values are its underlying type's. Of the 128-bit integers, __int128 and unsigned __int128 (Tier 0's I128
        // and U128, which also live in memory: check_lower_memory_class); a _BitInt(128) stays refused.
        const TypeId integer = kind == BUSTER_TYPE_KIND_ENUM ? types->types.base[type_index(base)] : base;
        const bool wide = bits == 128 && type_kind(types, integer) == BUSTER_TYPE_KIND_INTEGER;
        result = bits == 1 ? BUSTER_TYPED_CLASS_U8 : bits == 8 ? BUSTER_TYPED_CLASS_I8 : bits == 16 ? BUSTER_TYPED_CLASS_I16 :
                 bits == 32 ? BUSTER_TYPED_CLASS_I32 : bits == 64 ? BUSTER_TYPED_CLASS_I64 : wide ? BUSTER_TYPED_CLASS_I128 : UINT8_MAX;
        if (bits != 1 && result != UINT8_MAX && !sign)
        {
            result += 1;
        }
    }
    else if (check_lower_vector_bytes(types, base) != 0)
    {
        // Tier 0 keeps a vector's values in memory too (check_lower_memory_class), in their depths' 64-byte slots; each
        // entry that makes or moves one has its bytes (check_lower_vector_bytes), as the class has no width.
        result = BUSTER_TYPED_CLASS_VECTOR;
    }
    return result;
}

// Whether a class is a memory class, whose values Tier 0 keeps in their slots: long double's F80 and the 128-bit
// integers' I128 and U128 in 16-byte ones, and vectors (VECTOR) in 64-byte ones. A local or a parameter of one is a frame
// object, which LOCAL, STORE_LOCAL and SET_LOCAL of the class read and write (tier0_memory_object), and so is a temporary
// of one; a global of one is read and written through its address, as Tier 0 has no GLOBAL, STORE_GLOBAL or SET_GLOBAL of
// them.
static bool check_lower_memory_class(uint8_t class)
{
    return class == BUSTER_TYPED_CLASS_F80 || class == BUSTER_TYPED_CLASS_I128 || class == BUSTER_TYPED_CLASS_U128 || class == BUSTER_TYPED_CLASS_VECTOR;
}

// Whether a class is a 128-bit integer's, which the convention passes and returns as two INTEGER eightbytes: Tier 0
// takes such an argument, parameter and result as a 16-byte object, as a struct of them. Its division and remainder, and
// its conversions to and from the floating types, are libgcc's calls (CHECK_LOWER_RUNTIME_*), as in clang's code.
static bool check_lower_wide_class(uint8_t class)
{
    return class == BUSTER_TYPED_CLASS_I128 || class == BUSTER_TYPED_CLASS_U128;
}

// A complex type's element, unqualified, for one of a floating element (_Complex float, double and long double), or 0:
// GNU's complex integer types aren't lowered.
static TypeId check_lower_complex_element(const TypeTable* types, TypeId type)
{
    const TypeId base = type_conversion_operand(types, type);
    const TypeId element = type_kind(types, base) == BUSTER_TYPE_KIND_COMPLEX ? type_unqualified(type_strip_aligned(types, type_element(types, base))) : 0;
    return element == BUSTER_TYPE_FLOAT || element == BUSTER_TYPE_DOUBLE || element == BUSTER_TYPE_LONG_DOUBLE ? element : 0;
}

// A type's facts for the lowering (BUSTER_CHECK_LOWER_TYPE_*), from its aligned typedef's type (type_strip_aligned):
// whether it is a struct, a union or a complex type the lowering takes, an aggregate value, which the stream carries as
// its object's address (check_lower_aggregate); the complex one (check_lower_complex); whether its value is its address,
// which nothing loads through: a function's, an array's or an aggregate's; whether it is a pointer; and whether it is
// _Bool. BUSTER_CHECK_LOWER_TYPE_KIND marks the kinds whose types take their class and facts from the kind alone
// (check_lower_kind_facts), in that table alone.
// A type's facts (BUSTER_CHECK_LOWER_TYPE_*): the rule that the table of every type's holds (check_lower_classify_types),
// which the bodies read (check_lower_facts).
static uint8_t check_lower_type_facts(const TypeTable* types, TypeId type)
{
    const bool atomic = type_kind(types, type_strip_aligned(types, type)) == BUSTER_TYPE_KIND_ATOMIC;
    const TypeId base = type_conversion_operand(types, type);
    const uint8_t kind = type_kind(types, base);
    const bool complex = check_lower_complex_element(types, type) != 0;
    const bool aggregate = kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION || complex;
    const bool address = kind == BUSTER_TYPE_KIND_FUNCTION || type_kind_is_array(kind) || aggregate;
    return (aggregate ? BUSTER_CHECK_LOWER_TYPE_AGGREGATE : 0) | (complex ? BUSTER_CHECK_LOWER_TYPE_COMPLEX : 0) | (address ? BUSTER_CHECK_LOWER_TYPE_ADDRESS : 0) |
           (kind == BUSTER_TYPE_KIND_POINTER ? BUSTER_CHECK_LOWER_TYPE_POINTER : 0) | (base == BUSTER_TYPE_BOOL ? BUSTER_CHECK_LOWER_TYPE_BOOL : 0) |
           (atomic ? BUSTER_CHECK_LOWER_TYPE_ATOMIC : 0);
}

// The kinds whose types, but the builtins, take their class and facts from the kind alone, as the rules give them: each
// one's facts with BUSTER_CHECK_LOWER_TYPE_KIND, by kind, and its class. A table of 64 bytes each, so that 64 types'
// kinds look them up at once (check_lower_classify_types). The types of the other kinds (an aligned typedef's, an enum's,
// a _BitInt's, a complex type's, and a vector's, whose class its bytes and element decide) take the rules, as the
// builtins do.
static const uint8_t check_lower_kind_facts[64] __attribute__((aligned(64))) = {
    [BUSTER_TYPE_KIND_NULLPTR] = BUSTER_CHECK_LOWER_TYPE_KIND,
    [BUSTER_TYPE_KIND_POINTER] = BUSTER_CHECK_LOWER_TYPE_KIND | BUSTER_CHECK_LOWER_TYPE_POINTER,
    [BUSTER_TYPE_KIND_ARRAY] = BUSTER_CHECK_LOWER_TYPE_KIND | BUSTER_CHECK_LOWER_TYPE_ADDRESS,
    [BUSTER_TYPE_KIND_INCOMPLETE_ARRAY] = BUSTER_CHECK_LOWER_TYPE_KIND | BUSTER_CHECK_LOWER_TYPE_ADDRESS,
    [BUSTER_TYPE_KIND_VARIABLE_ARRAY] = BUSTER_CHECK_LOWER_TYPE_KIND | BUSTER_CHECK_LOWER_TYPE_ADDRESS,
    [BUSTER_TYPE_KIND_FUNCTION] = BUSTER_CHECK_LOWER_TYPE_KIND | BUSTER_CHECK_LOWER_TYPE_ADDRESS,
    [BUSTER_TYPE_KIND_STRUCT] = BUSTER_CHECK_LOWER_TYPE_KIND | BUSTER_CHECK_LOWER_TYPE_AGGREGATE | BUSTER_CHECK_LOWER_TYPE_ADDRESS,
    [BUSTER_TYPE_KIND_UNION] = BUSTER_CHECK_LOWER_TYPE_KIND | BUSTER_CHECK_LOWER_TYPE_AGGREGATE | BUSTER_CHECK_LOWER_TYPE_ADDRESS,
};

static const uint8_t check_lower_kind_classes[64] __attribute__((aligned(64))) = {
    [BUSTER_TYPE_KIND_NULLPTR] = BUSTER_TYPED_CLASS_U64,
    [BUSTER_TYPE_KIND_POINTER] = BUSTER_TYPED_CLASS_U64,
    [BUSTER_TYPE_KIND_ARRAY] = BUSTER_TYPED_CLASS_U64,
    [BUSTER_TYPE_KIND_INCOMPLETE_ARRAY] = BUSTER_TYPED_CLASS_U64,
    [BUSTER_TYPE_KIND_VARIABLE_ARRAY] = BUSTER_TYPED_CLASS_U64,
    [BUSTER_TYPE_KIND_FUNCTION] = BUSTER_TYPED_CLASS_U64,
    [BUSTER_TYPE_KIND_STRUCT] = UINT8_MAX,
    [BUSTER_TYPE_KIND_UNION] = UINT8_MAX,
};

// Every type's class and facts by the rules, `count` of each from index 0: check_lower_classify_types' twin.
static void check_lower_classify_types_scalar(const TypeTable* types, uint32_t count, uint8_t* classes, uint8_t* facts)
{
    for (uint32_t index = 0; index < count; index += 1)
    {
        const TypeId type = (TypeId)index << BUSTER_TYPE_INDEX_SHIFT;
        classes[index] = check_lower_type_class(types, type);
        facts[index] = check_lower_type_facts(types, type);
    }
}

// Every type's class and facts, `count` of each from index 0, as check_lower_classify_types_scalar makes them, before the
// bodies, which ask them of each node's types (check_lower_class): the builtins by the rules, then 64 types at a time,
// each one's by its kind from the kinds' tables, and by the rules the few whose kind the tables don't answer for. Reads
// the types' kinds alone (the last 64 masked to the `count` types), and writes `count` bytes of each column.
static void check_lower_classify_types(const TypeTable* types, uint32_t count, uint8_t* classes, uint8_t* facts)
{
#if !BUSTER_HOST_VECTOR
    check_lower_classify_types_scalar(types, count, classes, facts);
#else
    const uint32_t builtins = count < BUSTER_TYPE_BUILTIN_COUNT ? count : BUSTER_TYPE_BUILTIN_COUNT;
    check_lower_classify_types_scalar(types, builtins, classes, facts);
    const __m512i kind_facts = _mm512_load_si512(check_lower_kind_facts);
    const __m512i kind_classes = _mm512_load_si512(check_lower_kind_classes);
    const __m512i kind_bit = _mm512_set1_epi8((char)BUSTER_CHECK_LOWER_TYPE_KIND);
    for (uint32_t first = builtins; first < count; first += 64)
    {
        const __mmask64 active = count - first < 64 ? (UINT64_C(1) << (count - first)) - 1 : UINT64_MAX;
        const __m512i kinds = _mm512_maskz_loadu_epi8(active, types->types.kind + first);
        const __m512i kind_facts_of = _mm512_permutexvar_epi8(kinds, kind_facts);
        // A kind past the tables' 64 entries would read another's: its low 6 bits are all vpermb takes.
        const __mmask64 known = _mm512_mask_test_epi8_mask(_mm512_cmplt_epu8_mask(kinds, _mm512_set1_epi8(64)), kind_facts_of, kind_bit);
        _mm512_mask_storeu_epi8(classes + first, active, _mm512_permutexvar_epi8(kinds, kind_classes));
        _mm512_mask_storeu_epi8(facts + first, active, _mm512_andnot_si512(kind_bit, kind_facts_of));
        for (uint64_t rule = active & ~known; rule != 0; rule &= rule - 1)
        {
            const uint32_t index = first + (uint32_t)__builtin_ctzll(rule);
            const TypeId type = (TypeId)index << BUSTER_TYPE_INDEX_SHIFT;
            classes[index] = check_lower_type_class(types, type);
            facts[index] = check_lower_type_facts(types, type);
        }
    }
#endif
}

// A record's volatile subobjects are computed once before bodies share this immutable column. Pointer pointees are
// not contained storage. The explicit memoized DFS visits each type and by-value edge once, with no depth cap.
// A bounded member scan avoids the graph and its scratch when the unit has no volatile member.
static bool check_lower_copy_type_facts(const TypeTable* types, uint32_t count, uint8_t* facts, Arena* scratch, bool scalar)
{
    bool needed = false;
    const uint32_t members = types->members.count;
    if (scalar)
    {
        for (uint32_t m = 1; !needed && m < members; m += 1)
        {
            needed = (type_qualifiers(types->members.type[m]) & BUSTER_TYPE_VOLATILE) != 0;
        }
    }
    else
    {
        for (uint32_t m = 1; !needed && m < members; m += 16)
        {
            const uint32_t left = members - m;
            const __mmask16 active = left < 16 ? (__mmask16)((1u << left) - 1) : UINT16_MAX;
            const __m512i values = _mm512_maskz_loadu_epi32(active, types->members.type + m);
            needed = _mm512_test_epi32_mask(values, _mm512_set1_epi32(BUSTER_TYPE_VOLATILE)) != 0;
        }
    }
    const uint64_t mark = scratch->length;
    uint8_t* state = needed ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, count) : NULL;
    uint32_t* nodes = needed ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, count) : NULL;
    uint32_t* next = needed ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, count) : NULL;
    bool valid = !needed || (state != NULL && nodes != NULL && next != NULL);
    if (needed && valid)
    {
        memset(state, 0, count);
        for (uint32_t root = 0; valid && root < count; root += 1)
        {
            uint32_t depth = 0;
            if (state[root] == 0)
            {
                nodes[depth] = root;
                next[depth++] = 0;
                state[root] = 1;
            }
            while (valid && depth != 0)
            {
                const uint32_t index = nodes[depth - 1];
                const uint8_t kind = types->types.kind[index];
                const bool record = kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION;
                const bool base = type_kind_is_array(kind) || kind == BUSTER_TYPE_KIND_ALIGNED || kind == BUSTER_TYPE_KIND_ATOMIC;
                const uint32_t tag = record ? types->types.index[index] : 0;
                const uint32_t children = record ? types->tags.member_count[tag] : base ? 1 : 0;
                const uint32_t child_at = next[depth - 1];
                if (child_at == children || (facts[index] & BUSTER_CHECK_LOWER_TYPE_VOLATILE_COPY) != 0)
                {
                    state[index] = 2;
                    depth -= 1;
                }
                else
                {
                    const TypeId child = record ? types->members.type[types->tags.member_begin[tag] + child_at] : types->types.base[index];
                    const uint32_t child_index = type_index(child);
                    if ((type_qualifiers(child) & BUSTER_TYPE_VOLATILE) != 0)
                    {
                        facts[index] |= BUSTER_CHECK_LOWER_TYPE_VOLATILE_COPY;
                    }
                    else if (child_index >= count || state[child_index] == 1)
                    {
                        valid = false;
                    }
                    else if (state[child_index] == 2)
                    {
                        facts[index] |= facts[child_index] & BUSTER_CHECK_LOWER_TYPE_VOLATILE_COPY;
                        next[depth - 1] += 1;
                    }
                    else
                    {
                        nodes[depth] = child_index;
                        next[depth++] = 0;
                        state[child_index] = 1;
                    }
                }
            }
        }
    }
    scratch->length = mark;
    return valid;
}

// A type's class (check_lower_type_class's) from the writer's table of every type's (check_lower_classify_types). The
// lowering makes no type, so every type it reads has its place there; one past them, which no valid program has, has no
// class.
static uint8_t check_lower_class(const CheckLowerWriter* writer, TypeId type)
{
    const uint32_t index = type_index(type);
    return index < writer->type_count ? writer->type_classes[index] : UINT8_MAX;
}

// A type's facts (check_lower_type_facts', BUSTER_CHECK_LOWER_TYPE_*) from the writer's table, as check_lower_class.
static uint8_t check_lower_facts(const CheckLowerWriter* writer, TypeId type)
{
    const uint32_t index = type_index(type);
    return index < writer->type_count ? writer->type_facts[index] : 0;
}

// Whether a type is a complex one the lowering takes (check_lower_complex_element): two of its element's values, the
// real part then the imaginary one, which the stream carries as an aggregate's, by its object's address, so that
// copies, calls, returns and va_arg take it as a struct of the two (the convention classifies it, abi_classify). Its
// operations are its parts' (check_lower_complex_compute).
static bool check_lower_complex(const CheckLowerWriter* writer, TypeId type)
{
    return (check_lower_facts(writer, type) & BUSTER_CHECK_LOWER_TYPE_COMPLEX) != 0;
}

// Whether a type is a struct, a union or a complex one (check_lower_complex): an aggregate value, which the stream
// carries as its object's address. Each consumer copies the bytes from there (an assignment, an initializer) or reads a
// member or a part through it.
static bool check_lower_aggregate(const CheckLowerWriter* writer, TypeId type)
{
    return (check_lower_facts(writer, type) & BUSTER_CHECK_LOWER_TYPE_AGGREGATE) != 0;
}

// Whether a type is long double (BUSTER_TYPED_CLASS_F80): a memory class's, and the x87's.
static bool check_lower_x87(const CheckLowerWriter* writer, TypeId type)
{
    return check_lower_class(writer, type) == BUSTER_TYPED_CLASS_F80;
}

// Whether a type's values are of a memory class (check_lower_memory_class).
static bool check_lower_memory(const CheckLowerWriter* writer, TypeId type)
{
    return check_lower_memory_class(check_lower_class(writer, type));
}

// Whether a type is a 128-bit integer (check_lower_wide_class).
static bool check_lower_wide(const CheckLowerWriter* writer, TypeId type)
{
    return check_lower_wide_class(check_lower_class(writer, type));
}

// An expression's class on the stack: check_lower_class's, or U64 for an aggregate (its address).
static uint8_t check_lower_value_class(const CheckLowerWriter* writer, TypeId type)
{
    return check_lower_aggregate(writer, type) ? BUSTER_TYPED_CLASS_U64 : check_lower_class(writer, type);
}

// What a value of a type check_lower_class has no class for needs, named for the diagnostic. An aggregate's value is
// its address, so the type of one that reaches here is passed or returned by value.
static const char* check_lower_type_feature(const TypeTable* types, TypeId type)
{
    const TypeId base = type_unqualified(type_strip_aligned(types, type));
    const uint8_t kind = type_kind(types, base);
    const char* feature = "values of this type";
    if (base == BUSTER_TYPE_FLOAT16 || base == BUSTER_TYPE_FLOAT128 || kind == BUSTER_TYPE_KIND_FLOATING)
    {
        feature = "floating types other than float, double and long double";
    }
    else if (kind == BUSTER_TYPE_KIND_COMPLEX)
    {
        // One check_lower_complex doesn't take: GNU's complex integer types, or a complex type of another floating
        // type (_Complex _Float128, _Complex _Float16).
        const TypeId element = type_unqualified(type_strip_aligned(types, type_element(types, base)));
        feature = type_kind(types, element) == BUSTER_TYPE_KIND_INTEGER ? "complex integer types" : "complex types other than _Complex float, double and long double";
    }
    else if (kind == BUSTER_TYPE_KIND_BIT_INT)
    {
        feature = "_BitInt";
    }
    else if (kind == BUSTER_TYPE_KIND_VECTOR || kind == BUSTER_TYPE_KIND_EXT_VECTOR)
    {
        feature = type_unqualified(type_strip_aligned(types, type_element(types, base))) == BUSTER_TYPE_BOOL ? "vectors of bool" :
                  "vector types of other than 16, 32 or 64 bytes";
    }
    else if (kind == BUSTER_TYPE_KIND_ATOMIC)
    {
        feature = "_Atomic";
    }
    else if (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION)
    {
        feature = "structs and unions passed or returned by value";
    }
    else if (kind == BUSTER_TYPE_KIND_VARIABLE_ARRAY)
    {
        feature = "variable-length arrays";
    }
    return feature;
}

// Whether a type is a vector of other than bool that check_lower_class has no class for (check_lower_type_feature's
// "vector types of other than 16, 32 or 64 bytes").
static bool check_lower_classless_vector(const CheckLowerWriter* writer, TypeId type)
{
    const TypeTable* types = writer->program->types;
    const TypeId base = type_unqualified(type_strip_aligned(types, type));
    const uint8_t kind = type_kind(types, base);
    return (kind == BUSTER_TYPE_KIND_VECTOR || kind == BUSTER_TYPE_KIND_EXT_VECTOR) && check_lower_class(writer, type) == UINT8_MAX &&
           type_unqualified(type_strip_aligned(types, type_element(types, base))) != BUSTER_TYPE_BOOL;
}

// Why a struct or union can't be passed or returned by value, or NULL. Tier 0 takes every location abi_assign gives one
// as an argument or a parameter, of up to BUSTER_TIER0_MAX_ARGUMENT_BYTES aligned to 1 GB at most (past 16 bytes, its
// calls' outgoing areas and its objects realign their function's frame), and every result in registers that
// tier0_aggregate_result_valid takes: eightbytes, or one or two long doubles in st0 and st1.
static const char* check_lower_by_value(const CheckLowerWriter* writer, TypeId type, bool result)
{
    const TypeTable* types = writer->program->types;
    const AbiReturn returned = writer->abi_table->results[type_index(type)];
    const bool x87 = ((returned.registers[0] | returned.registers[1]) & BUSTER_ABI_REGISTER_X87) != 0;
    const char* feature = NULL;
    if (type_alignment_log2(types, type) > BUSTER_TIER0_MAX_OBJECT_ALIGNMENT_LOG2)
    {
        feature = "structs and unions aligned past 1 GB, by value";
    }
    else if (type_size(types, type) > BUSTER_TIER0_MAX_ARGUMENT_BYTES)
    {
        feature = "structs and unions larger than 64 KB, by value";
    }
    else if (result && returned.kind == BUSTER_ABI_RESULT_REGISTERS && !tier0_aggregate_result_valid(returned, (uint32_t)type_size(types, type)))
    {
        feature = x87 ? "long double" : "structs and unions returned in these registers";
    }
    return feature;
}

// Why an argument or a parameter of this type can't be passed, or NULL: a value of a class, or a struct or a union.
static const char* check_lower_passed(const CheckLowerWriter* writer, TypeId type)
{
    const TypeTable* types = writer->program->types;
    const char* feature = NULL;
    if (check_lower_aggregate(writer, type))
    {
        feature = check_lower_by_value(writer, type, false);
    }
    else if (check_lower_class(writer, type) == UINT8_MAX)
    {
        feature = check_lower_type_feature(types, type);
    }
    return feature;
}

// A pointer's stride: its pointee's size, or 0 for a variable-length array's, which its extents give at run time
// (check_lower_scale and CHECK_LOWER_SIZE).
static uint64_t check_lower_stride(CheckLowerWriter* writer, TypeId pointer)
{
    const TypeTable* types = writer->program->types;
    const CheckAction action = { .extensions = writer->program->input->graph.extensions };
    const uint64_t size = check_action_pointer_stride(types, pointer, &action);
    if (size == 0 && !check_action_runtime_size(types, type_element(types, type_strip_aligned(types, pointer))))
    {
        check_lower_unsupported(writer, "pointer arithmetic over types of no size");
    }
    return size;
}

// Persistent stack heads let a forward branch retain its complete typed stack without copying it at every
// label. This is bookkeeping for spilling live outer operands around statement regions; Tier0 still validates
// the finished stream independently, including both sides of every join.
static void check_lower_track(CheckLowerWriter* writer, uint8_t kind, uint8_t class, uint64_t value)
{
    if (!writer->reachable)
    {
        writer->stack_head = 0;
        writer->reachable = true;
    }
    uint32_t pop = 0;
    bool push = false;
    if (kind <= BUSTER_TYPED_GLOBAL_ADDRESS || kind == BUSTER_TYPED_DUP || kind == BUSTER_TYPED_STACK_SAVE || kind == BUSTER_TYPED_FRAME_ADDRESS)
    {
        push = true;
    }
    else if (kind == BUSTER_TYPED_NEG || kind == BUSTER_TYPED_NOT || kind == BUSTER_TYPED_CONVERT || kind == BUSTER_TYPED_LOAD || kind == BUSTER_TYPED_ALLOCA)
    {
        pop = 1;
        push = true;
    }
    else if (kind == BUSTER_TYPED_STORE || kind == BUSTER_TYPED_SET)
    {
        pop = 2;
        push = kind == BUSTER_TYPED_STORE;
    }
    else if (kind >= BUSTER_TYPED_STORE_LOCAL && kind <= BUSTER_TYPED_SET_GLOBAL)
    {
        pop = 1;
        push = kind == BUSTER_TYPED_STORE_LOCAL || kind == BUSTER_TYPED_STORE_GLOBAL;
    }
    else if (kind == BUSTER_TYPED_X86)
    {
        pop = value < writer->operation_count ? writer->operations[value].operand_count : 0;
        push = class != BUSTER_TYPED_CLASS_VOID;
    }
    else if (kind == BUSTER_TYPED_CALL)
    {
        // An indirect call (callee 0) pops the address it calls too, which the stream pushed before the arguments.
        pop = value < writer->call_count ? writer->call_counts[value] + (writer->call_targets[value] == 0) : 0;
        push = class != BUSTER_TYPED_CLASS_VOID;
    }
    else if (kind == BUSTER_TYPED_DISCARD || kind == BUSTER_TYPED_BRANCH_FALSE || kind == BUSTER_TYPED_BRANCH_TRUE || kind == BUSTER_TYPED_STACK_RESTORE ||
             kind == BUSTER_TYPED_JUMP_INDIRECT)
    {
        pop = 1;
    }
    else if (kind == BUSTER_TYPED_COPY)
    {
        pop = 2;
    }
    else if (kind == BUSTER_TYPED_RETURN)
    {
        pop = class != BUSTER_TYPED_CLASS_VOID;
    }
    else if (kind != BUSTER_TYPED_JUMP && kind != BUSTER_TYPED_JUMP_VALUE)
    {
        pop = 2;
        push = true;
    }
    for (uint32_t i = 0; i < pop; i += 1)
    {
        if (writer->stack_head == 0)
        {
            check_lower_fail(writer, CHECK_PROGRAM_INVALID);
        }
        else
        {
            writer->stack_head = writer->stack_parents[writer->stack_head];
        }
    }
    if (push && writer->stack_count + 1 < writer->capacity)
    {
        const uint32_t entry = ++writer->stack_count;
        writer->stack_parents[entry] = writer->stack_head;
        writer->stack_classes[entry] = class;
        writer->stack_head = entry;
    }
    else if (push)
    {
        check_lower_fail(writer, CHECK_PROGRAM_RESOURCE);
    }
    if (kind >= BUSTER_TYPED_BRANCH_FALSE && kind <= BUSTER_TYPED_JUMP_VALUE)
    {
        if (value < writer->label_count && writer->label_heads[value] == BUSTER_CHECK_NONE)
        {
            writer->label_heads[value] = writer->stack_head;
        }
        writer->reachable = kind == BUSTER_TYPED_BRANCH_FALSE || kind == BUSTER_TYPED_BRANCH_TRUE;
    }
    else if (kind == BUSTER_TYPED_RETURN || kind == BUSTER_TYPED_JUMP_INDIRECT)
    {
        if (writer->stack_head != 0)
        {
            check_lower_fail(writer, CHECK_PROGRAM_INVALID);
        }
        writer->reachable = false;
    }
}

static void check_lower_place(CheckLowerWriter* writer, uint32_t label)
{
    writer->labels[label] = writer->count;
    if (writer->track)
    {
        if (!writer->reachable)
        {
            writer->stack_head = writer->label_heads[label] != BUSTER_CHECK_NONE ? writer->label_heads[label] : 0;
        }
        if (writer->label_heads[label] == BUSTER_CHECK_NONE)
        {
            writer->label_heads[label] = writer->stack_head;
        }
        writer->reachable = true;
    }
}

// Before the token changes (CheckLowerWriter.source), the entries written and the tasks pushed since it last changed
// take it, so that no entry or push spends anything on the tokens.
static inline __attribute__((always_inline)) void check_lower_settle(CheckLowerWriter* writer)
{
    for (uint32_t e = writer->filled; e < writer->count; e += 1)
    {
        writer->tokens[e] = writer->source;
    }
    writer->filled = writer->count;
    for (uint32_t t = writer->settled; t < writer->depth; t += 1)
    {
        writer->task_sources[t] = writer->source;
    }
    writer->settled = writer->depth;
}

// The token whose lowering writes the next entries (CheckLowerWriter.source). Out of line, as only -g reaches it.
static __attribute__((noinline)) void check_lower_change_source(CheckLowerWriter* writer, uint32_t token)
{
    check_lower_settle(writer);
    writer->source = token;
}

// The token whose lowering writes the next entries, where a call or an initializer part sets it in the middle of a task,
// and at a function's start and end: a lowering without the tokens (`sources`) only tests the flag.
static void check_lower_source(CheckLowerWriter* writer, uint32_t token)
{
    if (writer->sources)
    {
        check_lower_change_source(writer, token);
    }
}

// check_lower_emit's entry in a function that tracks its stack (CheckLowerWriter.track: one with a statement expression,
// or a program with a terminal helper), out of line, as few functions do.
static __attribute__((noinline)) void check_lower_emit_tracked(CheckLowerWriter* writer, uint8_t kind, uint8_t class, uint64_t value)
{
    const bool operand = (kind >= BUSTER_TYPED_NEG && kind <= BUSTER_TYPED_SET_GLOBAL) || (kind >= BUSTER_TYPED_DISCARD && kind <= BUSTER_TYPED_BRANCH_TRUE) ||
                         kind == BUSTER_TYPED_JUMP_VALUE || (kind == BUSTER_TYPED_RETURN && class != BUSTER_TYPED_CLASS_VOID) || kind == BUSTER_TYPED_ALLOCA ||
                         kind == BUSTER_TYPED_STACK_RESTORE;
    const uint8_t top = writer->stack_head != 0 ? writer->stack_classes[writer->stack_head] : BUSTER_TYPED_CLASS_VOID;
    if (writer->reachable && operand && top != BUSTER_TYPED_CLASS_VOID && writer->count != 0 && writer->classes[writer->count - 1] != top)
    {
        // Tier0's operand-class column is the preceding entry's class. A void call or a spill of a different
        // stack class can interrupt that adjacency. This cancelling pair restores the contract without
        // reading or changing the pending value, and works for narrow, floating and pointer classes alike.
        if (writer->count + 2 >= writer->capacity || writer->count + 2 >= BUSTER_TIER0_MAX_ENTRIES)
        {
            check_lower_resource(writer, writer->count + 2 >= BUSTER_TIER0_MAX_ENTRIES ? "Tier 0 per-function entry limit" : "lowering entry storage");
        }
        else
        {
            writer->kinds[writer->count] = BUSTER_TYPED_CONST;
            writer->classes[writer->count] = top;
            writer->values[writer->count++] = 0;
            writer->kinds[writer->count] = BUSTER_TYPED_DISCARD;
            writer->classes[writer->count] = top;
            writer->values[writer->count++] = 0;
        }
    }
    if (writer->output->status == CHECK_PROGRAM_OK)
    {
        check_lower_track(writer, kind, class, value);
        writer->kinds[writer->count] = kind;
        writer->classes[writer->count] = class;
        writer->values[writer->count] = value;
        writer->count += 1;
    }
}

// A type's vector bytes (check_lower_vector_bytes), from its class first: a type of the writer's table is a vector's
// exactly where its class is VECTOR (check_lower_type_class), so that the calls, returns and temporaries of code
// without vectors pay a load and a compare for them (#136).
static uint32_t check_lower_vector_of(const CheckLowerWriter* writer, TypeId type)
{
    const uint32_t index = type_index(type);
    const bool vector = index < writer->type_count ? writer->type_classes[index] == BUSTER_TYPED_CLASS_VECTOR : true;
    return vector ? check_lower_vector_bytes(writer->program->types, type) : 0;
}

// Appends an entry. Its function's memory classes (Tier0Function.x87 and int128) come from its entries' classes once it
// is lowered (check_lower_entry_classes).
static void check_lower_emit(CheckLowerWriter* writer, uint8_t kind, uint8_t class, uint64_t value)
{
    const uint32_t count = writer->count;
    if (class >= writer->class_limit)
    {
        check_lower_unsupported(writer, class != BUSTER_TYPED_CLASS_VECTOR ? "values of this type" :
                                        check_lower_vector_target(writer->program->target.features) ? "vector types in a function whose target attribute turns off AVX-512F or AVX-512VL" :
                                        "vector types without AVX-512F and AVX-512VL");
    }
    else if (count >= writer->capacity || count >= BUSTER_TIER0_MAX_ENTRIES)
    {
        check_lower_resource(writer, count >= BUSTER_TIER0_MAX_ENTRIES ? "Tier 0 per-function entry limit" : "lowering entry storage");
    }
    else if (writer->track)
    {
        check_lower_emit_tracked(writer, kind, class, value);
    }
    else if (writer->output->status == CHECK_PROGRAM_OK)
    {
        writer->kinds[count] = kind;
        writer->classes[count] = class;
        writer->values[count] = value;
        writer->count = count + 1;
    }
}

// Which memory classes a function's `count` entries' classes have (Tier0Function.x87, int128 and vectors):
// BUSTER_CHECK_LOWER_ENTRIES_X87 where one is F80, BUSTER_CHECK_LOWER_ENTRIES_WIDE where one is I128 or U128, and
// BUSTER_CHECK_LOWER_ENTRIES_VECTOR where one is VECTOR. 64 classes at a time (the last 64 masked to the count);
// check_lower_entry_classes_scalar is its twin.
#define BUSTER_CHECK_LOWER_ENTRIES_X87 1u
#define BUSTER_CHECK_LOWER_ENTRIES_WIDE 2u
#define BUSTER_CHECK_LOWER_ENTRIES_VECTOR 4u

_Static_assert(BUSTER_TYPED_CLASS_U128 == (BUSTER_TYPED_CLASS_I128 | 1), "the 128-bit integers' classes differ in their low bit alone");

static uint32_t check_lower_entry_classes_scalar(const uint8_t* classes, uint32_t count);

static uint32_t check_lower_entry_classes(const uint8_t* classes, uint32_t count)
{
#if !BUSTER_HOST_VECTOR
    return check_lower_entry_classes_scalar(classes, count);
#else
    uint64_t x87 = 0;
    uint64_t wide = 0;
    uint64_t vector = 0;
    for (uint32_t first = 0; first < count; first += 64)
    {
        const __mmask64 active = count - first < 64 ? (UINT64_C(1) << (count - first)) - 1 : UINT64_MAX;
        // The lanes past the count read as 0, VOID's class.
        const __m512i some = _mm512_maskz_loadu_epi8(active, classes + first);
        x87 |= _mm512_cmpeq_epi8_mask(some, _mm512_set1_epi8(BUSTER_TYPED_CLASS_F80));
        wide |= _mm512_cmpeq_epi8_mask(_mm512_and_si512(some, _mm512_set1_epi8((char)~1)), _mm512_set1_epi8(BUSTER_TYPED_CLASS_I128));
        vector |= _mm512_cmpeq_epi8_mask(some, _mm512_set1_epi8(BUSTER_TYPED_CLASS_VECTOR));
    }
    return (x87 != 0 ? BUSTER_CHECK_LOWER_ENTRIES_X87 : 0) | (wide != 0 ? BUSTER_CHECK_LOWER_ENTRIES_WIDE : 0) |
           (vector != 0 ? BUSTER_CHECK_LOWER_ENTRIES_VECTOR : 0);
#endif
}

static uint32_t check_lower_entry_classes_scalar(const uint8_t* classes, uint32_t count)
{
    uint32_t result = 0;
    for (uint32_t i = 0; i < count; i += 1)
    {
        result |= classes[i] == BUSTER_TYPED_CLASS_F80 ? BUSTER_CHECK_LOWER_ENTRIES_X87 : 0;
        result |= check_lower_wide_class(classes[i]) ? BUSTER_CHECK_LOWER_ENTRIES_WIDE : 0;
        result |= classes[i] == BUSTER_TYPED_CLASS_VECTOR ? BUSTER_CHECK_LOWER_ENTRIES_VECTOR : 0;
    }
    return result;
}

static void check_lower_push(CheckLowerWriter* writer, CheckLowerTask task)
{
    if (writer->depth < writer->capacity)
    {
        writer->tasks[writer->depth++] = check_lower_task_pack(task);
    }
    else
    {
        check_lower_fail(writer, CHECK_PROGRAM_RESOURCE);
    }
}

static void check_lower_instruction(CheckLowerWriter* writer, uint8_t kind, uint8_t class, uint64_t value)
{
    check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_INSTRUCTION, .node = kind, .type = class, .value = value });
}

// Whether a conversion from `from` to `to` (check_lower_conversion) writes no entry and can't fail: neither type is
// complex, both have one class on the stack, a value's or both an aggregate's address, and `to` isn't _Bool, whose
// conversion normalizes even a _Bool's value. An array's decay to a pointer, a pointer's to another and a change of
// qualifiers are such conversions, which need no task.
static inline __attribute__((always_inline)) bool check_lower_conversion_empty(const CheckLowerWriter* writer, TypeId from, TypeId to)
{
    uint32_t from_info = 0;
    uint32_t to_info = 0;
    if (writer->type_templates != NULL)
    {
        const uint32_t a = type_index(from);
        const uint32_t b = type_index(to);
        from_info = a < writer->type_count ? writer->type_templates[a] : UINT8_MAX;
        to_info = b < writer->type_count ? writer->type_templates[b] : UINT8_MAX;
    }
    else
    {
        from_info = check_lower_class(writer, from) | (uint32_t)check_lower_facts(writer, from) << 8;
        to_info = check_lower_class(writer, to) | (uint32_t)check_lower_facts(writer, to) << 8;
    }
    const uint8_t from_facts = (uint8_t)(from_info >> 8);
    const uint8_t to_facts = (uint8_t)(to_info >> 8);
    const uint8_t source = (from_facts & BUSTER_CHECK_LOWER_TYPE_AGGREGATE) != 0 ? BUSTER_TYPED_CLASS_U64 : (uint8_t)from_info;
    const uint8_t target = (to_facts & BUSTER_CHECK_LOWER_TYPE_AGGREGATE) != 0 ? BUSTER_TYPED_CLASS_U64 : (uint8_t)to_info;
    return source == target && source < BUSTER_TYPED_CLASSES && ((from_facts ^ to_facts) & BUSTER_CHECK_LOWER_TYPE_AGGREGATE) == 0 &&
           ((from_facts | to_facts) & BUSTER_CHECK_LOWER_TYPE_COMPLEX) == 0 && (to_facts & BUSTER_CHECK_LOWER_TYPE_BOOL) == 0;
}

static void check_lower_convert(CheckLowerWriter* writer, TypeId from, TypeId to)
{
    if (to != 0 && from != to && !check_lower_conversion_empty(writer, from, to))
    {
        check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_CONVERSION, .node = from, .type = to });
    }
}

static void check_lower_expression(CheckLowerWriter* writer, uint32_t node, bool address, TypeId target)
{
    const CheckProgram* program = writer->program;
    if (node < program->stream->count && program->stream->values[node].overload_head == 0)
    {
        // A statement's assignment of an aggregate copies the bytes and leaves no address to discard, and one into a
        // bit-field (an update's too) leaves no value. A discarded comma discards its right operand in its place.
        const CheckOp op = program->input->graph.ops[node];
        const bool store = op == CHECK_OP_ASSIGN || op == CHECK_OP_INITIALIZE;
        const uint32_t destination = program->input->graph.operand0[node];
        const bool bit_field = (op == CHECK_OP_ASSIGN || (op >= CHECK_OP_PRE_INCREMENT && op <= CHECK_OP_BIT_XOR_ASSIGN)) &&
                               destination < program->stream->count && (program->stream->values[destination].flags & BUSTER_CHECK_BIT_FIELD) != 0;
        const bool discard = !address && target == BUSTER_TYPE_VOID &&
                             ((store && check_lower_aggregate(writer, program->stream->values[node].type)) || bit_field || op == CHECK_OP_COMMA);
        if (!address && !discard)
        {
            check_lower_convert(writer, program->stream->values[node].type, target);
        }
        check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_EXPRESSION, .node = node, .address = address, .discard = discard });
    }
    else
    {
        check_lower_fail(writer, CHECK_PROGRAM_INVALID);
    }
}

// Pending overload leaves carry a lexical placeholder. Only the checked consuming edge supplies an address.
static void check_lower_operand(CheckLowerWriter* writer, uint32_t node, uint32_t operand, bool address, TypeId target)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckOverloadResult* selected = check_program_overload_address(program, node, operand);
    if (selected != NULL)
    {
        if (!check_program_overload_valid(program, selected))
        {
            check_lower_fail(writer, CHECK_PROGRAM_INVALID);
        }
        else if ((selected->flags & BUSTER_CHECK_OVERLOADABLE) != 0)
        {
            check_lower_unsupported(writer, "addresses of overloadable functions");
        }
        else
        {
            if (!address)
            {
                check_lower_convert(writer, selected->type, target);
            }
            check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_ADDRESS, .node = selected->object });
        }
    }
    else
    {
        const uint32_t child = operand == 0 ? graph->operand0[node] : operand == 1 ? graph->operand1[node] : operand == 2 ? graph->operand2[node] :
                               operand - 3 < graph->argument_counts[node] ? graph->argument_nodes[graph->argument_offsets[node] + operand - 3] : BUSTER_CHECK_NONE;
        check_lower_expression(writer, child, address, target);
    }
}

static uint32_t check_lower_label(CheckLowerWriter* writer)
{
    const uint32_t label = writer->label_count;
    if (label < writer->capacity)
    {
        writer->labels[label] = BUSTER_CHECK_NONE;
        if (writer->track)
        {
            writer->label_heads[label] = BUSTER_CHECK_NONE;
        }
        writer->label_count += 1;
    }
    else
    {
        check_lower_fail(writer, CHECK_PROGRAM_RESOURCE);
    }
    return label;
}

static void check_lower_mark(CheckLowerWriter* writer, uint32_t label)
{
    check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_LABEL, .node = label });
}

static uint32_t check_lower_object(CheckLowerWriter* writer, uint64_t size, uint32_t alignment_log2);

// A local of a value's class: for a memory class (check_lower_memory_class), a frame object of a slot's 16 bytes, which
// LOCAL, STORE_LOCAL and SET_LOCAL of the class read and write. Its class is the temporary's value class.
static uint32_t check_lower_temporary(CheckLowerWriter* writer, uint8_t class)
{
    uint32_t local = writer->local_count;
    if (class == BUSTER_TYPED_CLASS_VECTOR)
    {
        // A vector's object has its bytes (check_lower_temporary_of), which a class doesn't give.
        check_lower_unsupported(writer, "vector values here");
    }
    else if (check_lower_memory_class(class))
    {
        local = check_lower_object(writer, 16, 4);
    }
    else if (local < writer->local_capacity && local < BUSTER_TIER0_MAX_LOCALS && class < BUSTER_TYPED_CLASSES && class != BUSTER_TYPED_CLASS_VOID)
    {
        writer->local_classes[local] = class;
        writer->local_flags[local] = 0;
        writer->local_sizes[local] = 0;
        writer->local_alignments[local] = 0;
        writer->indirect[local] = BUSTER_CHECK_LOWER_VLA_NONE;
        writer->local_count += 1;
    }
    else if (class >= BUSTER_TYPED_CLASSES)
    {
        check_lower_unsupported(writer, "values of this type");
    }
    else
    {
        check_lower_resource(writer, local >= BUSTER_TIER0_MAX_LOCALS ? "Tier 0 per-function local limit" : "lowering local storage");
    }
    if (writer->output->status == CHECK_PROGRAM_OK)
    {
        writer->value_classes[local] = class;
    }
    return local;
}

// A temporary of a type's values: check_lower_temporary's of its value class, but a vector's, a frame object of its bytes,
// which LOCAL, STORE_LOCAL and SET_LOCAL of VECTOR take its width from (check_lower_vector_bytes).
static uint32_t check_lower_temporary_of(CheckLowerWriter* writer, TypeId type)
{
    const uint32_t bytes = check_lower_vector_of(writer, type);
    uint32_t local = BUSTER_CHECK_NONE;
    if (bytes != 0)
    {
        local = check_lower_object(writer, bytes, 4);
        if (writer->output->status == CHECK_PROGRAM_OK)
        {
            writer->value_classes[local] = BUSTER_TYPED_CLASS_VECTOR;
        }
    }
    else
    {
        local = check_lower_temporary(writer, check_lower_value_class(writer, type));
    }
    return local;
}

// A row's index among the function's machine operations (CheckLowerWriter.operations): an equal row's, or a new one's.
// The rows are few (an operator's at a type), and check_vector_lower_row clears each one's padding.
static uint32_t check_lower_operation_index(CheckLowerWriter* writer, const Tier0Operation* row)
{
    uint32_t index = 0;
    while (index < writer->operation_count && memcmp(&writer->operations[index], row, sizeof(*row)) != 0)
    {
        index += 1;
    }
    if (index == writer->operation_count && index < writer->operation_capacity)
    {
        writer->operations[index] = *row;
        writer->operation_count += 1;
    }
    else if (index == writer->operation_count)
    {
        check_lower_fail(writer, CHECK_PROGRAM_RESOURCE);
    }
    return index;
}

// An X86 entry of an operator's row at a vector type (check_vector_lower_row), or its refusal: its operands are the
// stack's when it runs. Half-precision elements have clang's excess precision where the unit's target hasn't AVX512-FP16
// (check_lower_vector_half_chain).
static void check_lower_vector_row(CheckLowerWriter* writer, uint32_t op, TypeId vector, uint8_t scalar_class)
{
    Tier0Operation row;
    const bool excess = (writer->program->target.extensions & BUSTER_X86_EXTENSION(AVX512FP16)) == 0;
    const char* reason = check_vector_lower_row(writer->program->types, &writer->target, excess, op, vector, scalar_class, &row);
    if (reason != NULL)
    {
        check_lower_unsupported(writer, reason);
    }
    else
    {
        const uint32_t index = check_lower_operation_index(writer, &row);
        check_lower_instruction(writer, BUSTER_TYPED_X86, BUSTER_TYPED_CLASS_VECTOR, index);
    }
}

// clang's _Float16 excess precision (#136): where the unit's target hasn't AVX512-FP16, clang computes +, -, * and / of
// half-precision elements and their negation in float, and rounds to them only where a value leaves the chain of those
// operators (a store, a cast, a compare, a call's argument, a conditional's or a comma's operand), even in a function
// whose target attribute adds the extension. One operator's row in half precision rounds as clang's does (float's 24 bits
// are at least twice half's 11 and 2 more, so its two roundings round as once, NaNs propagating alike), and a negation's
// quiets the signaling NaNs as clang's float conversions do (check_vector_lower_row's `excess`). A chain's doesn't: an
// operator (`op`, one of those) whose operand `a` or `b` (or BUSTER_CHECK_NONE) is of half-precision elements and itself
// one of them, or a unary plus, is refused.
static void check_lower_vector_half_chain(CheckLowerWriter* writer, TypeId vector, uint32_t op, uint32_t a, uint32_t b)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckVectorElement element = check_vector_lower_element(program->types, vector);
    const bool excess = element.floating && element.bytes == 2 && (program->target.extensions & BUSTER_X86_EXTENSION(AVX512FP16)) == 0;
    const bool arithmetic = op == CHECK_OP_ADD || op == CHECK_OP_SUBTRACT || op == CHECK_OP_MULTIPLY || op == CHECK_OP_DIVIDE || op == CHECK_OP_NEGATIVE;
    bool chained = false;
    for (uint32_t i = 0; excess && arithmetic && i < 2; i += 1)
    {
        const uint32_t operand = i == 0 ? a : b;
        const CheckOp kind = operand != BUSTER_CHECK_NONE ? graph->ops[operand] : CHECK_OP_VALUE;
        const bool link = kind == CHECK_OP_ADD || kind == CHECK_OP_SUBTRACT || kind == CHECK_OP_MULTIPLY || kind == CHECK_OP_DIVIDE || kind == CHECK_OP_NEGATIVE ||
                          kind == CHECK_OP_POSITIVE;
        const CheckVectorElement linked = link ? check_vector_lower_element(program->types, program->stream->values[operand].type) : (CheckVectorElement){ 0 };
        chained |= linked.floating && linked.bytes == 2;
    }
    if (chained)
    {
        check_lower_unsupported(writer, "chained arithmetic of half-precision vectors, which clang computes in float without AVX512-FP16");
    }
}

// A frame object (an array, a struct or a union, or a value that must live in memory): a local of class VOID, reached
// through LOCAL_ADDRESS, but a value's, which LOCAL, STORE_LOCAL and SET_LOCAL of its class reach too
// (tier0_value_object): a memory class's, and one aligned past 8 bytes. One of no size (a GNU empty struct, a
// zero-length array) still takes a byte of its own. Aligned past 16 bytes, it realigns its function's frame.
static uint32_t check_lower_object(CheckLowerWriter* writer, uint64_t size, uint32_t alignment_log2)
{
    const uint32_t local = writer->local_count;
    if (size > BUSTER_TIER0_MAX_OBJECT_BYTES || alignment_log2 > BUSTER_TIER0_MAX_OBJECT_ALIGNMENT_LOG2)
    {
        check_lower_unsupported(writer, "local objects aligned past 1 GB or larger than 1 GB");
    }
    else if (local < writer->local_capacity && local < BUSTER_TIER0_MAX_LOCALS)
    {
        writer->local_classes[local] = BUSTER_TYPED_CLASS_VOID;
        writer->local_flags[local] = BUSTER_TYPED_LOCAL_ADDRESS_TAKEN;
        writer->local_sizes[local] = size != 0 ? (uint32_t)size : 1;
        writer->local_alignments[local] = (uint8_t)alignment_log2;
        writer->value_classes[local] = BUSTER_TYPED_CLASS_VOID;
        writer->indirect[local] = BUSTER_CHECK_LOWER_VLA_NONE;
        writer->local_count += 1;
        writer->objects = true;
    }
    else
    {
        check_lower_resource(writer, local >= BUSTER_TIER0_MAX_LOCALS ? "Tier 0 per-function local limit" : "lowering local storage");
    }
    return local;
}

static uint32_t check_lower_addressed_temporary(CheckLowerWriter* writer, uint8_t class)
{
    const uint32_t local = check_lower_temporary(writer, class);
    if (writer->output->status == CHECK_PROGRAM_OK)
    {
        writer->local_flags[local] |= BUSTER_TYPED_LOCAL_ADDRESS_TAKEN;
    }
    return local;
}

static void check_lower_address(CheckLowerWriter* writer, uint32_t object, int64_t offset)
{
    if (object == 0)
    {
        check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, (uint64_t)offset);
    }
    else if (object >= writer->program->object_count)
    {
        check_lower_fail(writer, CHECK_PROGRAM_INVALID);
    }
    else
    {
        const uint32_t local = writer->local_map[object];
        const SymbolId symbol = writer->program->object_symbols[object];
        if (local != BUSTER_CHECK_NONE && writer->indirect[local] != BUSTER_CHECK_LOWER_VLA_NONE)
        {
            // A variable-length array's address, which its local holds.
            check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, local);
        }
        else if (local != BUSTER_CHECK_NONE)
        {
            writer->local_flags[local] |= BUSTER_TYPED_LOCAL_ADDRESS_TAKEN;
            check_lower_emit(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, local);
        }
        else if (symbol != 0)
        {
            check_lower_emit(writer, BUSTER_TYPED_GLOBAL_ADDRESS, BUSTER_TYPED_CLASS_U64, symbol);
        }
        else
        {
            check_lower_fail(writer, CHECK_PROGRAM_INVALID);
        }
        if (offset != 0)
        {
            check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, (uint64_t)offset);
            check_lower_emit(writer, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, 1);
        }
    }
}

// The address on top of the stack moved `offset` bytes on.
static void check_lower_offset(CheckLowerWriter* writer, uint64_t offset)
{
    if (offset != 0)
    {
        check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, offset);
        check_lower_emit(writer, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, 1);
    }
}

// The size in bytes of `type`, an array with variable-length dimensions, onto the stack as `class` (U64, or I64 for a
// stride): the extents' locals, which their bounds set where they were declared (VLA_BOUND), multiplied, then by the
// product of the constant dimensions and the element's size. A variable-length dimension's type holds its bound's first
// token plus one, by which its array (and its extent's local) is found.
static void check_lower_vla_size(CheckLowerWriter* writer, TypeId type, uint8_t class)
{
    const CheckProgram* program = writer->program;
    const TypeTable* types = program->types;
    uint64_t factor = 1;
    uint32_t extents = 0;
    bool valid = true;
    TypeId at = type_strip_aligned(types, type);
    for (uint32_t depth = 0; valid && type_kind_is_array(type_kind(types, at)) && depth < types->types.count; depth += 1)
    {
        const uint64_t extent = types->types.extent[type_index(at)];
        if (type_kind(types, at) == BUSTER_TYPE_KIND_VARIABLE_ARRAY)
        {
            const uint32_t identity = extent != 0 && extent <= program->input->token_count ? writer->vla_identities[extent - 1] : 0;
            const uint32_t local = identity != 0 ? writer->extent_locals[identity - 1] : BUSTER_CHECK_NONE;
            valid = local != BUSTER_CHECK_NONE;
            if (valid)
            {
                check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, local);
                if (extents != 0)
                {
                    check_lower_emit(writer, BUSTER_TYPED_MUL, BUSTER_TYPED_CLASS_U64, 0);
                }
                extents += 1;
            }
        }
        else
        {
            factor *= extent;
        }
        at = type_strip_aligned(types, type_element(types, at));
    }
    factor *= type_size(types, at);
    if (valid && extents != 0)
    {
        if (factor != 1)
        {
            check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, factor);
            check_lower_emit(writer, BUSTER_TYPED_MUL, BUSTER_TYPED_CLASS_U64, 0);
        }
        if (class != BUSTER_TYPED_CLASS_U64)
        {
            check_lower_emit(writer, BUSTER_TYPED_CONVERT, class, 0);
        }
    }
    else
    {
        // A dimension whose bound this function hasn't evaluated: the checker's scopes make that impossible.
        check_lower_fail(writer, CHECK_PROGRAM_INVALID);
    }
}

// The index on top of the stack scaled by `pointer`'s stride (`size`, check_lower_stride's), as instructions pushed
// after the index's: a PTR_ADD by the stride, or for a pointer to a variable-length array, by 1 after the index's
// product with its size, computed from its extents (CHECK_LOWER_SIZE).
static void check_lower_scale(CheckLowerWriter* writer, TypeId pointer, uint64_t size)
{
    const TypeTable* types = writer->program->types;
    check_lower_instruction(writer, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, size != 0 ? size : 1);
    if (size == 0)
    {
        check_lower_instruction(writer, BUSTER_TYPED_MUL, BUSTER_TYPED_CLASS_I64, 0);
        check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_SIZE, .type = type_element(types, type_strip_aligned(types, pointer)) });
    }
}

static uint32_t check_lower_runtime_call(CheckLowerWriter* writer, CheckLowerRuntime runtime, TypeId result, const TypeId* arguments, uint32_t count, uint32_t object);

static bool check_lower_copy_volatile(const CheckLowerWriter* writer, const CheckValue* source, TypeId destination)
{
    return ((type_qualifiers(source->type) | type_qualifiers(destination)) & BUSTER_TYPE_VOLATILE) != 0 ||
           (source->flags & BUSTER_CHECK_VOLATILE_PART) != 0 ||
           ((check_lower_facts(writer, source->type) | check_lower_facts(writer, destination)) & BUSTER_CHECK_LOWER_TYPE_VOLATILE_COPY) != 0;
}

// Aggregate comma, conditional and statement values forward an address after lvalue conversion erased its qualifier.
// Follow only those value edges, not earlier side effects or an assignment whose volatile accesses already ran. The
// unused tail of this writer's task array is a bounded private worklist; no pending task or source-token state changes.
static bool check_lower_copy_forward_volatile(CheckLowerWriter* writer, uint32_t node)
{
    const CheckProgram* program = writer->program;
    const CheckStream* stream = program->stream;
    const CheckGraph* graph = &program->input->graph;
    uint32_t pending = 0;
    uint32_t budget = stream->count;
    uint32_t at = node;
    bool volatile_copy = false;
    while (!volatile_copy && at < stream->count && budget != 0 && writer->output->status == CHECK_PROGRAM_OK)
    {
        budget -= 1;
        volatile_copy = check_lower_copy_volatile(writer, &stream->values[at], 0);
        uint32_t next = BUSTER_CHECK_NONE;
        if (!volatile_copy)
        {
            const CheckOp op = graph->ops[at];
            if (op == CHECK_OP_SELECT || op == CHECK_OP_STATEMENT_VALUE || op == CHECK_OP_CONVERT)
            {
                next = graph->operand0[at];
            }
            else if (op == CHECK_OP_COMMA)
            {
                next = graph->operand1[at];
            }
            else if (op == CHECK_OP_CONDITIONAL)
            {
                bool truth = false;
                if (check_program_runtime_truth(program, at, graph->operand0[at], &truth))
                {
                    next = truth ? graph->operand1[at] : graph->operand2[at];
                }
                else if (pending < writer->capacity - writer->depth)
                {
                    writer->tasks[writer->depth + pending++].control = graph->operand2[at];
                    next = graph->operand1[at];
                }
                else
                {
                    check_lower_fail(writer, CHECK_PROGRAM_RESOURCE);
                }
            }
        }
        at = next;
        if (at == BUSTER_CHECK_NONE && pending != 0)
        {
            at = (uint32_t)writer->tasks[writer->depth + --pending].control;
        }
    }
    if (!volatile_copy && at < stream->count && writer->output->status == CHECK_PROGRAM_OK)
    {
        check_lower_fail(writer, CHECK_PROGRAM_RESOURCE);
    }
    return volatile_copy;
}

static bool check_lower_copy_node_volatile(CheckLowerWriter* writer, uint32_t node, TypeId destination)
{
    const CheckValue* source = &writer->program->stream->values[node];
    bool volatile_copy = check_lower_copy_volatile(writer, source, destination);
    if (!volatile_copy && (source->flags & BUSTER_CHECK_SIDE_EFFECT) != 0)
    {
        const CheckOp op = writer->program->input->graph.ops[node];
        if (op == CHECK_OP_SELECT || op == CHECK_OP_STATEMENT_VALUE || op == CHECK_OP_CONVERT || op == CHECK_OP_COMMA || op == CHECK_OP_CONDITIONAL)
        {
            volatile_copy = check_lower_copy_forward_volatile(writer, node);
        }
    }
    return volatile_copy;
}

// `size` bytes copied from the source's address on top of the stack to the destination's under it, which stays when
// `keep` (an assignment's value). Small copies use Tier 0's bounded COPY; larger nonvolatile ones call memmove through the ordinary
// runtime-symbol and ABI path, preserving live values and returning the destination without a temporary. memmove permits
// the same source and destination, which C aggregate assignment does too. The nonvolatile stream never grows with size.
// Volatile copies retain the previous inline pieces: a library may skip accesses when the ranges are the same.
// A kept small destination goes in a temporary: its consumer's class is read from the entry before it (tier0_validate),
// which a COPY, of class VOID, can't give.
// Always inlined into both copies of the tasks' loop (check_lower_drain_body).
static inline __attribute__((always_inline)) void check_lower_copy(CheckLowerWriter* writer, uint64_t size, bool keep, bool volatile_copy)
{
    if (size == 0)
    {
        check_lower_emit(writer, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_U64, 0);
        if (!keep)
        {
            check_lower_emit(writer, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_U64, 0);
        }
    }
    else if (!keep && size <= BUSTER_TIER0_MAX_COPY_BYTES)
    {
        check_lower_emit(writer, BUSTER_TYPED_COPY, BUSTER_TYPED_CLASS_VOID, size);
    }
    else if (size > BUSTER_TIER0_MAX_COPY_BYTES && !volatile_copy)
    {
        // Pointers and size_t use the same U64 INTEGER ABI as unsigned long on the generated SysV x86-64 target.
        // Reuse that builtin type so lowering creates no type after the shared ABI classifications are published.
        const TypeId arguments[3] = { BUSTER_TYPE_ULONG, BUSTER_TYPE_ULONG, BUSTER_TYPE_ULONG };
        const uint32_t call = check_lower_runtime_call(writer, CHECK_LOWER_RUNTIME_MEMMOVE, BUSTER_TYPE_ULONG, arguments, 3, BUSTER_CHECK_NONE);
        check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, size);
        check_lower_emit(writer, BUSTER_TYPED_CALL, BUSTER_TYPED_CLASS_U64, call);
        if (!keep)
        {
            check_lower_emit(writer, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_U64, 0);
        }
    }
    else
    {
        const uint32_t source = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
        const uint32_t destination = keep ? check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64) : BUSTER_CHECK_NONE;
        check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, source);
        if (keep)
        {
            check_lower_emit(writer, BUSTER_TYPED_STORE_LOCAL, BUSTER_TYPED_CLASS_U64, destination);
        }
        for (uint64_t offset = 0; offset < size && writer->output->status == CHECK_PROGRAM_OK; offset += BUSTER_TIER0_MAX_COPY_BYTES)
        {
            const uint64_t piece = size - offset < BUSTER_TIER0_MAX_COPY_BYTES ? size - offset : BUSTER_TIER0_MAX_COPY_BYTES;
            if (offset + piece != size)
            {
                check_lower_emit(writer, BUSTER_TYPED_DUP, BUSTER_TYPED_CLASS_U64, 0);
            }
            check_lower_offset(writer, offset);
            check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, source);
            check_lower_offset(writer, offset);
            check_lower_emit(writer, BUSTER_TYPED_COPY, BUSTER_TYPED_CLASS_VOID, piece);
        }
        if (keep)
        {
            check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, destination);
        }
    }
}

// Zeros past this many bytes are written by a loop of 8-byte stores rather than store by store.
#define BUSTER_CHECK_LOWER_FILL_UNROLLED 128

// The largest access of 8, 4, 2 or 1 bytes that `left` bytes take, and its class: runs of bytes are written (and a
// bit-field's bytes read) as such pieces, largest first.
static uint32_t check_lower_piece(uint64_t left)
{
    return left >= 8 ? 8 : left >= 4 ? 4 : left >= 2 ? 2 : 1;
}

static uint8_t check_lower_piece_class(uint32_t piece)
{
    return piece == 8 ? BUSTER_TYPED_CLASS_U64 : piece == 4 ? BUSTER_TYPED_CLASS_U32 : piece == 2 ? BUSTER_TYPED_CLASS_U16 : BUSTER_TYPED_CLASS_U8;
}

// An initializer's zeros (CHECK_INIT_CLEAR) or bytes (CHECK_INIT_BYTES) written into frame object `object`: stores of 8
// bytes, then 4, 2 and 1 for the rest, the bytes as constants of the stores' classes (the host's byte order is the
// target's); past BUSTER_CHECK_LOWER_FILL_UNROLLED bytes, the zeros' words by a loop.
static void check_lower_initializer_copy(CheckLowerWriter* writer, uint32_t pool, uint32_t object, uint64_t offset);

static void check_lower_fill(CheckLowerWriter* writer, const CheckInitPart* part, uint32_t object, uint32_t pool)
{
    const uint64_t begin = part->bit_offset / 8;
    const uint64_t size = part->byte_count;
    if (part->bit_offset % 8 != 0 || part->bit_width != 0 || (part->kind == CHECK_INIT_BYTES && size != 0 && part->bytes == NULL))
    {
        check_lower_unsupported(writer, "bit-field initializers in local objects");
    }
    else if (pool != BUSTER_CHECK_NONE)
    {
        check_lower_initializer_copy(writer, pool, object, begin);
    }
    else
    {
        uint64_t offset = 0;
        if (part->kind == CHECK_INIT_CLEAR && size > BUSTER_CHECK_LOWER_FILL_UNROLLED)
        {
            const uint32_t index = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
            const uint32_t top = check_lower_label(writer);
            const uint32_t end = check_lower_label(writer);
            check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 0);
            check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, index);
            check_lower_place(writer, top);
            check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, index);
            check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, size / 8);
            check_lower_emit(writer, BUSTER_TYPED_LT, BUSTER_TYPED_CLASS_I32, 0);
            check_lower_emit(writer, BUSTER_TYPED_BRANCH_FALSE, BUSTER_TYPED_CLASS_I32, end);
            check_lower_emit(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, object);
            check_lower_offset(writer, begin);
            check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, index);
            check_lower_emit(writer, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, 8);
            check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 0);
            check_lower_emit(writer, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_U64, 0);
            check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, index);
            check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 1);
            check_lower_emit(writer, BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0);
            check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, index);
            check_lower_emit(writer, BUSTER_TYPED_JUMP, BUSTER_TYPED_CLASS_VOID, top);
            check_lower_place(writer, end);
            offset = size / 8 * 8;
        }
        while (offset < size && writer->output->status == CHECK_PROGRAM_OK)
        {
            const uint32_t piece = check_lower_piece(size - offset);
            const uint8_t class = check_lower_piece_class(piece);
            uint64_t bits = 0;
            if (part->kind == CHECK_INIT_BYTES)
            {
                memcpy(&bits, part->bytes + offset, piece);
            }
            check_lower_emit(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, object);
            check_lower_offset(writer, begin + offset);
            check_lower_emit(writer, BUSTER_TYPED_CONST, class, bits);
            check_lower_emit(writer, BUSTER_TYPED_SET, class, 0);
            offset += piece;
        }
    }
}

// A bit-field's place: `width` bits, `shift` bits up from the start of the byte its first bit is in, `first` bytes
// into its record. It is read and written through that byte and the ones after it that hold its bits, never another
// member's (C11 makes those other memory locations): 1 to 8 bytes, accessed as pieces (check_lower_piece) that make
// up one little-endian unit. Its value is in `class`, sign-extended from its width when its type is signed (plain int
// bit-fields are, as in clang and GCC; an enum's are as its underlying type is).
typedef struct CheckLowerBitField CheckLowerBitField;
struct CheckLowerBitField
{
    uint64_t first;
    uint32_t shift;
    uint32_t width;
    uint8_t class;
    bool is_signed;
};

// The place of a bit-field of `width` bits and type `type` at bit `offset`; false for one that has no integer class or
// whose bits are spread over more than 8 bytes (a packed 64-bit one off a byte boundary).
static bool check_lower_bit_place(const TypeTable* types, uint64_t offset, uint32_t width, TypeId type, CheckLowerBitField* field)
{
    uint32_t bits = 0;
    bool sign = false;
    const bool integer = check_action_integer_type(types, type, &bits, &sign);
    field->first = offset / 8;
    field->shift = (uint32_t)(offset % 8);
    field->width = width;
    field->class = check_lower_type_class(types, type);
    field->is_signed = sign && bits != 1;
    return integer && width != 0 && width <= bits && field->shift + width <= 64 && field->class < BUSTER_TYPED_CLASSES &&
           field->class != BUSTER_TYPED_CLASS_VOID;
}

// The place of the bit-field that MEMBER or ARROW node `node` designates: at the offset the checker found it at.
static bool check_lower_bit_member(const CheckProgram* program, uint32_t node, CheckLowerBitField* field)
{
    const CheckStream* stream = program->stream;
    return stream->diagnostics[node] == CHECK_DIAG_NONE &&
           check_lower_bit_place(program->types, stream->member_offsets[node], stream->values[node].bit_width, stream->values[node].type, field);
}

// The MEMBER or ARROW node of the bit-field that a store's destination `node` designates, through _Generic's
// selections, or BUSTER_CHECK_NONE.
static uint32_t check_lower_bit_destination(const CheckProgram* program, uint32_t node)
{
    const CheckGraph* graph = &program->input->graph;
    uint32_t destination = node;
    while (destination < program->stream->count && graph->ops[destination] == CHECK_OP_SELECT)
    {
        destination = graph->operand0[destination];
    }
    const bool member = destination < program->stream->count && (graph->ops[destination] == CHECK_OP_MEMBER || graph->ops[destination] == CHECK_OP_ARROW);
    return member ? destination : BUSTER_CHECK_NONE;
}

// A place in a task's `type`: the shift in bits 0 to 2, the width less one in bits 3 to 8, the class in bits 9 to 12
// and the sign in bit 13. Its first byte is in the address the task finds.
static uint32_t check_lower_bit_pack(const CheckLowerBitField* field)
{
    return field->shift | (field->width - 1) << 3 | (uint32_t)field->class << 9 | (uint32_t)field->is_signed << 13;
}

static CheckLowerBitField check_lower_bit_unpack(uint32_t packed)
{
    return (CheckLowerBitField){
        .shift = packed & 7,
        .width = (packed >> 3 & 63) + 1,
        .class = (uint8_t)(packed >> 9 & 15),
        .is_signed = (packed >> 13 & 1) != 0,
    };
}

// A bit-field's unit from the address of its first byte on top of the stack: each of its `bytes` bytes' pieces loaded,
// widened and put in place, as a U64. More than one piece reads the address from a temporary.
static void check_lower_bit_unit(CheckLowerWriter* writer, uint32_t bytes)
{
    const bool whole = (bytes & (bytes - 1)) == 0;
    const uint32_t address = whole ? BUSTER_CHECK_NONE : check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
    if (!whole)
    {
        check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, address);
    }
    uint32_t offset = 0;
    while (offset < bytes && writer->output->status == CHECK_PROGRAM_OK)
    {
        const uint32_t piece = check_lower_piece(bytes - offset);
        const uint8_t class = check_lower_piece_class(piece);
        if (!whole)
        {
            check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, address);
            check_lower_offset(writer, offset);
        }
        check_lower_emit(writer, BUSTER_TYPED_LOAD, class, 0);
        if (class != BUSTER_TYPED_CLASS_U64)
        {
            check_lower_emit(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
        }
        if (offset != 0)
        {
            check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 8 * offset);
            check_lower_emit(writer, BUSTER_TYPED_SHL, BUSTER_TYPED_CLASS_U64, 0);
            check_lower_emit(writer, BUSTER_TYPED_OR, BUSTER_TYPED_CLASS_U64, 0);
        }
        offset += piece;
    }
}

// A bit-field's value from the U64 on top of the stack that holds its bits `shift` bits up: the bits moved to the top
// and back down, which sign- or zero-extends them, then converted to the field's class.
static void check_lower_bit_extract(CheckLowerWriter* writer, const CheckLowerBitField* field, uint32_t shift)
{
    const uint8_t wide = field->is_signed ? BUSTER_TYPED_CLASS_I64 : BUSTER_TYPED_CLASS_U64;
    const uint32_t up = 64 - shift - field->width;
    if (up != 0)
    {
        check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, up);
        check_lower_emit(writer, BUSTER_TYPED_SHL, BUSTER_TYPED_CLASS_U64, 0);
    }
    if (field->is_signed)
    {
        check_lower_emit(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_I64, 0);
    }
    if (field->width != 64)
    {
        check_lower_emit(writer, BUSTER_TYPED_CONST, wide, 64 - field->width);
        check_lower_emit(writer, BUSTER_TYPED_SHR, wide, 0);
    }
    if (field->class != wide)
    {
        check_lower_emit(writer, BUSTER_TYPED_CONVERT, field->class, 0);
    }
}

static void check_lower_bit_read(CheckLowerWriter* writer, uint32_t packed)
{
    const CheckLowerBitField field = check_lower_bit_unpack(packed);
    check_lower_bit_unit(writer, (field.shift + field.width + 7) / 8);
    check_lower_bit_extract(writer, &field, field.shift);
}

// The value on top of the stack, in the field's class, stored into the bit-field whose first byte's address local
// `address` holds: its low `width` bits kept in a temporary; the unit read, the field's bits cleared and the value's
// put in their place; the unit written back by its pieces. The value as the field now reads it stays unless `discard`.
// Always inlined into both copies of the tasks' loop (check_lower_drain_body).
static inline __attribute__((always_inline)) void check_lower_bit_store(CheckLowerWriter* writer, uint32_t packed, uint32_t address, bool discard)
{
    const CheckLowerBitField field = check_lower_bit_unpack(packed);
    const uint32_t bytes = (field.shift + field.width + 7) / 8;
    const bool whole = (bytes & (bytes - 1)) == 0;
    const uint64_t mask = field.width == 64 ? UINT64_MAX : (UINT64_C(1) << field.width) - 1;
    const uint32_t value = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
    if (field.class != BUSTER_TYPED_CLASS_U64)
    {
        check_lower_emit(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
    }
    if (field.width != 64)
    {
        check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, mask);
        check_lower_emit(writer, BUSTER_TYPED_AND, BUSTER_TYPED_CLASS_U64, 0);
    }
    check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, value);
    if (whole)
    {
        // The address the piece's SET writes through, under the unit.
        check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, address);
    }
    check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, address);
    check_lower_bit_unit(writer, bytes);
    check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, ~(mask << field.shift));
    check_lower_emit(writer, BUSTER_TYPED_AND, BUSTER_TYPED_CLASS_U64, 0);
    check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, value);
    if (field.shift != 0)
    {
        check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, field.shift);
        check_lower_emit(writer, BUSTER_TYPED_SHL, BUSTER_TYPED_CLASS_U64, 0);
    }
    check_lower_emit(writer, BUSTER_TYPED_OR, BUSTER_TYPED_CLASS_U64, 0);
    if (whole)
    {
        const uint8_t class = check_lower_piece_class(bytes);
        if (class != BUSTER_TYPED_CLASS_U64)
        {
            check_lower_emit(writer, BUSTER_TYPED_CONVERT, class, 0);
        }
        check_lower_emit(writer, BUSTER_TYPED_SET, class, 0);
    }
    else
    {
        const uint32_t unit = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
        check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, unit);
        uint32_t offset = 0;
        while (offset < bytes && writer->output->status == CHECK_PROGRAM_OK)
        {
            const uint32_t piece = check_lower_piece(bytes - offset);
            const uint8_t class = check_lower_piece_class(piece);
            check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, address);
            check_lower_offset(writer, offset);
            check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, unit);
            if (offset != 0)
            {
                check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 8 * offset);
                check_lower_emit(writer, BUSTER_TYPED_SHR, BUSTER_TYPED_CLASS_U64, 0);
            }
            if (class != BUSTER_TYPED_CLASS_U64)
            {
                check_lower_emit(writer, BUSTER_TYPED_CONVERT, class, 0);
            }
            check_lower_emit(writer, BUSTER_TYPED_SET, class, 0);
            offset += piece;
        }
    }
    if (!discard)
    {
        check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, value);
        check_lower_bit_extract(writer, &field, 0);
    }
}

static uint8_t check_lower_binary(CheckOp op)
{
    uint8_t kind = BUSTER_TYPED_NONE;
    switch (op)
    {
        case CHECK_OP_ADD: case CHECK_OP_ADD_ASSIGN: kind = BUSTER_TYPED_ADD; break;
        case CHECK_OP_SUBTRACT: case CHECK_OP_SUBTRACT_ASSIGN: kind = BUSTER_TYPED_SUB; break;
        case CHECK_OP_MULTIPLY: case CHECK_OP_MULTIPLY_ASSIGN: kind = BUSTER_TYPED_MUL; break;
        case CHECK_OP_DIVIDE: case CHECK_OP_DIVIDE_ASSIGN: kind = BUSTER_TYPED_DIV; break;
        case CHECK_OP_REMAINDER: case CHECK_OP_REMAINDER_ASSIGN: kind = BUSTER_TYPED_MOD; break;
        case CHECK_OP_SHIFT_LEFT: case CHECK_OP_SHIFT_LEFT_ASSIGN: kind = BUSTER_TYPED_SHL; break;
        case CHECK_OP_SHIFT_RIGHT: case CHECK_OP_SHIFT_RIGHT_ASSIGN: kind = BUSTER_TYPED_SHR; break;
        case CHECK_OP_BIT_AND: case CHECK_OP_BIT_AND_ASSIGN: kind = BUSTER_TYPED_AND; break;
        case CHECK_OP_BIT_OR: case CHECK_OP_BIT_OR_ASSIGN: kind = BUSTER_TYPED_OR; break;
        case CHECK_OP_BIT_XOR: case CHECK_OP_BIT_XOR_ASSIGN: kind = BUSTER_TYPED_XOR; break;
        case CHECK_OP_LESS: kind = BUSTER_TYPED_LT; break;
        case CHECK_OP_LESS_EQUAL: kind = BUSTER_TYPED_LE; break;
        case CHECK_OP_GREATER: kind = BUSTER_TYPED_GT; break;
        case CHECK_OP_GREATER_EQUAL: kind = BUSTER_TYPED_GE; break;
        case CHECK_OP_EQUAL: kind = BUSTER_TYPED_EQ; break;
        case CHECK_OP_NOT_EQUAL: kind = BUSTER_TYPED_NE; break;
        default: break;
    }
    return kind;
}

// Whether a class is an integer narrower than 32 bits, which Tier 0 compares only once converted (`!E` is `0 == E`,
// where E is promoted).
static bool check_lower_narrow(uint8_t class)
{
    return class >= BUSTER_TYPED_CLASS_I8 && class <= BUSTER_TYPED_CLASS_U16;
}

static void check_lower_truth(CheckLowerWriter* writer, TypeId type, bool negate)
{
    const uint8_t class = check_lower_class(writer, type);
    const bool narrow = check_lower_narrow(class);
    if (check_lower_complex(writer, type))
    {
        check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_COMPLEX_TRUTH, .type = type, .address = negate });
    }
    else if (class == BUSTER_TYPED_CLASS_VECTOR)
    {
        check_lower_unsupported(writer, "vector logical operators and conditions");
    }
    else
    {
        check_lower_instruction(writer, negate ? BUSTER_TYPED_EQ : BUSTER_TYPED_NE, BUSTER_TYPED_CLASS_I32, 0);
        check_lower_instruction(writer, BUSTER_TYPED_CONST, narrow ? BUSTER_TYPED_CLASS_I32 : class, 0);
    }
    if (narrow)
    {
        check_lower_instruction(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_I32, 0);
    }
}

// A conversion from or to a vector (#136), of the value on top of the stack: a cast between vectors of the same bytes is
// nothing, as their bits are the same; to void, a DISCARD; a scalar's splat converts it to the element's type first, then
// sets every element to it (a row's, check_vector_lower_row), an element of 1 or 2 bytes from an int's of its signedness:
// vpbroadcastb and vpbroadcastw read a 32-bit register's low byte or word, and a row's integer operand is no narrower
// than its field (tier0_operation_role_valid). A cast between a vector and an integer of its bytes isn't lowered.
static __attribute__((noinline)) void check_lower_vector_conversion(CheckLowerWriter* writer, TypeId from, TypeId to)
{
    const TypeTable* types = writer->program->types;
    const uint32_t source = check_lower_vector_bytes(types, from);
    const uint32_t target = check_lower_vector_bytes(types, to);
    if (source != 0 && target == source)
    {
    }
    else if (source != 0 && type_unqualified(type_strip_aligned(types, to)) == BUSTER_TYPE_VOID)
    {
        check_lower_emit(writer, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_VECTOR, 0);
    }
    else if (source == 0 && target != 0 && check_lower_class(writer, from) < BUSTER_TYPED_CLASS_F80 &&
             (check_action_is_integer(types, from) || check_lower_class(writer, from) == BUSTER_TYPED_CLASS_F32 || check_lower_class(writer, from) == BUSTER_TYPED_CLASS_F64))
    {
        const TypeId element = type_unqualified(type_strip_aligned(types, type_element(types, type_unqualified(type_strip_aligned(types, to)))));
        const uint8_t element_class = check_lower_class(writer, element);
        const bool narrow = element_class >= BUSTER_TYPED_CLASS_I8 && element_class <= BUSTER_TYPED_CLASS_U16;
        const TypeId wide = element_class == BUSTER_TYPED_CLASS_U8 || element_class == BUSTER_TYPED_CLASS_U16 ? BUSTER_TYPE_UINT : BUSTER_TYPE_INT;
        check_lower_vector_row(writer, BUSTER_CHECK_VECTOR_SPLAT, to, narrow ? check_lower_class(writer, wide) : element_class);
        if (narrow)
        {
            check_lower_convert(writer, element, wide);
        }
        check_lower_convert(writer, from, element);
    }
    else
    {
        check_lower_unsupported(writer, "casts between vectors and integers or vectors of other sizes");
    }
}

// A conversion of the value on top of the stack from `from` to `to`, neither of them complex (but to void): a complex
// one's are check_lower_complex_conversion's, which converts its parts here. Always inlined into both copies of the
// tasks' loop (check_lower_drain_body), through check_lower_conversion.
static inline __attribute__((always_inline)) void check_lower_scalar_conversion(CheckLowerWriter* writer, TypeId from, TypeId to)
{
    const TypeTable* types = writer->program->types;
    const uint8_t source = check_lower_value_class(writer, from);
    const uint8_t target = check_lower_value_class(writer, to);
    // An aggregate converts only to its own (compatible) type or to void: GNU's cast to a union isn't lowered.
    const bool mixed = check_lower_aggregate(writer, from) != check_lower_aggregate(writer, to) && target != BUSTER_TYPED_CLASS_VOID;
    if (source >= BUSTER_TYPED_CLASS_VECTOR || target >= BUSTER_TYPED_CLASS_VECTOR || mixed)
    {
        // VECTOR is the last class: a vector's conversions are out of line (check_lower_vector_conversion).
        if (!mixed && source < BUSTER_TYPED_CLASSES && target < BUSTER_TYPED_CLASSES)
        {
            check_lower_vector_conversion(writer, from, to);
        }
        else
        {
            check_lower_unsupported(writer, mixed ? "casts to union types" : check_lower_type_feature(types, source >= BUSTER_TYPED_CLASSES ? from : to));
        }
    }
    else if (target == BUSTER_TYPED_CLASS_VOID)
    {
        if (source != BUSTER_TYPED_CLASS_VOID)
        {
            check_lower_emit(writer, BUSTER_TYPED_DISCARD, source, 0);
        }
    }
    else if ((check_lower_facts(writer, to) & BUSTER_CHECK_LOWER_TYPE_BOOL) != 0)
    {
        if (check_lower_narrow(source))
        {
            check_lower_emit(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_I32, 0);
        }
        check_lower_emit(writer, BUSTER_TYPED_CONST, check_lower_narrow(source) ? BUSTER_TYPED_CLASS_I32 : source, 0);
        check_lower_emit(writer, BUSTER_TYPED_NE, BUSTER_TYPED_CLASS_I32, 0);
        check_lower_emit(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U8, 0);
    }
    else if (check_lower_wide_class(source) && (target == BUSTER_TYPED_CLASS_F32 || target == BUSTER_TYPED_CLASS_F64 || target == BUSTER_TYPED_CLASS_F80))
    {
        // libgcc's __floattisf ... __floatuntixf of the value, from a frame object of its own.
        const bool is_signed = source == BUSTER_TYPED_CLASS_I128;
        const uint32_t index = target == BUSTER_TYPED_CLASS_F32 ? 0 : target == BUSTER_TYPED_CLASS_F64 ? 1 : 2;
        const CheckLowerRuntime runtime = (CheckLowerRuntime)((is_signed ? CHECK_LOWER_RUNTIME_FLOATTISF : CHECK_LOWER_RUNTIME_FLOATUNTISF) + index);
        const TypeId result = target == BUSTER_TYPED_CLASS_F32 ? BUSTER_TYPE_FLOAT : target == BUSTER_TYPED_CLASS_F64 ? BUSTER_TYPE_DOUBLE : BUSTER_TYPE_LONG_DOUBLE;
        const TypeId argument = is_signed ? BUSTER_TYPE_INT128 : BUSTER_TYPE_UINT128;
        const uint32_t local = check_lower_temporary(writer, source);
        const uint32_t call = writer->output->status == CHECK_PROGRAM_OK ? check_lower_runtime_call(writer, runtime, result, &argument, 1, BUSTER_CHECK_NONE) : 0;
        check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, source, local);
        check_lower_emit(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, local);
        check_lower_emit(writer, BUSTER_TYPED_CALL, target, call);
    }
    else if (check_lower_wide_class(target) && (source == BUSTER_TYPED_CLASS_F32 || source == BUSTER_TYPED_CLASS_F64 || source == BUSTER_TYPED_CLASS_F80))
    {
        // libgcc's __fixsfti ... __fixunsxfti, its result from a frame object of its own.
        const bool is_signed = target == BUSTER_TYPED_CLASS_I128;
        const uint32_t index = source == BUSTER_TYPED_CLASS_F32 ? 0 : source == BUSTER_TYPED_CLASS_F64 ? 1 : 2;
        const CheckLowerRuntime runtime = (CheckLowerRuntime)((is_signed ? CHECK_LOWER_RUNTIME_FIXSFTI : CHECK_LOWER_RUNTIME_FIXUNSSFTI) + index);
        const TypeId result = is_signed ? BUSTER_TYPE_INT128 : BUSTER_TYPE_UINT128;
        const TypeId argument = source == BUSTER_TYPED_CLASS_F32 ? BUSTER_TYPE_FLOAT : source == BUSTER_TYPED_CLASS_F64 ? BUSTER_TYPE_DOUBLE : BUSTER_TYPE_LONG_DOUBLE;
        const uint32_t local = check_lower_temporary(writer, target);
        const uint32_t call = writer->output->status == CHECK_PROGRAM_OK ? check_lower_runtime_call(writer, runtime, result, &argument, 1, local) : 0;
        check_lower_emit(writer, BUSTER_TYPED_CALL, BUSTER_TYPED_CLASS_VOID, call);
        check_lower_emit(writer, BUSTER_TYPED_LOCAL, target, local);
    }
    else if (source != target)
    {
        check_lower_emit(writer, BUSTER_TYPED_CONVERT, target, 0);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Complex values
//
// A complex value of a floating element (check_lower_complex) is a frame object of two elements, the real part then the
// imaginary one, which the stream carries by its address, as an aggregate's. An operation computes into a frame object
// of its own (check_lower_complex_compute) from its operands' parts, which it reads through temporaries once both are
// evaluated (CheckLowerComplexOperand). A real operand stays real, as C keeps its type domain and clang computes it:
// no imaginary part joins a sum, a difference or a product with it, so that signed zeros and infinities come out as
// clang's. The product and the quotient of two complex values are libgcc's (__mulsc3 ... __divxc3), as clang -O0 calls
// them (its inline product, which hands a result of two NaNs to __muldc3, has the function's values), and so is a real
// dividend's over a complex divisor, its imaginary part +0.0.

// An operand of a complex operation: in temporary `local`, a complex one's address (U64), or a real one's value, of the
// computation's element's class.
typedef struct CheckLowerComplexOperand CheckLowerComplexOperand;
struct CheckLowerComplexOperand
{
    uint32_t local;
    bool complex;
};

// The address of part `part` (0 the real one, 1 the imaginary one) of frame object `object`, whose element is `size`
// bytes, onto the stack.
static void check_lower_complex_place(CheckLowerWriter* writer, uint32_t object, uint32_t part, uint64_t size)
{
    check_lower_emit(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, object);
    check_lower_offset(writer, part * size);
}

// Part `part` of `operand` onto the stack, of `class`, whose size is `size`: a complex one's loaded from its address, a
// real one's value, or for its imaginary part +0.0 (CONST 0, which for F80 is the constants' place 0).
static void check_lower_complex_part(CheckLowerWriter* writer, CheckLowerComplexOperand operand, uint32_t part, uint8_t class, uint64_t size)
{
    if (operand.complex)
    {
        check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, operand.local);
        check_lower_offset(writer, part * size);
        check_lower_emit(writer, BUSTER_TYPED_LOAD, class, 0);
    }
    else if (part == 0)
    {
        check_lower_emit(writer, BUSTER_TYPED_LOCAL, class, operand.local);
    }
    else
    {
        check_lower_emit(writer, BUSTER_TYPED_CONST, class, 0);
    }
}

// `op` (ADD, SUBTRACT, MULTIPLY, DIVIDE, NEGATIVE, BIT_NOT, the conjugate, or EQUAL and NOT_EQUAL) of `a` and `b` (`a`
// alone for a unary one) in complex type `type`: a result in a frame object of its own, which it returns, or for a
// comparison an I32 onto the stack, and BUSTER_CHECK_NONE.
static uint32_t check_lower_complex_compute(CheckLowerWriter* writer, CheckOp op, TypeId type, CheckLowerComplexOperand a, CheckLowerComplexOperand b)
{
    const TypeTable* types = writer->program->types;
    const TypeId element = check_lower_complex_element(types, type);
    const uint8_t class = check_lower_class(writer, element);
    const uint64_t size = type_size(types, element);
    const bool sum = op == CHECK_OP_ADD || op == CHECK_OP_SUBTRACT;
    const bool library = (op == CHECK_OP_MULTIPLY && a.complex && b.complex) || (op == CHECK_OP_DIVIDE && b.complex);
    uint32_t result = BUSTER_CHECK_NONE;
    if (op == CHECK_OP_EQUAL || op == CHECK_OP_NOT_EQUAL)
    {
        // Both parts compared, as clang compares them: equal when both are, unequal when either is (an unordered part is).
        for (uint32_t part = 0; part < 2; part += 1)
        {
            check_lower_complex_part(writer, a, part, class, size);
            check_lower_complex_part(writer, b, part, class, size);
            check_lower_emit(writer, op == CHECK_OP_EQUAL ? BUSTER_TYPED_EQ : BUSTER_TYPED_NE, BUSTER_TYPED_CLASS_I32, 0);
        }
        check_lower_emit(writer, op == CHECK_OP_EQUAL ? BUSTER_TYPED_AND : BUSTER_TYPED_OR, BUSTER_TYPED_CLASS_I32, 0);
    }
    else
    {
        result = check_lower_object(writer, 2 * size, type_alignment_log2(types, element));
    }
    if (result != BUSTER_CHECK_NONE && library)
    {
        // libgcc's __mulsc3 ... __divxc3 of the four parts, its result into the object.
        const uint32_t index = class == BUSTER_TYPED_CLASS_F32 ? 0 : class == BUSTER_TYPED_CLASS_F64 ? 1 : 2;
        const CheckLowerRuntime runtime = (CheckLowerRuntime)((op == CHECK_OP_MULTIPLY ? CHECK_LOWER_RUNTIME_MULSC3 : CHECK_LOWER_RUNTIME_DIVSC3) + index);
        const TypeId arguments[4] = { element, element, element, element };
        const TypeId returned = type_unqualified(type_strip_aligned(types, type));
        const uint32_t call = writer->output->status == CHECK_PROGRAM_OK ? check_lower_runtime_call(writer, runtime, returned, arguments, 4, result) : 0;
        for (uint32_t i = 0; i < 4; i += 1)
        {
            check_lower_complex_part(writer, i < 2 ? a : b, i % 2, class, size);
        }
        check_lower_emit(writer, BUSTER_TYPED_CALL, BUSTER_TYPED_CLASS_VOID, call);
    }
    for (uint32_t part = 0; result != BUSTER_CHECK_NONE && !library && part < 2; part += 1)
    {
        check_lower_complex_place(writer, result, part, size);
        if (op == CHECK_OP_NEGATIVE || (op == CHECK_OP_BIT_NOT && part == 1))
        {
            check_lower_complex_part(writer, a, part, class, size);
            check_lower_emit(writer, BUSTER_TYPED_NEG, class, 0);
        }
        else if (op == CHECK_OP_BIT_NOT)
        {
            check_lower_complex_part(writer, a, part, class, size);
        }
        else if (sum && part == 1 && !a.complex)
        {
            // A real value plus or minus a complex one: the complex one's imaginary part, negated for a difference.
            check_lower_complex_part(writer, b, 1, class, size);
            if (op == CHECK_OP_SUBTRACT)
            {
                check_lower_emit(writer, BUSTER_TYPED_NEG, class, 0);
            }
        }
        else if (sum && part == 1 && !b.complex)
        {
            check_lower_complex_part(writer, a, 1, class, size);
        }
        else
        {
            // The parts' sum or difference, or a product with a real value or a quotient by one, which each part takes.
            check_lower_complex_part(writer, a, a.complex ? part : 0, class, size);
            check_lower_complex_part(writer, b, b.complex ? part : 0, class, size);
            check_lower_emit(writer, check_lower_binary(op), class, 0);
        }
        check_lower_emit(writer, BUSTER_TYPED_SET, class, 0);
    }
    return result;
}

// The truth of the complex value of `type` whose address is on top of the stack, as an I32: 1 when either part isn't
// zero (an unordered one isn't), as clang tests one, or with `negate` its negation.
static void check_lower_complex_truth(CheckLowerWriter* writer, TypeId type, bool negate)
{
    const TypeTable* types = writer->program->types;
    const TypeId element = check_lower_complex_element(types, type);
    const uint8_t class = check_lower_class(writer, element);
    const CheckLowerComplexOperand value = { .local = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64), .complex = true };
    check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, value.local);
    for (uint32_t part = 0; part < 2; part += 1)
    {
        check_lower_complex_part(writer, value, part, class, type_size(types, element));
        check_lower_emit(writer, BUSTER_TYPED_CONST, class, 0);
        check_lower_emit(writer, BUSTER_TYPED_NE, BUSTER_TYPED_CLASS_I32, 0);
    }
    check_lower_emit(writer, BUSTER_TYPED_OR, BUSTER_TYPED_CLASS_I32, 0);
    if (negate)
    {
        check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 0);
        check_lower_emit(writer, BUSTER_TYPED_EQ, BUSTER_TYPED_CLASS_I32, 0);
    }
}

// A conversion of the value on top of the stack from `from` to `to`, one of them complex (check_lower_complex), and `to`
// not void: between complex types, each part converted into a frame object of the target's (nothing between ones of the
// same element); from one, its real part converted, or for _Bool its truth; to one, the value converted as its real
// part, +0.0 the imaginary one.
static void check_lower_complex_conversion(CheckLowerWriter* writer, TypeId from, TypeId to)
{
    const TypeTable* types = writer->program->types;
    const TypeId from_element = check_lower_complex_element(types, from);
    const TypeId to_element = check_lower_complex_element(types, to);
    const uint8_t from_class = from_element != 0 ? check_lower_class(writer, from_element) : check_lower_value_class(writer, from);
    const uint8_t to_class = to_element != 0 ? check_lower_class(writer, to_element) : 0;
    const uint64_t size = to_element != 0 ? type_size(types, to_element) : 0;
    const uint32_t result = to_element != 0 && to_element != from_element ? check_lower_object(writer, 2 * size, type_alignment_log2(types, to_element)) :
                                                                            BUSTER_CHECK_NONE;
    if (from_element != 0 && result != BUSTER_CHECK_NONE)
    {
        const CheckLowerComplexOperand source = { .local = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64), .complex = true };
        check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, source.local);
        for (uint32_t part = 0; part < 2; part += 1)
        {
            check_lower_complex_place(writer, result, part, size);
            check_lower_complex_part(writer, source, part, from_class, type_size(types, from_element));
            check_lower_scalar_conversion(writer, from_element, to_element);
            check_lower_emit(writer, BUSTER_TYPED_SET, to_class, 0);
        }
    }
    else if (from_element != 0 && to_element == 0 && type_unqualified(type_strip_aligned(types, to)) == BUSTER_TYPE_BOOL)
    {
        check_lower_complex_truth(writer, from, false);
        check_lower_emit(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U8, 0);
    }
    else if (from_element != 0 && to_element == 0)
    {
        check_lower_emit(writer, BUSTER_TYPED_LOAD, from_class, 0);
        check_lower_scalar_conversion(writer, from_element, to);
    }
    else if (result != BUSTER_CHECK_NONE)
    {
        const CheckLowerComplexOperand value = { .local = check_lower_temporary(writer, from_class), .complex = false };
        check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, from_class, value.local);
        check_lower_complex_place(writer, result, 0, size);
        check_lower_emit(writer, BUSTER_TYPED_LOCAL, from_class, value.local);
        check_lower_scalar_conversion(writer, from, to_element);
        check_lower_emit(writer, BUSTER_TYPED_SET, to_class, 0);
        check_lower_complex_place(writer, result, 1, size);
        check_lower_emit(writer, BUSTER_TYPED_CONST, to_class, 0);
        check_lower_emit(writer, BUSTER_TYPED_SET, to_class, 0);
    }
    if (result != BUSTER_CHECK_NONE)
    {
        check_lower_emit(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, result);
    }
}

// The conversion of the value on top of the stack from `from` to `to` (a CHECK_LOWER_CONVERSION task's): a complex type's
// check_lower_complex_conversion's, any other check_lower_scalar_conversion's. Always inlined into both copies of the
// tasks' loop (check_lower_drain_body).
static inline __attribute__((always_inline)) void check_lower_conversion(CheckLowerWriter* writer, TypeId from, TypeId to)
{
    if ((check_lower_complex(writer, from) || check_lower_complex(writer, to)) && check_lower_value_class(writer, to) != BUSTER_TYPED_CLASS_VOID)
    {
        check_lower_complex_conversion(writer, from, to);
    }
    else
    {
        check_lower_scalar_conversion(writer, from, to);
    }
}

// A long double constant's place in the function's table (CheckLowerWriter.x87_constants), its 80 bits as static data
// writes them (check_data_float_bytes): +0.0's is place 0, which every function's table has.
static uint64_t check_lower_x87_constant(CheckLowerWriter* writer, long double value)
{
    uint8_t bytes[16] = { 0 };
    check_data_float_bytes(bytes, BUSTER_TYPE_LONG_DOUBLE, value);
    uint64_t significand = 0;
    memcpy(&significand, bytes, sizeof(significand));
    const uint64_t top = (uint64_t)bytes[8] | ((uint64_t)bytes[9] << 8);
    uint64_t place = 0;
    if ((significand | top) != 0 && writer->x87_constant_count >= writer->x87_constant_capacity)
    {
        check_lower_fail(writer, CHECK_PROGRAM_RESOURCE);
    }
    else if ((significand | top) != 0)
    {
        place = writer->x87_constant_count++;
        writer->x87_constants[2 * place] = significand;
        writer->x87_constants[2 * place + 1] = top;
    }
    return place;
}

// A 128-bit integer constant of `class`: Tier 0's CONST takes 64 bits, sign-extended for I128 and zero-extended for U128,
// so a value one of those makes is one entry (and the other class's CONVERT); any other is its high half shifted up by
// 64, ORed with its low half.
static void check_lower_wide_constant(CheckLowerWriter* writer, uint8_t class, unsigned __int128 value)
{
    const uint64_t low = (uint64_t)value;
    const uint64_t high = (uint64_t)(value >> 64);
    if (high == (uint64_t)((int64_t)low >> 63) || high == 0)
    {
        const uint8_t made = high == 0 ? BUSTER_TYPED_CLASS_U128 : BUSTER_TYPED_CLASS_I128;
        check_lower_emit(writer, BUSTER_TYPED_CONST, made, low);
        if (made != class)
        {
            check_lower_emit(writer, BUSTER_TYPED_CONVERT, class, 0);
        }
    }
    else
    {
        check_lower_emit(writer, BUSTER_TYPED_CONST, class, high);
        check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U32, 64);
        check_lower_emit(writer, BUSTER_TYPED_SHL, class, 0);
        check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U128, low);
        if (class != BUSTER_TYPED_CLASS_U128)
        {
            check_lower_emit(writer, BUSTER_TYPED_CONVERT, class, 0);
        }
        check_lower_emit(writer, BUSTER_TYPED_OR, class, 0);
    }
}

// A floating constant's CONST value of `class` (F32, F64 or F80): its bits as the class's, or a long double's place in
// the function's table (check_lower_x87_constant).
static uint64_t check_lower_float_bits(CheckLowerWriter* writer, uint8_t class, long double value)
{
    uint64_t bits = 0;
    if (class == BUSTER_TYPED_CLASS_F80)
    {
        bits = check_lower_x87_constant(writer, value);
    }
    else if (class == BUSTER_TYPED_CLASS_F32)
    {
        const float number = (float)value;
        uint32_t small;
        memcpy(&small, &number, sizeof(small));
        bits = small;
    }
    else
    {
        const double number = (double)value;
        memcpy(&bits, &number, sizeof(bits));
    }
    return bits;
}

// A complex constant (check_lower_complex): its parts into a frame object of its own, whose address it leaves, the real
// one a floating constant's (its imaginary one +0.0) or both a complex one's, as static data takes them.
static void check_lower_complex_constant(CheckLowerWriter* writer, const CheckValue* value)
{
    const TypeTable* types = writer->program->types;
    const TypeId element = check_lower_complex_element(types, value->type);
    const uint8_t class = check_lower_class(writer, element);
    const uint64_t size = type_size(types, element);
    const long double parts[2] = { value->floating, value->constant_kind == CHECK_CONSTANT_COMPLEX ? value->imaginary : 0.0L };
    if (value->constant_kind != CHECK_CONSTANT_COMPLEX && value->constant_kind != CHECK_CONSTANT_FLOATING)
    {
        check_lower_fail(writer, CHECK_PROGRAM_INVALID);
    }
    else
    {
        const uint32_t result = check_lower_object(writer, 2 * size, type_alignment_log2(types, element));
        for (uint32_t part = 0; part < 2; part += 1)
        {
            check_lower_complex_place(writer, result, part, size);
            check_lower_emit(writer, BUSTER_TYPED_CONST, class, check_lower_float_bits(writer, class, parts[part]));
            check_lower_emit(writer, BUSTER_TYPED_SET, class, 0);
        }
        check_lower_emit(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, result);
    }
}

static void check_lower_constant(CheckLowerWriter* writer, const CheckValue* value)
{
    const uint8_t class = check_lower_class(writer, value->type);
    uint64_t bits = (uint64_t)value->integer;
    if (value->constant_kind == CHECK_CONSTANT_ADDRESS)
    {
        check_lower_address(writer, value->object, value->offset);
        // An lvalue's constant address is the object's, which its caller takes, whatever the object's type. An rvalue's is
        // its value: an address cast to `long` (`(long)&x`, which the checker keeps as a constant: a cast to 64 bits) is
        // the address's bits in the cast's class.
        if (value->category == CHECK_RVALUE && class >= BUSTER_TYPED_CLASS_I8 && class <= BUSTER_TYPED_CLASS_I64)
        {
            check_lower_emit(writer, BUSTER_TYPED_CONVERT, class, 0);
        }
    }
    else if (check_lower_complex(writer, value->type))
    {
        check_lower_complex_constant(writer, value);
    }
    else if (class == BUSTER_TYPED_CLASS_F80 && value->constant_kind != CHECK_CONSTANT_FLOATING)
    {
        // Static data takes a long double's value from a floating constant alone, and so does this.
        check_lower_fail(writer, CHECK_PROGRAM_INVALID);
    }
    else
    {
        if (class == BUSTER_TYPED_CLASS_F80 || (value->constant_kind == CHECK_CONSTANT_FLOATING && (class == BUSTER_TYPED_CLASS_F32 || class == BUSTER_TYPED_CLASS_F64)))
        {
            bits = check_lower_float_bits(writer, class, value->floating);
        }
        else if (class >= BUSTER_TYPED_CLASS_I8 && class <= BUSTER_TYPED_CLASS_U64)
        {
            const uint32_t width = 8u << ((class - 1) / 2);
            if (width < 64)
            {
                const uint64_t mask = (UINT64_C(1) << width) - 1;
                bits &= mask;
                if ((class & 1) != 0 && (bits >> (width - 1)) != 0)
                {
                    bits |= ~mask;
                }
            }
        }
        if (check_lower_wide_class(class))
        {
            check_lower_wide_constant(writer, class, value->integer);
        }
        else
        {
            check_lower_emit(writer, BUSTER_TYPED_CONST, class, bits);
        }
    }
}

static void check_lower_call(CheckLowerWriter* writer, uint32_t node)
{
    const CheckGraph* graph = &writer->program->input->graph;
    const CheckStream* stream = writer->program->stream;
    const TypeTable* types = writer->program->types;
    const uint32_t callee = graph->operand0[node];
    // The call's entries are its callee's line's, as GCC and clang place a call (the call's own node is its closing
    // parenthesis, which a call across lines ends on).
    check_lower_source(writer, graph->tokens[callee]);
    CheckValue value = stream->values[callee];
    const uint32_t overload = check_program_builtin_index(stream->overloads, stream->overload_count, sizeof(CheckOverloadEffect), node);
    bool mangled = false;
    if (overload != BUSTER_CHECK_NONE)
    {
        const CheckOverloadResult* selected = &stream->overloads[overload].selected;
        value = (CheckValue){ .type = selected->type, .category = CHECK_FUNCTION, .constant_kind = CHECK_CONSTANT_ADDRESS,
                              .object = selected->object, .flags = BUSTER_CHECK_STATIC_OBJECT };
        mangled = (selected->flags & BUSTER_CHECK_OVERLOADABLE) != 0;
    }
    const uint32_t count = graph->argument_counts[node];
    const uint32_t first = graph->argument_offsets[node];
    TypeId type = type_strip_aligned(types, value.type);
    if (type_kind(types, type) == BUSTER_TYPE_KIND_POINTER)
    {
        type = type_strip_aligned(types, type_element(types, type));
    }
    uint32_t parameters = 0;
    const TypeId* parameter_types = type_parameters(types, type, &parameters);
    const uint8_t flags = types->types.flags[type_index(type)];
    const bool variadic = (flags & BUSTER_TYPE_FLAG_VARIADIC) != 0 || (flags & BUSTER_TYPE_FLAG_PROTOTYPE) == 0;
    CheckLowerAbi* abi = writer->abi;
    // What the parameters' and the arguments' types need that isn't lowered, the first one's (a struct or a union passes
    // its address, and its bytes go where abi_assign puts them).
    const char* passed = NULL;
    for (uint32_t a = 0; a < parameters && passed == NULL; a += 1)
    {
        passed = check_lower_passed(writer, parameter_types[a]);
    }
    for (uint32_t a = 0; a < count && passed == NULL; a += 1)
    {
        const CheckConversion* conversion = stream->argument_conversions + first + a;
        passed = (conversion->flags & BUSTER_CHECK_CONVERT_TRANSPARENT_UNION) != 0 ? "transparent unions" : check_lower_passed(writer, conversion->type);
    }
    // A struct or union result lands in a frame object of the call's own, whose address is the call's value, and so do a
    // 128-bit integer's and a vector's, which LOCAL then reads.
    const TypeId result = stream->values[node].type;
    const bool aggregate_result = check_lower_aggregate(writer, result);
    const bool wide_result = check_lower_wide(writer, result) || check_lower_vector_of(writer, result) != 0;
    const char* returned = aggregate_result ? check_lower_by_value(writer, result, true) : check_lower_class(writer, result) == UINT8_MAX ? check_lower_type_feature(types, result) : NULL;
    // A callee that is a function's own symbol is called directly; any other (a pointer's value, a function designator
    // through one) is evaluated before the arguments and called through: Tier 0's indirect call, callee 0.
    const bool direct = value.category != CHECK_LVALUE && value.constant_kind == CHECK_CONSTANT_ADDRESS && value.object != 0 && value.offset == 0 &&
                        value.object < writer->program->object_count && writer->program->object_symbols[value.object] != 0 &&
                        (value.flags & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_FP_ENVIRONMENT)) == 0;
    if (mangled || passed != NULL || count > BUSTER_TIER0_MAX_CALL_ARGUMENTS || returned != NULL)
    {
        check_lower_unsupported(writer, mangled ? "calls of overloadable functions" : count > BUSTER_TIER0_MAX_CALL_ARGUMENTS ? "calls with more than 239 arguments" :
                                        passed != NULL ? passed : returned);
    }
    else if (abi->count >= abi->capacity || count > abi->argument_capacity - abi->argument_count)
    {
        check_lower_fail(writer, CHECK_PROGRAM_RESOURCE);
    }
    else
    {
        // A callee takes a hidden result pointer as aligned to the result's canonical type, an aligned typedef's attribute
        // left out (clang's ABI info has canonical types; its stores through the pointer may be movaps): the object is
        // aligned to the more aligned of the two.
        const uint32_t declared = aggregate_result ? type_alignment_log2(types, result) : 0;
        const uint32_t canonical = aggregate_result ? type_alignment_log2(types, type_strip_aligned(types, result)) : 0;
        const uint32_t call = writer->call_count++;
        const uint8_t result_class = check_lower_class(writer, result);
        const uint32_t object = aggregate_result ? check_lower_object(writer, type_size(types, result), declared > canonical ? declared : canonical) :
                                wide_result      ? check_lower_temporary_of(writer, result) :
                                                   BUSTER_CHECK_NONE;
        writer->call_targets[call] = direct ? writer->program->object_symbols[value.object] : 0;
        writer->call_first[call] = writer->argument_count;
        writer->call_counts[call] = count;
        writer->call_variadic[call] = variadic;
        writer->call_result_objects[call] = aggregate_result || wide_result ? object + 1 : 0;
        writer->aggregates |= aggregate_result || wide_result;
        abi->first[abi->count] = abi->argument_count;
        abi->results[abi->count] = result;
        abi->counts[abi->count] = count;
        abi->named[abi->count] = (flags & BUSTER_TYPE_FLAG_PROTOTYPE) != 0 ? parameters : count;
        abi->variadic[abi->count++] = variadic;
        for (uint32_t a = 0; a < count; a += 1)
        {
            // A 128-bit integer argument is a 16-byte object's address, the value set into it after it is made, and a
            // vector's an object of its bytes.
            const TypeId argument = stream->argument_conversions[first + a].type;
            const TypeId passed_type = a < parameters && check_action_kind(types, parameter_types[a]) == BUSTER_TYPE_KIND_ATOMIC
                                           ? parameter_types[a]
                                           : argument;
            const bool aggregate = check_lower_aggregate(writer, argument);
            const uint32_t vector = check_lower_vector_of(writer, argument);
            const bool wide = check_lower_wide(writer, argument) || vector != 0;
            abi->arguments[abi->argument_count++] = passed_type;
            writer->argument_sizes[writer->argument_count] = aggregate ? (uint32_t)type_size(types, passed_type) : vector != 0 ? vector : wide ? 16 : 0;
            writer->aggregates |= aggregate || wide;
            writer->argument_classes[writer->argument_count++] = wide ? BUSTER_TYPED_CLASS_U64 : check_lower_value_class(writer, argument);
        }
        if (aggregate_result)
        {
            check_lower_instruction(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, object);
        }
        else if (wide_result)
        {
            check_lower_instruction(writer, BUSTER_TYPED_LOCAL, result_class, object);
        }
        check_lower_instruction(writer, BUSTER_TYPED_CALL, aggregate_result || wide_result ? BUSTER_TYPED_CLASS_VOID : result_class, call);
        for (uint32_t a = count; a-- > 0 && writer->output->status == CHECK_PROGRAM_OK;)
        {
            const TypeId argument = stream->argument_conversions[first + a].type;
            const bool atomic = a < parameters && check_action_kind(types, parameter_types[a]) == BUSTER_TYPE_KIND_ATOMIC;
            if (atomic && check_lower_aggregate(writer, argument))
            {
                // The atomic parameter keeps its own ABI classification and storage padding, while its initializer
                // supplies only the underlying value's bytes. Capture those bytes into a padded argument object.
                const TypeId passed_type = parameter_types[a];
                const uint64_t bytes = type_size(types, passed_type);
                const uint32_t local = check_lower_object(writer, bytes, type_alignment_log2(types, passed_type));
                check_lower_instruction(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, local);
                check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_COPY, .value = type_size(types, type_conversion_operand(types, passed_type)),
                                                         .volatile_copy = check_lower_copy_node_volatile(writer, graph->argument_nodes[first + a], argument) });
                check_lower_operand(writer, node, a + 3, false, argument);
                check_lower_instruction(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, local);
                check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_CLEAR, .type = (TypeId)bytes, .value = local });
            }
            else
            {
                if (check_lower_wide(writer, argument) || check_lower_vector_of(writer, argument) != 0)
                {
                    const uint8_t class = check_lower_class(writer, argument);
                    const uint32_t local = check_lower_temporary_of(writer, argument);
                    check_lower_instruction(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, local);
                    check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, class, local);
                }
                check_lower_operand(writer, node, a + 3, false, argument);
            }
        }
        if (!direct)
        {
            check_lower_operand(writer, node, 0, false, 0);
        }
    }
}

static void check_lower_helper(CheckLowerWriter* writer, uint32_t node)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const CheckProgramHelpers* helpers = &writer->output->helpers;
    const uint32_t selected = check_program_builtin_index(helpers->uses, helpers->use_count, sizeof(CheckProgramHelperUse), node);
    const uint32_t count = graph->argument_counts[node];
    const uint32_t first = graph->argument_offsets[node];
    const CBuiltinId source_id = (CBuiltinId)graph->members[node];
    const bool c11 = check_builtin_atomic_c11(source_id);
    const bool compare = source_id == CBUILTIN_C11_ATOMIC_COMPARE_EXCHANGE_STRONG || source_id == CBUILTIN_C11_ATOMIC_COMPARE_EXCHANGE_WEAK;
    const bool init = source_id == CBUILTIN_C11_ATOMIC_INIT;
    const uint32_t parameters = count + compare + init;
    CheckLowerAbi* abi = writer->abi;
    if (selected == BUSTER_CHECK_NONE || helpers->uses[selected].helper >= helpers->count)
    {
        check_lower_unsupported(writer, "this builtin");
    }
    else if (abi->count >= abi->capacity || parameters > abi->argument_capacity - abi->argument_count)
    {
        check_lower_fail(writer, CHECK_PROGRAM_RESOURCE);
    }
    else
    {
        const CheckProgramHelper* helper = helpers->items + helpers->uses[selected].helper;
        if (parameters != helper->parameter_count)
        {
            check_lower_fail(writer, CHECK_PROGRAM_INVALID);
        }
        else
        {
            const uint32_t call = writer->call_count++;
            writer->call_targets[call] = helper->symbol;
            writer->call_first[call] = writer->argument_count;
            writer->call_counts[call] = parameters;
            writer->call_variadic[call] = 0;
            writer->call_result_objects[call] = 0;
            abi->first[abi->count] = abi->argument_count;
            abi->results[abi->count] = helper->result_type;
            abi->counts[abi->count] = parameters;
            abi->named[abi->count] = parameters;
            abi->variadic[abi->count++] = 0;
            for (uint32_t a = 0; a < parameters; a += 1)
            {
                // A vector leaf's vector (c_builtin_x86_helpers.c) is an object of its bytes, whose address the entry
                // passes, as a call's is (check_lower_call).
                const TypeId type = helper->parameter_types[a];
                const uint32_t vector = check_lower_vector_of(writer, type);
                abi->arguments[abi->argument_count++] = type;
                writer->argument_sizes[writer->argument_count] = vector;
                writer->argument_classes[writer->argument_count++] = vector != 0 ? BUSTER_TYPED_CLASS_U64 : check_lower_class(writer, type);
                writer->aggregates |= vector != 0;
            }
            // A floating value crosses an atomic helper's integer registers as its bits, through a frame temporary, as the
            // math helpers' do: the helpers move bits, so a numeric conversion would change the value. A vector leaf's
            // result lands in a frame object of the call's own, which LOCAL then reads.
            const uint8_t result_class = check_lower_class(writer, stream->values[node].type);
            const bool vector_result = check_lower_vector_of(writer, helper->result_type) != 0;
            const uint32_t object = vector_result ? check_lower_temporary_of(writer, helper->result_type) : BUSTER_CHECK_NONE;
            writer->call_result_objects[call] = vector_result ? object + 1 : 0;
            writer->aggregates |= vector_result;
            if (helper->terminates)
            {
                check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_TERMINATE });
            }
            else if (helper->result_type == BUSTER_TYPE_ULONG && (result_class == BUSTER_TYPED_CLASS_F32 || result_class == BUSTER_TYPED_CLASS_F64))
            {
                const uint32_t result = check_lower_addressed_temporary(writer, BUSTER_TYPED_CLASS_U64);
                check_lower_instruction(writer, BUSTER_TYPED_LOAD, result_class, 0);
                check_lower_instruction(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, result);
                check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, result);
            }
            else if (vector_result)
            {
                check_lower_instruction(writer, BUSTER_TYPED_LOCAL, result_class, object);
            }
            else
            {
                check_lower_convert(writer, helper->result_type, stream->values[node].type);
            }
            check_lower_instruction(writer, BUSTER_TYPED_CALL, vector_result ? BUSTER_TYPED_CLASS_VOID : check_lower_class(writer, helper->result_type), call);
            for (uint32_t a = parameters; a-- > 0 && writer->output->status == CHECK_PROGRAM_OK;)
            {
                const bool inserted = (compare && a == 3) || (init && a == 2);
                const uint32_t source = compare && a > 3 ? a - 1 : a;
                const TypeId checked = inserted ? BUSTER_TYPE_INT : stream->argument_conversions[first + source].type;
                const uint8_t class = check_lower_class(writer, checked);
                if (helper->parameter_types[a] == BUSTER_TYPE_ULONG && (class == BUSTER_TYPED_CLASS_F32 || class == BUSTER_TYPED_CLASS_F64))
                {
                    const uint32_t local = check_lower_addressed_temporary(writer, class);
                    if (class == BUSTER_TYPED_CLASS_F32)
                    {
                        check_lower_instruction(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
                    }
                    check_lower_instruction(writer, BUSTER_TYPED_LOAD, class == BUSTER_TYPED_CLASS_F32 ? BUSTER_TYPED_CLASS_U32 : BUSTER_TYPED_CLASS_U64, 0);
                    check_lower_instruction(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, local);
                    check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, class, local);
                }
                else if (check_lower_vector_of(writer, helper->parameter_types[a]) != 0)
                {
                    // The object of a vector leaf's argument, its value set into it after it is made.
                    const uint32_t local = check_lower_temporary_of(writer, checked);
                    check_lower_instruction(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, local);
                    check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, class, local);
                }
                else
                {
                    check_lower_convert(writer, checked, helper->parameter_types[a]);
                }
                if (inserted)
                {
                    check_lower_instruction(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, compare && source_id == CBUILTIN_C11_ATOMIC_COMPARE_EXCHANGE_WEAK);
                }
                else
                {
                    if (c11 && a == 1 && (source_id == CBUILTIN_C11_ATOMIC_FETCH_ADD || source_id == CBUILTIN_C11_ATOMIC_FETCH_SUB))
                    {
                        const TypeId object = type_element(program->types, stream->argument_conversions[first].type);
                        const TypeId value = type_conversion_operand(program->types, object);
                        if (check_action_kind(program->types, value) == BUSTER_TYPE_KIND_POINTER)
                        {
                            const uint64_t stride = check_lower_stride(writer, value);
                            check_lower_instruction(writer, BUSTER_TYPED_MUL, BUSTER_TYPED_CLASS_I64, 0);
                            if (stride == 0)
                            {
                                check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_SIZE, .type = type_element(program->types, value) });
                            }
                            else
                            {
                                check_lower_instruction(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, stride);
                            }
                        }
                    }
                    check_lower_operand(writer, node, source + 3, false, checked);
                }
            }
        }
    }
}

static uint32_t check_lower_native_call(CheckLowerWriter* writer, SymbolId symbol, TypeId result, const TypeId* arguments, uint32_t count)
{
    CheckLowerAbi* abi = writer->abi;
    uint32_t call = BUSTER_CHECK_NONE;
    if (symbol == 0 || result == 0 || (count != 0 && arguments == NULL))
    {
        check_lower_fail(writer, CHECK_PROGRAM_INVALID);
    }
    else if (abi->count >= abi->capacity || count > abi->argument_capacity - abi->argument_count)
    {
        check_lower_fail(writer, CHECK_PROGRAM_RESOURCE);
    }
    else
    {
        call = writer->call_count++;
        writer->call_targets[call] = symbol;
        writer->call_first[call] = writer->argument_count;
        writer->call_counts[call] = count;
        writer->call_variadic[call] = 0;
        writer->call_result_objects[call] = 0;
        abi->first[abi->count] = abi->argument_count;
        abi->results[abi->count] = result;
        abi->counts[abi->count] = count;
        abi->named[abi->count] = count;
        abi->variadic[abi->count++] = 0;
        for (uint32_t i = 0; i < count; i += 1)
        {
            abi->arguments[abi->argument_count++] = arguments[i];
            writer->argument_sizes[writer->argument_count] = 0;
            writer->argument_classes[writer->argument_count++] = check_lower_class(writer, arguments[i]);
        }
    }
    return call;
}

// The SymbolId calls of runtime function `runtime` take (CheckProgramLowered.runtime_symbols): the symbol of the unit's
// own declaration of its name with linkage, where the program has one (the linker binds both alike), or else one of the
// lowering's own, which a writer doesn't number: its calls take a provisional symbol, the function's own past the
// helpers' (CheckLowerWriter.provisional_runtimes), and check_lower_finish numbers the functions in the order the bodies
// first call them, after those it made before, as source order's lowering would (#151: the parts' writers run on lanes).
static SymbolId check_lower_runtime(CheckLowerWriter* writer, CheckLowerRuntime runtime)
{
    SymbolId symbol = writer->declared_runtimes[runtime];
    if (symbol == 0)
    {
        symbol = writer->provisional_runtimes + (SymbolId)runtime;
        if ((writer->runtime_mask & (UINT64_C(1) << runtime)) == 0)
        {
            writer->runtime_mask |= UINT64_C(1) << runtime;
            writer->runtimes[writer->runtime_count++] = (uint8_t)runtime;
        }
    }
    return symbol;
}

// A call of runtime function `runtime` (check_lower_runtime), of `count` arguments of `arguments`' types and a result of
// `result`'s: its row of the calls table and its signature for abi_assign. A 128-bit integer argument is a 16-byte
// object's address, as Tier 0 takes one, and a 128-bit result comes back into frame object `object`, BUSTER_CHECK_NONE
// for any other result. Returns the call, BUSTER_CHECK_NONE for a failure.
static uint32_t check_lower_runtime_call(CheckLowerWriter* writer, CheckLowerRuntime runtime, TypeId result, const TypeId* arguments, uint32_t count, uint32_t object)
{
    CheckLowerAbi* abi = writer->abi;
    const SymbolId symbol = check_lower_runtime(writer, runtime);
    uint32_t call = BUSTER_CHECK_NONE;
    if (symbol != 0 && (abi->count >= abi->capacity || count > abi->argument_capacity - abi->argument_count))
    {
        check_lower_fail(writer, CHECK_PROGRAM_RESOURCE);
    }
    else if (symbol != 0)
    {
        call = writer->call_count++;
        writer->call_targets[call] = symbol;
        writer->call_first[call] = writer->argument_count;
        writer->call_counts[call] = count;
        writer->call_variadic[call] = 0;
        writer->call_result_objects[call] = object != BUSTER_CHECK_NONE ? object + 1 : 0;
        writer->aggregates |= object != BUSTER_CHECK_NONE;
        abi->first[abi->count] = abi->argument_count;
        abi->results[abi->count] = result;
        abi->counts[abi->count] = count;
        abi->named[abi->count] = count;
        abi->variadic[abi->count++] = 0;
        for (uint32_t i = 0; i < count; i += 1)
        {
            const bool wide = check_lower_wide(writer, arguments[i]);
            abi->arguments[abi->argument_count++] = arguments[i];
            writer->argument_sizes[writer->argument_count] = wide ? 16 : 0;
            writer->aggregates |= wide;
            writer->argument_classes[writer->argument_count++] = wide ? BUSTER_TYPED_CLASS_U64 : check_lower_class(writer, arguments[i]);
        }
    }
    return call;
}

// A readonly image and a fresh automatic object's exact range are disjoint. The ordinary call ABI preserves live
// values and handles external-symbol access; its ignored result needs no temporary or source-level declaration.
static void check_lower_initializer_copy(CheckLowerWriter* writer, uint32_t pool, uint32_t object, uint64_t offset)
{
    const CheckLowerInitializers* pools = &writer->output->initializers;
    const TypeId arguments[3] = { BUSTER_TYPE_ULONG, BUSTER_TYPE_ULONG, BUSTER_TYPE_ULONG };
    const uint32_t call = check_lower_runtime_call(writer, CHECK_LOWER_RUNTIME_MEMCPY, BUSTER_TYPE_VOID, arguments, 3, BUSTER_CHECK_NONE);
    check_lower_emit(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, object);
    check_lower_offset(writer, offset);
    check_lower_emit(writer, BUSTER_TYPED_GLOBAL_ADDRESS, BUSTER_TYPED_CLASS_U64, pools->symbol_base + pool);
    check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, pools->items[pool].size);
    check_lower_emit(writer, BUSTER_TYPED_CALL, BUSTER_TYPED_CLASS_VOID, call);
}

// An operation of `kind` on the two values on top of the stack, of `class`, as instructions pushed for after them: its
// entry, or for a 128-bit integer's division and remainder, which Tier 0 has none for, a call of libgcc's (__divti3,
// __udivti3, __modti3, __umodti3), as clang -O0 makes it. Its operands go into frame objects of their own, passed by their
// addresses, the first one's taking the result, which comes back from it.
static void check_lower_operation(CheckLowerWriter* writer, uint8_t kind, uint8_t class)
{
    if (check_lower_wide_class(class) && (kind == BUSTER_TYPED_DIV || kind == BUSTER_TYPED_MOD))
    {
        const bool is_signed = class == BUSTER_TYPED_CLASS_I128;
        const CheckLowerRuntime runtime = kind == BUSTER_TYPED_DIV ? (is_signed ? CHECK_LOWER_RUNTIME_DIVTI3 : CHECK_LOWER_RUNTIME_UDIVTI3)
                                                                   : (is_signed ? CHECK_LOWER_RUNTIME_MODTI3 : CHECK_LOWER_RUNTIME_UMODTI3);
        const TypeId type = is_signed ? BUSTER_TYPE_INT128 : BUSTER_TYPE_UINT128;
        const TypeId arguments[2] = { type, type };
        const uint32_t a = check_lower_temporary(writer, class);
        const uint32_t b = check_lower_temporary(writer, class);
        const uint32_t call = writer->output->status == CHECK_PROGRAM_OK ? check_lower_runtime_call(writer, runtime, type, arguments, 2, a) : BUSTER_CHECK_NONE;
        check_lower_instruction(writer, BUSTER_TYPED_LOCAL, class, a);
        check_lower_instruction(writer, BUSTER_TYPED_CALL, BUSTER_TYPED_CLASS_VOID, call);
        check_lower_instruction(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, b);
        check_lower_instruction(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, a);
        check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, class, a);
        check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, class, b);
    }
    else
    {
        check_lower_instruction(writer, kind, class, 0);
    }
}

// A shift's count, which Tier 0 takes of a class I8 to U64: a 128-bit one converted to U64 after it (a count of 128 or
// more is undefined, so its low 64 bits keep every defined one), as an instruction pushed before the count's operand.
static void check_lower_count(CheckLowerWriter* writer, uint8_t kind, TypeId count)
{
    if ((kind == BUSTER_TYPED_SHL || kind == BUSTER_TYPED_SHR) && check_lower_wide(writer, count))
    {
        check_lower_instruction(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
    }
}

static void check_lower_math(CheckLowerWriter* writer, uint32_t node, const CheckBuiltinFloatEffect* effect)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const CheckProgramHelpers* helpers = &writer->output->helpers;
    const uint32_t selected = check_program_builtin_index(helpers->uses, helpers->use_count, sizeof(CheckProgramHelperUse), node);
    const uint32_t count = graph->argument_counts[node];
    const uint32_t first = graph->argument_offsets[node];
    const uint8_t class = check_lower_class(writer, effect->floating_type);
    const uint32_t parameters = count + effect->may_set_errno;
    if (selected == BUSTER_CHECK_NONE || helpers->uses[selected].helper >= helpers->count || effect->unsupported != 0 ||
        (class != BUSTER_TYPED_CLASS_F32 && class != BUSTER_TYPED_CLASS_F64))
    {
        check_lower_unsupported(writer, "this math builtin's type");
    }
    else
    {
        const CheckProgramHelper* helper = helpers->items + helpers->uses[selected].helper;
        if (count == 0 || count > 2 || effect->evaluated_operands != (1u << count) - 1 ||
            helper->id != effect->runtime_builtin || helper->parameter_count != parameters || helper->result_type != BUSTER_TYPE_ULONG ||
            (effect->may_set_errno && (writer->output->errno_symbol == 0 || effect->errno_pointer_type != writer->output->errno_pointer_type)))
        {
            check_lower_fail(writer, CHECK_PROGRAM_INVALID);
        }
        else
        {
            const uint32_t call = check_lower_native_call(writer, helper->symbol, helper->result_type, helper->parameter_types, parameters);
            const uint32_t result = check_lower_addressed_temporary(writer, BUSTER_TYPED_CLASS_U64);
            // These loads reinterpret IEEE representation bits. Numeric FP/integer CONVERT is deliberately
            // absent at the helper boundary; only the checked source conversion precedes each bit transport.
            check_lower_instruction(writer, BUSTER_TYPED_LOAD, class, 0);
            check_lower_instruction(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, result);
            check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, result);
            check_lower_instruction(writer, BUSTER_TYPED_CALL, BUSTER_TYPED_CLASS_U64, call);
            if (effect->may_set_errno)
            {
                const uint32_t accessor = check_lower_native_call(writer, writer->output->errno_symbol, effect->errno_pointer_type, NULL, 0);
                check_lower_instruction(writer, BUSTER_TYPED_CALL, BUSTER_TYPED_CLASS_U64, accessor);
            }
            for (uint32_t i = count; i-- > 0 && writer->output->status == CHECK_PROGRAM_OK;)
            {
                const uint32_t local = check_lower_addressed_temporary(writer, class);
                if (class == BUSTER_TYPED_CLASS_F32)
                {
                    check_lower_instruction(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
                }
                check_lower_instruction(writer, BUSTER_TYPED_LOAD, class == BUSTER_TYPED_CLASS_F32 ? BUSTER_TYPED_CLASS_U32 : BUSTER_TYPED_CLASS_U64, 0);
                check_lower_instruction(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, local);
                check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, class, local);
                check_lower_operand(writer, node, i + 3, false, stream->argument_conversions[first + i].type);
            }
        }
    }
}

// Always inlined into both copies of the tasks' loop (check_lower_drain_body).
static inline __attribute__((always_inline)) void check_lower_terminate(CheckLowerWriter* writer)
{
    // Tier0 has ordinary calls and jumps. A terminal native helper traps; this self edge also prevents its
    // typed CFG from reaching the continuation. The validator checks unreachable expressions from an empty
    // stack, so recreate only their pending classes, with dead constants that no runtime path can execute.
    const uint32_t pending = writer->stack_head;
    for (uint32_t value = pending; value != 0; value = writer->stack_parents[value])
    {
        check_lower_emit(writer, BUSTER_TYPED_DISCARD, writer->stack_classes[value], 0);
    }
    const uint32_t label = check_lower_label(writer);
    check_lower_place(writer, label);
    check_lower_emit(writer, BUSTER_TYPED_JUMP, BUSTER_TYPED_CLASS_VOID, label);
    for (uint32_t value = pending; value != 0; value = writer->stack_parents[value])
    {
        check_lower_instruction(writer, BUSTER_TYPED_CONST, writer->stack_classes[value], 0);
    }
}

static void check_lower_update(CheckLowerWriter* writer, uint32_t node)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const CheckOp op = graph->ops[node];
    const uint32_t left = graph->operand0[node];
    const TypeId stored = stream->values[left].type;
    const TypeId calculated = stream->conversions[node].operands[0].type;
    const TypeId right_type = stream->conversions[node].operands[1].type;
    const uint8_t stored_class = check_lower_class(writer, stored);
    const uint8_t calculated_class = check_lower_class(writer, calculated);
    const bool increment = op >= CHECK_OP_PRE_INCREMENT && op <= CHECK_OP_POST_DECREMENT;
    // A vector's compound assignment is its operator's row (check_lower_vector_row) between the load and the store, its
    // right operand a scalar's splat where it isn't a vector, at the stored vector's type, as a shift's count is.
    const bool vector = stored_class == BUSTER_TYPED_CLASS_VECTOR;
    const uint32_t vector_op = op <= CHECK_OP_SHIFT_RIGHT_ASSIGN ? CHECK_OP_ADD + (uint32_t)(op - CHECK_OP_ADD_ASSIGN) : CHECK_OP_BIT_AND + (uint32_t)(op - CHECK_OP_BIT_AND_ASSIGN);
    if (vector && increment)
    {
        check_lower_unsupported(writer, "increments of vectors");
    }
    const bool postfix = op == CHECK_OP_POST_INCREMENT || op == CHECK_OP_POST_DECREMENT;
    const bool subtract = op == CHECK_OP_PRE_DECREMENT || op == CHECK_OP_POST_DECREMENT || op == CHECK_OP_SUBTRACT_ASSIGN;
    const bool pointer = (check_lower_facts(writer, stored) & BUSTER_CHECK_LOWER_TYPE_POINTER) != 0;
    const uint32_t object = graph->ops[left] == CHECK_OP_VALUE ? stream->values[left].object : 0;
    const uint32_t local = object != 0 && object < program->object_count ? writer->local_map[object] : BUSTER_CHECK_NONE;
    const SymbolId symbol = object != 0 && object < program->object_count ? program->object_symbols[object] : 0;
    // A global of a memory class goes through its address, as memory does (a local's object takes LOCAL and STORE_LOCAL),
    // and a postfix one's old value through a temporary, a frame object of its own.
    const bool memory = check_lower_memory_class(stored_class);
    const bool direct = local != BUSTER_CHECK_NONE || (!memory && symbol != 0);
    const uint8_t get = local != BUSTER_CHECK_NONE ? BUSTER_TYPED_LOCAL : BUSTER_TYPED_GLOBAL;
    const uint64_t destination = local != BUSTER_CHECK_NONE ? local : symbol;
    const uint8_t store = direct ? (postfix ? local != BUSTER_CHECK_NONE ? BUSTER_TYPED_SET_LOCAL : BUSTER_TYPED_SET_GLOBAL :
                                            local != BUSTER_CHECK_NONE ? BUSTER_TYPED_STORE_LOCAL : BUSTER_TYPED_STORE_GLOBAL) :
                                  postfix ? BUSTER_TYPED_SET : BUSTER_TYPED_STORE;
    uint32_t address_temporary = BUSTER_CHECK_NONE;
    uint32_t old_temporary = BUSTER_CHECK_NONE;
    if (postfix && !direct)
    {
        address_temporary = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
        old_temporary = check_lower_temporary(writer, stored_class);
    }
    check_lower_instruction(writer, store, stored_class, direct ? destination : check_lower_access_value(program->types, stored_class, stored));
    check_lower_convert(writer, calculated, stored);
    if (vector)
    {
        const bool scalar = check_lower_vector_bytes(program->types, stream->values[graph->operand1[node]].type) == 0;
        check_lower_vector_half_chain(writer, calculated, vector_op, BUSTER_CHECK_NONE, graph->operand1[node]);
        check_lower_vector_row(writer, vector_op, calculated, 0);
        check_lower_operand(writer, node, 1, false, scalar ? calculated : right_type);
    }
    else if (pointer)
    {
        check_lower_scale(writer, stored, check_lower_stride(writer, stored));
        if (subtract)
        {
            check_lower_instruction(writer, BUSTER_TYPED_NEG, BUSTER_TYPED_CLASS_I64, 0);
        }
        if (increment)
        {
            check_lower_instruction(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 1);
        }
        else
        {
            check_lower_operand(writer, node, 1, false, BUSTER_TYPE_LONG);
        }
    }
    else
    {
        const uint8_t kind = increment ? subtract ? BUSTER_TYPED_SUB : BUSTER_TYPED_ADD : check_lower_binary(op);
        check_lower_operation(writer, kind, calculated_class);
        if (!increment)
        {
            check_lower_count(writer, kind, right_type);
        }
        if (increment)
        {
            uint64_t one = 1;
            if (calculated_class == BUSTER_TYPED_CLASS_F32)
            {
                one = UINT32_C(0x3f800000);
            }
            else if (calculated_class == BUSTER_TYPED_CLASS_F64)
            {
                one = UINT64_C(0x3ff0000000000000);
            }
            else if (calculated_class == BUSTER_TYPED_CLASS_F80)
            {
                one = check_lower_x87_constant(writer, 1.0L);
            }
            check_lower_instruction(writer, BUSTER_TYPED_CONST, calculated_class, one);
        }
        else
        {
            check_lower_operand(writer, node, 1, false, right_type);
        }
    }
    check_lower_convert(writer, stored, calculated);
    if (direct)
    {
        if (postfix)
        {
            check_lower_instruction(writer, BUSTER_TYPED_DUP, stored_class, 0);
        }
        check_lower_instruction(writer, get, stored_class, destination);
    }
    else if (!postfix)
    {
        check_lower_instruction(writer, BUSTER_TYPED_LOAD, stored_class, check_lower_access_value(program->types, stored_class, stored));
        check_lower_instruction(writer, BUSTER_TYPED_DUP, BUSTER_TYPED_CLASS_U64, 0);
        check_lower_operand(writer, node, 0, true, 0);
    }
    else
    {
        check_lower_instruction(writer, BUSTER_TYPED_LOCAL, stored_class, old_temporary);
        check_lower_instruction(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, address_temporary);
        check_lower_instruction(writer, BUSTER_TYPED_STORE_LOCAL, stored_class, old_temporary);
        check_lower_instruction(writer, BUSTER_TYPED_LOAD, stored_class, 0);
        check_lower_instruction(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, address_temporary);
        check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, address_temporary);
        check_lower_operand(writer, node, 0, true, 0);
    }
}

// Whether operation `op` of an expression of `result` computes on complex values (check_lower_complex_compute): an
// arithmetic one whose result is complex, or an equality whose operands are (`operand` the first one's converted type).
static bool check_lower_complex_operation(const CheckLowerWriter* writer, CheckOp op, TypeId result, TypeId operand)
{
    const bool arithmetic = op == CHECK_OP_ADD || op == CHECK_OP_SUBTRACT || op == CHECK_OP_MULTIPLY || op == CHECK_OP_DIVIDE || op == CHECK_OP_NEGATIVE ||
                            op == CHECK_OP_BIT_NOT;
    const bool equality = op == CHECK_OP_EQUAL || op == CHECK_OP_NOT_EQUAL;
    return (arithmetic && check_lower_complex(writer, result)) || (equality && check_lower_complex(writer, operand));
}

// The type an operand of a complex operation is evaluated to, from its own type and the computation's (`converted`, the
// checker's conversion of it): a complex one to the computation's type, a real one to its element, keeping its domain.
static TypeId check_lower_complex_target(const CheckLowerWriter* writer, TypeId own, TypeId converted)
{
    return check_lower_complex(writer, own) || !check_lower_complex(writer, converted) ? converted : check_lower_complex_element(writer->program->types, converted);
}

// `destination op= value` where either is complex (check_lower_complex_update_emit, once the destination's address and
// the value are evaluated): of a sum, a difference, a product or a quotient.
static void check_lower_complex_update(CheckLowerWriter* writer, uint32_t node)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckOp op = graph->ops[node];
    if (op != CHECK_OP_ADD_ASSIGN && op != CHECK_OP_SUBTRACT_ASSIGN && op != CHECK_OP_MULTIPLY_ASSIGN && op != CHECK_OP_DIVIDE_ASSIGN)
    {
        check_lower_unsupported(writer, "increments of complex values");
    }
    else
    {
        // The destination's address and the value each into a temporary as it is evaluated (check_lower_expand's complex
        // operations say why).
        const TypeTable* types = program->types;
        const TypeId own = program->stream->values[graph->operand1[node]].type;
        const TypeId calculated = program->stream->conversions[node].operands[0].type;
        const uint8_t class = check_lower_complex(writer, own) ? BUSTER_TYPED_CLASS_U64 : check_lower_class(writer, check_lower_complex_element(types, calculated));
        const uint32_t value = check_lower_temporary(writer, class);
        const uint32_t destination = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
        check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_COMPLEX, .node = node, .value = value | (uint64_t)destination << 32, .address = true });
        check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, class, value);
        check_lower_operand(writer, node, 1, false, check_lower_complex_target(writer, own, program->stream->conversions[node].operands[1].type));
        check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, destination);
        check_lower_operand(writer, node, 0, true, 0);
    }
}

// Expression `node`'s operation on complex values, its operands in temporaries `locals` (the first's in the low 32
// bits): computed in the operands' converted type (check_lower_complex_compute), leaving its result's address or an
// equality's I32.
static void check_lower_complex_emit(CheckLowerWriter* writer, uint32_t node, uint64_t locals)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const CheckOp op = graph->ops[node];
    const bool unary = op == CHECK_OP_NEGATIVE || op == CHECK_OP_BIT_NOT;
    const TypeId type = stream->conversions[node].operands[0].type;
    const CheckLowerComplexOperand first = { .local = (uint32_t)locals, .complex = check_lower_complex(writer, stream->values[graph->operand0[node]].type) };
    const CheckLowerComplexOperand last = unary ? first : (CheckLowerComplexOperand){ .local = (uint32_t)(locals >> 32), .complex = check_lower_complex(writer, stream->values[graph->operand1[node]].type) };
    const uint32_t result = check_lower_complex_compute(writer, op, type, first, last);
    if (result != BUSTER_CHECK_NONE)
    {
        check_lower_emit(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, result);
    }
}

// `destination op= value` (check_lower_complex_update), the value and the destination's address in temporaries `locals`
// (the value's in the low 32 bits): the destination read in the calculation's type, the result computed
// (check_lower_complex_compute) and stored back, converted: a complex destination's parts, whose address is the
// expression's value, or a real one's value (a _Bool one's the result's truth), which STORE leaves.
static void check_lower_complex_update_emit(CheckLowerWriter* writer, uint32_t node, uint64_t locals)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const TypeTable* types = program->types;
    const CheckOp op = graph->ops[node];
    const CheckOp computed = op == CHECK_OP_ADD_ASSIGN ? CHECK_OP_ADD : op == CHECK_OP_SUBTRACT_ASSIGN ? CHECK_OP_SUBTRACT :
                             op == CHECK_OP_MULTIPLY_ASSIGN ? CHECK_OP_MULTIPLY : CHECK_OP_DIVIDE;
    const TypeId stored = stream->values[graph->operand0[node]].type;
    const TypeId calculated = stream->conversions[node].operands[0].type;
    const TypeId element = check_lower_complex_element(types, calculated);
    const uint8_t class = check_lower_class(writer, element);
    const bool stored_complex = check_lower_complex(writer, stored);
    const bool value_complex = check_lower_complex(writer, stream->values[graph->operand1[node]].type);
    const uint8_t current_class = stored_complex ? BUSTER_TYPED_CLASS_U64 : class;
    const CheckLowerComplexOperand value = { .local = (uint32_t)locals, .complex = value_complex };
    const uint32_t destination = (uint32_t)(locals >> 32);
    const CheckLowerComplexOperand current = { .local = check_lower_temporary(writer, current_class), .complex = stored_complex };
    check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, destination);
    if (stored_complex)
    {
        check_lower_complex_conversion(writer, stored, calculated);
    }
    else
    {
        check_lower_emit(writer, BUSTER_TYPED_LOAD, check_lower_class(writer, stored), 0);
        check_lower_scalar_conversion(writer, stored, element);
    }
    check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, current_class, current.local);
    const uint32_t result = check_lower_complex_compute(writer, computed, calculated, current, value);
    const TypeId stored_element = stored_complex ? check_lower_complex_element(types, stored) : stored;
    for (uint32_t part = 0; stored_complex && result != BUSTER_CHECK_NONE && part < 2; part += 1)
    {
        check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, destination);
        check_lower_offset(writer, part * type_size(types, stored_element));
        check_lower_complex_place(writer, result, part, type_size(types, element));
        check_lower_emit(writer, BUSTER_TYPED_LOAD, class, 0);
        check_lower_scalar_conversion(writer, element, stored_element);
        check_lower_emit(writer, BUSTER_TYPED_SET, check_lower_class(writer, stored_element), 0);
    }
    if (stored_complex)
    {
        check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, destination);
    }
    else if (result != BUSTER_CHECK_NONE)
    {
        check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, destination);
        check_lower_emit(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, result);
        check_lower_complex_conversion(writer, calculated, stored);
        check_lower_emit(writer, BUSTER_TYPED_STORE, check_lower_class(writer, stored), 0);
    }
}

// A complex values' task (CHECK_LOWER_COMPLEX or CHECK_LOWER_COMPLEX_TRUTH), out of the tasks' loop: few functions
// have them.
static __attribute__((noinline)) void check_lower_complex_task(CheckLowerWriter* writer, CheckLowerTask task)
{
    if (task.kind == CHECK_LOWER_COMPLEX_TRUTH)
    {
        check_lower_complex_truth(writer, task.type, task.address);
    }
    else if (task.address)
    {
        check_lower_complex_update_emit(writer, task.node, task.value);
    }
    else
    {
        check_lower_complex_emit(writer, task.node, task.value);
    }
}

// `destination = value` on a bit-field: the address of the field's first byte in a temporary, then the value converted
// to the field's type, stored by check_lower_bit_store, whose value is the field's as it now reads.
static void check_lower_bit_assign(CheckLowerWriter* writer, uint32_t node, bool discard)
{
    const CheckProgram* program = writer->program;
    const uint32_t destination = check_lower_bit_destination(program, program->input->graph.operand0[node]);
    CheckLowerBitField field = { 0 };
    if (destination == BUSTER_CHECK_NONE || !check_lower_bit_member(program, destination, &field))
    {
        check_lower_unsupported(writer, "bit-fields of this type or spread over more than 8 bytes");
    }
    else
    {
        const uint32_t address = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
        check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_BIT_STORE, .type = check_lower_bit_pack(&field), .value = address, .discard = discard });
        check_lower_operand(writer, node, 1, false, program->stream->values[node].type);
        check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, address);
        check_lower_operand(writer, node, 0, true, 0);
    }
}

// ++, -- and op= on a bit-field, as check_lower_update does them on other lvalues: the address of the field's first
// byte in a temporary, the field read, converted, operated on and converted back, then stored by check_lower_bit_store.
// A postfix one's value is the field as it was, kept in a temporary.
static void check_lower_bit_update(CheckLowerWriter* writer, uint32_t node, bool discard)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const CheckOp op = graph->ops[node];
    const TypeId stored = stream->values[graph->operand0[node]].type;
    const TypeId calculated = stream->conversions[node].operands[0].type;
    const uint8_t calculated_class = check_lower_class(writer, calculated);
    const bool increment = op >= CHECK_OP_PRE_INCREMENT && op <= CHECK_OP_POST_DECREMENT;
    const bool postfix = op == CHECK_OP_POST_INCREMENT || op == CHECK_OP_POST_DECREMENT;
    const bool subtract = op == CHECK_OP_PRE_DECREMENT || op == CHECK_OP_POST_DECREMENT;
    const uint32_t destination = check_lower_bit_destination(program, graph->operand0[node]);
    CheckLowerBitField field = { 0 };
    if (destination == BUSTER_CHECK_NONE || !check_lower_bit_member(program, destination, &field) || calculated_class >= BUSTER_TYPED_CLASSES)
    {
        check_lower_unsupported(writer, "bit-fields of this type or spread over more than 8 bytes");
    }
    else
    {
        const uint32_t address = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
        const uint32_t old = postfix && !discard ? check_lower_temporary(writer, field.class) : BUSTER_CHECK_NONE;
        const uint32_t packed = check_lower_bit_pack(&field);
        if (old != BUSTER_CHECK_NONE)
        {
            check_lower_instruction(writer, BUSTER_TYPED_LOCAL, field.class, old);
        }
        check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_BIT_STORE, .type = packed, .value = address, .discard = postfix || discard });
        check_lower_convert(writer, calculated, stored);
        check_lower_instruction(writer, increment ? subtract ? BUSTER_TYPED_SUB : BUSTER_TYPED_ADD : check_lower_binary(op), calculated_class, 0);
        if (increment)
        {
            check_lower_instruction(writer, BUSTER_TYPED_CONST, calculated_class, 1);
        }
        else
        {
            check_lower_operand(writer, node, 1, false, stream->conversions[node].operands[1].type);
        }
        check_lower_convert(writer, stored, calculated);
        if (old != BUSTER_CHECK_NONE)
        {
            check_lower_instruction(writer, BUSTER_TYPED_STORE_LOCAL, field.class, old);
        }
        check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_BIT_READ, .type = packed });
        check_lower_instruction(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, address);
        check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, address);
        check_lower_operand(writer, node, 0, true, 0);
    }
}

static void check_lower_initializer(CheckLowerWriter* writer, CheckControl control);
static void check_lower_control(CheckLowerWriter* writer, uint32_t index);

// An integer type's descriptor for the builtins' recipes: its width (a 128-bit one's BUSTER_CBUILTIN_LOWER_WIDE) and
// whether it is signed.
static uint32_t check_lower_builtin_type(const TypeTable* types, TypeId type)
{
    uint32_t width = 0;
    bool sign = false;
    const bool integer = check_action_integer_type(types, type, &width, &sign);
    const uint32_t result = integer ? (width == 128 ? BUSTER_CBUILTIN_LOWER_WIDE : width) | (sign ? BUSTER_CBUILTIN_SIGNED : 0) : 0;
    return result;
}

static void check_lower_builtin_prepare(CheckLowerWriter* writer, uint32_t node)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckNodeConversions* conversions = program->stream->conversions + node;
    const bool overflow = graph->ops[node] == CHECK_OP_BUILTIN_OVERFLOW;
    const uint32_t depth = writer->builtin_depth++;
    if (depth == writer->builtin_levels)
    {
        memset(writer->builtin_operands + depth, 0xff, sizeof(CheckLowerBuiltinOperands));
        writer->builtin_levels += 1;
    }
    const uint32_t index = writer->builtin_count++;
    CBuiltinLowerCall* call = writer->builtins + index;
    *call = (CBuiltinLowerCall){ .id = (CBuiltinId)graph->members[node] };
    if (overflow)
    {
        const CheckStream* stream = program->stream;
        const uint32_t selected = check_program_builtin_index(stream->effects, stream->effect_count, sizeof(CheckStoreEffect), node);
        if (selected == BUSTER_CHECK_NONE)
        {
            check_lower_fail(writer, CHECK_PROGRAM_INVALID);
        }
        else if (stream->effects[selected].unsupported != 0 || stream->effects[selected].evaluated_operands != 7)
        {
            check_lower_unsupported(writer, "this overflow builtin");
        }
        else
        {
            call->id = stream->effects[selected].runtime_builtin;
        }
    }
    const uint32_t count = overflow ? 3 : 1;
    uint32_t locals[3] = { 0 };
    for (uint32_t i = 0; i < count; i += 1)
    {
        // The recipes take values of the register classes, and an overflow builtin's 128-bit operands in frame objects
        // of their own (check_lower_temporary's).
        const TypeId type = conversions->operands[i].type;
        if (check_lower_memory(writer, type) && !(overflow && check_lower_wide(writer, type)))
        {
            check_lower_unsupported(writer, "builtins of long double and __int128 operands");
        }
        const uint8_t class = check_lower_class(writer, type);
        if (class < BUSTER_TYPED_CLASSES && class != BUSTER_TYPED_CLASS_VOID)
        {
            uint32_t* local = &writer->builtin_operands[depth].locals[i][class];
            if (*local == BUSTER_CHECK_NONE)
            {
                *local = check_lower_temporary(writer, class);
            }
            locals[i] = *local;
        }
        else
        {
            check_lower_unsupported(writer, "builtins of this operand type");
        }
    }
    call->a = locals[0];
    call->b = locals[1];
    call->destination = locals[2];
    call->a_type = check_lower_builtin_type(program->types, conversions->operands[0].type);
    call->b_type = overflow ? check_lower_builtin_type(program->types, conversions->operands[1].type) : 0;
    call->result_type = overflow ? check_lower_builtin_type(program->types, conversions->store.type) : 0;
    check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_BUILTIN, .node = index, .type = program->stream->values[node].type });
    for (uint32_t i = count; i-- > 0 && writer->output->status == CHECK_PROGRAM_OK;)
    {
        // A 128-bit operand's object is a local of class VOID that SET_LOCAL of its value's class writes.
        check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, writer->value_classes[locals[i]], locals[i]);
        check_lower_operand(writer, node, i, false, conversions->operands[i].type);
    }
}

static void check_lower_builtin_finish(CheckLowerWriter* writer, const CBuiltinLowerBuffer* buffer, CBuiltinLowerResult result, TypeId type)
{
    if (result.error == CBUILTIN_LOWER_UNSUPPORTED)
    {
        check_lower_unsupported(writer, "this builtin");
    }
    else if (result.error != CBUILTIN_LOWER_OK)
    {
        if (result.error == CBUILTIN_LOWER_CAPACITY && result.limit != CBUILTIN_LOWER_LIMIT_NONE)
        {
            const char* reason = result.limit == CBUILTIN_LOWER_LIMIT_RECIPE ? "builtin recipe limit" :
                                 result.limit == CBUILTIN_LOWER_LIMIT_ENTRIES ?
                                     (buffer->capacity == BUSTER_TIER0_MAX_ENTRIES ? "Tier 0 per-function entry limit" : "lowering entry storage") :
                                     (buffer->local_capacity == BUSTER_TIER0_MAX_LOCALS ? "Tier 0 per-function local limit" : "lowering local storage");
            check_lower_resource(writer, reason);
        }
        else
        {
            check_lower_fail(writer, result.error == CBUILTIN_LOWER_CAPACITY ? CHECK_PROGRAM_RESOURCE : CHECK_PROGRAM_INVALID);
        }
    }
    else
    {
        uint32_t labels[BUSTER_CBUILTIN_LOWER_ENTRIES + 1];
        memset(labels, 0xff, ((uint64_t)result.count + 1) * sizeof(uint32_t));
        const uint32_t first = writer->count;
        // The recipe's temporaries are values, not frame objects or variable-length arrays (the buffer writes only their
        // classes and flags).
        memset(writer->local_sizes + writer->local_count, 0, ((uint64_t)buffer->local_count - writer->local_count) * sizeof(uint32_t));
        memset(writer->local_alignments + writer->local_count, 0, (uint64_t)buffer->local_count - writer->local_count);
        memset(writer->indirect + writer->local_count, BUSTER_CHECK_LOWER_VLA_NONE, (uint64_t)buffer->local_count - writer->local_count);
        writer->local_count = buffer->local_count;
        // External builtin recipes use absolute typed-entry offsets. Convert only their local branches into
        // this writer's symbolic labels before the ordinary whole-function patch, retaining every branch join.
        for (uint32_t i = first; i < buffer->count && writer->output->status == CHECK_PROGRAM_OK; i += 1)
        {
            if (writer->kinds[i] >= BUSTER_TYPED_BRANCH_FALSE && writer->kinds[i] <= BUSTER_TYPED_JUMP_VALUE)
            {
                const uint64_t target = writer->values[i];
                if (target < first || target > buffer->count)
                {
                    check_lower_fail(writer, CHECK_PROGRAM_INVALID);
                }
                else
                {
                    const uint32_t offset = (uint32_t)target - first;
                    if (labels[offset] == BUSTER_CHECK_NONE)
                    {
                        labels[offset] = check_lower_label(writer);
                    }
                    writer->values[i] = labels[offset];
                }
            }
        }
        while (writer->count < buffer->count && writer->output->status == CHECK_PROGRAM_OK)
        {
            const uint32_t i = writer->count;
            if (labels[i - first] != BUSTER_CHECK_NONE)
            {
                check_lower_place(writer, labels[i - first]);
            }
            if (writer->track)
            {
                check_lower_track(writer, writer->kinds[i], writer->classes[i], writer->values[i]);
            }
            writer->count += 1;
        }
        if (labels[result.count] != BUSTER_CHECK_NONE)
        {
            check_lower_place(writer, labels[result.count]);
        }
        const uint8_t class = check_lower_value_class(writer, type);
        if (class != result.result_class)
        {
            check_lower_emit(writer, BUSTER_TYPED_CONVERT, class, 0);
        }
    }
}

static void check_lower_builtin_emit(CheckLowerWriter* writer, CheckLowerTask task)
{
    CBuiltinLowerBuffer buffer = {
        .kinds = writer->kinds, .classes = writer->classes, .values = writer->values,
        .count = writer->count, .capacity = writer->capacity < BUSTER_TIER0_MAX_ENTRIES ? writer->capacity : BUSTER_TIER0_MAX_ENTRIES,
        .local_classes = writer->local_classes, .local_flags = writer->local_flags, .local_count = writer->local_count,
        .local_capacity = writer->local_capacity < BUSTER_TIER0_MAX_LOCALS ? writer->local_capacity : BUSTER_TIER0_MAX_LOCALS,
        .scratch = &writer->builtin_scratch,
    };
    const CBuiltinLowerResult result = BUSTER_SCALAR_PATH(writer->scalar) ? cbuiltin_lower_scalar(&buffer, writer->builtins + task.node) : cbuiltin_lower(&buffer, writer->builtins + task.node);
    check_lower_builtin_finish(writer, &buffer, result, task.type);
    writer->builtin_depth -= 1;
}

// A variadic definition's incoming frame anchors, made at its first va_start: address-only locals whose addresses are
// the register save area's and the overflow area's (Tier0Function.incoming_variadic).
static void check_lower_anchors(CheckLowerWriter* writer)
{
    if (writer->save_anchor == BUSTER_CHECK_NONE)
    {
        writer->save_anchor = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
        writer->overflow_anchor = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
        if (writer->output->status == CHECK_PROGRAM_OK)
        {
            writer->local_flags[writer->save_anchor] = BUSTER_TYPED_LOCAL_ADDRESS_TAKEN | BUSTER_TYPED_LOCAL_REGISTER_SAVE_AREA;
            writer->local_flags[writer->overflow_anchor] = BUSTER_TYPED_LOCAL_ADDRESS_TAKEN | BUSTER_TYPED_LOCAL_OVERFLOW_ARG_AREA;
        }
    }
}

// va_start, va_arg, va_copy and va_end (#137), by c_builtin_varargs_lower.c's recipes: each list operand's value (a
// va_list's address) evaluated once into a local of its own, va_start's anchors' addresses into two more, then the
// recipe. va_start's offsets are the function's own (CheckLowerWriter.gp_offset), and va_arg's plan abi_va_arg's.
static void check_lower_varargs_prepare(CheckLowerWriter* writer, uint32_t node)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const TypeTable* types = program->types;
    const uint32_t effect = graph->argument_offsets[node];
    const CBuiltinId id = (CBuiltinId)graph->members[node];
    const bool start = id == CBUILTIN_VA_START || id == CBUILTIN_C23_VA_START;
    const bool argument = id == CBUILTIN_VA_ARG;
    const bool copy = id == CBUILTIN_VA_COPY;
    const TypeId requested = effect < stream->varargs_count ? stream->varargs[effect].requested_type : 0;
    if (effect >= stream->varargs_count || stream->varargs[effect].node != node || (start && !writer->variadic))
    {
        check_lower_fail(writer, CHECK_PROGRAM_INVALID);
    }
    else if (argument && check_lower_passed(writer, requested) != NULL)
    {
        check_lower_unsupported(writer, check_lower_passed(writer, requested));
    }
    else
    {
        const uint32_t index = writer->varargs_count++;
        CBuiltinVaLowerCall* call = writer->varargs + index;
        *call = (CBuiltinVaLowerCall){ .id = id };
        call->list = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
        if (start)
        {
            check_lower_anchors(writer);
            call->register_save_area = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
            call->overflow_arg_area = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
            call->gp_offset = writer->gp_offset;
            call->fp_offset = writer->fp_offset;
        }
        // A 128-bit integer comes as a struct of two eightbytes does, its value loaded from their address.
        const bool wide = argument && check_lower_wide(writer, requested);
        if (argument)
        {
            // A struct or union's bytes are copied from the save area into a frame object of its own (with room for
            // whole eightbytes), or read in the overflow area; its value is their address.
            const bool aggregate = check_lower_aggregate(writer, requested);
            const uint64_t size = aggregate || wide ? type_size(types, requested) : 0;
            call->plan = abi_va_arg(writer->abi_table, types, requested);
            call->result_class = wide ? BUSTER_TYPED_CLASS_U64 : check_lower_value_class(writer, requested);
            // (An empty one, which the call ignores, still names its object: 1 byte, as its frame object takes.)
            call->size = aggregate && size == 0 ? 1 : (uint32_t)size;
            call->object = aggregate || wide ? check_lower_object(writer, (size + 7) & ~UINT64_C(7), type_alignment_log2(types, requested)) : 0;
        }
        if (wide)
        {
            check_lower_instruction(writer, BUSTER_TYPED_LOAD, check_lower_class(writer, requested), 0);
        }
        check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_VARARGS, .node = index, .type = wide ? BUSTER_TYPE_ULONG : stream->values[node].type });
        if (start)
        {
            check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, call->overflow_arg_area);
            check_lower_instruction(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, writer->overflow_anchor);
            check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, call->register_save_area);
            check_lower_instruction(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, writer->save_anchor);
        }
        if (copy)
        {
            call->source = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
            check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, call->source);
            check_lower_operand(writer, node, 1, false, stream->conversions[node].operands[1].type);
        }
        check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, call->list);
        check_lower_operand(writer, node, 0, false, stream->conversions[node].operands[0].type);
    }
}

static void check_lower_varargs_emit(CheckLowerWriter* writer, CheckLowerTask task)
{
    CBuiltinLowerBuffer buffer = {
        .kinds = writer->kinds, .classes = writer->classes, .values = writer->values,
        .count = writer->count, .capacity = writer->capacity < BUSTER_TIER0_MAX_ENTRIES ? writer->capacity : BUSTER_TIER0_MAX_ENTRIES,
        .local_classes = writer->local_classes, .local_flags = writer->local_flags, .local_count = writer->local_count,
        .local_capacity = writer->local_capacity < BUSTER_TIER0_MAX_LOCALS ? writer->local_capacity : BUSTER_TIER0_MAX_LOCALS,
    };
    const CBuiltinVaLowerCall* call = writer->varargs + task.node;
    const CBuiltinLowerResult result = BUSTER_SCALAR_PATH(writer->scalar) ? cbuiltin_varargs_lower_scalar(&buffer, call) : cbuiltin_varargs_lower(&buffer, call);
    check_lower_builtin_finish(writer, &buffer, result, task.type);
}

static void check_lower_miscellaneous_prepare(CheckLowerWriter* writer, uint32_t node)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const uint32_t selected = check_program_builtin_index(stream->misc, stream->misc_count, sizeof(CheckBuiltinMiscEffect), node);
    const uint32_t count = graph->argument_counts[node];
    const uint32_t first = graph->argument_offsets[node];
    // A frame builtin's level is a constant of its own, which nothing evaluates (CheckBuiltinMiscEffect.frame_level): its
    // recipe takes no arguments. Every other builtin's operands are evaluated in order, each into a temporary.
    const CBuiltinId runtime = selected != BUSTER_CHECK_NONE ? stream->misc[selected].runtime_builtin : CBUILTIN_NONE;
    const bool frame = runtime == CBUILTIN_FRAME_ADDRESS || runtime == CBUILTIN_RETURN_ADDRESS;
    const uint32_t evaluated = frame ? 0 : count;
    if (selected == BUSTER_CHECK_NONE)
    {
        check_lower_fail(writer, CHECK_PROGRAM_INVALID);
    }
    else if (count > 3 || stream->misc[selected].evaluated_operands != (1u << evaluated) - 1)
    {
        check_lower_unsupported(writer, "this builtin");
    }
    else if (frame && stream->misc[selected].frame_level != 0)
    {
        // A caller's frame, which clang -O0's code finds through the frame pointers' chain: Tier 0's functions keep no
        // frame pointer but where they need one.
        check_lower_unsupported(writer, "__builtin_frame_address and __builtin_return_address of a caller's frame");
    }
    else
    {
        const uint32_t index = writer->miscellaneous_count++;
        CBuiltinMiscLowerCall* call = writer->miscellaneous + index;
        *call = (CBuiltinMiscLowerCall){ .id = runtime, .argument_count = evaluated };
        if (count == 3 && (call->id == CBUILTIN_MEMCPY || call->id == CBUILTIN_MEMMOVE || call->id == CBUILTIN_MEMSET || call->id == CBUILTIN_MEMCMP))
        {
            const uint32_t size_node = graph->argument_nodes[first + 2];
            const CheckOp size_op = graph->ops[size_node];
            const CheckValue* size = stream->values + size_node;
            // Match ordinary constant emission's proof: a retained folded value is not a runtime size when a
            // floating environment or evaluation effect still matters. Argument evaluation remains below.
            if (size_op != CHECK_OP_BUILTIN_QUERY && size_op != CHECK_OP_STATEMENT_VALUE && size_op != CHECK_OP_INITIALIZE &&
                size->category == CHECK_RVALUE && size->constant_kind == CHECK_CONSTANT_INTEGER && size->fault == CHECK_FAULT_NONE &&
                (size->flags & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_FP_ENVIRONMENT)) == 0 && (uint64_t)size->integer <= UINT32_MAX)
            {
                call->constant_size_known = true;
                call->constant_size = (uint32_t)(uint64_t)size->integer;
            }
        }
        for (uint32_t i = 0; i < evaluated; i += 1)
        {
            const TypeId type = stream->argument_conversions[first + i].type;
            if (check_lower_memory(writer, type))
            {
                check_lower_unsupported(writer, "builtins of long double and __int128 operands");
            }
            call->arguments[i] = check_lower_temporary(writer, check_lower_class(writer, type));
        }
        check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_BUILTIN_MISC, .node = index, .type = stream->values[node].type });
        for (uint32_t i = evaluated; i-- > 0 && writer->output->status == CHECK_PROGRAM_OK;)
        {
            const uint32_t local = call->arguments[i];
            check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, writer->local_classes[local], local);
            check_lower_operand(writer, node, i + 3, false, stream->argument_conversions[first + i].type);
        }
    }
}

static void check_lower_miscellaneous_emit(CheckLowerWriter* writer, CheckLowerTask task)
{
    CBuiltinLowerBuffer buffer = {
        .kinds = writer->kinds, .classes = writer->classes, .values = writer->values,
        .count = writer->count, .capacity = writer->capacity < BUSTER_TIER0_MAX_ENTRIES ? writer->capacity : BUSTER_TIER0_MAX_ENTRIES,
        .local_classes = writer->local_classes, .local_flags = writer->local_flags, .local_count = writer->local_count,
        .local_capacity = writer->local_capacity < BUSTER_TIER0_MAX_LOCALS ? writer->local_capacity : BUSTER_TIER0_MAX_LOCALS,
    };
    const CBuiltinMiscLowerCall* call = writer->miscellaneous + task.node;
    const CBuiltinLowerResult result = BUSTER_SCALAR_PATH(writer->scalar) ? cbuiltin_misc_lower_scalar(&buffer, call) : cbuiltin_misc_lower(&buffer, call);
    check_lower_builtin_finish(writer, &buffer, result, task.type);
}

static void check_lower_floating_recipe(CheckLowerWriter* writer, const CBuiltinFloatLowerCall* call, TypeId type)
{
    CBuiltinLowerBuffer buffer = {
        .kinds = writer->kinds, .classes = writer->classes, .values = writer->values,
        .count = writer->count, .capacity = writer->capacity < BUSTER_TIER0_MAX_ENTRIES ? writer->capacity : BUSTER_TIER0_MAX_ENTRIES,
        .local_classes = writer->local_classes, .local_flags = writer->local_flags, .local_count = writer->local_count,
        .local_capacity = writer->local_capacity < BUSTER_TIER0_MAX_LOCALS ? writer->local_capacity : BUSTER_TIER0_MAX_LOCALS,
    };
    const CBuiltinLowerResult result = BUSTER_SCALAR_PATH(writer->scalar) ? cbuiltin_float_lower_scalar(&buffer, call) : cbuiltin_float_lower(&buffer, call);
    check_lower_builtin_finish(writer, &buffer, result, type);
}

// A classification that FENV_ACCESS makes strict: fpclassify's conditional classifier, and isinf_sign's compare before
// its recipe. Out of line, as few tasks are, so that the tasks' loop holds none of its code (check_lower_floating_emit).
static __attribute__((noinline)) void check_lower_floating_strict(CheckLowerWriter* writer, CheckLowerTask task)
{
    const CBuiltinFloatLowerCall* call = writer->floating + task.node;
    const uint8_t class = (uint8_t)call->argument_class;
    const bool narrow = class == BUSTER_TYPED_CLASS_F32;
    const bool x87 = class == BUSTER_TYPED_CLASS_F80;
    const TypeId floating = narrow ? BUSTER_TYPE_FLOAT : x87 ? BUSTER_TYPE_LONG_DOUBLE : BUSTER_TYPE_DOUBLE;
    const CBuiltinFloatLowerCall absolute = {
        .id = narrow ? CBUILTIN_FABSF : x87 ? CBUILTIN_FABSL : CBUILTIN_FABS,
        .argument_local = call->argument_local,
        .argument_class = class,
    };
    if (call->id == CBUILTIN_FPCLASSIFY)
    {
        // Match the checked conditional classifier: zero, unordered, infinity, then normal magnitude. Quiet
        // comparisons retain signaling-NaN exceptions and the machine's DAZ treatment without changing MXCSR; a long
        // double's are the x87's (fucomip), and its fabsl clears the argument's sign, which nothing reads after it.
        const uint64_t infinity = check_lower_float_bits(writer, class, __builtin_infl());
        const uint64_t smallest = check_lower_float_bits(writer, class, narrow ? 0x1p-126L : x87 ? 0x1p-16382L : 0x1p-1022L);
        const uint32_t zero = check_lower_label(writer);
        const uint32_t nan = check_lower_label(writer);
        const uint32_t infinite = check_lower_label(writer);
        const uint32_t subnormal = check_lower_label(writer);
        const uint32_t end = check_lower_label(writer);
        const uint32_t magnitude = check_lower_temporary(writer, class);
        check_lower_emit(writer, BUSTER_TYPED_LOCAL, class, call->argument_local);
        check_lower_emit(writer, BUSTER_TYPED_CONST, class, 0);
        check_lower_emit(writer, BUSTER_TYPED_EQ, BUSTER_TYPED_CLASS_I32, 0);
        check_lower_emit(writer, BUSTER_TYPED_BRANCH_TRUE, BUSTER_TYPED_CLASS_I32, zero);
        check_lower_emit(writer, BUSTER_TYPED_LOCAL, class, call->argument_local);
        check_lower_emit(writer, BUSTER_TYPED_LOCAL, class, call->argument_local);
        check_lower_emit(writer, BUSTER_TYPED_NE, BUSTER_TYPED_CLASS_I32, 0);
        check_lower_emit(writer, BUSTER_TYPED_BRANCH_TRUE, BUSTER_TYPED_CLASS_I32, nan);
        check_lower_floating_recipe(writer, &absolute, floating);
        check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, class, magnitude);
        check_lower_emit(writer, BUSTER_TYPED_LOCAL, class, magnitude);
        check_lower_emit(writer, BUSTER_TYPED_CONST, class, infinity);
        check_lower_emit(writer, BUSTER_TYPED_EQ, BUSTER_TYPED_CLASS_I32, 0);
        check_lower_emit(writer, BUSTER_TYPED_BRANCH_TRUE, BUSTER_TYPED_CLASS_I32, infinite);
        check_lower_emit(writer, BUSTER_TYPED_LOCAL, class, magnitude);
        check_lower_emit(writer, BUSTER_TYPED_CONST, class, smallest);
        check_lower_emit(writer, BUSTER_TYPED_LT, BUSTER_TYPED_CLASS_I32, 0);
        check_lower_emit(writer, BUSTER_TYPED_BRANCH_TRUE, BUSTER_TYPED_CLASS_I32, subnormal);
        check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 2);
        check_lower_emit(writer, BUSTER_TYPED_JUMP_VALUE, BUSTER_TYPED_CLASS_I32, end);
        check_lower_place(writer, nan);
        check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 0);
        check_lower_emit(writer, BUSTER_TYPED_JUMP_VALUE, BUSTER_TYPED_CLASS_I32, end);
        check_lower_place(writer, infinite);
        check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 1);
        check_lower_emit(writer, BUSTER_TYPED_JUMP_VALUE, BUSTER_TYPED_CLASS_I32, end);
        check_lower_place(writer, subnormal);
        check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 3);
        check_lower_emit(writer, BUSTER_TYPED_JUMP_VALUE, BUSTER_TYPED_CLASS_I32, end);
        check_lower_place(writer, zero);
        check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 4);
        check_lower_place(writer, end);
    }
    else
    {
        // isinf_sign: clang's quiet compare of the magnitude with infinity, for the exceptions it raises, its value
        // discarded; then the recipe.
        CBuiltinFloatLowerCall magnitude = absolute;
        if (x87)
        {
            // fabsl clears its object's sign in place, and the recipe after the compare reads the argument's.
            magnitude.argument_local = check_lower_temporary(writer, class);
            check_lower_emit(writer, BUSTER_TYPED_LOCAL, class, call->argument_local);
            check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, class, magnitude.argument_local);
        }
        check_lower_floating_recipe(writer, &magnitude, floating);
        check_lower_emit(writer, BUSTER_TYPED_CONST, class, check_lower_float_bits(writer, class, __builtin_infl()));
        check_lower_emit(writer, BUSTER_TYPED_EQ, BUSTER_TYPED_CLASS_I32, 0);
        check_lower_emit(writer, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_I32, 0);
        check_lower_floating_recipe(writer, call, task.type);
    }
}

// Always inlined into both copies of the tasks' loop (check_lower_drain_body).
static inline __attribute__((always_inline)) void check_lower_floating_emit(CheckLowerWriter* writer, CheckLowerTask task)
{
    const CBuiltinFloatLowerCall* call = writer->floating + task.node;
    if (task.kind == CHECK_LOWER_BUILTIN_FLOAT_STRICT && (call->id == CBUILTIN_FPCLASSIFY || call->id == CBUILTIN_ISINF_SIGN))
    {
        check_lower_floating_strict(writer, task);
    }
    else
    {
        check_lower_floating_recipe(writer, call, task.type);
    }
}

static void check_lower_selector_test(CheckLowerWriter* writer, uint32_t local, uint32_t category, uint32_t target, bool equal)
{
    check_lower_instruction(writer, equal ? BUSTER_TYPED_BRANCH_TRUE : BUSTER_TYPED_BRANCH_FALSE, BUSTER_TYPED_CLASS_I32, target);
    check_lower_instruction(writer, BUSTER_TYPED_EQ, BUSTER_TYPED_CLASS_I32, 0);
    check_lower_instruction(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, category);
    check_lower_instruction(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_I32, local);
}

// isgreater ... isunordered of `node`'s two arguments, converted to `common`, the real floating type of their usual
// arithmetic conversions, with quiet compares, as clang -O0's are (ucomiss, ucomisd, fucomip): a NaN raises FE_INVALID
// only when it signals. Tier 0's EQ and NE are quiet in every class, but its LT and LE of floats and doubles signal
// (cmpsd's LT_OS and LE_OS, C's relational operators'). Their operands stay in their common format, so that the SSE
// comparisons retain MXCSR's DAZ policy. Evaluate both once before any comparison, then test both with quiet self-EQs:
// their eager AND raises FE_INVALID for either signaling NaN, including one beside a quiet NaN. Only an ordered pair
// reaches LT/LE/GT/GE. The x87's own ordered compares are quiet already and need no guard.
// islessgreater is `(a == a) & (b == b) & (a != b)` of temporaries that hold them, and isunordered `(a != a) | (b != b)`.
// Pushed in reverse, as tasks run.
static void check_lower_floating_compare(CheckLowerWriter* writer, uint32_t node, CBuiltinId id, TypeId common)
{
    const uint8_t class = check_lower_class(writer, common);
    if (id == CBUILTIN_ISLESSGREATER || id == CBUILTIN_ISUNORDERED)
    {
        const bool unordered = id == CBUILTIN_ISUNORDERED;
        const uint32_t a = check_lower_temporary(writer, class);
        const uint32_t b = check_lower_temporary(writer, class);
        if (!unordered)
        {
            check_lower_instruction(writer, BUSTER_TYPED_AND, BUSTER_TYPED_CLASS_I32, 0);
            check_lower_instruction(writer, BUSTER_TYPED_NE, BUSTER_TYPED_CLASS_I32, 0);
            check_lower_instruction(writer, BUSTER_TYPED_LOCAL, class, b);
            check_lower_instruction(writer, BUSTER_TYPED_LOCAL, class, a);
        }
        check_lower_instruction(writer, unordered ? BUSTER_TYPED_OR : BUSTER_TYPED_AND, BUSTER_TYPED_CLASS_I32, 0);
        check_lower_instruction(writer, unordered ? BUSTER_TYPED_NE : BUSTER_TYPED_EQ, BUSTER_TYPED_CLASS_I32, 0);
        check_lower_instruction(writer, BUSTER_TYPED_LOCAL, class, b);
        check_lower_instruction(writer, BUSTER_TYPED_LOCAL, class, b);
        check_lower_instruction(writer, unordered ? BUSTER_TYPED_NE : BUSTER_TYPED_EQ, BUSTER_TYPED_CLASS_I32, 0);
        check_lower_instruction(writer, BUSTER_TYPED_LOCAL, class, a);
        check_lower_instruction(writer, BUSTER_TYPED_LOCAL, class, a);
        check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, class, b);
        check_lower_operand(writer, node, 4, false, common);
        check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, class, a);
        check_lower_operand(writer, node, 3, false, common);
    }
    else
    {
        const uint8_t kind = id == CBUILTIN_ISGREATER ? BUSTER_TYPED_GT : id == CBUILTIN_ISGREATEREQUAL ? BUSTER_TYPED_GE :
                             id == CBUILTIN_ISLESS ? BUSTER_TYPED_LT : BUSTER_TYPED_LE;
        if (class == BUSTER_TYPED_CLASS_F80)
        {
            check_lower_instruction(writer, kind, BUSTER_TYPED_CLASS_I32, 0);
            check_lower_operand(writer, node, 4, false, common);
            check_lower_operand(writer, node, 3, false, common);
        }
        else
        {
            const uint32_t a = check_lower_temporary(writer, class);
            const uint32_t b = check_lower_temporary(writer, class);
            const uint32_t unordered = check_lower_label(writer);
            const uint32_t end = check_lower_label(writer);
            check_lower_mark(writer, end);
            check_lower_instruction(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 0);
            check_lower_mark(writer, unordered);
            check_lower_instruction(writer, BUSTER_TYPED_JUMP_VALUE, BUSTER_TYPED_CLASS_I32, end);
            check_lower_instruction(writer, kind, BUSTER_TYPED_CLASS_I32, 0);
            check_lower_instruction(writer, BUSTER_TYPED_LOCAL, class, b);
            check_lower_instruction(writer, BUSTER_TYPED_LOCAL, class, a);
            check_lower_instruction(writer, BUSTER_TYPED_BRANCH_FALSE, BUSTER_TYPED_CLASS_I32, unordered);
            check_lower_instruction(writer, BUSTER_TYPED_AND, BUSTER_TYPED_CLASS_I32, 0);
            check_lower_instruction(writer, BUSTER_TYPED_EQ, BUSTER_TYPED_CLASS_I32, 0);
            check_lower_instruction(writer, BUSTER_TYPED_LOCAL, class, b);
            check_lower_instruction(writer, BUSTER_TYPED_LOCAL, class, b);
            check_lower_instruction(writer, BUSTER_TYPED_EQ, BUSTER_TYPED_CLASS_I32, 0);
            check_lower_instruction(writer, BUSTER_TYPED_LOCAL, class, a);
            check_lower_instruction(writer, BUSTER_TYPED_LOCAL, class, a);
            check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, class, b);
            check_lower_operand(writer, node, 4, false, common);
            check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, class, a);
            check_lower_operand(writer, node, 3, false, common);
        }
    }
}

static void check_lower_floating_prepare(CheckLowerWriter* writer, uint32_t node)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const uint32_t selected = check_program_builtin_index(stream->floating, stream->floating_count, sizeof(CheckBuiltinFloatEffect), node);
    const uint32_t count = graph->argument_counts[node];
    if (selected == BUSTER_CHECK_NONE)
    {
        check_lower_fail(writer, CHECK_PROGRAM_INVALID);
    }
    else
    {
        const CheckBuiltinFloatEffect* effect = stream->floating + selected;
        const CBuiltinId id = effect->runtime_builtin;
        const bool classify = (id >= CBUILTIN_ISNAN && id <= CBUILTIN_ISINF_SIGN) || id == CBUILTIN_FABS || id == CBUILTIN_FABSF || id == CBUILTIN_FABSL;
        const bool compare = id >= CBUILTIN_ISGREATER && id <= CBUILTIN_ISUNORDERED;
        const bool selectors = id == CBUILTIN_FPCLASSIFY;
        if (effect->unsupported != 0 || (classify && count != (selectors ? 6u : 1u)) || (compare && count != 2))
        {
            check_lower_unsupported(writer, "this floating builtin");
        }
        else if (compare)
        {
            check_lower_floating_compare(writer, node, id, effect->floating_type);
        }
        else if (selectors && effect->selected_selector < 5)
        {
            // A known category retains the selector prefix and its side effects in the tested Clang 23.1.4's order.
            // C does not specify this order; Clang's own build compiler can affect its CodeGen argument order.
            const uint32_t order[] = { 5, 4, 0, 1, 2, 3 };
            const uint32_t result = check_lower_temporary(writer, BUSTER_TYPED_CLASS_I32);
            check_lower_instruction(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_I32, result);
            for (uint32_t i = BUSTER_ARRAY_LENGTH(order); i-- > 0;)
            {
                const uint32_t operand = order[i];
                if ((effect->evaluated_operands & (1u << operand)) != 0)
                {
                    if (operand == effect->selected_selector)
                    {
                        check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_I32, result);
                    }
                    check_lower_operand(writer, node, operand + 3, false,
                                           operand == effect->selected_selector ? BUSTER_TYPE_INT : BUSTER_TYPE_VOID);
                }
            }
        }
        else if (classify)
        {
            const uint32_t index = writer->floating_count++;
            const uint8_t class = check_lower_class(writer, effect->floating_type);
            // A long double's argument is a frame object of its own, which fabsl clears the sign of in place.
            const uint32_t argument = check_lower_temporary(writer, class);
            const bool fenv_access = graph->fp_environments != NULL && (graph->fp_environments[node] & BUSTER_CHECK_FP_ENV_ACCESS) != 0;
            writer->floating[index] = (CBuiltinFloatLowerCall){ .id = id, .argument_local = argument, .argument_class = class, .fenv_access = fenv_access };
            if (selectors)
            {
                const uint32_t category = check_lower_temporary(writer, BUSTER_TYPED_CLASS_I32);
                const uint32_t result = check_lower_temporary(writer, BUSTER_TYPED_CLASS_I32);
                const uint32_t normal = check_lower_temporary(writer, BUSTER_TYPED_CLASS_I32);
                const uint32_t end = check_lower_label(writer);
                check_lower_instruction(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_I32, result);
                check_lower_mark(writer, end);
                check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_I32, result);
                check_lower_instruction(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_I32, normal);
                check_lower_selector_test(writer, category, 2, end, false);
                // Emitted in reverse: normal runs before subnormal, as in official Clang 23.1.4.
                check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_I32, result);
                check_lower_operand(writer, node, 6, false, BUSTER_TYPE_INT);
                check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_I32, normal);
                check_lower_operand(writer, node, 5, false, BUSTER_TYPE_INT);
                for (uint32_t i = 3; i-- > 0;)
                {
                    const uint32_t operand = i == 0 ? 4 : i - 1;
                    check_lower_selector_test(writer, category, operand, end, true);
                    check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_I32, result);
                    check_lower_operand(writer, node, operand + 3, false, BUSTER_TYPE_INT);
                }
                check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_I32, category);
            }
            check_lower_push(writer, (CheckLowerTask){
                .kind = effect->may_raise_fp_exceptions ? CHECK_LOWER_BUILTIN_FLOAT_STRICT : CHECK_LOWER_BUILTIN_FLOAT,
                .node = index,
                .type = stream->values[node].type,
            });
            check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, class, argument);
            check_lower_operand(writer, node, selectors ? 8 : 3, false, effect->floating_type);
        }
        else if (stream->values[node].constant_kind == CHECK_CONSTANT_FLOATING &&
                 (stream->values[node].flags & BUSTER_CHECK_FP_ENVIRONMENT) == 0)
        {
            check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_CONSTANT, .node = node });
            for (uint32_t i = count; i-- > 0;)
            {
                if (i < 32 && (effect->evaluated_operands & (1u << i)) != 0)
                {
                    check_lower_operand(writer, node, i + 3, false, BUSTER_TYPE_VOID);
                }
            }
        }
        else
        {
            check_lower_unsupported(writer, "this floating builtin");
        }
    }
}

#include "c_check_program_atomic.c"

static void check_lower_region(CheckLowerWriter* writer, CheckLowerTask task)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const uint32_t selected = program->input->expression_regions[graph->tokens[task.node]];
    if (selected == 0 || selected > program->region_count)
    {
        check_lower_fail(writer, CHECK_PROGRAM_INVALID);
    }
    else
    {
        const CheckProgramRegion* region = program->regions + selected - 1;
        const uint32_t first = writer->local_count;
        // A region starts with an empty evaluation stack. Escaping to an outer label or returning abandons these
        // saved operands; only the ordinary exit restores them. This also covers regions in later call arguments.
        while (writer->reachable && writer->stack_head != 0 && writer->output->status == CHECK_PROGRAM_OK)
        {
            const uint8_t class = writer->stack_classes[writer->stack_head];
            const uint32_t local = check_lower_temporary(writer, class);
            check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, class, local);
        }
        const uint32_t end = writer->local_count;
        const TypeId type = program->stream->values[task.node].type;
        const uint8_t class = task.address ? BUSTER_TYPED_CLASS_U64 : check_lower_value_class(writer, type);
        const uint32_t result = class == BUSTER_TYPED_CLASS_VOID ? BUSTER_CHECK_NONE :
                                task.address                     ? check_lower_temporary(writer, class) :
                                                                   check_lower_temporary_of(writer, type);
        if (result != BUSTER_CHECK_NONE)
        {
            check_lower_instruction(writer, BUSTER_TYPED_LOCAL, class, result);
        }
        for (uint32_t local = first; local < end; local += 1)
        {
            check_lower_instruction(writer, BUSTER_TYPED_LOCAL, writer->value_classes[local], local);
        }
        if (result != BUSTER_CHECK_NONE)
        {
            check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, class, result);
        }
        const TypeId converted = program->stream->conversions[task.node].operands[0].type;
        check_lower_operand(writer, task.node, 0, task.address, converted);
        for (uint32_t i = region->count; i-- > 0;)
        {
            check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_CONTROL, .node = region->first + i });
        }
    }
}

// A vector operator (#136): its operands, converted to the operator's vector type (a scalar's splat, which
// check_lower_vector_conversion makes), and an X86 entry of its row. A compare's row is its operands' type's, whose
// result is its mask's -1 or 0 elements; a shift's count, when it is a scalar, is splat at the shifted vector's type.
static __attribute__((noinline)) void check_lower_vector_operation(CheckLowerWriter* writer, uint32_t node)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const TypeTable* types = program->types;
    const CheckOp op = graph->ops[node];
    const uint32_t a = graph->operand0[node];
    const uint32_t b = graph->operand1[node];
    if (op == CHECK_OP_NEGATIVE || op == CHECK_OP_BIT_NOT)
    {
        const TypeId vector = stream->values[a].type;
        const CheckVectorElement element = check_vector_lower_element(types, vector);
        check_lower_vector_half_chain(writer, vector, op, a, BUSTER_CHECK_NONE);
        if (op == CHECK_OP_NEGATIVE && element.floating)
        {
            // The sign bit, the row's integer operand, after the vector.
            check_lower_vector_row(writer, op, vector, 0);
            check_lower_instruction(writer, BUSTER_TYPED_CONST, element.bytes == 8 ? BUSTER_TYPED_CLASS_U64 : BUSTER_TYPED_CLASS_U32,
                                     element.bytes == 2 ? UINT64_C(0x8000) : element.bytes == 4 ? UINT64_C(0x80000000) : UINT64_C(0x8000000000000000));
        }
        else
        {
            check_lower_vector_row(writer, op, vector, 0);
        }
        check_lower_operand(writer, node, 0, false, 0);
    }
    else
    {
        const TypeId vector = stream->conversions[node].operands[0].type;
        const bool shift = op == CHECK_OP_SHIFT_LEFT || op == CHECK_OP_SHIFT_RIGHT;
        const bool scalar_count = shift && check_lower_vector_bytes(types, stream->values[b].type) == 0;
        check_lower_vector_half_chain(writer, vector, op, a, b);
        check_lower_vector_row(writer, op, vector, 0);
        check_lower_operand(writer, node, 1, false, scalar_count ? vector : stream->conversions[node].operands[1].type);
        check_lower_operand(writer, node, 0, false, vector);
    }
}

// A TypeId of an integer or floating class, for a row's result's conversion to its builtin's type.
static TypeId check_lower_class_type(uint8_t class)
{
    static const TypeId types[] = {
        [BUSTER_TYPED_CLASS_I8] = BUSTER_TYPE_SCHAR,
        [BUSTER_TYPED_CLASS_U8] = BUSTER_TYPE_UCHAR,
        [BUSTER_TYPED_CLASS_I16] = BUSTER_TYPE_SHORT,
        [BUSTER_TYPED_CLASS_U16] = BUSTER_TYPE_USHORT,
        [BUSTER_TYPED_CLASS_I32] = BUSTER_TYPE_INT,
        [BUSTER_TYPED_CLASS_U32] = BUSTER_TYPE_UINT,
        [BUSTER_TYPED_CLASS_I64] = BUSTER_TYPE_LONG,
        [BUSTER_TYPED_CLASS_U64] = BUSTER_TYPE_ULONG,
        [BUSTER_TYPED_CLASS_F32] = BUSTER_TYPE_FLOAT,
        [BUSTER_TYPED_CLASS_F64] = BUSTER_TYPE_DOUBLE,
    };
    return class < BUSTER_ARRAY_LENGTH(types) ? types[class] : 0;
}

// An integer operand's class in an X86 row: its argument's, but 32 bits for an argument narrower than that (a mask of 8
// or 16 bits, a char or a short), signed as it is: a row reads an operand's register through its form's field, of 32
// bits at least (tier0_operation_role_valid), so the narrower value is converted to it first.
static uint8_t check_lower_x86_operand_class(uint8_t class)
{
    return class == BUSTER_TYPED_CLASS_I8 || class == BUSTER_TYPED_CLASS_I16 ? BUSTER_TYPED_CLASS_I32 :
           class == BUSTER_TYPED_CLASS_U8 || class == BUSTER_TYPED_CLASS_U16 ? BUSTER_TYPED_CLASS_U32 :
                                                                               class;
}

// Whether a builtin's catalog entry is a name's.
static bool check_lower_x86_named(const BuiltinIntrinsicInfo* info, const char* name)
{
    const uint32_t length = (uint32_t)strlen(name);
    return info != NULL && info->name_length == length && memcmp(info->name, name, length) == 0;
}

// An x86 builtin's row (#136, #148): an X86 entry of its row (check_x86_lower_row), after its arguments but its immediate,
// which the row's records take, and the row's result converted to the call's type. Its record in the stream
// (CheckStream.x86, in its nodes' order) names it; an integer operand's class is its converted argument's, widened to 32
// bits (check_lower_x86_operand_class). A reduction's is two rows (check_x86_lower_reduce_row): the first of its vector,
// the last argument, to 16 bytes, the second of those and its floating start, the first, where it has one.
static void check_lower_x86_row_builtin(CheckLowerWriter* writer, uint32_t node)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    uint32_t low = 0;
    uint32_t high = stream->x86_count;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        if (stream->x86[middle].node < node)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    const CheckBuiltinX86Effect* effect = low < stream->x86_count && stream->x86[low].node == node ? stream->x86 + low : NULL;
    const uint32_t first = graph->argument_offsets[node];
    const uint32_t count = graph->argument_counts[node];
    const BuiltinIntrinsicInfo* info = effect != NULL ? effect->info : NULL;
    const CheckX86LowerRule* rule = info != NULL ? check_x86_lower_rule(info->name, info->name_length) : NULL;
    // The immediate the rule's records take, and its rounding control, its last argument where it has one.
    const uint32_t skipped = rule != NULL ? rule->immediate : BUSTER_CHECK_X86_NO_IMMEDIATE;
    const uint32_t rounded = rule != NULL && rule->rounding != CHECK_X86_ROUND_NONE && count != 0 ? count - 1 : BUSTER_CHECK_X86_NO_IMMEDIATE;
    CheckValue immediates[2] = { 0 };
    bool constants = true;
    for (uint32_t i = 0; i < 2; i += 1)
    {
        const uint32_t a = i == 0 ? skipped : rounded;
        if (a < count)
        {
            // Immediates follow the same retained prototype conversion as evaluated arguments. In particular, a wide
            // integer converted to the scale's int may be 1, 2, 4 or 8 even when its original payload is not.
            immediates[i] = stream->values[graph->argument_nodes[first + a]];
            check_action_convert_value(program->types, &immediates[i], stream->argument_conversions[first + a].type, node);
            constants &= immediates[i].constant_kind == CHECK_CONSTANT_INTEGER;
        }
        else
        {
            constants &= a == BUSTER_CHECK_X86_NO_IMMEDIATE;
        }
    }
    Tier0Operation row;
    const char* reason = info == NULL ? "this x86 builtin" :
                         !constants ? "x86 builtins of an immediate that isn't a constant" :
                         check_x86_lower_row_of(&writer->target, rule, (uint64_t)immediates[0].integer,
                                                rounded != BUSTER_CHECK_X86_NO_IMMEDIATE ? (uint64_t)immediates[1].integer : 4, &row);
    Tier0Operation tail;
    const bool reduce = reason == NULL && rule->family == CHECK_X86_LOWER_REDUCE;
    if (reduce)
    {
        check_x86_lower_reduce_row(rule, &tail);
    }
    // The row's operands, the arguments but the immediates, each integer one of its argument's class, widened.
    uint32_t operand = 0;
    for (uint32_t a = 0; reason == NULL && a < count; a += 1)
    {
        const uint8_t class = check_lower_value_class(writer, stream->argument_conversions[first + a].type);
        const bool taken = a != skipped && a != rounded;
        if (taken && operand < row.operand_count && row.operand_classes[operand] != BUSTER_TYPED_CLASS_VECTOR)
        {
            row.operand_classes[operand] = check_lower_x86_operand_class(class);
        }
        operand += taken;
    }
    if (reason == NULL && operand != (reduce ? row.operand_count + tail.operand_count - 1 : row.operand_count))
    {
        check_lower_fail(writer, CHECK_PROGRAM_INVALID);
    }
    else if (reason != NULL)
    {
        check_lower_unsupported(writer, reason);
    }
    else
    {
        const uint32_t index = check_lower_operation_index(writer, &row);
        const uint32_t last = reduce ? check_lower_operation_index(writer, &tail) : index;
        const uint8_t result_class = reduce ? tail.result_class : row.result_class;
        const TypeId result = stream->values[node].type;
        // ktestz's result is whether its row's is 0: an int from the test, converted to the call's type.
        const bool zero_test = rule->zero_test;
        if (result_class != BUSTER_TYPED_CLASS_VOID && result_class != BUSTER_TYPED_CLASS_VECTOR)
        {
            check_lower_convert(writer, zero_test ? BUSTER_TYPE_INT : check_lower_class_type(result_class), result);
        }
        if (zero_test)
        {
            check_lower_truth(writer, check_lower_class_type(row.result_class), true);
        }
        if (reduce)
        {
            check_lower_instruction(writer, BUSTER_TYPED_X86, tail.result_class, last);
        }
        check_lower_instruction(writer, BUSTER_TYPED_X86, row.result_class, index);
        // An X86 entry makes the function one with vectors (Tier0Function.vectors), whatever its values' classes.
        writer->vectors = true;
        for (uint32_t a = count; a-- > 0;)
        {
            if (a != skipped && a != rounded)
            {
                // The argument at its prototype's type, then widened (the tasks run in the reverse of their pushes).
                const TypeId type = stream->argument_conversions[first + a].type;
                const uint8_t class = check_lower_value_class(writer, type);
                const uint8_t widened = check_lower_x86_operand_class(class);
                if (widened != class)
                {
                    check_lower_convert(writer, type, check_lower_class_type(widened));
                }
                check_lower_operand(writer, node, a + 3, false, type);
            }
        }
    }
}

// An x86 builtin's call: a leaf's (c_builtin_x86_helpers.c) where the helpers planned one for it, the generic
// __builtin_unpredictable's operand (its hint is the branch's, which Tier 0 doesn't take) and __builtin_assume's nothing
// (its operand isn't evaluated), or its row (check_lower_x86_row_builtin).
static __attribute__((noinline)) void check_lower_x86_builtin(CheckLowerWriter* writer, uint32_t node)
{
    const CheckProgram* program = writer->program;
    const CheckStream* stream = program->stream;
    const CheckProgramHelpers* helpers = &writer->output->helpers;
    const uint32_t first = program->input->graph.argument_offsets[node];
    const uint32_t effect = check_program_builtin_index(stream->x86, stream->x86_count, sizeof(CheckBuiltinX86Effect), node);
    const BuiltinIntrinsicInfo* info = effect != BUSTER_CHECK_NONE ? stream->x86[effect].info : NULL;
    if (check_program_builtin_index(helpers->uses, helpers->use_count, sizeof(CheckProgramHelperUse), node) != BUSTER_CHECK_NONE)
    {
        // The leaf's instructions' features (c_builtin_x86_helpers.c), which the target must have.
        const CBuiltinX86HelperKind leaf = info != NULL ? cbuiltin_x86_helper_kind(info->name, info->name_length) : CBUILTIN_X86_HELPER_NONE;
        const uint64_t needed = cbuiltin_x86_helper_infos[leaf].features;
        if ((writer->target.features & needed) != needed)
        {
            check_lower_unsupported(writer, cbuiltin_x86_helper_infos[leaf].refusal);
        }
        else
        {
            check_lower_helper(writer, node);
        }
    }
    else if (check_lower_x86_named(info, "__builtin_unpredictable") && program->input->graph.argument_counts[node] == 1)
    {
        const TypeId type = stream->argument_conversions[first].type;
        check_lower_convert(writer, type, stream->values[node].type);
        check_lower_operand(writer, node, 3, false, type);
    }
    else if (!check_lower_x86_named(info, "__builtin_assume"))
    {
        check_lower_x86_row_builtin(writer, node);
    }
}

// A vector constant's index among the function's (CheckLowerWriter.vector_constants): an equal constant's, or a new
// one's. `words` holds its bytes, the first first, and zeros past them.
static uint32_t check_lower_vector_constant(CheckLowerWriter* writer, const uint64_t words[8], uint32_t bytes)
{
    uint32_t index = 0;
    while (index < writer->vector_constant_count &&
           (writer->vector_constant_bytes[index] != bytes || memcmp(writer->vector_constants + 8 * (uint64_t)index, words, 8 * sizeof(uint64_t)) != 0))
    {
        index += 1;
    }
    if (index == writer->vector_constant_count && index < writer->vector_constant_capacity)
    {
        memcpy(writer->vector_constants + 8 * (uint64_t)index, words, 8 * sizeof(uint64_t));
        writer->vector_constant_bytes[index] = (uint8_t)bytes;
        writer->vector_constant_count += 1;
    }
    else if (index == writer->vector_constant_count)
    {
        check_lower_fail(writer, CHECK_PROGRAM_RESOURCE);
    }
    return index;
}

// Whether a node is __builtin_nondeterministic_value's call, whose value is any of its type's.
static bool check_lower_nondeterministic(const CheckProgram* program, uint32_t node)
{
    bool result = false;
    if (node < program->stream->count && program->input->graph.ops[node] == CHECK_OP_BUILTIN_VECTOR)
    {
        const CheckStream* stream = program->stream;
        const uint32_t selected = check_program_builtin_index(stream->vectors, stream->vector_count, sizeof(CheckBuiltinVectorEffect), node);
        result = selected != BUSTER_CHECK_NONE && stream->vectors[selected].runtime_builtin == CBUILTIN_NONDETERMINISTIC_VALUE;
    }
    return result;
}

// A shuffle's indices (__builtin_shufflevector's arguments past its vectors, `lanes` of them, constants the checker
// folded): each one's value at its own type's width (as clang reads them), -1 of a signed type an undefined element
// (BUSTER_CHECK_VECTOR_SHUFFLE_UNDEFINED), and with `b_undefined` b's elements too. False where one isn't a constant.
static bool check_lower_shuffle_indices(const CheckProgram* program, uint32_t node, uint32_t lanes, uint32_t source_lanes, bool b_undefined,
                                        uint32_t indices[BUSTER_CHECK_VECTOR_SHUFFLE_LANES])
{
    const CheckGraph* graph = &program->input->graph;
    const uint32_t first = graph->argument_offsets[node];
    bool constant = lanes <= BUSTER_CHECK_VECTOR_SHUFFLE_LANES;
    for (uint32_t i = 0; constant && i < lanes; i += 1)
    {
        const CheckValue* value = program->stream->values + graph->argument_nodes[first + 2 + i];
        uint32_t width = 0;
        bool is_signed = false;
        constant = value->constant_kind == CHECK_CONSTANT_INTEGER && check_action_integer_type(program->types, value->type, &width, &is_signed);
        const unsigned __int128 bits = constant ? value->integer & check_action_mask(width) : 0;
        const bool undefined = is_signed && bits == check_action_mask(width);
        indices[i] = undefined || (b_undefined && bits >= source_lanes) ? BUSTER_CHECK_VECTOR_SHUFFLE_UNDEFINED : (uint32_t)bits;
        constant &= undefined || bits < (unsigned __int128)2 * source_lanes;
    }
    return constant;
}

// A type's bytes, as the lowering lays out its objects.
static uint32_t check_lower_vector_size(const CheckLowerWriter* writer, TypeId type)
{
    const uint64_t size = type_size(writer->program->types, type_unqualified(type_strip_aligned(writer->program->types, type)));
    return size <= 64 ? (uint32_t)size : 0;
}

// The bytes of a compound literal of `size` bytes (at most 64) whose every initializer part is an integer constant or
// clears its bytes (a vector's elements: the intrinsics' zeros), with no initializer effects, or false.
static bool check_lower_constant_bytes(const CheckLowerWriter* writer, uint32_t node, uint8_t bytes[64], uint32_t size)
{
    const CheckProgram* program = writer->program;
    const TypeTable* types = program->types;
    const CheckValue* value = node < program->stream->count ? program->stream->values + node : NULL;
    bool result = value != NULL && !check_lower_copy_volatile(writer, value, 0) && program->input->graph.ops[node] == CHECK_OP_VALUE && value->object != 0 && value->object < program->object_count &&
                  program->input->compound_objects[value->object - 1] && !program->input->static_objects[value->object - 1] &&
                  program->initializers != 0 && size != 0 && size <= 64;
    const uint32_t selected = result ? check_initializers_plan_index(program->initializers, value->object - 1) : 0;
    const CheckInitializer* plan = selected != 0 ? program->initializers->plans + selected - 1 : NULL;
    result = plan != NULL && plan->status == CHECK_INIT_OK && plan->object_size == size && (plan->count == 0 || plan->parts != NULL);
    memset(bytes, 0, 64);
    for (uint32_t i = 0; result && i < plan->count; i += 1)
    {
        const CheckInitPart* part = plan->parts + i;
        const uint64_t part_bytes = part->kind == CHECK_INIT_VALUE ? type_size(types, part->type) : part->byte_count;
        result = part->bit_width == 0 && part->bit_offset % 8 == 0 && part->bit_offset / 8 + part_bytes <= size &&
                 (part->kind == CHECK_INIT_CLEAR || part->kind == CHECK_INIT_VALUE);
        if (result && part->kind == CHECK_INIT_VALUE)
        {
            // The part's value is converted to its type already (c_check_initializer.c).
            const CheckValue* constant = &part->value;
            uint32_t width = 0;
            bool is_signed = false;
            result = constant->constant_kind == CHECK_CONSTANT_INTEGER && constant->fault == CHECK_FAULT_NONE &&
                     (constant->flags & BUSTER_CHECK_SIDE_EFFECT) == 0 && check_action_integer_type(types, part->type, &width, &is_signed) &&
                     width == part_bytes * 8 && part_bytes <= 8;
            for (uint32_t byte = 0; result && byte < part_bytes; byte += 1)
            {
                bytes[part->bit_offset / 8 + byte] = (uint8_t)(constant->integer >> (8 * byte));
            }
        }
        else if (result)
        {
            memset(bytes + part->bit_offset / 8, 0, part_bytes);
        }
    }
    return result;
}

// The generic vector builtins' X86 entries (c_check_program_vector_builtins.c), or their refusal by name. A row's
// operands are the builtin's vector arguments at their types, whose tasks run before its entry's (the tasks run in the
// reverse of their pushes):
//
// - a shuffle's two vectors, then its index vector, a constant, where it permutes; its second vector is left out where
//   its elements are unread and its evaluation has no effects, or is __builtin_nondeterministic_value's;
// - a conversion's vector, or where it converts a shuffle's first elements in place (the intrinsics' conversions of a
//   vector's low elements: a shuffle of fewer than 16 bytes that Tier 0 has no class for), that shuffle's first vector,
//   its second one having no effects;
// - an elementwise function's operands;
// - a reduction's vector, through its rows, the last one's element converted to the call's type.
//
// __builtin_nondeterministic_value gives zeros, without evaluating its operand.
static __attribute__((noinline)) void check_lower_vector_builtin(CheckLowerWriter* writer, uint32_t node)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const TypeTable* types = program->types;
    const X86Target* target = &writer->target;
    const uint32_t selected = check_program_builtin_index(stream->vectors, stream->vector_count, sizeof(CheckBuiltinVectorEffect), node);
    const CheckBuiltinVectorEffect* effect = selected != BUSTER_CHECK_NONE ? stream->vectors + selected : NULL;
    const CBuiltinId builtin = effect != NULL ? effect->runtime_builtin : CBUILTIN_NONE;
    const TypeId result = stream->values[node].type;
    const uint32_t first = graph->argument_offsets[node];
    const uint32_t count = graph->argument_counts[node];
    const char* reason = NULL;
    Tier0Operation rows[BUSTER_CHECK_VECTOR_REDUCE_ROWS];
    uint32_t row_count = 0;
    if (effect == NULL)
    {
        check_lower_fail(writer, CHECK_PROGRAM_INVALID);
    }
    else if (effect->unsupported != 0)
    {
        reason = "vector builtins of operands the checker doesn't fold";
    }
    else if (builtin == CBUILTIN_SHUFFLEVECTOR && count < 3)
    {
        reason = "vector shuffles by a vector of indices";
    }
    else if (builtin == CBUILTIN_SHUFFLEVECTOR)
    {
        const TypeId source = stream->argument_conversions[first].type;
        const uint32_t b = graph->argument_nodes[first + 1];
        const bool b_undefined = check_lower_nondeterministic(program, b);
        const bool b_effects = !b_undefined && (stream->values[b].flags & BUSTER_CHECK_SIDE_EFFECT) != 0;
        uint32_t indices[BUSTER_CHECK_VECTOR_SHUFFLE_LANES];
        CheckVectorShuffle plan;
        // A shuffle of a conversion into a vector narrower than 16 bytes: its row reads the conversion's operand, and
        // the second vector's constant bytes where it permutes (check_vector_narrow_shuffle_row).
        const uint32_t a = graph->argument_nodes[first];
        const uint32_t converted = a < stream->count && graph->ops[a] == CHECK_OP_BUILTIN_VECTOR && graph->argument_counts[a] >= 1 ?
                                   check_program_builtin_index(stream->vectors, stream->vector_count, sizeof(CheckBuiltinVectorEffect), a) : BUSTER_CHECK_NONE;
        const bool narrow = check_lower_vector_of(writer, source) == 0 && converted != BUSTER_CHECK_NONE &&
                            stream->vectors[converted].runtime_builtin == CBUILTIN_CONVERTVECTOR && stream->vectors[converted].unsupported == 0 &&
                            check_lower_vector_of(writer, stream->argument_conversions[graph->argument_offsets[a]].type) != 0;
        const bool fused = narrow && check_lower_vector_of(writer, result) != 0;
        const TypeId operand = fused ? stream->argument_conversions[graph->argument_offsets[a]].type : source;
        if (!fused && (check_lower_vector_of(writer, source) == 0 || check_lower_vector_of(writer, result) == 0))
        {
            reason = "vector types of other than 16, 32 or 64 bytes";
        }
        else if (!check_lower_shuffle_indices(program, node, count - 2, check_builtin_vector_lanes(types, source), b_undefined, indices))
        {
            reason = "vector builtins of operands the checker doesn't fold";
        }
        else if (fused)
        {
            // The second vector's bytes, a compound literal's constants (or none, __builtin_nondeterministic_value's), which
            // the row takes at compile time: it is never evaluated, as it has no effects.
            uint8_t b_bytes[64];
            const bool constant = !b_undefined && check_lower_constant_bytes(writer, b, b_bytes, check_lower_vector_size(writer, source));
            reason = !b_undefined && !constant ? "narrow vector shuffle operands with nonconstant values or side effects" :
                                                 check_vector_narrow_shuffle_row(types, target, operand, source, result, indices, constant ? b_bytes : NULL, rows, &plan);
        }
        else
        {
            reason = check_vector_shuffle_row(types, target, source, result, indices, b_effects, rows, &plan);
        }
        if (reason == NULL)
        {
            check_lower_instruction(writer, BUSTER_TYPED_X86, BUSTER_TYPED_CLASS_VECTOR, check_lower_operation_index(writer, rows));
            if (plan.permutes)
            {
                check_lower_instruction(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_VECTOR, check_lower_vector_constant(writer, plan.constant, plan.constant_bytes));
            }
            if (fused && plan.permutes)
            {
                check_lower_instruction(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_VECTOR, check_lower_vector_constant(writer, plan.second, 16));
            }
            else if (!fused && (plan.reads_b || b_effects))
            {
                check_lower_operand(writer, node, 4, false, stream->argument_conversions[first + 1].type);
            }
            // A fused shuffle's first operand is its conversion's.
            check_lower_operand(writer, fused ? a : node, 3, false, operand);
            writer->vectors = true;
        }
    }
    else if (builtin == CBUILTIN_CONVERTVECTOR)
    {
        // The vector converted, or the shuffle whose first elements are (its first vector, then, the operand).
        const TypeId source = stream->argument_conversions[first].type;
        const uint32_t argument = graph->argument_nodes[first];
        uint32_t operand_node = node;
        TypeId operand_type = source;
        if (check_lower_vector_of(writer, source) == 0 && argument < stream->count && graph->ops[argument] == CHECK_OP_BUILTIN_VECTOR &&
            graph->argument_counts[argument] >= 3)
        {
            const uint32_t inner = check_program_builtin_index(stream->vectors, stream->vector_count, sizeof(CheckBuiltinVectorEffect), argument);
            const uint32_t inner_first = graph->argument_offsets[argument];
            const TypeId inner_source = stream->argument_conversions[inner_first].type;
            const uint32_t inner_b = graph->argument_nodes[inner_first + 1];
            const uint32_t lanes = graph->argument_counts[argument] - 2;
            uint32_t indices[BUSTER_CHECK_VECTOR_SHUFFLE_LANES];
            bool prefix = inner != BUSTER_CHECK_NONE && stream->vectors[inner].runtime_builtin == CBUILTIN_SHUFFLEVECTOR && stream->vectors[inner].unsupported == 0 &&
                          check_lower_vector_of(writer, inner_source) != 0 &&
                          (check_lower_nondeterministic(program, inner_b) || (stream->values[inner_b].flags & BUSTER_CHECK_SIDE_EFFECT) == 0) &&
                          check_lower_shuffle_indices(program, argument, lanes, check_builtin_vector_lanes(types, inner_source), true, indices);
            for (uint32_t i = 0; prefix && i < lanes; i += 1)
            {
                prefix = indices[i] == i;
            }
            if (prefix)
            {
                operand_node = argument;
                operand_type = inner_source;
            }
        }
        if (check_lower_vector_of(writer, operand_type) == 0 || check_lower_vector_of(writer, result) == 0)
        {
            reason = "vector types of other than 16, 32 or 64 bytes";
        }
        else
        {
            reason = check_vector_convert_row(types, target, source, check_lower_vector_of(writer, operand_type), result, rows);
        }
        if (reason == NULL)
        {
            check_lower_instruction(writer, BUSTER_TYPED_X86, BUSTER_TYPED_CLASS_VECTOR, check_lower_operation_index(writer, rows));
            check_lower_operand(writer, operand_node, 3, false, operand_type);
            writer->vectors = true;
        }
    }
    else if (builtin >= CBUILTIN_ELEMENTWISE_ABS && builtin <= CBUILTIN_ELEMENTWISE_SUB_SAT)
    {
        // Of vectors, or of float or double scalars of one class.
        const uint32_t arguments = effect->evaluated_argument_count;
        const uint8_t scalar = check_lower_class(writer, result);
        bool vectors = check_lower_vector_of(writer, result) != 0 && arguments >= 1 && arguments <= 3 && arguments <= count;
        bool scalars = !vectors && (scalar == BUSTER_TYPED_CLASS_F32 || scalar == BUSTER_TYPED_CLASS_F64) && arguments >= 1 && arguments <= 3 && arguments <= count;
        for (uint32_t a = 0; (vectors || scalars) && a < arguments; a += 1)
        {
            const TypeId argument = stream->argument_conversions[first + a].type;
            vectors = vectors && check_lower_vector_of(writer, argument) == check_lower_vector_of(writer, result);
            scalars = scalars && check_lower_class(writer, argument) == scalar;
        }
        reason = vectors ? check_vector_elementwise_row(types, target, builtin, result, arguments, rows) :
                 scalars ? check_vector_scalar_row(target, builtin, scalar, arguments, rows) :
                           "elementwise builtins of scalars";
        if (reason == NULL)
        {
            check_lower_instruction(writer, BUSTER_TYPED_X86, vectors ? BUSTER_TYPED_CLASS_VECTOR : scalar, check_lower_operation_index(writer, rows));
            for (uint32_t a = arguments; a-- > 0;)
            {
                check_lower_operand(writer, node, a + 3, false, stream->argument_conversions[first + a].type);
            }
            writer->vectors = true;
        }
    }
    else if (builtin >= CBUILTIN_REDUCE_ADD && builtin <= CBUILTIN_REDUCE_OR)
    {
        const TypeId source = stream->argument_conversions[first].type;
        reason = check_lower_vector_of(writer, source) == 0 ? "vector types of other than 16, 32 or 64 bytes" :
                                                                check_vector_reduce_rows(types, target, builtin, source, rows, &row_count);
        if (reason == NULL && row_count != 0)
        {
            // The last row's element, at its class's type, converted to the call's; the rows run first to last.
            check_lower_convert(writer, check_lower_class_type(rows[row_count - 1].result_class), result);
            for (uint32_t r = row_count; r-- > 0;)
            {
                check_lower_instruction(writer, BUSTER_TYPED_X86, rows[r].result_class, check_lower_operation_index(writer, rows + r));
            }
            check_lower_operand(writer, node, 3, false, source);
            writer->vectors = true;
        }
    }
    else if (builtin == CBUILTIN_NONDETERMINISTIC_VALUE)
    {
        // Any value of the type: zeros.
        const uint32_t bytes = check_lower_vector_of(writer, result);
        const uint8_t class = check_lower_value_class(writer, result);
        if (bytes != 0)
        {
            const uint64_t zeros[8] = { 0 };
            check_lower_instruction(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_VECTOR, check_lower_vector_constant(writer, zeros, bytes));
            writer->vectors = true;
        }
        else if (class == BUSTER_TYPED_CLASS_I32 || class == BUSTER_TYPED_CLASS_U32 || class == BUSTER_TYPED_CLASS_I64 || class == BUSTER_TYPED_CLASS_U64 ||
                 class == BUSTER_TYPED_CLASS_F32 || class == BUSTER_TYPED_CLASS_F64)
        {
            check_lower_instruction(writer, BUSTER_TYPED_CONST, class, 0);
        }
        else
        {
            reason = "nondeterministic values of this type";
        }
    }
    else if (builtin == CBUILTIN_NONTEMPORAL_LOAD || builtin == CBUILTIN_NONTEMPORAL_STORE)
    {
        // A vector's, at an address aligned to its bytes (the pointer's element type's alignment, its typedef's among
        // them), a store's value of the vector's bytes.
        const bool store = builtin == CBUILTIN_NONTEMPORAL_STORE;
        const uint32_t arguments = store ? 2 : 1;
        const TypeId memory = type_element(types, type_strip_aligned(types, stream->argument_conversions[first + arguments - 1].type));
        const uint32_t bytes = check_lower_vector_bytes(types, memory);
        if (bytes == 0 || count != arguments)
        {
            reason = "nontemporal builtins of scalars";
        }
        else if (((uint64_t)1 << type_alignment_log2(types, memory)) < bytes)
        {
            reason = "nontemporal vector builtins at underaligned addresses";
        }
        else if (store && check_lower_vector_bytes(types, stream->argument_conversions[first].type) != bytes)
        {
            reason = "nontemporal stores of other than a vector of the stored type";
        }
        else
        {
            check_vector_nontemporal_row(types, memory, store, rows);
            check_lower_instruction(writer, BUSTER_TYPED_X86, store ? BUSTER_TYPED_CLASS_VOID : BUSTER_TYPED_CLASS_VECTOR, check_lower_operation_index(writer, rows));
            for (uint32_t a = arguments; a-- > 0;)
            {
                check_lower_operand(writer, node, a + 3, false, stream->argument_conversions[first + a].type);
            }
            writer->vectors = true;
        }
    }
    else
    {
        reason = "this vector builtin";
    }
    if (reason != NULL)
    {
        check_lower_unsupported(writer, reason);
    }
}

// A __builtin_bit_cast side's class where its bits are the value of a register: an integer's, an enum's or a pointer's, a
// float's or a double's, or a vector's; UINT8_MAX for any other (aggregates, arrays, long double, 128-bit integers and
// the half-precision floats), whose bits the lowering doesn't move.
static uint8_t check_lower_bit_cast_class(const CheckLowerWriter* writer, TypeId type)
{
    const uint8_t kind = check_action_kind(writer->program->types, type);
    const uint8_t class = check_lower_aggregate(writer, type) ? UINT8_MAX : check_lower_class(writer, type);
    const bool integer = (kind == BUSTER_TYPE_KIND_INTEGER || kind == BUSTER_TYPE_KIND_ENUM || kind == BUSTER_TYPE_KIND_POINTER) && class >= BUSTER_TYPED_CLASS_I8 &&
                         class <= BUSTER_TYPED_CLASS_U64;
    const bool real = kind == BUSTER_TYPE_KIND_FLOATING && (class == BUSTER_TYPED_CLASS_F32 || class == BUSTER_TYPED_CLASS_F64);
    const bool vector = class == BUSTER_TYPED_CLASS_VECTOR && check_lower_vector_of(writer, type) != 0;
    return integer || real || vector ? class : UINT8_MAX;
}

// __builtin_bit_cast(T, x): x's bytes as a T, of x's size. One the checker folded is its bytes, a floating T's constant
// (an integer T's is its value's, which the constants take before this). At run time, between integers or pointers, or
// between vectors, the bits are the value already; between an integer and a float or a double they go through a frame
// temporary, as clang -O0's go through memory.
static void check_lower_bit_cast(CheckLowerWriter* writer, uint32_t node)
{
    const CheckStream* stream = writer->program->stream;
    const uint32_t selected = check_program_builtin_index(stream->bit_casts, stream->bit_cast_count, sizeof(CheckBuiltinBitCastEffect), node);
    const CheckBuiltinBitCastEffect* effect = selected != BUSTER_CHECK_NONE ? stream->bit_casts + selected : NULL;
    const TypeId result = stream->values[node].type;
    const uint8_t from = effect != NULL ? check_lower_bit_cast_class(writer, effect->source_type) : UINT8_MAX;
    const uint8_t to = result != 0 ? check_lower_bit_cast_class(writer, result) : UINT8_MAX;
    if (effect == NULL)
    {
        check_lower_fail(writer, CHECK_PROGRAM_INVALID);
    }
    else if (from == UINT8_MAX || to == UINT8_MAX)
    {
        check_lower_unsupported(writer, "bit-cast builtins of types other than integers, pointers, floats, doubles and vectors");
    }
    else if (effect->folded)
    {
        uint64_t bits = 0;
        for (uint32_t i = 0; i < effect->payload_count && i < 8; i += 1)
        {
            bits |= (uint64_t)effect->bytes[i] << (8 * i);
        }
        check_lower_instruction(writer, BUSTER_TYPED_CONST, to, bits);
    }
    else if (from == to || (from <= BUSTER_TYPED_CLASS_U64 && to <= BUSTER_TYPED_CLASS_U64))
    {
        // A vector's bytes are its register's, whatever its elements; an integer's of the size keep their bits.
        if (to != BUSTER_TYPED_CLASS_VECTOR)
        {
            check_lower_convert(writer, effect->source_type, result);
        }
        check_lower_operand(writer, node, 3, false, effect->source_type);
    }
    else
    {
        const uint32_t local = check_lower_addressed_temporary(writer, from);
        check_lower_instruction(writer, BUSTER_TYPED_LOAD, to, 0);
        check_lower_instruction(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, local);
        check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, from, local);
        check_lower_operand(writer, node, 3, false, effect->source_type);
    }
}

// Always inlined into both copies of the tasks' loop (check_lower_drain_body).
static inline __attribute__((always_inline)) void check_lower_expand(CheckLowerWriter* writer, uint32_t node, bool address, bool initialized, bool discard)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const TypeTable* types = program->types;
    const CheckOp op = graph->ops[node];
    const CheckValue* value = stream->values + node;
    const uint32_t a = graph->operand0[node];
    const uint32_t b = graph->operand1[node];
    const TypeId type_a = stream->conversions[node].operands[0].type;
    const TypeId type_b = stream->conversions[node].operands[1].type;
    const uint8_t facts = check_lower_facts(writer, value->type);
    // An aggregate's value is its address (U64); a function's, an array's or an aggregate's is its address, which nothing
    // loads through.
    const bool aggregate = (facts & BUSTER_CHECK_LOWER_TYPE_AGGREGATE) != 0;
    const bool address_value = (facts & BUSTER_CHECK_LOWER_TYPE_ADDRESS) != 0;
    const uint8_t class = aggregate ? BUSTER_TYPED_CLASS_U64 : check_lower_class(writer, value->type);
    writer->token = graph->tokens[node];
    const bool constant = value->constant_kind != CHECK_CONSTANT_NONE && value->fault == CHECK_FAULT_NONE &&
                          (value->flags & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_FP_ENVIRONMENT)) == 0;
    if (op == CHECK_OP_VALUE && !initialized && value->object != 0 && value->object < program->object_count &&
        program->input->compound_objects[value->object - 1] && !program->input->static_objects[value->object - 1])
    {
        const uint32_t selected = program->initializers != 0 ? check_initializers_plan_index(program->initializers, value->object - 1) : 0;
        if (writer->local_map[value->object] == BUSTER_CHECK_NONE && check_lower_classless_vector(writer, value->type))
        {
            // One the locals left without an object (a narrowing shuffle's constant is never evaluated).
            check_lower_unsupported(writer, check_lower_type_feature(types, value->type));
        }
        else if (selected == 0)
        {
            check_lower_fail(writer, CHECK_PROGRAM_INVALID);
        }
        else
        {
            initialized = true;
            check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_EXPRESSION, .node = node, .address = address, .initialized = initialized, .discard = discard });
            check_lower_initializer(writer, (CheckControl){ .token = value->object - 1, .target = selected - 1 });
        }
    }
    else if (op == CHECK_OP_STATEMENT_VALUE)
    {
        check_lower_region(writer, (CheckLowerTask){ .kind = CHECK_LOWER_EXPRESSION, .node = node, .address = address, .initialized = initialized, .discard = discard });
    }
    else if (!address && value->category == CHECK_LVALUE && (facts & BUSTER_CHECK_LOWER_TYPE_ATOMIC) != 0)
    {
        check_lower_atomic_prepare(writer, node, false, discard);
    }
    else if (constant && op != CHECK_OP_INITIALIZE && op != CHECK_OP_BUILTIN_QUERY &&
             ((value->category == CHECK_RVALUE && !address) || ((address || address_value) && value->constant_kind == CHECK_CONSTANT_ADDRESS)))
    {
        // A discarded one (a comma's, whose operands have no effects) is nothing.
        if (!discard)
        {
            check_lower_constant(writer, value);
        }
    }
    else if (op == CHECK_OP_VALUE)
    {
        const uint32_t object = value->object;
        if (value->type == BUSTER_TYPE_VOID)
        {
            // The final value of a statement region without an expression statement is void.
        }
        else if (address || address_value)
        {
            check_lower_address(writer, object, value->offset);
        }
        else if (value->category == CHECK_LVALUE && object != 0 && object < program->object_count && check_lower_memory_class(class) &&
                 writer->local_map[object] == BUSTER_CHECK_NONE)
        {
            // A global of a memory class, through its address (a local's object takes LOCAL below).
            check_lower_address(writer, object, value->offset);
            check_lower_emit(writer, BUSTER_TYPED_LOAD, class, check_lower_access_value(types, class, value->type));
        }
        else if (value->category == CHECK_LVALUE && object != 0 && object < program->object_count)
        {
            const uint32_t local = writer->local_map[object];
            const SymbolId symbol = program->object_symbols[object];
            if (local != BUSTER_CHECK_NONE)
            {
                check_lower_emit(writer, BUSTER_TYPED_LOCAL, class, local);
            }
            else if (symbol != 0)
            {
                check_lower_emit(writer, BUSTER_TYPED_GLOBAL, class, symbol);
            }
            else
            {
                check_lower_fail(writer, CHECK_PROGRAM_INVALID);
            }
        }
        else
        {
            check_lower_unsupported(writer, "this value");
        }
    }
    else if (op == CHECK_OP_SELECT)
    {
        check_lower_operand(writer, node, 0, address, 0);
    }
    else if (op == CHECK_OP_REAL || op == CHECK_OP_IMAGINARY)
    {
        const TypeId input_type = stream->values[a].type;
        const bool complex = type_kind(types, type_strip_aligned(types, input_type)) == BUSTER_TYPE_KIND_COMPLEX;
        if (!complex && op == CHECK_OP_REAL)
        {
            check_lower_operand(writer, node, 0, address, 0);
        }
        else if (!complex)
        {
            check_lower_instruction(writer, BUSTER_TYPED_CONST, class, 0);
            check_lower_operand(writer, node, 0, false, BUSTER_TYPE_VOID);
        }
        else if (stream->values[a].category == CHECK_LVALUE || check_lower_complex(writer, input_type))
        {
            // The part at its offset from the value's address: an lvalue's, or a complex rvalue's, whose value is its
            // object's address.
            if (!address)
            {
                check_lower_instruction(writer, BUSTER_TYPED_LOAD, class, 0);
            }
            if (op == CHECK_OP_IMAGINARY)
            {
                check_lower_instruction(writer, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, 1);
                check_lower_instruction(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, type_size(types, type_element(types, input_type)));
            }
            check_lower_operand(writer, node, 0, stream->values[a].category == CHECK_LVALUE, 0);
        }
        else
        {
            check_lower_unsupported(writer, "_Complex values that are not lvalues");
        }
    }
    else if (op == CHECK_OP_VLA_BOUND || op == CHECK_OP_VLA_EXTENT)
    {
        const uint32_t identity = graph->members[node];
        if (identity == 0 || identity > program->input->vla_count || program->input->vlas[identity - 1].body != writer->body->opening)
        {
            check_lower_fail(writer, CHECK_PROGRAM_INVALID);
        }
        else
        {
            if (writer->extent_locals[identity - 1] == BUSTER_CHECK_NONE)
            {
                writer->extent_locals[identity - 1] = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
            }
            const uint32_t local = writer->extent_locals[identity - 1];
            check_lower_instruction(writer, op == CHECK_OP_VLA_BOUND ? BUSTER_TYPED_STORE_LOCAL : BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, local);
            if (op == CHECK_OP_VLA_BOUND)
            {
                check_lower_operand(writer, node, 0, false, BUSTER_TYPE_ULONG);
            }
        }
    }
    else if (op == CHECK_OP_SIZEOF_VLA)
    {
        check_lower_operand(writer, node, 0, false, BUSTER_TYPE_ULONG);
    }
    else if (op == CHECK_OP_BUILTIN_UNARY || op == CHECK_OP_BUILTIN_OVERFLOW)
    {
        check_lower_builtin_prepare(writer, node);
    }
    else if (op == CHECK_OP_BUILTIN_MISC)
    {
        const CBuiltinId id = (CBuiltinId)graph->members[node];
        if (id == CBUILTIN_TRAP || id == CBUILTIN_UNREACHABLE)
        {
            check_lower_helper(writer, node);
        }
        else
        {
            check_lower_miscellaneous_prepare(writer, node);
        }
    }
    else if (op == CHECK_OP_BUILTIN_ATOMIC)
    {
        const uint32_t helper = check_program_builtin_index(writer->output->helpers.uses, writer->output->helpers.use_count, sizeof(CheckProgramHelperUse), node);
        if (check_builtin_atomic_c11((CBuiltinId)graph->members[node]) && helper == BUSTER_CHECK_NONE)
        {
            check_lower_atomic_prepare(writer, node, true, discard);
        }
        else
        {
            check_lower_helper(writer, node);
        }
    }
    else if (op == CHECK_OP_BUILTIN_X86)
    {
        check_lower_x86_builtin(writer, node);
    }
    else if (op == CHECK_OP_BUILTIN_VECTOR)
    {
        check_lower_vector_builtin(writer, node);
    }
    else if (op == CHECK_OP_BUILTIN_BIT_CAST)
    {
        check_lower_bit_cast(writer, node);
    }
    else if (op == CHECK_OP_BUILTIN_FLOAT)
    {
        const uint32_t selected = check_program_builtin_index(stream->floating, stream->floating_count, sizeof(CheckBuiltinFloatEffect), node);
        const CheckBuiltinFloatEffect* effect = selected != BUSTER_CHECK_NONE ? stream->floating + selected : NULL;
        if (effect != NULL && effect->math_kind != CHECK_BUILTIN_MATH_NONE && effect->runtime_builtin != CBUILTIN_NONE)
        {
            check_lower_math(writer, node, effect);
        }
        else if (graph->members[node] == CBUILTIN_FLT_ROUNDS)
        {
            check_lower_helper(writer, node);
        }
        else
        {
            check_lower_floating_prepare(writer, node);
        }
    }
    else if (op == CHECK_OP_BUILTIN_QUERY)
    {
        const uint32_t selected = check_program_builtin_index(stream->queries, stream->query_count, sizeof(CheckBuiltinQueryEffect), node);
        if (selected == BUSTER_CHECK_NONE)
        {
            check_lower_fail(writer, CHECK_PROGRAM_INVALID);
        }
        else if (!check_program_query_known(program, node, stream->queries + selected))
        {
            check_lower_unsupported(writer, "this constant query at run time");
        }
        else
        {
            const uint32_t count = graph->argument_counts[node];
            const uint32_t evaluated = stream->queries[selected].evaluated_operands;
            const uint32_t mask = count < 32 ? (UINT32_C(1) << count) - 1 : UINT32_MAX;
            const bool constant_query = graph->members[node] == CBUILTIN_CONSTANT_P;
            const bool evaluate = stream->queries[selected].runtime_plan == CHECK_BUILTIN_QUERY_RUNTIME_EVALUATE_KNOWN;
            if ((evaluated & ~mask) != 0 || (constant_query && (count != 1 || evaluated != (uint32_t)evaluate)))
            {
                check_lower_fail(writer, CHECK_PROGRAM_INVALID);
            }
            check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_QUERY, .node = selected });
            for (uint32_t i = count; i-- > 0;)
            {
                if (i < 32 && (evaluated & (UINT32_C(1) << i)) != 0)
                {
                    check_lower_operand(writer, node, i + 3, false, BUSTER_TYPE_VOID);
                }
            }
        }
    }
    else if (op == CHECK_OP_ADDRESS)
    {
        check_lower_operand(writer, node, 0, true, 0);
    }
    else if (op == CHECK_OP_DEREFERENCE)
    {
        if (!address && !address_value)
        {
            check_lower_instruction(writer, BUSTER_TYPED_LOAD, class, check_lower_access_value(types, class, value->type));
        }
        check_lower_operand(writer, node, 0, false, type_a);
    }
    else if (op == CHECK_OP_MEMBER || op == CHECK_OP_ARROW)
    {
        // A struct's or union's member, at the offset the checker found it at (CheckStream.member_offsets), for a node it
        // checked without a diagnostic; not a vector's elements (`v.y`), which share the operator. A bit-field's address,
        // which only its stores ask for, is its first byte's.
        const TypeId record = op == CHECK_OP_ARROW ? type_element(types, type_a) : stream->values[a].type;
        const uint8_t record_kind = check_action_kind(types, record);
        const uint64_t offset = stream->member_offsets[node];
        const bool bit_field = (value->flags & BUSTER_CHECK_BIT_FIELD) != 0;
        CheckLowerBitField field = { 0 };
        if (stream->diagnostics[node] != CHECK_DIAG_NONE || (record_kind != BUSTER_TYPE_KIND_STRUCT && record_kind != BUSTER_TYPE_KIND_UNION) ||
            (!bit_field && offset % 8 != 0))
        {
            check_lower_unsupported(writer, "this member access");
        }
        else if (bit_field && !check_lower_bit_member(program, node, &field))
        {
            check_lower_unsupported(writer, "bit-fields of this type or spread over more than 8 bytes");
        }
        else
        {
            if (bit_field && !address)
            {
                check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_BIT_READ, .type = check_lower_bit_pack(&field) });
            }
            else if (!address && !address_value)
            {
                check_lower_instruction(writer, BUSTER_TYPED_LOAD, class, check_lower_access_value(types, class, value->type));
            }
            if (offset / 8 != 0)
            {
                check_lower_instruction(writer, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, 1);
                check_lower_instruction(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, offset / 8);
            }
            check_lower_operand(writer, node, 0, op == CHECK_OP_MEMBER, type_a);
        }
    }
    else if (class == BUSTER_TYPED_CLASS_VECTOR && op >= CHECK_OP_NEGATIVE && op <= CHECK_OP_BIT_XOR && op != CHECK_OP_NOT)
    {
        check_lower_vector_operation(writer, node);
    }
    else if (op == CHECK_OP_CAST || op == CHECK_OP_CONVERT || op == CHECK_OP_POSITIVE)
    {
        check_lower_operand(writer, node, 0, false, value->type);
    }
    else if (check_lower_complex_operation(writer, op, value->type, type_a))
    {
        // An operation on complex values (check_lower_complex_emit): each operand into a temporary as it is evaluated (a
        // real one kept real, converted to the computation's element), since Tier 0 reads an entry's last operand as the
        // class of the entry before it.
        const bool unary = op == CHECK_OP_NEGATIVE || op == CHECK_OP_BIT_NOT;
        const uint8_t element_class = check_lower_class(writer, check_lower_complex_element(types, type_a));
        const uint8_t class_a = check_lower_complex(writer, stream->values[a].type) ? BUSTER_TYPED_CLASS_U64 : element_class;
        const uint8_t class_b = !unary && check_lower_complex(writer, stream->values[b].type) ? BUSTER_TYPED_CLASS_U64 : element_class;
        const uint32_t first = check_lower_temporary(writer, class_a);
        const uint32_t last = unary ? first : check_lower_temporary(writer, class_b);
        check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_COMPLEX, .node = node, .value = first | (uint64_t)last << 32 });
        if (!unary)
        {
            check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, class_b, last);
            check_lower_operand(writer, node, 1, false, check_lower_complex_target(writer, stream->values[b].type, type_b));
        }
        check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, class_a, first);
        check_lower_operand(writer, node, 0, false, check_lower_complex_target(writer, stream->values[a].type, type_a));
    }
    else if (op == CHECK_OP_NEGATIVE || op == CHECK_OP_BIT_NOT)
    {
        check_lower_instruction(writer, op == CHECK_OP_NEGATIVE ? BUSTER_TYPED_NEG : BUSTER_TYPED_NOT, class, 0);
        check_lower_operand(writer, node, 0, false, type_a);
    }
    else if (op == CHECK_OP_NOT || op == CHECK_OP_CONDITION)
    {
        check_lower_truth(writer, type_a, op == CHECK_OP_NOT);
        check_lower_operand(writer, node, 0, false, type_a);
    }
    else if (op == CHECK_OP_COMMA)
    {
        check_lower_operand(writer, node, 1, address, discard ? BUSTER_TYPE_VOID : value->type);
        check_lower_operand(writer, node, 0, false, BUSTER_TYPE_VOID);
    }
    else if (op == CHECK_OP_LOGICAL_AND || op == CHECK_OP_LOGICAL_OR || op == CHECK_OP_CONDITIONAL)
    {
        const bool reuse = op == CHECK_OP_CONDITIONAL && a == b;
        bool truth = false;
        if (check_program_runtime_truth(program, node, a, &truth))
        {
            if (op == CHECK_OP_CONDITIONAL)
            {
                check_lower_operand(writer, node, truth ? 1 : 2, false, value->type);
            }
            else if ((op == CHECK_OP_LOGICAL_AND && !truth) || (op == CHECK_OP_LOGICAL_OR && truth))
            {
                check_lower_instruction(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, truth);
            }
            else
            {
                check_lower_truth(writer, type_b, false);
                check_lower_operand(writer, node, 1, false, type_b);
            }
            if ((!reuse || !truth) && check_program_runtime_effects(program, a))
            {
                check_lower_operand(writer, node, 0, false, BUSTER_TYPE_VOID);
            }
        }
        else
        {
            const uint32_t alternate = check_lower_label(writer);
            const uint32_t end = check_lower_label(writer);
            const uint32_t retained = reuse ? check_lower_temporary(writer, check_lower_class(writer, type_a)) : BUSTER_CHECK_NONE;
            check_lower_mark(writer, end);
            if (op == CHECK_OP_CONDITIONAL)
            {
                check_lower_operand(writer, node, 2, false, value->type);
            }
            else
            {
                check_lower_instruction(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, op == CHECK_OP_LOGICAL_OR);
            }
            check_lower_mark(writer, alternate);
            check_lower_instruction(writer, class == BUSTER_TYPED_CLASS_VOID ? BUSTER_TYPED_JUMP : BUSTER_TYPED_JUMP_VALUE, class, end);
            if (op == CHECK_OP_CONDITIONAL)
            {
                if (reuse)
                {
                    check_lower_convert(writer, type_a, value->type);
                    check_lower_instruction(writer, BUSTER_TYPED_LOCAL, check_lower_class(writer, type_a), retained);
                }
                else
                {
                    check_lower_operand(writer, node, 1, false, value->type);
                }
            }
            else
            {
                check_lower_truth(writer, type_b, false);
                check_lower_operand(writer, node, 1, false, type_b);
            }
            check_lower_instruction(writer, op == CHECK_OP_LOGICAL_OR ? BUSTER_TYPED_BRANCH_TRUE : BUSTER_TYPED_BRANCH_FALSE, BUSTER_TYPED_CLASS_I32, alternate);
            check_lower_truth(writer, type_a, false);
            if (reuse)
            {
                check_lower_instruction(writer, BUSTER_TYPED_LOCAL, check_lower_class(writer, type_a), retained);
                check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, check_lower_class(writer, type_a), retained);
            }
            check_lower_operand(writer, node, 0, false, type_a);
        }
    }
    else if (op == CHECK_OP_CALL)
    {
        check_lower_call(writer, node);
    }
    else if (op == CHECK_OP_VARARGS)
    {
        check_lower_varargs_prepare(writer, node);
    }
    else if ((op == CHECK_OP_ASSIGN || (op >= CHECK_OP_PRE_INCREMENT && op <= CHECK_OP_BIT_XOR_ASSIGN)) &&
             (check_lower_facts(writer, stream->values[a].type) & BUSTER_CHECK_LOWER_TYPE_ATOMIC) != 0)
    {
        if (op != CHECK_OP_ASSIGN && check_lower_vector_of(writer, stream->values[a].type) != 0)
        {
            check_lower_unsupported(writer, "updates of atomic vector values");
        }
        else
        {
            check_lower_atomic_prepare(writer, node, false, discard);
        }
    }
    else if (op == CHECK_OP_ASSIGN && (stream->values[a].flags & BUSTER_CHECK_BIT_FIELD) != 0)
    {
        check_lower_bit_assign(writer, node, discard);
    }
    else if (op >= CHECK_OP_PRE_INCREMENT && op <= CHECK_OP_BIT_XOR_ASSIGN && (stream->values[a].flags & BUSTER_CHECK_BIT_FIELD) != 0)
    {
        check_lower_bit_update(writer, node, discard);
    }
    else if ((op == CHECK_OP_ASSIGN || op == CHECK_OP_INITIALIZE) && aggregate)
    {
        // A struct's or union's assignment copies its bytes, and the destination's address is its value.
        const uint32_t declaration = op == CHECK_OP_INITIALIZE ? writer->initializer_declarations[node] : BUSTER_CHECK_NONE;
        const TypeId destination = op == CHECK_OP_ASSIGN ? stream->values[a].type :
                                   declaration < program->input->token_count ? program->input->declared_types[declaration] : value->type;
        const bool volatile_copy = check_lower_copy_node_volatile(writer, op == CHECK_OP_INITIALIZE ? a : b, destination) ||
                                   (op == CHECK_OP_ASSIGN && (stream->values[a].flags & BUSTER_CHECK_VOLATILE_PART) != 0);
        check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_COPY, .value = type_size(types, value->type), .address = !discard, .volatile_copy = volatile_copy });
        check_lower_operand(writer, node, op == CHECK_OP_INITIALIZE ? 0 : 1, false, value->type);
        if (op == CHECK_OP_ASSIGN)
        {
            check_lower_operand(writer, node, 0, true, 0);
        }
        else if (declaration < program->input->token_count)
        {
            check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_ADDRESS, .node = program->input->objects[declaration] });
        }
        else
        {
            check_lower_fail(writer, CHECK_PROGRAM_INVALID);
        }
    }
    else if (op == CHECK_OP_ASSIGN || op == CHECK_OP_INITIALIZE)
    {
        const uint32_t declaration = op == CHECK_OP_INITIALIZE ? writer->initializer_declarations[node] : BUSTER_CHECK_NONE;
        const uint32_t object = declaration != BUSTER_CHECK_NONE ? program->input->objects[declaration] : graph->ops[a] == CHECK_OP_VALUE ? stream->values[a].object : 0;
        const uint32_t local = object != 0 && object < program->object_count ? writer->local_map[object] : BUSTER_CHECK_NONE;
        const SymbolId symbol = object != 0 && object < program->object_count ? program->object_symbols[object] : 0;
        // A global of a memory class is stored through its address, which goes first (check_lower_memory_class).
        const bool memory = check_lower_memory_class(class);
        if (local != BUSTER_CHECK_NONE || (!memory && symbol != 0))
        {
            check_lower_instruction(writer, local != BUSTER_CHECK_NONE ? BUSTER_TYPED_STORE_LOCAL : BUSTER_TYPED_STORE_GLOBAL, class,
                                     local != BUSTER_CHECK_NONE ? local : symbol);
            check_lower_operand(writer, node, op == CHECK_OP_INITIALIZE ? 0 : 1, false, value->type);
        }
        else if (op == CHECK_OP_ASSIGN)
        {
            check_lower_instruction(writer, BUSTER_TYPED_STORE, class, check_lower_access_value(types, class, value->type));
            check_lower_operand(writer, node, 1, false, value->type);
            check_lower_operand(writer, node, 0, true, 0);
        }
        else if (local != BUSTER_CHECK_NONE || symbol != 0)
        {
            check_lower_instruction(writer, BUSTER_TYPED_STORE, class, check_lower_access_value(types, class, value->type));
            check_lower_operand(writer, node, 0, false, value->type);
            check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_ADDRESS, .node = object });
        }
        else
        {
            check_lower_fail(writer, CHECK_PROGRAM_INVALID);
        }
    }
    else if (op >= CHECK_OP_PRE_INCREMENT && op <= CHECK_OP_BIT_XOR_ASSIGN && (check_lower_complex(writer, stream->values[a].type) || check_lower_complex(writer, type_a)))
    {
        check_lower_complex_update(writer, node);
    }
    else if (op >= CHECK_OP_PRE_INCREMENT && op <= CHECK_OP_BIT_XOR_ASSIGN)
    {
        check_lower_update(writer, node);
    }
    else if (op == CHECK_OP_SUBSCRIPT && (value->flags & BUSTER_CHECK_VECTOR_ELEMENT) != 0)
    {
        // A vector's element, at its index's place in the vector's bytes: the vector's own place when it is an lvalue, or
        // a temporary's that the value is stored into.
        const TypeId vector = stream->values[a].type;
        const CheckValue* base = stream->values + a;
        if (base->category == CHECK_LVALUE && check_lower_copy_volatile(writer, base, 0))
        {
            // Clang reads the whole volatile vector, and writes an element through a whole-vector read/insert/store.
            // A scalar access at the element's address cannot preserve that access width. Rvalues already captured
            // their volatile vector before extraction, so their owned temporary remains supported.
            check_lower_unsupported(writer, "element access through volatile vectors");
        }
        else if (!address && !address_value)
        {
            check_lower_instruction(writer, BUSTER_TYPED_LOAD, class, 0);
        }
        check_lower_instruction(writer, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, type_size(types, value->type));
        check_lower_operand(writer, node, 1, false, BUSTER_TYPE_LONG);
        if (stream->values[a].category == CHECK_LVALUE)
        {
            check_lower_operand(writer, node, 0, true, 0);
        }
        else
        {
            const uint32_t local = check_lower_temporary_of(writer, vector);
            check_lower_instruction(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, local);
            check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_VECTOR, local);
            check_lower_operand(writer, node, 0, false, vector);
        }
    }
    else if (op == CHECK_OP_SUBSCRIPT || op == CHECK_OP_ADD || op == CHECK_OP_SUBTRACT)
    {
        const bool left_pointer = (check_lower_facts(writer, type_a) & BUSTER_CHECK_LOWER_TYPE_POINTER) != 0;
        const bool right_pointer = (check_lower_facts(writer, type_b) & BUSTER_CHECK_LOWER_TYPE_POINTER) != 0;
        if (left_pointer || right_pointer)
        {
            const TypeId pointer_type = left_pointer ? type_a : type_b;
            const uint64_t size = check_lower_stride(writer, pointer_type);
            if (left_pointer && right_pointer && op == CHECK_OP_SUBTRACT)
            {
                check_lower_instruction(writer, BUSTER_TYPED_DIV, BUSTER_TYPED_CLASS_I64, 0);
                if (size != 0)
                {
                    check_lower_instruction(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, size);
                }
                else
                {
                    const TypeId pointee = type_element(types, type_strip_aligned(types, pointer_type));
                    check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_SIZE, .type = pointee });
                }
                check_lower_instruction(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_I64, 0);
                check_lower_instruction(writer, BUSTER_TYPED_SUB, BUSTER_TYPED_CLASS_U64, 0);
                check_lower_operand(writer, node, 1, false, type_b);
                check_lower_operand(writer, node, 0, false, type_a);
            }
            else
            {
                if (op == CHECK_OP_SUBSCRIPT && !address && !address_value)
                {
                    check_lower_instruction(writer, BUSTER_TYPED_LOAD, class, check_lower_access_value(types, class, value->type));
                }
                check_lower_scale(writer, pointer_type, size);
                if (op == CHECK_OP_SUBTRACT)
                {
                    check_lower_instruction(writer, BUSTER_TYPED_NEG, BUSTER_TYPED_CLASS_I64, 0);
                }
                check_lower_operand(writer, node, left_pointer ? 1 : 0, false, BUSTER_TYPE_LONG);
                check_lower_operand(writer, node, left_pointer ? 0 : 1, false, pointer_type);
            }
        }
        else
        {
            check_lower_instruction(writer, check_lower_binary(op), class, 0);
            check_lower_operand(writer, node, 1, false, type_b);
            check_lower_operand(writer, node, 0, false, type_a);
        }
    }
    else if (check_lower_binary(op) != BUSTER_TYPED_NONE)
    {
        const uint8_t kind = check_lower_binary(op);
        check_lower_operation(writer, kind, class);
        check_lower_count(writer, kind, type_b);
        check_lower_operand(writer, node, 1, false, type_b);
        check_lower_operand(writer, node, 0, false, type_a);
    }
    else
    {
        check_lower_unsupported(writer, "this operation");
    }
}

#include "c_check_program_batch.c"

// The token a task's entries take, set as it is popped (check_lower_drain_sources): an expression's node's, a statement's
// own, or for any other task the one it was pushed under (CheckLowerWriter.task_sources, or `source` for one pushed since
// it last changed).
static void check_lower_task_source(CheckLowerWriter* writer, CheckLowerTask task)
{
    const CheckProgram* program = writer->program;
    const uint32_t index = writer->depth;
    const uint32_t token = task.kind == CHECK_LOWER_EXPRESSION ? program->input->graph.tokens[task.node] :
                           task.kind == CHECK_LOWER_CONTROL ? program->controls[writer->body->first + task.node].token :
                           index < writer->settled ? writer->task_sources[index] : writer->source;
    writer->settled = index < writer->settled ? index : writer->settled;
    check_lower_settle(writer);
    writer->source = token;
}

// The tasks' loop, as check_lower_drain and check_lower_drain_sources compile it: with `sources`, each task's token set
// before it runs (check_lower_task_source). A constant in each, so that the plain loop does none of it.
static inline __attribute__((always_inline)) void check_lower_drain_body(CheckLowerWriter* writer, bool sources)
{
    uint64_t expression_nodes = 0;
    while (writer->depth != 0 && writer->output->status == CHECK_PROGRAM_OK)
    {
        const CheckLowerTaskPacked* task = writer->tasks + --writer->depth;
        // The x86 host word has node bytes 0..3, kind 4, flags 5 and instruction class 6.
        const uint8_t* control = (const uint8_t*)&task->control;
        const uint8_t flags = control[5];
        const CheckLowerTaskKind kind = (CheckLowerTaskKind)control[4];
        const uint32_t node = (uint32_t)task->control;
        const TypeId type = (uint32_t)task->payload;
        const uint64_t value = task->payload;
        if (sources)
        {
            check_lower_task_source(writer, check_lower_task_unpack(*task));
        }
        // Six of the tasks' functions are always_inline, as clang inlines them into a loop it compiles once: with a call in
        // each copy it left them out of line, and a compile without -g took 2% more cycles (#426).
        if (__builtin_expect(kind <= CHECK_LOWER_INSTRUCTION, 1))
        {
            if (kind == CHECK_LOWER_EXPRESSION)
            {
                const bool value_node = writer->program->input->graph.ops[node] == CHECK_OP_VALUE;
                const bool simple = !value_node && !sources && !BUSTER_SCALAR_PATH(writer->scalar) && writer->type_templates != NULL && ((flags >> 2) & 1) == 0 &&
                                    check_lower_value(writer, node, (flags & 1) != 0, ((flags >> 1) & 1) != 0);
                const bool plain = !value_node && !simple && !sources && !BUSTER_SCALAR_PATH(writer->scalar) && check_lower_plain(writer, node, (flags & 1) != 0);
                const bool batched = !value_node && !plain && !simple && !sources && !BUSTER_SCALAR_PATH(writer->scalar) && !writer->track && writer->type_templates != NULL && (flags & 5) == 0 &&
                                     check_lower_batch_expression(writer, node);
                if (!batched)
                {
                    if (!simple && !plain)
                    {
                        check_lower_expand(writer, node, (flags & 1) != 0, ((flags >> 1) & 1) != 0, ((flags >> 2) & 1) != 0);
                    }
                    expression_nodes += 1;
                }
            }
            else
            {
                check_lower_emit(writer, (uint8_t)node, control[6], value);
            }
        }
        else if (kind == CHECK_LOWER_CONVERSION)
        {
            check_lower_conversion(writer, node, type);
        }
        else if (kind == CHECK_LOWER_LABEL)
        {
            check_lower_place(writer, node);
        }
        else if (kind == CHECK_LOWER_CONTROL)
        {
            check_lower_control(writer, node);
        }
        else if (kind == CHECK_LOWER_BUILTIN)
        {
            check_lower_builtin_emit(writer, check_lower_task_unpack(*task));
        }
        else if (kind == CHECK_LOWER_BUILTIN_MISC)
        {
            check_lower_miscellaneous_emit(writer, check_lower_task_unpack(*task));
        }
        else if (kind == CHECK_LOWER_BUILTIN_FLOAT || kind == CHECK_LOWER_BUILTIN_FLOAT_STRICT)
        {
            check_lower_floating_emit(writer, check_lower_task_unpack(*task));
        }
        else if (kind == CHECK_LOWER_CONSTANT)
        {
            check_lower_constant(writer, writer->program->stream->values + node);
        }
        else if (kind == CHECK_LOWER_ADDRESS)
        {
            check_lower_address(writer, node, (int64_t)value);
        }
        else if (kind == CHECK_LOWER_QUERY)
        {
            check_lower_constant(writer, &writer->program->stream->queries[node].runtime_value);
        }
        else if (kind == CHECK_LOWER_TERMINATE)
        {
            check_lower_terminate(writer);
        }
        else if (kind == CHECK_LOWER_COPY)
        {
            check_lower_copy(writer, value, (flags & 1) != 0, ((flags >> 3) & 1) != 0);
        }
        else if (kind == CHECK_LOWER_FILL)
        {
            check_lower_fill(writer, writer->program->initializers->plans[node].parts + type, (uint32_t)(value >> 32),
                             check_lower_init_pool(&writer->output->initializers, node, type));
        }
        else if (kind == CHECK_LOWER_CLEAR)
        {
            const CheckInitPart clear = { .kind = CHECK_INIT_CLEAR, .bit_offset = (uint64_t)node * 8, .byte_count = type };
            check_lower_fill(writer, &clear, (uint32_t)(value >> 32), BUSTER_CHECK_NONE);
        }
        else if (kind == CHECK_LOWER_INITIALIZER_COPY)
        {
            check_lower_initializer_copy(writer, node, (uint32_t)(value >> 32), 0);
        }
        else if (kind == CHECK_LOWER_BIT_READ)
        {
            check_lower_bit_read(writer, type);
        }
        else if (kind == CHECK_LOWER_BIT_STORE)
        {
            check_lower_bit_store(writer, type, (uint32_t)(value >> 32), ((flags >> 2) & 1) != 0);
        }
        else if (kind == CHECK_LOWER_VARARGS)
        {
            check_lower_varargs_emit(writer, check_lower_task_unpack(*task));
        }
        else if (kind == CHECK_LOWER_SIZE)
        {
            check_lower_vla_size(writer, type, BUSTER_TYPED_CLASS_I64);
        }
        else if (kind == CHECK_LOWER_ATOMIC)
        {
            check_lower_atomic_emit(writer, node);
        }
        else if (kind >= CHECK_LOWER_COMPLEX)
        {
            // The complex values' tasks, the kinds' last, behind one compare: few functions have them.
            check_lower_complex_task(writer, check_lower_task_unpack(*task));
        }
    }
    writer->expression_nodes += expression_nodes;
}

static void check_lower_drain(CheckLowerWriter* writer)
{
    check_lower_drain_body(writer, false);
}

// The tasks' loop for -g's rows (check_program_lower_sources): a copy of its own (check_lower_drain_body).
static __attribute__((noinline)) void check_lower_drain_sources(CheckLowerWriter* writer)
{
    check_lower_drain_body(writer, true);
}

// An initializer's value, converted to its part's type: its expression, or the object an overloaded function's
// address selected.
static void check_lower_part_value(CheckLowerWriter* writer, const CheckInitPart* write)
{
    const CheckProgram* program = writer->program;
    if (write->overload_candidate != 0)
    {
        const CheckGraph* graph = &program->input->graph;
        const CheckOverloadCandidate* candidate = write->overload_candidate <= graph->overload_candidate_count ?
                                                  graph->overload_candidates + write->overload_candidate - 1 : NULL;
        if (candidate == NULL || write->value.overload_head != 0 || write->value.constant_kind != CHECK_CONSTANT_ADDRESS ||
            write->value.object != candidate->object || write->value.object == 0 || write->value.object >= program->object_count)
        {
            check_lower_fail(writer, CHECK_PROGRAM_INVALID);
        }
        else if ((candidate->flags & BUSTER_CHECK_OVERLOADABLE) != 0)
        {
            check_lower_unsupported(writer, "addresses of overloadable functions");
        }
        else
        {
            check_lower_convert(writer, write->value.type, write->conversion.type);
            check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_ADDRESS, .node = write->value.object, .value = (uint64_t)write->value.offset });
        }
    }
    else
    {
        check_lower_expression(writer, write->node, false, write->conversion.type);
    }
}

// Part `index` of plan `plan` written into frame object `object`, as tasks: its zeros or bytes (check_lower_fill), or
// its value stored at its offset, or for an aggregate copied there.
static void check_lower_object_part(CheckLowerWriter* writer, uint32_t plan, uint32_t index, uint32_t object, TypeId destination)
{
    const TypeTable* types = writer->program->types;
    const CheckInitPart* write = &writer->program->initializers->plans[plan].parts[index];
    const bool aggregate = check_lower_aggregate(writer, write->type);
    const uint8_t class = check_lower_class(writer, write->type);
    // A bit-field's value, or zeros over bits that don't start or end a byte (as unsigned bits of no particular type).
    const bool bits = write->bit_width != 0 && (write->kind == CHECK_INIT_VALUE || write->kind == CHECK_INIT_CLEAR);
    CheckLowerBitField field = {
        .first = write->bit_offset / 8,
        .shift = (uint32_t)(write->bit_offset % 8),
        .width = write->bit_width,
        .class = BUSTER_TYPED_CLASS_U64,
    };
    const bool placed = write->kind == CHECK_INIT_VALUE ? check_lower_bit_place(types, write->bit_offset, write->bit_width, write->type, &field)
                                                        : field.shift + field.width <= 64;
    if (bits && !placed)
    {
        check_lower_unsupported(writer, "bit-fields of this type or spread over more than 8 bytes");
    }
    else if (bits)
    {
        const uint32_t address = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
        check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_BIT_STORE, .type = check_lower_bit_pack(&field), .value = address, .discard = true });
        if (write->kind == CHECK_INIT_CLEAR)
        {
            check_lower_instruction(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 0);
        }
        else
        {
            check_lower_part_value(writer, write);
        }
        check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, address);
        if (field.first != 0)
        {
            check_lower_instruction(writer, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, 1);
            check_lower_instruction(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, field.first);
        }
        check_lower_instruction(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, object);
    }
    else if (write->kind != CHECK_INIT_VALUE)
    {
        check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_FILL, .node = plan, .type = index, .value = object });
    }
    else if (write->bit_offset % 8 != 0 || (!aggregate && (class >= BUSTER_TYPED_CLASSES || class == BUSTER_TYPED_CLASS_VOID)))
    {
        check_lower_unsupported(writer, write->bit_offset % 8 != 0 ? "values that don't start a byte in local objects" :
                                                                     check_lower_type_feature(types, write->type));
    }
    else
    {
        if (aggregate)
        {
            // Initializer plans carry a part's value type; the declared object's volatility still governs its stores.
            const TypeId copy_type = write->type | type_qualifiers(destination);
            check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_COPY, .value = type_size(types, write->type),
                                                     .volatile_copy = write->node < writer->program->stream->count ?
                                                                      check_lower_copy_node_volatile(writer, write->node, copy_type) :
                                                                      check_lower_copy_volatile(writer, &write->value, copy_type) });
        }
        else
        {
            check_lower_instruction(writer, BUSTER_TYPED_SET, class, check_lower_access_value(types, class, write->type));
        }
        check_lower_part_value(writer, write);
        if (write->bit_offset != 0)
        {
            check_lower_instruction(writer, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, 1);
            check_lower_instruction(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, write->bit_offset / 8);
        }
        check_lower_instruction(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, object);
    }
}

static void check_lower_initializer(CheckLowerWriter* writer, CheckControl control)
{
    const CheckProgram* program = writer->program;
    const CheckInitializers* initializers = program->initializers;
    const uint32_t declaration = control.token;
    if (initializers == 0 || control.target >= initializers->count || declaration >= program->input->token_count)
    {
        check_lower_fail(writer, CHECK_PROGRAM_INVALID);
    }
    else
    {
        const CheckInitializer* plan = &initializers->plans[control.target];
        const uint32_t local = writer->local_map[program->input->objects[declaration]];
        const bool object = local != BUSTER_CHECK_NONE && writer->local_sizes[local] != 0;
        const uint32_t pool = object ? check_lower_init_pool(&writer->output->initializers, control.target, BUSTER_CHECK_NONE) : BUSTER_CHECK_NONE;
        const uint8_t class = check_lower_class(writer, plan->type);
        if (local == BUSTER_CHECK_NONE || (!object && class == UINT8_MAX))
        {
            check_lower_unsupported(writer, "initializers of this type");
        }
        if (pool != BUSTER_CHECK_NONE)
        {
            const uint32_t size = writer->output->initializers.items[pool].size;
            if (size < writer->local_sizes[local])
            {
                check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_CLEAR, .node = size, .type = writer->local_sizes[local] - size, .value = local });
            }
            check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_INITIALIZER_COPY, .node = pool, .value = local });
        }
        for (uint32_t part = plan->count; pool == BUSTER_CHECK_NONE && part-- > 0 && writer->output->status == CHECK_PROGRAM_OK;)
        {
            const CheckInitPart* write = &plan->parts[part];
            writer->token = write->token;
            check_lower_source(writer, write->token);
            if (object)
            {
                check_lower_object_part(writer, control.target, part, local, program->input->declared_types[declaration]);
            }
            else if (write->bit_offset != 0 || write->bit_width != 0 || write->kind == CHECK_INIT_BYTES)
            {
                check_lower_unsupported(writer, "this initializer");
            }
            else if (write->kind == CHECK_INIT_CLEAR)
            {
                check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, class, local);
                check_lower_instruction(writer, BUSTER_TYPED_CONST, class, 0);
            }
            else
            {
                check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, class, local);
                check_lower_part_value(writer, write);
            }
        }
        // What the parts leave out is zero (C11 6.7.9p21), which static storage has before any part: an object is
        // cleared first, unless its first part clears it whole.
        const CheckInitPart* first = plan->count != 0 ? plan->parts : NULL;
        const bool cleared = first != NULL && first->kind == CHECK_INIT_CLEAR && first->bit_offset == 0 && first->bit_width == 0 &&
                             object && first->byte_count >= writer->local_sizes[local];
        if (object && !cleared && pool == BUSTER_CHECK_NONE)
        {
            check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_CLEAR, .type = writer->local_sizes[local], .value = local });
        }
    }
}

static void check_lower_case(CheckLowerWriter* writer, const CheckProgramCase* item)
{
    const uint32_t local = writer->switch_locals[item->owner];
    const uint8_t class = check_lower_class(writer, item->type);
    if (class == UINT8_MAX)
    {
        check_lower_unsupported(writer, check_lower_type_feature(writer->program->types, item->type));
    }
    else if (local == BUSTER_CHECK_NONE)
    {
        check_lower_fail(writer, CHECK_PROGRAM_INVALID);
    }
    else
    {
        const bool range = item->lower != item->upper;
        const uint32_t skip = range ? check_lower_label(writer) : BUSTER_CHECK_NONE;
        check_lower_emit(writer, BUSTER_TYPED_LOCAL, class, local);
        const CheckValue lower = { .type = item->type, .category = CHECK_RVALUE, .constant_kind = CHECK_CONSTANT_INTEGER, .integer = item->lower };
        check_lower_constant(writer, &lower);
        check_lower_emit(writer, range ? BUSTER_TYPED_GE : BUSTER_TYPED_EQ, BUSTER_TYPED_CLASS_I32, 0);
        if (range)
        {
            check_lower_emit(writer, BUSTER_TYPED_BRANCH_FALSE, BUSTER_TYPED_CLASS_I32, skip);
            check_lower_emit(writer, BUSTER_TYPED_LOCAL, class, local);
            const CheckValue upper = { .type = item->type, .category = CHECK_RVALUE, .constant_kind = CHECK_CONSTANT_INTEGER, .integer = item->upper };
            check_lower_constant(writer, &upper);
            check_lower_emit(writer, BUSTER_TYPED_LE, BUSTER_TYPED_CLASS_I32, 0);
        }
        check_lower_emit(writer, BUSTER_TYPED_BRANCH_TRUE, BUSTER_TYPED_CLASS_I32, item->target);
        if (range)
        {
            check_lower_place(writer, skip);
        }
    }
}

// The cases sorted by their first values' keys, bottom up, between `cases` and `room`: whichever holds them at the end.
static CheckLowerCaseRange* check_lower_sort_cases(CheckLowerCaseRange* cases, CheckLowerCaseRange* room, uint32_t count)
{
    CheckLowerCaseRange* from = cases;
    CheckLowerCaseRange* to = room;
    for (uint64_t width = 1; width < count; width *= 2)
    {
        for (uint64_t begin = 0; begin < count; begin += width * 2)
        {
            const uint32_t middle = begin + width < count ? (uint32_t)(begin + width) : count;
            const uint32_t end = begin + width * 2 < count ? (uint32_t)(begin + width * 2) : count;
            uint32_t a = (uint32_t)begin;
            uint32_t b = middle;
            for (uint32_t at = (uint32_t)begin; at < end; at += 1)
            {
                const bool take_a = a < middle && (b == end || from[a].lower <= from[b].lower);
                to[at] = take_a ? from[a++] : from[b++];
            }
        }
        CheckLowerCaseRange* swap = from;
        from = to;
        to = swap;
    }
    return from;
}

// Whether the switch at control `index` of body `b`, of class `class`, dispatches through a jump table, as clang -O0 makes
// one: LLVM's SwitchLowering::findJumpTables, which at -O0 takes a switch's whole range or nothing, makes one of four
// clusters or more (runs of cases of consecutive values that go to one label) whose cases hold at least a tenth of the
// range's values (its jump-table density, 10%); here of a class of 32 or 64 bits and a range of at most
// BUSTER_CHECK_LOWER_MAX_TABLE values. The switch's CASE controls follow it, then its JUMP to its default or its end;
// an empty GNU range among them (`case 5 ... 3`) counts for nothing. A table joins `lowered`'s (room for `capacity`),
// its entries and labels from `output` and its labels numbered after the tables' before it. `cases` and `sorted` are room
// for the program's cases. Returns false only when `output` has no room. This is per switch, with the cases sorted: a
// scalar pass over a few cases.
static bool check_lower_switch_table(CheckProgramLowered* lowered, uint32_t capacity, const CheckProgram* program, uint32_t b, uint32_t index, uint8_t class,
                                     CheckLowerCaseRange* cases_room, CheckLowerCaseRange* sorted_room, Arena* output)
{
    const CheckProgramBody* body = program->bodies + b;
    const CheckControl* controls = program->controls + body->first;
    uint32_t count = 0;
    while (index + 1 + count < body->count && controls[index + 1 + count].kind == CHECK_CONTROL_CASE)
    {
        count += 1;
    }
    const uint32_t end = index + 1 + count;
    const bool narrow = class == BUSTER_TYPED_CLASS_I32 || class == BUSTER_TYPED_CLASS_U32;
    const bool wide = class == BUSTER_TYPED_CLASS_I64 || class == BUSTER_TYPED_CLASS_U64;
    const uint64_t mask = narrow ? UINT32_MAX : UINT64_MAX;
    const uint64_t sign = class == BUSTER_TYPED_CLASS_I32 ? UINT64_C(1) << 31 : class == BUSTER_TYPED_CLASS_I64 ? UINT64_C(1) << 63 : 0;
    bool valid = true;
    uint32_t cases = 0;
    if ((narrow || wide) && count >= 4 && end < body->count && controls[end].kind == CHECK_CONTROL_JUMP && lowered->table_count < capacity)
    {
        // A GNU range whose upper value is below its lower one matches nothing, and clang's Sema drops it before LLVM
        // sees the switch (#536's review): it takes no part in the table, its values, its range or its clusters.
        for (uint32_t i = 0; i < count; i += 1)
        {
            const CheckProgramCase* item = program->cases + controls[index + 1 + i].target;
            cases_room[cases] = (CheckLowerCaseRange){
                .lower = ((uint64_t)item->lower & mask) ^ sign,
                .upper = ((uint64_t)item->upper & mask) ^ sign,
                .label = item->target,
            };
            cases += cases_room[cases].upper >= cases_room[cases].lower;
        }
    }
    if (cases >= 4)
    {
        const CheckLowerCaseRange* sorted = check_lower_sort_cases(cases_room, sorted_room, cases);
        unsigned __int128 values = 0;
        uint32_t clusters = 0;
        for (uint32_t i = 0; i < cases; i += 1)
        {
            values += (unsigned __int128)(sorted[i].upper - sorted[i].lower) + 1;
            clusters += i == 0 || sorted[i].label != sorted[i - 1].label || sorted[i].lower != sorted[i - 1].upper + 1;
        }
        const unsigned __int128 range = (unsigned __int128)(sorted[cases - 1].upper - sorted[0].lower) + 1;
        if (clusters >= 4 && range <= BUSTER_CHECK_LOWER_MAX_TABLE && values * 100 >= range * 10)
        {
            const uint32_t length = (uint32_t)range;
            uint32_t* entries = BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, length);
            uint32_t* labels = BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, (uint64_t)cases + 1);
            valid = entries != 0 && labels != 0;
            if (valid)
            {
                // The default's or the end's label first, then one per run of neighbors in value order that go to one
                // place, as they share LLVM's block: a label that two cases apart take gets one for each.
                const uint32_t first = lowered->case_symbol_count;
                uint32_t label_count = 1;
                labels[0] = controls[end].target;
                for (uint32_t v = 0; v < length; v += 1)
                {
                    entries[v] = first;
                }
                for (uint32_t i = 0; i < cases; i += 1)
                {
                    labels[label_count] = sorted[i].label;
                    label_count += i == 0 || sorted[i].label != sorted[i - 1].label;
                    for (uint64_t v = sorted[i].lower - sorted[0].lower; v <= sorted[i].upper - sorted[0].lower; v += 1)
                    {
                        entries[v] = first + label_count - 1;
                    }
                }
                lowered->tables[lowered->table_count] = (CheckLowerTable){
                    .entries = entries,
                    .count = length,
                    .function = b,
                    .control = body->first + index,
                    .labels = labels,
                    .label_count = label_count,
                    .first_label = first,
                    .addresses = { BUSTER_CHECK_NONE, BUSTER_CHECK_NONE },
                    .lower = (sorted[0].lower ^ sign) & mask,
                    .fallback = controls[end].target,
                };
                lowered->table_count += 1;
                lowered->case_symbol_count += label_count;
            }
        }
    }
    return valid;
}

// Every switch's jump table or none (check_lower_switch_table), before any body is lowered, so that whatever lane lowers
// a body, the tables and their labels are numbered in source order (#151), the tables in `lowered` from `output`, the
// cases' room from `scratch`. Only a switch of four cases or more may have one, and the program lists those
// (CheckProgram.switches): the bodies' among them, the two in the controls' order (a selected program's bodies are some
// of the checked program's, with its controls). Returns false when an arena has no room.
static bool check_lower_switch_tables(CheckProgramLowered* lowered, const CheckProgram* program, Arena* output, Arena* scratch)
{
    const uint32_t capacity = program->case_count / 4;
    bool valid = true;
    if (capacity != 0 && program->switch_count != 0 && program->stream != 0)
    {
        lowered->tables = BUSTER_ARENA_PUSH_ARRAY(output, CheckLowerTable, capacity);
        CheckLowerCaseRange* cases = BUSTER_ARENA_PUSH_ARRAY(scratch, CheckLowerCaseRange, program->case_count);
        CheckLowerCaseRange* sorted = BUSTER_ARENA_PUSH_ARRAY(scratch, CheckLowerCaseRange, program->case_count);
        valid = lowered->tables != 0 && cases != 0 && sorted != 0;
        uint32_t s = 0;
        for (uint32_t b = 0; valid && b < program->body_count; b += 1)
        {
            const CheckProgramBody* body = program->bodies + b;
            while (s < program->switch_count && program->switches[s] < body->first)
            {
                s += 1;
            }
            for (; valid && s < program->switch_count && program->switches[s] - body->first < body->count; s += 1)
            {
                const CheckControl control = program->controls[program->switches[s]];
                const uint8_t class = check_lower_type_class(program->types, program->stream->values[control.node].type);
                valid = check_lower_switch_table(lowered, capacity, program, b, program->switches[s] - body->first, class, cases, sorted, output);
            }
        }
    }
    return valid;
}

// The jump table of the SWITCH control at `control`, an index in the program's controls, or BUSTER_CHECK_NONE: a search
// of the `count` tables, which check_lower_switch_tables made in the controls' order.
static uint32_t check_lower_table_of(const CheckLowerTable* tables, uint32_t count, uint32_t control)
{
    uint32_t first = 0;
    uint32_t end = count;
    while (first < end)
    {
        const uint32_t middle = first + (end - first) / 2;
        const bool before = tables[middle].control < control;
        first = before ? middle + 1 : first;
        end = before ? end : middle;
    }
    return first < count && tables[first].control == control ? first : BUSTER_CHECK_NONE;
}

// The index of a jump table's entry: the switch's value (in `local`, of `class`) less the table's lowest, unsigned.
static void check_lower_table_index(CheckLowerWriter* writer, const CheckLowerTable* table, uint32_t local, uint8_t class, uint8_t index_class)
{
    check_lower_emit(writer, BUSTER_TYPED_LOCAL, class, local);
    if (class != index_class)
    {
        check_lower_emit(writer, BUSTER_TYPED_CONVERT, index_class, 0);
    }
    if (table->lower != 0)
    {
        check_lower_emit(writer, BUSTER_TYPED_CONST, index_class, table->lower);
        check_lower_emit(writer, BUSTER_TYPED_SUB, index_class, 0);
    }
}

// Jump table `table`'s dispatch, at its switch's first CASE control, the switch's value in `local` of class `class`: its
// labels, which JUMP_INDIRECT may reach (Tier0Function.label_entries), with their places among the tables' labels in the
// part's label symbols; then to the default (or the switch's end) when the index (check_lower_table_index) is past the
// table's last entry, and otherwise to the table's address (its provisional symbol, which check_lower_finish and the
// emission's merge replace) plus the entry's 32-bit offset of its label from it, as clang -O0's code goes. Its entries
// are the switch statement's, for -g's rows, as clang's code is.
static void check_lower_switch_dispatch(CheckLowerWriter* writer, uint32_t table, uint32_t local, uint8_t class)
{
    CheckLowerTable* item = writer->tables + table;
    CheckProgramLowered* status = writer->output;
    check_lower_source(writer, writer->table_switch);
    for (uint32_t j = 0; j < item->label_count; j += 1)
    {
        if (status->label_symbol_count < writer->label_symbol_capacity && writer->label_entry_count <= writer->program->control_count)
        {
            writer->label_entries[writer->label_entry_count++] = item->labels[j];
            status->label_symbols[status->label_symbol_count++] = BUSTER_CHECK_LOWER_CASE_SYMBOL | (item->first_label + j);
        }
        else
        {
            check_lower_fail(writer, CHECK_PROGRAM_INVALID);
        }
    }
    const uint8_t index_class = class == BUSTER_TYPED_CLASS_I32 || class == BUSTER_TYPED_CLASS_U32 ? BUSTER_TYPED_CLASS_U32 : BUSTER_TYPED_CLASS_U64;
    check_lower_table_index(writer, item, local, class, index_class);
    check_lower_emit(writer, BUSTER_TYPED_CONST, index_class, (uint64_t)item->count - 1);
    check_lower_emit(writer, BUSTER_TYPED_GT, BUSTER_TYPED_CLASS_I32, 0);
    check_lower_emit(writer, BUSTER_TYPED_BRANCH_TRUE, BUSTER_TYPED_CLASS_I32, item->fallback);
    check_lower_emit(writer, BUSTER_TYPED_GLOBAL_ADDRESS, BUSTER_TYPED_CLASS_U64, (uint64_t)writer->provisional_tables + table);
    item->addresses[0] = writer->count - 1;
    check_lower_emit(writer, BUSTER_TYPED_GLOBAL_ADDRESS, BUSTER_TYPED_CLASS_U64, (uint64_t)writer->provisional_tables + table);
    item->addresses[1] = writer->count - 1;
    check_lower_table_index(writer, item, local, class, index_class);
    if (index_class == BUSTER_TYPED_CLASS_U32)
    {
        check_lower_emit(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
    }
    check_lower_emit(writer, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, 4);
    check_lower_emit(writer, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_I32, 0);
    check_lower_emit(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_I64, 0);
    check_lower_emit(writer, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, 1);
    check_lower_emit(writer, BUSTER_TYPED_JUMP_INDIRECT, BUSTER_TYPED_CLASS_U64, 0);
}

// A return of a struct or a union (`node` its value, the bytes' address; or none, for the end of the function): through
// the hidden pointer, its bytes copied where parameter 0 points and that address returned; in registers, the bytes'
// address, which Tier 0's epilogue loads them from (a frame object's of its own at the end); and an empty one's
// nothing, as a void function's. The end's frame object is there only for a function whose end is reached.
static void check_lower_return_aggregate(CheckLowerWriter* writer, uint32_t node)
{
    const TypeTable* types = writer->program->types;
    const uint64_t size = type_size(types, writer->result);
    const uint64_t value_size = type_size(types, type_conversion_operand(types, writer->result));
    const AbiReturn returned = writer->abi_table->results[type_index(writer->result)];
    if (writer->hidden != BUSTER_CHECK_NONE)
    {
        check_lower_instruction(writer, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_U64, 0);
        check_lower_instruction(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, writer->hidden);
        if (node != BUSTER_CHECK_NONE)
        {
            check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_COPY, .value = size, .address = false,
                                                     .volatile_copy = check_lower_copy_node_volatile(writer, node, writer->result) });
            if (size > value_size)
            {
                const uint32_t local = check_lower_object(writer, size, type_alignment_log2(types, writer->result));
                check_lower_instruction(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, local);
                check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_COPY, .value = value_size,
                                                         .volatile_copy = check_lower_copy_node_volatile(writer, node, writer->result) });
                check_lower_expression(writer, node, false, 0);
                check_lower_instruction(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, local);
                check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_CLEAR, .type = (TypeId)size, .value = local });
            }
            else
            {
                check_lower_expression(writer, node, false, 0);
            }
            check_lower_instruction(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, writer->hidden);
        }
    }
    else if (returned.kind == BUSTER_ABI_RESULT_REGISTERS)
    {
        check_lower_instruction(writer, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_U64, 0);
        if (node != BUSTER_CHECK_NONE)
        {
            check_lower_expression(writer, node, false, 0);
        }
        else
        {
            const uint32_t object = check_lower_object(writer, size, type_alignment_log2(types, writer->result));
            check_lower_instruction(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, object);
        }
    }
    else
    {
        check_lower_instruction(writer, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
        if (node != BUSTER_CHECK_NONE)
        {
            check_lower_expression(writer, node, false, BUSTER_TYPE_VOID);
        }
    }
}

// A block's stack position, saved into a local of its own before its first variable-length array
// (CHECK_CONTROL_STACK_SAVE, once per block in the control stream), and restored from it at the block's end and before
// the jumps out of it (CHECK_CONTROL_STACK_RESTORE, which come after the save). `target` is the block's closing token.
static void check_lower_stack(CheckLowerWriter* writer, CheckControl control)
{
    const bool save = control.kind == CHECK_CONTROL_STACK_SAVE;
    const bool block = control.target < writer->program->input->token_count;
    if (save && block)
    {
        writer->save_locals[control.target] = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
    }
    const uint32_t local = block ? writer->save_locals[control.target] : BUSTER_CHECK_NONE;
    if (local >= writer->local_count)
    {
        check_lower_fail(writer, CHECK_PROGRAM_INVALID);
    }
    else if (save)
    {
        check_lower_emit(writer, BUSTER_TYPED_STACK_SAVE, BUSTER_TYPED_CLASS_U64, 0);
        check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, local);
    }
    else
    {
        check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, local);
        check_lower_emit(writer, BUSTER_TYPED_STACK_RESTORE, BUSTER_TYPED_CLASS_U64, 0);
    }
}

// A variable-length array's object at its declaration (CHECK_CONTROL_ALLOCATE): its size from its type's extents,
// taken from the stack (ALLOCA) into its local. Aligned past 16 bytes, whose multiple ALLOCA's address is, it takes its
// alignment less 16 more and its address rounds up to its alignment, (address + alignment - 1) & -alignment, where
// clang's code rounds rsp down. Refused in a block whose end has no control entry of its own (a for statement's
// declaration, a statement expression's block), or initialized (C23's = {}).
static void check_lower_allocate(CheckLowerWriter* writer, CheckControl control)
{
    const CheckProgram* program = writer->program;
    const CheckInput* input = program->input;
    const uint32_t declaration = control.token;
    const uint32_t object = declaration < input->token_count ? input->objects[declaration] : 0;
    const uint32_t local = object != 0 && object < program->object_count ? writer->local_map[object] : BUSTER_CHECK_NONE;
    const uint8_t role = control.target < input->token_count ? program->parser->tree.roles[control.target] : 0;
    const bool block = (role == PARSE_ROLE_CLOSE_BLOCK || role == PARSE_ROLE_CLOSE_BODY) && input->expression_regions[control.target] == 0;
    const TypeId type = local != BUSTER_CHECK_NONE ? input->declared_types[declaration] : 0;
    const CheckInputAttributes* attributes = local != BUSTER_CHECK_NONE ? input->attributes[declaration] : 0;
    // Its type's alignment, or its declaration's attribute's where larger (the log2 plus one, 0 for none).
    const uint32_t declared_alignment = attributes != 0 && attributes->alignment_log2 != 0 ? attributes->alignment_log2 - 1u : 0;
    const uint32_t type_alignment = type != 0 ? type_alignment_log2(program->types, type) : 0;
    const uint32_t alignment_log2 = declared_alignment > type_alignment ? declared_alignment : type_alignment;
    const uint64_t alignment = UINT64_C(1) << (alignment_log2 & 63);
    const bool initialized = program->initializers != 0 && check_initializers_plan_index(program->initializers, object - 1) != 0;
    if (local >= writer->local_count || writer->indirect[local] == BUSTER_CHECK_LOWER_VLA_NONE)
    {
        check_lower_fail(writer, CHECK_PROGRAM_INVALID);
    }
    else if (!block)
    {
        check_lower_unsupported(writer, "variable-length arrays declared in a for statement or in a statement expression's block");
    }
    else if (initialized)
    {
        check_lower_unsupported(writer, "initialized variable-length arrays");
    }
    else
    {
        check_lower_vla_size(writer, type, BUSTER_TYPED_CLASS_U64);
        if (alignment_log2 > 4)
        {
            check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, alignment - 16);
            check_lower_emit(writer, BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0);
        }
        check_lower_emit(writer, BUSTER_TYPED_ALLOCA, BUSTER_TYPED_CLASS_U64, 0);
        if (alignment_log2 > 4)
        {
            check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, alignment - 1);
            check_lower_emit(writer, BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0);
            check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, ~(alignment - 1));
            check_lower_emit(writer, BUSTER_TYPED_AND, BUSTER_TYPED_CLASS_U64, 0);
        }
        check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, local);
        writer->indirect[local] = BUSTER_CHECK_LOWER_VLA_ALLOCATED;
    }
}

// Pushes, for after the tasks pushed after it, the address of the slot at `offset` in asm operand block `object`.
static void check_lower_asm_slot(CheckLowerWriter* writer, uint32_t object, uint32_t offset)
{
    if (offset != 0)
    {
        check_lower_instruction(writer, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, 1);
        check_lower_instruction(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, offset);
    }
    check_lower_instruction(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, object);
}

// An asm statement (CHECK_CONTROL_ASM), as check_program_asm_plan planned it, in clang's order: each output's address,
// with a `+` operand's value loaded through it, then each input's value (a memory input's address). With a helper,
// they go into its operand block, a frame object, whose address the helper's call takes; a template of no
// instructions has no helper, and then its operands are evaluated alone: a matching input's value is its output's, a
// `+` output keeps its own (stored again, as clang's code stores it), and any other output keeps its value, which the
// template doesn't write. The tasks are pushed last first.
static void check_lower_asm(CheckLowerWriter* writer, CheckControl control)
{
    const CheckProgram* program = writer->program;
    const CheckInput* input = program->input;
    const TypeTable* types = program->types;
    const CheckProgramAsms* asms = &writer->output->asms;
    const CheckProgramAsm* plan = asms->plans != 0 && control.target < input->asm_count ? asms->plans + control.target : 0;
    const CheckInputAsm* statement = plan != 0 ? input->asm_statements + control.target : 0;
    const uint32_t count = statement != 0 ? statement->output_count + statement->input_count : 0;
    if (plan == 0 || !plan->planned)
    {
        check_lower_fail(writer, CHECK_PROGRAM_INVALID);
    }
    else if (plan->reason != NULL)
    {
        check_lower_unsupported(writer, plan->reason);
    }
    else if (count > BUSTER_CHECK_ASM_OPERANDS)
    {
        check_lower_fail(writer, CHECK_PROGRAM_INVALID);
    }
    else if (plan->helper != BUSTER_CHECK_NONE)
    {
        const CheckProgramHelper* helper = writer->output->helpers.items + plan->helper;
        const uint32_t object = plan->block != 0 ? check_lower_object(writer, plan->block, 3) : BUSTER_CHECK_NONE;
        const TypeId argument = BUSTER_TYPE_ULONG;
        const uint32_t call = writer->output->status == CHECK_PROGRAM_OK ? check_lower_native_call(writer, helper->symbol, BUSTER_TYPE_VOID, &argument, plan->block != 0 ? 1 : 0) : 0;
        check_lower_instruction(writer, BUSTER_TYPED_CALL, BUSTER_TYPED_CLASS_VOID, call);
        if (plan->block != 0)
        {
            check_lower_instruction(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, object);
        }
        for (uint32_t k = count; k-- > 0 && writer->output->status == CHECK_PROGRAM_OK;)
        {
            const CheckAsmPlace* place = plan->places + k;
            const uint32_t node = statement->operands[k].node;
            const uint8_t class = check_lower_class(writer, place->type);
            if (place->written && place->read && place->kind != BUSTER_CHECK_ASM_MEMORY)
            {
                // A `+` register operand's value, through its address the slot holds.
                check_lower_instruction(writer, BUSTER_TYPED_SET, class, 0);
                check_lower_instruction(writer, BUSTER_TYPED_LOAD, class, 0);
                check_lower_instruction(writer, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_U64, 0);
                check_lower_asm_slot(writer, object, place->slot);
                check_lower_asm_slot(writer, object, place->value);
            }
            if (place->written || place->kind == BUSTER_CHECK_ASM_MEMORY)
            {
                check_lower_instruction(writer, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_U64, 0);
                check_lower_expression(writer, node, true, 0);
                check_lower_asm_slot(writer, object, place->slot);
            }
            else if (place->kind != BUSTER_CHECK_ASM_CONSTANT)
            {
                check_lower_instruction(writer, BUSTER_TYPED_SET, class, 0);
                check_lower_expression(writer, node, false, place->type);
                check_lower_asm_slot(writer, object, place->slot);
            }
        }
    }
    else
    {
        // Each output a matching input sets keeps its address in a temporary till the input's value comes.
        uint32_t addresses[BUSTER_CHECK_ASM_OPERANDS];
        for (uint32_t k = 0; k < count; k += 1)
        {
            const uint32_t tied = plan->places[k].tied;
            if (tied < statement->output_count && writer->output->status == CHECK_PROGRAM_OK)
            {
                addresses[tied] = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
            }
        }
        for (uint32_t k = count; k-- > 0 && writer->output->status == CHECK_PROGRAM_OK;)
        {
            const CheckAsmPlace* place = plan->places + k;
            const uint32_t node = statement->operands[k].node;
            bool matched = false;
            for (uint32_t i = statement->output_count; i < count; i += 1)
            {
                matched |= plan->places[i].tied == k;
            }
            if (place->tied < statement->output_count)
            {
                // The input in its output's register, as GCC and clang place it (check_asm_tied_type): a narrower integer
                // zero-extended from its own width, so "=r"(long) : "0"(-1) is 4294967295.
                const CheckAsmPlace* output = plan->places + place->tied;
                const uint8_t output_class = check_lower_class(writer, output->type);
                const TypeId placed = check_asm_tied_type(types, place->type, output->type);
                check_lower_instruction(writer, BUSTER_TYPED_SET, output_class, 0);
                if (placed != output->type)
                {
                    check_lower_instruction(writer, BUSTER_TYPED_CONVERT, output_class, 0);
                }
                check_lower_expression(writer, node, false, placed);
                check_lower_instruction(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, addresses[place->tied]);
            }
            else if (place->written && matched)
            {
                check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, addresses[k]);
                check_lower_expression(writer, node, true, 0);
            }
            else if (place->written && place->read && place->kind != BUSTER_CHECK_ASM_MEMORY)
            {
                const uint8_t class = check_lower_class(writer, place->type);
                check_lower_instruction(writer, BUSTER_TYPED_SET, class, 0);
                check_lower_instruction(writer, BUSTER_TYPED_LOAD, class, 0);
                check_lower_instruction(writer, BUSTER_TYPED_DUP, BUSTER_TYPED_CLASS_U64, 0);
                check_lower_expression(writer, node, true, 0);
            }
            else if (place->written || place->kind == BUSTER_CHECK_ASM_MEMORY)
            {
                check_lower_instruction(writer, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_U64, 0);
                check_lower_expression(writer, node, true, 0);
            }
            else if (place->kind != BUSTER_CHECK_ASM_CONSTANT)
            {
                check_lower_instruction(writer, BUSTER_TYPED_DISCARD, check_lower_class(writer, place->type), 0);
                check_lower_expression(writer, node, false, place->type);
            }
        }
    }
}

static void check_lower_control(CheckLowerWriter* writer, uint32_t index)
{
    const CheckProgram* program = writer->program;
    const CheckControl control = program->controls[writer->body->first + index];
    check_lower_place(writer, index);
    writer->token = control.token;
    if (control.kind == CHECK_CONTROL_JUMP && (writer->table_current == BUSTER_CHECK_NONE || control.token != writer->table_switch))
    {
        check_lower_emit(writer, BUSTER_TYPED_JUMP, BUSTER_TYPED_CLASS_VOID, control.target);
    }
    else if (control.kind == CHECK_CONTROL_JUMP)
    {
        // A table switch's jump to its default or its end, after its cases: the dispatch goes there itself.
        writer->table_current = BUSTER_CHECK_NONE;
    }
    else if (control.kind == CHECK_CONTROL_LABEL)
    {
        // A label whose address the selected program takes has its symbol, which the emission defines at its record.
        const SymbolId symbol = program->object_symbols[control.token + 1];
        if (symbol != 0 && writer->output->label_symbol_count < writer->label_symbol_capacity)
        {
            writer->label_entries[writer->label_entry_count++] = index;
            writer->output->label_symbols[writer->output->label_symbol_count++] = symbol;
        }
        else if (symbol != 0)
        {
            check_lower_fail(writer, CHECK_PROGRAM_INVALID);
        }
    }
    else if (control.kind == CHECK_CONTROL_JUMP_INDIRECT)
    {
        check_lower_instruction(writer, BUSTER_TYPED_JUMP_INDIRECT, BUSTER_TYPED_CLASS_U64, 0);
        check_lower_expression(writer, control.node, false, 0);
    }
    else if (control.kind == CHECK_CONTROL_INITIALIZE)
    {
        check_lower_initializer(writer, control);
    }
    else if (control.kind == CHECK_CONTROL_VLA)
    {
        check_lower_expression(writer, control.node, false, BUSTER_TYPE_VOID);
    }
    else if (control.kind == CHECK_CONTROL_STACK_SAVE || control.kind == CHECK_CONTROL_STACK_RESTORE)
    {
        check_lower_stack(writer, control);
    }
    else if (control.kind == CHECK_CONTROL_ALLOCATE)
    {
        check_lower_allocate(writer, control);
    }
    else if (control.kind == CHECK_CONTROL_ASM)
    {
        check_lower_asm(writer, control);
    }
    else if (control.kind == CHECK_CONTROL_SWITCH)
    {
        const TypeId type = program->stream->values[control.node].type;
        const uint8_t class = check_lower_class(writer, type);
        const uint32_t local = check_lower_temporary(writer, class);
        writer->switch_locals[control.token] = local;
        check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, class, local);
        check_lower_expression(writer, control.node, false, type);
        // Its CASE controls come next: through its jump table (check_lower_switch_tables decided it), its first one
        // writes the dispatch, and none compares.
        writer->table_switch = control.token;
        writer->table_current = check_lower_table_of(writer->tables, writer->table_count, writer->body->first + index);
        writer->table_pending = writer->table_current != BUSTER_CHECK_NONE;
    }
    else if (control.kind == CHECK_CONTROL_CASE)
    {
        const CheckProgramCase* item = program->cases + control.target;
        if (writer->table_current == BUSTER_CHECK_NONE || item->owner != writer->table_switch)
        {
            check_lower_case(writer, item);
        }
        else if (writer->table_pending)
        {
            check_lower_switch_dispatch(writer, writer->table_current, writer->switch_locals[item->owner], check_lower_class(writer, item->type));
            writer->table_pending = false;
        }
    }
    else if (control.kind == CHECK_CONTROL_RETURN && check_lower_aggregate(writer, writer->result))
    {
        check_lower_return_aggregate(writer, control.node);
    }
    else if (control.kind != CHECK_CONTROL_END)
    {
        if (control.kind == CHECK_CONTROL_RETURN && writer->result_object != BUSTER_CHECK_NONE)
        {
            // A 128-bit integer's or a vector's: set into the result object, whose address Tier 0 returns
            // (Tier0Function.result_size).
            check_lower_instruction(writer, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_U64, 0);
            check_lower_instruction(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, writer->result_object);
            check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, check_lower_class(writer, writer->result), writer->result_object);
        }
        else if (control.kind == CHECK_CONTROL_RETURN)
        {
            check_lower_instruction(writer, BUSTER_TYPED_RETURN, check_lower_class(writer, writer->result), 0);
        }
        else if (control.kind == CHECK_CONTROL_BRANCH_FALSE || control.kind == CHECK_CONTROL_BRANCH_TRUE)
        {
            check_lower_instruction(writer, control.kind == CHECK_CONTROL_BRANCH_FALSE ? BUSTER_TYPED_BRANCH_FALSE : BUSTER_TYPED_BRANCH_TRUE,
                                    BUSTER_TYPED_CLASS_I32, control.target);
        }
        if (control.node != BUSTER_CHECK_NONE)
        {
            check_lower_expression(writer, control.node, false, control.kind == CHECK_CONTROL_EVALUATE ? BUSTER_TYPE_VOID : 0);
        }
    }
}

static uint32_t check_lower_runtime_symbols(CheckProgramLowered* lowered, const CheckProgram* program)
{
    const CheckInput* input = program->input;
    const CheckStream* stream = program->stream;
    uint32_t calls = 0;
    uint32_t floating = 0;
    uint32_t query = 0;
    for (uint32_t i = 0; i < program->builtin_count && lowered->status == CHECK_PROGRAM_OK; i += 1)
    {
        const uint32_t node = program->builtin_nodes[i];
        while (floating < stream->floating_count && stream->floating[floating].node < node)
        {
            floating += 1;
        }
        while (query < stream->query_count && stream->queries[query].node < node)
        {
            query += 1;
        }
        if (input->graph.ops[node] == CHECK_OP_BUILTIN_QUERY)
        {
            const CheckBuiltinQueryEffect* effect = query < stream->query_count && stream->queries[query].node == node ? stream->queries + query : NULL;
            const uint32_t arguments = input->graph.argument_counts[node];
            const uint32_t mask = arguments < 32 ? (UINT32_C(1) << arguments) - 1 : UINT32_MAX;
            const bool constant = input->graph.members[node] == CBUILTIN_CONSTANT_P;
            if (effect == NULL)
            {
                lowered->status = CHECK_PROGRAM_INVALID;
            }
            else if (!check_program_query_known(program, node, effect))
            {
                lowered->status = CHECK_PROGRAM_UNSUPPORTED;
                lowered->reason = "this constant query at run time";
            }
            else if ((effect->evaluated_operands & ~mask) != 0 ||
                     (constant && (arguments != 1 || effect->evaluated_operands != (uint32_t)(effect->runtime_plan == CHECK_BUILTIN_QUERY_RUNTIME_EVALUATE_KNOWN))))
            {
                lowered->status = CHECK_PROGRAM_INVALID;
            }
            if (lowered->status != CHECK_PROGRAM_OK)
            {
                lowered->token = input->graph.tokens[node];
            }
        }
        if (floating < stream->floating_count && stream->floating[floating].node == node && stream->floating[floating].may_set_errno)
        {
            const TypeId pointer = stream->floating[floating].errno_pointer_type;
            if (pointer == 0 || type_kind(program->types, pointer) != BUSTER_TYPE_KIND_POINTER ||
                type_element(program->types, pointer) != BUSTER_TYPE_INT ||
                (lowered->errno_pointer_type != 0 && lowered->errno_pointer_type != pointer))
            {
                lowered->status = CHECK_PROGRAM_INVALID;
                lowered->token = input->graph.tokens[node];
            }
            else
            {
                lowered->errno_pointer_type = pointer;
                calls += 1;
            }
        }
    }
    if (calls != 0 && lowered->status == CHECK_PROGRAM_OK)
    {
        // Linux x86-64 libc's errno.h declares __errno_location as int *(void). This is an explicit target-runtime
        // dependency, not a fabricated C declaration or guessed TLS address. Source symbols keep their own IDs.
        static const char name[] = "__errno_location";
        const Parser* parser = program->parser;
        const InternTable* identifiers = program->identifiers;
        if (parser == NULL || identifiers == NULL)
        {
            lowered->status = CHECK_PROGRAM_INVALID;
        }
        for (uint32_t declaration = 0; declaration < input->token_count && lowered->status == CHECK_PROGRAM_OK; declaration += 1)
        {
            const uint32_t identifier = parser->tokens.names[declaration];
            if (input->declarations[declaration] == declaration && input->linkage[declaration] != 0 && identifier < identifiers->count &&
                intern_length(identifiers, identifier) == sizeof(name) - 1 && memcmp(intern_bytes(identifiers, identifier), name, sizeof(name) - 1) == 0)
            {
                const TypeId type = type_strip_aligned(program->types, input->declared_types[declaration]);
                uint32_t parameters = 0;
                const bool function = type_kind(program->types, type) == BUSTER_TYPE_KIND_FUNCTION;
                if (function)
                {
                    type_parameters(program->types, type, &parameters);
                }
                if (!function || parameters != 0 || (program->types->types.flags[type_index(type)] & BUSTER_TYPE_FLAG_VARIADIC) != 0 ||
                    type_unqualified(type_return(program->types, type)) != lowered->errno_pointer_type)
                {
                    lowered->status = CHECK_PROGRAM_UNSUPPORTED;
                    lowered->reason = "a declaration of __errno_location other than int *(void)";
                    lowered->token = declaration;
                }
                else if (input->linkage[declaration] == 2)
                {
                    const uint32_t object = input->objects[declaration];
                    if (object != 0 && object < program->object_count && program->object_symbols[object] != 0)
                    {
                        lowered->errno_symbol = program->object_symbols[object];
                    }
                }
            }
        }
        if (lowered->status == CHECK_PROGRAM_OK && lowered->errno_symbol == 0)
        {
            const uint64_t symbol = (uint64_t)program->symbol_count + lowered->helpers.count + lowered->initializers.count + 1;
            if (symbol >= UINT32_MAX)
            {
                lowered->status = CHECK_PROGRAM_RESOURCE;
            }
            else
            {
                lowered->errno_symbol = (SymbolId)symbol;
                lowered->runtime_order[lowered->runtime_symbol_count++] = CHECK_LOWER_RUNTIME_ERRNO;
            }
        }
        lowered->runtime_symbols[CHECK_LOWER_RUNTIME_ERRNO] = lowered->errno_symbol;
    }
    return calls;
}

// The lowering's program-wide state, which check_lower_prepare makes before any body is lowered and the writers of the
// bodies' parts read (check_lower_part; #151: check_program_emit_lanes lowers a part of the bodies on each of its lanes,
// with a writer each). The tables a body writes, indexed by its tokens and objects (local_map, switch_locals and
// save_locals), are shared too: no two bodies write the same entry, and a body puts local_map's entries back to none
// before it ends.
typedef struct CheckLowerShared CheckLowerShared;
struct CheckLowerShared
{
    const CheckProgram* program;
    CheckProgramLowered* lowered;
    // Every type's classification, made before the bodies: a body's struct or union result decides its returns
    // (check_lower_by_value), and a variadic definition's named registers its va_start.
    AbiTable table;
    // Every type's class and facts (check_lower_classify_types), which the bodies ask of their nodes' types: `type_count`
    // of each, one per type of the program's table, which the lowering doesn't grow.
    uint8_t* type_classes;
    uint8_t* type_facts;
    uint32_t* type_templates;
    uint32_t type_count;
    uint32_t* initializer_declarations;
    uint32_t* local_map;
    uint32_t* switch_locals;
    uint32_t* vla_identities;
    uint32_t* save_locals;
    uint32_t* local_first;
    uint32_t* local_next;
    // Each body's own signature, its index in its part's batch, and whether the body starts its variadic list.
    uint32_t* signatures;
    uint8_t* incoming;
    uint32_t capacity;
    uint32_t local_capacity;
    uint32_t abi_capacity;
    uint32_t argument_capacity;
    uint32_t x87_constant_capacity;
    uint32_t label_symbol_capacity;
    uint32_t builtin_count;
    uint32_t atomic_capacity;
    uint32_t integer_builtin_count;
    bool terminal;
    bool scalar;
    // The runtime's functions: each one's symbol where the unit declares it (check_lower_runtime), 0 for none, and the
    // first of the provisional symbols the lowering's own take until check_lower_finish numbers them, the helpers' end.
    SymbolId declared_runtimes[CHECK_LOWER_RUNTIMES];
    SymbolId provisional_runtimes;
    // The first of the jump tables' provisional symbols (check_lower_switch_tables decided the tables), after the runtime
    // functions': table t's address is provisional_tables + t while the bodies compile.
    SymbolId provisional_tables;
};

// The steps of a part's lowering (check_lower_part) that can fail, in the order source order's lowering takes them for
// the whole program: every body's walk; the locations' room (with the label symbols' check before it, which
// check_lower_finish makes); every call's convention; and each body's parameters and tier0_validate's check of it.
// check_lower_finish reports the first failure in that order, the first part's in a step.
typedef enum CheckLowerStage
{
    CHECK_LOWER_STAGE_NONE,
    CHECK_LOWER_STAGE_BODY,
    CHECK_LOWER_STAGE_ROOM,
    CHECK_LOWER_STAGE_CONVENTION,
    CHECK_LOWER_STAGE_FRAME,
} CheckLowerStage;

// A part of the bodies, `first` to `end`, lowered in order by one writer (check_lower_part): its first failure's step,
// status, token, entry and reason, as check_program_lower reports them; its writer's token after its last body (the one a
// failure source order's lowering finds after the last body is at); the entries it wrote; its bodies' label symbols, in
// their order, from its writer's room for them; and the runtime functions its bodies call first, in the order they do.
typedef struct CheckLowerPart CheckLowerPart;
struct CheckLowerPart
{
    uint32_t first;
    uint32_t end;
    CheckLowerStage stage;
    CheckProgramStatus status;
    uint32_t token;
    uint32_t backend_node;
    const char* reason;
    uint32_t final_token;
    uint64_t entries;
    uint64_t expression_nodes;
    uint64_t batch_nodes;
    uint64_t classified_nodes;
    const SymbolId* label_symbols;
    uint32_t label_symbol_count;
    uint32_t runtime_count;
    uint8_t runtimes[CHECK_LOWER_RUNTIMES];
};

// Leaves `lowered` with its failure alone, `output` as it was at `mark` (check_program_lower's contract for a failure): a
// failure with no status of its own is a resource's.
static void check_lower_failed(CheckProgramLowered* lowered, Arena* output, uint64_t mark)
{
    const CheckProgramStatus status = lowered->status == CHECK_PROGRAM_OK ? CHECK_PROGRAM_RESOURCE : lowered->status;
    const uint32_t token = lowered->token;
    const uint32_t backend_node = lowered->backend_node;
    const char* reason = lowered->reason;
    arena_pop_to(output, mark);
    *lowered = (CheckProgramLowered){ .status = status, .token = token, .backend_node = backend_node, .reason = reason };
}

// What the lowering of every body shares (CheckLowerShared), its tables from `scratch`, and the program's lowering's
// columns from `output`: its helpers, its label symbols' room, its functions and, with `sources`, their entries' tokens.
// Returns false with `lowered`'s failure (its status, or OK for a resource's), and leaves the arenas' pushes to the
// caller.
static bool check_lower_prepare(CheckLowerShared* shared, CheckProgramLowered* lowered, const CheckProgram* program, bool scalar, bool sources,
                                Arena* output, Arena* scratch)
{
    (void)scalar;

    *lowered = (CheckProgramLowered){ .token = BUSTER_CHECK_NONE };
    *shared = (CheckLowerShared){ .program = program, .lowered = lowered, .scalar = BUSTER_SCALAR_PATH(scalar) };
    const CheckInput* input = program->input;
    const TypeTable* types = program->types;
    const uint32_t count = input != 0 ? input->token_count : 0;
    const uint32_t nodes = input != 0 ? input->graph.count : 0;
    uint32_t builtin_count = 0;
    uint32_t atomic_capacity = 0;
    uint32_t integer_builtin_count = 0;
    for (uint32_t node = 0; node < nodes; node += 1)
    {
        integer_builtin_count += input->graph.ops[node] == CHECK_OP_BUILTIN_UNARY || input->graph.ops[node] == CHECK_OP_BUILTIN_OVERFLOW;
        builtin_count += input->graph.ops[node] == CHECK_OP_BUILTIN_UNARY || input->graph.ops[node] == CHECK_OP_BUILTIN_OVERFLOW ||
                         input->graph.ops[node] == CHECK_OP_BUILTIN_MISC || input->graph.ops[node] == CHECK_OP_BUILTIN_FLOAT;
        const CheckOp op = input->graph.ops[node];
        const uint32_t left = input->graph.operand0[node];
        const bool update = op == CHECK_OP_ASSIGN || (op >= CHECK_OP_PRE_INCREMENT && op <= CHECK_OP_BIT_XOR_ASSIGN);
        const TypeId type = program->stream != NULL && program->stream->values != NULL ? program->stream->values[node].type : 0;
        const TypeId target = update && left < node && program->stream != NULL ? program->stream->values[left].type : 0;
        atomic_capacity += op == CHECK_OP_BUILTIN_ATOMIC ||
                           (type != 0 && type_kind(types, type_strip_aligned(types, type)) == BUSTER_TYPE_KIND_ATOMIC) ||
                           (target != 0 && type_kind(types, type_strip_aligned(types, target)) == BUSTER_TYPE_KIND_ATOMIC);
    }
    // Each varargs builtin is a recipe with up to four locals of its own besides its temporaries, and each body has up to
    // three more (the hidden pointer and the anchors) and an end's frame object. A program the checker refused may have
    // no stream, and is refused below without reading one.
    const uint64_t varargs = program->status == CHECK_PROGRAM_OK && program->stream != 0 ? program->stream->varargs_count : 0;
    // Each long double constant a function's table holds comes from a node of that type (a constant's, or ++ and --'s
    // 1.0), so no function has more than the program has nodes, besides +0.0: a bound without a pass over the nodes'
    // types, which every program would pay for (#423's review). A classification under FENV_ACCESS takes two, infinity
    // and the smallest normal, and the nodes of its six operands none. Only a function with long double values writes
    // the table.
    const uint64_t x87_capacity64 = (uint64_t)nodes + 1;
    // An asm statement's lowering (check_lower_asm) writes its operand block, up to 16 entries an operand besides its
    // expression's, and its call, with its block and a temporary an operand among the locals.
    const uint64_t asm_statements = program->status == CHECK_PROGRAM_OK && input != 0 ? input->asm_count : 0;
    const uint64_t asm_operands = asm_statements != 0 && program->asms != 0 ? program->asms->count : 0;
    const uint64_t capacity64 = (uint64_t)nodes * 32 + (uint64_t)program->control_count * 4 + (uint64_t)builtin_count * BUSTER_CBUILTIN_LOWER_ENTRIES +
                                varargs * BUSTER_CBUILTIN_LOWER_ENTRIES + asm_operands * 16 + asm_statements * 4 + 32;
    // A node's lowering takes up to two temporaries, and a 128-bit integer's four more: a division's two operands, a
    // conversion's or an argument's object, and a call's result object. The bounds are address space: an arena's push
    // touches none of it.
    // Each lane's writer resets its integer pool per body and copies completed local columns to output, so one pool
    // bounds the writer's reusable local room regardless of the number of bodies.
    const uint64_t recipe_pools = integer_builtin_count != 0 ? 1 : 0;
    const uint64_t local_capacity64 = (uint64_t)count + (uint64_t)nodes * 6 + ((uint64_t)builtin_count - integer_builtin_count + recipe_pools) * BUSTER_CBUILTIN_LOWER_TEMPORARIES +
                                      varargs * (BUSTER_CBUILTIN_LOWER_TEMPORARIES + 4) + (uint64_t)program->body_count * 5 + asm_operands + asm_statements + 1;
    bool valid = output != scratch && program->status == CHECK_PROGRAM_OK && capacity64 <= UINT32_MAX && local_capacity64 <= UINT32_MAX &&
                 x87_capacity64 <= UINT32_MAX;
    lowered->status = valid ? CHECK_PROGRAM_OK : CHECK_PROGRAM_INVALID;
    if (valid && !check_program_helpers(&lowered->helpers, program, output, scratch))
    {
        lowered->status = lowered->helpers.status;
        lowered->token = lowered->helpers.token;
        lowered->reason = lowered->helpers.reason;
        valid = false;
    }
    // The asm statements' helpers after the builtins', before the runtime's symbols, which come after every helper's.
    if (valid && !check_program_asm_plan(&lowered->asms, &lowered->helpers, program, BUSTER_SCALAR_PATH(scalar), output, scratch))
    {
        lowered->status = lowered->asms.status;
        lowered->token = lowered->asms.token;
        valid = false;
    }
    // Freeze each type's class and by-value containment facts before pooling. Bodies and pool eligibility read
    // the same columns; preparing a record initializer never allocates or clears a unit-sized traversal table.
    shared->type_count = valid ? types->types.count : 0;
    shared->type_classes = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)shared->type_count + 1) : 0;
    shared->type_facts = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)shared->type_count + 1) : 0;
    valid = valid && shared->type_classes != 0 && shared->type_facts != 0;
    if (valid)
    {
        if (BUSTER_SCALAR_PATH(scalar))
        {
            check_lower_classify_types_scalar(types, shared->type_count, shared->type_classes, shared->type_facts);
        }
        else
        {
            check_lower_classify_types(types, shared->type_count, shared->type_classes, shared->type_facts);
        }
        valid = check_lower_copy_type_facts(types, shared->type_count, shared->type_facts, scratch, scalar);
    }
    if (valid && !check_lower_init_pools(&lowered->initializers, program, shared->type_facts, shared->type_count, output, scratch))
    {
        lowered->status = CHECK_PROGRAM_RESOURCE;
        valid = false;
    }
    const uint32_t errno_calls = valid ? check_lower_runtime_symbols(lowered, program) : 0;
    valid = valid && lowered->status == CHECK_PROGRAM_OK;
    for (uint32_t i = 0; i < lowered->helpers.count; i += 1)
    {
        shared->terminal |= lowered->helpers.items[i].terminates;
    }
    // File-scope assembly is the emission's (check_program_emit_file_asm, #619): it lowers no code.
    shared->capacity = valid ? (uint32_t)capacity64 : 0;
    shared->local_capacity = valid ? (uint32_t)local_capacity64 : 0;
    shared->x87_constant_capacity = valid ? (uint32_t)x87_capacity64 : 0;
    shared->builtin_count = builtin_count;
    shared->atomic_capacity = atomic_capacity;
    shared->integer_builtin_count = integer_builtin_count;
    // A node makes a call of its own, and with 128-bit integers up to two of libgcc's more (a division, its conversions to
    // or from a floating type, an update's two), each of up to two arguments. An aggregate copy makes one three-argument
    // runtime call, independent of its byte size, within the same per-node bounds. The runtime functions' provisional symbols
    // (check_lower_runtime) follow the helpers'.
    const uint64_t abi_capacity64 = (uint64_t)nodes * 3 + program->body_count + errno_calls + asm_statements + lowered->initializers.count + 1;
    const uint64_t argument_capacity64 = input != 0 ? (uint64_t)input->graph.argument_count + count + (uint64_t)nodes * 4 + errno_calls + asm_statements +
                                                    (uint64_t)lowered->initializers.count * 3 + 1 : 0;
    const uint64_t pool_base = (uint64_t)program->symbol_count + lowered->helpers.count + 1;
    const uint64_t provisional64 = pool_base + lowered->initializers.count;
    lowered->initializers.symbol_base = provisional64 < UINT32_MAX ? (SymbolId)pool_base : 0;
    if (valid && (abi_capacity64 > UINT32_MAX || argument_capacity64 > UINT32_MAX || provisional64 + CHECK_LOWER_RUNTIMES >= UINT32_MAX))
    {
        lowered->status = CHECK_PROGRAM_RESOURCE;
        valid = false;
    }
    shared->abi_capacity = valid ? (uint32_t)abi_capacity64 : 0;
    shared->argument_capacity = valid ? (uint32_t)argument_capacity64 : 0;
    shared->provisional_runtimes = valid ? (SymbolId)provisional64 : 0;
    shared->provisional_tables = valid ? (SymbolId)(provisional64 + CHECK_LOWER_RUNTIMES) : 0;
#define BUSTER_CHECK_LOWER_TABLE(field, type, length) shared->field = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, type, length) : 0; valid &= shared->field != 0
    BUSTER_CHECK_LOWER_TABLE(local_map, uint32_t, (uint64_t)count + 1);
    BUSTER_CHECK_LOWER_TABLE(switch_locals, uint32_t, (uint64_t)count + 1);
    BUSTER_CHECK_LOWER_TABLE(vla_identities, uint32_t, input->vla_count != 0 ? (uint64_t)count + 1 : 1);
    BUSTER_CHECK_LOWER_TABLE(save_locals, uint32_t, input->vla_count != 0 ? (uint64_t)count + 1 : 1);
    BUSTER_CHECK_LOWER_TABLE(initializer_declarations, uint32_t, (uint64_t)nodes + 1);
#undef BUSTER_CHECK_LOWER_TABLE
    // The labels' symbols: of the selected program's objects, those whose token is a label's.
    for (uint32_t s = 0; valid && s < program->symbol_count; s += 1)
    {
        const uint32_t object = program->symbol_objects[s];
        shared->label_symbol_capacity += object != 0 && object - 1 < count && program->parser->tree.roles[object - 1] == PARSE_ROLE_LABEL;
    }
    // Every switch's jump table, decided before the bodies; the label symbols' room holds the tables' labels too.
    valid = valid && check_lower_switch_tables(lowered, program, output, scratch);
    valid = valid && (uint64_t)shared->provisional_tables + lowered->table_count < UINT32_MAX;
    const uint64_t label_room = valid ? (uint64_t)shared->label_symbol_capacity + lowered->case_symbol_count : 0;
    lowered->label_symbols = valid && label_room != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, SymbolId, label_room) : 0;
    valid &= label_room == 0 || lowered->label_symbols != 0;
    // Every type's classification first: a body's struct or union result decides its returns (check_lower_by_value),
    // and a variadic definition's named registers its va_start; abi_assign takes each part's signatures together after.
    // The lowering makes no type, so the table needs rows for the types the unit has, not for the type table's room.
    // Calls, returns and va_arg belong to function bodies. Object initializers and generated helpers don't read this
    // table, so a unit without a body needs neither its columns nor its classifications (#613).
    valid = valid && (program->body_count == 0 ||
                      abi_initialize_types(&shared->table, scratch, types, check_lower_vector_register_bytes(program->target.features), types->types.count));
    if (valid && program->body_count != 0)
    {
        if (BUSTER_SCALAR_PATH(scalar))
        {
            abi_classify_all_scalar(&shared->table, types);
        }
        else
        {
            abi_classify_all(&shared->table, types);
        }
    }
    if (valid)
    {
        // One bounded lookup obtains both columns for the leaf and vector kernels. The optional table's
        // failure retains scalar expression expansion; scalar/source-row lowering allocates none.
        shared->type_templates = valid && !BUSTER_SCALAR_PATH(scalar) && !sources && program->body_count != 0 ?
                                 BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)shared->type_count + 1) : NULL;
        for (uint32_t first = 0; shared->type_templates != NULL && first < shared->type_count; first += 16)
        {
            const uint32_t count = shared->type_count - first < 16 ? shared->type_count - first : 16;
            const __mmask16 active = (__mmask16)((1u << count) - 1);
            const __m512i classes = _mm512_cvtepu8_epi32(_mm_maskz_loadu_epi8(active, shared->type_classes + first));
            const __m512i facts = _mm512_cvtepu8_epi32(_mm_maskz_loadu_epi8(active, shared->type_facts + first));
            _mm512_mask_storeu_epi32(shared->type_templates + first, active, _mm512_or_si512(classes, _mm512_slli_epi32(facts, 8)));
        }
        if (shared->type_templates != NULL)
        {
            shared->type_templates[shared->type_count] = UINT8_MAX;
        }
    }
    shared->signatures = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)program->body_count + 1) : 0;
    shared->incoming = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)program->body_count + 1) : 0;
    shared->local_first = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    shared->local_next = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    // The tokens that declare or start an initializer, which the pass below visits rather than every token of the unit.
    uint32_t* declaring = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 16) : 0;
    lowered->functions = valid && program->body_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, Tier0Function, program->body_count) : 0;
    lowered->entry_tokens = valid && sources && program->body_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t*, program->body_count) : 0;
    valid = valid && shared->signatures != 0 && shared->incoming != 0 && shared->local_first != 0 && shared->local_next != 0 && declaring != 0 &&
            (program->body_count == 0 || lowered->functions != 0) && (!sources || program->body_count == 0 || lowered->entry_tokens != 0);
    if (!valid && lowered->status == CHECK_PROGRAM_OK)
    {
        lowered->status = CHECK_PROGRAM_RESOURCE;
    }
    if (valid)
    {
        memset(shared->initializer_declarations, 0xff, ((uint64_t)nodes + 1) * sizeof(uint32_t));
        memset(shared->local_map, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
        memset(shared->switch_locals, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
        if (input->vla_count != 0)
        {
            memset(shared->vla_identities, 0, ((uint64_t)count + 1) * sizeof(uint32_t));
            memset(shared->save_locals, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
            for (uint32_t v = 0; v < input->vla_count; v += 1)
            {
                if (input->vlas[v].bound_begin < count)
                {
                    shared->vla_identities[input->vlas[v].bound_begin] = v + 1;
                }
            }
        }
        memset(shared->local_first, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
        // In reverse, so that a node's declaration is its first token and each body's locals are linked in token order.
        for (uint32_t d = check_program_declaring(input->declarations, input->initializer_nodes, nodes, count, declaring, BUSTER_SCALAR_PATH(scalar)); d-- > 0;)
        {
            const uint32_t token = declaring[d];
            if (input->initializer_nodes[token] < nodes)
            {
                shared->initializer_declarations[input->initializer_nodes[token]] = token;
            }
            const uint32_t body = input->declaration_bodies[token];
            if (input->declarations[token] == token && body < count && input->declaration_kinds[token] == C_BINDING_OBJECT && !input->static_objects[token])
            {
                shared->local_next[token] = shared->local_first[body];
                shared->local_first[body] = token;
            }
        }
        // The runtime functions the unit declares, for every writer: each one's symbol is the first declaration's of its
        // name with linkage that has one (the errno accessor's is check_lower_runtime_symbols' already). One pass over the
        // declarations, which only a unit that spells one of the names takes.
        shared->declared_runtimes[CHECK_LOWER_RUNTIME_ERRNO] = lowered->errno_symbol;
        IdentId identifiers[CHECK_LOWER_RUNTIMES] = { 0 };
        bool spelled = false;
        for (uint32_t r = CHECK_LOWER_RUNTIME_ERRNO + 1; program->parser != NULL && program->identifiers != NULL && r < CHECK_LOWER_RUNTIMES; r += 1)
        {
            const char* name = check_lower_runtime_names[r];
            const uint32_t length = (uint32_t)strlen(name);
            identifiers[r] = intern_find_hashed(program->identifiers, (const uint8_t*)name, length, intern_hash_bytes((const uint8_t*)name, length));
            spelled |= identifiers[r] != 0;
        }
        for (uint32_t declaration = 0; spelled && declaration < input->token_count; declaration += 1)
        {
            const uint32_t object = input->objects[declaration];
            const uint32_t name = program->parser->tokens.names[declaration];
            if (name != 0 && input->declarations[declaration] == declaration && input->linkage[declaration] == 2 && object != 0 && object < program->object_count)
            {
                for (uint32_t r = CHECK_LOWER_RUNTIME_ERRNO + 1; r < CHECK_LOWER_RUNTIMES; r += 1)
                {
                    if (identifiers[r] == name && shared->declared_runtimes[r] == 0)
                    {
                        shared->declared_runtimes[r] = program->object_symbols[object];
                    }
                }
            }
        }
    }
    return valid;
}

// A writer of the bodies' parts (check_lower_part) for one lane: its columns, with room for any of the program's bodies,
// and its signatures' from `scratch`; the shared tables from `shared`; and `status`, the lane's copy of the program's
// lowering, which a part's failure sets (check_lower_fail), whose helpers and errno function the bodies read and whose
// label symbols are the writer's room for its part's. It only pushes, and writes nothing in what it pushes, which
// check_lower_writer_bytes measures it by.
static bool check_lower_writer_begin(CheckLowerWriter* writer, CheckLowerAbi* abi, CheckProgramLowered* status, const CheckLowerShared* shared, bool sources,
                                     Arena* scratch)
{
    const CheckProgram* program = shared->program;
    const CheckInput* input = program->input;
    const uint32_t capacity = shared->capacity;
    const uint32_t local_capacity = shared->local_capacity;
    const uint32_t abi_capacity = shared->abi_capacity;
    const uint32_t argument_capacity = shared->argument_capacity;
    const bool track = program->region_count != 0 || shared->terminal;
    bool valid = true;
    *status = (CheckProgramLowered){
        .helpers = shared->lowered->helpers,
        .asms = shared->lowered->asms,
        .initializers = shared->lowered->initializers,
        .errno_symbol = shared->lowered->errno_symbol,
        .errno_pointer_type = shared->lowered->errno_pointer_type,
        .token = BUSTER_CHECK_NONE,
    };
    *abi = (CheckLowerAbi){ .capacity = abi_capacity, .argument_capacity = argument_capacity };
#define BUSTER_CHECK_LOWER_ABI(field, type, length) abi->field = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, type, length) : 0; valid &= abi->field != 0
    BUSTER_CHECK_LOWER_ABI(results, TypeId, abi_capacity);
    BUSTER_CHECK_LOWER_ABI(arguments, TypeId, argument_capacity);
    BUSTER_CHECK_LOWER_ABI(counts, uint32_t, abi_capacity);
    BUSTER_CHECK_LOWER_ABI(named, uint32_t, abi_capacity);
    BUSTER_CHECK_LOWER_ABI(first, uint32_t, abi_capacity);
    BUSTER_CHECK_LOWER_ABI(variadic, uint8_t, abi_capacity);
#undef BUSTER_CHECK_LOWER_ABI
    *writer = (CheckLowerWriter){
        .program = program,
        .output = status,
        .abi = abi,
        .capacity = capacity,
        .local_capacity = local_capacity,
        .scalar = BUSTER_SCALAR_PATH(shared->scalar),
        .sources = sources,
        .x87_constant_capacity = shared->x87_constant_capacity,
        .label_symbol_capacity = shared->label_symbol_capacity,
        .class_limit = check_lower_vector_target(program->target.features) ? BUSTER_TYPED_CLASSES : BUSTER_TYPED_CLASS_VECTOR,
        .target = program->target,
        .abi_table = &shared->table,
        .type_classes = shared->type_classes,
        .type_facts = shared->type_facts,
        .type_templates = shared->type_templates,
        .type_count = shared->type_count,
        .local_map = shared->local_map,
        .switch_locals = shared->switch_locals,
        .vla_identities = shared->vla_identities,
        .save_locals = shared->save_locals,
        .initializer_declarations = shared->initializer_declarations,
        .declared_runtimes = shared->declared_runtimes,
        .provisional_runtimes = shared->provisional_runtimes,
        .table_current = BUSTER_CHECK_NONE,
        .tables = shared->lowered->tables,
        .table_count = shared->lowered->table_count,
        .provisional_tables = shared->provisional_tables,
    };
#define BUSTER_CHECK_LOWER_COLUMN(field, type, length) writer->field = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, type, length) : 0; valid &= writer->field != 0
    // Give the following columns the same offset modulo 4 KB relative to the task stack as 32-byte
    // records. Padding is at most 4080 bytes, while each active task still touches just 16 bytes.
    BUSTER_CHECK_LOWER_COLUMN(tasks, CheckLowerTaskPacked, (uint64_t)capacity + (capacity & 255u));
    BUSTER_CHECK_LOWER_COLUMN(kinds, uint8_t, capacity);
    BUSTER_CHECK_LOWER_COLUMN(classes, uint8_t, capacity);
    BUSTER_CHECK_LOWER_COLUMN(values, uint64_t, capacity);
    BUSTER_CHECK_LOWER_COLUMN(tokens, uint32_t, sources ? capacity : 1);
    BUSTER_CHECK_LOWER_COLUMN(task_sources, uint32_t, sources ? capacity : 1);
    BUSTER_CHECK_LOWER_COLUMN(labels, uint32_t, capacity);
    BUSTER_CHECK_LOWER_COLUMN(label_heads, uint32_t, track ? capacity : 1);
    BUSTER_CHECK_LOWER_COLUMN(stack_parents, uint32_t, track ? capacity : 1);
    BUSTER_CHECK_LOWER_COLUMN(stack_classes, uint8_t, track ? capacity : 1);
    BUSTER_CHECK_LOWER_COLUMN(extent_locals, uint32_t, (uint64_t)input->vla_count + 1);
    BUSTER_CHECK_LOWER_COLUMN(indirect, uint8_t, local_capacity);
    BUSTER_CHECK_LOWER_COLUMN(local_classes, uint8_t, local_capacity);
    BUSTER_CHECK_LOWER_COLUMN(local_flags, uint8_t, local_capacity);
    BUSTER_CHECK_LOWER_COLUMN(value_classes, uint8_t, local_capacity);
    BUSTER_CHECK_LOWER_COLUMN(local_sizes, uint32_t, local_capacity);
    BUSTER_CHECK_LOWER_COLUMN(local_alignments, uint8_t, local_capacity);
    BUSTER_CHECK_LOWER_COLUMN(call_targets, SymbolId, abi_capacity);
    BUSTER_CHECK_LOWER_COLUMN(call_first, uint32_t, abi_capacity);
    BUSTER_CHECK_LOWER_COLUMN(call_counts, uint32_t, abi_capacity);
    BUSTER_CHECK_LOWER_COLUMN(call_variadic, uint8_t, abi_capacity);
    BUSTER_CHECK_LOWER_COLUMN(argument_classes, uint8_t, argument_capacity);
    BUSTER_CHECK_LOWER_COLUMN(argument_sizes, uint32_t, argument_capacity);
    BUSTER_CHECK_LOWER_COLUMN(call_result_objects, uint32_t, abi_capacity);
    BUSTER_CHECK_LOWER_COLUMN(varargs, CBuiltinVaLowerCall, (uint64_t)program->stream->varargs_count + 1);
    BUSTER_CHECK_LOWER_COLUMN(builtins, CBuiltinLowerCall, (uint64_t)shared->builtin_count + 1);
    BUSTER_CHECK_LOWER_COLUMN(builtin_operands, CheckLowerBuiltinOperands, (uint64_t)shared->integer_builtin_count + 1);
    BUSTER_CHECK_LOWER_COLUMN(miscellaneous, CBuiltinMiscLowerCall, (uint64_t)program->stream->misc_count + 1);
    BUSTER_CHECK_LOWER_COLUMN(floating, CBuiltinFloatLowerCall, (uint64_t)program->stream->floating_count + 1);
    BUSTER_CHECK_LOWER_COLUMN(atomics, CheckLowerAtomic, (uint64_t)shared->atomic_capacity + 1);
    BUSTER_CHECK_LOWER_COLUMN(x87_constants, uint64_t, 2 * (uint64_t)shared->x87_constant_capacity);
    // The rows' room and the vector constants': reserved, and touched only by functions with vectors.
    writer->operation_capacity = BUSTER_CHECK_LOWER_OPERATIONS;
    BUSTER_CHECK_LOWER_COLUMN(operations, Tier0Operation, BUSTER_CHECK_LOWER_OPERATIONS);
    writer->vector_constant_capacity = BUSTER_CHECK_LOWER_VECTOR_CONSTANTS;
    BUSTER_CHECK_LOWER_COLUMN(vector_constants, uint64_t, 8 * (uint64_t)BUSTER_CHECK_LOWER_VECTOR_CONSTANTS);
    BUSTER_CHECK_LOWER_COLUMN(vector_constant_bytes, uint8_t, BUSTER_CHECK_LOWER_VECTOR_CONSTANTS);
    BUSTER_CHECK_LOWER_COLUMN(label_entries, uint32_t, (uint64_t)program->control_count + 1);
#undef BUSTER_CHECK_LOWER_COLUMN
    const uint64_t label_room = (uint64_t)shared->label_symbol_capacity + shared->lowered->case_symbol_count;
    writer->label_symbol_room = valid && label_room != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, SymbolId, label_room) : 0;
    valid &= label_room == 0 || writer->label_symbol_room != 0;
    return valid;
}

// The room check_lower_writer_begin takes for `shared`'s bodies from an arena whose base is page-aligned, as an arena
// reserved for its columns has (check_program_emit_lanes): its pushes on an arena of no memory (arena_measure), which
// they take whatever room the caller's arenas have, since the writer writes nothing in what it pushes. UINT64_MAX if a
// column's bytes overflow, which no reservation takes.
static uint64_t check_lower_writer_bytes(const CheckLowerShared* shared, bool sources)
{
    CheckLowerWriter writer;
    CheckLowerAbi abi;
    CheckProgramLowered status;
    Arena measure = arena_measure();
    return check_lower_writer_begin(&writer, &abi, &status, shared, sources, &measure) ? measure.length : UINT64_MAX;
}

// The most nodes a statement's prefetch takes (check_lower_prefetch): its root's and those just before it, the first the
// tasks' loop expands.
#define BUSTER_CHECK_LOWER_PREFETCH_NODES 64u

// Prefetches the checked values and conversions of the nodes from `from` to `node`, a statement's root (the last
// BUSTER_CHECK_LOWER_PREFETCH_NODES of them): a statement's nodes come before its root in the graph, and the tasks' loop
// waited on its first touch of each node's 96 bytes of checked value and 32 of conversions (#531). Returns the node after
// the last one prefetched, or `from` for a root that isn't past it.
static uint32_t check_lower_prefetch(const CheckProgram* program, uint32_t from, uint32_t node)
{
    uint32_t result = from;
    if (node != BUSTER_CHECK_NONE && node < program->stream->count && node >= from)
    {
        const uint32_t first = node - from >= BUSTER_CHECK_LOWER_PREFETCH_NODES ? node + 1 - BUSTER_CHECK_LOWER_PREFETCH_NODES : from;
        const uintptr_t ranges[2][2] = {
            {
                (uintptr_t)(program->stream->values + first),
                (uintptr_t)(program->stream->values + node + 1),
            },
            {
                (uintptr_t)(program->stream->conversions + first),
                (uintptr_t)(program->stream->conversions + node + 1),
            },
        };
        for (uint32_t r = 0; r < 2; r += 1)
        {
            for (uintptr_t line = ranges[r][0] & ~(uintptr_t)63; line < ranges[r][1]; line += 64)
            {
                _mm_prefetch((const char*)line, _MM_HINT_T0);
            }
        }
        result = node + 1;
    }
    return result;
}

// Lowers `part`'s bodies, in order, with `writer` (check_lower_writer_begin's), each into the program's Tier0Function, its
// columns from `output`, and its writer's room for its signatures: the part's calls' locations from `output` after its
// bodies, as abi_assign gives them together, and each body's parameters, then tier0_validate's check of it. Stops at the
// part's first failure, which it records with its step (CheckLowerStage), as check_lower_finish orders the parts'. With
// `sources`, each body's entries' source tokens too: a constant in each copy (check_lower_part and
// check_lower_part_sources), so that the plain lowering, every compile's without -g, carries none of the tokens' state
// through its loops: a flag clang can't fold there made it spill more in them, which every compile paid (#426).
static inline __attribute__((always_inline)) void check_lower_part_body(CheckLowerWriter* writer, const CheckLowerShared* shared, CheckLowerPart* part,
                                                                       bool sources, Arena* output, Arena* scratch)
{
    const CheckProgram* program = shared->program;
    CheckProgramLowered* lowered = shared->lowered;
    CheckProgramLowered* status = writer->output;
    CheckLowerAbi* abi = writer->abi;
    const AbiTable* table = &shared->table;
    const CheckInput* input = program->input;
    const TypeTable* types = program->types;
    status->status = CHECK_PROGRAM_OK;
    status->token = BUSTER_CHECK_NONE;
    status->backend_node = 0;
    status->reason = NULL;
    // The part's label symbols follow the ones the writer's parts before it kept, in its room for the program's.
    status->label_symbols = writer->label_symbol_room != 0 ? writer->label_symbol_room + writer->label_symbol_used : 0;
    status->label_symbol_count = 0;
    writer->label_symbol_capacity = shared->label_symbol_capacity + lowered->case_symbol_count - writer->label_symbol_used;
    abi->count = 0;
    abi->argument_count = 0;
    writer->runtime_mask = 0;
    writer->runtime_count = 0;
    part->stage = CHECK_LOWER_STAGE_NONE;
    part->entries = 0;
    writer->expression_nodes = 0;
    writer->batch_nodes = 0;
    writer->classified_nodes = 0;
    for (uint32_t b = part->first; b < part->end && status->status == CHECK_PROGRAM_OK; b += 1)
    {
        const CheckProgramBody body = program->bodies[b];
        const TypeId type = input->declared_types[body.declaration];
        // A naked function's code is its templates' (check_program_asm_plan_naked): its own is a void function of no
        // parameters, locals or statements, which the emission doesn't compile, its one entry at its first statement.
        const CheckProgramNaked* naked = lowered->asms.naked != 0 ? lowered->asms.naked + b : 0;
        const bool bare = naked != 0 && naked->planned;
        const TypeId result = bare ? BUSTER_TYPE_VOID : type_return(types, type);
        Tier0Function* function = &lowered->functions[b];
        *function = (Tier0Function){ 0 };
        writer->count = 0;
        writer->label_entry_count = 0;
        writer->table_current = BUSTER_CHECK_NONE;
        writer->table_pending = false;
        writer->depth = 0;
        if (sources)
        {
            writer->filled = 0;
            writer->settled = 0;
        }
        writer->local_count = 0;
        writer->call_count = 0;
        writer->argument_count = 0;
        writer->builtin_count = 0;
        writer->atomic_count = 0;
        writer->builtin_depth = 0;
        writer->builtin_levels = 0;
        writer->builtin_scratch.count = 0;
        writer->miscellaneous_count = 0;
        writer->floating_count = 0;
        writer->label_count = body.count + 1;
        writer->stack_count = 0;
        writer->stack_head = 0;
        writer->reachable = true;
        writer->objects = false;
        writer->aggregates = false;
        writer->track = body.count != body.entry_count || shared->terminal;
        writer->body = program->bodies + b;
        // The function's instruction set (CheckProgram.body_targets), and so the classes it takes.
        const uint32_t body_target = program->body_targets != 0 ? program->body_targets[b] : 0;
        writer->target = body_target == 0 || body_target == BUSTER_CHECK_PROGRAM_TARGET_VERSIONS ? program->target : program->targets[body_target - 1];
        writer->class_limit = check_lower_vector_target(program->target.features & writer->target.features) ? BUSTER_TYPED_CLASSES : BUSTER_TYPED_CLASS_VECTOR;
        writer->result = result;
        writer->token = body.declaration;
        check_lower_source(writer, body.declaration);
        writer->hidden = BUSTER_CHECK_NONE;
        writer->variadic = !bare && (types->types.flags[type_index(type)] & BUSTER_TYPE_FLAG_VARIADIC) != 0;
        writer->save_anchor = BUSTER_CHECK_NONE;
        writer->overflow_anchor = BUSTER_CHECK_NONE;
        writer->varargs_count = 0;
        writer->x87 = check_lower_x87(writer, result);
        writer->int128 = false;
        writer->vectors = false;
        writer->operation_count = 0;
        writer->vector_constant_count = 0;
        writer->result_object = BUSTER_CHECK_NONE;
        writer->x87_constant_count = 1;
        writer->x87_constants[0] = 0;
        writer->x87_constants[1] = 0;
        memset(writer->extent_locals, 0xff, ((uint64_t)input->vla_count + 1) * sizeof(uint32_t));
        memset(writer->labels, 0xff, ((uint64_t)body.count + 1) * sizeof(uint32_t));
        if (writer->track)
        {
            memset(writer->label_heads, 0xff, ((uint64_t)body.count + 1) * sizeof(uint32_t));
        }
        shared->signatures[b] = abi->count;
        abi->first[abi->count] = abi->argument_count;
        abi->results[abi->count] = result;
        abi->variadic[abi->count] = writer->variadic;
        // A struct or union result goes through a hidden pointer (parameter 0, in rdi) or comes back in registers from
        // the address each return gives (Tier0Function.result_size).
        const bool aggregate_result = check_lower_aggregate(writer, result);
        const char* returned = aggregate_result ? check_lower_by_value(writer, result, true) : check_lower_class(writer, result) == UINT8_MAX ? check_lower_type_feature(types, result) : NULL;
        if (body_target == BUSTER_CHECK_PROGRAM_TARGET_VERSIONS)
        {
            // GNU's function multiversioning: a version's symbol and the resolver's ifunc that calls reach.
            check_lower_unsupported(writer, "functions of several target versions (target_clones, or target attributes of other strings)");
        }
        else if (bare && naked->reason != NULL)
        {
            check_lower_unsupported(writer, naked->reason);
        }
        else if (returned != NULL)
        {
            check_lower_unsupported(writer, returned);
        }
        else if (aggregate_result && table->results[type_index(result)].kind == BUSTER_ABI_RESULT_MEMORY)
        {
            writer->hidden = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
            function->parameter_count += 1;
        }
        else if (aggregate_result && table->results[type_index(result)].kind == BUSTER_ABI_RESULT_REGISTERS)
        {
            function->result_size = (uint32_t)type_size(types, result);
        }
        else if (check_lower_wide(writer, result) || check_lower_vector_of(writer, result) != 0)
        {
            // A 128-bit integer returns from its object's 16 bytes, in rax and rdx, as a struct of two eightbytes does, and a
            // vector from its object's, in xmm0, ymm0 or zmm0: the object comes after the parameters (below).
            function->result_size = check_lower_wide(writer, result) ? 16 : check_lower_vector_of(writer, result);
        }
        uint32_t declared = 0;
        // A K&R definition's callers pass its parameters' promoted types, its ABI's (CHECK_INPUT_FUNCTION_KR).
        const bool kr = (input->function_flags[body.declaration] & CHECK_INPUT_FUNCTION_KR) != 0;
        for (uint32_t parameter = bare ? BUSTER_CHECK_NONE : input->parameters[body.opening]; parameter != BUSTER_CHECK_NONE; parameter = input->parameter_next[parameter])
        {
            const TypeId parameter_type = input->declared_types[parameter];
            if (kr && type_default_argument_promote(types, type_unqualified(parameter_type)) != type_unqualified(parameter_type))
            {
                check_lower_unsupported(writer, "K&R parameters the default argument promotions change");
            }
            const CheckInputAttributes* attributes = input->attributes[parameter];
            // A struct or union parameter is a frame object, whose bytes the prologue places (aligned past 16 bytes, in a
            // realigned frame, or where they arrive).
            const bool aggregate = check_lower_aggregate(writer, parameter_type);
            const uint32_t declared_alignment = attributes != 0 && attributes->alignment_log2 != 0 ? attributes->alignment_log2 - 1u : 0;
            const uint32_t alignment = type_alignment_log2(types, parameter_type) > declared_alignment ? type_alignment_log2(types, parameter_type) : declared_alignment;
            const char* passed = check_lower_passed(writer, parameter_type);
            // So is a memory class's (check_lower_memory_class): a long double's, from its 16 bytes on the stack, and a
            // 128-bit integer's, from its two eightbytes, as a struct of them; and a value's aligned past 8 bytes, whose
            // slot would be aligned to 8, from its register's part or its slot, as a struct of it.
            const bool object = aggregate || check_lower_memory(writer, parameter_type) || alignment > 3;
            writer->token = parameter;
            if (alignment > BUSTER_TIER0_MAX_OBJECT_ALIGNMENT_LOG2)
            {
                check_lower_unsupported(writer, "parameters aligned past 1 GB");
            }
            if (passed != NULL)
            {
                check_lower_unsupported(writer, passed);
            }
            const uint32_t local = object ? check_lower_object(writer, type_size(types, parameter_type), alignment) :
                                            check_lower_temporary(writer, check_lower_class(writer, parameter_type));
            if (((type_qualifiers(parameter_type) & BUSTER_TYPE_VOLATILE) != 0 ||
                 type_kind(types, type_strip_aligned(types, parameter_type)) == BUSTER_TYPE_KIND_ATOMIC) && local < writer->local_count)
            {
                writer->local_flags[local] |= BUSTER_TYPED_LOCAL_ADDRESS_TAKEN;
            }
            writer->local_map[input->objects[parameter]] = local;
            abi->arguments[abi->argument_count++] = parameter_type;
            function->parameter_count += 1;
            declared += 1;
        }
        writer->token = body.declaration;
        abi->counts[abi->count] = declared;
        abi->named[abi->count++] = declared;
        if (writer->variadic && status->status == CHECK_PROGRAM_OK)
        {
            // va_start's offsets: the named arguments' registers, as abi_assign gives the function's own call (the batch
            // below gives it the same).
            const uint64_t mark = scratch->length;
            AbiLocation* named = BUSTER_ARENA_PUSH_ARRAY(scratch, AbiLocation, (uint64_t)declared + 1);
            AbiCall own = { 0 };
            if (named == 0)
            {
                check_lower_fail(writer, CHECK_PROGRAM_RESOURCE);
            }
            else
            {
                const uint8_t variadic = 1;
                const AbiSignatures signature = {
                    .results = abi->results + shared->signatures[b],
                    .counts = &declared,
                    .named = &declared,
                    .variadic = &variadic,
                    .count = 1,
                    .arguments = abi->arguments + abi->first[shared->signatures[b]],
                };
                abi_assign_scalar(table, signature, named, &own);
            }
            arena_pop_to(scratch, mark);
            writer->gp_offset = BUSTER_ABI_SLOT_BYTES * own.integer_registers;
            writer->fp_offset = BUSTER_ABI_SAVE_AREA_VECTOR_OFFSET + BUSTER_ABI_SAVE_AREA_VECTOR_SLOT_BYTES * own.vector_registers;
        }
        if (check_lower_wide(writer, result) || check_lower_vector_of(writer, result) != 0)
        {
            writer->result_object = check_lower_temporary_of(writer, result);
        }
        for (uint32_t declaration = bare ? BUSTER_CHECK_NONE : shared->local_first[body.opening]; declaration != BUSTER_CHECK_NONE; declaration = shared->local_next[declaration])
        {
            if (writer->local_map[input->objects[declaration]] == BUSTER_CHECK_NONE)
            {
                const TypeId local_type = input->declared_types[declaration];
                const uint8_t kind = type_kind(types, type_strip_aligned(types, local_type));
                const CheckInputAttributes* attributes = input->attributes[declaration];
                // The attribute's alignment is its log2 plus one, 0 for none.
                const uint32_t declared_alignment = attributes != 0 && attributes->alignment_log2 != 0 ? attributes->alignment_log2 - 1u : 0;
                const uint32_t alignment = type_alignment_log2(types, local_type) > declared_alignment ? type_alignment_log2(types, local_type) : declared_alignment;
                uint32_t local = BUSTER_CHECK_NONE;
                writer->token = declaration;
                if (check_action_runtime_size(types, local_type))
                {
                    // A variable-length array lives on the stack from its declaration (CHECK_CONTROL_ALLOCATE) to its
                    // block's end: its local holds its address.
                    local = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
                    if (local < writer->local_count)
                    {
                        writer->indirect[local] = BUSTER_CHECK_LOWER_VLA_DECLARED;
                    }
                }
                else if (kind == BUSTER_TYPE_KIND_ARRAY || kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION || kind == BUSTER_TYPE_KIND_ATOMIC || check_lower_complex(writer, local_type))
                {
                    // Arrays, structs, unions and complex values are frame objects (aligned past 16 bytes, in a realigned
                    // frame). An incomplete type has no size at all.
                    if (!type_is_complete(types, local_type))
                    {
                        check_lower_unsupported(writer, "local objects of incomplete type");
                    }
                    local = check_lower_object(writer, type_size(types, local_type), alignment);
                }
                else if (check_lower_memory(writer, local_type))
                {
                    // A memory class's value is a frame object too (check_lower_memory_class), aligned to 16.
                    local = check_lower_object(writer, type_size(types, local_type), alignment);
                }
                else if (input->compound_objects[input->objects[declaration] - 1] && check_lower_classless_vector(writer, local_type))
                {
                    // A compound literal of a vector Tier 0 has no class for (of 2, 4 or 8 bytes) has no local: a
                    // narrowing shuffle reads its constants at compile time (the intrinsics' (__v4qi){0, 0, 0, 0}:
                    // check_lower_constant_bytes), and its evaluation is refused anywhere else (check_lower_expand).
                }
                else if (!type_kind_is_array(kind) && check_lower_class(writer, local_type) == UINT8_MAX)
                {
                    // A type no class holds and that isn't an object's: what its values need (check_lower_type_feature).
                    check_lower_unsupported(writer, check_lower_type_feature(types, local_type));
                }
                else if (type_kind_is_array(kind))
                {
                    check_lower_unsupported(writer, "local values aligned past 8 bytes");
                }
                else if (alignment > 3)
                {
                    // A value aligned past 8 bytes, whose slot would be aligned to 8: a frame object of its alignment, which
                    // LOCAL, STORE_LOCAL and SET_LOCAL of its class read and write (tier0_value_object).
                    local = check_lower_object(writer, type_size(types, local_type), alignment);
                }
                else
                {
                    local = check_lower_temporary(writer, check_lower_class(writer, local_type));
                }
                if (((type_qualifiers(local_type) & BUSTER_TYPE_VOLATILE) != 0 || kind == BUSTER_TYPE_KIND_ATOMIC) && local < writer->local_count)
                {
                    writer->local_flags[local] |= BUSTER_TYPED_LOCAL_ADDRESS_TAKEN;
                }
                writer->local_map[input->objects[declaration]] = local;
            }
        }
        // Each statement's nodes are prefetched a statement before the tasks' loop expands them, the first two's before the
        // first (check_lower_prefetch).
        uint32_t prefetched = 0;
        for (uint32_t i = 0; !bare && i < body.entry_count && status->status == CHECK_PROGRAM_OK; i += 1)
        {
            if (sources)
            {
                // A statement's entries are its token's (check_lower_task_source sets a nested one's).
                check_lower_change_source(writer, program->controls[body.first + i].token);
            }
            for (uint32_t ahead = i == 0 ? 0 : 1; ahead < 2 && i + ahead < body.entry_count; ahead += 1)
            {
                prefetched = check_lower_prefetch(program, prefetched, program->controls[body.first + i + ahead].node);
            }
            check_lower_control(writer, i);
            if (sources)
            {
                check_lower_drain_sources(writer);
            }
            else
            {
                check_lower_drain(writer);
            }
        }
        // Whether control reaches the function's end: a statement that falls through to it, or a branch to it.
        const bool falls = (body.return_flow && body.falls) || (writer->track ? writer->reachable || writer->label_heads[body.count] != BUSTER_CHECK_NONE :
                                                                            writer->count == 0 || writer->kinds[writer->count - 1] != BUSTER_TYPED_RETURN);
        check_lower_place(writer, body.count);
        // The function's end, its implicit return, is its closing brace's (an opener's parent is its closer); a naked
        // function's one entry its first statement's `asm`, where its code starts, as clang's row is.
        const bool first_asm = bare && body.count != 0 && program->controls[body.first].kind == CHECK_CONTROL_ASM;
        check_lower_source(writer, first_asm ? input->asm_statements[program->controls[body.first].target].token : program->parser->tree.parents[body.opening]);
        for (uint32_t local = 0; local < writer->local_count; local += 1)
        {
            if (writer->indirect[local] == BUSTER_CHECK_LOWER_VLA_DECLARED)
            {
                // A variable-length array the control stream never allocated: its address would be garbage.
                check_lower_fail(writer, CHECK_PROGRAM_INVALID);
            }
        }
        // C specifies zero for main's implicit return. Other non-void fallthrough remains undefined behavior, but a
        // struct or union result's return there must still give Tier 0 an address (or the hidden pointer) to return.
        if (result == BUSTER_TYPE_VOID)
        {
            check_lower_emit(writer, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
        }
        else if (body.is_main && result == BUSTER_TYPE_INT)
        {
            check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 0);
            check_lower_emit(writer, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_I32, 0);
        }
        else if (writer->result_object != BUSTER_CHECK_NONE && falls)
        {
            // An end that falls off: the result object's address, which Tier 0 must return (its value is undefined).
            check_lower_emit(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, writer->result_object);
            check_lower_emit(writer, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_U64, 0);
        }
        else if (aggregate_result && falls)
        {
            check_lower_return_aggregate(writer, BUSTER_CHECK_NONE);
            if (sources)
            {
                check_lower_drain_sources(writer);
            }
            else
            {
                check_lower_drain(writer);
            }
        }
        for (uint32_t i = 0; i < writer->count && status->status == CHECK_PROGRAM_OK; i += 1)
        {
            if (writer->kinds[i] >= BUSTER_TYPED_BRANCH_FALSE && writer->kinds[i] <= BUSTER_TYPED_JUMP_VALUE)
            {
                const uint64_t label = writer->values[i];
                if (label >= writer->label_count || writer->labels[label] == BUSTER_CHECK_NONE)
                {
                    check_lower_fail(writer, CHECK_PROGRAM_INVALID);
                }
                else
                {
                    writer->values[i] = writer->labels[label];
                }
            }
        }
        // The labels whose addresses the program takes, from their control entries to their entries, as the targets.
        for (uint32_t k = 0; k < writer->label_entry_count && status->status == CHECK_PROGRAM_OK; k += 1)
        {
            const uint32_t label = writer->label_entries[k];
            if (label >= writer->label_count || writer->labels[label] == BUSTER_CHECK_NONE)
            {
                check_lower_fail(writer, CHECK_PROGRAM_INVALID);
            }
            else
            {
                writer->label_entries[k] = writer->labels[label];
            }
        }
        // The function has long double values (Tier0Function.x87) where its result is one or an entry is of class F80,
        // 128-bit integer ones (Tier0Function.int128) where an entry is of class I128 or U128, and vectors
        // (Tier0Function.vectors) where one is of class VECTOR or an x86 builtin made an X86 entry.
        const uint32_t memory = BUSTER_SCALAR_PATH(writer->scalar) ? check_lower_entry_classes_scalar(writer->classes, writer->count) : check_lower_entry_classes(writer->classes, writer->count);
        writer->x87 = writer->x87 || (memory & BUSTER_CHECK_LOWER_ENTRIES_X87) != 0;
        writer->int128 = (memory & BUSTER_CHECK_LOWER_ENTRIES_WIDE) != 0;
        writer->vectors = writer->vectors || (memory & BUSTER_CHECK_LOWER_ENTRIES_VECTOR) != 0;
#define BUSTER_CHECK_LOWER_COPY(field, source, type, length) \
        if (status->status == CHECK_PROGRAM_OK && (length) != 0) \
        { \
            type* column = BUSTER_ARENA_PUSH_ARRAY(output, type, length); \
            if (column != 0) \
            { \
                memcpy(column, writer->source, (uint64_t)(length) * sizeof(type)); \
                function->field = column; \
            } \
            else \
            { \
                check_lower_fail(writer, CHECK_PROGRAM_RESOURCE); \
            } \
        }
        BUSTER_CHECK_LOWER_COPY(kinds, kinds, uint8_t, writer->count);
        BUSTER_CHECK_LOWER_COPY(classes, classes, uint8_t, writer->count);
        BUSTER_CHECK_LOWER_COPY(values, values, uint64_t, writer->count);
        BUSTER_CHECK_LOWER_COPY(local_classes, local_classes, uint8_t, writer->local_count);
        BUSTER_CHECK_LOWER_COPY(local_flags, local_flags, uint8_t, writer->local_count);
        if (writer->objects)
        {
            BUSTER_CHECK_LOWER_COPY(local_sizes, local_sizes, uint32_t, writer->local_count);
            BUSTER_CHECK_LOWER_COPY(local_alignments, local_alignments, uint8_t, writer->local_count);
        }
        BUSTER_CHECK_LOWER_COPY(call_targets, call_targets, SymbolId, writer->call_count);
        BUSTER_CHECK_LOWER_COPY(call_first, call_first, uint32_t, writer->call_count);
        BUSTER_CHECK_LOWER_COPY(call_argument_counts, call_counts, uint32_t, writer->call_count);
        BUSTER_CHECK_LOWER_COPY(call_variadic, call_variadic, uint8_t, writer->call_count);
        BUSTER_CHECK_LOWER_COPY(argument_classes, argument_classes, uint8_t, writer->argument_count);
        if (writer->aggregates)
        {
            BUSTER_CHECK_LOWER_COPY(argument_sizes, argument_sizes, uint32_t, writer->argument_count);
            BUSTER_CHECK_LOWER_COPY(call_result_objects, call_result_objects, uint32_t, writer->call_count);
        }
        if (writer->x87)
        {
            BUSTER_CHECK_LOWER_COPY(x87_constants, x87_constants, uint64_t, 2 * (uint64_t)writer->x87_constant_count);
            function->x87_constant_count = writer->x87_constant_count;
            function->x87 = true;
        }
        function->int128 = writer->int128;
        function->vectors = writer->vectors;
        BUSTER_CHECK_LOWER_COPY(operations, operations, Tier0Operation, writer->operation_count);
        function->operation_count = writer->operation_count;
        BUSTER_CHECK_LOWER_COPY(vector_constants, vector_constants, uint64_t, 8 * (uint64_t)writer->vector_constant_count);
        BUSTER_CHECK_LOWER_COPY(vector_constant_bytes, vector_constant_bytes, uint8_t, writer->vector_constant_count);
        function->vector_constant_count = writer->vector_constant_count;
        BUSTER_CHECK_LOWER_COPY(label_entries, label_entries, uint32_t, writer->label_entry_count);
        function->label_count = writer->label_entry_count;
#undef BUSTER_CHECK_LOWER_COPY
        if (sources && status->status == CHECK_PROGRAM_OK)
        {
            // The last entries' token, the closing brace's.
            check_lower_settle(writer);
            uint32_t* tokens = BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, (uint64_t)writer->count + 1);
            if (tokens != 0)
            {
                memcpy(tokens, writer->tokens, (uint64_t)writer->count * sizeof(uint32_t));
                lowered->entry_tokens[b] = tokens;
            }
            else
            {
                check_lower_fail(writer, CHECK_PROGRAM_RESOURCE);
            }
        }
        // A variadic definition that starts its list has its incoming frame (Tier0Function.incoming_variadic): its own
        // call, which abi_assign gives below.
        shared->incoming[b] = writer->save_anchor != BUSTER_CHECK_NONE;
        function->count = writer->count;
        function->local_count = writer->local_count;
        function->call_count = writer->call_count;
        function->argument_count = writer->argument_count;
        part->entries += writer->count;
        for (uint32_t declaration = shared->local_first[body.opening]; declaration != BUSTER_CHECK_NONE; declaration = shared->local_next[declaration])
        {
            writer->local_map[input->objects[declaration]] = BUSTER_CHECK_NONE;
        }
        for (uint32_t parameter = input->parameters[body.opening]; parameter != BUSTER_CHECK_NONE; parameter = input->parameter_next[parameter])
        {
            writer->local_map[input->objects[parameter]] = BUSTER_CHECK_NONE;
        }
    }
    part->stage = status->status != CHECK_PROGRAM_OK ? CHECK_LOWER_STAGE_BODY : CHECK_LOWER_STAGE_NONE;
    part->final_token = writer->token;
    AbiLocation* locations = part->stage == CHECK_LOWER_STAGE_NONE ? BUSTER_ARENA_PUSH_ARRAY(output, AbiLocation, (uint64_t)abi->argument_count + 1) : 0;
    AbiCall* calls = part->stage == CHECK_LOWER_STAGE_NONE ? BUSTER_ARENA_PUSH_ARRAY(output, AbiCall, (uint64_t)abi->count + 1) : 0;
    part->stage = part->stage == CHECK_LOWER_STAGE_NONE && (locations == 0 || calls == 0) ? CHECK_LOWER_STAGE_ROOM : part->stage;
    if (part->stage == CHECK_LOWER_STAGE_NONE)
    {
        for (uint32_t i = 0; i < abi->argument_count + abi->count; i += 1)
        {
            const TypeId type = i < abi->argument_count ? abi->arguments[i] : abi->results[i - abi->argument_count];
            const uint32_t index = type_index(type);
            if ((table->pass[index] & BUSTER_ABI_PASS_CLASSIFIED) == 0 || (table->pass[index] & BUSTER_ABI_PASS_UNSUPPORTED) != 0 || table->stack_bytes[index] == UINT32_MAX)
            {
                check_lower_unsupported(writer, "this type in a call (its calling convention)");
            }
        }
        part->stage = status->status != CHECK_PROGRAM_OK ? CHECK_LOWER_STAGE_CONVENTION : CHECK_LOWER_STAGE_NONE;
    }
    if (part->stage == CHECK_LOWER_STAGE_NONE)
    {
        const AbiSignatures signature = {
            .results = abi->results,
            .counts = abi->counts,
            .named = abi->named,
            .variadic = abi->variadic,
            .count = abi->count,
            .arguments = abi->arguments,
        };
        if (BUSTER_SCALAR_PATH(shared->scalar))
        {
            abi_assign_scalar(table, signature, locations, calls);
        }
        else
        {
            abi_assign(table, signature, locations, calls);
        }
        for (uint32_t b = part->first; b < part->end && status->status == CHECK_PROGRAM_OK; b += 1)
        {
            Tier0Function* function = &lowered->functions[b];
            const uint32_t signature_index = shared->signatures[b];
            const uint32_t first = abi->first[signature_index];
            const uint32_t declared = abi->counts[signature_index];
            function->parameters = locations + first;
            function->result = calls[signature_index].result;
            function->calls = calls + signature_index + 1;
            function->argument_locations = locations + first + declared;
            function->incoming_variadic = shared->incoming[b] ? calls + signature_index : 0;
            if (function->parameter_count != declared)
            {
                // The hidden pointer, parameter 0, in rdi before the declared parameters' locations.
                AbiLocation* parameters = BUSTER_ARENA_PUSH_ARRAY(output, AbiLocation, function->parameter_count);
                if (parameters == 0)
                {
                    status->status = CHECK_PROGRAM_RESOURCE;
                }
                else
                {
                    parameters[0] = (AbiLocation){ .registers = { BUSTER_ABI_RDI, BUSTER_ABI_REGISTER_NONE }, .kind = BUSTER_ABI_LOCATION_REGISTERS };
                    memcpy(parameters + 1, locations + first, (uint64_t)declared * sizeof(AbiLocation));
                    function->parameters = parameters;
                }
            }
            // The stream's rules, by the vector path where it takes the stream and by the scalar validator otherwise, which
            // --scalar runs alone (#569).
            Tier0Check check = { .error = TIER0_OK };
            if (status->status == CHECK_PROGRAM_OK)
            {
                check = shared->scalar ? tier0_validate_scalar(function) : tier0_validate(function);
            }
            if (check.error != TIER0_OK)
            {
                status->status = CHECK_PROGRAM_INVALID;
                status->token = program->bodies[b].declaration;
                status->backend_node = check.node;
                status->reason = check.rule;
            }
        }
        part->stage = status->status != CHECK_PROGRAM_OK ? CHECK_LOWER_STAGE_FRAME : CHECK_LOWER_STAGE_NONE;
    }
    part->status = part->stage == CHECK_LOWER_STAGE_ROOM ? CHECK_PROGRAM_RESOURCE : status->status;
    part->token = part->stage == CHECK_LOWER_STAGE_ROOM ? BUSTER_CHECK_NONE : status->token;
    part->backend_node = status->backend_node;
    part->reason = part->stage == CHECK_LOWER_STAGE_ROOM ? NULL : status->reason;
    part->label_symbols = status->label_symbols;
    part->label_symbol_count = status->label_symbol_count;
    writer->label_symbol_used += status->label_symbol_count;
    part->runtime_count = writer->runtime_count;
    part->expression_nodes = writer->expression_nodes;
    part->batch_nodes = writer->batch_nodes;
    part->classified_nodes = writer->classified_nodes;
    memcpy(part->runtimes, writer->runtimes, CHECK_LOWER_RUNTIMES);
}

// A part's lowering (check_lower_part_body), plain: every compile's without -g. Out of line, so that the program's
// lowering (check_program_lower) and the emission's lanes (check_program_emit_lanes) share one copy of its loops.
static __attribute__((noinline)) void check_lower_part(CheckLowerWriter* writer, const CheckLowerShared* shared, CheckLowerPart* part, Arena* output, Arena* scratch)
{
    check_lower_part_body(writer, shared, part, false, output, scratch);
}

// The same with each body's entries' source tokens (CheckProgramLowered.entry_tokens), for -g's rows: a copy of its own.
static __attribute__((noinline)) void check_lower_part_sources(CheckLowerWriter* writer, const CheckLowerShared* shared, CheckLowerPart* part, Arena* output,
                                                               Arena* scratch)
{
    check_lower_part_body(writer, shared, part, true, output, scratch);
}

// The program's lowering from its parts' (`count` of them, in source order, which together hold every body): their first
// failure in source order's lowering's order (CheckLowerStage, and between the bodies' walks and the locations' room,
// every label symbol's place), into `lowered`; and without one, the label symbols, the runtime functions' symbols
// numbered in the order the bodies first call them and every call of one its symbol, and the entries. A failure at the
// label symbols or the convention is at the token source order's writer had after the last body, the last part's.
static bool check_lower_finish(CheckProgramLowered* lowered, const CheckLowerShared* shared, const CheckLowerPart* parts, uint32_t count)
{
    const CheckProgram* program = shared->program;
    const CheckLowerPart* failed = NULL;
    uint32_t label_symbols = 0;
    for (uint32_t stage = CHECK_LOWER_STAGE_BODY; failed == NULL && stage <= CHECK_LOWER_STAGE_FRAME; stage += 1)
    {
        for (uint32_t p = 0; failed == NULL && p < count; p += 1)
        {
            failed = parts[p].stage == stage ? parts + p : NULL;
        }
        if (stage == CHECK_LOWER_STAGE_BODY && failed == NULL)
        {
            // A label symbol without its function's place, or more than the program has, is the lowering's own failure.
            for (uint32_t p = 0; p < count; p += 1)
            {
                label_symbols += parts[p].label_symbol_count;
            }
            if (label_symbols != shared->label_symbol_capacity + lowered->case_symbol_count)
            {
                lowered->status = CHECK_PROGRAM_INVALID;
                lowered->token = count != 0 ? parts[count - 1].final_token : BUSTER_CHECK_NONE;
                stage = CHECK_LOWER_STAGE_FRAME;
            }
        }
    }
    if (failed != NULL)
    {
        lowered->status = failed->status;
        lowered->token = failed->stage == CHECK_LOWER_STAGE_CONVENTION ? parts[count - 1].final_token : failed->token;
        lowered->backend_node = failed->backend_node;
        lowered->reason = failed->reason;
    }
    bool valid = lowered->status == CHECK_PROGRAM_OK;
    uint32_t written = 0;
    uint64_t runtimes = 0;
    for (uint32_t p = 0; valid && p < count; p += 1)
    {
        if (parts[p].label_symbol_count != 0)
        {
            memcpy(lowered->label_symbols + written, parts[p].label_symbols, (uint64_t)parts[p].label_symbol_count * sizeof(SymbolId));
            written += parts[p].label_symbol_count;
        }
        lowered->entries += parts[p].entries;
        lowered->expression_nodes += parts[p].expression_nodes;
        lowered->batch_nodes += parts[p].batch_nodes;
        lowered->classified_nodes += parts[p].classified_nodes;
        for (uint32_t i = 0; i < parts[p].runtime_count; i += 1)
        {
            const uint32_t runtime = parts[p].runtimes[i];
            if (lowered->runtime_symbols[runtime] == 0)
            {
                lowered->runtime_symbols[runtime] = (SymbolId)((uint64_t)program->symbol_count + lowered->helpers.count + lowered->initializers.count + lowered->runtime_symbol_count + 1);
                lowered->runtime_order[lowered->runtime_symbol_count++] = (uint8_t)runtime;
            }
            runtimes |= UINT64_C(1) << runtime;
        }
    }
    lowered->label_symbol_count = valid ? written : 0;
    // Every call of a runtime function takes its symbol (check_lower_runtime's provisional one until now).
    for (uint32_t b = 0; valid && runtimes != 0 && b < program->body_count; b += 1)
    {
        Tier0Function* function = &lowered->functions[b];
        SymbolId* targets = (SymbolId*)function->call_targets;
        for (uint32_t c = 0; c < function->call_count; c += 1)
        {
            const uint64_t runtime = (uint64_t)targets[c] - shared->provisional_runtimes;
            targets[c] = runtime < CHECK_LOWER_RUNTIMES ? lowered->runtime_symbols[runtime] : targets[c];
        }
    }
    // The jump tables' symbols and their labels', now that the runtime's functions are all known: the lowering's own, after
    // theirs. The labels' are numbered in label_symbols, and the tables' in their dispatches' GLOBAL_ADDRESS entries (the
    // emission's merge replaces the provisional symbols in the code's references).
    const uint64_t case_symbol_base = (uint64_t)program->symbol_count + lowered->helpers.count + lowered->initializers.count + lowered->runtime_symbol_count + 1;
    if (valid && lowered->table_count != 0 && case_symbol_base + lowered->table_count + lowered->case_symbol_count >= BUSTER_CHECK_LOWER_CASE_SYMBOL)
    {
        lowered->status = CHECK_PROGRAM_RESOURCE;
        valid = false;
    }
    else if (valid && lowered->table_count != 0)
    {
        lowered->case_symbol_base = (SymbolId)case_symbol_base;
        for (uint32_t k = 0; k < lowered->label_symbol_count; k += 1)
        {
            const SymbolId symbol = lowered->label_symbols[k];
            const bool table_label = (symbol & BUSTER_CHECK_LOWER_CASE_SYMBOL) != 0;
            lowered->label_symbols[k] = table_label ? lowered->case_symbol_base + lowered->table_count + (symbol & ~BUSTER_CHECK_LOWER_CASE_SYMBOL) : symbol;
        }
        for (uint32_t t = 0; t < lowered->table_count; t += 1)
        {
            const CheckLowerTable* table = lowered->tables + t;
            uint64_t* values = (uint64_t*)lowered->functions[table->function].values;
            values[table->addresses[0]] = lowered->case_symbol_base + t;
            values[table->addresses[1]] = lowered->case_symbol_base + t;
        }
    }
    return valid;
}

// The lowering, as check_program_lower and check_program_lower_sources compile it: with `sources`, each function's entries'
// source tokens too (CheckProgramLowered.entry_tokens). Its bodies are one part, lowered by one writer
// (check_lower_part).
static inline __attribute__((always_inline)) bool check_program_lower_body(CheckProgramLowered* lowered, const CheckProgram* program, bool scalar, bool sources,
                                                                          Arena* output, Arena* scratch)
{
    (void)scalar;

    const uint64_t mark = output->length;
    const uint64_t temporary = scratch->length;
    CheckLowerShared shared;
    CheckLowerWriter writer;
    CheckLowerAbi abi;
    CheckProgramLowered status;
    CheckLowerPart part = { .first = 0, .end = program->body_count };
    bool valid = check_lower_prepare(&shared, lowered, program, BUSTER_SCALAR_PATH(scalar), sources, output, scratch);
    if (valid && !check_lower_writer_begin(&writer, &abi, &status, &shared, sources, scratch))
    {
        lowered->status = CHECK_PROGRAM_RESOURCE;
        valid = false;
    }
    if (valid)
    {
        if (sources)
        {
            check_lower_part_sources(&writer, &shared, &part, output, scratch);
        }
        else
        {
            check_lower_part(&writer, &shared, &part, output, scratch);
        }
        valid = check_lower_finish(lowered, &shared, &part, 1);
    }
    if (valid)
    {
        lowered->count = program->body_count;
        lowered->output_bytes = output->length - mark;
    }
    else
    {
        check_lower_failed(lowered, output, mark);
    }
    arena_pop_to(scratch, temporary);
    return valid;
}

static bool check_program_lower(CheckProgramLowered* lowered, const CheckProgram* program, bool scalar, Arena* output, Arena* scratch)
{
    (void)scalar;

    return check_program_lower_body(lowered, program, BUSTER_SCALAR_PATH(scalar), false, output, scratch);
}

// The lowering with each function's entries' source tokens (CheckProgramLowered.entry_tokens), for -g's rows: a copy of
// its own (check_program_lower_body).
static __attribute__((noinline)) bool check_program_lower_sources(CheckProgramLowered* lowered, const CheckProgram* program, bool scalar, Arena* output,
                                                                  Arena* scratch)
{
    (void)scalar;

    return check_program_lower_body(lowered, program, BUSTER_SCALAR_PATH(scalar), true, output, scratch);
}
