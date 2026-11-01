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
// from the floating types; the complex types' multiplication and division, of each floating element (__float128's
// too); __float128's arithmetic, comparisons and conversions (check_lower_quad_compute, check_lower_quad_conversion);
// the products, quotients and remainders of _BitInts past 128 bits, and their conversions to and from the floating
// types (check_lower_limb_library, check_lower_limb_conversion); and the half-precision types' extension and roundings
// where the function's target has no instruction for them (c_check_program_half.c); and the C library's mathematical
// functions that clang -O0 calls for the generic vector builtins (check_lower_vector_calls: acosf ... powf, acos ... pow
// and acosl ... powl, each name's float, double and long double ones, in BUSTER_CHECK_LOWER_LIBM's order), with fmaxl,
// fminl and ldexpl, a long double's maxnum, minnum and ldexp; __builtin_powi's (#148); and the library builtins'
// functions (c_builtin_library.c), from CHECK_LOWER_RUNTIME_LIBRARY on, in its list's order: library function l's is
// CHECK_LOWER_RUNTIME_LIBRARY + l.
#define BUSTER_CHECK_LOWER_LIBM(X) \
    X(ACOS, "acos") \
    X(ASIN, "asin") \
    X(ATAN, "atan") \
    X(COS, "cos") \
    X(COSH, "cosh") \
    X(EXP, "exp") \
    X(EXP10, "exp10") \
    X(EXP2, "exp2") \
    X(LOG, "log") \
    X(LOG10, "log10") \
    X(LOG2, "log2") \
    X(SIN, "sin") \
    X(SINH, "sinh") \
    X(TAN, "tan") \
    X(TANH, "tanh") \
    X(CEIL, "ceil") \
    X(FLOOR, "floor") \
    X(TRUNC, "trunc") \
    X(RINT, "rint") \
    X(NEARBYINT, "nearbyint") \
    X(ROUNDEVEN, "roundeven") \
    X(ROUND, "round") \
    X(ATAN2, "atan2") \
    X(FMOD, "fmod") \
    X(POW, "pow")

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
    CHECK_LOWER_RUNTIME_ADDTF3,
    CHECK_LOWER_RUNTIME_SUBTF3,
    CHECK_LOWER_RUNTIME_MULTF3,
    CHECK_LOWER_RUNTIME_DIVTF3,
    CHECK_LOWER_RUNTIME_LTTF2,
    CHECK_LOWER_RUNTIME_LETF2,
    CHECK_LOWER_RUNTIME_GTTF2,
    CHECK_LOWER_RUNTIME_GETF2,
    CHECK_LOWER_RUNTIME_EQTF2,
    CHECK_LOWER_RUNTIME_NETF2,
    CHECK_LOWER_RUNTIME_EXTENDSFTF2,
    CHECK_LOWER_RUNTIME_EXTENDDFTF2,
    CHECK_LOWER_RUNTIME_EXTENDXFTF2,
    CHECK_LOWER_RUNTIME_TRUNCTFSF2,
    CHECK_LOWER_RUNTIME_TRUNCTFDF2,
    CHECK_LOWER_RUNTIME_TRUNCTFXF2,
    CHECK_LOWER_RUNTIME_FLOATSITF,
    CHECK_LOWER_RUNTIME_FLOATUNSITF,
    CHECK_LOWER_RUNTIME_FLOATDITF,
    CHECK_LOWER_RUNTIME_FLOATUNDITF,
    CHECK_LOWER_RUNTIME_FLOATTITF,
    CHECK_LOWER_RUNTIME_FLOATUNTITF,
    CHECK_LOWER_RUNTIME_FIXTFSI,
    CHECK_LOWER_RUNTIME_FIXUNSTFSI,
    CHECK_LOWER_RUNTIME_FIXTFDI,
    CHECK_LOWER_RUNTIME_FIXUNSTFDI,
    CHECK_LOWER_RUNTIME_FIXTFTI,
    CHECK_LOWER_RUNTIME_FIXUNSTFTI,
    CHECK_LOWER_RUNTIME_MULBITINT3,
    CHECK_LOWER_RUNTIME_DIVMODBITINT4,
    CHECK_LOWER_RUNTIME_FLOATBITINTSF,
    CHECK_LOWER_RUNTIME_FLOATBITINTDF,
    CHECK_LOWER_RUNTIME_FLOATBITINTXF,
    CHECK_LOWER_RUNTIME_FLOATBITINTTF,
    CHECK_LOWER_RUNTIME_FIXSFBITINT,
    CHECK_LOWER_RUNTIME_FIXDFBITINT,
    CHECK_LOWER_RUNTIME_FIXXFBITINT,
    CHECK_LOWER_RUNTIME_FIXTFBITINT,
    CHECK_LOWER_RUNTIME_MULTC3,
    CHECK_LOWER_RUNTIME_DIVTC3,
    CHECK_LOWER_RUNTIME_MEMCPY_CHK,
    CHECK_LOWER_RUNTIME_MEMMOVE_CHK,
    CHECK_LOWER_RUNTIME_MEMPCPY_CHK,
    CHECK_LOWER_RUNTIME_MEMSET_CHK,
    CHECK_LOWER_RUNTIME_STRCPY_CHK,
    CHECK_LOWER_RUNTIME_STPCPY_CHK,
    CHECK_LOWER_RUNTIME_STRNCPY_CHK,
    CHECK_LOWER_RUNTIME_STPNCPY_CHK,
    CHECK_LOWER_RUNTIME_STRCAT_CHK,
    CHECK_LOWER_RUNTIME_STRNCAT_CHK,
    CHECK_LOWER_RUNTIME_SPRINTF_CHK,
    CHECK_LOWER_RUNTIME_SNPRINTF_CHK,
    CHECK_LOWER_RUNTIME_VSPRINTF_CHK,
    CHECK_LOWER_RUNTIME_VSNPRINTF_CHK,
    CHECK_LOWER_RUNTIME_MEMPCPY,
    CHECK_LOWER_RUNTIME_MEMSET,
    CHECK_LOWER_RUNTIME_STRCPY,
    CHECK_LOWER_RUNTIME_STPCPY,
    CHECK_LOWER_RUNTIME_STRNCPY,
    CHECK_LOWER_RUNTIME_STPNCPY,
    CHECK_LOWER_RUNTIME_STRCAT,
    CHECK_LOWER_RUNTIME_STRNCAT,
    CHECK_LOWER_RUNTIME_SPRINTF,
    CHECK_LOWER_RUNTIME_SNPRINTF,
    CHECK_LOWER_RUNTIME_VSPRINTF,
    CHECK_LOWER_RUNTIME_VSNPRINTF,
    CHECK_LOWER_RUNTIME_EXTENDHFSF2,
    CHECK_LOWER_RUNTIME_TRUNCSFHF2,
    CHECK_LOWER_RUNTIME_TRUNCDFHF2,
    CHECK_LOWER_RUNTIME_TRUNCXFHF2,
    CHECK_LOWER_RUNTIME_TRUNCSFBF2,
    CHECK_LOWER_RUNTIME_TRUNCDFBF2,
    CHECK_LOWER_RUNTIME_TRUNCXFBF2,
    CHECK_LOWER_RUNTIME_FLOATTIHF,
    CHECK_LOWER_RUNTIME_FLOATUNTIHF,
    CHECK_LOWER_RUNTIME_FIXHFTI,
    CHECK_LOWER_RUNTIME_FIXUNSHFTI,
#define BUSTER_CHECK_LOWER_LIBM_ENUM(id, name) CHECK_LOWER_RUNTIME_LIBM_##id##F, CHECK_LOWER_RUNTIME_LIBM_##id, CHECK_LOWER_RUNTIME_LIBM_##id##L,
    BUSTER_CHECK_LOWER_LIBM(BUSTER_CHECK_LOWER_LIBM_ENUM)
#undef BUSTER_CHECK_LOWER_LIBM_ENUM
    CHECK_LOWER_RUNTIME_FMAXL,
    CHECK_LOWER_RUNTIME_FMINL,
    CHECK_LOWER_RUNTIME_LDEXPL,
    CHECK_LOWER_RUNTIME_POWIDF2,
    CHECK_LOWER_RUNTIME_POWISF2,
    CHECK_LOWER_RUNTIME_POWIXF2,
    CHECK_LOWER_RUNTIME_LIBRARY,
    CHECK_LOWER_RUNTIMES = CHECK_LOWER_RUNTIME_LIBRARY + CBUILTIN_LIBRARY_COUNT,
} CheckLowerRuntime;

// The libm functions' first, from which a name's float, double and long double ones follow (check_lower_libm_function).
#define BUSTER_CHECK_LOWER_LIBM_FIRST CHECK_LOWER_RUNTIME_LIBM_ACOSF

// The mask of the runtime functions a part calls has a bit each, in as many 64-bit words as that takes.
_Static_assert(CHECK_LOWER_RUNTIMES <= UINT16_MAX, "a part's runtime functions are numbered in 16 bits");

static const char* const check_lower_runtime_names[CHECK_LOWER_RUNTIMES] = {
    "__errno_location", "__divti3",    "__udivti3",   "__modti3",    "__umodti3",  "__floattisf",  "__floattidf",  "__floattixf",  "__floatuntisf",
    "__floatuntidf",    "__floatuntixf", "__fixsfti", "__fixdfti",   "__fixxfti",  "__fixunssfti", "__fixunsdfti", "__fixunsxfti", "__mulsc3",
    "__muldc3",         "__mulxc3",    "__divsc3",    "__divdc3",    "__divxc3", "memmove", "memcpy",
    "__atomic_load", "__atomic_store", "__atomic_exchange", "__atomic_compare_exchange", "__atomic_is_lock_free",
    "fminf", "fmin", "fmaxf", "fmax",
    "__addtf3", "__subtf3", "__multf3", "__divtf3", "__lttf2", "__letf2", "__gttf2", "__getf2", "__eqtf2", "__netf2",
    "__extendsftf2", "__extenddftf2", "__extendxftf2", "__trunctfsf2", "__trunctfdf2", "__trunctfxf2",
    "__floatsitf", "__floatunsitf", "__floatditf", "__floatunditf", "__floattitf", "__floatuntitf",
    "__fixtfsi", "__fixunstfsi", "__fixtfdi", "__fixunstfdi", "__fixtfti", "__fixunstfti",
    "__mulbitint3", "__divmodbitint4", "__floatbitintsf", "__floatbitintdf", "__floatbitintxf", "__floatbitinttf",
    "__fixsfbitint", "__fixdfbitint", "__fixxfbitint", "__fixtfbitint", "__multc3", "__divtc3",
    "__memcpy_chk",
    "__memmove_chk",
    "__mempcpy_chk",
    "__memset_chk",
    "__strcpy_chk",
    "__stpcpy_chk",
    "__strncpy_chk",
    "__stpncpy_chk",
    "__strcat_chk",
    "__strncat_chk",
    "__sprintf_chk",
    "__snprintf_chk",
    "__vsprintf_chk",
    "__vsnprintf_chk",
    "mempcpy",
    "memset",
    "strcpy",
    "stpcpy",
    "strncpy",
    "stpncpy",
    "strcat",
    "strncat",
    "sprintf",
    "snprintf",
    "vsprintf",
    "vsnprintf",
    "__extendhfsf2", "__truncsfhf2", "__truncdfhf2", "__truncxfhf2", "__truncsfbf2", "__truncdfbf2", "__truncxfbf2", "__floattihf",
    "__floatuntihf", "__fixhfti", "__fixunshfti",
#define BUSTER_CHECK_LOWER_LIBM_NAME(id, name) name "f", name, name "l",
    BUSTER_CHECK_LOWER_LIBM(BUSTER_CHECK_LOWER_LIBM_NAME)
#undef BUSTER_CHECK_LOWER_LIBM_NAME
    "fmaxl", "fminl", "ldexpl", "__powidf2", "__powisf2", "__powixf2",
#define BUSTER_CBUILTIN_LIBRARY_CALL(id, name) name,
#include "c_builtin_library_calls.inc"
#undef BUSTER_CBUILTIN_LIBRARY_CALL
};

// The runtime function a library builtin's call calls (#148): the lowering's own of its function's name where it has one
// before the library's (fmax, mempcpy, strcpy, sprintf, ...), so that a unit has one symbol of each name, or else the
// library's entry.
static CheckLowerRuntime check_lower_library_runtime(CBuiltinLibraryId library)
{
    CheckLowerRuntime result = (CheckLowerRuntime)(CHECK_LOWER_RUNTIME_LIBRARY + library);
    for (uint32_t r = 0; r < CHECK_LOWER_RUNTIME_LIBRARY && result >= CHECK_LOWER_RUNTIME_LIBRARY; r += 1)
    {
        result = strcmp(check_lower_runtime_names[r], cbuiltin_library_names[library]) == 0 ? (CheckLowerRuntime)r : result;
    }
    return result;
}

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
// KIND marks check_lower_kind_facts' entries alone, and no type's facts hold it (check_lower_classify_types drops it): a
// _BitInt's facts, which the rules alone give (check_lower_type_facts), take its bit.
#define BUSTER_CHECK_LOWER_TYPE_BIT_INT BUSTER_CHECK_LOWER_TYPE_KIND

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
    uint16_t runtime_order[CHECK_LOWER_RUNTIMES];
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
    // Half-precision chain link `node` computed in float (check_lower_half_task): the last kind, which the complex values'
    // compare takes too (check_lower_complex_task), so that the tasks' loop has no test of its own for it.
    CHECK_LOWER_HALF,
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
    // An expression of a _BitInt whose own entries check_lower_bit_int_node pushed: expanded as any other now.
    bool bit_int;
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
_Static_assert(CHECK_LOWER_HALF <= UINT8_MAX && sizeof(TypeId) == sizeof(uint32_t), "pending task tags and type identities fit their fields");

static inline __attribute__((always_inline)) CheckLowerTaskPacked check_lower_task_pack(CheckLowerTask task)
{
    const bool full = task.kind == CHECK_LOWER_INSTRUCTION || task.kind == CHECK_LOWER_ADDRESS || task.kind == CHECK_LOWER_COPY || task.kind == CHECK_LOWER_COMPLEX;
    return (CheckLowerTaskPacked){
        .control = task.node | (uint64_t)task.kind << 32 | (uint64_t)task.address << 40 | (uint64_t)task.initialized << 41 | (uint64_t)task.discard << 42 |
                   (uint64_t)task.volatile_copy << 43 | (uint64_t)task.bit_int << 44 |
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
        .bit_int = ((packed.control >> 44) & 1) != 0,
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
    // GNU's generic forms, whose values and results are where their arguments point (check_lower_atomic_generic).
    bool by_pointer;
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
    // Tail calls (#147's musttail): whether the function has one, which its TAIL_CALL entries' rows then say
    // (Tier0Function.call_tails); and the call node a musttail return lowers now (check_lower_tail_return), which
    // check_lower_call_body makes a TAIL_CALL.
    uint32_t tail;
    bool tails;
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
    // The frame temporaries, a U32's and a float's, through which half-precision bits move between the stacks where the
    // function's target hasn't AVX (check_lower_half_move), and a U16's, which a compare's right operand waits in
    // (check_lower_half_entry): check_lower_half_temporary's.
    uint32_t half_locals[3];
    // Whether the body whose opening token is `half_strict_body` - 1 has FENV_ACCESS anywhere (check_lower_half_strict):
    // the first half-precision fold or packed floating comparison asks, and the body's later uses read the answer.
    uint32_t half_strict_body;
    bool half_strict;
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
    // Whether the program's type table has a _BitInt type, the only one whose operations can have narrow operands
    // (check_lower_computed): a conversion, a constant, an update and a compare test their types for one only then.
    bool bit_ints;
    // A function whose result is a 128-bit integer returns it from this frame object's 16 bytes (Tier0Function.result_size,
    // as a struct of two INTEGER eightbytes returns), BUSTER_CHECK_NONE for any other.
    uint32_t result_object;
    // This function's shared return slot for CHECK_CONTROL_RESULT and its RETURN, after cleanups. Nested returns in a
    // statement expression use the same slot, so their expansion cannot rebind an enclosing return's pending store.
    // LOCAL_ADDRESS where `kept_address` says, of `kept_class` (BUSTER_CHECK_NONE, VOID: nothing).
    uint32_t kept_local;
    uint8_t kept_class;
    bool kept_address;
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
    // Each label's jump table (Tier0Function.label_tables, #785): 1 + t for the function's table t's, numbered as its
    // dispatches come, and 0 for a label whose address the program takes; and each table's JUMP_INDIRECT's entry
    // (Tier0Function.table_jumps), `table_jump_count` of them, with room for the program's tables.
    uint32_t* label_tables;
    uint32_t* table_jumps;
    uint32_t table_jump_count;
    // The writer's room for the program's label symbols (CheckProgramLowered.label_symbols), which its parts take one after
    // another (check_lower_part): `label_symbol_used` of it kept by the parts before the current one.
    SymbolId* label_symbol_room;
    uint32_t label_symbol_used;
    // The runtime's functions (check_lower_runtime): each one's symbol where the unit declares it, 0 for none
    // (CheckLowerShared.declared_runtimes); the first of the provisional symbols that a call of one of the lowering's own
    // takes; and the ones the writer's part calls first, in the order it does (`runtimes`, `runtime_mask` their bits in
    // words), which check_lower_finish numbers in source order's order (#151).
    const SymbolId* declared_runtimes;
    SymbolId provisional_runtimes;
    uint64_t runtime_mask[(CHECK_LOWER_RUNTIMES + 63) / 64];
    uint32_t runtime_count;
    uint16_t runtimes[CHECK_LOWER_RUNTIMES];
    // The first token where clang's code may classify a function's type (check_lower_first_classification), plus one: 0
    // until the writer first asks.
    uint64_t first_classification;
};


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

// A vector type's bytes, where Tier 0 takes its values (BUSTER_TYPED_CLASS_VECTOR): GNU C's and Clang's extended
// vectors of 4, 8, 16, 32 or 64 bytes, as the intrinsics' __m64 and __m128 to __m512i and the narrow vectors of their
// shuffles and conversions, of elements other than bool (whose extended vectors pack a bit per element); 0 for any
// other type. Atomic wrappers use these value bytes; ABI rows retain their declared atomic type, storage size and
// alignment separately.
static uint32_t check_lower_vector_bytes(const TypeTable* types, TypeId type)
{
    const TypeId base = type_conversion_operand(types, type);
    const uint8_t kind = type_kind(types, base);
    const uint64_t size = kind == BUSTER_TYPE_KIND_VECTOR || kind == BUSTER_TYPE_KIND_EXT_VECTOR ? type_size(types, base) : 0;
    const bool boolean = size != 0 && type_unqualified(type_strip_aligned(types, type_element(types, base))) == BUSTER_TYPE_BOOL;
    return (size == 4 || size == 8 || size == 16 || size == 32 || size == 64) && !boolean ? (uint32_t)size : 0;
}

// AMX's _tile1024i is a 1024-byte vector of int. Its C value and ABI are a memory object's, never a Tier 0 VECTOR's
// 64-byte slot. The internal intrinsics consume its packed active rows through that object's address.
static bool check_lower_tile_type(const TypeTable* types, TypeId type)
{
    const TypeId base = type_conversion_operand(types, type);
    return type_kind(types, base) == BUSTER_TYPE_KIND_VECTOR && type_size(types, base) == 1024 &&
           type_unqualified(type_strip_aligned(types, type_element(types, base))) == BUSTER_TYPE_INT;
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

// The classes the lowering gives the half-precision types (#136, c_check_program_half.c), past Tier 0's:
// binary16's (_Float16's and __fp16's) and bfloat16's (__bf16's). A value of one is its 16 bits on the integer stack,
// but no entry has the class: check_lower_emit writes the entries that move one as U16's and those that compare or
// return one as its format needs (check_lower_half_entry), and what computes with one takes it out of line, as a class
// past VECTOR, the last of Tier 0's, so that the tasks' loop tests nothing for them.
#define BUSTER_CHECK_LOWER_CLASS_BINARY16 BUSTER_TYPED_CLASSES
#define BUSTER_CHECK_LOWER_CLASS_BFLOAT16 (BUSTER_TYPED_CLASSES + 1)

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
        // and U128, which also live in memory: check_lower_memory_class), and _BitInt(65) to _BitInt(128). A _BitInt(N)
        // whose N is no class's width takes the narrowest class that holds it, its values extended from N bits in it
        // (check_lower_bit_int_shift).
        const TypeId integer = kind == BUSTER_TYPE_KIND_ENUM ? types->types.base[type_index(base)] : base;
        const bool wide = (bits == 128 && type_kind(types, integer) == BUSTER_TYPE_KIND_INTEGER) || (kind == BUSTER_TYPE_KIND_BIT_INT && bits > 64);
        result = bits == 1 ? BUSTER_TYPED_CLASS_U8 : bits == 8 ? BUSTER_TYPED_CLASS_I8 : bits == 16 ? BUSTER_TYPED_CLASS_I16 :
                 bits == 32 ? BUSTER_TYPED_CLASS_I32 : bits == 64 ? BUSTER_TYPED_CLASS_I64 : wide ? BUSTER_TYPED_CLASS_I128 : UINT8_MAX;
        if (result == UINT8_MAX && bits < 64 && kind == BUSTER_TYPE_KIND_BIT_INT)
        {
            result = bits < 8 ? BUSTER_TYPED_CLASS_I8 : bits < 16 ? BUSTER_TYPED_CLASS_I16 : bits < 32 ? BUSTER_TYPED_CLASS_I32 : BUSTER_TYPED_CLASS_I64;
        }
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
    else if (check_action_half_format(base) != CHECK_HALF_NONE)
    {
        // An _Atomic one's too (#137): its loads and stores are the atomics' of its 2 bytes, through buffers whose LOCAL
        // and SET_LOCAL of the class are a U16's (check_lower_half_entry), and its value the type's.
        result = check_action_half_format(base) == CHECK_HALF_IEEE ? BUSTER_CHECK_LOWER_CLASS_BINARY16 : BUSTER_CHECK_LOWER_CLASS_BFLOAT16;
    }
    return result;
}

// Whether a class is a half-precision type's (BUSTER_CHECK_LOWER_CLASS_BINARY16 or _BFLOAT16), and its format.
static CheckHalf check_lower_class_half(uint8_t class)
{
    return class == BUSTER_CHECK_LOWER_CLASS_BINARY16 ? CHECK_HALF_IEEE : class == BUSTER_CHECK_LOWER_CLASS_BFLOAT16 ? CHECK_HALF_BRAIN : CHECK_HALF_NONE;
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

// A complex type's element, unqualified, for one the lowering takes, or 0: a floating element (_Complex _Float16,
// __bf16, float, double, long double and __float128: the half-precision ones carried and converted, not computed with,
// check_lower_complex_compute), or an integer one: GNU's complex integer types (_Complex char to _Complex unsigned
// __int128), and the complex _BitInts that the usual arithmetic conversions make (#137), whose parts Tier 0's classes
// hold as a _BitInt's values (check_lower_type_class) up to 128 bits, and past them frame objects of their limbs. C can't
// spell a _Complex __bf16, but the usual arithmetic conversions make one (of a _Complex int and a __bf16), which clang
// passes as a <2 x bfloat>, as it passes a _Complex _Float16 as a <2 x half> (#137).
static TypeId check_lower_complex_element(const TypeTable* types, TypeId type)
{
    const TypeId base = type_conversion_operand(types, type);
    const TypeId element = type_kind(types, base) == BUSTER_TYPE_KIND_COMPLEX ? type_unqualified(type_strip_aligned(types, type_element(types, base))) : 0;
    // _Complex __float128's parts are __float128s, and a complex _BitInt's past 128 bits are _BitInts past 128 bits: values
    // carried by their addresses (check_lower_complex_parts).
    const bool floating = element == BUSTER_TYPE_FLOAT16 || element == BUSTER_TYPE_BF16 || element == BUSTER_TYPE_FLOAT || element == BUSTER_TYPE_DOUBLE ||
                          element == BUSTER_TYPE_LONG_DOUBLE || element == BUSTER_TYPE_FLOAT128;
    const bool integer = element >= BUSTER_TYPE_CHAR && element <= BUSTER_TYPE_UINT128;
    const bool bit_int = element != 0 && type_kind(types, element) == BUSTER_TYPE_KIND_BIT_INT;
    return floating || integer || bit_int ? element : 0;
}

// A _BitInt past 128 bits (#137): its 64-bit limbs, its width N and its signedness, after its qualifiers, an aligned
// typedef and _Atomic; no limbs for any other type. Its value is its limbs' frame object's address, an aggregate's
// (check_lower_limb_arithmetic says how its operations go).
typedef struct CheckLowerLimbs
{
    uint32_t count;
    uint32_t width;
    bool sign;
} CheckLowerLimbs;

static CheckLowerLimbs check_lower_limb_type(const TypeTable* types, TypeId type)
{
    const TypeId base = type_conversion_operand(types, type);
    CheckLowerLimbs limbs = { 0 };
    if (type_kind(types, base) == BUSTER_TYPE_KIND_BIT_INT)
    {
        uint64_t rank = 0;
        uint64_t bits = 0;
        bool sign = false;
        type_integer_properties(types, base, &rank, &bits, &sign);
        if (bits > 128)
        {
            limbs = (CheckLowerLimbs){ .count = (uint32_t)((bits + 63) / 64), .width = (uint32_t)bits, .sign = sign };
        }
    }
    return limbs;
}

// Whether a unit's types hold a _BitInt past 128 bits: a scan for the kind, which a unit without a _BitInt ends at once.
static bool check_lower_limb_types(const TypeTable* types)
{
    const uint8_t* kinds = types->types.kind;
    const uint8_t* at = memchr(kinds, BUSTER_TYPE_KIND_BIT_INT, types->types.count);
    bool found = false;
    while (at != NULL && !found)
    {
        const uint32_t index = (uint32_t)(at - kinds);
        found = types->types.extent[index] > 128;
        at = index + 1 < types->types.count ? memchr(at + 1, BUSTER_TYPE_KIND_BIT_INT, types->types.count - index - 1) : NULL;
    }
    return found;
}

// The room a node of `type` takes in check_lower_limb_room, in units of 64 entries a limb, and whether it is complex: a
// _BitInt's past 128 bits its limbs; a complex one's of such parts four times its element's, as both its parts go
// through the element's computations (a quotient's four products, two sums and a quotient a part,
// check_lower_complex_parts_compute) and an update converts both of them there and back; none for any other type.
static uint32_t check_lower_limb_room_units(const TypeTable* types, TypeId type, bool* complex)
{
    const TypeId element = check_lower_complex_element(types, type);
    const uint32_t units = element != 0 ? 4 * check_lower_limb_type(types, element).count : check_lower_limb_type(types, type).count;
    *complex = *complex || (element != 0 && units != 0);
    return units;
}

// The room a unit's _BitInts past 128 bits take beyond a node's (check_lower_prepare's bounds): the entries, the
// temporaries and frame objects (`locals`), and the calls of libgcc's functions and memmove, and their arguments.
typedef struct CheckLowerLimbRoom
{
    uint64_t entries;
    uint64_t locals;
    uint64_t calls;
    uint64_t arguments;
} CheckLowerLimbRoom;

// The room a unit's _BitInts past 128 bits take (CheckLowerLimbRoom), none in a unit without one
// (check_lower_limb_types): each node that computes, converts or updates one writes up to 64 entries a limb
// (check_lower_limb_arithmetic and the rest go a limb at a time, an update converting, computing and converting back),
// the most of its value's, its operands' and their converted types (check_lower_limb_room_units, four times a complex
// one's element's), and takes up to 16 temporaries and frame objects, or 128 for a complex one's, whose parts'
// computations each take their own. A complex one also makes up to 24 calls of 128 arguments in all beyond a node's
// 3 calls: a quotient's ten of libgcc's (four products and a quotient a part, of 6 and 8 arguments), memmove's copies
// of parts past 256 bytes, and an update's conversions of both parts there and back. Out of line, as few units have such
// a type: the preparation's own loops compile as they do without it.
static __attribute__((noinline)) CheckLowerLimbRoom check_lower_limb_room(const CheckProgram* program)
{
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const TypeTable* types = program->types;
    const uint32_t count = check_lower_limb_types(types) ? graph->count : 0;
    CheckLowerLimbRoom room = { 0 };
    for (uint32_t node = 0; node < count; node += 1)
    {
        const uint32_t operands[2] = { graph->operand0[node], graph->operand1[node] };
        bool complex = false;
        uint32_t units = check_lower_limb_room_units(types, stream->values[node].type, &complex);
        for (uint32_t i = 0; i < 2; i += 1)
        {
            const uint32_t own = operands[i] < graph->count ? check_lower_limb_room_units(types, stream->values[operands[i]].type, &complex) : 0;
            const uint32_t converted = check_lower_limb_room_units(types, stream->conversions[node].operands[i].type, &complex);
            units = own > units ? own : units;
            units = converted > units ? converted : units;
        }
        room.entries += 64 * (uint64_t)units;
        room.locals += complex ? 128 : units != 0 ? 16 : 0;
        room.calls += complex ? 24 : 0;
        room.arguments += complex ? 128 : 0;
    }
    return room;
}

// A type's facts for the lowering (BUSTER_CHECK_LOWER_TYPE_*), from its aligned typedef's type (type_strip_aligned):
// whether it is a struct, a union, a complex type the lowering takes, __float128, a _BitInt past 128 bits or an AMX tile, an
// aggregate value, which the stream carries as
// its object's address (check_lower_aggregate); the complex one (check_lower_complex); whether its value is its address,
// which nothing loads through: a function's, an array's or an aggregate's; whether it is a pointer; whether it is
// _Bool; whether it is _Atomic, whose lvalues load atomically (check_lower_atomic_prepare); and whether it is a _BitInt,
// whose expressions check_lower_bit_int_node takes first (the batch's templates take none of these last two,
// c_check_program_batch.c). BUSTER_CHECK_LOWER_TYPE_KIND marks the kinds whose types take their class and facts from the
// kind alone (check_lower_kind_facts), in that table alone, and BIT_INT takes its bit in the facts.
// A type's facts (BUSTER_CHECK_LOWER_TYPE_*): the rule that the table of every type's holds (check_lower_classify_types),
// which the bodies read (check_lower_facts).
static uint8_t check_lower_type_facts(const TypeTable* types, TypeId type)
{
    const bool atomic = type_kind(types, type_strip_aligned(types, type)) == BUSTER_TYPE_KIND_ATOMIC;
    const TypeId base = type_conversion_operand(types, type);
    const uint8_t kind = type_kind(types, base);
    const bool complex = check_lower_complex_element(types, type) != 0;
    // A __float128's value is its 16-byte object's address, an aggregate's (check_lower_quad), and so is a _BitInt's past
    // 128 bits, of its limbs (check_lower_limb_type).
    const bool aggregate = kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION || complex || base == BUSTER_TYPE_FLOAT128 ||
                           check_lower_limb_type(types, type).count != 0 || check_lower_tile_type(types, type);
    const bool address = kind == BUSTER_TYPE_KIND_FUNCTION || type_kind_is_array(kind) || aggregate;
    return (aggregate ? BUSTER_CHECK_LOWER_TYPE_AGGREGATE : 0) | (complex ? BUSTER_CHECK_LOWER_TYPE_COMPLEX : 0) | (address ? BUSTER_CHECK_LOWER_TYPE_ADDRESS : 0) |
           (kind == BUSTER_TYPE_KIND_POINTER ? BUSTER_CHECK_LOWER_TYPE_POINTER : 0) | (base == BUSTER_TYPE_BOOL ? BUSTER_CHECK_LOWER_TYPE_BOOL : 0) |
           (atomic ? BUSTER_CHECK_LOWER_TYPE_ATOMIC : 0) | (kind == BUSTER_TYPE_KIND_BIT_INT ? BUSTER_CHECK_LOWER_TYPE_BIT_INT : 0);
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

// Whether a type's value is carried as its object's address: a struct, union, supported complex type, __float128,
// large _BitInt or AMX tile. Each consumer copies its bytes or reads a member, element or part through that address.
// The numeric aggregate paths take complex, quad and limb arithmetic; a tile's arithmetic remains unsupported.
static bool check_lower_aggregate(const CheckLowerWriter* writer, TypeId type)
{
    return (check_lower_facts(writer, type) & BUSTER_CHECK_LOWER_TYPE_AGGREGATE) != 0;
}

// Whether a type is __float128 (#137), after its qualifiers, an aligned typedef and _Atomic: an aggregate's value, its
// 16-byte object's address, whose operations are libgcc's (check_lower_quad_compute).
static bool check_lower_quad(const CheckLowerWriter* writer, TypeId type)
{
    return type_conversion_operand(writer->program->types, type) == BUSTER_TYPE_FLOAT128;
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

// A _BitInt(N) narrower than its class (check_lower_type_class: unsigned _BitInt(1), every N below 64 but 8, 16 and 32,
// and every N from 65 to 127, of the 128-bit classes): the count that a left shift and a right shift by it extend its low
// N bits through the class with, signed or not by the class, as clang stores such a value; 0 for every other type. Its values stay so extended, as the class's
// operations, compares and conversions read them: what may leave other bits (a sum, a difference, a product, a left
// shift, a negation, a complement, a value from memory or a call, a conversion to it) is extended again after it.
static uint32_t check_lower_bit_int_shift(const TypeTable* types, TypeId type)
{
    const TypeId base = type_unqualified(type_strip_aligned(types, type));
    uint32_t bits = 0;
    bool sign = false;
    uint32_t shift = 0;
    if (type_kind(types, base) == BUSTER_TYPE_KIND_BIT_INT && check_action_integer_type(types, base, &bits, &sign))
    {
        const uint32_t width = bits <= 8 ? 8 : bits <= 16 ? 16 : bits <= 32 ? 32 : bits <= 64 ? 64 : 128;
        shift = width - bits;
    }
    return shift;
}

// An expression's class on the stack: check_lower_class's, or U64 for an aggregate (its address).
static uint8_t check_lower_value_class(const CheckLowerWriter* writer, TypeId type)
{
    return check_lower_aggregate(writer, type) ? BUSTER_TYPED_CLASS_U64 : check_lower_class(writer, type);
}

// A type's half-precision format (CheckHalf, c_check_program_half.c's values): an _Atomic one's value's too, as its class
// is (check_lower_type_class), whose loads and stores are the atomics'.
static CheckHalf check_lower_half(const TypeTable* types, TypeId type)
{
    return check_action_half_format(type_conversion_operand(types, type));
}

// A type's half-precision format from its cached class (check_lower_class_half of check_lower_class's).
static CheckHalf check_lower_half_of(const CheckLowerWriter* writer, TypeId type)
{
    return check_lower_class_half(check_lower_class(writer, type));
}

// A type's class where its values are stored, read and passed along in Tier 0's entries: check_lower_value_class's, or
// U16 for a half-precision value's bits (c_check_program_half.c).
static uint8_t check_lower_storage_class(const CheckLowerWriter* writer, TypeId type)
{
    const uint8_t class = check_lower_value_class(writer, type);
    return check_lower_class_half(class) != CHECK_HALF_NONE ? BUSTER_TYPED_CLASS_U16 : class;
}

// What a value of a type check_lower_class has no class for needs, named for the diagnostic. An aggregate's value is
// its address, so the type of one that reaches here is passed or returned by value.
static const char* check_lower_type_feature(const TypeTable* types, TypeId type)
{
    const TypeId base = type_unqualified(type_strip_aligned(types, type));
    const uint8_t kind = type_kind(types, base);
    const char* feature = "values of this type";
    if (base == BUSTER_TYPE_FLOAT16 || base == BUSTER_TYPE_HALF || base == BUSTER_TYPE_BF16)
    {
        // A place that takes no half-precision value (check_lower_storage_class's are the ones that do).
        feature = "half-precision values here";
    }
    else if (kind == BUSTER_TYPE_KIND_FLOATING)
    {
        feature = "__float128";
    }
    else if (kind == BUSTER_TYPE_KIND_COMPLEX)
    {
        // One check_lower_complex doesn't take: a complex type of another floating type.
        feature = "complex types other than _Complex _Float16, __bf16, float, double, long double and __float128";
    }
    else if (kind == BUSTER_TYPE_KIND_BIT_INT)
    {
        feature = "_BitInt";
    }
    else if (kind == BUSTER_TYPE_KIND_VECTOR || kind == BUSTER_TYPE_KIND_EXT_VECTOR)
    {
        feature = type_unqualified(type_strip_aligned(types, type_element(types, base))) == BUSTER_TYPE_BOOL ? "vectors of bool" :
                  "vector types of other than 4, 8, 16, 32 or 64 bytes";
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

// Whether a non-bool vector has neither an ordinary vector class nor the AMX tile's aggregate transport.
static bool check_lower_classless_vector(const CheckLowerWriter* writer, TypeId type)
{
    const TypeTable* types = writer->program->types;
    const TypeId base = type_unqualified(type_strip_aligned(types, type));
    const uint8_t kind = type_kind(types, base);
    return (kind == BUSTER_TYPE_KIND_VECTOR || kind == BUSTER_TYPE_KIND_EXT_VECTOR) && !check_lower_aggregate(writer, type) && check_lower_class(writer, type) == UINT8_MAX &&
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

// A transparent union's first member, unqualified, or 0 for another type (#147). clang classifies an argument, a
// parameter and va_arg's type that is one as that member (useFirstFieldIfTransparentUnion), whose value is the union's
// bytes; a result keeps the union's own classification. An _Atomic union isn't one.
static TypeId check_lower_transparent(const CheckLowerWriter* writer, TypeId type)
{
    const TypeTable* types = writer->program->types;
    TypeId first = 0;
    if (type_is_transparent_union(types, type))
    {
        const uint32_t tag = types->types.index[type_index(type_strip_aligned(types, type))];
        first = type_unqualified(types->members.type[types->tags.member_begin[tag]]);
    }
    return first;
}

// The type an argument, a parameter or va_arg's operand of a struct or a union passes as: a transparent union's first
// member (check_lower_transparent), or the type itself.
static TypeId check_lower_passed_as(const CheckLowerWriter* writer, TypeId type)
{
    const TypeId first = check_lower_transparent(writer, type);
    return first != 0 ? first : type;
}

// The class a transparent union's first member passes in: an integer's or a pointer's (an enum's, _Bool's), or UINT8_MAX
// for a member passed by its bytes, as an aggregate is, from the union's object: a struct, a union, an array, a 128-bit
// integer, an _Atomic type or any other.
static uint8_t check_lower_transparent_class(const CheckLowerWriter* writer, TypeId first)
{
    const uint8_t class = check_lower_class(writer, first);
    const bool value = (check_lower_facts(writer, first) & (BUSTER_CHECK_LOWER_TYPE_ADDRESS | BUSTER_CHECK_LOWER_TYPE_ATOMIC)) == 0;
    return value && class >= BUSTER_TYPED_CLASS_I8 && class <= BUSTER_TYPED_CLASS_U64 ? class : UINT8_MAX;
}

// Whether static object declaration `token`'s initializer names a function (check_lower_first_classification): one of
// its tokens bound to a function's declaration.
static bool check_lower_initializer_names_function(const CheckInput* input, uint32_t token)
{
    const uint32_t count = input->token_count;
    const uint32_t end = input->initializer_ends[token] < count ? input->initializer_ends[token] : count;
    bool named = false;
    for (uint32_t use = input->initializer_begins[token]; use < end && !named; use += 1)
    {
        const uint32_t declaration = input->declarations[use];
        named = declaration < count && input->declaration_kinds[declaration] == C_BINDING_FUNCTION;
    }
    return named;
}

// The first token where clang's code may classify a function's type, which it keeps for the type's every use
// (check_lower_transparent_late), or UINT32_MAX for a unit with none: the first function definition's body, or a static
// object's initializer before it that names a function, whose address clang's code takes where it emits the object, at
// its definition. clang 23.1.4 passes a union to `int f(union U)` as the union after `int (*p)(union U) = f;`, though a
// later declaration makes it transparent before any definition. Every such initializer counts, though clang's code
// emits a static object's when a function first uses it, and so does every function, though only the function's own
// type keeps its classification: the earliest place clang's code may classify, so that a union it may pass either way
// is refused. A lane's writer finds it the first time it asks (CheckLowerWriter.first_classification), which only a
// transparent union whose first member passes otherwise than it makes it do. AVX-512 finds the static objects'
// declarations with initializers 16 tokens at a time; the rest and the `scalar` path test every token.
static __attribute__((noinline)) uint32_t check_lower_first_classification(CheckLowerWriter* writer)
{
    if (writer->first_classification == 0)
    {
        const CheckProgram* program = writer->program;
        const CheckInput* input = program->input;
        const uint32_t limit = program->body_count != 0 ? program->bodies[0].opening : input->token_count;
        const uint32_t none = program->body_count != 0 ? limit : UINT32_MAX;
        const __m512i lanes = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
        uint32_t found = none;
        uint32_t token = 0;
        for (; !BUSTER_SCALAR_PATH(writer->scalar) && found == none && limit - token >= BUSTER_CHECK_PROGRAM_LANES; token += BUSTER_CHECK_PROGRAM_LANES)
        {
            const __m512i tokens = _mm512_add_epi32(_mm512_set1_epi32((int)token), lanes);
            const __mmask16 initialized = _mm512_cmpeq_epi32_mask(_mm512_loadu_si512(input->declarations + token), tokens) &
                                          _mm_cmpneq_epi8_mask(_mm_loadu_si128((const __m128i*)(input->static_objects + token)), _mm_setzero_si128()) &
                                          _mm512_cmplt_epu32_mask(_mm512_loadu_si512(input->initializer_begins + token), _mm512_loadu_si512(input->initializer_ends + token));
            for (uint32_t bits = initialized; bits != 0 && found == none; bits &= bits - 1)
            {
                const uint32_t declaration = token + (uint32_t)__builtin_ctz(bits);
                found = check_lower_initializer_names_function(input, declaration) ? declaration : none;
            }
        }
        for (; found == none && token < limit; token += 1)
        {
            if (input->declarations[token] == token && input->static_objects[token] && input->initializer_begins[token] < input->initializer_ends[token] &&
                check_lower_initializer_names_function(input, token))
            {
                found = token;
            }
        }
        writer->first_classification = (uint64_t)found + 1;
    }
    return (uint32_t)(writer->first_classification - 1);
}

// Whether a transparent union was made so, by a later declaration's attribute (CheckGraph.transparent_unions has its
// activation's token), after clang's code may have classified a function's type (check_lower_first_classification).
// clang's code classifies a function's type the first time it meets it and keeps that for the type's every use: a type
// met before the attribute passes the union as the union, after it too.
static bool check_lower_transparent_late(CheckLowerWriter* writer, TypeId type)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const uint32_t tag = program->types->types.index[type_index(type_strip_aligned(program->types, type))];
    return graph->transparent_unions != NULL && tag < graph->tag_completion_count && graph->transparent_unions[tag] > check_lower_first_classification(writer);
}

// Why a struct or a union can't pass as a transparent union's first member (check_lower_passed_as), or NULL (another's
// passes as itself). A transparent union passes so where it has that member's size and alignment, as it does but by an
// aligned or packed attribute of its own: clang's code takes such a union's registers from the member and its stack slot
// from the union (getIndirectResult), which differ. And where the union was made transparent after clang's code may have
// classified a function's type (check_lower_transparent_late) and its first member passes otherwise than it (its
// registers, its stack or its extension), clang's code may pass it as either. Out of line: only aggregates ask.
static __attribute__((noinline)) const char* check_lower_transparent_passed(CheckLowerWriter* writer, TypeId type)
{
    const AbiTable* table = writer->abi_table;
    const uint32_t places = BUSTER_ABI_PASS_REGISTER_CLASSES | BUSTER_ABI_PASS_VECTOR | BUSTER_ABI_PASS_WIDE | BUSTER_ABI_PASS_MEMORY | BUSTER_ABI_PASS_UNCOUNTED |
                            BUSTER_ABI_PASS_IGNORE | BUSTER_ABI_PASS_PAIR | BUSTER_ABI_PASS_SIGN | BUSTER_ABI_PASS_ZERO | BUSTER_ABI_PASS_LOW_GENERAL;
    const uint32_t own = type_index(type);
    const uint32_t first = type_index(check_lower_passed_as(writer, type));
    const char* feature = NULL;
    if (table->stack_bytes[own] != table->stack_bytes[first] || table->alignment_log2[own] != table->alignment_log2[first])
    {
        feature = "transparent unions sized or aligned otherwise than their first member";
    }
    else if (first != own && ((table->pass[own] ^ table->pass[first]) & places) != 0 && check_lower_transparent_late(writer, type))
    {
        feature = "unions made transparent after a function definition or an initializer naming a function, whose first members pass otherwise";
    }
    return feature;
}

// Why an argument or a parameter of this type can't be passed, or NULL: a value of a class, or a struct or a union (and a
// transparent union as its first member, check_lower_transparent_passed).
static const char* check_lower_passed(CheckLowerWriter* writer, TypeId type)
{
    const TypeTable* types = writer->program->types;
    const char* feature = NULL;
    if (check_lower_aggregate(writer, type))
    {
        feature = check_lower_by_value(writer, type, false);
        feature = feature == NULL ? check_lower_transparent_passed(writer, type) : feature;
    }
    else if (check_lower_class(writer, type) == UINT8_MAX)
    {
        feature = check_lower_type_feature(types, type);
    }
    return feature;
}

// check_lower_passed's for a call's parameters and arguments (check_lower_call_body), but a half-precision type's is
// check_lower_half_passed: a call that passes one is check_lower_call_halves', which a call without one doesn't reach.
static const char check_lower_half_passed[] = "half-precision arguments";

static const char* check_lower_call_passed(CheckLowerWriter* writer, TypeId type)
{
    const TypeTable* types = writer->program->types;
    const char* feature = NULL;
    if (check_lower_aggregate(writer, type))
    {
        feature = check_lower_by_value(writer, type, false);
        feature = feature == NULL ? check_lower_transparent_passed(writer, type) : feature;
    }
    else if (check_lower_class(writer, type) >= BUSTER_TYPED_CLASSES)
    {
        feature = check_lower_class(writer, type) == UINT8_MAX ? check_lower_type_feature(types, type) : check_lower_half_passed;
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
    else if (kind == BUSTER_TYPED_CALL || kind == BUSTER_TYPED_TAIL_CALL)
    {
        // An indirect call (callee 0) pops the address it calls too, which the stream pushed before the arguments. A tail
        // call's class is VOID.
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
    else if (kind == BUSTER_TYPED_RETURN || kind == BUSTER_TYPED_JUMP_INDIRECT || kind == BUSTER_TYPED_TAIL_CALL)
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

// Appends an entry of a class the function takes: check_lower_emit's, and the entries of the half-precision classes'
// (check_lower_half_entry), which check_lower_emit writes through it.
static inline __attribute__((always_inline)) void check_lower_append(CheckLowerWriter* writer, uint8_t kind, uint8_t class, uint64_t value)
{
    const uint32_t count = writer->count;
    if (count >= writer->capacity || count >= BUSTER_TIER0_MAX_ENTRIES)
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

static __attribute__((noinline)) void check_lower_emit_classless(CheckLowerWriter* writer, uint8_t kind, uint8_t class, uint64_t value);

// Appends an entry. Its function's memory classes (Tier0Function.x87 and int128) come from its entries' classes once it
// is lowered (check_lower_entry_classes). A class past the function's (CheckLowerWriter.class_limit) is
// check_lower_emit_classless's, out of line.
static void check_lower_emit(CheckLowerWriter* writer, uint8_t kind, uint8_t class, uint64_t value)
{
    if (class >= writer->class_limit)
    {
        check_lower_emit_classless(writer, kind, class, value);
    }
    else
    {
        check_lower_append(writer, kind, class, value);
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
// conversion normalizes even a _Bool's value, nor a _BitInt, whose conversion extends a value from its width
// (check_lower_scalar_conversion), and `from` is a _BitInt only where `to` is one: a _BitInt past 128 bits, an
// aggregate, converts to another aggregate, a __float128 (check_lower_limb_conversion). An array's decay to a pointer, a
// pointer's to another and a change of qualifiers are such conversions, which need no task.
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
    return source == target && source < BUSTER_TYPED_CLASSES && ((from_facts ^ to_facts) & (BUSTER_CHECK_LOWER_TYPE_AGGREGATE | BUSTER_CHECK_LOWER_TYPE_BIT_INT)) == 0 &&
           ((from_facts | to_facts) & BUSTER_CHECK_LOWER_TYPE_COMPLEX) == 0 && (to_facts & (BUSTER_CHECK_LOWER_TYPE_BOOL | BUSTER_CHECK_LOWER_TYPE_BIT_INT)) == 0;
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

// check_lower_temporary's local of a class past Tier 0's (BUSTER_TYPED_CLASSES and above), out of line: a half-precision
// value's is a frame object of its 2 bytes, which LOCAL, STORE_LOCAL and SET_LOCAL of U16 read and write
// (check_lower_half_entry), and which a parameter of one is (check_lower_part_body allocates it here) as a struct of it
// is: the prologue stores the XMM register it arrives in there; any other class's is refused.
static __attribute__((noinline)) uint32_t check_lower_temporary_classless(CheckLowerWriter* writer, uint8_t class)
{
    uint32_t local = writer->local_count;
    if (check_lower_class_half(class) == CHECK_HALF_NONE)
    {
        check_lower_unsupported(writer, "values of this type");
    }
    else
    {
        local = check_lower_object(writer, 2, 1);
    }
    return local;
}

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
        local = check_lower_temporary_classless(writer, class);
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

// The entries of an operator at a vector type of bf16 elements (check_vector_lower_brain_rows), or its refusal, which
// clang's bfloat16 excess precision computes in float where the unit's target hasn't AVX10.2: its row's X86 entry, its
// operands the stack's when it runs; or a 64-byte vector's three rows under excess precision, its operands set into frame
// temporaries of its bytes (the last operand first), each half's row after the temporaries' addresses, then the join's.
static __attribute__((noinline)) void check_lower_vector_brain(CheckLowerWriter* writer, uint32_t op, TypeId vector, uint8_t scalar_class)
{
    Tier0Operation rows[3];
    uint32_t count = 0;
    const bool excess = (writer->program->target.extensions & BUSTER_X86_EXTENSION(AVX10_2)) == 0;
    const uint32_t bytes = check_lower_vector_bytes(writer->program->types, vector);
    const char* reason = check_vector_lower_brain_rows(&writer->target, excess, op, bytes, scalar_class, rows, &count);
    if (reason != NULL)
    {
        check_lower_unsupported(writer, reason);
    }
    else if (count == 1)
    {
        check_lower_instruction(writer, BUSTER_TYPED_X86, BUSTER_TYPED_CLASS_VECTOR, check_lower_operation_index(writer, rows));
    }
    else
    {
        // Pushed last first: the join, each half's row and the addresses before it, then the stores, the last operand's
        // first.
        const uint32_t operands = rows[0].operand_count;
        uint32_t temporaries[2] = { BUSTER_CHECK_NONE, BUSTER_CHECK_NONE };
        for (uint32_t j = 0; j < operands; j += 1)
        {
            temporaries[j] = check_lower_temporary_of(writer, vector);
        }
        check_lower_instruction(writer, BUSTER_TYPED_X86, BUSTER_TYPED_CLASS_VECTOR, check_lower_operation_index(writer, rows + 2));
        for (uint32_t h = 2; h-- > 0;)
        {
            check_lower_instruction(writer, BUSTER_TYPED_X86, BUSTER_TYPED_CLASS_VECTOR, check_lower_operation_index(writer, rows + h));
            for (uint32_t j = operands; j-- > 0;)
            {
                check_lower_instruction(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, temporaries[j]);
            }
        }
        for (uint32_t j = 0; j < operands; j += 1)
        {
            check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_VECTOR, temporaries[j]);
        }
    }
}

// An X86 entry of an operator's row at a vector type (check_vector_lower_row), or its refusal: its operands are the
// stack's when it runs. Half-precision elements have clang's excess precision where the unit's target hasn't AVX512-FP16
// (check_lower_vector_half_chain), and bf16 elements' operators are check_lower_vector_brain's.
static void check_lower_vector_row(CheckLowerWriter* writer, uint32_t op, TypeId vector, uint8_t scalar_class)
{
    Tier0Operation row;
    if (check_vector_lower_brain(writer->program->types, vector))
    {
        check_lower_vector_brain(writer, op, vector, scalar_class);
    }
    else
    {
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
}

// clang's _Float16 excess precision (#136): where the unit's target hasn't AVX512-FP16, clang computes +, -, * and / of
// half-precision elements and their negation in float, and rounds to them only where a value leaves the chain of those
// operators (a store, a cast, a compare, a call's argument, a conditional's or a comma's operand), even in a function
// whose target attribute adds the extension. One operator's row in half precision rounds as clang's does (float's 24 bits
// are at least twice half's 11 and 2 more, so its two roundings round as once, NaNs propagating alike), and a negation's
// quiets the signaling NaNs as clang's float conversions do (check_vector_lower_row's `excess`). A chain's doesn't: an
// operator (`op`, one of those) whose operand `a` or `b` (or BUSTER_CHECK_NONE) is of half-precision elements and itself
// one of them, or a unary plus, is refused. So is bf16 elements' where the unit's target hasn't AVX10.2, whose one
// operator check_vector_lower_brain_rows computes in float as clang does.
static void check_lower_vector_half_chain(CheckLowerWriter* writer, TypeId vector, uint32_t op, uint32_t a, uint32_t b)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckVectorElement element = check_vector_lower_element(program->types, vector);
    const bool brain = check_vector_lower_brain(program->types, vector);
    const bool excess = (element.floating && element.bytes == 2 && (program->target.extensions & BUSTER_X86_EXTENSION(AVX512FP16)) == 0) ||
                        (brain && (program->target.extensions & BUSTER_X86_EXTENSION(AVX10_2)) == 0);
    const bool arithmetic = op == CHECK_OP_ADD || op == CHECK_OP_SUBTRACT || op == CHECK_OP_MULTIPLY || op == CHECK_OP_DIVIDE || op == CHECK_OP_NEGATIVE;
    bool chained = false;
    for (uint32_t i = 0; excess && arithmetic && i < 2; i += 1)
    {
        const uint32_t operand = i == 0 ? a : b;
        const CheckOp kind = operand != BUSTER_CHECK_NONE ? graph->ops[operand] : CHECK_OP_VALUE;
        const bool link = kind == CHECK_OP_ADD || kind == CHECK_OP_SUBTRACT || kind == CHECK_OP_MULTIPLY || kind == CHECK_OP_DIVIDE || kind == CHECK_OP_NEGATIVE ||
                          kind == CHECK_OP_POSITIVE;
        const TypeId type = link ? program->stream->values[operand].type : 0;
        const CheckVectorElement linked = link ? check_vector_lower_element(program->types, type) : (CheckVectorElement){ 0 };
        chained |= brain ? link && check_vector_lower_brain(program->types, type) : linked.floating && linked.bytes == 2;
    }
    if (chained)
    {
        check_lower_unsupported(writer, brain ? "chained arithmetic of bf16 vectors, which clang computes in float without AVX10.2" :
                                                "chained arithmetic of half-precision vectors, which clang computes in float without AVX512-FP16");
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
static uint32_t check_lower_wide_division(CheckLowerWriter* writer, uint8_t kind, uint8_t class, uint32_t* a, uint32_t* b);
static void check_lower_quad_conversion(CheckLowerWriter* writer, TypeId from, TypeId to);
static bool check_lower_complex_parts(const CheckLowerWriter* writer, TypeId type);
static void check_lower_complex_parts_conversion(CheckLowerWriter* writer, TypeId from, TypeId to);

// A _BitInt past 128 bits' limbs (check_lower_limb_type), none for any other type.
static CheckLowerLimbs check_lower_limbs(const CheckLowerWriter* writer, TypeId type)
{
    return check_lower_limb_type(writer->program->types, type);
}

// Whether a type is a _BitInt past 128 bits, from its facts alone: an aggregate that is a _BitInt (check_lower_type_facts).
static bool check_lower_limb_value(const CheckLowerWriter* writer, TypeId type)
{
    const uint8_t limbs = BUSTER_CHECK_LOWER_TYPE_AGGREGATE | BUSTER_CHECK_LOWER_TYPE_BIT_INT;
    return (check_lower_facts(writer, type) & limbs) == limbs;
}

static void check_lower_limb_conversion(CheckLowerWriter* writer, TypeId from, TypeId to);

// A conversion of the value on top of the stack between `from` and `to`, either one an aggregate (and `to` not void): a
// _BitInt's past 128 bits to or from another type, check_lower_limb_conversion's; a __float128's to or from a scalar,
// check_lower_quad_conversion's; any other's nothing, as an aggregate converts to its own (compatible) type alone, and
// GNU's cast to a union isn't lowered.
static __attribute__((noinline)) void check_lower_aggregate_conversion(CheckLowerWriter* writer, TypeId from, TypeId to)
{
    const TypeTable* types = writer->program->types;
    const bool from_aggregate = check_lower_aggregate(writer, from);
    const bool to_aggregate = check_lower_aggregate(writer, to);
    const bool limbs = check_lower_limb_value(writer, from) || check_lower_limb_value(writer, to);
    if (((from_aggregate && check_lower_tile_type(types, from)) || (to_aggregate && check_lower_tile_type(types, to))) &&
        type_conversion_operand(types, from) != type_conversion_operand(types, to))
    {
        check_lower_unsupported(writer, "conversions or splats of 1024-byte tile vectors");
    }
    else if (limbs && type_conversion_operand(types, from) != type_conversion_operand(types, to))
    {
        check_lower_limb_conversion(writer, from, to);
    }
    else if (from_aggregate != to_aggregate && (check_lower_quad(writer, from) || check_lower_quad(writer, to)))
    {
        check_lower_quad_conversion(writer, from, to);
    }
    else if (from_aggregate != to_aggregate)
    {
        check_lower_unsupported(writer, "casts to union types");
    }
}

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

// The class an operation on values of `class` computes in: Tier 0 computes, compares and shifts in I32, U32, I64 and U64
// alone, so a value of class I8, U8, I16 or U16 (a _BitInt's alone, as C promotes the other narrow types) is converted to
// I32 or U32 by its signedness for it, and its result back.
static uint8_t check_lower_computed(uint8_t class)
{
    return check_lower_narrow(class) ? (uint8_t)((class & 1) != 0 ? BUSTER_TYPED_CLASS_I32 : BUSTER_TYPED_CLASS_U32) : class;
}

// The value on top, of `type`, converted to its computing class (check_lower_computed) unless it is in it (`computed`),
// extended from a _BitInt's width through that class (check_lower_bit_int_shift's), and converted back to its own: the
// entries emitted now, or pushed as instructions, which run in the reverse order (`push`). A type whose width is its
// class's, a _BitInt's of 8, 16, 32 or 64 bits too, takes only the conversions: back, when `computed`.
static void check_lower_bit_int_extend(CheckLowerWriter* writer, TypeId type, bool computed, bool push)
{
    const uint8_t class = check_lower_class(writer, type);
    const uint8_t computing = check_lower_computed(class);
    const uint32_t shift = check_lower_bit_int_shift(writer->program->types, type);
    // A narrow class's count is its computing class's (32 bits) less its own width's place.
    const uint32_t count = shift != 0 ? shift + 8 * (tier0_class_bytes(computing) - tier0_class_bytes(class)) : 0;
    uint8_t kinds[6];
    uint8_t classes[6];
    uint64_t values[6];
    uint32_t length = 0;
#define BUSTER_CHECK_LOWER_ENTRY(k, c, v) do { kinds[length] = (k); classes[length] = (c); values[length] = (v); length += 1; } while (0)
    if (!computed && computing != class && count != 0)
    {
        BUSTER_CHECK_LOWER_ENTRY(BUSTER_TYPED_CONVERT, computing, 0);
    }
    if (count != 0)
    {
        BUSTER_CHECK_LOWER_ENTRY(BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U32, count);
        BUSTER_CHECK_LOWER_ENTRY(BUSTER_TYPED_SHL, computing, 0);
        BUSTER_CHECK_LOWER_ENTRY(BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U32, count);
        BUSTER_CHECK_LOWER_ENTRY(BUSTER_TYPED_SHR, computing, 0);
    }
    if (computing != class && (computed || count != 0))
    {
        BUSTER_CHECK_LOWER_ENTRY(BUSTER_TYPED_CONVERT, class, 0);
    }
#undef BUSTER_CHECK_LOWER_ENTRY
    for (uint32_t i = 0; i < length; i += 1)
    {
        const uint32_t at = push ? length - 1 - i : i;
        if (push)
        {
            check_lower_instruction(writer, kinds[at], classes[at], values[at]);
        }
        else
        {
            check_lower_emit(writer, kinds[at], classes[at], values[at]);
        }
    }
}

// A 128-bit integer of class `source` on top of the stack converted to floating class `target`: libgcc's __floattisf ...
// __floatuntixf of the value, from a frame object of its own.
static void check_lower_wide_to_floating(CheckLowerWriter* writer, uint8_t source, uint8_t target)
{
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

// A floating value of class `source` on top of the stack converted to 128-bit integer class `target`: libgcc's __fixsfti
// ... __fixunsxfti, its result from a frame object of its own.
static void check_lower_wide_from_floating(CheckLowerWriter* writer, uint8_t source, uint8_t target)
{
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

#include "c_check_program_half.c"

// check_lower_emit's entry of a class past the function's (CheckLowerWriter.class_limit), out of line: a half-precision
// class's, which check_lower_half_entry writes as Tier 0's; any other is refused, a vector's where the function's target
// hasn't AVX-512F and AVX-512VL.
static __attribute__((noinline)) void check_lower_emit_classless(CheckLowerWriter* writer, uint8_t kind, uint8_t class, uint64_t value)
{
    if (check_lower_class_half(class) != CHECK_HALF_NONE)
    {
        check_lower_half_entry(writer, kind, class, value);
    }
    else
    {
        check_lower_unsupported(writer, class != BUSTER_TYPED_CLASS_VECTOR ? "values of this type" :
                                        check_lower_vector_target(writer->program->target.features) ? "vector types in a function whose target attribute turns off AVX-512F or AVX-512VL" :
                                        "vector types without AVX-512F and AVX-512VL");
    }
}

static void check_lower_truth(CheckLowerWriter* writer, TypeId type, bool negate)
{
    const uint8_t class = check_lower_class(writer, type);
    const bool narrow = check_lower_narrow(class);
    if (check_lower_aggregate(writer, type))
    {
        // A complex value's or a __float128's (check_lower_complex_truth).
        check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_COMPLEX_TRUTH, .type = type, .address = negate });
    }
    else if (class >= BUSTER_TYPED_CLASS_VECTOR)
    {
        // VECTOR, the last of Tier 0's classes; the half-precision classes past it, whose truth is their float's; and no
        // class, whose CONST is refused as it is written.
        if (class == BUSTER_TYPED_CLASS_VECTOR)
        {
            check_lower_unsupported(writer, "vector logical operators and conditions");
        }
        else if (check_lower_class_half(class) != CHECK_HALF_NONE)
        {
            check_lower_half_truth(writer, type, negate);
        }
        else
        {
            check_lower_instruction(writer, negate ? BUSTER_TYPED_EQ : BUSTER_TYPED_NE, BUSTER_TYPED_CLASS_I32, 0);
            check_lower_instruction(writer, BUSTER_TYPED_CONST, class, 0);
        }
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

// The row of a cast between a vector of 4 or 8 bytes and an integer of its bytes (check_vector_cast's), or of a bit
// cast between it and an integer, a float or a double of its bytes, of the scalar's class: its bits moved between the
// vector's XMM register and a general one (vmovd, vmovq) or the scalar's XMM register (vmovq), into the vector (`into`)
// or out of it.
static void check_lower_vector_bits(CheckLowerWriter* writer, uint8_t class, uint32_t bytes, bool into)
{
    Tier0Operation row;
    memset(&row, 0, sizeof(row));
    const bool wide = bytes == 8;
    const bool floating = class == BUSTER_TYPED_CLASS_F32 || class == BUSTER_TYPED_CLASS_F64;
    row.record_count = 1;
    row.operand_count = 1;
    if (into && floating)
    {
        // The register's first 8 bytes: past a float's 4 they are the vector's past its 4, which nothing reads.
        row.records[0] = check_vector_lower_record(X86_FORM_VMOVQ_X_XM64_EVEX, BUSTER_TIER0_ROLE_VECTOR(0), 0, BUSTER_TIER0_ROLE_FLOATING(0));
    }
    else if (into)
    {
        row.records[0] = check_vector_lower_record(wide ? X86_FORM_VMOVQ_X_RM64_EVEX : X86_FORM_VMOVD_X_RM32_EVEX, BUSTER_TIER0_ROLE_VECTOR(0), 0,
                                                   BUSTER_TIER0_ROLE_INTEGER(0));
    }
    else if (floating)
    {
        row.records[0] = check_vector_lower_record(X86_FORM_VMOVQ_X_XM64_EVEX, BUSTER_TIER0_ROLE_FLOATING_RESULT, 0, BUSTER_TIER0_ROLE_VECTOR(0));
    }
    else
    {
        row.records[0] = check_vector_lower_record(wide ? X86_FORM_VMOVQ_RM64_X_EVEX : X86_FORM_VMOVD_RM32_X_EVEX, BUSTER_TIER0_ROLE_VECTOR(0), 0,
                                                   BUSTER_TIER0_ROLE_INTEGER_RESULT);
    }
    if (into)
    {
        row.result_class = BUSTER_TYPED_CLASS_VECTOR;
        row.result_bytes = (uint8_t)bytes;
        row.operand_classes[0] = class;
    }
    else
    {
        row.result_class = class;
        row.operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
        row.operand_bytes[0] = (uint8_t)bytes;
    }
    check_lower_instruction(writer, BUSTER_TYPED_X86, row.result_class, check_lower_operation_index(writer, &row));
    writer->vectors = true;
}

// A scalar's splat into the vector `to` (#136), of the value on top of the stack: it converts to the element's type first,
// then sets every element to it (a row's, check_vector_lower_row), an element of 1 or 2 bytes from an int's of its
// signedness: vpbroadcastb and vpbroadcastw read a 32-bit register's low byte or word, and a row's integer operand is no
// narrower than its field (tier0_operation_role_valid). An operator's scalar operand and a conditional's scalar arm
// splat so (check_lower_vector_operand), and so does an ext_vector_type vector's cast of a scalar.
static void check_lower_vector_splat(CheckLowerWriter* writer, TypeId from, TypeId to)
{
    const TypeTable* types = writer->program->types;
    const uint8_t class = check_lower_class(writer, from);
    if (check_lower_storage_class(writer, from) < BUSTER_TYPED_CLASS_F80 &&
        (check_action_is_integer(types, from) || class == BUSTER_TYPED_CLASS_F32 || class == BUSTER_TYPED_CLASS_F64 || check_lower_half(types, from) != CHECK_HALF_NONE))
    {
        // A half-precision element's splat is its bits', a U16's, zero-extended rather than converted.
        const TypeId element = type_unqualified(type_strip_aligned(types, type_element(types, type_unqualified(type_strip_aligned(types, to)))));
        const bool half = check_lower_half(types, element) != CHECK_HALF_NONE;
        const uint8_t element_class = check_lower_storage_class(writer, element);
        const bool narrow = element_class >= BUSTER_TYPED_CLASS_I8 && element_class <= BUSTER_TYPED_CLASS_U16;
        const TypeId wide = element_class == BUSTER_TYPED_CLASS_U8 || element_class == BUSTER_TYPED_CLASS_U16 ? BUSTER_TYPE_UINT : BUSTER_TYPE_INT;
        check_lower_vector_row(writer, BUSTER_CHECK_VECTOR_SPLAT, to, narrow ? check_lower_class(writer, wide) : element_class);
        if (half)
        {
            check_lower_instruction(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U32, 0);
        }
        else if (narrow)
        {
            check_lower_convert(writer, element, wide);
        }
        check_lower_convert(writer, from, element);
    }
    else
    {
        check_lower_unsupported(writer, "splats of this type into vectors");
    }
}

// A conversion from or to a vector (#136), of the value on top of the stack: a cast between vectors of the same bytes
// is nothing, as their bits are the same; to void, a DISCARD. A cast between a GNU C vector of 4 or 8 bytes and an
// integer of its bytes (check_vector_cast's) is its bits (check_lower_vector_bits), where an ext_vector_type vector's
// cast of a scalar splats it (check_lower_vector_splat). An operator's scalar operand, whose conversion to the vector
// splats it however wide it is (a constant's of the vector's bytes too), splats at its operator
// (check_lower_vector_operand): so an integer's conversion to a GNU C vector of its bytes is a cast's here, and the tasks'
// loop, which lowers the casts, tests nothing for it. Of the other vectors, a cast between one and an integer isn't
// lowered. A conversion of neither a vector nor to one is a half-precision type's, whose class is past VECTOR too
// (check_lower_half_conversion).
static __attribute__((noinline)) void check_lower_vector_conversion(CheckLowerWriter* writer, TypeId from, TypeId to)
{
    const TypeTable* types = writer->program->types;
    const uint32_t source = check_lower_vector_bytes(types, from);
    const uint32_t target = check_lower_vector_bytes(types, to);
    const bool integer = check_action_is_integer(types, source == 0 ? from : to);
    if (source == 0 && target == 0)
    {
        check_lower_half_conversion(writer, from, to);
    }
    else if (source != 0 && target == source)
    {
    }
    else if (source != 0 && type_unqualified(type_strip_aligned(types, to)) == BUSTER_TYPE_VOID)
    {
        check_lower_emit(writer, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_VECTOR, 0);
    }
    else if (source == 0 && target != 0 && target < 16 && integer && type_size(types, from) == target && check_action_kind(types, to) == BUSTER_TYPE_KIND_VECTOR)
    {
        check_lower_vector_bits(writer, check_lower_class(writer, from), target, true);
    }
    else if (source == 0 && target != 0)
    {
        check_lower_vector_splat(writer, from, to);
    }
    else if (source != 0 && source < 16 && target == 0 && integer && type_size(types, to) == source)
    {
        check_lower_vector_bits(writer, check_lower_class(writer, to), source, false);
    }
    else
    {
        check_lower_unsupported(writer, "casts between vectors and integers or vectors of other sizes");
    }
}

// Operand `operand` of node `node`, a vector operator's or a vector conditional's arm (#136), converted to the vector
// type `type`: a scalar's splat (check_lower_vector_splat), which the operator makes itself, as the conversion of an
// integer to a GNU C vector of its bytes is a cast's, its bits (check_lower_vector_conversion).
static void check_lower_vector_operand(CheckLowerWriter* writer, uint32_t node, uint32_t operand, TypeId type)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const uint32_t child = operand == 0 ? graph->operand0[node] : operand == 1 ? graph->operand1[node] : graph->operand2[node];
    const TypeId from = child < program->stream->count ? program->stream->values[child].type : 0;
    if (from != 0 && check_lower_vector_bytes(program->types, from) == 0 && check_lower_vector_bytes(program->types, type) != 0)
    {
        check_lower_vector_splat(writer, from, type);
        check_lower_operand(writer, node, operand, false, from);
    }
    else
    {
        check_lower_operand(writer, node, operand, false, type);
    }
}

// A conversion of the value on top of the stack from `from` to `to`, neither of them complex (but to void): a complex
// one's are check_lower_complex_conversion's, which converts its parts here. Always inlined into the copies of the
// tasks' loop (check_lower_drain_body), through check_lower_conversion_body, with `bit_ints` CheckLowerWriter.bit_ints,
// which each copy has as a constant.
static inline __attribute__((always_inline)) void check_lower_scalar_conversion_body(CheckLowerWriter* writer, TypeId from, TypeId to, bool bit_ints)
{
    const TypeTable* types = writer->program->types;
    const uint8_t source = check_lower_value_class(writer, from);
    const uint8_t target = check_lower_value_class(writer, to);
    // A conversion from or to an aggregate, but to void, is out of line (check_lower_aggregate_conversion).
    const bool mixed = (check_lower_aggregate(writer, from) || check_lower_aggregate(writer, to)) && target != BUSTER_TYPED_CLASS_VOID;
    if (source >= BUSTER_TYPED_CLASS_VECTOR || target >= BUSTER_TYPED_CLASS_VECTOR || mixed)
    {
        // VECTOR is the last of Tier 0's classes, and the half-precision classes are past it: a vector's conversions and
        // theirs are out of line (check_lower_vector_conversion).
        if (!mixed && source != UINT8_MAX && target != UINT8_MAX)
        {
            check_lower_vector_conversion(writer, from, to);
        }
        else if (mixed)
        {
            check_lower_aggregate_conversion(writer, from, to);
        }
        else
        {
            check_lower_unsupported(writer, check_lower_type_feature(types, source == UINT8_MAX ? from : to));
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
        check_lower_wide_to_floating(writer, source, target);
    }
    else if (check_lower_wide_class(target) && (source == BUSTER_TYPED_CLASS_F32 || source == BUSTER_TYPED_CLASS_F64 || source == BUSTER_TYPED_CLASS_F80))
    {
        check_lower_wide_from_floating(writer, source, target);
    }
    else if (bit_ints && check_lower_bit_int_shift(types, to) != 0)
    {
        // A _BitInt narrower than its class: converted to the class it computes in, extended from its width there, and
        // converted to its own (check_lower_bit_int_extend).
        if (source != check_lower_computed(target))
        {
            check_lower_emit(writer, BUSTER_TYPED_CONVERT, check_lower_computed(target), 0);
        }
        check_lower_bit_int_extend(writer, to, true, false);
    }
    else if (source != target)
    {
        check_lower_emit(writer, BUSTER_TYPED_CONVERT, target, 0);
    }
}

static inline __attribute__((always_inline)) void check_lower_scalar_conversion(CheckLowerWriter* writer, TypeId from, TypeId to)
{
    check_lower_scalar_conversion_body(writer, from, to, writer->bit_ints);
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

// Part `part` of `operand` onto the stack, of its element's class: a complex one's loaded from its address, a real one's
// value, or for its imaginary part zero (CONST 0, which for F80 is the constants' place 0). A _BitInt's part from memory,
// whose bits past its width may be any (another compiler's argument's), is extended from its width
// (check_lower_bit_int_extend), as every _BitInt value from memory is.
static void check_lower_complex_part(CheckLowerWriter* writer, CheckLowerComplexOperand operand, uint32_t part, TypeId element)
{
    const uint8_t class = check_lower_class(writer, element);
    if (operand.complex)
    {
        check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, operand.local);
        check_lower_offset(writer, part * type_size(writer->program->types, element));
        check_lower_emit(writer, BUSTER_TYPED_LOAD, class, 0);
        check_lower_bit_int_extend(writer, element, false, false);
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

// Part `part` of `operand` onto the stack in `computing`, its element's class or, for a complex integer type of 8 or 16
// bits, the 32-bit class Tier 0 computes in (check_lower_computed).
static void check_lower_complex_value(CheckLowerWriter* writer, CheckLowerComplexOperand operand, uint32_t part, TypeId element, uint8_t computing)
{
    check_lower_complex_part(writer, operand, part, element);
    if (check_lower_half_of(writer, element) != CHECK_HALF_NONE && computing == BUSTER_TYPED_CLASS_F32)
    {
        check_lower_scalar_conversion(writer, element, BUSTER_TYPE_FLOAT);
    }
    else if (computing != check_lower_class(writer, element))
    {
        check_lower_emit(writer, BUSTER_TYPED_CONVERT, computing, 0);
    }
}

// The value on top, computed in `computing`, as clang's code of a complex integer type of `element` has it: wrapped to
// the element's width, a _BitInt's extended from it (check_lower_bit_int_extend), and back in `computing` (nothing for a
// class Tier 0 computes in itself, of its own width).
static void check_lower_complex_wrap(CheckLowerWriter* writer, TypeId element, uint8_t computing)
{
    check_lower_bit_int_extend(writer, element, true, false);
    if (computing != check_lower_class(writer, element))
    {
        check_lower_emit(writer, BUSTER_TYPED_CONVERT, computing, 0);
    }
}

// The quotient of the two values on top, of `class`: a 128-bit integer's by libgcc's call (check_lower_wide_division,
// emitted now, as check_lower_operation pushes it), any other's DIV.
static void check_lower_complex_divide(CheckLowerWriter* writer, uint8_t class)
{
    if (check_lower_wide_class(class))
    {
        uint32_t a = 0;
        uint32_t b = 0;
        const uint32_t call = check_lower_wide_division(writer, BUSTER_TYPED_DIV, class, &a, &b);
        check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, class, b);
        check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, class, a);
        check_lower_emit(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, a);
        check_lower_emit(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, b);
        check_lower_emit(writer, BUSTER_TYPED_CALL, BUSTER_TYPED_CLASS_VOID, call);
        check_lower_emit(writer, BUSTER_TYPED_LOCAL, class, a);
    }
    else
    {
        check_lower_emit(writer, BUSTER_TYPED_DIV, class, 0);
    }
}

// A complex integer quotient's part `part` (#137), as clang -O0 computes it in the element's type whatever the operands'
// domains (a real one has an imaginary part of 0): (ac + bd) / (cc + dd) for the real part and (bc - ad) / (cc + dd) for
// the imaginary one, each product and sum wrapped to the element's width before the division.
static void check_lower_complex_quotient(CheckLowerWriter* writer, CheckLowerComplexOperand a, CheckLowerComplexOperand b, uint32_t part, TypeId element,
                                         uint8_t computing)
{
    check_lower_complex_value(writer, a, part, element, computing);
    check_lower_complex_value(writer, b, 0, element, computing);
    check_lower_emit(writer, BUSTER_TYPED_MUL, computing, 0);
    check_lower_complex_value(writer, a, 1 - part, element, computing);
    check_lower_complex_value(writer, b, 1, element, computing);
    check_lower_emit(writer, BUSTER_TYPED_MUL, computing, 0);
    check_lower_emit(writer, part == 0 ? BUSTER_TYPED_ADD : BUSTER_TYPED_SUB, computing, 0);
    check_lower_complex_wrap(writer, element, computing);
    for (uint32_t k = 0; k < 2; k += 1)
    {
        check_lower_complex_value(writer, b, k, element, computing);
        check_lower_complex_value(writer, b, k, element, computing);
        check_lower_emit(writer, BUSTER_TYPED_MUL, computing, 0);
    }
    check_lower_emit(writer, BUSTER_TYPED_ADD, computing, 0);
    check_lower_complex_wrap(writer, element, computing);
    check_lower_complex_divide(writer, computing);
}

// `op` (ADD, SUBTRACT, MULTIPLY, DIVIDE, NEGATIVE, BIT_NOT, the conjugate, or EQUAL and NOT_EQUAL) of `a` and `b` (`a`
// alone for a unary one) in complex type `type`: a result in a frame object of its own, which it returns, or for a
// comparison an I32 onto the stack, and BUSTER_CHECK_NONE. A complex integer type's (#137) computes in its element's
// class, or in 32 bits for one of 8 or 16, and is stored back in its own, a _BitInt's extended from its width: its
// product of two complex values is (ac - bd) + (ad + bc)i and its quotient check_lower_complex_quotient's, as clang's
// code has them, where a floating one's are libgcc's. A 128-bit element's (__int128's, and a _BitInt's of 65 to 128
// bits) computes in Tier 0's 128-bit classes, its quotient's divisions libgcc's too.
static uint32_t check_lower_complex_compute(CheckLowerWriter* writer, CheckOp op, TypeId type, CheckLowerComplexOperand a, CheckLowerComplexOperand b)
{
    const TypeTable* types = writer->program->types;
    const TypeId element = check_lower_complex_element(types, type);
    const uint8_t class = check_lower_class(writer, element);
    const uint8_t computing = check_lower_computed(class);
    const uint64_t size = type_size(types, element);
    const uint8_t element_kind = type_kind(types, element);
    const bool integer = element_kind == BUSTER_TYPE_KIND_INTEGER || element_kind == BUSTER_TYPE_KIND_BIT_INT;
    const bool sum = op == CHECK_OP_ADD || op == CHECK_OP_SUBTRACT;
    const bool library = !integer && ((op == CHECK_OP_MULTIPLY && a.complex && b.complex) || (op == CHECK_OP_DIVIDE && b.complex));
    const bool product = integer && op == CHECK_OP_MULTIPLY && a.complex && b.complex;
    const bool quotient = integer && op == CHECK_OP_DIVIDE;
    uint32_t result = BUSTER_CHECK_NONE;
    if (element == BUSTER_TYPE_FLOAT16 || element == BUSTER_TYPE_BF16)
    {
        // Copies, conversions and bit casts need only the two binary16 or bfloat16 representations. Arithmetic needs its
        // own excess-precision/native rules and complex library calls; never compute with the representation's bits.
        check_lower_unsupported(writer, element == BUSTER_TYPE_FLOAT16 ? "arithmetic and comparisons of _Complex _Float16 values" :
                                                                         "arithmetic and comparisons of _Complex __bf16 values");
    }
    else if (op == CHECK_OP_EQUAL || op == CHECK_OP_NOT_EQUAL)
    {
        // Both parts compared, as clang compares them: equal when both are, unequal when either is (an unordered part is).
        for (uint32_t part = 0; part < 2; part += 1)
        {
            check_lower_complex_value(writer, a, part, element, computing);
            check_lower_complex_value(writer, b, part, element, computing);
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
            check_lower_complex_part(writer, i < 2 ? a : b, i % 2, element);
        }
        check_lower_emit(writer, BUSTER_TYPED_CALL, BUSTER_TYPED_CLASS_VOID, call);
    }
    for (uint32_t part = 0; result != BUSTER_CHECK_NONE && !library && part < 2; part += 1)
    {
        check_lower_complex_place(writer, result, part, size);
        if (quotient)
        {
            check_lower_complex_quotient(writer, a, b, part, element, computing);
        }
        else if (product)
        {
            // ac - bd for the real part, ad + bc for the imaginary one.
            check_lower_complex_value(writer, a, 0, element, computing);
            check_lower_complex_value(writer, b, part, element, computing);
            check_lower_emit(writer, BUSTER_TYPED_MUL, computing, 0);
            check_lower_complex_value(writer, a, 1, element, computing);
            check_lower_complex_value(writer, b, 1 - part, element, computing);
            check_lower_emit(writer, BUSTER_TYPED_MUL, computing, 0);
            check_lower_emit(writer, part == 0 ? BUSTER_TYPED_SUB : BUSTER_TYPED_ADD, computing, 0);
        }
        else if (op == CHECK_OP_NEGATIVE || (op == CHECK_OP_BIT_NOT && part == 1))
        {
            check_lower_complex_value(writer, a, part, element, computing);
            check_lower_emit(writer, BUSTER_TYPED_NEG, computing, 0);
        }
        else if (op == CHECK_OP_BIT_NOT)
        {
            check_lower_complex_value(writer, a, part, element, computing);
        }
        else if (sum && part == 1 && !a.complex)
        {
            // A real value plus or minus a complex one: the complex one's imaginary part, negated for a difference.
            check_lower_complex_value(writer, b, 1, element, computing);
            if (op == CHECK_OP_SUBTRACT)
            {
                check_lower_emit(writer, BUSTER_TYPED_NEG, computing, 0);
            }
        }
        else if (sum && part == 1 && !b.complex)
        {
            check_lower_complex_value(writer, a, 1, element, computing);
        }
        else
        {
            // The parts' sum or difference, or a product with a real value or a quotient by one, which each part takes.
            check_lower_complex_value(writer, a, a.complex ? part : 0, element, computing);
            check_lower_complex_value(writer, b, b.complex ? part : 0, element, computing);
            check_lower_emit(writer, check_lower_binary(op), computing, 0);
        }
        check_lower_bit_int_extend(writer, element, true, false);
        check_lower_emit(writer, BUSTER_TYPED_SET, class, 0);
    }
    return result;
}

// The truth of the complex value of `type` whose address is on top of the stack, as an I32: 1 when either part isn't
// zero (an unordered one isn't), as clang tests one, or with `negate` its negation. A part of 8 or 16 bits compares in
// 32 (check_lower_computed).
static void check_lower_complex_truth(CheckLowerWriter* writer, TypeId type, bool negate)
{
    const TypeTable* types = writer->program->types;
    const TypeId element = check_lower_complex_element(types, type);
    const uint8_t computing = check_lower_half_of(writer, element) != CHECK_HALF_NONE ? BUSTER_TYPED_CLASS_F32 : check_lower_computed(check_lower_class(writer, element));
    const CheckLowerComplexOperand value = { .local = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64), .complex = true };
    check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, value.local);
    for (uint32_t part = 0; part < 2; part += 1)
    {
        check_lower_complex_value(writer, value, part, element, computing);
        check_lower_emit(writer, BUSTER_TYPED_CONST, computing, 0);
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
// not void, neither complex of aggregate parts: between complex types, each part converted into a frame object of the target's
// (nothing between ones of the same element); from one, its real part converted, or for _Bool its truth; to one, the
// value converted as its real part, zero the imaginary one.
static void check_lower_complex_class_conversion(CheckLowerWriter* writer, TypeId from, TypeId to)
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
            check_lower_complex_part(writer, source, part, from_element);
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
        check_lower_bit_int_extend(writer, from_element, false, false);
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

// A conversion of the value on top of the stack from `from` to `to`, one of them complex, and `to` not void: a complex
// type's of aggregate parts on either side check_lower_complex_parts_conversion's, any other's
// check_lower_complex_class_conversion's.
static void check_lower_complex_conversion(CheckLowerWriter* writer, TypeId from, TypeId to)
{
    if (check_lower_complex_parts(writer, from) || check_lower_complex_parts(writer, to))
    {
        check_lower_complex_parts_conversion(writer, from, to);
    }
    else
    {
        check_lower_complex_class_conversion(writer, from, to);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// __float128 values
//
// A __float128 value (#137) is a frame object of its 16 bytes, aligned to 16, which the stream carries by its address, as
// an aggregate's (check_lower_type_facts), so that copies, calls, returns and va_arg take it as the convention classifies
// it: SSE, SSEUP, a vector register's 16 bytes, which Tier 0 moves with movups. Tier 0 has no entry of its arithmetic, and
// as clang -O0's code a sum, a difference, a product and a quotient are libgcc's __addtf3 ... __divtf3, a comparison its
// __lttf2 ... __netf2's int against 0, a value's truth __netf2's against +0.0, and a conversion __extendsftf2 ...
// __fixunstfti; a negation flips a copy's sign bit, as clang's xorps does. The checker folds no __float128 constant
// (check_action_float_supported), so every __float128 value is computed at run time.

// A frame object of a __float128's 16 bytes.
static uint32_t check_lower_quad_object(CheckLowerWriter* writer)
{
    return check_lower_object(writer, 16, 4);
}

// A call of libgcc's `runtime` of `count` arguments of the types `arguments`, whose values are on top of the stack, the
// last on top (a __float128's, or a 128-bit integer's, its object's address), and of result type `result`: a __float128
// or a 128-bit integer into a frame object of its own, whose address, or value, it leaves; any other's value onto the
// stack.
static void check_lower_quad_call(CheckLowerWriter* writer, CheckLowerRuntime runtime, TypeId result, const TypeId* arguments, uint32_t count)
{
    const bool quad = check_lower_quad(writer, result);
    const bool wide = check_lower_wide(writer, result);
    const uint8_t class = quad ? BUSTER_TYPED_CLASS_U64 : check_lower_class(writer, result);
    const uint32_t object = quad ? check_lower_quad_object(writer) : wide ? check_lower_temporary(writer, class) : BUSTER_CHECK_NONE;
    const uint32_t call = writer->output->status == CHECK_PROGRAM_OK ? check_lower_runtime_call(writer, runtime, result, arguments, count, object) : 0;
    check_lower_emit(writer, BUSTER_TYPED_CALL, object != BUSTER_CHECK_NONE ? BUSTER_TYPED_CLASS_VOID : class, call);
    if (quad)
    {
        check_lower_emit(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, object);
    }
    else if (wide)
    {
        check_lower_emit(writer, BUSTER_TYPED_LOCAL, class, object);
    }
}

// A folded __float128 constant: a frame object of its 16 bytes, whose address it leaves, the value converted from the
// checker's long double exactly. The checker folds none of __float128's arithmetic (check_action_float_supported), but
// gives a real value's __imag__ as a constant zero of its type (c_check_complex.c), which comes here.
static void check_lower_quad_constant(CheckLowerWriter* writer, const CheckValue* value)
{
    const uint32_t result = check_lower_quad_object(writer);
    const __float128 quad = (__float128)value->floating;
    uint64_t halves[2];
    memcpy(halves, &quad, sizeof(halves));
    for (uint32_t half = 0; half < 2; half += 1)
    {
        check_lower_complex_place(writer, result, half, 8);
        check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, halves[half]);
        check_lower_emit(writer, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_U64, 0);
    }
    check_lower_emit(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, result);
}

// A frame object of zeros of `type`, an aggregate's (a __float128's, a _BitInt's past 128 bits), its zeros written now:
// a real value's imaginary part, whose address is its value.
static uint32_t check_lower_aggregate_zero(CheckLowerWriter* writer, TypeId type)
{
    const TypeTable* types = writer->program->types;
    const uint64_t size = type_size(types, type);
    const uint32_t zero = check_lower_object(writer, size, type_alignment_log2(types, type));
    for (uint64_t offset = 0; offset < size && writer->output->status == CHECK_PROGRAM_OK; offset += 8)
    {
        check_lower_complex_place(writer, zero, 0, 0);
        check_lower_offset(writer, offset);
        check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 0);
        check_lower_emit(writer, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_U64, 0);
    }
    return zero;
}

// `op` (ADD, SUBTRACT, MULTIPLY, DIVIDE or NEGATIVE, or LESS to NOT_EQUAL) of the __float128 values whose addresses
// temporaries `a` and `b` hold (`a` alone for NEGATIVE): an arithmetic result into a frame object of its own, whose
// address it leaves, or a comparison's I32, libgcc's int against 0 as clang tests it (unordered operands make each false
// but !=).
static void check_lower_quad_compute(CheckLowerWriter* writer, CheckOp op, uint32_t a, uint32_t b)
{
    const TypeId arguments[2] = { BUSTER_TYPE_FLOAT128, BUSTER_TYPE_FLOAT128 };
    if (op == CHECK_OP_NEGATIVE)
    {
        // A copy, its high eightbyte's sign bit flipped.
        const uint32_t result = check_lower_quad_object(writer);
        check_lower_emit(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, result);
        check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, a);
        check_lower_emit(writer, BUSTER_TYPED_COPY, BUSTER_TYPED_CLASS_VOID, 16);
        check_lower_complex_place(writer, result, 1, 8);
        check_lower_complex_place(writer, result, 1, 8);
        check_lower_emit(writer, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_U64, 0);
        check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, UINT64_C(0x8000000000000000));
        check_lower_emit(writer, BUSTER_TYPED_XOR, BUSTER_TYPED_CLASS_U64, 0);
        check_lower_emit(writer, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_U64, 0);
        check_lower_emit(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, result);
    }
    else
    {
        const bool comparison = op >= CHECK_OP_LESS && op <= CHECK_OP_NOT_EQUAL;
        const CheckLowerRuntime runtime = comparison ? (CheckLowerRuntime)(CHECK_LOWER_RUNTIME_LTTF2 + (op - CHECK_OP_LESS)) :
                                          op == CHECK_OP_ADD ? CHECK_LOWER_RUNTIME_ADDTF3 : op == CHECK_OP_SUBTRACT ? CHECK_LOWER_RUNTIME_SUBTF3 :
                                          op == CHECK_OP_MULTIPLY ? CHECK_LOWER_RUNTIME_MULTF3 : CHECK_LOWER_RUNTIME_DIVTF3;
        check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, a);
        check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, b);
        check_lower_quad_call(writer, runtime, comparison ? BUSTER_TYPE_INT : BUSTER_TYPE_FLOAT128, arguments, 2);
        if (comparison)
        {
            check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 0);
            check_lower_emit(writer, check_lower_binary(op), BUSTER_TYPED_CLASS_I32, 0);
        }
    }
}

// The truth of the __float128 value whose address is on top of the stack, as an I32: __netf2's against +0.0 not 0, as
// clang tests one (a NaN is true), or with `negate` its negation.
static void check_lower_quad_truth(CheckLowerWriter* writer, bool negate)
{
    const TypeId arguments[2] = { BUSTER_TYPE_FLOAT128, BUSTER_TYPE_FLOAT128 };
    const uint32_t value = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
    const uint32_t zero = check_lower_quad_object(writer);
    check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, value);
    for (uint32_t part = 0; part < 2; part += 1)
    {
        check_lower_complex_place(writer, zero, part, 8);
        check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 0);
        check_lower_emit(writer, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_U64, 0);
    }
    check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, value);
    check_lower_emit(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, zero);
    check_lower_quad_call(writer, CHECK_LOWER_RUNTIME_NETF2, BUSTER_TYPE_INT, arguments, 2);
    check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 0);
    check_lower_emit(writer, negate ? BUSTER_TYPED_EQ : BUSTER_TYPED_NE, BUSTER_TYPED_CLASS_I32, 0);
}

// A conversion between integer types, but to _Bool, of the value on top of the stack, as check_lower_scalar_conversion
// makes one: a CONVERT where their classes differ, and a _BitInt narrower than its class extended from its width
// (check_lower_bit_int_extend). check_lower_quad_conversion's to and from libgcc's integers are made here, so that it
// doesn't call back into the conversion that calls it.
static void check_lower_integer_conversion(CheckLowerWriter* writer, TypeId from, TypeId to)
{
    const uint8_t source = check_lower_class(writer, from);
    const uint8_t target = check_lower_class(writer, to);
    if (writer->bit_ints && check_lower_bit_int_shift(writer->program->types, to) != 0)
    {
        if (source != check_lower_computed(target))
        {
            check_lower_emit(writer, BUSTER_TYPED_CONVERT, check_lower_computed(target), 0);
        }
        check_lower_bit_int_extend(writer, to, true, false);
    }
    else if (source != target)
    {
        check_lower_emit(writer, BUSTER_TYPED_CONVERT, target, 0);
    }
}

// A conversion of the value on top of the stack between `from` and `to`, one of them __float128 and the other a scalar
// type (check_lower_scalar_conversion's mixed one), as clang -O0 makes it: libgcc's of a float, a double or a long double
// by its own; of an integer through int, unsigned int, long, unsigned long, __int128 or unsigned __int128, the first
// that holds its values, a signed one where one does, as LLVM promotes a narrower unsigned type's conversion to a signed
// one's (a 128-bit integer's value passing and coming back in a frame object); and to _Bool, a value's truth.
static __attribute__((noinline)) void check_lower_quad_conversion(CheckLowerWriter* writer, TypeId from, TypeId to)
{
    const TypeTable* types = writer->program->types;
    const bool from_quad = check_lower_quad(writer, from);
    const TypeId other = from_quad ? to : from;
    const uint8_t class = check_lower_class(writer, other);
    uint32_t bits = 0;
    bool sign = false;
    const bool integer = check_action_integer_type(types, type_conversion_operand(types, other), &bits, &sign);
    if (from_quad && (check_lower_facts(writer, to) & BUSTER_CHECK_LOWER_TYPE_BOOL) != 0)
    {
        check_lower_quad_truth(writer, false);
        check_lower_emit(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U8, 0);
    }
    else if (class == BUSTER_TYPED_CLASS_F32 || class == BUSTER_TYPED_CLASS_F64 || class == BUSTER_TYPED_CLASS_F80 || integer)
    {
        const uint32_t index = class == BUSTER_TYPED_CLASS_F32 ? 0 : class == BUSTER_TYPED_CLASS_F64 ? 1 : 2;
        // An integer's: int, unsigned, long, unsigned long, __int128 and unsigned __int128, and their runtime functions,
        // __floatsitf ... __floatuntitf and __fixtfsi ... __fixunstfti, in that order.
        const uint32_t step = bits <= 31 || (bits == 32 && sign) ? 0 : bits == 32 ? 1 : bits <= 63 || (bits == 64 && sign) ? 2 : bits == 64 ? 3 :
                              bits <= 127 || sign ? 4 : 5;
        const TypeId steps[6] = { BUSTER_TYPE_INT, BUSTER_TYPE_UINT, BUSTER_TYPE_LONG, BUSTER_TYPE_ULONG, BUSTER_TYPE_INT128, BUSTER_TYPE_UINT128 };
        const TypeId through = integer ? steps[step] : class == BUSTER_TYPED_CLASS_F32 ? BUSTER_TYPE_FLOAT : class == BUSTER_TYPED_CLASS_F64 ? BUSTER_TYPE_DOUBLE :
                                                                                                                                       BUSTER_TYPE_LONG_DOUBLE;
        const CheckLowerRuntime runtime = (CheckLowerRuntime)(integer ? (from_quad ? CHECK_LOWER_RUNTIME_FIXTFSI : CHECK_LOWER_RUNTIME_FLOATSITF) + step :
                                                                        (from_quad ? CHECK_LOWER_RUNTIME_TRUNCTFSF2 : CHECK_LOWER_RUNTIME_EXTENDSFTF2) + index);
        const TypeId argument = from_quad ? BUSTER_TYPE_FLOAT128 : through;
        if (!from_quad)
        {
            // A floating value's own type is its runtime function's (`through` of its class).
            if (integer)
            {
                check_lower_integer_conversion(writer, other, through);
            }
            if (check_lower_wide(writer, through))
            {
                // A 128-bit integer's value passes from a frame object of its own.
                const uint32_t local = check_lower_temporary(writer, check_lower_class(writer, through));
                check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, check_lower_class(writer, through), local);
                check_lower_emit(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, local);
            }
        }
        check_lower_quad_call(writer, runtime, from_quad ? through : BUSTER_TYPE_FLOAT128, &argument, 1);
        if (from_quad && integer)
        {
            check_lower_integer_conversion(writer, through, other);
        }
    }
    else
    {
        check_lower_unsupported(writer, "conversions between __float128 and this type");
    }
}

// `destination op= value` (check_lower_complex_update) where the calculation's type is __float128, the value's address and
// the destination's in temporaries `locals` (the value's in the low 32 bits): the destination read in the calculation's
// type (a real one's value converted, a _BitInt's past 128 bits from its address), the result computed
// (check_lower_quad_compute) and stored back: copied into a __float128 destination, whose address is the expression's
// value, and so into a _BitInt one past 128 bits once converted into a frame object of its own; or converted to a real
// one's type and stored, which STORE leaves.
static void check_lower_quad_update_emit(CheckLowerWriter* writer, uint32_t node, uint64_t locals)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const CheckOp op = graph->ops[node];
    const CheckOp computed = op == CHECK_OP_ADD_ASSIGN ? CHECK_OP_ADD : op == CHECK_OP_SUBTRACT_ASSIGN ? CHECK_OP_SUBTRACT :
                             op == CHECK_OP_MULTIPLY_ASSIGN ? CHECK_OP_MULTIPLY : CHECK_OP_DIVIDE;
    const TypeId stored = stream->values[graph->operand0[node]].type;
    const TypeId calculated = stream->conversions[node].operands[0].type;
    const bool stored_quad = check_lower_quad(writer, stored);
    const CheckLowerLimbs stored_limbs = check_lower_limbs(writer, stored);
    const uint32_t value = (uint32_t)locals;
    const uint32_t destination = (uint32_t)(locals >> 32);
    const uint32_t current = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
    const uint32_t result = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
    check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, destination);
    if (stored_limbs.count != 0)
    {
        check_lower_limb_conversion(writer, stored, calculated);
    }
    else if (!stored_quad)
    {
        check_lower_emit(writer, BUSTER_TYPED_LOAD, check_lower_class(writer, stored), 0);
        check_lower_bit_int_extend(writer, stored, false, false);
        check_lower_quad_conversion(writer, stored, calculated);
    }
    check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, current);
    check_lower_quad_compute(writer, computed, current, value);
    check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, result);
    check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, destination);
    check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, result);
    if (stored_quad)
    {
        check_lower_emit(writer, BUSTER_TYPED_COPY, BUSTER_TYPED_CLASS_VOID, 16);
        check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, destination);
    }
    else if (stored_limbs.count != 0)
    {
        check_lower_limb_conversion(writer, calculated, stored);
        check_lower_copy(writer, 8 * (uint64_t)stored_limbs.count, false, false);
        check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, destination);
    }
    else
    {
        check_lower_quad_conversion(writer, calculated, stored);
        check_lower_emit(writer, BUSTER_TYPED_STORE, check_lower_class(writer, stored), 0);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// _BitInt values past 128 bits
//
// A _BitInt(N) past 128 bits (#137) has no class: its value is a frame object of its 64-bit limbs, the least significant
// first, ceil(N / 64) of them aligned to 8 as clang lays one out, which the stream carries by its address, as an
// aggregate's (check_lower_type_facts). Copies, calls, returns and va_arg take it so, and the convention passes and
// returns it in memory. A result keeps its bits past N extended from N, as clang's stores leave them; an operation that
// reads them (a compare, a right shift, a conversion, a value's truth) extends its operand's top limb again as it reads
// it (check_lower_limb_load), as clang's loads read N bits alone, since another compiler's value or a byte copy may leave
// them otherwise. Its operations go a limb at a time: sums and differences with their carries, the bitwise ones, compares
// from the least significant limb up, and shifts through a buffer of twice its limbs (check_lower_limb_shift). As GCC
// -O0's code does (clang's expands them), a product, a quotient and a remainder are libgcc's __mulbitint3 and
// __divmodbitint4, and its conversions to and from float, double, long double and __float128 libgcc's __floatbitint*
// and __fix*bitint, whose precision arguments are -N for a signed _BitInt and N for an unsigned one.

// A frame object of the limbs of a _BitInt past 128 bits.
static uint32_t check_lower_limb_object(CheckLowerWriter* writer, CheckLowerLimbs limbs)
{
    return check_lower_object(writer, 8 * (uint64_t)limbs.count, 3);
}

// The address of limb `limb` of a _BitInt past 128 bits onto the stack: of frame object `local` (`object`), or of the
// one whose address temporary `local` holds.
static void check_lower_limb_address(CheckLowerWriter* writer, uint32_t local, bool object, uint32_t limb)
{
    check_lower_emit(writer, object ? BUSTER_TYPED_LOCAL_ADDRESS : BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, local);
    check_lower_offset(writer, 8 * (uint64_t)limb);
}

// The top limb of a _BitInt past 128 bits `limbs`, a U64 on top of the stack, extended from the width through the bits
// past it (its sign's, or zeros), as `class` (U64, or I64 for a signed compare).
static void check_lower_limb_extend(CheckLowerWriter* writer, CheckLowerLimbs limbs, uint8_t class)
{
    const uint32_t shift = 64 * limbs.count - limbs.width;
    uint8_t current = BUSTER_TYPED_CLASS_U64;
    if (shift != 0)
    {
        if (limbs.sign)
        {
            check_lower_emit(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_I64, 0);
            current = BUSTER_TYPED_CLASS_I64;
        }
        check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U32, shift);
        check_lower_emit(writer, BUSTER_TYPED_SHL, current, 0);
        check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U32, shift);
        check_lower_emit(writer, BUSTER_TYPED_SHR, current, 0);
    }
    if (current != class)
    {
        check_lower_emit(writer, BUSTER_TYPED_CONVERT, class, 0);
    }
}

// Limb `limb` of the _BitInt past 128 bits `limbs` whose address temporary `local` holds onto the stack, as `class`: its
// top limb extended from the width again where `canonical` (check_lower_limb_extend).
static void check_lower_limb_load(CheckLowerWriter* writer, CheckLowerLimbs limbs, uint32_t local, uint32_t limb, uint8_t class, bool canonical)
{
    const bool extend = canonical && limb + 1 == limbs.count;
    check_lower_limb_address(writer, local, false, limb);
    check_lower_emit(writer, BUSTER_TYPED_LOAD, extend ? BUSTER_TYPED_CLASS_U64 : class, 0);
    if (extend)
    {
        check_lower_limb_extend(writer, limbs, class);
    }
}

// Frame object `result`'s top limb extended from the width again, where a call (libgcc's) left its bits past it.
static void check_lower_limb_settle(CheckLowerWriter* writer, CheckLowerLimbs limbs, uint32_t result)
{
    if (64 * limbs.count != limbs.width)
    {
        check_lower_limb_address(writer, result, true, limbs.count - 1);
        check_lower_limb_address(writer, result, true, limbs.count - 1);
        check_lower_emit(writer, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_U64, 0);
        check_lower_limb_extend(writer, limbs, BUSTER_TYPED_CLASS_U64);
        check_lower_emit(writer, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_U64, 0);
    }
}

// The limbs of `limbs` past the first `from`, each the U64 in temporary `fill` (the sign's or zeros past a narrower
// value), into frame object `result`, the top one extended from the width.
static void check_lower_limb_fill(CheckLowerWriter* writer, CheckLowerLimbs limbs, uint32_t result, uint32_t from, uint32_t fill)
{
    for (uint32_t limb = from; limb < limbs.count && writer->output->status == CHECK_PROGRAM_OK; limb += 1)
    {
        check_lower_limb_address(writer, result, true, limb);
        check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, fill);
        if (limb + 1 == limbs.count)
        {
            check_lower_limb_extend(writer, limbs, BUSTER_TYPED_CLASS_U64);
        }
        check_lower_emit(writer, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_U64, 0);
    }
}

// The limb past the most significant one of the _BitInt past 128 bits `limbs` whose address temporary `local` holds, a
// U64 onto the stack: its sign's, all ones or zeros, for a signed one where `extend`, and zeros otherwise.
static void check_lower_limb_sign(CheckLowerWriter* writer, CheckLowerLimbs limbs, uint32_t local, bool extend)
{
    if (extend && limbs.sign)
    {
        check_lower_limb_load(writer, limbs, local, limbs.count - 1, BUSTER_TYPED_CLASS_I64, true);
        check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U32, 63);
        check_lower_emit(writer, BUSTER_TYPED_SHR, BUSTER_TYPED_CLASS_I64, 0);
        check_lower_emit(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
    }
    else
    {
        check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 0);
    }
}

// A sum, a difference, a negation (0 less the operand), a complement or a bitwise operation of the _BitInt past 128 bits
// `limbs`, whose operands' addresses temporaries `a` and `b` hold (`a` alone for NEGATIVE and BIT_NOT), a limb at a time
// from the least significant, into a frame object of its own, whose address it leaves. A sum's carry and a difference's
// borrow go to the next limb in an I32 temporary, 0 or 1: with c the one in, a limb r = x + y + c carries where r < x,
// or r == x and c; a limb r = x - y - c borrows where r > x, or r == x and c. Neither reads the bits past the width,
// which only reach the result's, extended again.
static void check_lower_limb_arithmetic(CheckLowerWriter* writer, CheckOp op, CheckLowerLimbs limbs, uint32_t a, uint32_t b)
{
    const uint32_t result = check_lower_limb_object(writer, limbs);
    const bool add = op == CHECK_OP_ADD;
    const bool carried = add || op == CHECK_OP_SUBTRACT || op == CHECK_OP_NEGATIVE;
    const uint8_t kind = op == CHECK_OP_BIT_AND ? BUSTER_TYPED_AND : op == CHECK_OP_BIT_OR ? BUSTER_TYPED_OR : op == CHECK_OP_BIT_XOR ? BUSTER_TYPED_XOR :
                         add ? BUSTER_TYPED_ADD : BUSTER_TYPED_SUB;
    const uint32_t carry = carried ? check_lower_temporary(writer, BUSTER_TYPED_CLASS_I32) : BUSTER_CHECK_NONE;
    const uint32_t x = carried ? check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64) : BUSTER_CHECK_NONE;
    const uint32_t r = carried ? check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64) : BUSTER_CHECK_NONE;
    for (uint32_t limb = 0; limb < limbs.count && writer->output->status == CHECK_PROGRAM_OK; limb += 1)
    {
        const bool top = limb + 1 == limbs.count;
        check_lower_limb_address(writer, result, true, limb);
        if (op == CHECK_OP_NEGATIVE)
        {
            check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 0);
        }
        else
        {
            check_lower_limb_load(writer, limbs, a, limb, BUSTER_TYPED_CLASS_U64, false);
        }
        if (carried && !top)
        {
            check_lower_emit(writer, BUSTER_TYPED_STORE_LOCAL, BUSTER_TYPED_CLASS_U64, x);
        }
        if (op == CHECK_OP_BIT_NOT)
        {
            check_lower_emit(writer, BUSTER_TYPED_NOT, BUSTER_TYPED_CLASS_U64, 0);
        }
        else
        {
            check_lower_limb_load(writer, limbs, op == CHECK_OP_NEGATIVE ? a : b, limb, BUSTER_TYPED_CLASS_U64, false);
            check_lower_emit(writer, kind, BUSTER_TYPED_CLASS_U64, 0);
        }
        if (carried && limb != 0)
        {
            check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_I32, carry);
            check_lower_emit(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
            check_lower_emit(writer, add ? BUSTER_TYPED_ADD : BUSTER_TYPED_SUB, BUSTER_TYPED_CLASS_U64, 0);
        }
        if (carried && !top)
        {
            check_lower_emit(writer, BUSTER_TYPED_STORE_LOCAL, BUSTER_TYPED_CLASS_U64, r);
        }
        if (top)
        {
            check_lower_limb_extend(writer, limbs, BUSTER_TYPED_CLASS_U64);
        }
        check_lower_emit(writer, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_U64, 0);
        if (carried && !top)
        {
            check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, r);
            check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, x);
            check_lower_emit(writer, add ? BUSTER_TYPED_LT : BUSTER_TYPED_GT, BUSTER_TYPED_CLASS_I32, 0);
            if (limb != 0)
            {
                check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, r);
                check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, x);
                check_lower_emit(writer, BUSTER_TYPED_EQ, BUSTER_TYPED_CLASS_I32, 0);
                check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_I32, carry);
                check_lower_emit(writer, BUSTER_TYPED_AND, BUSTER_TYPED_CLASS_I32, 0);
                check_lower_emit(writer, BUSTER_TYPED_OR, BUSTER_TYPED_CLASS_I32, 0);
            }
            check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_I32, carry);
        }
    }
    check_lower_emit(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, result);
}

// A compare of the _BitInts past 128 bits `limbs` whose addresses temporaries `a` and `b` hold, as an I32: an equality
// the limbs' XORs ORed together against 0; an order from the least significant limb up, each limb's deciding where it
// isn't equal, r = (x < y) | ((x == y) & r), the top limbs compared signed for a signed _BitInt; a greater one's with
// the operands swapped, and <= and >= the negations of > and <.
static void check_lower_limb_compare(CheckLowerWriter* writer, CheckOp op, CheckLowerLimbs limbs, uint32_t a, uint32_t b)
{
    if (op == CHECK_OP_EQUAL || op == CHECK_OP_NOT_EQUAL)
    {
        for (uint32_t limb = 0; limb < limbs.count && writer->output->status == CHECK_PROGRAM_OK; limb += 1)
        {
            check_lower_limb_load(writer, limbs, a, limb, BUSTER_TYPED_CLASS_U64, true);
            check_lower_limb_load(writer, limbs, b, limb, BUSTER_TYPED_CLASS_U64, true);
            check_lower_emit(writer, BUSTER_TYPED_XOR, BUSTER_TYPED_CLASS_U64, 0);
            if (limb != 0)
            {
                check_lower_emit(writer, BUSTER_TYPED_OR, BUSTER_TYPED_CLASS_U64, 0);
            }
        }
        check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 0);
        check_lower_emit(writer, op == CHECK_OP_EQUAL ? BUSTER_TYPED_EQ : BUSTER_TYPED_NE, BUSTER_TYPED_CLASS_I32, 0);
    }
    else
    {
        const bool swap = op == CHECK_OP_GREATER || op == CHECK_OP_LESS_EQUAL;
        const uint32_t x = swap ? b : a;
        const uint32_t y = swap ? a : b;
        for (uint32_t limb = 0; limb < limbs.count && writer->output->status == CHECK_PROGRAM_OK; limb += 1)
        {
            const uint8_t class = limbs.sign && limb + 1 == limbs.count ? BUSTER_TYPED_CLASS_I64 : BUSTER_TYPED_CLASS_U64;
            if (limb != 0)
            {
                check_lower_limb_load(writer, limbs, x, limb, class, true);
                check_lower_limb_load(writer, limbs, y, limb, class, true);
                check_lower_emit(writer, BUSTER_TYPED_EQ, BUSTER_TYPED_CLASS_I32, 0);
                check_lower_emit(writer, BUSTER_TYPED_AND, BUSTER_TYPED_CLASS_I32, 0);
            }
            check_lower_limb_load(writer, limbs, x, limb, class, true);
            check_lower_limb_load(writer, limbs, y, limb, class, true);
            check_lower_emit(writer, BUSTER_TYPED_LT, BUSTER_TYPED_CLASS_I32, 0);
            if (limb != 0)
            {
                check_lower_emit(writer, BUSTER_TYPED_OR, BUSTER_TYPED_CLASS_I32, 0);
            }
        }
        if (op == CHECK_OP_LESS_EQUAL || op == CHECK_OP_GREATER_EQUAL)
        {
            check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 0);
            check_lower_emit(writer, BUSTER_TYPED_EQ, BUSTER_TYPED_CLASS_I32, 0);
        }
    }
}

// The truth of the _BitInt past 128 bits `type` whose address is on top of the stack, as an I32: its limbs ORed together
// against 0, or with `negate` its negation.
static void check_lower_limb_truth(CheckLowerWriter* writer, TypeId type, bool negate)
{
    const CheckLowerLimbs limbs = check_lower_limbs(writer, type);
    const uint32_t value = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
    check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, value);
    for (uint32_t limb = 0; limb < limbs.count && writer->output->status == CHECK_PROGRAM_OK; limb += 1)
    {
        check_lower_limb_load(writer, limbs, value, limb, BUSTER_TYPED_CLASS_U64, true);
        if (limb != 0)
        {
            check_lower_emit(writer, BUSTER_TYPED_OR, BUSTER_TYPED_CLASS_U64, 0);
        }
    }
    check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 0);
    check_lower_emit(writer, negate ? BUSTER_TYPED_EQ : BUSTER_TYPED_NE, BUSTER_TYPED_CLASS_I32, 0);
}

// A precision argument of libgcc's _BitInt functions: -N for a signed _BitInt, N for an unsigned one.
static void check_lower_limb_precision(CheckLowerWriter* writer, CheckLowerLimbs limbs, bool sign)
{
    check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, sign && limbs.sign ? (uint64_t)-(int64_t)limbs.width : limbs.width);
}

// A product, a quotient or a remainder of the _BitInts past 128 bits `limbs` whose addresses temporaries `a` and `b`
// hold, as GCC -O0 makes it: libgcc's __mulbitint3 (result, its precision, a, its precision, b, its precision) or
// __divmodbitint4 (quotient, its precision, remainder, its precision, a, b, as before), a quotient's without a remainder
// (a null pointer of precision 0) and a remainder's without a quotient. The result goes into a frame object of its own,
// whose address it leaves, of precision N (its bits past the width are libgcc's to leave, and extended again after).
static void check_lower_limb_library(CheckLowerWriter* writer, CheckOp op, CheckLowerLimbs limbs, uint32_t a, uint32_t b)
{
    const TypeId arguments[8] = { BUSTER_TYPE_ULONG, BUSTER_TYPE_INT, BUSTER_TYPE_ULONG, BUSTER_TYPE_INT,
                                  BUSTER_TYPE_ULONG, BUSTER_TYPE_INT, BUSTER_TYPE_ULONG, BUSTER_TYPE_INT };
    const bool product = op == CHECK_OP_MULTIPLY;
    const uint32_t result = check_lower_limb_object(writer, limbs);
    const uint32_t call = writer->output->status == CHECK_PROGRAM_OK ?
                              check_lower_runtime_call(writer, product ? CHECK_LOWER_RUNTIME_MULBITINT3 : CHECK_LOWER_RUNTIME_DIVMODBITINT4, BUSTER_TYPE_VOID,
                                                       arguments, product ? 6 : 8, BUSTER_CHECK_NONE) :
                              0;
    for (uint32_t place = 0; !product && place < 2; place += 1)
    {
        // The quotient's place, then the remainder's: the result's where it is the one asked for.
        const bool asked = (place == 0) == (op == CHECK_OP_DIVIDE);
        if (asked)
        {
            check_lower_emit(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, result);
        }
        else
        {
            check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 0);
        }
        check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, asked ? limbs.width : 0);
    }
    if (product)
    {
        check_lower_emit(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, result);
        check_lower_limb_precision(writer, limbs, false);
    }
    check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, a);
    check_lower_limb_precision(writer, limbs, true);
    check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, b);
    check_lower_limb_precision(writer, limbs, true);
    check_lower_emit(writer, BUSTER_TYPED_CALL, BUSTER_TYPED_CLASS_VOID, call);
    check_lower_limb_settle(writer, limbs, result);
    check_lower_emit(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, result);
}

// A shift of the _BitInt past 128 bits `limbs` whose address temporary `a` holds, by the count in temporary `b` of
// `count`'s value class (a _BitInt past 128 bits' address, whose low limb counts), into a frame object of its own whose
// address it leaves. It goes through a buffer B of twice the limbs, n of them: the operand above n zeros for a left
// shift, or under n limbs of its sign (or zeros) for a right one, its top limb extended from the width, so that each
// limb i of the result takes two neighbouring limbs of B q limbs from its own place, shifted by r bits, the count's
// q = count / 64 and r = count % 64: a left shift's (B[n + i - q] << r) | ((B[n + i - q - 1] >> 1) >> (63 - r)), a right
// one's (B[i + q] >> r) | ((B[i + q + 1] << 1) << (63 - r)), each second half 0 where r is 0. A count past the width is
// undefined; q is kept to n - 1 at most, so that B's limbs are all it reads.
static void check_lower_limb_shift(CheckLowerWriter* writer, bool left, CheckLowerLimbs limbs, uint32_t a, uint32_t b, TypeId count)
{
    const uint32_t n = limbs.count;
    const CheckLowerLimbs count_limbs = check_lower_limbs(writer, count);
    const uint8_t count_class = check_lower_value_class(writer, count);
    const uint32_t buffer = check_lower_object(writer, 16 * (uint64_t)n, 3);
    const uint32_t result = check_lower_limb_object(writer, limbs);
    const uint32_t q = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
    const uint32_t r = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U32);
    const uint32_t rest = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U32);
    const uint32_t base = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
    // The count as a U64, then r and 63 - r, and q at most n - 1: q - ((q - (n - 1)) & -(q > n - 1)).
    check_lower_emit(writer, BUSTER_TYPED_LOCAL, count_class, b);
    if (count_limbs.count != 0)
    {
        check_lower_emit(writer, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_U64, 0);
    }
    else if (count_class != BUSTER_TYPED_CLASS_U64)
    {
        check_lower_emit(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
    }
    check_lower_emit(writer, BUSTER_TYPED_STORE_LOCAL, BUSTER_TYPED_CLASS_U64, q);
    check_lower_emit(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U32, 0);
    check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U32, 63);
    check_lower_emit(writer, BUSTER_TYPED_AND, BUSTER_TYPED_CLASS_U32, 0);
    check_lower_emit(writer, BUSTER_TYPED_STORE_LOCAL, BUSTER_TYPED_CLASS_U32, r);
    check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U32, 63);
    check_lower_emit(writer, BUSTER_TYPED_XOR, BUSTER_TYPED_CLASS_U32, 0);
    check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U32, rest);
    check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, q);
    check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U32, 6);
    check_lower_emit(writer, BUSTER_TYPED_SHR, BUSTER_TYPED_CLASS_U64, 0);
    check_lower_emit(writer, BUSTER_TYPED_STORE_LOCAL, BUSTER_TYPED_CLASS_U64, q);
    check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, q);
    check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, n - 1);
    check_lower_emit(writer, BUSTER_TYPED_SUB, BUSTER_TYPED_CLASS_U64, 0);
    check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, q);
    check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, n - 1);
    check_lower_emit(writer, BUSTER_TYPED_GT, BUSTER_TYPED_CLASS_I32, 0);
    check_lower_emit(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
    check_lower_emit(writer, BUSTER_TYPED_NEG, BUSTER_TYPED_CLASS_U64, 0);
    check_lower_emit(writer, BUSTER_TYPED_AND, BUSTER_TYPED_CLASS_U64, 0);
    check_lower_emit(writer, BUSTER_TYPED_SUB, BUSTER_TYPED_CLASS_U64, 0);
    check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, q);
    // B: the operand's limbs, in the upper half for a left shift; the other half zeros, or a right shift's sign (in `base`
    // until q's place goes there).
    check_lower_limb_address(writer, buffer, true, left ? n : 0);
    check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, a);
    check_lower_copy(writer, 8 * (uint64_t)n, false, false);
    if (!left)
    {
        check_lower_limb_address(writer, buffer, true, n - 1);
        check_lower_limb_load(writer, limbs, a, n - 1, BUSTER_TYPED_CLASS_U64, true);
        check_lower_emit(writer, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_U64, 0);
    }
    check_lower_limb_sign(writer, limbs, a, !left);
    check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, base);
    for (uint32_t limb = 0; limb < n && writer->output->status == CHECK_PROGRAM_OK; limb += 1)
    {
        check_lower_limb_address(writer, buffer, true, left ? limb : n + limb);
        check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, base);
        check_lower_emit(writer, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_U64, 0);
    }
    // Then q's limbs from B's address (a left shift's from its upper half's last but one), as the base the result's limbs read
    // from: B[n - 1 + i - q] and the limb after it for a left shift, B[i + q] and the limb after it for a right one.
    check_lower_emit(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, buffer);
    check_lower_offset(writer, left ? 8 * (uint64_t)(n - 1) : 0);
    check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, q);
    check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U32, 3);
    check_lower_emit(writer, BUSTER_TYPED_SHL, BUSTER_TYPED_CLASS_U64, 0);
    check_lower_emit(writer, left ? BUSTER_TYPED_SUB : BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0);
    check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, base);
    for (uint32_t limb = 0; limb < n && writer->output->status == CHECK_PROGRAM_OK; limb += 1)
    {
        const uint8_t near = left ? BUSTER_TYPED_SHL : BUSTER_TYPED_SHR;
        const uint8_t far = left ? BUSTER_TYPED_SHR : BUSTER_TYPED_SHL;
        check_lower_limb_address(writer, result, true, limb);
        check_lower_limb_address(writer, base, false, left ? limb + 1 : limb);
        check_lower_emit(writer, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_U64, 0);
        check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U32, r);
        check_lower_emit(writer, near, BUSTER_TYPED_CLASS_U64, 0);
        check_lower_limb_address(writer, base, false, left ? limb : limb + 1);
        check_lower_emit(writer, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_U64, 0);
        check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U32, 1);
        check_lower_emit(writer, far, BUSTER_TYPED_CLASS_U64, 0);
        check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U32, rest);
        check_lower_emit(writer, far, BUSTER_TYPED_CLASS_U64, 0);
        check_lower_emit(writer, BUSTER_TYPED_OR, BUSTER_TYPED_CLASS_U64, 0);
        if (limb + 1 == n)
        {
            check_lower_limb_extend(writer, limbs, BUSTER_TYPED_CLASS_U64);
        }
        check_lower_emit(writer, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_U64, 0);
    }
    check_lower_emit(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, result);
}

// `op` of the _BitInt past 128 bits `type`, its operands' addresses in temporaries `a` and `b` (`a` alone for NEGATIVE
// and BIT_NOT; a shift's count in `b`, of `count`'s value class): a result into a frame object of its own, whose address
// it leaves, or a compare's I32.
static void check_lower_limb_compute(CheckLowerWriter* writer, CheckOp op, TypeId type, uint32_t a, uint32_t b, TypeId count)
{
    const CheckLowerLimbs limbs = check_lower_limbs(writer, type);
    if (op >= CHECK_OP_LESS && op <= CHECK_OP_NOT_EQUAL)
    {
        check_lower_limb_compare(writer, op, limbs, a, b);
    }
    else if (op == CHECK_OP_MULTIPLY || op == CHECK_OP_DIVIDE || op == CHECK_OP_REMAINDER)
    {
        check_lower_limb_library(writer, op, limbs, a, b);
    }
    else if (op == CHECK_OP_SHIFT_LEFT || op == CHECK_OP_SHIFT_RIGHT)
    {
        check_lower_limb_shift(writer, op == CHECK_OP_SHIFT_LEFT, limbs, a, b, count);
    }
    else
    {
        check_lower_limb_arithmetic(writer, op, limbs, a, b);
    }
}

// A conversion of the value on top of the stack between `from` and `to`, one of them a _BitInt past 128 bits (`limbs`
// for its limbs) and the other not complex, with `wider` the limbs of the value's when both are, into a frame object of
// its own whose address it leaves where `to` is one: a value of another _BitInt past 128 bits its limbs copied (its top
// one extended from its width, as more limbs read it), then its sign's or zeros; an integer's (an enum's, _Bool's, a
// pointer's) its value's low limbs, then its sign's or zeros; a floating value libgcc's __fix*bitint of it (its result's
// place, its precision, the value). To another type: an integer its low limb (two of them for 128 bits) converted,
// _Bool the value's truth, a floating type libgcc's __floatbitint* of it (its address, its precision).
static __attribute__((noinline)) void check_lower_limb_conversion(CheckLowerWriter* writer, TypeId from, TypeId to)
{
    const TypeTable* types = writer->program->types;
    const CheckLowerLimbs source = check_lower_limbs(writer, from);
    const CheckLowerLimbs target = check_lower_limbs(writer, to);
    const TypeId other = target.count != 0 ? from : to;
    const uint8_t class = source.count != 0 && target.count != 0 ? BUSTER_TYPED_CLASS_U64 : check_lower_class(writer, other);
    const bool quad = check_lower_quad(writer, other);
    const bool floating = class == BUSTER_TYPED_CLASS_F32 || class == BUSTER_TYPED_CLASS_F64 || class == BUSTER_TYPED_CLASS_F80 || quad;
    const bool boolean = (check_lower_facts(writer, other) & BUSTER_CHECK_LOWER_TYPE_BOOL) != 0;
    const bool integer = (class >= BUSTER_TYPED_CLASS_I8 && class <= BUSTER_TYPED_CLASS_U64) || check_lower_wide_class(class);
    const uint32_t index = class == BUSTER_TYPED_CLASS_F32 ? 0 : class == BUSTER_TYPED_CLASS_F64 ? 1 : class == BUSTER_TYPED_CLASS_F80 ? 2 : 3;
    if (source.count != 0 && target.count != 0)
    {
        const uint32_t value = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
        const uint32_t fill = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
        const uint32_t result = check_lower_limb_object(writer, target);
        const uint32_t kept = source.count < target.count ? source.count : target.count;
        check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, value);
        for (uint32_t limb = 0; limb < kept && writer->output->status == CHECK_PROGRAM_OK; limb += 1)
        {
            check_lower_limb_address(writer, result, true, limb);
            check_lower_limb_load(writer, source, value, limb, BUSTER_TYPED_CLASS_U64, true);
            if (limb + 1 == target.count)
            {
                check_lower_limb_extend(writer, target, BUSTER_TYPED_CLASS_U64);
            }
            check_lower_emit(writer, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_U64, 0);
        }
        if (kept < target.count)
        {
            check_lower_limb_sign(writer, source, value, true);
            check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, fill);
            check_lower_limb_fill(writer, target, result, kept, fill);
        }
        check_lower_emit(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, result);
    }
    else if (target.count != 0 && floating)
    {
        // __fix{sf,df,xf,tf}bitint (place, precision, value): the value into a temporary of its own first (a __float128's
        // address), as it comes before them.
        const TypeId arguments[3] = { BUSTER_TYPE_ULONG, BUSTER_TYPE_INT, quad ? BUSTER_TYPE_FLOAT128 : type_conversion_operand(types, from) };
        const uint8_t value_class = quad ? BUSTER_TYPED_CLASS_U64 : class;
        const uint32_t value = check_lower_temporary(writer, value_class);
        const uint32_t result = check_lower_limb_object(writer, target);
        check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, value_class, value);
        const uint32_t call = writer->output->status == CHECK_PROGRAM_OK ?
                                  check_lower_runtime_call(writer, (CheckLowerRuntime)(CHECK_LOWER_RUNTIME_FIXSFBITINT + index), BUSTER_TYPE_VOID, arguments, 3,
                                                           BUSTER_CHECK_NONE) :
                                  0;
        check_lower_emit(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, result);
        check_lower_limb_precision(writer, target, true);
        check_lower_emit(writer, BUSTER_TYPED_LOCAL, value_class, value);
        check_lower_emit(writer, BUSTER_TYPED_CALL, BUSTER_TYPED_CLASS_VOID, call);
        check_lower_limb_settle(writer, target, result);
        check_lower_emit(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, result);
    }
    else if (target.count != 0 && integer)
    {
        // The value's low limbs (a 128-bit one's two), then its sign's (its top bit's, shifted through) or zeros.
        const bool wide = check_lower_wide_class(class);
        const bool sign = class == BUSTER_TYPED_CLASS_I128 || (class <= BUSTER_TYPED_CLASS_U64 && (class & 1) != 0);
        const uint8_t value_class = wide ? class : sign ? BUSTER_TYPED_CLASS_I64 : BUSTER_TYPED_CLASS_U64;
        const uint32_t value = check_lower_temporary(writer, value_class);
        const uint32_t fill = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
        const uint32_t result = check_lower_limb_object(writer, target);
        const uint32_t low = wide ? 2 : 1;
        if (value_class != class)
        {
            check_lower_emit(writer, BUSTER_TYPED_CONVERT, value_class, 0);
        }
        check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, value_class, value);
        for (uint32_t limb = 0; limb < low; limb += 1)
        {
            check_lower_limb_address(writer, result, true, limb);
            check_lower_emit(writer, BUSTER_TYPED_LOCAL, value_class, value);
            if (limb != 0)
            {
                check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U32, 64);
                check_lower_emit(writer, BUSTER_TYPED_SHR, value_class, 0);
            }
            if (value_class != BUSTER_TYPED_CLASS_U64)
            {
                check_lower_emit(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
            }
            check_lower_emit(writer, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_U64, 0);
        }
        if (sign)
        {
            check_lower_emit(writer, BUSTER_TYPED_LOCAL, value_class, value);
            check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U32, wide ? 127 : 63);
            check_lower_emit(writer, BUSTER_TYPED_SHR, value_class, 0);
            check_lower_emit(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
        }
        else
        {
            check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 0);
        }
        check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, fill);
        check_lower_limb_fill(writer, target, result, low, fill);
        check_lower_emit(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, result);
    }
    else if (target.count != 0)
    {
        check_lower_unsupported(writer, "conversions to _BitInt past 128 bits from this type");
    }
    else if (boolean)
    {
        check_lower_limb_truth(writer, from, false);
        check_lower_emit(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U8, 0);
    }
    else if (floating)
    {
        // __floatbitint{sf,df,xf,tf} (address, precision); a __float128 comes back into a frame object of its own.
        const TypeId arguments[2] = { BUSTER_TYPE_ULONG, BUSTER_TYPE_INT };
        check_lower_limb_precision(writer, source, true);
        check_lower_quad_call(writer, (CheckLowerRuntime)(CHECK_LOWER_RUNTIME_FLOATBITINTSF + index), type_conversion_operand(types, to), arguments, 2);
    }
    else if (integer)
    {
        // The low limb, or the low two as a 128-bit value, converted, and a narrower _BitInt's extended from its width.
        const uint32_t value = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
        check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, value);
        if (check_lower_wide_class(class))
        {
            check_lower_limb_load(writer, source, value, 1, BUSTER_TYPED_CLASS_U64, false);
            check_lower_emit(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U128, 0);
            check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U32, 64);
            check_lower_emit(writer, BUSTER_TYPED_SHL, BUSTER_TYPED_CLASS_U128, 0);
            check_lower_limb_load(writer, source, value, 0, BUSTER_TYPED_CLASS_U64, false);
            check_lower_emit(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U128, 0);
            check_lower_emit(writer, BUSTER_TYPED_OR, BUSTER_TYPED_CLASS_U128, 0);
            if (class != BUSTER_TYPED_CLASS_U128)
            {
                check_lower_emit(writer, BUSTER_TYPED_CONVERT, class, 0);
            }
        }
        else
        {
            check_lower_limb_load(writer, source, value, 0, BUSTER_TYPED_CLASS_U64, false);
            if (class != BUSTER_TYPED_CLASS_U64)
            {
                check_lower_emit(writer, BUSTER_TYPED_CONVERT, class, 0);
            }
        }
        check_lower_bit_int_extend(writer, to, false, false);
    }
    else
    {
        check_lower_unsupported(writer, "conversions from _BitInt past 128 bits to this type");
    }
}

// A folded constant of a _BitInt past 128 bits: its 128 bits' limbs, then its sign's or zeros, into a frame object of its
// own whose address it leaves (the checker folds none yet, as its values hold 128 bits).
static void check_lower_limb_constant(CheckLowerWriter* writer, const CheckValue* value)
{
    const CheckLowerLimbs limbs = check_lower_limbs(writer, value->type);
    const uint32_t result = check_lower_limb_object(writer, limbs);
    const uint64_t halves[2] = { (uint64_t)value->integer, (uint64_t)((unsigned __int128)value->integer >> 64) };
    const uint64_t fill = limbs.sign && (int64_t)halves[1] < 0 ? UINT64_MAX : 0;
    for (uint32_t limb = 0; limb < limbs.count && writer->output->status == CHECK_PROGRAM_OK; limb += 1)
    {
        check_lower_limb_address(writer, result, true, limb);
        check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, limb < 2 ? halves[limb] : fill);
        if (limb + 1 == limbs.count)
        {
            check_lower_limb_extend(writer, limbs, BUSTER_TYPED_CLASS_U64);
        }
        check_lower_emit(writer, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_U64, 0);
    }
    check_lower_emit(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, result);
}

// `destination op= value`, or an increment or a decrement, where the calculation's type is a _BitInt past 128 bits
// (check_lower_complex_update), the value's (none for an increment) and the destination's address in temporaries
// `locals` (the value's in the low 32 bits): the destination read and converted to the calculation's type, the result
// computed (check_lower_limb_compute, an increment's with 1) and converted back, then copied into a _BitInt past 128
// bits' destination, whose address is the expression's value (a postfix one's a copy of the old value's), or stored into
// a scalar one, which STORE leaves.
static void check_lower_limb_update_emit(CheckLowerWriter* writer, uint32_t node, uint64_t locals)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const CheckOp op = graph->ops[node];
    const bool increment = op >= CHECK_OP_PRE_INCREMENT && op <= CHECK_OP_POST_DECREMENT;
    const bool postfix = op == CHECK_OP_POST_INCREMENT || op == CHECK_OP_POST_DECREMENT;
    const CheckOp computed = increment ? (op == CHECK_OP_PRE_INCREMENT || op == CHECK_OP_POST_INCREMENT ? CHECK_OP_ADD : CHECK_OP_SUBTRACT) :
                             op <= CHECK_OP_SHIFT_RIGHT_ASSIGN ? (CheckOp)(CHECK_OP_ADD + (op - CHECK_OP_ADD_ASSIGN)) :
                                                                 (CheckOp)(CHECK_OP_BIT_AND + (op - CHECK_OP_BIT_AND_ASSIGN));
    const TypeId stored = stream->values[graph->operand0[node]].type;
    const TypeId calculated = stream->conversions[node].operands[0].type;
    const TypeId count = increment ? BUSTER_TYPE_INT : stream->conversions[node].operands[1].type;
    const CheckLowerLimbs stored_limbs = check_lower_limbs(writer, stored);
    const uint32_t destination = (uint32_t)(locals >> 32);
    uint32_t value = (uint32_t)locals;
    const uint32_t current = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
    const uint32_t result = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
    const uint32_t old = postfix ? check_lower_limb_object(writer, stored_limbs) : BUSTER_CHECK_NONE;
    if (increment)
    {
        value = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
        check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 1);
        check_lower_limb_conversion(writer, BUSTER_TYPE_INT, calculated);
        check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, value);
    }
    if (postfix)
    {
        check_lower_emit(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, old);
        check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, destination);
        check_lower_copy(writer, 8 * (uint64_t)stored_limbs.count, false, false);
    }
    check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, destination);
    if (stored_limbs.count == 0)
    {
        check_lower_emit(writer, BUSTER_TYPED_LOAD, check_lower_class(writer, stored), 0);
        check_lower_bit_int_extend(writer, stored, false, false);
        check_lower_limb_conversion(writer, stored, calculated);
    }
    else if (type_conversion_operand(program->types, stored) != type_conversion_operand(program->types, calculated))
    {
        check_lower_limb_conversion(writer, stored, calculated);
    }
    check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, current);
    check_lower_limb_compute(writer, computed, calculated, current, value, count);
    check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, result);
    check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, destination);
    check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, result);
    if (stored_limbs.count != 0)
    {
        if (type_conversion_operand(program->types, stored) != type_conversion_operand(program->types, calculated))
        {
            check_lower_limb_conversion(writer, calculated, stored);
        }
        check_lower_copy(writer, 8 * (uint64_t)stored_limbs.count, false, false);
        check_lower_emit(writer, postfix ? BUSTER_TYPED_LOCAL_ADDRESS : BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, postfix ? old : destination);
    }
    else
    {
        check_lower_limb_conversion(writer, calculated, stored);
        check_lower_emit(writer, BUSTER_TYPED_STORE, check_lower_class(writer, stored), 0);
    }
}

// `destination op= value` where the destination is a _BitInt past 128 bits and the calculation's type a real floating one
// (check_lower_complex_update), the value, of its class, and the destination's address in temporaries `locals` (the
// value's in the low 32 bits): the destination converted to the calculation's type (check_lower_limb_conversion), the
// result computed in its class, then converted back into a frame object of its own and copied into the destination,
// whose address is the expression's value. Only a sum, a difference, a product and a quotient take a floating value.
static void check_lower_limb_scalar_update_emit(CheckLowerWriter* writer, uint32_t node, uint64_t locals)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const TypeId stored = stream->values[graph->operand0[node]].type;
    const TypeId calculated = stream->conversions[node].operands[0].type;
    const uint8_t class = check_lower_value_class(writer, calculated);
    const uint32_t value = (uint32_t)locals;
    const uint32_t destination = (uint32_t)(locals >> 32);
    const uint32_t result = check_lower_temporary(writer, class);
    check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, destination);
    check_lower_limb_conversion(writer, stored, calculated);
    check_lower_emit(writer, BUSTER_TYPED_LOCAL, class, value);
    check_lower_emit(writer, check_lower_binary(graph->ops[node]), class, 0);
    check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, class, result);
    check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, destination);
    check_lower_emit(writer, BUSTER_TYPED_LOCAL, class, result);
    check_lower_limb_conversion(writer, calculated, stored);
    check_lower_copy(writer, 8 * (uint64_t)check_lower_limbs(writer, stored).count, false, false);
    check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, destination);
}

// ---------------------------------------------------------------------------------------------------------------------
// Complex values of aggregate parts
//
// _Complex __float128 and the complex _BitInts past 128 bits that the usual arithmetic conversions make (#137) are frame
// objects of two parts, the real one then the imaginary one, whose parts are values as their element's are: an
// aggregate's (check_lower_quad, check_lower_limb_type), each part's value its bytes' address. Their operations take each
// part through the element's (check_lower_quad_compute, check_lower_limb_compute), a part's result copied into the
// object; a real operand stays real, as for the other complex types (check_lower_complex_compute has the domains'
// rules). As clang -O0's code has them, __float128's product and quotient of two complex values are libgcc's __multc3
// and __divtc3, and a _BitInt's are the integer types' formulas: (ac - bd) + (ad + bc)i, and (ac + bd) / (cc + dd) +
// ((bc - ad) / (cc + dd))i whatever the operands' domains, its parts' products and quotients libgcc's
// (check_lower_limb_library).

// Whether a complex type's element is an aggregate: __float128, or a _BitInt past 128 bits (check_lower_complex_element).
static bool check_lower_complex_parts(const CheckLowerWriter* writer, TypeId type)
{
    const TypeId element = check_lower_complex_element(writer->program->types, type);
    return element != 0 && check_lower_aggregate(writer, element);
}

// The address of part `part` of `operand`, whose parts (or whose value, a real one's) are aggregates of `size` bytes, onto
// the stack: a complex one's part's, a real one's own for its real part and frame object `zero`'s for its imaginary one.
static void check_lower_complex_aggregate_part(CheckLowerWriter* writer, CheckLowerComplexOperand operand, uint32_t part, uint64_t size, uint32_t zero)
{
    if (operand.complex || part == 0)
    {
        check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, operand.local);
        check_lower_offset(writer, operand.complex ? part * size : 0);
    }
    else
    {
        check_lower_emit(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, zero);
    }
}

// Part `part` of `operand` (check_lower_complex_aggregate_part) into temporary `local`.
static void check_lower_complex_aggregate_into(CheckLowerWriter* writer, CheckLowerComplexOperand operand, uint32_t part, uint64_t size, uint32_t zero,
                                               uint32_t local)
{
    check_lower_complex_aggregate_part(writer, operand, part, size, zero);
    check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, local);
}

// `op` of the parts of aggregate `element` whose addresses temporaries `x` and `y` hold (`x` alone for NEGATIVE): a
// __float128's (check_lower_quad_compute) or a _BitInt's (check_lower_limb_compute), leaving a result's address or an
// equality's I32.
static void check_lower_complex_element_compute(CheckLowerWriter* writer, CheckOp op, TypeId element, uint32_t x, uint32_t y)
{
    if (element == BUSTER_TYPE_FLOAT128)
    {
        check_lower_quad_compute(writer, op, x, y);
    }
    else
    {
        check_lower_limb_compute(writer, op, element, x, y, element);
    }
}

// `op` of `a` and `b` (`a` alone for a unary one) in complex type `type` of aggregate parts, as check_lower_complex_compute's:
// a result in a frame object of its own, which it returns, or for an equality an I32 onto the stack and
// BUSTER_CHECK_NONE. Each part's sum, difference, negation, product with or quotient by a real value is the element's,
// into an object of its own that is copied into the result's part; a __float128 product and quotient of complex values
// are __multc3's and __divtc3's of the four parts (a real dividend's imaginary part zero), into the result; a _BitInt's
// product of complex values and quotient are the formulas, through temporaries `x`, `y`, `t` and `u`.
static uint32_t check_lower_complex_parts_compute(CheckLowerWriter* writer, CheckOp op, TypeId type, CheckLowerComplexOperand a, CheckLowerComplexOperand b)
{
    const TypeTable* types = writer->program->types;
    const TypeId element = check_lower_complex_element(types, type);
    const uint64_t size = type_size(types, element);
    const uint32_t alignment = type_alignment_log2(types, element);
    const bool quad = element == BUSTER_TYPE_FLOAT128;
    const bool sum = op == CHECK_OP_ADD || op == CHECK_OP_SUBTRACT;
    const bool library = quad && ((op == CHECK_OP_MULTIPLY && a.complex && b.complex) || (op == CHECK_OP_DIVIDE && b.complex));
    const bool product = !quad && op == CHECK_OP_MULTIPLY && a.complex && b.complex;
    const bool quotient = !quad && op == CHECK_OP_DIVIDE;
    const uint32_t x = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
    const uint32_t y = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
    const uint32_t t = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
    const uint32_t u = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
    const uint32_t zero = !a.complex || !b.complex ? check_lower_aggregate_zero(writer, element) : BUSTER_CHECK_NONE;
    uint32_t result = BUSTER_CHECK_NONE;
    if (op == CHECK_OP_EQUAL || op == CHECK_OP_NOT_EQUAL)
    {
        // Both parts compared, equal when both are, unequal when either is (an unordered part is).
        for (uint32_t part = 0; part < 2; part += 1)
        {
            check_lower_complex_aggregate_into(writer, a, part, size, zero, x);
            check_lower_complex_aggregate_into(writer, b, part, size, zero, y);
            check_lower_complex_element_compute(writer, op, element, x, y);
        }
        check_lower_emit(writer, op == CHECK_OP_EQUAL ? BUSTER_TYPED_AND : BUSTER_TYPED_OR, BUSTER_TYPED_CLASS_I32, 0);
    }
    else if (library)
    {
        // libgcc's __multc3 or __divtc3 of the four parts, its result into the object.
        const TypeId arguments[4] = { BUSTER_TYPE_FLOAT128, BUSTER_TYPE_FLOAT128, BUSTER_TYPE_FLOAT128, BUSTER_TYPE_FLOAT128 };
        const TypeId returned = type_unqualified(type_strip_aligned(types, type));
        result = check_lower_object(writer, 2 * size, alignment);
        const uint32_t call = writer->output->status == CHECK_PROGRAM_OK ?
                                  check_lower_runtime_call(writer, op == CHECK_OP_MULTIPLY ? CHECK_LOWER_RUNTIME_MULTC3 : CHECK_LOWER_RUNTIME_DIVTC3, returned,
                                                           arguments, 4, result) :
                                  0;
        for (uint32_t i = 0; i < 4; i += 1)
        {
            check_lower_complex_aggregate_part(writer, i < 2 ? a : b, i % 2, size, zero);
        }
        check_lower_emit(writer, BUSTER_TYPED_CALL, BUSTER_TYPED_CLASS_VOID, call);
    }
    else
    {
        result = check_lower_object(writer, 2 * size, alignment);
    }
    for (uint32_t part = 0; result != BUSTER_CHECK_NONE && !library && part < 2; part += 1)
    {
        check_lower_complex_place(writer, result, part, size);
        if (quotient)
        {
            // (ac + bd) / (cc + dd) for the real part, (bc - ad) / (cc + dd) for the imaginary one.
            check_lower_complex_aggregate_into(writer, a, part, size, zero, x);
            check_lower_complex_aggregate_into(writer, b, 0, size, zero, y);
            check_lower_complex_element_compute(writer, CHECK_OP_MULTIPLY, element, x, y);
            check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, t);
            check_lower_complex_aggregate_into(writer, a, 1 - part, size, zero, x);
            check_lower_complex_aggregate_into(writer, b, 1, size, zero, y);
            check_lower_complex_element_compute(writer, CHECK_OP_MULTIPLY, element, x, y);
            check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, u);
            check_lower_complex_element_compute(writer, part == 0 ? CHECK_OP_ADD : CHECK_OP_SUBTRACT, element, t, u);
            check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, t);
            check_lower_complex_aggregate_into(writer, b, 0, size, zero, x);
            check_lower_complex_element_compute(writer, CHECK_OP_MULTIPLY, element, x, x);
            check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, u);
            check_lower_complex_aggregate_into(writer, b, 1, size, zero, x);
            check_lower_complex_element_compute(writer, CHECK_OP_MULTIPLY, element, x, x);
            check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, y);
            check_lower_complex_element_compute(writer, CHECK_OP_ADD, element, u, y);
            check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, u);
            check_lower_complex_element_compute(writer, CHECK_OP_DIVIDE, element, t, u);
        }
        else if (product)
        {
            // ac - bd for the real part, ad + bc for the imaginary one.
            check_lower_complex_aggregate_into(writer, a, 0, size, zero, x);
            check_lower_complex_aggregate_into(writer, b, part, size, zero, y);
            check_lower_complex_element_compute(writer, CHECK_OP_MULTIPLY, element, x, y);
            check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, t);
            check_lower_complex_aggregate_into(writer, a, 1, size, zero, x);
            check_lower_complex_aggregate_into(writer, b, 1 - part, size, zero, y);
            check_lower_complex_element_compute(writer, CHECK_OP_MULTIPLY, element, x, y);
            check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, u);
            check_lower_complex_element_compute(writer, part == 0 ? CHECK_OP_SUBTRACT : CHECK_OP_ADD, element, t, u);
        }
        else if (op == CHECK_OP_NEGATIVE || (op == CHECK_OP_BIT_NOT && part == 1))
        {
            check_lower_complex_aggregate_into(writer, a, part, size, zero, x);
            check_lower_complex_element_compute(writer, CHECK_OP_NEGATIVE, element, x, x);
        }
        else if (op == CHECK_OP_BIT_NOT || (sum && part == 1 && !b.complex))
        {
            check_lower_complex_aggregate_part(writer, a, part, size, zero);
        }
        else if (sum && part == 1 && !a.complex)
        {
            // A real value plus or minus a complex one: the complex one's imaginary part, negated for a difference.
            check_lower_complex_aggregate_part(writer, b, 1, size, zero);
            if (op == CHECK_OP_SUBTRACT)
            {
                check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, x);
                check_lower_complex_element_compute(writer, CHECK_OP_NEGATIVE, element, x, x);
            }
        }
        else
        {
            // The parts' sum or difference, or a product with a real value or a quotient by one, which each part takes.
            check_lower_complex_aggregate_into(writer, a, a.complex ? part : 0, size, zero, x);
            check_lower_complex_aggregate_into(writer, b, b.complex ? part : 0, size, zero, y);
            check_lower_complex_element_compute(writer, op, element, x, y);
        }
        check_lower_copy(writer, size, false, false);
    }
    return result;
}

// The truth of the complex value of `type`, of aggregate parts, whose address is on top of the stack, as an I32: 1 when
// either part isn't zero (the element's truth: check_lower_quad_truth, check_lower_limb_truth), or with `negate` its
// negation.
static void check_lower_complex_parts_truth(CheckLowerWriter* writer, TypeId type, bool negate)
{
    const TypeTable* types = writer->program->types;
    const TypeId element = check_lower_complex_element(types, type);
    const uint32_t value = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
    check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, value);
    for (uint32_t part = 0; part < 2; part += 1)
    {
        check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, value);
        check_lower_offset(writer, part * type_size(types, element));
        if (element == BUSTER_TYPE_FLOAT128)
        {
            check_lower_quad_truth(writer, false);
        }
        else
        {
            check_lower_limb_truth(writer, element, false);
        }
    }
    check_lower_emit(writer, BUSTER_TYPED_OR, BUSTER_TYPED_CLASS_I32, 0);
    if (negate)
    {
        check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 0);
        check_lower_emit(writer, BUSTER_TYPED_EQ, BUSTER_TYPED_CLASS_I32, 0);
    }
}

// A conversion of the value on top of the stack from `from` to `to`, one of them complex of aggregate parts and the other
// a complex or a real type (check_lower_complex_conversion's): between complex types, each part converted into a frame
// object of the target's, an aggregate part by its address (copied into one); from one, its real part, whose address is
// the value's, converted, or for _Bool its truth; to one, the value converted as its real part, zero the imaginary one.
// A part's conversion is check_lower_scalar_conversion's, which takes an aggregate to and from the other types out of
// line (check_lower_aggregate_conversion).
static void check_lower_complex_parts_conversion(CheckLowerWriter* writer, TypeId from, TypeId to)
{
    const TypeTable* types = writer->program->types;
    const TypeId from_element = check_lower_complex_element(types, from);
    const TypeId to_element = check_lower_complex_element(types, to);
    const bool from_parts = from_element != 0 && check_lower_aggregate(writer, from_element);
    const bool to_parts = to_element != 0 && check_lower_aggregate(writer, to_element);
    const uint64_t from_size = from_element != 0 ? type_size(types, from_element) : 0;
    const uint64_t size = to_element != 0 ? type_size(types, to_element) : 0;
    const bool same = from_element != 0 && to_element != 0 && type_unqualified(from_element) == type_unqualified(to_element);
    const uint32_t result = to_element != 0 && !same ? check_lower_object(writer, 2 * size, type_alignment_log2(types, to_element)) : BUSTER_CHECK_NONE;
    const uint8_t source_class = from_element != 0 ? BUSTER_TYPED_CLASS_U64 : check_lower_value_class(writer, from);
    const uint32_t source = result != BUSTER_CHECK_NONE ? check_lower_temporary(writer, source_class) : BUSTER_CHECK_NONE;
    if (from_element != 0 && to_element == 0 && (check_lower_facts(writer, to) & BUSTER_CHECK_LOWER_TYPE_BOOL) != 0)
    {
        check_lower_complex_parts_truth(writer, from, false);
        check_lower_emit(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U8, 0);
    }
    else if (from_element != 0 && to_element == 0)
    {
        // The real part's value: an aggregate's its address, the complex value's.
        check_lower_scalar_conversion(writer, from_element, to);
    }
    else if (result != BUSTER_CHECK_NONE)
    {
        check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, source_class, source);
        for (uint32_t part = 0; part < 2; part += 1)
        {
            const TypeId element = from_element != 0 ? from_element : from;
            check_lower_complex_place(writer, result, part, size);
            if (from_element == 0 && part == 1 && to_parts)
            {
                // A real value's imaginary part: zero, an aggregate's 8 bytes at a time.
                for (uint64_t offset = 0; offset < size && writer->output->status == CHECK_PROGRAM_OK; offset += 8)
                {
                    if (offset != 0)
                    {
                        check_lower_complex_place(writer, result, 1, size);
                        check_lower_offset(writer, offset);
                    }
                    check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 0);
                    check_lower_emit(writer, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_U64, 0);
                }
            }
            else if (from_element == 0 && part == 1)
            {
                check_lower_emit(writer, BUSTER_TYPED_CONST, check_lower_class(writer, to_element), 0);
                check_lower_emit(writer, BUSTER_TYPED_SET, check_lower_class(writer, to_element), 0);
            }
            else
            {
                const CheckLowerComplexOperand operand = { .local = source, .complex = true };
                if (from_parts)
                {
                    check_lower_complex_aggregate_part(writer, operand, part, from_size, BUSTER_CHECK_NONE);
                }
                else if (from_element != 0)
                {
                    check_lower_complex_part(writer, operand, part, from_element);
                }
                else
                {
                    check_lower_emit(writer, BUSTER_TYPED_LOCAL, source_class, source);
                }
                check_lower_scalar_conversion(writer, element, to_element);
                if (to_parts)
                {
                    check_lower_copy(writer, size, false, false);
                }
                else
                {
                    check_lower_emit(writer, BUSTER_TYPED_SET, check_lower_class(writer, to_element), 0);
                }
            }
        }
        check_lower_emit(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, result);
    }
}

// `destination op= value` where the calculation's type is complex of aggregate parts (check_lower_complex_update), the
// value and the destination's address in temporaries `locals` (the value's in the low 32 bits): the destination read in
// the calculation's type (check_lower_complex_parts_conversion), the result computed (check_lower_complex_parts_compute)
// and stored back, converted: copied into an aggregate destination, whose address is the expression's value, or stored
// into a scalar one, which STORE leaves.
static void check_lower_complex_parts_update_emit(CheckLowerWriter* writer, uint32_t node, uint64_t locals)
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
    const bool converted = type_unqualified(type_strip_aligned(types, stored)) != type_unqualified(type_strip_aligned(types, calculated));
    const bool stored_aggregate = check_lower_aggregate(writer, stored);
    const CheckLowerComplexOperand value = { .local = (uint32_t)locals, .complex = check_lower_complex(writer, stream->values[graph->operand1[node]].type) };
    const uint32_t destination = (uint32_t)(locals >> 32);
    const CheckLowerComplexOperand current = { .local = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64), .complex = true };
    const uint32_t result = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
    check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, destination);
    if (!stored_aggregate)
    {
        check_lower_emit(writer, BUSTER_TYPED_LOAD, check_lower_class(writer, stored), 0);
        check_lower_bit_int_extend(writer, stored, false, false);
    }
    if (converted)
    {
        check_lower_complex_parts_conversion(writer, stored, calculated);
    }
    check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, current.local);
    const uint32_t object = check_lower_complex_parts_compute(writer, computed, calculated, current, value);
    if (object != BUSTER_CHECK_NONE)
    {
        check_lower_emit(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, object);
        check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, result);
        check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, destination);
        check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, result);
        if (converted)
        {
            check_lower_complex_parts_conversion(writer, calculated, stored);
        }
        if (stored_aggregate)
        {
            check_lower_copy(writer, type_size(types, stored), false, false);
            check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, destination);
        }
        else
        {
            check_lower_emit(writer, BUSTER_TYPED_STORE, check_lower_class(writer, stored), 0);
        }
    }
}

// The conversion of the value on top of the stack from `from` to `to` (a CHECK_LOWER_CONVERSION task's): a complex type's
// check_lower_complex_conversion's, any other check_lower_scalar_conversion_body's. Always inlined into the copies of the
// tasks' loop (check_lower_drain_body), with their constant `bit_ints`.
static inline __attribute__((always_inline)) void check_lower_conversion_body(CheckLowerWriter* writer, TypeId from, TypeId to, bool bit_ints)
{
    if ((check_lower_complex(writer, from) || check_lower_complex(writer, to)) && check_lower_value_class(writer, to) != BUSTER_TYPED_CLASS_VOID)
    {
        check_lower_complex_conversion(writer, from, to);
    }
    else
    {
        check_lower_scalar_conversion_body(writer, from, to, bit_ints);
    }
}

static inline __attribute__((always_inline)) void check_lower_conversion(CheckLowerWriter* writer, TypeId from, TypeId to)
{
    check_lower_conversion_body(writer, from, to, writer->bit_ints);
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
    // A complex integer type's (#137): its real part in the integer's low 64 bits and its imaginary one in the high 64, each
    // folded to the element's width and extended through 64 bits as its class has them (the checker's), or a real
    // constant's value alone; a _BitInt's part is masked to its width there, and extended from it here
    // (check_lower_bit_int_extend).
    const uint8_t element_kind = type_kind(types, element);
    const bool integer = element_kind == BUSTER_TYPE_KIND_INTEGER || element_kind == BUSTER_TYPE_KIND_BIT_INT;
    const uint64_t halves[2] = { (uint64_t)value->integer, value->constant_kind == CHECK_CONSTANT_COMPLEX ? (uint64_t)(value->integer >> 64) : 0 };
    const CheckConstantKind real = integer ? CHECK_CONSTANT_INTEGER : CHECK_CONSTANT_FLOATING;
    if (value->constant_kind != CHECK_CONSTANT_COMPLEX && value->constant_kind != real)
    {
        check_lower_fail(writer, CHECK_PROGRAM_INVALID);
    }
    else
    {
        const uint32_t result = check_lower_object(writer, 2 * size, type_alignment_log2(types, element));
        for (uint32_t part = 0; part < 2; part += 1)
        {
            check_lower_complex_place(writer, result, part, size);
            const CheckHalf half = check_lower_class_half(class);
            const uint64_t bits = integer ? halves[part] : half != CHECK_HALF_NONE ? check_action_half_bits(half, parts[part]) :
                                                                                  check_lower_float_bits(writer, class, parts[part]);
            check_lower_emit(writer, BUSTER_TYPED_CONST, class, bits);
            check_lower_bit_int_extend(writer, element, false, false);
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
    else if (check_lower_limb_value(writer, value->type))
    {
        check_lower_limb_constant(writer, value);
    }
    else if (value->constant_kind == CHECK_CONSTANT_FLOATING && check_lower_aggregate(writer, value->type))
    {
        // A __float128's, the one aggregate whose value the checker folds to a floating constant.
        check_lower_quad_constant(writer, value);
    }
    else if (check_lower_aggregate(writer, value->type) && check_lower_tile_type(writer->program->types, value->type))
    {
        check_lower_unsupported(writer, "constant expressions of 1024-byte tile vectors");
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
            // A constant's value is its width's bits: a _BitInt narrower than its class extends from its own width
            // (check_lower_bit_int_shift), which only a program with a _BitInt type can have (CheckLowerWriter.bit_ints).
            const uint32_t shift = writer->bit_ints ? check_lower_bit_int_shift(writer->program->types, value->type) : 0;
            const uint32_t width = (8u << ((class - 1) / 2)) - shift;
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
        else if (value->constant_kind == CHECK_CONSTANT_FLOATING && check_lower_class_half(class) != CHECK_HALF_NONE)
        {
            // A half-precision literal's value, exact: its bits, which its CONST writes as a U16's (check_lower_half_entry).
            bits = check_action_half_bits(check_lower_class_half(class), value->floating);
        }
        if (check_lower_wide_class(class))
        {
            // A _BitInt's of 65 to 127 bits comes folded to its width's bits too, and extends from its width here, as its
            // values are kept: a conversion to __int128 or a compare reads it as it is.
            const uint32_t shift = writer->bit_ints ? check_lower_bit_int_shift(writer->program->types, value->type) : 0;
            const unsigned __int128 integer = (unsigned __int128)value->integer << shift;
            check_lower_wide_constant(writer, class, shift == 0 ? (unsigned __int128)value->integer :
                                                     class == BUSTER_TYPED_CLASS_I128 ? (unsigned __int128)((__int128)integer >> shift) : integer >> shift);
        }
        else
        {
            check_lower_emit(writer, BUSTER_TYPED_CONST, class, bits);
        }
    }
}

// The first member a call's argument passes as when it is a transparent union's (#147), or 0: a member's value its
// parameter's union takes (BUSTER_CHECK_CONVERT_TRANSPARENT_UNION), or a union's own value, named or not, but an _Atomic
// parameter's, which passes as the atomic type.
static TypeId check_lower_transparent_argument(const CheckLowerWriter* writer, const CheckConversion* conversion, TypeId parameter)
{
    TypeId first = 0;
    // The conversion's flag and the argument's facts first: every call's argument asks, and few are aggregates.
    if ((conversion->flags & BUSTER_CHECK_CONVERT_TRANSPARENT_UNION) != 0)
    {
        first = check_lower_transparent(writer, parameter);
    }
    else if (check_lower_aggregate(writer, conversion->type) && (parameter == 0 || check_action_kind(writer->program->types, parameter) != BUSTER_TYPE_KIND_ATOMIC))
    {
        first = check_lower_transparent(writer, conversion->type);
    }
    return first;
}

// A transparent union's argument as it passes, as its first member `first` (check_lower_transparent_argument): the
// member's value, of its class (check_lower_transparent_class), or the address of the union's bytes. `member` is the
// argument's type: the union's, whose value is its object's address, or the member's that a conversion to the union
// selects, whose value is the union's first bytes, as every member has the first's size. An integer's or a pointer's bits
// are the first member's value; a struct's, a union's or a complex value's bytes are at its address; any other value's go
// through a temporary, whose bytes are the union's.
static void check_lower_transparent_value(CheckLowerWriter* writer, uint32_t node, uint32_t operand, TypeId member, TypeId first)
{
    const uint8_t first_class = check_lower_transparent_class(writer, first);
    const uint8_t member_class = check_lower_class(writer, member);
    const bool address = check_lower_aggregate(writer, member);
    // The tasks run last pushed first: the operand, then what passes it.
    if (!address && first_class != UINT8_MAX && member_class >= BUSTER_TYPED_CLASS_I8 && member_class <= BUSTER_TYPED_CLASS_U64)
    {
        if (member_class != first_class)
        {
            check_lower_instruction(writer, BUSTER_TYPED_CONVERT, first_class, 0);
        }
    }
    else if (!address)
    {
        const bool object = check_lower_wide(writer, member) || check_lower_vector_of(writer, member) != 0;
        const uint32_t local = object ? check_lower_temporary_of(writer, member) : check_lower_addressed_temporary(writer, member_class);
        if (first_class != UINT8_MAX)
        {
            check_lower_instruction(writer, BUSTER_TYPED_LOAD, first_class, 0);
        }
        check_lower_instruction(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, local);
        check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, member_class, local);
    }
    else if (first_class != UINT8_MAX)
    {
        check_lower_instruction(writer, BUSTER_TYPED_LOAD, first_class, 0);
    }
    check_lower_operand(writer, node, operand, false, member);
}

static SymbolId check_lower_runtime(CheckLowerWriter* writer, CheckLowerRuntime runtime);

// A call's entries, as check_lower_call_to and check_lower_call_halves compile them: a normal source call, or an exactly
// typed fortified libc call (`runtime`), with `halves` one that passes a half-precision argument, in an XMM register's low
// 16 bits (a float's, its bits moved there as it is passed: check_lower_half_entry's CONVERT); without, any other, whose
// half-precision argument (check_lower_call_passed) it leaves to check_lower_call_halves, returning true, so that its
// own code has no test of one. A removed interval (`omitted` arguments from `omit`) contains only certified effect-free
// constants; source argument indices and conversions remain unchanged for every retained argument.
static inline __attribute__((always_inline)) bool check_lower_call_body(CheckLowerWriter* writer, uint32_t node, CheckLowerRuntime runtime, uint32_t omit,
                                                                         uint32_t omitted, bool halves)
{
    bool retry = false;
    const CheckGraph* graph = &writer->program->input->graph;
    const CheckStream* stream = writer->program->stream;
    const TypeTable* types = writer->program->types;
    const bool builtin = runtime != CHECK_LOWER_RUNTIMES;
    const uint32_t callee = builtin ? node : graph->operand0[node];
    // A musttail return's call (check_lower_tail_return): a TAIL_CALL, whose result is the function's own, with no object.
    const bool tail = node == writer->tail;
    // The call's entries are its callee's line's, as GCC and clang place a call (the call's own node is its closing
    // parenthesis, which a call across lines ends on).
    check_lower_source(writer, graph->tokens[callee]);
    CheckValue value = builtin ? (CheckValue){ .type = graph->types[node], .category = CHECK_FUNCTION } : stream->values[callee];
    const uint32_t overload = check_program_builtin_index(stream->overloads, stream->overload_count, sizeof(CheckOverloadEffect), node);
    if (overload != BUSTER_CHECK_NONE)
    {
        const CheckOverloadResult* selected = &stream->overloads[overload].selected;
        value = (CheckValue){ .type = selected->type, .category = CHECK_FUNCTION, .constant_kind = CHECK_CONSTANT_ADDRESS,
                              .object = selected->object, .flags = BUSTER_CHECK_STATIC_OBJECT };
    }
    const uint32_t source_count = graph->argument_counts[node];
    const uint32_t count = source_count - omitted;
    const uint32_t first = graph->argument_offsets[node];
    TypeId type = type_strip_aligned(types, value.type);
    if (type_kind(types, type) == BUSTER_TYPE_KIND_POINTER)
    {
        type = type_strip_aligned(types, type_element(types, type));
    }
    uint32_t parameters = 0;
    const uint8_t flags = types->types.flags[type_index(type)];
    // Keep a known K&R signature's atomic ABI types even though its designator has no prototype.
    const bool kr = (flags & BUSTER_TYPE_FLAG_PROTOTYPE) == 0 && type_kind(types, graph->types[node]) == BUSTER_TYPE_KIND_FUNCTION;
    const TypeId* parameter_types = type_parameters(types, kr ? graph->types[node] : type, &parameters);
    const bool variadic = (flags & BUSTER_TYPE_FLAG_VARIADIC) != 0 || (flags & BUSTER_TYPE_FLAG_PROTOTYPE) == 0;
    CheckLowerAbi* abi = writer->abi;
    // What the parameters' and the arguments' types need that isn't lowered, the first one's (a struct or a union passes
    // its address, and its bytes go where abi_assign puts them).
    const char* passed = NULL;
    for (uint32_t a = 0; a < parameters && passed == NULL; a += 1)
    {
        passed = halves ? check_lower_passed(writer, parameter_types[a]) : check_lower_call_passed(writer, parameter_types[a]);
    }
    for (uint32_t kept = 0; kept < count && passed == NULL; kept += 1)
    {
        const uint32_t a = kept + (kept >= omit ? omitted : 0);
        const TypeId argument = stream->argument_conversions[first + a].type;
        passed = halves ? check_lower_passed(writer, argument) : check_lower_call_passed(writer, argument);
    }
    // A struct or union result lands in a frame object of the call's own, whose address is the call's value, and so do a
    // 128-bit integer's and a vector's, which LOCAL then reads.
    const TypeId result = stream->values[node].type;
    const bool aggregate_result = check_lower_aggregate(writer, result);
    const bool wide_result = check_lower_wide(writer, result) || check_lower_vector_of(writer, result) != 0;
    const char* returned = aggregate_result ? check_lower_by_value(writer, result, true) : check_lower_class(writer, result) == UINT8_MAX ? check_lower_type_feature(types, result) : NULL;
    // A callee that is a function's own symbol is called directly; any other (a pointer's value, a function designator
    // through one) is evaluated before the arguments and called through: Tier 0's indirect call, callee 0.
    const bool direct = builtin || (value.category != CHECK_LVALUE && value.constant_kind == CHECK_CONSTANT_ADDRESS && value.object != 0 && value.offset == 0 &&
                        value.object < writer->program->object_count && writer->program->object_symbols[value.object] != 0 &&
                        (value.flags & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_FP_ENVIRONMENT)) == 0);
    if (passed != NULL || count > BUSTER_TIER0_MAX_CALL_ARGUMENTS || returned != NULL)
    {
        // A half-precision argument (the first type it finds of the parameters' and the arguments'): the call is
        // check_lower_call_halves', which checks them all again.
        retry = !halves && passed == check_lower_half_passed && count <= BUSTER_TIER0_MAX_CALL_ARGUMENTS && returned == NULL;
        if (!retry)
        {
            check_lower_unsupported(writer, count > BUSTER_TIER0_MAX_CALL_ARGUMENTS ? "calls with more than 239 arguments" :
                                            passed != NULL && passed != check_lower_half_passed ? passed : returned);
        }
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
        const bool object_result = !tail && (aggregate_result || wide_result);
        const uint32_t object = !object_result ? BUSTER_CHECK_NONE :
                                aggregate_result ? check_lower_object(writer, type_size(types, result), declared > canonical ? declared : canonical) :
                                                   check_lower_temporary_of(writer, result);
        writer->call_targets[call] = builtin ? check_lower_runtime(writer, runtime) : direct ? writer->program->object_symbols[value.object] : 0;
        writer->call_first[call] = writer->argument_count;
        writer->call_counts[call] = count;
        writer->call_variadic[call] = variadic;
        writer->call_result_objects[call] = object_result ? object + 1 : 0;
        writer->aggregates |= object_result;
        writer->tails |= tail;
        abi->first[abi->count] = abi->argument_count;
        abi->results[abi->count] = result;
        abi->counts[abi->count] = count;
        abi->named[abi->count] = (flags & BUSTER_TYPE_FLAG_PROTOTYPE) != 0 ? parameters - omitted : count;
        abi->variadic[abi->count++] = variadic;
        // Whether an argument is a transparent union's, which the operands' loop asks again only then.
        bool transparent = false;
        for (uint32_t kept = 0; kept < count; kept += 1)
        {
            const uint32_t a = kept + (kept >= omit ? omitted : 0);
            // A 128-bit integer argument is a 16-byte object's address, the value set into it after it is made, and a
            // vector's an object of its bytes.
            const CheckConversion* conversion = stream->argument_conversions + first + a;
            const TypeId argument = conversion->type;
            TypeId passed_type = a < parameters && check_action_kind(types, parameter_types[a]) == BUSTER_TYPE_KIND_ATOMIC ? parameter_types[a] : argument;
            bool aggregate = check_lower_aggregate(writer, argument);
            uint32_t vector = check_lower_vector_of(writer, argument);
            bool wide = check_lower_wide(writer, argument) || vector != 0;
            uint8_t class = wide                                                              ? BUSTER_TYPED_CLASS_U64 :
                            halves && check_lower_half_of(writer, argument) != CHECK_HALF_NONE ? BUSTER_TYPED_CLASS_F32 :
                                                                                                 check_lower_value_class(writer, argument);
            // A transparent union's passes as its first member: that member's value, or the union's bytes
            // (check_lower_transparent_class). Only a conversion to one or an aggregate's value may be one.
            const TypeId member = aggregate || (conversion->flags & BUSTER_CHECK_CONVERT_TRANSPARENT_UNION) != 0
                                      ? check_lower_transparent_argument(writer, conversion, a < parameters ? parameter_types[a] : 0)
                                      : 0;
            if (member != 0)
            {
                const uint8_t member_class = check_lower_transparent_class(writer, member);
                passed_type = member;
                aggregate = member_class == UINT8_MAX;
                vector = 0;
                wide = false;
                class = aggregate ? BUSTER_TYPED_CLASS_U64 : member_class;
                transparent = true;
            }
            abi->arguments[abi->argument_count++] = passed_type;
            writer->argument_sizes[writer->argument_count] = aggregate ? (uint32_t)type_size(types, passed_type) : vector != 0 ? vector : wide ? 16 : 0;
            writer->aggregates |= aggregate || wide;
            writer->argument_classes[writer->argument_count++] = class;
        }
        if (aggregate_result && object_result)
        {
            check_lower_instruction(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, object);
        }
        else if (wide_result && object_result)
        {
            check_lower_instruction(writer, BUSTER_TYPED_LOCAL, result_class, object);
        }
        check_lower_instruction(writer, tail ? BUSTER_TYPED_TAIL_CALL : BUSTER_TYPED_CALL,
                                tail || aggregate_result || wide_result ? BUSTER_TYPED_CLASS_VOID : result_class, call);
        for (uint32_t kept = count; kept-- > 0 && writer->output->status == CHECK_PROGRAM_OK;)
        {
            const uint32_t a = kept + (kept >= omit ? omitted : 0);
            const CheckConversion* conversion = stream->argument_conversions + first + a;
            const TypeId argument = conversion->type;
            const bool atomic = a < parameters && check_action_kind(types, parameter_types[a]) == BUSTER_TYPE_KIND_ATOMIC;
            const TypeId member = transparent ? check_lower_transparent_argument(writer, conversion, a < parameters ? parameter_types[a] : 0) : 0;
            if (member != 0)
            {
                check_lower_transparent_value(writer, node, a + 3, argument, member);
            }
            else if (atomic && check_lower_aggregate(writer, argument))
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
                else if (halves && check_lower_half_of(writer, argument) != CHECK_HALF_NONE)
                {
                    check_lower_instruction(writer, BUSTER_TYPED_CONVERT, check_lower_class(writer, argument), 0);
                }
                check_lower_operand(writer, node, a + 3, false, argument);
            }
        }
        if (!direct)
        {
            check_lower_operand(writer, node, 0, false, 0);
        }
    }
    return retry;
}

// A call that passes a half-precision argument (check_lower_call_body's), out of line.
static __attribute__((noinline)) void check_lower_call_halves(CheckLowerWriter* writer, uint32_t node, CheckLowerRuntime runtime, uint32_t omit, uint32_t omitted)
{
    (void)check_lower_call_body(writer, node, runtime, omit, omitted, true);
}

// A normal source call, or an exactly typed fortified libc call (check_lower_call_body's).
static void check_lower_call_to(CheckLowerWriter* writer, uint32_t node, CheckLowerRuntime runtime, uint32_t omit, uint32_t omitted)
{
    if (check_lower_call_body(writer, node, runtime, omit, omitted, false))
    {
        check_lower_call_halves(writer, node, runtime, omit, omitted);
    }
}

static void check_lower_call(CheckLowerWriter* writer, uint32_t node)
{
    check_lower_call_to(writer, node, CHECK_LOWER_RUNTIMES, 0, 0);
}

static void check_lower_helper(CheckLowerWriter* writer, uint32_t node)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const CheckProgramHelpers* helpers = &writer->output->helpers;
    const uint32_t selected = check_program_builtin_index(helpers->uses, helpers->use_count, sizeof(CheckProgramHelperUse), node);
    // A scoped family's builtin calls its base's helper (check_builtin_atomic_base) on the base's arguments, its scope, the
    // last, evaluated after them and dropped.
    const uint32_t scoped = check_builtin_atomic_scope((CBuiltinId)graph->members[node]) != CHECK_BUILTIN_ATOMIC_SCOPE_NONE;
    const uint32_t count = graph->argument_counts[node] - scoped;
    const uint32_t first = graph->argument_offsets[node];
    const CBuiltinId source_id = check_builtin_atomic_base((CBuiltinId)graph->members[node]);
    const bool c11 = check_builtin_atomic_c11(source_id);
    const bool compare = source_id == CBUILTIN_C11_ATOMIC_COMPARE_EXCHANGE_STRONG || source_id == CBUILTIN_C11_ATOMIC_COMPARE_EXCHANGE_WEAK;
    const bool init = source_id == CBUILTIN_C11_ATOMIC_INIT;
    const uint32_t parameters = count + compare + init;
    // clang's code computes the IEEE maximum and minimum by AVX10.2's vminmax in a function whose target has it, whose NaN
    // results aren't the expansion's that the leaves compute (cbuiltin_atomic_rmw_ieee_body).
    const bool minmax = source_id >= CBUILTIN_ATOMIC_FETCH_FMAXIMUM && source_id <= CBUILTIN_ATOMIC_FETCH_FMINIMUM_NUM &&
                        (writer->target.extensions & BUSTER_X86_EXTENSION(AVX10_2)) != 0;
    CheckLowerAbi* abi = writer->abi;
    if (selected == BUSTER_CHECK_NONE || helpers->uses[selected].helper >= helpers->count)
    {
        check_lower_unsupported(writer, "this builtin");
    }
    else if (minmax)
    {
        check_lower_unsupported(writer, "clang's atomic IEEE maximum and minimum for a target with AVX10.2");
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
            if (scoped != 0)
            {
                check_lower_operand(writer, node, count + 3, false, BUSTER_TYPE_VOID);
            }
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
        if ((writer->runtime_mask[runtime >> 6] & (UINT64_C(1) << (runtime & 63))) == 0)
        {
            writer->runtime_mask[runtime >> 6] |= UINT64_C(1) << (runtime & 63);
            writer->runtimes[writer->runtime_count++] = (uint16_t)runtime;
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
            // A 128-bit integer's and a __float128's value passes from its object, whose address the stack holds.
            const bool wide = check_lower_wide(writer, arguments[i]) || check_lower_quad(writer, arguments[i]);
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

// A 128-bit integer's division or remainder (`kind` DIV or MOD, of `class`), which Tier 0 has none for: the call of
// libgcc's (__divti3, __udivti3, __modti3, __umodti3) clang -O0 makes, and the frame objects its operands go into, `a`
// and `b`, passed by their addresses, the first one's taking the result, which comes back from it.
static uint32_t check_lower_wide_division(CheckLowerWriter* writer, uint8_t kind, uint8_t class, uint32_t* a, uint32_t* b)
{
    const bool is_signed = class == BUSTER_TYPED_CLASS_I128;
    const CheckLowerRuntime runtime = kind == BUSTER_TYPED_DIV ? (is_signed ? CHECK_LOWER_RUNTIME_DIVTI3 : CHECK_LOWER_RUNTIME_UDIVTI3)
                                                               : (is_signed ? CHECK_LOWER_RUNTIME_MODTI3 : CHECK_LOWER_RUNTIME_UMODTI3);
    const TypeId type = is_signed ? BUSTER_TYPE_INT128 : BUSTER_TYPE_UINT128;
    const TypeId arguments[2] = { type, type };
    *a = check_lower_temporary(writer, class);
    *b = check_lower_temporary(writer, class);
    return writer->output->status == CHECK_PROGRAM_OK ? check_lower_runtime_call(writer, runtime, type, arguments, 2, *a) : BUSTER_CHECK_NONE;
}

// An operation of `kind` on the two values on top of the stack, of `class`, as instructions pushed for after them: its
// entry, or for a 128-bit integer's division and remainder libgcc's call (check_lower_wide_division).
static void check_lower_operation(CheckLowerWriter* writer, uint8_t kind, uint8_t class)
{
    if (check_lower_wide_class(class) && (kind == BUSTER_TYPED_DIV || kind == BUSTER_TYPED_MOD))
    {
        uint32_t a = 0;
        uint32_t b = 0;
        const uint32_t call = check_lower_wide_division(writer, kind, class, &a, &b);
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
// more is undefined, so its low 64 bits keep every defined one), and a _BitInt's past 128 bits, its limbs' address, its
// low limb loaded, for the same reason; as an instruction pushed before the count's operand. The kinds past the shifts'
// are the compares', whose right operand's type is `count` too: a compare of half-precision values (of their bits on the
// stack, which no integer compare has) is the instruction pushed last, check_lower_operation's, given its operands' class,
// which check_lower_half_entry writes as a float compare.
static void check_lower_count(CheckLowerWriter* writer, uint8_t kind, TypeId count)
{
    if (kind >= BUSTER_TYPED_SHL)
    {
        if (kind <= BUSTER_TYPED_SHR && check_lower_wide(writer, count))
        {
            check_lower_instruction(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
        }
        else if (kind <= BUSTER_TYPED_SHR && check_lower_limb_value(writer, count))
        {
            check_lower_instruction(writer, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_U64, 0);
        }
        else if (kind >= BUSTER_TYPED_EQ && kind <= BUSTER_TYPED_GE && check_lower_half_of(writer, count) != CHECK_HALF_NONE && writer->depth != 0)
        {
            CheckLowerTaskPacked* compare = writer->tasks + writer->depth - 1;
            compare->control = (compare->control & ~(UINT64_C(0xffff) << 48)) | (uint64_t)check_lower_class(writer, count) << 48;
        }
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

// The computation of an update whose stored class or computation's class is past VECTOR (check_lower_update's and
// check_lower_bit_update's), out of line: from the old value on top, converted to `through` (check_lower_update_through:
// the computation's type `calculated`, or float), to the result of it, of `through`. A half-precision computation's is
// in float: the old value extended, the right operand a float (its chain's, unrounded, under excess precision), the
// result rounded once, as clang computes it, but neither where the old value is a float already and the result stays
// one; a real computation's of a half-precision value is its class's, as check_lower_update's; and any other class's
// entries are refused as they are written, as check_lower_update's would be.
static __attribute__((noinline)) void check_lower_update_classless(CheckLowerWriter* writer, uint32_t node, uint8_t kind, bool increment, TypeId calculated, TypeId through,
                                                                   TypeId right_type)
{
    const uint8_t class = check_lower_class(writer, calculated);
    const CheckHalf half = check_lower_class_half(class);
    if (half == CHECK_HALF_BRAIN && !check_lower_half_excess(writer, half))
    {
        check_lower_unsupported(writer, "__bf16 arithmetic where the unit's target has AVX10.2");
    }
    else if (half != CHECK_HALF_NONE)
    {
        check_lower_convert(writer, BUSTER_TYPE_FLOAT, through);
        check_lower_instruction(writer, kind, BUSTER_TYPED_CLASS_F32, 0);
        if (increment)
        {
            check_lower_instruction(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_F32, UINT32_C(0x3f800000));
        }
        else
        {
            check_lower_half_operand(writer, node, 1, right_type, check_lower_half_excess(writer, half));
        }
        check_lower_convert(writer, through, BUSTER_TYPE_FLOAT);
    }
    else
    {
        // A real computation's (an increment's is of the half-precision type), or no class's.
        check_lower_operation(writer, kind, class);
        if (increment)
        {
            check_lower_instruction(writer, BUSTER_TYPED_CONST, class, 1);
        }
        else
        {
            check_lower_operand(writer, node, 1, false, right_type);
        }
    }
}

// The type an update whose stored class or computation's class is past VECTOR converts its old value to and its result
// from (check_lower_update's and check_lower_bit_update's `through`): its computation's type `calculated`, but float
// for a half-precision computation under excess precision into a destination of another type (an integer, a _Bool, a
// bit-field, the other half-precision format), which clang's code converts to float and back directly (its compound
// assignment's promotion types), with no rounding to the computation's type, as the C standard's excess precision
// allows.
static __attribute__((noinline)) TypeId check_lower_update_through(const CheckLowerWriter* writer, TypeId stored, TypeId calculated)
{
    const CheckHalf half = check_lower_half_of(writer, calculated);
    const bool other = check_lower_half_of(writer, stored) != half;
    return half != CHECK_HALF_NONE && other && check_lower_half_excess(writer, half) ? BUSTER_TYPE_FLOAT : calculated;
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
    // A _BitInt's computes in its computing class (check_lower_computed) and is extended from its width after
    // (check_lower_bit_int_extend), before it is converted and stored.
    const uint8_t computing = check_lower_computed(calculated_class);
    const bool bit_int = writer->bit_ints && type_kind(program->types, type_strip_aligned(program->types, calculated)) == BUSTER_TYPE_KIND_BIT_INT;
    // The old value of a _BitInt narrower than its class is extended from its width as it is read, before a postfix one
    // keeps it and anything computes with it: its bits past the width in memory are whatever wrote them last (a union's
    // other member, a character type's store), which clang's loads ignore, as check_lower_bit_int_node's do (#534's
    // review).
    const bool old_bit_int = writer->bit_ints && check_lower_bit_int_shift(program->types, stored) != 0;
    const bool increment = op >= CHECK_OP_PRE_INCREMENT && op <= CHECK_OP_POST_DECREMENT;
    // A vector's compound assignment is its operator's row (check_lower_vector_row) between the load and the store, its
    // right operand a scalar's splat where it isn't a vector, at the stored vector's type, as a shift's count is. Past
    // VECTOR, the last of Tier 0's classes, the half-precision classes and no class compute out of line
    // (check_lower_update_classless), a half-precision computation's into a destination of another type too, through
    // `through` (check_lower_update_through).
    const bool vector = stored_class >= BUSTER_TYPED_CLASS_VECTOR || calculated_class >= BUSTER_TYPED_CLASS_VECTOR;
    const TypeId through = vector ? check_lower_update_through(writer, stored, calculated) : calculated;
    const uint32_t vector_op = op <= CHECK_OP_SHIFT_RIGHT_ASSIGN ? CHECK_OP_ADD + (uint32_t)(op - CHECK_OP_ADD_ASSIGN) : CHECK_OP_BIT_AND + (uint32_t)(op - CHECK_OP_BIT_AND_ASSIGN);
    if (vector && increment && stored_class == BUSTER_TYPED_CLASS_VECTOR)
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
    check_lower_convert(writer, through, stored);
    if (vector)
    {
        if (stored_class == BUSTER_TYPED_CLASS_VECTOR)
        {
            const bool scalar = check_lower_vector_bytes(program->types, stream->values[graph->operand1[node]].type) == 0;
            check_lower_vector_half_chain(writer, calculated, vector_op, BUSTER_CHECK_NONE, graph->operand1[node]);
            check_lower_vector_row(writer, vector_op, calculated, 0);
            check_lower_vector_operand(writer, node, 1, scalar ? calculated : right_type);
        }
        else
        {
            check_lower_update_classless(writer, node, increment ? subtract ? BUSTER_TYPED_SUB : BUSTER_TYPED_ADD : check_lower_binary(op), increment, calculated, through,
                                         right_type);
        }
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
        if (bit_int)
        {
            check_lower_bit_int_extend(writer, calculated, true, true);
        }
        check_lower_operation(writer, kind, computing);
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
            check_lower_instruction(writer, BUSTER_TYPED_CONST, computing, one);
        }
        else
        {
            if (computing != calculated_class && kind != BUSTER_TYPED_SHL && kind != BUSTER_TYPED_SHR)
            {
                check_lower_instruction(writer, BUSTER_TYPED_CONVERT, computing, 0);
            }
            check_lower_operand(writer, node, 1, false, right_type);
        }
    }
    if (computing != calculated_class && !pointer)
    {
        check_lower_instruction(writer, BUSTER_TYPED_CONVERT, computing, 0);
    }
    check_lower_convert(writer, stored, through);
    if (direct)
    {
        if (postfix)
        {
            check_lower_instruction(writer, BUSTER_TYPED_DUP, stored_class, 0);
        }
        if (old_bit_int)
        {
            check_lower_bit_int_extend(writer, stored, false, true);
        }
        check_lower_instruction(writer, get, stored_class, destination);
    }
    else if (!postfix)
    {
        if (old_bit_int)
        {
            check_lower_bit_int_extend(writer, stored, false, true);
        }
        check_lower_instruction(writer, BUSTER_TYPED_LOAD, stored_class, check_lower_access_value(program->types, stored_class, stored));
        check_lower_instruction(writer, BUSTER_TYPED_DUP, BUSTER_TYPED_CLASS_U64, 0);
        check_lower_operand(writer, node, 0, true, 0);
    }
    else
    {
        check_lower_instruction(writer, BUSTER_TYPED_LOCAL, stored_class, old_temporary);
        check_lower_instruction(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, address_temporary);
        check_lower_instruction(writer, BUSTER_TYPED_STORE_LOCAL, stored_class, old_temporary);
        if (old_bit_int)
        {
            check_lower_bit_int_extend(writer, stored, false, true);
        }
        check_lower_instruction(writer, BUSTER_TYPED_LOAD, stored_class, 0);
        check_lower_instruction(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, address_temporary);
        check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, address_temporary);
        check_lower_operand(writer, node, 0, true, 0);
    }
}

// Whether operation `op` of an expression of `result` computes on complex values (check_lower_complex_compute), on
// __float128s (check_lower_quad_compute) or on _BitInts past 128 bits (check_lower_limb_compute): an arithmetic one whose
// result is one, or a comparison whose operands are (`operand` the first one's converted type). Every node that the
// expansion's earlier cases leave asks it, so it is inline: the operators' range, then one type's facts.
static inline __attribute__((always_inline)) bool check_lower_complex_operation(const CheckLowerWriter* writer, CheckOp op, TypeId result, TypeId operand)
{
    // An aggregate's arithmetic and comparisons are a complex value's (equality alone), a __float128's or a _BitInt's past
    // 128 bits (check_lower_aggregate), whose remainders, shifts and bitwise operations C gives no other aggregate. The
    // arithmetic operators and the comparisons are the operators from the negation to the exclusive or, but `!`.
    const bool comparison = op >= CHECK_OP_LESS && op <= CHECK_OP_NOT_EQUAL;
    return op >= CHECK_OP_NEGATIVE && op <= CHECK_OP_BIT_XOR && op != CHECK_OP_NOT && check_lower_aggregate(writer, comparison ? operand : result);
}

// The type an operand of a complex operation is evaluated to, from its own type and the computation's (`converted`, the
// checker's conversion of it): a complex one to the computation's type, a real one to its element, keeping its domain.
static TypeId check_lower_complex_target(const CheckLowerWriter* writer, TypeId own, TypeId converted)
{
    return check_lower_complex(writer, own) || !check_lower_complex(writer, converted) ? converted : check_lower_complex_element(writer->program->types, converted);
}

// `destination op= value` where either is an aggregate (check_lower_complex_update_emit, check_lower_quad_update_emit,
// check_lower_limb_update_emit, check_lower_complex_parts_update_emit, once the destination's address and the value are
// evaluated): of a sum, a difference, a product or a quotient; or where the destination is a _BitInt past 128 bits and
// the calculation's type a real floating one (check_lower_limb_scalar_update_emit).
static void check_lower_complex_update(CheckLowerWriter* writer, uint32_t node)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckOp op = graph->ops[node];
    const TypeId calculated = program->stream->conversions[node].operands[0].type;
    // A _BitInt's past 128 bits takes each one, and an increment's has no value (check_lower_limb_update_emit); so does a
    // real floating calculation's type, whose value is of its class, into such a destination.
    const bool limbs = check_lower_limb_value(writer, calculated) || !check_lower_aggregate(writer, calculated);
    const bool increment = op >= CHECK_OP_PRE_INCREMENT && op <= CHECK_OP_POST_DECREMENT;
    if (!limbs && op != CHECK_OP_ADD_ASSIGN && op != CHECK_OP_SUBTRACT_ASSIGN && op != CHECK_OP_MULTIPLY_ASSIGN && op != CHECK_OP_DIVIDE_ASSIGN)
    {
        check_lower_unsupported(writer, check_lower_quad(writer, calculated) ? "increments of __float128 values" : "increments of complex values");
    }
    else
    {
        // The destination's address and the value each into a temporary as it is evaluated (check_lower_expand's complex
        // operations say why).
        const TypeTable* types = program->types;
        const TypeId own = increment ? calculated : program->stream->values[graph->operand1[node]].type;
        const TypeId converted = increment ? calculated : program->stream->conversions[node].operands[1].type;
        const uint8_t class = limbs ? check_lower_value_class(writer, converted) :
                              check_lower_complex(writer, own) || check_lower_quad(writer, calculated) || check_lower_complex_parts(writer, calculated) ?
                                                                                                          BUSTER_TYPED_CLASS_U64 :
                              check_lower_class(writer, check_lower_complex_element(types, calculated));
        const uint32_t value = increment ? BUSTER_CHECK_NONE : check_lower_temporary(writer, class);
        const uint32_t destination = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
        check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_COMPLEX, .node = node, .value = value | (uint64_t)destination << 32, .address = true });
        if (!increment)
        {
            check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, class, value);
            check_lower_operand(writer, node, 1, false, check_lower_complex_target(writer, own, converted));
        }
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
    if (check_lower_quad(writer, type))
    {
        check_lower_quad_compute(writer, op, first.local, last.local);
    }
    else if (check_lower_limb_value(writer, type))
    {
        check_lower_limb_compute(writer, op, type, first.local, last.local, stream->conversions[node].operands[1].type);
    }
    else
    {
        const uint32_t result = check_lower_complex_parts(writer, type) ? check_lower_complex_parts_compute(writer, op, type, first, last) :
                                                                          check_lower_complex_compute(writer, op, type, first, last);
        if (result != BUSTER_CHECK_NONE)
        {
            check_lower_emit(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, result);
        }
    }
}

// `destination op= value` (check_lower_complex_update), the value and the destination's address in temporaries `locals`
// (the value's in the low 32 bits): the destination read in the calculation's type, the result computed
// (check_lower_complex_compute) and stored back, converted: a complex destination's parts, whose address is the
// expression's value, or a real one's value (a _Bool one's the result's truth), which STORE leaves. A _BitInt
// destination past 128 bits, or such a part of a complex one, is an aggregate (check_lower_limb_type): read through its
// conversion from its address, and written by a copy of its converted value's bytes, the destination's address the
// expression's value.
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
    const TypeId stored_element = stored_complex ? check_lower_complex_element(types, stored) : stored;
    const bool stored_aggregate = check_lower_aggregate(writer, stored_element);
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
        if (!stored_aggregate)
        {
            check_lower_emit(writer, BUSTER_TYPED_LOAD, check_lower_class(writer, stored), 0);
            check_lower_bit_int_extend(writer, stored, false, false);
        }
        check_lower_scalar_conversion(writer, stored, element);
    }
    check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, current_class, current.local);
    const uint32_t result = check_lower_complex_compute(writer, computed, calculated, current, value);
    for (uint32_t part = 0; stored_complex && result != BUSTER_CHECK_NONE && part < 2; part += 1)
    {
        check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, destination);
        check_lower_offset(writer, part * type_size(types, stored_element));
        check_lower_complex_place(writer, result, part, type_size(types, element));
        check_lower_emit(writer, BUSTER_TYPED_LOAD, class, 0);
        check_lower_scalar_conversion(writer, element, stored_element);
        if (stored_aggregate)
        {
            check_lower_copy(writer, type_size(types, stored_element), false, false);
        }
        else
        {
            check_lower_emit(writer, BUSTER_TYPED_SET, check_lower_class(writer, stored_element), 0);
        }
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
        if (stored_aggregate)
        {
            check_lower_copy(writer, type_size(types, stored), false, false);
            check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, destination);
        }
        else
        {
            check_lower_emit(writer, BUSTER_TYPED_STORE, check_lower_class(writer, stored), 0);
        }
    }
}

// A complex values', __float128s' or _BitInts' past 128 bits task (CHECK_LOWER_COMPLEX or CHECK_LOWER_COMPLEX_TRUTH), or
// a half-precision chain link's (CHECK_LOWER_HALF), out of the tasks' loop: few functions have them.
static __attribute__((noinline)) void check_lower_complex_task(CheckLowerWriter* writer, CheckLowerTask task)
{
    const bool half = task.kind == CHECK_LOWER_HALF;
    const TypeId type = task.kind == CHECK_LOWER_COMPLEX_TRUTH ? task.type : half ? BUSTER_TYPE_VOID : writer->program->stream->conversions[task.node].operands[0].type;
    const bool quad = check_lower_quad(writer, type);
    const bool limbs = check_lower_limb_value(writer, type);
    if (half)
    {
        check_lower_half_task(writer, task);
    }
    else if (task.kind == CHECK_LOWER_COMPLEX_TRUTH && quad)
    {
        check_lower_quad_truth(writer, task.address);
    }
    else if (task.kind == CHECK_LOWER_COMPLEX_TRUTH && limbs)
    {
        check_lower_limb_truth(writer, type, task.address);
    }
    else if (task.kind == CHECK_LOWER_COMPLEX_TRUTH && check_lower_complex_parts(writer, type))
    {
        check_lower_complex_parts_truth(writer, type, task.address);
    }
    else if (task.kind == CHECK_LOWER_COMPLEX_TRUTH)
    {
        check_lower_complex_truth(writer, task.type, task.address);
    }
    else if (task.address && quad)
    {
        check_lower_quad_update_emit(writer, task.node, task.value);
    }
    else if (task.address && limbs)
    {
        check_lower_limb_update_emit(writer, task.node, task.value);
    }
    else if (task.address && check_lower_complex_parts(writer, type))
    {
        check_lower_complex_parts_update_emit(writer, task.node, task.value);
    }
    else if (task.address && !check_lower_aggregate(writer, type))
    {
        check_lower_limb_scalar_update_emit(writer, task.node, task.value);
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
    // A half-precision computation's (the field's value converted to `through`, check_lower_update_through) is out of line
    // (check_lower_update_classless).
    const bool half = check_lower_class_half(calculated_class) != CHECK_HALF_NONE;
    if (destination == BUSTER_CHECK_NONE || !check_lower_bit_member(program, destination, &field) || (calculated_class >= BUSTER_TYPED_CLASSES && !half))
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
        // A narrow _BitInt's computes in its computing class (check_lower_computed), and goes back to its own, whose
        // field the store truncates.
        const uint8_t computing = check_lower_computed(calculated_class);
        const uint8_t kind = increment ? subtract ? BUSTER_TYPED_SUB : BUSTER_TYPED_ADD : check_lower_binary(op);
        const TypeId through = half ? check_lower_update_through(writer, stored, calculated) : calculated;
        check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_BIT_STORE, .type = packed, .value = address, .discard = postfix || discard });
        check_lower_convert(writer, through, stored);
        if (half)
        {
            check_lower_update_classless(writer, node, kind, increment, calculated, through, stream->conversions[node].operands[1].type);
        }
        else
        {
            if (computing != calculated_class)
            {
                check_lower_instruction(writer, BUSTER_TYPED_CONVERT, calculated_class, 0);
            }
            check_lower_instruction(writer, kind, computing, 0);
            if (increment)
            {
                check_lower_instruction(writer, BUSTER_TYPED_CONST, computing, 1);
            }
            else
            {
                if (computing != calculated_class && kind != BUSTER_TYPED_SHL && kind != BUSTER_TYPED_SHR)
                {
                    check_lower_instruction(writer, BUSTER_TYPED_CONVERT, computing, 0);
                }
                check_lower_operand(writer, node, 1, false, stream->conversions[node].operands[1].type);
            }
            if (computing != calculated_class)
            {
                check_lower_instruction(writer, BUSTER_TYPED_CONVERT, computing, 0);
            }
        }
        check_lower_convert(writer, stored, through);
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

// Whether an overflow builtin's operand or destination is a _BitInt of 65 to 127 bits, which its recipes don't take: they
// take a 128-bit one whole (BUSTER_CBUILTIN_LOWER_WIDE) and narrower ones of up to 64 bits.
static bool check_lower_builtin_between(const TypeTable* types, TypeId type)
{
    uint32_t width = 0;
    bool sign = false;
    return check_action_integer_type(types, type, &width, &sign) && width > 64 && width < 128;
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
        else if (check_lower_builtin_between(program->types, conversions->operands[0].type) ||
                 check_lower_builtin_between(program->types, conversions->operands[1].type) || check_lower_builtin_between(program->types, conversions->store.type))
        {
            check_lower_unsupported(writer, "overflow builtins of _BitInt of 65 to 127 bits");
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
        // A 128-bit integer comes as a struct of two eightbytes does, and a vector as a struct of its bytes (a 16-byte one in
        // an XMM register's slot of the save area, a wider one in the overflow area), its value loaded from their address.
        const uint8_t requested_class = argument ? check_lower_class(writer, requested) : BUSTER_TYPED_CLASS_VOID;
        const bool bytes = check_lower_wide_class(requested_class) || requested_class == BUSTER_TYPED_CLASS_VECTOR;
        if (argument)
        {
            // A struct or union's bytes are copied from the save area into a frame object of its own (with room for
            // whole eightbytes), or read in the overflow area; its value is their address.
            const bool aggregate = check_lower_aggregate(writer, requested);
            const uint64_t size = aggregate || bytes ? type_size(types, requested) : 0;
            // A transparent union's as its first member's (check_lower_passed_as), into the union's object.
            call->plan = abi_va_arg(writer->abi_table, types, aggregate ? check_lower_passed_as(writer, requested) : requested);
            call->result_class = bytes ? BUSTER_TYPED_CLASS_U64 : check_lower_storage_class(writer, requested);
            // (An empty one, which the call ignores, still names its object: 1 byte, as its frame object takes.)
            call->size = aggregate && size == 0 ? 1 : (uint32_t)size;
            call->object = aggregate || bytes ? check_lower_object(writer, (size + 7) & ~UINT64_C(7), type_alignment_log2(types, requested)) : 0;
        }
        if (bytes)
        {
            check_lower_instruction(writer, BUSTER_TYPED_LOAD, requested_class, check_lower_access_value(types, requested_class, requested));
        }
        check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_VARARGS, .node = index, .type = bytes ? BUSTER_TYPE_ULONG : stream->values[node].type });
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
    // A half-precision va_arg's result is its bits, the recipe's U16 (check_lower_varargs_prepare), which is a U16's.
    check_lower_builtin_finish(writer, &buffer, result, check_lower_half_of(writer, task.type) != CHECK_HALF_NONE ? BUSTER_TYPE_USHORT : task.type);
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
    else if (count > 4 || stream->misc[selected].evaluated_operands != (1u << evaluated) - 1)
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
        if (cbuiltin_bits(call->id))
        {
            // A bit builtin's recipe (c_builtin_bits_lower.c) takes its arguments' and result's descriptors, of up to 128
            // bits (the checker refuses wider _BitInts), and a result past 64 bits' frame object, whose value it is.
            const TypeId result_type = stream->values[node].type;
            const uint32_t result = check_lower_builtin_type(program->types, result_type);
            bool described = cbuiltin_carry(call->id) || result != 0;
            for (uint32_t i = 0; i < evaluated; i += 1)
            {
                const bool pointer = cbuiltin_carry(call->id) && i == 3;
                const uint32_t type = pointer ? 0 : check_lower_builtin_type(program->types, stream->argument_conversions[first + i].type);
                described = described && (pointer || type != 0);
                call->argument_types[i] = type;
            }
            call->result_type = result;
            if (!described)
            {
                check_lower_fail(writer, CHECK_PROGRAM_INVALID);
            }
            else if (!cbuiltin_carry(call->id) && cbuiltin_bits_width(result) > 64)
            {
                call->result_object = check_lower_temporary(writer, check_lower_class(writer, result_type));
            }
        }
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
            // A bit builtin's 128-bit integer is a frame object of its own (check_lower_temporary's), whose limbs its
            // recipe loads.
            const TypeId type = stream->argument_conversions[first + i].type;
            if (check_lower_memory(writer, type) && !cbuiltin_bits(call->id))
            {
                check_lower_unsupported(writer, "builtins of long double and __int128 operands");
            }
            call->arguments[i] = check_lower_temporary(writer, check_lower_class(writer, type));
        }
        check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_BUILTIN_MISC, .node = index, .type = stream->values[node].type });
        for (uint32_t i = evaluated; i-- > 0 && writer->output->status == CHECK_PROGRAM_OK;)
        {
            // A frame object's value is its value class's (a 128-bit integer's), as SET_LOCAL writes it.
            const uint32_t local = call->arguments[i];
            check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, writer->value_classes[local], local);
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

// isfpclass of a vector: each lane's classification by the scalar recipe of the lane loaded into the argument local,
// stored into the result's frame object as an int (a float's lane) or a long long (a double's), 1 or 0, as clang's zero
// extension of llvm.is.fpclass's lanes is; then the result vector. Out of line, as few tasks are.
static __attribute__((noinline)) void check_lower_floating_lanes(CheckLowerWriter* writer, CheckLowerTask task)
{
    const CBuiltinFloatLowerCall* call = writer->floating + task.node;
    const uint8_t element = (uint8_t)call->argument_class;
    const uint8_t lane_class = call->lane_bytes == 8 ? BUSTER_TYPED_CLASS_I64 : BUSTER_TYPED_CLASS_I32;
    for (uint32_t lane = 0; lane < call->lanes && writer->output->status == CHECK_PROGRAM_OK; lane += 1)
    {
        const uint64_t offset = (uint64_t)lane * call->lane_bytes;
        check_lower_emit(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, call->result_local);
        check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, offset);
        check_lower_emit(writer, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, 1);
        check_lower_emit(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, call->vector_local);
        check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, offset);
        check_lower_emit(writer, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, 1);
        check_lower_emit(writer, BUSTER_TYPED_LOAD, element, 0);
        check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, element, call->argument_local);
        check_lower_floating_recipe(writer, call, BUSTER_TYPE_INT);
        if (lane_class == BUSTER_TYPED_CLASS_I64)
        {
            check_lower_emit(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_I64, 0);
        }
        check_lower_emit(writer, BUSTER_TYPED_SET, lane_class, 0);
    }
    check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_VECTOR, call->result_local);
}

// Always inlined into both copies of the tasks' loop (check_lower_drain_body).
static inline __attribute__((always_inline)) void check_lower_floating_emit(CheckLowerWriter* writer, CheckLowerTask task)
{
    const CBuiltinFloatLowerCall* call = writer->floating + task.node;
    if (call->lanes != 0)
    {
        check_lower_floating_lanes(writer, task);
    }
    else if (task.kind == CHECK_LOWER_BUILTIN_FLOAT_STRICT && (call->id == CBUILTIN_FPCLASSIFY || call->id == CBUILTIN_ISINF_SIGN))
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
        const bool classify = (id >= CBUILTIN_ISNAN && id <= CBUILTIN_ISINF_SIGN) || id == CBUILTIN_FABS || id == CBUILTIN_FABSF || id == CBUILTIN_FABSL ||
                              cbuiltin_float_class(id) || cbuiltin_float_signbit(id) || cbuiltin_float_canonicalize(id);
        const bool compare = id >= CBUILTIN_ISGREATER && id <= CBUILTIN_ISUNORDERED;
        const bool selectors = id == CBUILTIN_FPCLASSIFY;
        // isfpclass's mask (its second argument) is a constant the effect holds, never evaluated. Its first may be a vector.
        const uint32_t arguments = selectors ? 6u : id == CBUILTIN_ISFPCLASS ? 2u : 1u;
        const TypeId operand = id == CBUILTIN_ISFPCLASS && count == 2 ?
                               type_unqualified(type_strip_aligned(program->types, stream->values[graph->argument_nodes[graph->argument_offsets[node]]].type)) : 0;
        const uint8_t operand_kind = operand != 0 ? type_kind(program->types, operand) : BUSTER_TYPE_KIND_VOID;
        const bool vector = operand_kind == BUSTER_TYPE_KIND_VECTOR || operand_kind == BUSTER_TYPE_KIND_EXT_VECTOR;
        if (effect->unsupported != 0 || (classify && count != arguments) || (compare && count != 2))
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
        else if (classify && vector && check_lower_vector_of(writer, operand) == 0)
        {
            check_lower_unsupported(writer, "vector types of other than 4, 8, 16, 32 or 64 bytes");
        }
        else if (classify && vector)
        {
            const CheckVectorShape shape = check_vector_builtin_shape(program->types, operand);
            if (shape.element != 4 && shape.element != 8)
            {
                // A long double's 16-byte element takes a vector of __int128 results (clang's signed vector of its
                // elements' width), whose lanes these int and long long stores don't hold. Refuse, rather than write
                // each lane's int at an offset no element stride gives.
                check_lower_unsupported(writer, "isfpclass of a vector whose elements are not 4 or 8 bytes");
            }
            else
            {
                // isfpclass of a vector of floats or doubles: the vector stored into a frame object, then each lane
                // classified by the scalar recipe and stored into the result's frame object (check_lower_floating_lanes).
                const uint32_t index = writer->floating_count++;
                const uint8_t class = check_lower_class(writer, effect->floating_type);
                const uint32_t vector_local = check_lower_temporary_of(writer, operand);
                const bool fenv_access = graph->fp_environments != NULL && (graph->fp_environments[node] & BUSTER_CHECK_FP_ENV_ACCESS) != 0;
                writer->floating[index] = (CBuiltinFloatLowerCall){
                    .id = id,
                    .argument_local = check_lower_temporary(writer, class),
                    .argument_class = class,
                    .fenv_access = fenv_access,
                    .class_mask = effect->class_mask,
                    .vector_local = vector_local,
                    .result_local = check_lower_temporary_of(writer, stream->values[node].type),
                    .lanes = shape.lanes,
                    .lane_bytes = shape.element,
                };
                check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_BUILTIN_FLOAT, .node = index, .type = stream->values[node].type });
                check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_VECTOR, vector_local);
                check_lower_operand(writer, node, 3, false, operand);
                writer->vectors = true;
            }
        }
        else if (classify)
        {
            const uint32_t index = writer->floating_count++;
            const uint8_t class = check_lower_class(writer, effect->floating_type);
            // A long double's argument is a frame object of its own, which fabsl clears the sign of in place.
            const uint32_t argument = check_lower_temporary(writer, class);
            const bool fenv_access = graph->fp_environments != NULL && (graph->fp_environments[node] & BUSTER_CHECK_FP_ENV_ACCESS) != 0;
            writer->floating[index] = (CBuiltinFloatLowerCall){
                .id = id,
                .argument_local = argument,
                .argument_class = class,
                .fenv_access = fenv_access,
                .class_mask = effect->class_mask,
            };
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
        // Its block's cleanups run once its value is kept (CheckProgramRegion.cleanups); an aggregate's, whose value is its
        // bytes' address, in a frame object of its own first, which the cleanups can't write.
        const uint32_t cleanups = region->cleanups <= region->count ? region->cleanups : 0;
        for (uint32_t i = region->count; i-- > region->count - cleanups;)
        {
            check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_CONTROL, .node = region->first + i });
        }
        if (result != BUSTER_CHECK_NONE)
        {
            check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, class, result);
        }
        const TypeId converted = program->stream->conversions[task.node].operands[0].type;
        const bool copied = cleanups != 0 && result != BUSTER_CHECK_NONE && check_lower_aggregate(writer, converted);
        const uint32_t copy = copied ? check_lower_object(writer, type_size(program->types, converted), type_alignment_log2(program->types, converted)) :
                                       BUSTER_CHECK_NONE;
        if (copied)
        {
            check_lower_instruction(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, copy);
            check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_COPY, .value = type_size(program->types, converted), .address = false });
        }
        check_lower_operand(writer, task.node, 0, task.address, converted);
        if (copied)
        {
            check_lower_instruction(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, copy);
        }
        for (uint32_t i = region->count - cleanups; i-- > 0;)
        {
            check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_CONTROL, .node = region->first + i });
        }
    }
}

// An expression of a _BitInt, or a compare of narrow operands (a _BitInt's alone: check_lower_computed): an operation
// that may leave bits past its width computes in its computing class (check_lower_computed) from its operands converted
// to it, and is extended from its width after and converted back (check_lower_bit_int_extend), as is a compare's
// operands; anything else is pushed again, marked (CheckLowerTask.bit_int), for the ordinary expansion, after what
// extends a value from memory or a call again (whose bits past its width the convention leaves unspecified, as clang's
// loads read them). A folded constant extends from its width itself (check_lower_constant). It takes the expression
// task's fields rather than the task: given the task, clang copied every task whole as its expansion began, a 32-byte
// load that the push's narrower stores can't forward, and the lowering of sqlite3.c took 5% longer on the 9700X.
static __attribute__((noinline)) void check_lower_bit_int_node(CheckLowerWriter* writer, uint32_t node, bool address, bool initialized, bool discard)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const TypeTable* types = program->types;
    const CheckOp op = graph->ops[node];
    const CheckValue value = stream->values[node];
    const TypeId type_a = stream->conversions[node].operands[0].type;
    const TypeId type_b = stream->conversions[node].operands[1].type;
    const uint8_t value_kind = type_kind(types, type_strip_aligned(types, value.type));
    const bool bit_int = value_kind == BUSTER_TYPE_KIND_BIT_INT;
    const bool folded = value.constant_kind != CHECK_CONSTANT_NONE && value.fault == CHECK_FAULT_NONE &&
                        (value.flags & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_FP_ENVIRONMENT)) == 0 && value.category == CHECK_RVALUE;
    const uint8_t kind = op == CHECK_OP_NEGATIVE ? BUSTER_TYPED_NEG : op == CHECK_OP_BIT_NOT ? BUSTER_TYPED_NOT : check_lower_binary(op);
    const bool compare = kind >= BUSTER_TYPED_EQ && kind <= BUSTER_TYPED_GE;
    const bool unary = op == CHECK_OP_NEGATIVE || op == CHECK_OP_BIT_NOT;
    const bool binary = (op >= CHECK_OP_ADD && op <= CHECK_OP_SHIFT_RIGHT) || (op >= CHECK_OP_BIT_AND && op <= CHECK_OP_BIT_XOR);
    writer->token = graph->tokens[node];
    // A _BitInt's past 128 bits has no class: its operations go to check_lower_limb_compute (check_lower_complex_operation).
    if (!folded && !address && (compare || (bit_int && (unary || binary))) && !check_lower_aggregate(writer, compare ? type_a : value.type))
    {
        // The operands' class: a compare's its operands', an operation's its value's.
        const uint8_t operands = check_lower_class(writer, compare ? type_a : value.type);
        const uint8_t computing = check_lower_computed(operands);
        const bool shift = kind == BUSTER_TYPED_SHL || kind == BUSTER_TYPED_SHR;
        if (!compare)
        {
            check_lower_bit_int_extend(writer, value.type, true, true);
        }
        check_lower_operation(writer, kind, compare ? BUSTER_TYPED_CLASS_I32 : computing);
        if (!unary)
        {
            check_lower_count(writer, kind, type_b);
            if (computing != operands && !shift)
            {
                check_lower_instruction(writer, BUSTER_TYPED_CONVERT, computing, 0);
            }
            check_lower_operand(writer, node, 1, false, type_b);
        }
        if (computing != operands)
        {
            check_lower_instruction(writer, BUSTER_TYPED_CONVERT, computing, 0);
        }
        check_lower_operand(writer, node, 0, false, type_a);
    }
    else
    {
        const bool aggregate = check_lower_aggregate(writer, value.type);
        const bool compound = op == CHECK_OP_VALUE && !initialized && value.object != 0 && value.object < program->object_count &&
                              program->input->compound_objects[value.object - 1] && !program->input->static_objects[value.object - 1];
        const bool loaded = op == CHECK_OP_VALUE || op == CHECK_OP_DEREFERENCE || op == CHECK_OP_MEMBER || op == CHECK_OP_ARROW ||
                            op == CHECK_OP_SUBSCRIPT || op == CHECK_OP_CALL || op == CHECK_OP_VARARGS || op == CHECK_OP_BUILTIN_BIT_CAST;
        if (bit_int && loaded && !folded && !address && !discard && !aggregate && !compound)
        {
            check_lower_bit_int_extend(writer, value.type, false, true);
        }
        check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_EXPRESSION, .node = node, .address = address, .initialized = initialized,
                                                   .discard = discard, .bit_int = true });
    }
}

// A conditional's arm, converted to its type `type`: a vector's conditional (`vector`) splats a scalar arm, as a
// vector operator splats a scalar operand (check_lower_vector_operand).
static inline __attribute__((always_inline)) void check_lower_logical_arm(CheckLowerWriter* writer, uint32_t node, uint32_t arm, TypeId type, bool vector)
{
    if (vector)
    {
        check_lower_vector_operand(writer, node, arm, type);
    }
    else
    {
        check_lower_operand(writer, node, arm, false, type);
    }
}

// A logical operator or a conditional, of class `class` and value `value` (check_lower_expand's), and a vector's
// conditional (check_lower_vector_operation's), with `vector` set, which splats a scalar arm (check_lower_logical_arm):
// check_lower_expand's copy, with `vector` false, has no test of it.
static inline __attribute__((always_inline)) void check_lower_logical(CheckLowerWriter* writer, const CheckProgram* program, uint32_t node, CheckOp op, uint32_t a,
                                                                      uint32_t b, TypeId type_a, TypeId type_b, const CheckValue* value, uint8_t class, bool vector)
{
    const bool reuse = op == CHECK_OP_CONDITIONAL && a == b;
    bool truth = false;
    if (check_program_runtime_truth(program, node, a, &truth))
    {
        if (op == CHECK_OP_CONDITIONAL)
        {
            check_lower_logical_arm(writer, node, truth ? 1 : 2, value->type, vector);
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
            check_lower_logical_arm(writer, node, 2, value->type, vector);
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
                if (vector && check_lower_vector_bytes(program->types, type_a) == 0)
                {
                    check_lower_vector_splat(writer, type_a, value->type);
                }
                else
                {
                    check_lower_convert(writer, type_a, value->type);
                }
                check_lower_instruction(writer, BUSTER_TYPED_LOCAL, check_lower_class(writer, type_a), retained);
            }
            else
            {
                check_lower_logical_arm(writer, node, 1, value->type, vector);
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

// An operator of check_lower_expand's whose class is VECTOR or past it, out of line, which only such an operator reaches.
// A vector operator (#136): its operands, converted to the operator's vector type (a scalar's splat,
// check_lower_vector_operand), and an X86 entry of its row. A compare's row is its operands' type's, whose result is its
// mask's -1 or 0 elements; a shift's count, when it is a scalar, is splat at the shifted vector's type. A vector's
// conditional is check_lower_logical's, whose scalar arm splats so too. A half-precision type's arithmetic is
// c_check_program_half.c's, and its other operators and a type of no class's are check_lower_expand's as it would lower
// them, whose entries of the class check_lower_emit writes or refuses (a conditional of either is check_lower_expand's
// own, check_lower_logical).
static __attribute__((noinline)) void check_lower_vector_operation(CheckLowerWriter* writer, uint32_t node)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const TypeTable* types = program->types;
    const CheckOp op = graph->ops[node];
    const uint32_t a = graph->operand0[node];
    const uint32_t b = graph->operand1[node];
    const uint8_t class = check_lower_class(writer, stream->values[node].type);
    const CheckHalf half = check_lower_class_half(class);
    if (class != BUSTER_TYPED_CLASS_VECTOR)
    {
        const TypeId type_a = stream->conversions[node].operands[0].type;
        const TypeId type_b = stream->conversions[node].operands[1].type;
        if (half != CHECK_HALF_NONE && (op == CHECK_OP_NEGATIVE || op == CHECK_OP_ADD || op == CHECK_OP_SUBTRACT || op == CHECK_OP_MULTIPLY || op == CHECK_OP_DIVIDE))
        {
            check_lower_half_expression(writer, node, half);
        }
        else if (op == CHECK_OP_NEGATIVE || op == CHECK_OP_BIT_NOT)
        {
            check_lower_instruction(writer, op == CHECK_OP_NEGATIVE ? BUSTER_TYPED_NEG : BUSTER_TYPED_NOT, class, 0);
            check_lower_operand(writer, node, 0, false, type_a);
        }
        else
        {
            check_lower_operation(writer, check_lower_binary(op), class);
            check_lower_count(writer, check_lower_binary(op), type_b);
            check_lower_operand(writer, node, 1, false, type_b);
            check_lower_operand(writer, node, 0, false, type_a);
        }
    }
    else if (op == CHECK_OP_CONDITIONAL)
    {
        check_lower_logical(writer, program, node, op, a, b, stream->conversions[node].operands[0].type, stream->conversions[node].operands[1].type,
                            stream->values + node, BUSTER_TYPED_CLASS_VECTOR, true);
    }
    else if (op == CHECK_OP_NEGATIVE || op == CHECK_OP_BIT_NOT)
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
        check_lower_vector_operand(writer, node, 1, scalar_count ? vector : stream->conversions[node].operands[1].type);
        check_lower_vector_operand(writer, node, 0, vector);
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

// A type's in an X86 row, and its class: a half-precision type's bits (its U16), an unsigned short's, as a row moves them
// to and from its registers (a reduction's start and result), and any other type itself.
static TypeId check_lower_x86_bits_type(const CheckLowerWriter* writer, TypeId type)
{
    return check_lower_class_half(check_lower_value_class(writer, type)) != CHECK_HALF_NONE ? BUSTER_TYPE_USHORT : type;
}

static uint8_t check_lower_x86_bits_class(const CheckLowerWriter* writer, TypeId type)
{
    return check_lower_value_class(writer, check_lower_x86_bits_type(writer, type));
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
// the last argument, to 16 bytes, the second of those and its floating start, the first, where it has one. A
// half-precision argument and result are their bits (c_check_program_half.c's U16), which a row reads and writes in a
// general register: an unsigned short's, widened, and an unsigned int's, narrowed.
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
    // A MASK_UNARY's or MASK_MERGE's mask, its last argument but a rounding control and an immediate (which follows the
    // mask of the scalar range, reduce and rndscale builtins), where it is a constant.
    uint64_t mask = 0;
    if (rule != NULL && (rule->family == CHECK_X86_LOWER_MASK_UNARY || rule->family == CHECK_X86_LOWER_MASK_MERGE) && count >= 3)
    {
        const uint32_t last = count - 1 - (rounded != BUSTER_CHECK_X86_NO_IMMEDIATE ? 1u : 0u);
        const uint32_t a = last == skipped ? last - 1 : last;
        CheckValue value = stream->values[graph->argument_nodes[first + a]];
        check_action_convert_value(program->types, &value, stream->argument_conversions[first + a].type, node);
        mask = value.constant_kind == CHECK_CONSTANT_INTEGER ? (uint64_t)value.integer : 0;
    }
    // Clang constrains the whole function when any block has FENV_ACCESS. Share the existing once-per-body scan
    // used by half-precision lowering, and ask only for packed float-compare rows, leaving other builtins unchanged.
    const bool packed = rule != NULL && rule->fixed == CHECK_X86_PREDICATE_FCMP &&
                        (rule->family == CHECK_X86_LOWER_COMPARE || rule->family == CHECK_X86_LOWER_BINARY_IMMEDIATE);
    const bool strict = packed && check_lower_half_strict(writer);
    Tier0Operation row;
    const char* reason = info == NULL ? "this x86 builtin" :
                         !constants ? "x86 builtins of an immediate that isn't a constant" :
                         check_x86_lower_row_of(&writer->target, rule, (uint64_t)immediates[0].integer,
                                                rounded != BUSTER_CHECK_X86_NO_IMMEDIATE ? (uint64_t)immediates[1].integer : 4, mask, strict, &row);
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
        const uint8_t class = check_lower_x86_bits_class(writer, stream->argument_conversions[first + a].type);
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
        const TypeId result = check_lower_x86_bits_type(writer, stream->values[node].type);
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
                const uint8_t class = check_lower_x86_bits_class(writer, type);
                const uint8_t widened = check_lower_x86_operand_class(class);
                if (widened != class)
                {
                    check_lower_convert(writer, check_lower_x86_bits_type(writer, type), check_lower_class_type(widened));
                }
                check_lower_operand(writer, node, a + 3, false, type);
            }
        }
    }
}

#include "c_check_program_fortify.c"

// A tile builtin's private scalar/address ABI (c_builtin_amx_helpers.c). Its C tile operands are already addresses;
// a nonvoid result is written into this call's own frame object, passed as the final raw pointer argument. Keeping
// the public type's ABI separate here preserves the 1024-byte by-value stack argument and hidden-result convention.
static void check_lower_amx_helper(CheckLowerWriter* writer, uint32_t node, const CBuiltinX86HelperInfo* info)
{
    const CheckGraph* graph = &writer->program->input->graph;
    const CheckStream* stream = writer->program->stream;
    const CheckProgramHelpers* helpers = &writer->output->helpers;
    const uint32_t selected = check_program_builtin_index(helpers->uses, helpers->use_count, sizeof(CheckProgramHelperUse), node);
    const uint32_t count = graph->argument_counts[node];
    const uint32_t first = graph->argument_offsets[node];
    const bool output = info->output_bytes != 0;
    const uint32_t parameters = count + output;
    const TypeId result = stream->values[node].type;
    CheckLowerAbi* abi = writer->abi;
    if (selected == BUSTER_CHECK_NONE || helpers->uses[selected].helper >= helpers->count || parameters != info->parameter_count)
    {
        check_lower_fail(writer, CHECK_PROGRAM_INVALID);
    }
    else if (abi->count >= abi->capacity || parameters > abi->argument_capacity - abi->argument_count)
    {
        check_lower_fail(writer, CHECK_PROGRAM_RESOURCE);
    }
    else
    {
        const CheckProgramHelper* helper = helpers->items + helpers->uses[selected].helper;
        const uint32_t object = !output ? BUSTER_CHECK_NONE : info->output_bytes == 64 ? check_lower_temporary_of(writer, result) :
                                check_lower_object(writer, info->output_bytes, type_alignment_log2(writer->program->types, result));
        const uint32_t call = writer->call_count++;
        writer->call_targets[call] = helper->symbol;
        writer->call_first[call] = writer->argument_count;
        writer->call_counts[call] = parameters;
        writer->call_variadic[call] = 0;
        writer->call_result_objects[call] = 0;
        abi->first[abi->count] = abi->argument_count;
        abi->results[abi->count] = BUSTER_TYPE_VOID;
        abi->counts[abi->count] = parameters;
        abi->named[abi->count] = parameters;
        abi->variadic[abi->count++] = 0;
        for (uint32_t a = 0; a < parameters; a += 1)
        {
            abi->arguments[abi->argument_count++] = BUSTER_TYPE_ULONG;
            writer->argument_sizes[writer->argument_count] = 0;
            writer->argument_classes[writer->argument_count++] = BUSTER_TYPED_CLASS_U64;
        }
        if (output)
        {
            check_lower_instruction(writer, info->output_bytes == 64 ? BUSTER_TYPED_LOCAL : BUSTER_TYPED_LOCAL_ADDRESS,
                                    info->output_bytes == 64 ? BUSTER_TYPED_CLASS_VECTOR : BUSTER_TYPED_CLASS_U64, object);
        }
        check_lower_instruction(writer, BUSTER_TYPED_CALL, BUSTER_TYPED_CLASS_VOID, call);
        // Tasks run in reverse order. The final argument is our output object's address, and preceding arguments
        // evaluate once each using the checker's conversions. Only tile aggregates skip scalar conversion.
        if (output)
        {
            check_lower_instruction(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, object);
        }
        for (uint32_t a = count; a-- > 0;)
        {
            const TypeId type = stream->argument_conversions[first + a].type;
            if (!check_lower_tile_type(writer->program->types, type))
            {
                check_lower_convert(writer, type, BUSTER_TYPE_ULONG);
            }
            check_lower_operand(writer, node, a + 3, false, type);
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
    const CBuiltinFortifyId fortify = info != NULL ? cbuiltin_fortify_id(info->name, info->name_length) : CBUILTIN_FORTIFY_COUNT;
    const bool candidate = info != NULL && cbuiltin_library_candidate(info->name, info->name_length);
    const CBuiltinLibraryId library = candidate ? cbuiltin_library_id(info->name, info->name_length) : CBUILTIN_LIBRARY_COUNT;
    const uint32_t libgcc = candidate ? cbuiltin_library_libgcc(info->name, info->name_length) : BUSTER_CBUILTIN_LIBRARY_LIBGCC;
    const char* refusal = candidate ? cbuiltin_library_refusal(info->name, info->name_length) : NULL;
    if (fortify != CBUILTIN_FORTIFY_COUNT)
    {
        check_lower_fortify(writer, node, fortify);
    }
    else if (library != CBUILTIN_LIBRARY_COUNT)
    {
        // A library builtin's call is a call of its function, by its prototype (#148, c_builtin_library.c).
        check_lower_call_to(writer, node, check_lower_library_runtime(library), 0, 0);
    }
    else if (libgcc != BUSTER_CBUILTIN_LIBRARY_LIBGCC)
    {
        check_lower_call_to(writer, node, (CheckLowerRuntime)(CHECK_LOWER_RUNTIME_POWIDF2 + libgcc), 0, 0);
    }
    else if (refusal != NULL)
    {
        check_lower_unsupported(writer, refusal);
    }
    else if (check_program_builtin_index(helpers->uses, helpers->use_count, sizeof(CheckProgramHelperUse), node) != BUSTER_CHECK_NONE)
    {
        // The leaf's instructions' features and extensions (c_builtin_x86_helpers.c), which the target must have.
        const CBuiltinX86HelperKind leaf = info != NULL ? cbuiltin_x86_helper_kind(info->name, info->name_length) : CBUILTIN_X86_HELPER_NONE;
        const uint64_t needed = cbuiltin_x86_helper_infos[leaf].features;
        const uint64_t extensions = cbuiltin_x86_helper_infos[leaf].extensions;
        if ((writer->target.features & needed) != needed || (writer->target.extensions & extensions) != extensions)
        {
            check_lower_unsupported(writer, cbuiltin_x86_helper_infos[leaf].refusal);
        }
        else if (cbuiltin_x86_helper_infos[leaf].amx != 0)
        {
            check_lower_amx_helper(writer, node, cbuiltin_x86_helper_infos + leaf);
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

// The arguments of a fused multiply-add of bf16 vectors (the call's three, from `first`) that are negations of bf16
// vectors of `vector`'s bytes, a bit each (check_vector_elementwise_row's `negated`): their negations fold into the row's
// form, as clang's instruction selection folds them, where the negation alone would round through float under excess
// precision (check_vector_lower_brain_rows).
static uint32_t check_lower_brain_negations(const CheckProgram* program, uint32_t first, TypeId vector)
{
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const TypeTable* types = program->types;
    const uint32_t bytes = check_lower_vector_bytes(types, vector);
    uint32_t negated = 0;
    for (uint32_t a = 0; a < 3; a += 1)
    {
        const uint32_t argument = graph->argument_nodes[first + a];
        const bool negation = argument < stream->count && graph->ops[argument] == CHECK_OP_NEGATIVE;
        const TypeId operand = negation && graph->operand0[argument] < stream->count ? stream->values[graph->operand0[argument]].type : 0;
        const bool folds = negation && operand != 0 && check_vector_lower_brain(types, stream->values[argument].type) && check_vector_lower_brain(types, operand) &&
                           check_lower_vector_bytes(types, operand) == bytes;
        negated |= folds ? 1u << a : 0;
    }
    return negated;
}

// The C library's function of a floating elementwise builtin clang -O0's code calls (BUSTER_CHECK_LOWER_LIBM's: acos ...
// tanh, then ceil ... round, then atan2, fmod and pow, in the catalog's order of their builtins), of a value of `class`:
// F32's the float one (sinf), F64's the double one (sin), F80's the long double one (sinl).
static CheckLowerRuntime check_lower_libm_function(CBuiltinId builtin, uint8_t class)
{
    const uint32_t index = builtin <= CBUILTIN_ELEMENTWISE_TANH ? (uint32_t)(builtin - CBUILTIN_ELEMENTWISE_ACOS) :
                           builtin <= CBUILTIN_ELEMENTWISE_ROUND ? 15 + (uint32_t)(builtin - CBUILTIN_ELEMENTWISE_CEIL) :
                                                                   22 + (uint32_t)(builtin - CBUILTIN_ELEMENTWISE_ATAN2);
    const uint32_t kind = class == BUSTER_TYPED_CLASS_F32 ? 0 : class == BUSTER_TYPED_CLASS_F64 ? 1 : 2;
    return (CheckLowerRuntime)(BUSTER_CHECK_LOWER_LIBM_FIRST + 3 * index + kind);
}

// Whether clang -O0's code computes a floating elementwise builtin by the C library's function for each element:
// acos ... tanh, atan2, fmod (its frem) and pow, whatever the target.
static bool check_lower_libm_builtin(CBuiltinId builtin)
{
    return (builtin >= CBUILTIN_ELEMENTWISE_ACOS && builtin <= CBUILTIN_ELEMENTWISE_TANH) || (builtin >= CBUILTIN_ELEMENTWISE_ATAN2 && builtin <= CBUILTIN_ELEMENTWISE_POW);
}

// Whether node `node`, a call of `builtin`, is __builtin_elementwise_round under FENV_ACCESS (#934's review): clang
// computes it by the constrained round, the C library's round for each element, since the rows' expansion
// (check_vector_floating_row's) adds the half below 0.5 in MXCSR's rounding mode, which a caller may have changed (0.5f
// rounds to 0 under FE_DOWNWARD), and raises inexact; ceil, floor, trunc and roundeven fix their rounding in their
// instructions, and rint and nearbyint take MXCSR's, as their functions do.
static bool check_lower_strict_round(const CheckGraph* graph, uint32_t node, CBuiltinId builtin)
{
    return builtin == CBUILTIN_ELEMENTWISE_ROUND && graph->fp_environments != NULL && (graph->fp_environments[node] & BUSTER_CHECK_FP_ENV_ACCESS) != 0;
}

// Pushes the instructions of an address `offset` bytes into frame object `object`, which run in the reverse of their
// pushes: LOCAL_ADDRESS, then the offset's PTR_ADD.
static void check_lower_element_address(CheckLowerWriter* writer, uint32_t object, uint32_t offset)
{
    if (offset != 0)
    {
        check_lower_instruction(writer, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, 1);
        check_lower_instruction(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, offset);
    }
    check_lower_instruction(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, object);
}

// Pushes a row's constant operands (c_check_program_vector_builtins.c's CheckVectorConstants), the row's operands from
// `first` on, the last first: a VECTOR one's pattern repeated through its bytes, a floating one's bits.
static void check_lower_row_constants(CheckLowerWriter* writer, const Tier0Operation* row, const CheckVectorConstants* constants, uint32_t first)
{
    for (uint32_t c = constants->count; c-- > 0;)
    {
        const uint32_t operand = first + c;
        const uint8_t class = row->operand_classes[operand];
        if (class == BUSTER_TYPED_CLASS_VECTOR)
        {
            uint64_t words[8] = { 0 };
            uint8_t* bytes = (uint8_t*)words;
            const uint32_t count = row->operand_bytes[operand];
            for (uint32_t b = 0; b < count && b < 64; b += 1)
            {
                bytes[b] = constants->patterns[c][b % constants->bytes[c]];
            }
            check_lower_instruction(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_VECTOR, check_lower_vector_constant(writer, words, count));
        }
        else
        {
            uint64_t bits = 0;
            memcpy(&bits, constants->patterns[c], 8);
            check_lower_instruction(writer, BUSTER_TYPED_CONST, class, bits);
        }
    }
}

// A floating elementwise builtin as the C library's function's calls (check_lower_libm_function), clang -O0's: of a
// scalar (a float, a double or a long double), one call; of a vector of floats or doubles, a call an element, in element
// order, each element read from a frame object the vector is stored into (one per operand) and each result written into
// another, the result vector. clang's code extracts and inserts the elements in registers instead; the calls, their
// operands and their results are the same, but not their order, which is clang's instruction scheduler's and changes
// with the code around them (LLVM's llvm.log and the rest don't model errno): so errno after a vector's calls is
// unspecified. Half-precision and bf16 elements, which clang's code converts to floats and back around the calls, are
// refused by name.
static void check_lower_vector_calls(CheckLowerWriter* writer, uint32_t node, CBuiltinId builtin, uint32_t arguments)
{
    const CheckProgram* program = writer->program;
    const CheckStream* stream = program->stream;
    const TypeTable* types = program->types;
    const TypeId result = stream->values[node].type;
    const uint32_t first = program->input->graph.argument_offsets[node];
    const uint32_t bytes = check_lower_vector_of(writer, result);
    const CheckVectorShape shape = check_vector_builtin_shape(types, result);
    const uint8_t class = check_lower_class(writer, result);
    if (bytes != 0 && (!shape.floating || shape.bfloat || shape.element == 2))
    {
        check_lower_unsupported(writer, "elementwise vector builtins of half-precision or bf16 elements");
    }
    else if (bytes == 0 && class != BUSTER_TYPED_CLASS_F32 && class != BUSTER_TYPED_CLASS_F64 && class != BUSTER_TYPED_CLASS_F80)
    {
        check_lower_unsupported(writer, "elementwise builtins of half-precision, bf16 or __float128 values");
    }
    else if (bytes != 0)
    {
        const bool d = shape.element == 8;
        const uint8_t element_class = d ? BUSTER_TYPED_CLASS_F64 : BUSTER_TYPED_CLASS_F32;
        const TypeId element = d ? BUSTER_TYPE_DOUBLE : BUSTER_TYPE_FLOAT;
        const TypeId parameters[2] = { element, element };
        const CheckLowerRuntime runtime = check_lower_libm_function(builtin, element_class);
        uint32_t operands[2] = { 0, 0 };
        for (uint32_t a = 0; a < arguments && a < 2; a += 1)
        {
            operands[a] = check_lower_temporary_of(writer, stream->argument_conversions[first + a].type);
        }
        const uint32_t out = check_lower_temporary_of(writer, result);
        check_lower_instruction(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_VECTOR, out);
        // Pushed in the reverse of the calls' order: the last element's first.
        for (uint32_t i = shape.lanes; i-- > 0 && writer->output->status == CHECK_PROGRAM_OK;)
        {
            const uint32_t call = check_lower_runtime_call(writer, runtime, element, parameters, arguments, BUSTER_CHECK_NONE);
            check_lower_instruction(writer, BUSTER_TYPED_SET, element_class, 0);
            check_lower_instruction(writer, BUSTER_TYPED_CALL, element_class, call);
            for (uint32_t a = arguments; a-- > 0;)
            {
                check_lower_instruction(writer, BUSTER_TYPED_LOAD, element_class, 0);
                check_lower_element_address(writer, operands[a], i * shape.element);
            }
            check_lower_element_address(writer, out, i * shape.element);
        }
        for (uint32_t a = arguments; a-- > 0;)
        {
            check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_VECTOR, operands[a]);
            check_lower_operand(writer, node, a + 3, false, stream->argument_conversions[first + a].type);
        }
        writer->vectors = true;
    }
    else
    {
        const TypeId parameters[2] = { result, result };
        const uint32_t call = check_lower_runtime_call(writer, check_lower_libm_function(builtin, class), result, parameters, arguments, BUSTER_CHECK_NONE);
        check_lower_instruction(writer, BUSTER_TYPED_CALL, class, call);
        for (uint32_t a = arguments; a-- > 0;)
        {
            check_lower_operand(writer, node, a + 3, false, stream->argument_conversions[first + a].type);
        }
    }
}

// __builtin_reduce_in_order_fadd(v, start) of a vector of floats or doubles: ((start + v[0]) + v[1]) + ... in order, as
// clang -O0's code adds them, its elements read from a frame object the vector is stored into: the typed stream's ADD
// of the sum and each element, the sum first, which the start is converted to the element type for.
static void check_lower_reduce_in_order(CheckLowerWriter* writer, uint32_t node)
{
    const CheckProgram* program = writer->program;
    const CheckStream* stream = program->stream;
    const uint32_t first = program->input->graph.argument_offsets[node];
    const TypeId source = stream->argument_conversions[first].type;
    const CheckVectorShape shape = check_vector_builtin_shape(program->types, source);
    if (check_lower_vector_of(writer, source) == 0)
    {
        check_lower_unsupported(writer, "vector types of other than 4, 8, 16, 32 or 64 bytes");
    }
    else if (!shape.floating || shape.bfloat || shape.element == 2)
    {
        check_lower_unsupported(writer, "vector reductions of half-precision or bf16 elements");
    }
    else
    {
        const uint8_t class = shape.element == 8 ? BUSTER_TYPED_CLASS_F64 : BUSTER_TYPED_CLASS_F32;
        const uint32_t vector = check_lower_temporary_of(writer, source);
        for (uint32_t i = shape.lanes; i-- > 0;)
        {
            check_lower_instruction(writer, BUSTER_TYPED_ADD, class, 0);
            check_lower_instruction(writer, BUSTER_TYPED_LOAD, class, 0);
            check_lower_element_address(writer, vector, i * shape.element);
        }
        check_lower_operand(writer, node, 4, false, stream->argument_conversions[first + 1].type);
        check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_VECTOR, vector);
        check_lower_operand(writer, node, 3, false, source);
        writer->vectors = true;
    }
}

// __builtin_elementwise_pdep and _pext of vectors of integers: each element's BMI2 PDEP or PEXT (no vector instruction
// deposits or extracts bits), of its zero extension to 32 bits for 8- and 16-bit elements (the mask's extension has no
// bits past theirs, so neither has the result), the elements read from frame objects the vectors are stored into and
// each result, truncated to its element, written into another, the result vector.
static void check_lower_vector_deposit(CheckLowerWriter* writer, uint32_t node, CBuiltinId builtin)
{
    const CheckProgram* program = writer->program;
    const CheckStream* stream = program->stream;
    const TypeId result = stream->values[node].type;
    const uint32_t first = program->input->graph.argument_offsets[node];
    const CheckVectorShape shape = check_vector_builtin_shape(program->types, result);
    if (check_lower_vector_of(writer, result) == 0)
    {
        check_lower_unsupported(writer, check_lower_class(writer, result) == BUSTER_TYPED_CLASS_VECTOR ? "vector types of other than 4, 8, 16, 32 or 64 bytes" :
                                                                                                        "elementwise builtins of scalars");
    }
    else if (!shape.integer || shape.element == 0)
    {
        check_lower_unsupported(writer, "elementwise vector builtins of elements of this size");
    }
    else if ((writer->target.features & BUSTER_X86_FEATURE(BMI2)) == 0)
    {
        check_lower_unsupported(writer, "vector bit deposits and extractions without BMI2");
    }
    else
    {
        const bool wide = shape.element == 8;
        const uint8_t computed = wide ? BUSTER_TYPED_CLASS_U64 : BUSTER_TYPED_CLASS_U32;
        const uint8_t stored = shape.element == 1 ? BUSTER_TYPED_CLASS_U8 : shape.element == 2 ? BUSTER_TYPED_CLASS_U16 : computed;
        const bool deposit = builtin == CBUILTIN_ELEMENTWISE_PDEP;
        Tier0Operation row;
        memset(&row, 0, sizeof(row));
        row.records[0] = check_vector_lower_record(deposit ? (wide ? X86_FORM_PDEP_R64_R64_RM64 : X86_FORM_PDEP_R32_R32_RM32) :
                                                             (wide ? X86_FORM_PEXT_R64_R64_RM64 : X86_FORM_PEXT_R32_R32_RM32),
                                                   BUSTER_TIER0_ROLE_INTEGER_RESULT, BUSTER_TIER0_ROLE_INTEGER(0), BUSTER_TIER0_ROLE_INTEGER(1));
        row.record_count = 1;
        row.operand_count = 2;
        row.operand_classes[0] = computed;
        row.operand_classes[1] = computed;
        row.result_class = computed;
        const uint32_t operation = check_lower_operation_index(writer, &row);
        const uint32_t operands[2] = { check_lower_temporary_of(writer, stream->argument_conversions[first].type),
                                       check_lower_temporary_of(writer, stream->argument_conversions[first + 1].type) };
        const uint32_t out = check_lower_temporary_of(writer, result);
        check_lower_instruction(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_VECTOR, out);
        for (uint32_t i = shape.lanes; i-- > 0;)
        {
            check_lower_instruction(writer, BUSTER_TYPED_SET, stored, 0);
            if (stored != computed)
            {
                check_lower_instruction(writer, BUSTER_TYPED_CONVERT, stored, 0);
            }
            check_lower_instruction(writer, BUSTER_TYPED_X86, computed, operation);
            for (uint32_t a = 2; a-- > 0;)
            {
                if (stored != computed)
                {
                    check_lower_instruction(writer, BUSTER_TYPED_CONVERT, computed, 0);
                }
                check_lower_instruction(writer, BUSTER_TYPED_LOAD, stored, 0);
                check_lower_element_address(writer, operands[a], i * shape.element);
            }
            check_lower_element_address(writer, out, i * shape.element);
        }
        for (uint32_t a = 2; a-- > 0;)
        {
            check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_VECTOR, operands[a]);
            check_lower_operand(writer, node, a + 3, false, stream->argument_conversions[first + a].type);
        }
        writer->vectors = true;
    }
}

// The floating elementwise builtins clang -O0's code computes by instructions (check_vector_floating_instructions) and
// __builtin_elementwise_ldexp: of vectors of floats or doubles, check_vector_floating_row's or check_vector_ldexp_row's
// row, its constants after the arguments; of float or double scalars, check_vector_floating_scalar_row's, ldexp's
// exponent first converted from its promoted type, taken as the signed integer of its width as llvm.ldexp takes it, to
// the value's type; of long doubles, the C library's function clang's code calls (floorl ... roundl, fmaxl for maxnum,
// fminl for minnum and ldexpl, of an int's exponent), and the others, which clang's code computes with x87 instructions,
// refused by name.
static void check_lower_vector_floating(CheckLowerWriter* writer, uint32_t node, CBuiltinId builtin, uint32_t arguments)
{
    const CheckProgram* program = writer->program;
    const CheckStream* stream = program->stream;
    const TypeTable* types = program->types;
    const TypeId result = stream->values[node].type;
    const uint32_t first = program->input->graph.argument_offsets[node];
    const uint32_t bytes = check_lower_vector_of(writer, result);
    const uint8_t class = check_lower_class(writer, result);
    const bool ldexp = builtin == CBUILTIN_ELEMENTWISE_LDEXP;
    const TypeId exponent = ldexp ? stream->argument_conversions[first + 1].type : 0;
    Tier0Operation row;
    CheckVectorConstants constants = { 0 };
    const char* reason = NULL;
    if (bytes != 0)
    {
        reason = ldexp && check_lower_vector_of(writer, exponent) == 0 ? "vector types of other than 4, 8, 16, 32 or 64 bytes" :
                 ldexp ? check_vector_ldexp_row(types, &writer->target, result, exponent, &row) :
                         check_vector_floating_row(types, builtin, result, &row, &constants);
        if (reason == NULL)
        {
            check_lower_instruction(writer, BUSTER_TYPED_X86, BUSTER_TYPED_CLASS_VECTOR, check_lower_operation_index(writer, &row));
            check_lower_row_constants(writer, &row, &constants, arguments);
            for (uint32_t a = arguments; a-- > 0;)
            {
                check_lower_operand(writer, node, a + 3, false, stream->argument_conversions[first + a].type);
            }
            writer->vectors = true;
        }
    }
    else if (class == BUSTER_TYPED_CLASS_F32 || class == BUSTER_TYPED_CLASS_F64)
    {
        // An exponent wider than 64 bits (an __int128's, a _BitInt's) has no instruction's conversion.
        const uint64_t width = ldexp ? type_size(types, exponent) : 0;
        reason = ldexp && width != 4 && width != 8 ? "ldexp of exponents wider than 64 bits" : check_vector_floating_scalar_row(&writer->target, builtin, class, &row, &constants);
        if (reason == NULL)
        {
            check_lower_instruction(writer, BUSTER_TYPED_X86, class, check_lower_operation_index(writer, &row));
            check_lower_row_constants(writer, &row, &constants, arguments);
            if (ldexp)
            {
                const TypeId integer = width == 8 ? BUSTER_TYPE_LONG : BUSTER_TYPE_INT;
                check_lower_convert(writer, integer, result);
                check_lower_convert(writer, exponent, integer);
            }
            for (uint32_t a = arguments; a-- > 0;)
            {
                check_lower_operand(writer, node, a + 3, false, stream->argument_conversions[first + a].type);
            }
            writer->vectors = true;
        }
    }
    else if (class == BUSTER_TYPED_CLASS_F80 && (builtin <= CBUILTIN_ELEMENTWISE_ROUND || builtin == CBUILTIN_ELEMENTWISE_MAXNUM || builtin == CBUILTIN_ELEMENTWISE_MINNUM ||
                                                 (ldexp && type_size(types, exponent) == 4)))
    {
        const CheckLowerRuntime runtime = builtin <= CBUILTIN_ELEMENTWISE_ROUND ? check_lower_libm_function(builtin, class) :
                                          builtin == CBUILTIN_ELEMENTWISE_MAXNUM ? CHECK_LOWER_RUNTIME_FMAXL :
                                          builtin == CBUILTIN_ELEMENTWISE_MINNUM ? CHECK_LOWER_RUNTIME_FMINL :
                                                                                   CHECK_LOWER_RUNTIME_LDEXPL;
        const TypeId parameters[2] = { result, ldexp ? BUSTER_TYPE_INT : result };
        const uint32_t call = check_lower_runtime_call(writer, runtime, result, parameters, arguments, BUSTER_CHECK_NONE);
        check_lower_instruction(writer, BUSTER_TYPED_CALL, class, call);
        for (uint32_t a = arguments; a-- > 0;)
        {
            const TypeId argument = stream->argument_conversions[first + a].type;
            check_lower_convert(writer, argument, parameters[a]);
            check_lower_operand(writer, node, a + 3, false, argument);
        }
    }
    else
    {
        reason = class == BUSTER_TYPED_CLASS_F80 ? "elementwise builtins of long double values that clang computes with x87 instructions" :
                                                   "elementwise builtins of half-precision, bf16 or __float128 values";
    }
    if (reason != NULL)
    {
        check_lower_unsupported(writer, reason);
    }
}

// Pushes a reduction's vector operand, `source`, and where its elements don't fill it, the row that gives the lanes past
// them the reduction's identity (check_vector_reduce_fill_row) with its two constants, which run after the operand.
static void check_lower_reduce_operand(CheckLowerWriter* writer, uint32_t node, CBuiltinId builtin, TypeId source)
{
    Tier0Operation row;
    uint64_t keep[8];
    uint64_t fill[8];
    if (check_vector_reduce_fill_row(writer->program->types, builtin, source, &row, keep, fill))
    {
        check_lower_instruction(writer, BUSTER_TYPED_X86, BUSTER_TYPED_CLASS_VECTOR, check_lower_operation_index(writer, &row));
        check_lower_instruction(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_VECTOR, check_lower_vector_constant(writer, fill, row.result_bytes));
        check_lower_instruction(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_VECTOR, check_lower_vector_constant(writer, keep, row.result_bytes));
    }
    check_lower_operand(writer, node, 3, false, source);
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
// - an elementwise function's operands, then any constant splats its row takes;
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
        // A shuffle of a conversion into a vector Tier 0 has no class for (of 2 bytes): its row reads the conversion's
        // operand, and the second vector's constant bytes where it permutes (check_vector_narrow_shuffle_row).
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
            reason = "vector types of other than 4, 8, 16, 32 or 64 bytes";
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
            reason = "vector types of other than 4, 8, 16, 32 or 64 bytes";
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
    else if (check_lower_libm_builtin(builtin) || check_lower_strict_round(graph, node, builtin))
    {
        check_lower_vector_calls(writer, node, builtin, effect->evaluated_argument_count);
    }
    else if (check_vector_floating_instructions(builtin) || builtin == CBUILTIN_ELEMENTWISE_LDEXP)
    {
        check_lower_vector_floating(writer, node, builtin, effect->evaluated_argument_count);
    }
    else if (builtin == CBUILTIN_REDUCE_IN_ORDER_FADD)
    {
        check_lower_reduce_in_order(writer, node);
    }
    else if (builtin == CBUILTIN_ELEMENTWISE_PDEP || builtin == CBUILTIN_ELEMENTWISE_PEXT)
    {
        check_lower_vector_deposit(writer, node, builtin);
    }
    else if (builtin >= CBUILTIN_MASKED_LOAD && builtin <= CBUILTIN_MATRIX_COLUMN_MAJOR_STORE)
    {
        // A masked builtin's mask is a vector of bools, which the lowering has no class for; a matrix builtin's call is
        // an error before it (no unit has a matrix type).
        reason = "masked vector memory builtins, whose masks are vectors of bools";
    }
    else if (builtin >= CBUILTIN_REDUCE_MAXIMUM && builtin <= CBUILTIN_REDUCE_ASSOC_FADD)
    {
        // The rows run first to last, each but the first taking the one before's vector; a maximum's or minimum's each take
        // its mask, a constant, after it; a sum's last its start value (-0.0 without one), evaluated after the vector.
        const TypeId source = stream->argument_conversions[first].type;
        const bool sum = builtin == CBUILTIN_REDUCE_ASSOC_FADD;
        Tier0Operation floating[BUSTER_CHECK_VECTOR_FLOATING_REDUCE_ROWS];
        reason = check_lower_vector_of(writer, source) == 0 ? "vector types of other than 4, 8, 16, 32 or 64 bytes" :
                                                                check_vector_reduce_floating_rows(types, builtin, source, floating, &row_count);
        if (reason == NULL && row_count != 0)
        {
            const CheckVectorShape shape = check_vector_builtin_shape(types, source);
            const bool d = shape.element == 8;
            const uint8_t class = d ? BUSTER_TYPED_CLASS_F64 : BUSTER_TYPED_CLASS_F32;
            for (uint32_t r = row_count; r-- > 0;)
            {
                const Tier0Operation* row = floating + r;
                check_lower_instruction(writer, BUSTER_TYPED_X86, row->result_class, check_lower_operation_index(writer, row));
                if (!sum)
                {
                    uint64_t words[8] = { 0 };
                    const uint64_t mask = builtin == CBUILTIN_REDUCE_MAXIMUM ? check_vector_floating_sign(d) - 1 : check_vector_floating_sign(d);
                    for (uint32_t w = 0; w < 8; w += 1)
                    {
                        words[w] = d ? mask : mask | mask << 32;
                    }
                    const uint32_t bytes = row->operand_bytes[1];
                    memset((uint8_t*)words + bytes, 0, 64 - bytes);
                    check_lower_instruction(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_VECTOR, check_lower_vector_constant(writer, words, bytes));
                }
                else if (r == row_count - 1 && count == 2)
                {
                    check_lower_operand(writer, node, 4, false, stream->argument_conversions[first + 1].type);
                }
                else if (r == row_count - 1)
                {
                    check_lower_instruction(writer, BUSTER_TYPED_CONST, class, check_vector_floating_sign(d));
                }
            }
            check_lower_reduce_operand(writer, node, builtin, source);
            writer->vectors = true;
        }
    }
    else if (check_builtin_vector_elementwise(builtin))
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
        const uint32_t negated = vectors && builtin == CBUILTIN_ELEMENTWISE_FMA && arguments == 3 && check_vector_lower_brain(types, result) ?
                                     check_lower_brain_negations(program, first, result) :
                                     0;
        CheckVectorConstants constants = { 0 };
        reason = vectors ? check_vector_elementwise_row(types, target, builtin, result, arguments, negated, rows, &constants) :
                 scalars ? check_vector_scalar_row(target, builtin, scalar, arguments, rows) :
                           "elementwise builtins of scalars";
        if (reason == NULL)
        {
            check_lower_instruction(writer, BUSTER_TYPED_X86, vectors ? BUSTER_TYPED_CLASS_VECTOR : scalar, check_lower_operation_index(writer, rows));
            check_lower_row_constants(writer, rows, &constants, arguments);
            for (uint32_t a = arguments; a-- > 0;)
            {
                // A negated operand's negation is the row's form's: its operand in its place.
                const bool folded = (negated >> a & 1) != 0;
                check_lower_operand(writer, folded ? graph->argument_nodes[first + a] : node, folded ? 0 : a + 3, false, stream->argument_conversions[first + a].type);
            }
            writer->vectors = true;
        }
    }
    else if ((builtin >= CBUILTIN_REDUCE_ADD && builtin <= CBUILTIN_REDUCE_OR) || builtin == CBUILTIN_REDUCE_XOR)
    {
        const TypeId source = stream->argument_conversions[first].type;
        reason = check_lower_vector_of(writer, source) == 0 ? "vector types of other than 4, 8, 16, 32 or 64 bytes" :
                                                                check_vector_reduce_rows(types, target, builtin, source, rows, &row_count);
        if (reason == NULL && row_count != 0)
        {
            // The last row's element, at its class's type, converted to the call's; the rows run first to last.
            check_lower_convert(writer, check_lower_class_type(rows[row_count - 1].result_class), result);
            for (uint32_t r = row_count; r-- > 0;)
            {
                check_lower_instruction(writer, BUSTER_TYPED_X86, rows[r].result_class, check_lower_operation_index(writer, rows + r));
            }
            check_lower_reduce_operand(writer, node, builtin, source);
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

// Whether a type is a complex half-precision one (_Complex _Float16, or the _Complex __bf16 the usual arithmetic
// conversions make): four bytes of its two parts' bits, an aggregate's value.
static bool check_lower_complex_half(const TypeTable* types, TypeId type)
{
    const TypeId element = check_lower_complex_element(types, type);
    return element == BUSTER_TYPE_FLOAT16 || element == BUSTER_TYPE_BF16;
}

// A __builtin_bit_cast side's class where its bits are the value of a register: an integer's, an enum's or a pointer's, a
// float's or a double's, or a vector's, and a half-precision value's bits, a U16's. A complex half-precision value's four
// bytes have U32's bits, read from or written to its frame object (check_lower_bit_cast); UINT8_MAX for any other
// (other aggregates, arrays, long double and 128-bit integers), whose bits the lowering doesn't move.
static uint8_t check_lower_bit_cast_class(const CheckLowerWriter* writer, TypeId type)
{
    const uint8_t kind = check_action_kind(writer->program->types, type);
    const uint8_t class = check_lower_aggregate(writer, type) ? UINT8_MAX : check_lower_class(writer, type);
    const bool integer = (kind == BUSTER_TYPE_KIND_INTEGER || kind == BUSTER_TYPE_KIND_ENUM || kind == BUSTER_TYPE_KIND_POINTER) && class >= BUSTER_TYPED_CLASS_I8 &&
                         class <= BUSTER_TYPED_CLASS_U64;
    const bool real = kind == BUSTER_TYPE_KIND_FLOATING && (class == BUSTER_TYPED_CLASS_F32 || class == BUSTER_TYPED_CLASS_F64);
    const bool vector = class == BUSTER_TYPED_CLASS_VECTOR && check_lower_vector_of(writer, type) != 0;
    const bool half = check_lower_class_half(class) != CHECK_HALF_NONE;
    const bool complex_half = check_lower_complex_half(writer->program->types, type);
    return integer || real || vector ? class : half ? BUSTER_TYPED_CLASS_U16 : complex_half ? BUSTER_TYPED_CLASS_U32 : UINT8_MAX;
}

// __builtin_bit_cast(T, x): x's bytes as a T, of x's size. One the checker folded is its bytes, a floating T's constant
// or a vector T's (an integer T's is its value's, which the constants take before this). At run time, between integers
// or pointers, or between vectors, the bits are the value already; between a vector of 4 or 8 bytes and a scalar of its
// size they move between their registers (check_lower_vector_bits); between an integer and a float or a double they go
// through a frame temporary, as clang -O0's go through memory.
static void check_lower_bit_cast(CheckLowerWriter* writer, uint32_t node)
{
    const CheckStream* stream = writer->program->stream;
    const uint32_t selected = check_program_builtin_index(stream->bit_casts, stream->bit_cast_count, sizeof(CheckBuiltinBitCastEffect), node);
    const CheckBuiltinBitCastEffect* effect = selected != BUSTER_CHECK_NONE ? stream->bit_casts + selected : NULL;
    const TypeId result = stream->values[node].type;
    const uint8_t from = effect != NULL ? check_lower_bit_cast_class(writer, effect->source_type) : UINT8_MAX;
    const uint8_t to = result != 0 ? check_lower_bit_cast_class(writer, result) : UINT8_MAX;
    const bool source_complex = effect != NULL && check_lower_complex_half(writer->program->types, effect->source_type);
    const bool target_complex = result != 0 && check_lower_complex_half(writer->program->types, result);
    if (effect == NULL)
    {
        check_lower_fail(writer, CHECK_PROGRAM_INVALID);
    }
    else if (from == UINT8_MAX || to == UINT8_MAX)
    {
        check_lower_unsupported(writer, "bit-cast builtins of types other than integers, pointers, floats, doubles, half-precision types, their complex types and vectors");
    }
    else if (target_complex)
    {
        // A complex half-precision result is its own four-byte object, never a U32 value mistaken for an address.
        // SET_LOCAL writes the source's representation directly, so even signaling NaNs and signed zeros retain every bit.
        const uint32_t local = check_lower_object(writer, 4, type_alignment_log2(writer->program->types, result));
        const uint8_t stored = from == BUSTER_TYPED_CLASS_VECTOR || source_complex || effect->folded ? BUSTER_TYPED_CLASS_U32 : from;
        check_lower_instruction(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, local);
        check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, stored, local);
        if (effect->folded)
        {
            uint64_t bits = 0;
            for (uint32_t i = 0; i < effect->payload_count && i < 4; i += 1)
            {
                bits |= (uint64_t)effect->bytes[i] << (8 * i);
            }
            check_lower_instruction(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U32, bits);
        }
        else
        {
            if (source_complex)
            {
                check_lower_instruction(writer, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_U32, 0);
            }
            else if (from == BUSTER_TYPED_CLASS_VECTOR)
            {
                check_lower_vector_bits(writer, BUSTER_TYPED_CLASS_U32, 4, false);
            }
            check_lower_operand(writer, node, 3, false, effect->source_type);
        }
    }
    else if (effect->folded)
    {
        uint64_t words[8] = { 0 };
        for (uint32_t i = 0; i < effect->payload_count && i < sizeof(effect->bytes); i += 1)
        {
            words[i / 8] |= (uint64_t)effect->bytes[i] << (8 * (i % 8));
        }
        if (to == BUSTER_TYPED_CLASS_VECTOR)
        {
            check_lower_instruction(writer, BUSTER_TYPED_CONST, to, check_lower_vector_constant(writer, words, check_lower_vector_of(writer, result)));
            writer->vectors = true;
        }
        else
        {
            check_lower_instruction(writer, BUSTER_TYPED_CONST, to, words[0]);
        }
    }
    else if (source_complex)
    {
        // Read the object's bytes in the destination's class, without extending or rounding either binary16 part.
        // A four-byte vector goes through U32's register move, as an ordinary scalar/vector bit cast does.
        if (to == BUSTER_TYPED_CLASS_VECTOR)
        {
            check_lower_vector_bits(writer, BUSTER_TYPED_CLASS_U32, 4, true);
        }
        check_lower_instruction(writer, BUSTER_TYPED_LOAD, to == BUSTER_TYPED_CLASS_VECTOR ? BUSTER_TYPED_CLASS_U32 : to, 0);
        check_lower_operand(writer, node, 3, false, effect->source_type);
    }
    else if ((from == BUSTER_TYPED_CLASS_VECTOR) != (to == BUSTER_TYPED_CLASS_VECTOR))
    {
        const bool into = to == BUSTER_TYPED_CLASS_VECTOR;
        check_lower_vector_bits(writer, into ? from : to, check_lower_vector_of(writer, into ? result : effect->source_type), into);
        check_lower_operand(writer, node, 3, false, effect->source_type);
    }
    else if ((from == to || (from <= BUSTER_TYPED_CLASS_U64 && to <= BUSTER_TYPED_CLASS_U64)) &&
             (check_lower_half(writer->program->types, effect->source_type) != CHECK_HALF_NONE || check_lower_half(writer->program->types, result) != CHECK_HALF_NONE))
    {
        // A half-precision value's bits and a 16-bit integer's are the same register's: a CONVERT of the class alone, never
        // a conversion of the value.
        if (from != to)
        {
            check_lower_instruction(writer, BUSTER_TYPED_CONVERT, to, 0);
        }
        check_lower_operand(writer, node, 3, false, effect->source_type);
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

// Always inlined into the copies of the tasks' loop (check_lower_drain_body), each with its constant `bit_ints`
// (CheckLowerWriter.bit_ints). `bit_int` is CheckLowerTask.bit_int.
static inline __attribute__((always_inline)) void check_lower_expand(CheckLowerWriter* writer, uint32_t node, bool address, bool initialized, bool discard, bool bit_ints,
                                                                     bool bit_int)
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
    // An expression of a _BitInt goes to check_lower_bit_int_node first, out of line, which may push it again for what
    // follows (CheckLowerTask.bit_int).
    if (bit_ints && (facts & BUSTER_CHECK_LOWER_TYPE_BIT_INT) != 0 && !bit_int)
    {
        check_lower_bit_int_node(writer, node, address, initialized, discard);
    }
    else if (op == CHECK_OP_VALUE && !initialized && value->object != 0 && value->object < program->object_count &&
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
            // A real value's imaginary part: zero, after the value, evaluated and discarded; an aggregate's (a __float128's,
            // a _BitInt's past 128 bits) a frame object of zeros, whose address is its value (check_lower_aggregate_zero),
            // not an address of 0.
            if (aggregate)
            {
                check_lower_instruction(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, check_lower_aggregate_zero(writer, value->type));
            }
            else
            {
                check_lower_instruction(writer, BUSTER_TYPED_CONST, class, 0);
            }
            check_lower_operand(writer, node, 0, false, BUSTER_TYPE_VOID);
        }
        else if (stream->values[a].category == CHECK_LVALUE || check_lower_complex(writer, input_type))
        {
            // The part at its offset from the value's address: an lvalue's, or a complex rvalue's, whose value is its
            // object's address. An aggregate part's value (a __float128's, a _BitInt's past 128 bits) is its address.
            if (!address && !aggregate)
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
        const CBuiltinId id = check_builtin_atomic_base((CBuiltinId)graph->members[node]);
        if (id == CBUILTIN_ATOMIC_ALWAYS_LOCK_FREE || id == CBUILTIN_ATOMIC_IS_LOCK_FREE)
        {
            check_lower_atomic_query(writer, node);
        }
        else if ((check_builtin_atomic_c11(id) && helper == BUSTER_CHECK_NONE) || check_builtin_atomic_generic(id))
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
    else if (class >= BUSTER_TYPED_CLASS_VECTOR && op >= CHECK_OP_NEGATIVE && op <= CHECK_OP_BIT_XOR && op != CHECK_OP_NOT)
    {
        // VECTOR, the last of Tier 0's classes, and past it the half-precision classes and no class: out of line.
        check_lower_vector_operation(writer, node);
    }
    else if (op == CHECK_OP_CAST || op == CHECK_OP_CONVERT || op == CHECK_OP_POSITIVE)
    {
        check_lower_operand(writer, node, 0, false, value->type);
    }
    else if (((op >= CHECK_OP_NEGATIVE && op <= CHECK_OP_LOGICAL_OR) || (op >= CHECK_OP_PRE_INCREMENT && op <= CHECK_OP_BIT_XOR_ASSIGN) ||
              (op == CHECK_OP_CONDITIONAL && check_lower_aggregate(writer, type_a))) &&
             (aggregate || check_lower_aggregate(writer, type_a)) &&
             (check_lower_tile_type(types, value->type) || check_lower_tile_type(types, type_a)))
    {
        // The tile type gains value transport, not the ordinary VECTOR rows' arithmetic. In particular its address
        // must never reach a scalar arithmetic row or the complex-number path merely because it is an aggregate.
        check_lower_unsupported(writer, "arithmetic on 1024-byte tile vectors");
    }
    else if (check_lower_complex_operation(writer, op, value->type, type_a))
    {
        // An operation on complex values (check_lower_complex_emit): each operand into a temporary as it is evaluated (a
        // real one kept real, converted to the computation's element), since Tier 0 reads an entry's last operand as the
        // class of the entry before it.
        const bool unary = op == CHECK_OP_NEGATIVE || op == CHECK_OP_BIT_NOT;
        // A __float128's operands are converted to it, each its object's address, and so are a _BitInt's past 128 bits,
        // but a shift's count, of its own promoted type.
        const bool limbs = check_lower_limb_value(writer, type_a);
        const uint8_t element_class = check_lower_quad(writer, type_a) || limbs || check_lower_complex_parts(writer, type_a) ?
                                          BUSTER_TYPED_CLASS_U64 :
                                          check_lower_class(writer, check_lower_complex_element(types, type_a));
        const uint8_t class_a = check_lower_complex(writer, stream->values[a].type) ? BUSTER_TYPED_CLASS_U64 : element_class;
        const uint8_t class_b = !unary && check_lower_complex(writer, stream->values[b].type) ? BUSTER_TYPED_CLASS_U64 :
                                limbs && !unary ? check_lower_value_class(writer, type_b) : element_class;
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
        // A vector's conditional splats a scalar arm, out of line (check_lower_vector_operation).
        if (class == BUSTER_TYPED_CLASS_VECTOR && op == CHECK_OP_CONDITIONAL)
        {
            check_lower_vector_operation(writer, node);
        }
        else
        {
            check_lower_logical(writer, program, node, op, a, b, type_a, type_b, value, class, false);
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
    else if (op >= CHECK_OP_PRE_INCREMENT && op <= CHECK_OP_BIT_XOR_ASSIGN && (check_lower_aggregate(writer, stream->values[a].type) || check_lower_aggregate(writer, type_a)))
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
        else if (check_lower_tile_type(types, vector))
        {
            // A returned tile value already is the address of its complete object, unlike an ordinary VECTOR's
            // register value. An element of that rvalue reads through the address directly.
            check_lower_operand(writer, node, 0, false, vector);
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
        // A compare of narrow operands, which only a program with a _BitInt type has (CheckLowerWriter.bit_ints), compares
        // in their computing class (check_lower_bit_int_node).
        if (bit_ints && kind >= BUSTER_TYPED_EQ && kind <= BUSTER_TYPED_GE && check_lower_narrow(check_lower_class(writer, type_a)))
        {
            check_lower_bit_int_node(writer, node, address, initialized, discard);
        }
        else
        {
            check_lower_operation(writer, kind, class);
            check_lower_count(writer, kind, type_b);
            check_lower_operand(writer, node, 1, false, type_b);
            check_lower_operand(writer, node, 0, false, type_a);
        }
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

// The tasks' loop, as check_lower_drain, check_lower_drain_bit_ints and check_lower_drain_sources compile it: with
// `sources`, each task's token set before it runs (check_lower_task_source); with `bit_ints`, a program's that has a
// _BitInt type (CheckLowerWriter.bit_ints), whose expressions and conversions it tests for one. Constants in each but the
// last's `bit_ints`, so that the plain loop does none of it: the tests for a _BitInt took 3.9 million of sqlite3.c's
// instructions there (#137).
static inline __attribute__((always_inline)) void check_lower_drain_body(CheckLowerWriter* writer, bool sources, bool bit_ints)
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
                        check_lower_expand(writer, node, (flags & 1) != 0, ((flags >> 1) & 1) != 0, ((flags >> 2) & 1) != 0, bit_ints, ((flags >> 4) & 1) != 0);
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
            check_lower_conversion_body(writer, node, type, bit_ints);
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
            // The complex values' tasks and the half-precision values' (check_lower_complex_task), the kinds' last, behind one
            // compare: few functions have them.
            check_lower_complex_task(writer, check_lower_task_unpack(*task));
        }
    }
    writer->expression_nodes += expression_nodes;
}

static void check_lower_drain(CheckLowerWriter* writer)
{
    check_lower_drain_body(writer, false, false);
}

// The tasks' loop for a program that has a _BitInt type (CheckLowerWriter.bit_ints): a copy of its own
// (check_lower_drain_body).
static __attribute__((noinline)) void check_lower_drain_bit_ints(CheckLowerWriter* writer)
{
    check_lower_drain_body(writer, false, true);
}

// The tasks' loop for -g's rows (check_program_lower_sources): a copy of its own (check_lower_drain_body).
static __attribute__((noinline)) void check_lower_drain_sources(CheckLowerWriter* writer)
{
    check_lower_drain_body(writer, true, writer->bit_ints);
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
    else if (write->bit_offset % 8 != 0 || (!aggregate && (class == UINT8_MAX || class == BUSTER_TYPED_CLASS_VOID)))
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
    const uint8_t own = check_lower_class(writer, item->type);
    // A narrow _BitInt's switch keeps its value in its computing class (check_lower_computed), which the cases' constants
    // are converted to.
    const uint8_t class = check_lower_computed(own);
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
        if (class != own)
        {
            check_lower_emit(writer, BUSTER_TYPED_CONVERT, class, 0);
        }
        check_lower_emit(writer, range ? BUSTER_TYPED_GE : BUSTER_TYPED_EQ, BUSTER_TYPED_CLASS_I32, 0);
        if (range)
        {
            check_lower_emit(writer, BUSTER_TYPED_BRANCH_FALSE, BUSTER_TYPED_CLASS_I32, skip);
            check_lower_emit(writer, BUSTER_TYPED_LOCAL, class, local);
            const CheckValue upper = { .type = item->type, .category = CHECK_RVALUE, .constant_kind = CHECK_CONSTANT_INTEGER, .integer = item->upper };
            check_lower_constant(writer, &upper);
            if (class != own)
            {
                check_lower_emit(writer, BUSTER_TYPED_CONVERT, class, 0);
            }
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
            writer->label_tables[writer->label_entry_count] = writer->table_jump_count + 1;
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
    if (writer->table_jump_count < writer->table_count)
    {
        writer->table_jumps[writer->table_jump_count++] = writer->count - 1;
    }
    else
    {
        check_lower_fail(writer, CHECK_PROGRAM_INVALID);
    }
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

// A cleanup's call (CHECK_CONTROL_CLEANUP, #147): its function's, of its variable's address, the result dropped. A function
// of a parameter other than a pointer is refused by name (clang's code bit-casts the address to a _Bool, invalid IR,
// which leaves the address's low bit), and so is one of a result that comes back in memory or a result object (an
// aggregate, a 128-bit integer, a vector); a variadic one isn't: one parameter, no vector registers.
static void check_lower_cleanup(CheckLowerWriter* writer, CheckControl control)
{
    const CheckProgram* program = writer->program;
    const CheckInput* input = program->input;
    const TypeTable* types = program->types;
    const CheckInputCleanup* cleanup = control.target < input->cleanup_count ? input->cleanups + control.target : NULL;
    const uint32_t variable = cleanup != NULL ? input->objects[cleanup->declaration] : 0;
    const uint32_t function = cleanup != NULL ? input->objects[cleanup->function] : 0;
    const SymbolId symbol = function != 0 && function < program->object_count ? program->object_symbols[function] : 0;
    const TypeId type = cleanup != NULL ? type_strip_aligned(types, input->declared_types[cleanup->function]) : 0;
    uint32_t parameters = 0;
    const TypeId* parameter_types = type != 0 && type_kind(types, type) == BUSTER_TYPE_KIND_FUNCTION ? type_parameters(types, type, &parameters) : NULL;
    const TypeId result = parameter_types != NULL ? type_return(types, type) : 0;
    const uint8_t class = result != 0 ? check_lower_class(writer, result) : UINT8_MAX;
    if (symbol == 0 || parameters != 1 || variable == 0 || variable >= program->object_count)
    {
        check_lower_fail(writer, CHECK_PROGRAM_INVALID);
    }
    else if (type_kind(types, type_strip_aligned(types, parameter_types[0])) != BUSTER_TYPE_KIND_POINTER)
    {
        check_lower_unsupported(writer, "cleanup functions of a parameter other than a pointer");
    }
    else if (class == UINT8_MAX || check_lower_aggregate(writer, result) || check_lower_wide(writer, result) || check_lower_vector_of(writer, result) != 0)
    {
        check_lower_unsupported(writer, "cleanup functions whose result returns in memory, a 128-bit integer or a vector");
    }
    else
    {
        const uint32_t call = check_lower_native_call(writer, symbol, result, parameter_types, 1);
        if (call != BUSTER_CHECK_NONE)
        {
            const bool variadic = (types->types.flags[type_index(type)] & BUSTER_TYPE_FLAG_VARIADIC) != 0;
            writer->call_variadic[call] = variadic;
            writer->abi->variadic[writer->abi->count - 1] = variadic;
            check_lower_address(writer, variable, 0);
            check_lower_emit(writer, BUSTER_TYPED_CALL, class, call);
            if (class != BUSTER_TYPED_CLASS_VOID)
            {
                check_lower_emit(writer, BUSTER_TYPED_DISCARD, class, 0);
            }
        }
    }
}

// A return's value before its cleanups (CHECK_CONTROL_RESULT, #147), kept where its RETURN of BUSTER_CHECK_CONTROL_KEPT
// returns it from (check_lower_return_kept), as clang's code stores it in its return slot first: a struct's or a union's
// bytes copied where the hidden pointer points, or into a frame object of their own for registers (so that a cleanup
// writing the object they came from doesn't change them); a result object's value set into it; any other value into a
// temporary of its class; and an empty aggregate's or a void function's evaluated for its effects.
static void check_lower_result(CheckLowerWriter* writer, uint32_t node)
{
    const TypeTable* types = writer->program->types;
    const bool aggregate = check_lower_aggregate(writer, writer->result);
    const bool hidden = aggregate && writer->hidden != BUSTER_CHECK_NONE;
    const bool registers = aggregate && !hidden && writer->abi_table->results[type_index(writer->result)].kind == BUSTER_ABI_RESULT_REGISTERS;
    const uint8_t class = aggregate || writer->result_object != BUSTER_CHECK_NONE ? BUSTER_TYPED_CLASS_U64 : check_lower_class(writer, writer->result);
    writer->kept_class = aggregate && !hidden && !registers ? BUSTER_TYPED_CLASS_VOID : class;
    writer->kept_address = registers || (!aggregate && writer->result_object != BUSTER_CHECK_NONE);
    if (hidden || registers)
    {
        const uint64_t size = type_size(types, writer->result);
        if (writer->kept_local == BUSTER_CHECK_NONE)
        {
            writer->kept_local = hidden ? writer->hidden : check_lower_object(writer, size, type_alignment_log2(types, writer->result));
        }
        check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_COPY, .value = size, .address = false,
                                                 .volatile_copy = check_lower_copy_node_volatile(writer, node, writer->result) });
        check_lower_expression(writer, node, false, 0);
        check_lower_instruction(writer, hidden ? BUSTER_TYPED_LOCAL : BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, writer->kept_local);
    }
    else if (!aggregate && writer->result_object != BUSTER_CHECK_NONE)
    {
        writer->kept_local = writer->result_object;
        check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, check_lower_class(writer, writer->result), writer->result_object);
        check_lower_expression(writer, node, false, 0);
    }
    else if (writer->kept_class != BUSTER_TYPED_CLASS_VOID)
    {
        if (writer->kept_local == BUSTER_CHECK_NONE)
        {
            writer->kept_local = check_lower_temporary(writer, class);
        }
        check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, class, writer->kept_local);
        check_lower_expression(writer, node, false, 0);
    }
    else
    {
        check_lower_expression(writer, node, false, BUSTER_TYPE_VOID);
    }
}

// The RETURN of a value check_lower_result kept, after the return's cleanups.
static void check_lower_return_kept(CheckLowerWriter* writer)
{
    check_lower_instruction(writer, BUSTER_TYPED_RETURN, writer->kept_class, 0);
    if (writer->kept_local != BUSTER_CHECK_NONE)
    {
        check_lower_instruction(writer, writer->kept_address ? BUSTER_TYPED_LOCAL_ADDRESS : BUSTER_TYPED_LOCAL, writer->kept_address ? BUSTER_TYPED_CLASS_U64 : writer->kept_class,
                                writer->kept_local);
    }
}

// A musttail return (#147, CHECK_CONTROL_RETURN of BUSTER_CHECK_CONTROL_TAIL): its value's call, as a tail call, the
// return's conversion (none: c_check_musttail.c checked that the call's result is the function's own) and the selections
// around the call (_Generic's, __builtin_choose_expr's, __extension__'s) looked through. Its call's own lowering
// (check_lower_call_body, which takes it for `writer->tail`) writes the TAIL_CALL, after which nothing of the function
// runs; neither the conversions nor the result objects of a call's value expression are there.
static void check_lower_tail_return(CheckLowerWriter* writer, uint32_t node)
{
    const CheckGraph* graph = &writer->program->input->graph;
    uint32_t call = node < graph->count && graph->ops[node] == CHECK_OP_CONVERT ? graph->operand0[node] : node;
    for (uint32_t steps = 0; call < graph->count && steps < graph->count && graph->ops[call] == CHECK_OP_SELECT; steps += 1)
    {
        call = graph->operand0[call];
    }
    if (call < graph->count && graph->ops[call] == CHECK_OP_CALL)
    {
        writer->tail = call;
        check_lower_call(writer, call);
    }
    else
    {
        check_lower_fail(writer, CHECK_PROGRAM_INVALID);
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
            writer->label_tables[writer->label_entry_count] = 0;
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
    else if (control.kind == CHECK_CONTROL_CLEANUP)
    {
        check_lower_cleanup(writer, control);
    }
    else if (control.kind == CHECK_CONTROL_RESULT)
    {
        check_lower_result(writer, control.node);
    }
    else if (control.kind == CHECK_CONTROL_RETURN && control.target == BUSTER_CHECK_CONTROL_KEPT)
    {
        check_lower_return_kept(writer);
    }
    else if (control.kind == CHECK_CONTROL_RETURN && control.target == BUSTER_CHECK_CONTROL_TAIL)
    {
        check_lower_tail_return(writer, control.node);
    }
    else if (control.kind == CHECK_CONTROL_SWITCH)
    {
        // A narrow _BitInt's value is kept in its computing class, which its cases compare in (check_lower_computed).
        const TypeId type = program->stream->values[control.node].type;
        const uint8_t own = check_lower_class(writer, type);
        const uint8_t class = check_lower_computed(own);
        const uint32_t local = check_lower_temporary(writer, class);
        writer->switch_locals[control.token] = local;
        check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, class, local);
        if (class != own)
        {
            check_lower_instruction(writer, BUSTER_TYPED_CONVERT, class, 0);
        }
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
    // Whether the program's type table has a _BitInt type (CheckLowerWriter.bit_ints).
    bool bit_ints;
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
    uint16_t runtimes[CHECK_LOWER_RUNTIMES];
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

// `declaring` is check_program_declaring's source-ordered list, including initializer-only tokens. Its logical
// length is declaring_count; no padding is read here and the original declaration/linkage predicates still apply.
// The normal path reuses that already-built compact list; the scalar oracle retains the independent full-token
// traversal. All borrowed columns are immutable during this pass, and symbols belongs to CheckLowerShared. The names
// are the `spelled` runtime functions' the unit spells, `identifiers[s]` runtime function `runtimes[s]`'s.
static void check_lower_declared_runtimes(const CheckProgram* program, const uint32_t* declaring, uint32_t declaring_count, const IdentId* identifiers,
                                          const uint16_t* runtimes, uint32_t spelled, SymbolId* symbols, bool scalar)
{
    const CheckInput* input = program->input;
    const uint32_t* declarations = input->declarations;
    const uint32_t* objects = input->objects;
    const IdentId* names = program->parser->tokens.names;
    const uint8_t* linkage = input->linkage;
    const SymbolId* object_symbols = program->object_symbols;
    const uint32_t object_count = program->object_count;
    const uint32_t count = scalar ? input->token_count : declaring_count;
    for (uint32_t d = 0; d < count; d += 1)
    {
        const uint32_t declaration = scalar ? d : declaring[d];
        const uint32_t object = objects[declaration];
        const IdentId name = names[declaration];
        if (name != 0 && declarations[declaration] == declaration && linkage[declaration] == 2 && object != 0 && object < object_count)
        {
            // Most declarations name none: the names' compare, a reduction clang vectorizes, comes first.
            bool any = false;
            for (uint32_t s = 0; s < spelled; s += 1)
            {
                any |= identifiers[s] == name;
            }
            for (uint32_t s = 0; any && s < spelled; s += 1)
            {
                const uint32_t r = runtimes[s];
                if (identifiers[s] == name && symbols[r] == 0)
                {
                    symbols[r] = object_symbols[object];
                }
            }
        }
    }
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
    // A libm builtin of a vector (check_lower_vector_calls) calls the C library's function once an element, past what the
    // per-node bounds below give a node (#934's review): its lanes' calls and their arguments, and each call's entries, its
    // SET and CALL, an argument's LOAD, and the element addresses of each argument and of the result (up to 13 with two
    // arguments).
    uint64_t lane_calls = 0;
    uint64_t lane_arguments = 0;
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
        const uint8_t kind = type != 0 ? type_kind(types, type_strip_aligned(types, type)) : BUSTER_TYPE_KIND_NONE;
        const CBuiltinId member = (CBuiltinId)input->graph.members[node];
        if (op == CHECK_OP_BUILTIN_VECTOR && (check_lower_libm_builtin(member) || check_lower_strict_round(&input->graph, node, member)) &&
            (kind == BUSTER_TYPE_KIND_VECTOR || kind == BUSTER_TYPE_KIND_EXT_VECTOR))
        {
            const uint64_t lanes = check_vector_builtin_shape(types, type).lanes;
            lane_calls += lanes;
            lane_arguments += lanes * input->graph.argument_counts[node];
        }
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
    // A unit with a _BitInt past 128 bits (#137) has room for its limbs, its computations' temporaries and libgcc's calls
    // (check_lower_limb_room).
    const CheckLowerLimbRoom limb_room = program->status == CHECK_PROGRAM_OK && program->stream != 0 && input != 0 && types != 0 ? check_lower_limb_room(program) :
                                                                                                                                (CheckLowerLimbRoom){ 0 };
    const uint64_t capacity64 = (uint64_t)nodes * 32 + (uint64_t)program->control_count * 4 + (uint64_t)builtin_count * BUSTER_CBUILTIN_LOWER_ENTRIES +
                                varargs * BUSTER_CBUILTIN_LOWER_ENTRIES + asm_operands * 16 + asm_statements * 4 + (uint64_t)(input != NULL ? input->kr_parameter_count : 0) * 5 +
                                limb_room.entries + lane_calls * 13 + 32;
    // A node's lowering takes up to two temporaries, and a 128-bit integer's four more: a division's two operands, a
    // conversion's or an argument's object, and a call's result object. The bounds are address space: an arena's push
    // touches none of it.
    // Each lane's writer resets its integer pool per body and copies completed local columns to output, so one pool
    // bounds the writer's reusable local room regardless of the number of bodies.
    const uint64_t recipe_pools = integer_builtin_count != 0 ? 1 : 0;
    const uint64_t local_capacity64 = (uint64_t)count + (uint64_t)nodes * 6 + ((uint64_t)builtin_count - integer_builtin_count + recipe_pools) * BUSTER_CBUILTIN_LOWER_TEMPORARIES +
                                      varargs * (BUSTER_CBUILTIN_LOWER_TEMPORARIES + 4) + (uint64_t)program->body_count * 5 + asm_operands + asm_statements + (input != NULL ? input->kr_parameter_count : 0) +
                                      limb_room.locals + 1;
    bool valid = output != scratch && program->status == CHECK_PROGRAM_OK && capacity64 <= UINT32_MAX && local_capacity64 <= UINT32_MAX &&
                 x87_capacity64 <= UINT32_MAX;
    lowered->status = valid ? CHECK_PROGRAM_OK : CHECK_PROGRAM_INVALID;
    // Each lowered function's validation (tier0_validate) reads the effects tables (its X86 entries' forms, #760), which
    // a lowering of bodies without the emission's setup (check_program_emit's), a test's, builds here: a unit of data
    // alone leaves the backend cold, as the emission does.
    if (valid && program->body_count != 0 && !(x86_semantics_initialized || x86_semantics_initialize()))
    {
        lowered->status = CHECK_PROGRAM_BACKEND;
        valid = false;
    }
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
        shared->bit_ints = memchr(types->types.kind, BUSTER_TYPE_KIND_BIT_INT, types->types.count) != NULL;
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
    // runtime call, independent of its byte size, within the same per-node bounds. A complex _BitInt's past 128 bits makes
    // more, which check_lower_limb_room counts. The runtime functions' provisional symbols (check_lower_runtime) follow
    // the helpers'.
    const uint64_t abi_capacity64 = (uint64_t)nodes * 3 + program->body_count + errno_calls + asm_statements + lowered->initializers.count + limb_room.calls +
                                    lane_calls + 1;
    const uint64_t argument_capacity64 = input != 0 ? (uint64_t)input->graph.argument_count + count + (uint64_t)nodes * 4 + errno_calls + asm_statements +
                                                    (uint64_t)lowered->initializers.count * 3 + limb_room.arguments + lane_arguments + 1 : 0;
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
        const uint32_t declaring_count = check_program_declaring(input->declarations, input->initializer_nodes, nodes, count, declaring, BUSTER_SCALAR_PATH(scalar));
        // In reverse, so that a node's declaration is its first token and each body's locals are linked in token order.
        for (uint32_t d = declaring_count; d-- > 0;)
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
        // name with linkage that has one (the errno accessor's is check_lower_runtime_symbols' already). The names are the
        // lowering's own functions' and those of the library builtins the unit calls (#148), of which the ones the unit
        // spells are listed, `spelled` of them, and one pass over the declarations, which only such a unit takes, matches
        // each declaration's name with them.
        shared->declared_runtimes[CHECK_LOWER_RUNTIME_ERRNO] = lowered->errno_symbol;
        uint64_t called[(CHECK_LOWER_RUNTIMES + 63) / 64] = { 0 };
        for (uint32_t x = 0; program->stream != NULL && x < program->stream->x86_count; x += 1)
        {
            const BuiltinIntrinsicInfo* info = program->stream->x86[x].info;
            const CBuiltinLibraryId library = info != NULL && cbuiltin_library_candidate(info->name, info->name_length) ?
                                                  cbuiltin_library_id(info->name, info->name_length) : CBUILTIN_LIBRARY_COUNT;
            if (library != CBUILTIN_LIBRARY_COUNT)
            {
                const uint32_t runtime = check_lower_library_runtime(library);
                called[runtime >> 6] |= UINT64_C(1) << (runtime & 63);
            }
        }
        IdentId identifiers[CHECK_LOWER_RUNTIMES];
        uint16_t spelled_runtimes[CHECK_LOWER_RUNTIMES];
        uint32_t spelled = 0;
        for (uint32_t r = CHECK_LOWER_RUNTIME_ERRNO + 1; program->parser != NULL && program->identifiers != NULL && r < CHECK_LOWER_RUNTIMES; r += 1)
        {
            if (r < CHECK_LOWER_RUNTIME_LIBRARY || (called[r >> 6] & (UINT64_C(1) << (r & 63))) != 0)
            {
                const char* name = check_lower_runtime_names[r];
                const uint32_t length = (uint32_t)strlen(name);
                const IdentId identifier = intern_find_hashed(program->identifiers, (const uint8_t*)name, length, intern_hash_bytes((const uint8_t*)name, length));
                identifiers[spelled] = identifier;
                spelled_runtimes[spelled] = (uint16_t)r;
                spelled += identifier != 0;
            }
        }
        if (spelled != 0)
        {
            check_lower_declared_runtimes(program, declaring, declaring_count, identifiers, spelled_runtimes, spelled, shared->declared_runtimes,
                                          BUSTER_SCALAR_PATH(scalar));
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
        .bit_ints = shared->bit_ints,
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
    BUSTER_CHECK_LOWER_COLUMN(label_tables, uint32_t, (uint64_t)program->control_count + 1);
    BUSTER_CHECK_LOWER_COLUMN(table_jumps, uint32_t, (uint64_t)shared->lowered->table_count + 1);
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
    memset(writer->runtime_mask, 0, sizeof(writer->runtime_mask));
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
        writer->table_jump_count = 0;
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
        writer->tails = false;
        writer->tail = BUSTER_CHECK_NONE;
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
        writer->kept_local = BUSTER_CHECK_NONE;
        writer->kept_class = BUSTER_TYPED_CLASS_VOID;
        writer->kept_address = false;
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
        // ABI locals are the full signature's contiguous prefix, not the compact list of names. An unnamed parameter
        // still consumes its register(s) or stack slot; only the source object's binding is absent (#792).
        uint32_t parameter_count = 0;
        const TypeId* parameter_types = type_parameters(types, type, &parameter_count);
        uint32_t parameter = input->parameters[body.opening];
        for (uint32_t p = 0; !bare && p < parameter_count; p += 1)
        {
            const bool named = parameter != BUSTER_CHECK_NONE && input->parameters[parameter] == p;
            const TypeId declared_type = named ? input->declared_types[parameter] : parameter_types[p];
            const TypeId parameter_type = kr ? check_input_kr_parameter_type(types, declared_type) : declared_type;
            const bool promoted = type_unqualified(parameter_type) != type_unqualified(declared_type);
            // A promoted argument's ABI slot is distinct from the declared object, including its requested alignment.
            const CheckInputAttributes* attributes = named && !promoted ? input->attributes[parameter] : NULL;
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
            writer->token = named ? parameter : body.declaration;
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
            if (!promoted && ((type_qualifiers(declared_type) & BUSTER_TYPE_VOLATILE) != 0 ||
                              type_kind(types, type_strip_aligned(types, declared_type)) == BUSTER_TYPE_KIND_ATOMIC) && local < writer->local_count)
            {
                writer->local_flags[local] |= BUSTER_TYPED_LOCAL_ADDRESS_TAKEN;
            }
            if (named)
            {
                writer->local_map[input->objects[parameter]] = local;
                parameter = input->parameter_next[parameter];
            }
            // A transparent union's as its first member (check_lower_passed_as), whose bytes the prologue places.
            abi->arguments[abi->argument_count++] = aggregate ? check_lower_passed_as(writer, parameter_type) : parameter_type;
            function->parameter_count += 1;
            declared += 1;
        }
        // Incoming ABI locals must remain the contiguous prefix, including a hidden result pointer. Allocate the
        // declared objects only after that prefix, then convert once before any body expression can read or address
        // them. This cold pass exists only for K&R definitions; ordinary prototypes keep their original path.
        for (uint32_t parameter = kr && !bare ? input->parameters[body.opening] : BUSTER_CHECK_NONE;
             parameter != BUSTER_CHECK_NONE && status->status == CHECK_PROGRAM_OK; parameter = input->parameter_next[parameter])
        {
            const TypeId declared_type = input->declared_types[parameter];
            const TypeId incoming_type = check_input_kr_parameter_type(types, declared_type);
            if (incoming_type != type_unqualified(declared_type))
            {
                const CheckInputAttributes* attributes = input->attributes[parameter];
                const uint32_t requested = attributes != NULL && attributes->alignment_log2 != 0 ? attributes->alignment_log2 - 1u : 0;
                const uint32_t natural = type_alignment_log2(types, declared_type);
                const uint32_t alignment = requested > natural ? requested : natural;
                const uint8_t class = check_lower_class(writer, declared_type);
                const uint32_t incoming = writer->local_map[input->objects[parameter]];
                writer->token = parameter;
                check_lower_source(writer, parameter);
                if (alignment > BUSTER_TIER0_MAX_OBJECT_ALIGNMENT_LOG2)
                {
                    check_lower_unsupported(writer, "parameters aligned past 1 GB");
                }
                const uint32_t local = alignment > 3 ? check_lower_object(writer, type_size(types, declared_type), alignment) :
                                                      check_lower_temporary(writer, class);
                if ((type_qualifiers(declared_type) & BUSTER_TYPE_VOLATILE) != 0 && local < writer->local_count)
                {
                    writer->local_flags[local] |= BUSTER_TYPED_LOCAL_ADDRESS_TAKEN;
                }
                writer->local_map[input->objects[parameter]] = local;
                check_lower_emit(writer, BUSTER_TYPED_LOCAL, check_lower_class(writer, incoming_type), incoming);
                if ((check_lower_facts(writer, declared_type) & BUSTER_CHECK_LOWER_TYPE_BOOL) != 0)
                {
                    // Clang's K&R entry truncates the promoted int to i1, then extends it into the byte local.
                    // This is not the truth conversion used by an ordinary assignment to _Bool.
                    check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 1);
                    check_lower_emit(writer, BUSTER_TYPED_AND, BUSTER_TYPED_CLASS_I32, 0);
                    check_lower_emit(writer, BUSTER_TYPED_CONVERT, class, 0);
                }
                else
                {
                    check_lower_conversion(writer, incoming_type, declared_type);
                }
                check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, class, local);
            }
        }
        writer->token = body.declaration;
        check_lower_source(writer, body.declaration);
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
                // The attribute's alignment is its log2 plus one, 0 for none. Without one, an array of 16 bytes or more is
                // aligned to at least 16, as the x86-64 psABI and clang align a local array (type_large_array, #978), but a
                // compound literal, which takes its type's.
                const bool attributed = attributes != 0 && attributes->alignment_log2 != 0;
                const uint32_t declared_alignment = attributed ? attributes->alignment_log2 - 1u :
                                                    !input->compound_objects[input->objects[declaration] - 1] && type_large_array(types, local_type) ? 4 : 0;
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
                else if (kind == BUSTER_TYPE_KIND_ARRAY || kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION || kind == BUSTER_TYPE_KIND_ATOMIC || check_lower_aggregate(writer, local_type))
                {
                    // Arrays, structs, unions, complex values and __float128s are frame objects (aligned past 16 bytes, in a
                    // realigned frame). An incomplete type has no size at all.
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
            else if (writer->bit_ints)
            {
                check_lower_drain_bit_ints(writer);
            }
            else
            {
                check_lower_drain(writer);
            }
        }
        // Whether control reaches the function's end: a statement that falls through to it, or a branch to it.
        const bool falls = (body.return_flow && body.falls) || (writer->track ? writer->reachable || writer->label_heads[body.count] != BUSTER_CHECK_NONE :
                                                                            writer->count == 0 || (writer->kinds[writer->count - 1] != BUSTER_TYPED_RETURN &&
                                                                                                   writer->kinds[writer->count - 1] != BUSTER_TYPED_TAIL_CALL));
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
            else if (writer->bit_ints)
            {
                check_lower_drain_bit_ints(writer);
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
        if (writer->tails && status->status == CHECK_PROGRAM_OK)
        {
            // Each TAIL_CALL's row, a tail call's (Tier0Function.call_tails): a pass over the entries of a function with one.
            uint8_t* tails = BUSTER_ARENA_PUSH_ARRAY(output, uint8_t, writer->call_count);
            if (tails != 0)
            {
                memset(tails, 0, writer->call_count);
                for (uint32_t e = 0; e < writer->count; e += 1)
                {
                    if (writer->kinds[e] == BUSTER_TYPED_TAIL_CALL && writer->values[e] < writer->call_count)
                    {
                        tails[writer->values[e]] = 1;
                    }
                }
                function->call_tails = tails;
            }
            else
            {
                check_lower_fail(writer, CHECK_PROGRAM_RESOURCE);
            }
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
        BUSTER_CHECK_LOWER_COPY(label_tables, label_tables, uint32_t, writer->table_jump_count != 0 ? writer->label_entry_count : 0);
        BUSTER_CHECK_LOWER_COPY(table_jumps, table_jumps, uint32_t, writer->table_jump_count);
        function->table_count = writer->table_jump_count;
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
    memcpy(part->runtimes, writer->runtimes, (uint64_t)writer->runtime_count * sizeof(uint16_t));
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
    bool runtimes = false;
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
                lowered->runtime_order[lowered->runtime_symbol_count++] = (uint16_t)runtime;
            }
            runtimes = true;
        }
    }
    lowered->label_symbol_count = valid ? written : 0;
    // Every call of a runtime function takes its symbol (check_lower_runtime's provisional one until now).
    for (uint32_t b = 0; valid && runtimes && b < program->body_count; b += 1)
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
