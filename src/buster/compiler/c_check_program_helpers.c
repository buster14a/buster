// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Per-unit native helpers for reached atomic, terminal, floating-environment and x86 builtin nodes (the x86 ones no row
// takes, c_builtin_x86_helpers.c). Include after CheckProgram and the
// standalone builtin helper emitters. The program's sorted active-node list is the execution consumer's input;
// unselected source expressions never reserve helpers. Signatures are deduplicated in first-use order, and
// symbols follow the program's ordinary symbols. This planner never mutates Link or publishes code atoms.
//
// Calls use the helpers' raw SysV integer ABI: addresses/value bits are unsigned long, orders are int, and loads
// return unsigned long. The caller applies checked argument conversions and converts a load result to its C
// value type. Atomic/environment calls remain unknown-effect compiler barriers; terminal calls have no successor.
// Output and scratch are disjoint stable-address arenas. Failure restores both lengths and clears the result.
//
// An atomic access's object must be aligned to its size, as clang's code generation gives its address's alignment
// (c_checker.c's check_graph_atomic_alignment): a misaligned one, which clang calls libatomic for, is refused.
//
// #1059: a helper is the fallback, not the representation. Each family publishes its operation's contract
// (check_program_helper_contracts): what a caller must keep of it whatever code implements it, the language's effects
// (memory, atomicity, order, the floating environment, errno, termination) and the machine's beyond a call's (target
// features, fixed registers), and where a direct form exists, what it admits. The physical effects of a helper's code are
// x86_semantics.c's, read from its records (check_program_helper_physical), never a list of this file's. A fence of a
// constant order lowers to the typed stream's FENCE (check_program_direct_fence) and plans no helper; every other use
// keeps its helper, and the plan counts each family's direct and helper uses (CheckProgramHelpers.direct and .helper).
#ifndef BUSTER_C_CHECK_PROGRAM_HELPERS_INCLUDED
#define BUSTER_C_CHECK_PROGRAM_HELPERS_INCLUDED

#define BUSTER_CHECK_PROGRAM_HELPER_FIXED 28u
// A read/modify/write signature's key follows the others': its rule's, its width's and its format's; and an x86 leaf's
// the read/modify/write ones', its kind's.
#define BUSTER_CHECK_PROGRAM_HELPER_X86 (BUSTER_CHECK_PROGRAM_HELPER_FIXED + (uint32_t)BUSTER_ARRAY_LENGTH(check_builtin_atomic_rmw_rules) * 16u)
#define BUSTER_CHECK_PROGRAM_HELPER_SIGNATURES (BUSTER_CHECK_PROGRAM_HELPER_X86 + (uint32_t)CBUILTIN_X86_HELPER_COUNT)

typedef struct CheckProgramHelper CheckProgramHelper;
struct CheckProgramHelper
{
    CBuiltinId id;
    uint32_t bytes;
    SymbolId symbol;
    Tier0Code code;
    uint32_t parameter_count;
    TypeId parameter_types[7];
    TypeId result_type;
    // A read/modify/write leaf's (c_builtin_atomic_rmw_helpers.c) value format.
    CBuiltinAtomicRmwFormat rmw_format;
    bool rmw;
    CheckBuiltinMathKind math_kind;
    TypeId floating_type;
    bool terminates;
    bool compiler_barrier;
    bool reads_fp_environment;
    bool may_raise_fp_exceptions;
    bool may_raise_inexact;
    bool requires_errno_address;
    uint32_t required_alignment;
    // An asm statement's helper (c_check_program_asm.c): the statement's index plus one; 0 for a builtin's.
    uint32_t assembly;
    // An x86 builtin's leaf (c_builtin_x86_helpers.c), or CBUILTIN_X86_HELPER_NONE.
    CBuiltinX86HelperKind x86;
};

// The helper families (#1059's inventory), each a row of check_program_helper_contracts: an atomic load or store
// (load_n, store_n, C11's and an _Atomic object's), an atomic read/modify/write (c_builtin_atomic_rmw_helpers.c), a
// fence, a terminal builtin (trap, unreachable), FLT_ROUNDS, a math builtin (sqrt to rint, copysign), an x86 builtin's
// leaf (c_builtin_x86_helpers.c), and an asm statement with instructions (c_check_program_asm.c).
typedef enum CheckProgramHelperFamily
{
    CHECK_PROGRAM_HELPER_ATOMIC_ACCESS,
    CHECK_PROGRAM_HELPER_ATOMIC_RMW,
    CHECK_PROGRAM_HELPER_FENCE,
    CHECK_PROGRAM_HELPER_TERMINAL,
    CHECK_PROGRAM_HELPER_ROUNDS,
    CHECK_PROGRAM_HELPER_MATH,
    CHECK_PROGRAM_HELPER_X86,
    CHECK_PROGRAM_HELPER_ASM,
    CHECK_PROGRAM_HELPER_FAMILIES,
} CheckProgramHelperFamily;

// An operation's contract's effects, what a caller keeps whatever implements it (CheckProgramHelperContract.effects):
//   - READS and WRITES: memory through an address it is given (an asm statement's, its operands' and its clobber's);
//   - ATOMIC: its access is one access of its object's width, aligned to it, atomic as other threads see it;
//   - ORDERS: a memory order over the thread's other accesses (a helper strengthens every order to seq_cst);
//   - BARRIER: no access moves across it, nor is one eliminated past it: an unknown call's, the helpers' fallback;
//   - FP_ENVIRONMENT and FP_EXCEPTIONS: it reads the floating environment (MXCSR's rounding), and may raise its flags;
//   - ERRNO: it may set errno through the address it is given;
//   - TERMINATES: it doesn't return, and has no successor;
//   - FEATURES: its instructions need target features its call site's function must have;
//   - FIXED: its instructions take fixed or implicit registers past the call's convention;
//   - UNMODELED: it has effects past these (system state, a template's own), which a consumer keeps whole.
#define BUSTER_CHECK_HELPER_READS 0x0001u
#define BUSTER_CHECK_HELPER_WRITES 0x0002u
#define BUSTER_CHECK_HELPER_ATOMIC 0x0004u
#define BUSTER_CHECK_HELPER_ORDERS 0x0008u
#define BUSTER_CHECK_HELPER_BARRIER 0x0010u
#define BUSTER_CHECK_HELPER_FP_ENVIRONMENT 0x0020u
#define BUSTER_CHECK_HELPER_FP_EXCEPTIONS 0x0040u
#define BUSTER_CHECK_HELPER_ERRNO 0x0080u
#define BUSTER_CHECK_HELPER_TERMINATES 0x0100u
#define BUSTER_CHECK_HELPER_FEATURES 0x0200u
#define BUSTER_CHECK_HELPER_FIXED 0x0400u
#define BUSTER_CHECK_HELPER_UNMODELED 0x0800u

// A family's published contract: its name, the effects every one of its operations may have (each planned helper's own,
// check_program_helper_effects, are a subset), what its direct form admits (NULL where none exists yet) and what keeps its
// helper.
typedef struct CheckProgramHelperContract CheckProgramHelperContract;
struct CheckProgramHelperContract
{
    const char* name;
    uint32_t effects;
    const char* direct;
    const char* helper;
};

static const CheckProgramHelperContract check_program_helper_contracts[CHECK_PROGRAM_HELPER_FAMILIES] = {
    [CHECK_PROGRAM_HELPER_ATOMIC_ACCESS] = {
        .name = "atomic-access",
        .effects = BUSTER_CHECK_HELPER_READS | BUSTER_CHECK_HELPER_WRITES | BUSTER_CHECK_HELPER_ATOMIC | BUSTER_CHECK_HELPER_ORDERS | BUSTER_CHECK_HELPER_BARRIER,
        .direct = NULL,
        .helper = "every load and store of 1, 2, 4 or 8 bytes aligned to its size; a misaligned or wider one is refused",
    },
    [CHECK_PROGRAM_HELPER_ATOMIC_RMW] = {
        .name = "atomic-rmw",
        .effects = BUSTER_CHECK_HELPER_READS | BUSTER_CHECK_HELPER_WRITES | BUSTER_CHECK_HELPER_ATOMIC | BUSTER_CHECK_HELPER_ORDERS | BUSTER_CHECK_HELPER_BARRIER |
                   BUSTER_CHECK_HELPER_FIXED | BUSTER_CHECK_HELPER_FP_ENVIRONMENT | BUSTER_CHECK_HELPER_FP_EXCEPTIONS,
        .direct = NULL,
        .helper = "every read/modify/write of 1, 2, 4 or 8 bytes aligned to its size, of integer, bool or GNU's float and double formats",
    },
    [CHECK_PROGRAM_HELPER_FENCE] = {
        .name = "fence",
        .effects = BUSTER_CHECK_HELPER_ORDERS | BUSTER_CHECK_HELPER_BARRIER,
        .direct = "a thread or signal fence, GNU's or C11's, unscoped, and __sync_synchronize, of a constant order 0 to 5 evaluated for no effect: the typed stream's FENCE",
        .helper = "a fence of a dynamic or out-of-range order, which the helper strengthens to seq_cst, or a scoped family's",
    },
    [CHECK_PROGRAM_HELPER_TERMINAL] = {
        .name = "terminal",
        .effects = BUSTER_CHECK_HELPER_TERMINATES,
        .direct = NULL,
        .helper = "__builtin_trap and __builtin_unreachable",
    },
    [CHECK_PROGRAM_HELPER_ROUNDS] = {
        .name = "flt-rounds",
        .effects = BUSTER_CHECK_HELPER_FP_ENVIRONMENT | BUSTER_CHECK_HELPER_BARRIER,
        .direct = NULL,
        .helper = "__builtin_flt_rounds",
    },
    [CHECK_PROGRAM_HELPER_MATH] = {
        .name = "math",
        .effects = BUSTER_CHECK_HELPER_FP_ENVIRONMENT | BUSTER_CHECK_HELPER_FP_EXCEPTIONS | BUSTER_CHECK_HELPER_ERRNO | BUSTER_CHECK_HELPER_BARRIER,
        .direct = NULL,
        .helper = "a math builtin the checker doesn't fold: sqrt (with errno), the roundings and copysign",
    },
    [CHECK_PROGRAM_HELPER_X86] = {
        .name = "x86-leaf",
        .effects = BUSTER_CHECK_HELPER_READS | BUSTER_CHECK_HELPER_WRITES | BUSTER_CHECK_HELPER_ORDERS | BUSTER_CHECK_HELPER_BARRIER | BUSTER_CHECK_HELPER_FP_ENVIRONMENT |
                   BUSTER_CHECK_HELPER_FEATURES | BUSTER_CHECK_HELPER_FIXED | BUSTER_CHECK_HELPER_UNMODELED,
        .direct = NULL,
        .helper = "an x86 builtin whose instruction no Tier 0 row takes (c_builtin_x86_helpers.c), where the target has its features",
    },
    [CHECK_PROGRAM_HELPER_ASM] = {
        .name = "asm",
        .effects = BUSTER_CHECK_HELPER_READS | BUSTER_CHECK_HELPER_WRITES | BUSTER_CHECK_HELPER_ORDERS | BUSTER_CHECK_HELPER_BARRIER | BUSTER_CHECK_HELPER_FP_ENVIRONMENT |
                   BUSTER_CHECK_HELPER_FP_EXCEPTIONS | BUSTER_CHECK_HELPER_FIXED | BUSTER_CHECK_HELPER_UNMODELED,
        .direct = NULL,
        .helper = "an asm statement with instructions, its operands placed by their constraints in the helper's frame (docs/c-inline-assembly.md)",
    },
};

typedef struct CheckProgramHelperUse CheckProgramHelperUse;
struct CheckProgramHelperUse
{
    uint32_t node;
    uint32_t helper;
};

typedef struct CheckProgramHelpers CheckProgramHelpers;
struct CheckProgramHelpers
{
    CheckProgramHelper* items;
    uint32_t count;
    CheckProgramHelperUse* uses;
    uint32_t use_count;
    // Each family's uses that lower directly, planning no helper (#1059: check_program_direct_fence's), and those that
    // call their helper (the builtins' uses, `uses`; an asm statement's are its plan's, check_program_asm_plan).
    uint32_t direct[CHECK_PROGRAM_HELPER_FAMILIES];
    uint32_t helper[CHECK_PROGRAM_HELPER_FAMILIES];
    CheckProgramStatus status;
    uint32_t token;
    // What an UNSUPPORTED status lacks, or NULL.
    const char* reason;
};

// Whether a type is a 16-byte vector of elements other than bool, as a leaf's vector parameters and result are
// (CBuiltinX86HelperInfo.vectors): a value Tier 0 passes in an XMM register.
static bool check_program_helper_vector(const TypeTable* types, TypeId type)
{
    const TypeId base = type_conversion_operand(types, type);
    const uint8_t kind = type_kind(types, base);
    const bool vector = (kind == BUSTER_TYPE_KIND_VECTOR || kind == BUSTER_TYPE_KIND_EXT_VECTOR) && type_size(types, base) == 16;
    return vector && type_unqualified(type_strip_aligned(types, type_element(types, base))) != BUSTER_TYPE_BOOL;
}

// A planned helper's family (check_program_helper_contracts' row).
static CheckProgramHelperFamily check_program_helper_family(const CheckProgramHelper* helper)
{
    const bool fence = helper->id == CBUILTIN_ATOMIC_THREAD_FENCE || helper->id == CBUILTIN_ATOMIC_SIGNAL_FENCE || helper->id == CBUILTIN_SYNC_SYNCHRONIZE;
    return helper->assembly != 0                   ? CHECK_PROGRAM_HELPER_ASM :
           helper->x86 != CBUILTIN_X86_HELPER_NONE ? CHECK_PROGRAM_HELPER_X86 :
           helper->terminates                      ? CHECK_PROGRAM_HELPER_TERMINAL :
           helper->math_kind != CHECK_BUILTIN_MATH_NONE ? CHECK_PROGRAM_HELPER_MATH :
           helper->id == CBUILTIN_FLT_ROUNDS       ? CHECK_PROGRAM_HELPER_ROUNDS :
           helper->rmw                             ? CHECK_PROGRAM_HELPER_ATOMIC_RMW :
           fence                                   ? CHECK_PROGRAM_HELPER_FENCE :
                                                     CHECK_PROGRAM_HELPER_ATOMIC_ACCESS;
}

// A planned helper's own effects within its family's contract: a load's read, a store's write, a release's or a clear's
// write (a read/modify/write leaf's without a result), the floating environment and exceptions of clang's IEEE maximum
// and minimum (whose maxss, minss and ucomiss read MXCSR and may raise invalid) but no other read/modify/write's, and the
// floating environment, exceptions and errno a math builtin's plan has.
static __attribute__((unused)) uint32_t check_program_helper_effects(const CheckProgramHelper* helper)
{
    const CheckProgramHelperFamily family = check_program_helper_family(helper);
    uint32_t effects = check_program_helper_contracts[family].effects;
    if (family == CHECK_PROGRAM_HELPER_ATOMIC_ACCESS)
    {
        effects &= helper->id == CBUILTIN_ATOMIC_LOAD_N ? ~BUSTER_CHECK_HELPER_WRITES : ~BUSTER_CHECK_HELPER_READS;
    }
    else if (family == CHECK_PROGRAM_HELPER_ATOMIC_RMW)
    {
        const CheckBuiltinAtomicRmwRule* rule = check_builtin_atomic_rmw_rule(helper->id);
        const bool ieee = rule != NULL && check_builtin_atomic_rmw_ieee(rule->operation);
        effects &= (helper->result_type == BUSTER_TYPE_VOID ? ~BUSTER_CHECK_HELPER_READS : ~0u) &
                   (ieee ? ~0u : ~(BUSTER_CHECK_HELPER_FP_ENVIRONMENT | BUSTER_CHECK_HELPER_FP_EXCEPTIONS));
    }
    else if (family == CHECK_PROGRAM_HELPER_MATH)
    {
        effects &= (helper->reads_fp_environment ? ~0u : ~BUSTER_CHECK_HELPER_FP_ENVIRONMENT) &
                   (helper->may_raise_fp_exceptions ? ~0u : ~BUSTER_CHECK_HELPER_FP_EXCEPTIONS) & (helper->requires_errno_address ? ~0u : ~BUSTER_CHECK_HELPER_ERRNO);
    }
    return effects;
}

// A helper's code's physical effects (#1059), read from its records by x86_semantics.c (x86_instructions_effects and
// x86_effects), not from a list of this file's: the resources its records read and write (BUSTER_X86_RESOURCE_*: general
// and vector registers, flags, MXCSR, the x87's control word), their forms' memory effects (BUSTER_X86_MEMORY_EFFECT_*:
// a fence, a locked access) and properties (BUSTER_X86_EFFECT_*: a fault, a floating exception), whether it reads or
// writes memory through an operand other than its own stack's (an object a pointer names), and whether it returns or
// traps; all of its covered records'. A record of a form the tables don't cover has only the conservative record (every
// resource, every effect), which says nothing of it: it is counted apart (`uncovered`, the first's form), and a consumer
// takes a helper with one as having unknown effects. `valid` is false where its scratch had no room for the resolved
// effects.
typedef struct CheckProgramHelperPhysical CheckProgramHelperPhysical;
struct CheckProgramHelperPhysical
{
    uint64_t reads;
    uint64_t writes;
    uint16_t properties;
    uint8_t memory;
    bool reads_object;
    bool writes_object;
    bool returns;
    bool traps;
    bool valid;
    uint32_t uncovered;
    uint16_t uncovered_form;
};

static __attribute__((unused)) CheckProgramHelperPhysical check_program_helper_physical(const Tier0Code* code, Arena* scratch)
{
    CheckProgramHelperPhysical physical = { 0 };
    const uint64_t mark = scratch->length;
    uint64_t* reads = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, (uint64_t)code->count + 1);
    uint64_t* writes = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, (uint64_t)code->count + 1);
    physical.valid = reads != NULL && writes != NULL;
    if (physical.valid)
    {
        x86_instructions_effects(code->records, code->count, reads, writes);
    }
    for (uint32_t r = 0; physical.valid && r < code->count; r += 1)
    {
        const X86Instruction* record = &code->records[r];
        const X86Effects* effects = x86_effects(record->form & BUSTER_X86_FORM_NUMBER);
        if (effects->operation == X86_OPERATION_NONE)
        {
            physical.uncovered_form = physical.uncovered == 0 ? (uint16_t)(record->form & BUSTER_X86_FORM_NUMBER) : physical.uncovered_form;
            physical.uncovered += 1;
        }
        else
        {
            const bool stack = (record->mode & BUSTER_X86_MODE_BASE) != 0 && (record->rm & 15) == X86_REGISTER_RSP;
            const bool object = (record->mode & BUSTER_X86_MODE_MEMORY) != 0 && !stack && (effects->memory & BUSTER_X86_MEMORY_EFFECT_ADDRESS) == 0;
            const uint32_t control = x86_effects_control(effects);
            physical.reads |= reads[r];
            physical.writes |= writes[r];
            physical.properties |= effects->properties;
            physical.memory |= effects->memory;
            physical.reads_object |= object && (reads[r] & BUSTER_X86_RESOURCE_MEMORY) != 0;
            physical.writes_object |= object && (writes[r] & BUSTER_X86_RESOURCE_MEMORY) != 0;
            physical.returns |= control == BUSTER_X86_CONTROL_RETURN;
            physical.traps |= control == BUSTER_X86_CONTROL_TRAP || (effects->properties & BUSTER_X86_EFFECT_FAULT) != 0;
        }
    }
    arena_pop_to(scratch, mark);
    return physical;
}

// A fence that lowers to the typed stream's FENCE rather than its helper's call (#1059, CHECK_PROGRAM_HELPER_FENCE's
// direct form): __atomic_thread_fence, __atomic_signal_fence, __c11_atomic_thread_fence and __c11_atomic_signal_fence,
// unscoped, of an order the checker knows (CheckBuiltinAtomicEffect.known_orders) that is one of C's (0 to 5) and whose
// argument evaluates for no effect, and __sync_synchronize; `effect` is the node's. Returns its FENCE value (the order,
// with BUSTER_TYPED_FENCE_SIGNAL for a signal fence), or UINT32_MAX for one its helper takes (a dynamic or out-of-range
// order, which the helper strengthens to seq_cst where clang emits a switch or nothing, or a scoped family's, whose scope
// it evaluates) and for any other node. The planner and the lowering ask it alike.
static uint32_t check_program_direct_fence(const CheckProgram* program, uint32_t node, const CheckBuiltinAtomicEffect* effect)
{
    const CheckGraph* graph = &program->input->graph;
    const CBuiltinId id = (CBuiltinId)graph->members[node];
    const bool synchronize = id == CBUILTIN_SYNC_SYNCHRONIZE;
    const bool fence = id == CBUILTIN_ATOMIC_THREAD_FENCE || id == CBUILTIN_ATOMIC_SIGNAL_FENCE || id == CBUILTIN_C11_ATOMIC_THREAD_FENCE ||
                       id == CBUILTIN_C11_ATOMIC_SIGNAL_FENCE;
    const uint32_t arguments = graph->argument_counts != NULL ? graph->argument_counts[node] : BUSTER_CHECK_NONE;
    uint32_t value = UINT32_MAX;
    if (effect != NULL && effect->node == node && effect->unsupported == 0 && ((synchronize && arguments == 0) || (fence && arguments == 1)) &&
        (effect->known_orders & BUSTER_CHECK_BUILTIN_ATOMIC_KNOWN_SUCCESS_ORDER) != 0 && effect->success_order <= BUSTER_TYPED_FENCE_SEQ_CST &&
        effect->fence_scope != CHECK_BUILTIN_ATOMIC_FENCE_NONE)
    {
        const uint32_t order = synchronize ? BUSTER_CHECK_NONE : graph->argument_nodes[graph->argument_offsets[node]];
        const bool quiet = synchronize || (check_stream_flags(program->stream, order) & BUSTER_CHECK_SIDE_EFFECT) == 0;
        value = quiet ? effect->success_order | (effect->fence_scope == CHECK_BUILTIN_ATOMIC_FENCE_SIGNAL ? BUSTER_TYPED_FENCE_SIGNAL : 0) : UINT32_MAX;
    }
    return value;
}

static uint32_t check_program_helper_key(CBuiltinId id, uint64_t bytes, bool rmw, CBuiltinAtomicRmwFormat format)
{
    uint32_t key = UINT32_MAX;
    const CheckBuiltinAtomicRmwRule* rule = rmw ? check_builtin_atomic_rmw_rule(id) : NULL;
    const uint32_t width = bytes == 1 ? 0 : bytes == 2 ? 1 : bytes == 4 ? 2 : 3;
    // A floating format's width is its type's.
    const uint32_t layout = format == CBUILTIN_ATOMIC_RMW_INTEGER_BITS ? 0 : format == CBUILTIN_ATOMIC_RMW_BOOL_BITS ? 1 :
                            format == CBUILTIN_ATOMIC_RMW_SIGNED_BITS ? 2 :
                            format == CBUILTIN_ATOMIC_RMW_F32_BITS || format == CBUILTIN_ATOMIC_RMW_F64_BITS ? 3 : UINT32_MAX;
    if (rule != NULL && (bytes == 1 || bytes == 2 || bytes == 4 || bytes == 8) && layout != UINT32_MAX)
    {
        key = BUSTER_CHECK_PROGRAM_HELPER_FIXED + ((uint32_t)(rule - check_builtin_atomic_rmw_rules) * 4 + width) * 4 + layout;
    }
    else if (!rmw && (id == CBUILTIN_ATOMIC_LOAD_N || id == CBUILTIN_ATOMIC_STORE_N) &&
             (bytes == 1 || bytes == 2 || bytes == 4 || bytes == 8))
    {
        key = (id == CBUILTIN_ATOMIC_STORE_N ? 4 : 0) + width;
    }
    else if (!rmw && bytes == 0)
    {
        switch (id)
        {
            case CBUILTIN_ATOMIC_THREAD_FENCE: key = 8; break;
            case CBUILTIN_ATOMIC_SIGNAL_FENCE: key = 9; break;
            case CBUILTIN_SYNC_SYNCHRONIZE: key = 10; break;
            case CBUILTIN_TRAP: key = 11; break;
            case CBUILTIN_UNREACHABLE: key = 12; break;
            case CBUILTIN_FLT_ROUNDS: key = 13; break;
            default: break;
        }
        if (id >= CBUILTIN_SQRT && id <= CBUILTIN_RINTF)
        {
            key = 14 + (uint32_t)(id - CBUILTIN_SQRT);
        }
    }
    return key;
}

static CheckProgramStatus check_program_helper_math(const CheckProgram* program, const CheckBuiltinFloatEffect* effect, CBuiltinId id, CheckProgramHelper* helper)
{
    const CheckBuiltinMathKind kind = check_builtin_math_kind(id);
    const bool copy = kind == CHECK_BUILTIN_MATH_COPYSIGN;
    const bool square_root = kind == CHECK_BUILTIN_MATH_SQRT;
    const uint32_t arguments = copy ? 2 : 1;
    const uint32_t mask = (1u << arguments) - 1;
    const TypeId floating = check_builtin_math_parameter(id);
    CheckProgramStatus status = CHECK_PROGRAM_OK;
    if (effect == NULL || effect->runtime_builtin != id || effect->math_kind != kind || kind == CHECK_BUILTIN_MATH_NONE ||
        effect->floating_type != floating || effect->evaluated_operands != mask || effect->definite_operands != mask ||
        effect->folded || effect->may_set_errno != square_root ||
        (!copy && (!effect->compiler_barrier || !effect->reads_fp_environment || !effect->may_raise_fp_exceptions)) ||
        ((square_root || kind == CHECK_BUILTIN_MATH_RINT) && !effect->may_raise_inexact))
    {
        status = CHECK_PROGRAM_INVALID;
    }
    else if (effect->unsupported != 0)
    {
        status = CHECK_PROGRAM_UNSUPPORTED;
    }
    else if (square_root && (program->types == NULL || !type_is_valid(program->types, effect->errno_pointer_type) ||
                            check_action_kind(program->types, effect->errno_pointer_type) != BUSTER_TYPE_KIND_POINTER ||
                            type_element(program->types, effect->errno_pointer_type) != BUSTER_TYPE_INT))
    {
        status = CHECK_PROGRAM_INVALID;
    }
    for (uint32_t i = 0; status == CHECK_PROGRAM_OK && i < arguments; i += 1)
    {
        if (effect->converted_arguments[i].type != floating || effect->converted_arguments[i].category != CHECK_RVALUE)
        {
            status = CHECK_PROGRAM_INVALID;
        }
    }
    if (status == CHECK_PROGRAM_OK)
    {
        *helper = (CheckProgramHelper){
            .id = id,
            .parameter_count = copy || square_root ? 2 : 1,
            .parameter_types = {
                BUSTER_TYPE_ULONG,
                copy || square_root ? BUSTER_TYPE_ULONG : BUSTER_TYPE_VOID,
                BUSTER_TYPE_VOID,
            },
            .result_type = BUSTER_TYPE_ULONG,
            .math_kind = kind,
            .floating_type = floating,
            .compiler_barrier = true,
            .reads_fp_environment = effect->reads_fp_environment,
            .may_raise_fp_exceptions = effect->may_raise_fp_exceptions,
            .may_raise_inexact = effect->may_raise_inexact,
            .requires_errno_address = square_root,
        };
    }
    return status;
}

// What an atomic call's unsupported bits, or its object's alignment, lack, for the refusal.
static const char* check_program_helper_atomic_reason(const CheckBuiltinAtomicEffect* effect)
{
    const uint32_t bits = effect->unsupported;
    const char* reason = NULL;
    if ((bits & BUSTER_CHECK_BUILTIN_ATOMIC_UNSUPPORTED_WIDTH) != 0)
    {
        reason = "atomic objects of other than 1, 2, 4 or 8 bytes";
    }
    else if ((bits & BUSTER_CHECK_BUILTIN_ATOMIC_UNSUPPORTED_UNKNOWN_ALIGNMENT) != 0 || !effect->alignment_known)
    {
        reason = "an atomic object whose address's alignment this doesn't work out";
    }
    else if ((bits & BUSTER_CHECK_BUILTIN_ATOMIC_UNSUPPORTED_ALIGNMENT) != 0 || effect->minimum_alignment < effect->object_size)
    {
        reason = "a misaligned atomic object, which clang calls libatomic for";
    }
    else if ((bits & BUSTER_CHECK_BUILTIN_ATOMIC_RMW_UNSUPPORTED_FP_RMW) != 0)
    {
        reason = "atomic arithmetic, minimum or maximum of floating values";
    }
    else if ((bits & BUSTER_CHECK_BUILTIN_ATOMIC_UNSUPPORTED_FLOAT_FORMAT) != 0)
    {
        reason = "atomic operations on floating values other than float and double";
    }
    else if ((bits & BUSTER_CHECK_BUILTIN_ATOMIC_RMW_UNSUPPORTED_BOOL_RMW) != 0)
    {
        reason = "atomic arithmetic or NAND of bool";
    }
    else if ((bits & BUSTER_CHECK_BUILTIN_ATOMIC_RMW_UNSUPPORTED_SUBBYTE_INTEGER) != 0)
    {
        reason = "atomic read-modify-write of an integer narrower than a byte";
    }
    else if ((bits & BUSTER_CHECK_BUILTIN_ATOMIC_UNSUPPORTED_CONVERSION) != 0)
    {
        reason = "an atomic builtin's complex or extended floating value";
    }
    return reason;
}

// A read/modify/write's leaf (c_builtin_atomic_rmw_helpers.c), of its builtin, width and value format: integer and pointer
// bits, bool's 0 or 1, for min and max a signed integer's, and for GNU's exchange and clang's IEEE maximum and minimum a
// float's or a double's (the lowering passes and takes their bits, check_lower_helper). A call passes the object's address and its fixed arguments
// (GNU's compare-exchange the expected object's address), the orders and GNU's weak flag as ints, and takes the old or new
// bits, or whether a compare-exchange stored or a test_and_set's byte was set (a legacy release and a clear nothing). A
// legacy call's protected variables aren't taken.
static CheckProgramStatus check_program_helper_rmw(const CheckProgram* program, const CheckBuiltinAtomicEffect* effect, const CheckBuiltinAtomicRmwRule* rule,
                                                    uint32_t arguments, CheckProgramHelper* helper, const char** reason)
{
    const TypeTable* types = program->types;
    CheckProgramStatus status = CHECK_PROGRAM_OK;
    const bool compare = rule->operation == CHECK_BUILTIN_ATOMIC_RMW_COMPARE_EXCHANGE;
    const bool ordered = rule->operation == CHECK_BUILTIN_ATOMIC_RMW_MIN || rule->operation == CHECK_BUILTIN_ATOMIC_RMW_MAX;
    const bool valid_type = types != NULL && type_is_valid(types, effect->value_type);
    uint32_t width = 0;
    bool is_signed = false;
    const bool integer = valid_type && check_action_integer_type(types, effect->value_type, &width, &is_signed);
    const bool pointer = valid_type && check_action_kind(types, effect->value_type) == BUSTER_TYPE_KIND_POINTER;
    const TypeId plain = valid_type ? type_unqualified(type_strip_aligned(types, effect->value_type)) : 0;
    const bool floating = (rule->id == CBUILTIN_ATOMIC_EXCHANGE_N || check_builtin_atomic_rmw_ieee(rule->operation)) &&
                          (plain == BUSTER_TYPE_FLOAT || plain == BUSTER_TYPE_DOUBLE);
    // A legacy release is a store, no read/modify/write of its object, and so is a clear.
    const bool release = rule->operation == CHECK_BUILTIN_ATOMIC_RMW_RELEASE || rule->operation == CHECK_BUILTIN_ATOMIC_RMW_CLEAR;
    if (effect->runtime_builtin != rule->id || !effect->compiler_barrier || effect->rmw == release || !valid_type ||
        effect->evaluated_operands != (UINT32_C(1) << rule->fixed_count) - 1 || arguments == BUSTER_CHECK_NONE)
    {
        status = CHECK_PROGRAM_INVALID;
    }
    else if (arguments != rule->fixed_count)
    {
        status = CHECK_PROGRAM_UNSUPPORTED;
        *reason = "a legacy atomic builtin's protected variables";
    }
    else if (effect->unsupported != 0 || !effect->alignment_known || effect->minimum_alignment < effect->object_size)
    {
        status = CHECK_PROGRAM_UNSUPPORTED;
        *reason = check_program_helper_atomic_reason(effect);
    }
    else if (!integer && !pointer && !floating)
    {
        status = CHECK_PROGRAM_UNSUPPORTED;
        *reason = "atomic read-modify-write of floating values but GNU's exchange and clang's IEEE maximum and minimum";
    }
    if (status == CHECK_PROGRAM_OK)
    {
        const bool boolean = plain == BUSTER_TYPE_BOOL;
        *helper = (CheckProgramHelper){
            .id = rule->id,
            .bytes = (uint32_t)effect->object_size,
            .parameter_count = rule->fixed_count,
            .result_type = release ? BUSTER_TYPE_VOID : BUSTER_TYPE_ULONG,
            .compiler_barrier = true,
            .required_alignment = (uint32_t)effect->object_size,
            .rmw_format = boolean ? CBUILTIN_ATOMIC_RMW_BOOL_BITS : plain == BUSTER_TYPE_FLOAT && floating ? CBUILTIN_ATOMIC_RMW_F32_BITS :
                          floating ? CBUILTIN_ATOMIC_RMW_F64_BITS : ordered && is_signed ? CBUILTIN_ATOMIC_RMW_SIGNED_BITS : CBUILTIN_ATOMIC_RMW_INTEGER_BITS,
            .rmw = true,
        };
        // The byte flag's order is its second argument.
        const uint32_t first_int = rule->legacy ? rule->fixed_count : compare ? 3 : check_builtin_atomic_rmw_byte(rule->operation) ? 1 : 2;
        for (uint32_t i = 0; i < rule->fixed_count; i += 1)
        {
            helper->parameter_types[i] = i >= first_int ? BUSTER_TYPE_INT : BUSTER_TYPE_ULONG;
        }
    }
    return status;
}

static CheckProgramStatus check_program_helper_atomic(const CheckProgram* program, const CheckBuiltinAtomicEffect* effect, CBuiltinId id, uint32_t arguments,
                                                       CheckProgramHelper* helper, const char** reason)
{
    CheckProgramStatus status = CHECK_PROGRAM_OK;
    const CheckBuiltinAtomicRmwRule* rule = check_builtin_atomic_rmw_rule(id);
    const bool memory = id == CBUILTIN_ATOMIC_LOAD_N || id == CBUILTIN_ATOMIC_STORE_N;
    const bool load = id == CBUILTIN_ATOMIC_LOAD_N;
    const bool store = id == CBUILTIN_ATOMIC_STORE_N;
    const uint32_t parameters = load ? 2 : store ? 3 : id == CBUILTIN_SYNC_SYNCHRONIZE ? 0 : 1;
    if (rule != NULL)
    {
        status = check_program_helper_rmw(program, effect, rule, arguments, helper, reason);
    }
    else if (effect->runtime_builtin != id || !effect->compiler_barrier ||
             effect->evaluated_operands != (UINT32_C(1) << parameters) - 1 || effect->rmw)
    {
        status = CHECK_PROGRAM_INVALID;
    }
    else if (effect->unsupported != 0 || check_program_helper_key(id, effect->object_size, false, CBUILTIN_ATOMIC_RMW_INTEGER_BITS) == UINT32_MAX ||
             (memory && (!effect->alignment_known || effect->minimum_alignment < effect->object_size)))
    {
        status = CHECK_PROGRAM_UNSUPPORTED;
        *reason = check_program_helper_atomic_reason(effect);
    }
    else if ((memory && (effect->required_alignment != effect->object_size || effect->minimum_alignment == 0 ||
                         (effect->minimum_alignment & (effect->minimum_alignment - 1)) != 0)) ||
             (!memory && effect->required_alignment != 0))
    {
        status = CHECK_PROGRAM_INVALID;
    }
    if (rule == NULL && status == CHECK_PROGRAM_OK)
    {
        *helper = (CheckProgramHelper){
            .id = id,
            .bytes = (uint32_t)effect->object_size,
            .parameter_count = parameters,
            .parameter_types = {
                memory ? BUSTER_TYPE_ULONG : parameters != 0 ? BUSTER_TYPE_INT : BUSTER_TYPE_VOID,
                load ? BUSTER_TYPE_INT : store ? BUSTER_TYPE_ULONG : BUSTER_TYPE_VOID,
                store ? BUSTER_TYPE_INT : BUSTER_TYPE_VOID,
            },
            .result_type = load ? BUSTER_TYPE_ULONG : BUSTER_TYPE_VOID,
            .compiler_barrier = true,
            .required_alignment = memory ? (uint32_t)effect->object_size : 0,
        };
    }
    return status;
}

static bool check_program_helpers(CheckProgramHelpers* result, const CheckProgram* program, Arena* output, Arena* scratch)
{
    *result = (CheckProgramHelpers){ .token = BUSTER_CHECK_NONE };
    const uint64_t mark = output != 0 ? output->length : 0;
    const uint64_t temporary = scratch != 0 ? scratch->length : 0;
    bool valid = program != 0 && program->status == CHECK_PROGRAM_OK && program->input != 0 && program->stream != 0 &&
                 output != 0 && scratch != 0 && output != scratch && output->length <= output->capacity && scratch->length <= scratch->capacity &&
                 (output->base != 0 || output->capacity == 0) && (scratch->base != 0 || scratch->capacity == 0);
    if (valid)
    {
        const CheckGraph* graph = &program->input->graph;
        const CheckStream* stream = program->stream;
        valid = stream->status == CHECK_STATUS_OK && stream->report_count == 0 && stream->count == graph->count &&
                (program->builtin_count == 0 || (program->builtin_nodes != 0 && graph->ops != 0 && graph->members != 0 && graph->tokens != 0)) &&
                (stream->atomic_count == 0 || stream->atomics != 0) && (stream->misc_count == 0 || stream->misc != 0) &&
                (stream->floating_count == 0 || stream->floating != 0) && (stream->x86_count == 0 || stream->x86 != 0);
    }
    if (!valid)
    {
        result->status = CHECK_PROGRAM_INVALID;
    }
    uint32_t implicit_widths = 0;
    uint32_t implicit_node = 0;
    bool atomic_types = false;
    for (uint32_t t = 0; valid && program->types != NULL && !atomic_types && t < program->types->types.count; t += 1)
    {
        atomic_types = program->types->types.kind[t] == BUSTER_TYPE_KIND_ATOMIC;
    }
    const CheckGraph* graph = valid ? &program->input->graph : NULL;
    for (uint32_t n = 0; atomic_types && n < graph->count; n += 1)
    {
        const CheckValue checked_value = check_stream_value(program->stream, n);
        const CheckValue* value = &checked_value;
        const TypeId type = type_strip_aligned(program->types, value->type);
        if (value->category == CHECK_LVALUE && type_kind(program->types, type) == BUSTER_TYPE_KIND_ATOMIC &&
            program->parser != NULL && check_program_token_body(program, graph->tokens[n]) != 0)
        {
            const uint64_t bytes = type_size(program->types, type);
            const TypeId plain = type_conversion_operand(program->types, type);
            if (check_builtin_atomic_c11_native(program->types, CBUILTIN_C11_ATOMIC_LOAD, plain, bytes, true))
            {
                implicit_widths |= 1u << (bytes == 1 ? 0 : bytes == 2 ? 1 : bytes == 4 ? 2 : 3);
                implicit_node = n;
            }
        }
    }
    for (uint32_t a = 0; valid && a < program->stream->atomic_count; a += 1)
    {
        // C11's builtins and GNU's generic forms (a scoped family's of them too) use the implicit widths' leaves where their
        // objects are native ones (check_lower_atomic_prepare).
        const CheckBuiltinAtomicEffect* effect = program->stream->atomics + a;
        const CBuiltinId id = check_builtin_atomic_base((CBuiltinId)graph->members[effect->node]);
        if ((check_builtin_atomic_c11(id) || check_builtin_atomic_generic(id)) && effect->object_type != 0 &&
            check_builtin_atomic_c11_native(program->types, CBUILTIN_C11_ATOMIC_LOAD, effect->value_type, effect->object_size,
                                            effect->alignment_known && effect->minimum_alignment >= effect->object_size))
        {
            const uint64_t bytes = effect->object_size;
            implicit_widths |= 1u << (bytes == 1 ? 0 : bytes == 2 ? 1 : bytes == 4 ? 2 : 3);
            implicit_node = effect->node;
        }
    }
    uint32_t keys[BUSTER_CHECK_PROGRAM_HELPER_SIGNATURES] = { 0 };
    // At most a signature per builtin node, and per key.
    const uint64_t candidates = valid ? (uint64_t)program->builtin_count + (uint32_t)__builtin_popcount(implicit_widths) * 4 : 0;
    const uint32_t signatures = candidates < BUSTER_CHECK_PROGRAM_HELPER_SIGNATURES ? (uint32_t)candidates : BUSTER_CHECK_PROGRAM_HELPER_SIGNATURES;
    CheckProgramHelperUse* uses = valid && program->builtin_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckProgramHelperUse, program->builtin_count) : 0;
    CheckProgramHelper* planned = signatures != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckProgramHelper, signatures) : 0;
    uint32_t* first_nodes = planned != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, signatures) : 0;
    if (valid && ((program->builtin_count != 0 && uses == NULL) || (signatures != 0 && first_nodes == NULL)))
    {
        result->status = CHECK_PROGRAM_RESOURCE;
    }
    // Ordinary _Atomic expressions have no explicit builtin node. Reserve the existing raw-bit leaves once per
    // represented width; their object accesses use these same unknown-effect call boundaries as explicit builtins.
    const CBuiltinId implicit_ids[] = { CBUILTIN_ATOMIC_LOAD_N, CBUILTIN_ATOMIC_STORE_N, CBUILTIN_ATOMIC_COMPARE_EXCHANGE_N, CBUILTIN_ATOMIC_EXCHANGE_N };
    for (uint32_t w = 0; result->status == CHECK_PROGRAM_OK && w < 4; w += 1)
    {
        for (uint32_t operation = 0; (implicit_widths & (1u << w)) != 0 && operation < BUSTER_ARRAY_LENGTH(implicit_ids); operation += 1)
        {
            const CBuiltinId id = implicit_ids[operation];
            const bool compare = operation == 2;
            const bool rmw = operation >= 2;
            const uint32_t key = check_program_helper_key(id, 1u << w, rmw, CBUILTIN_ATOMIC_RMW_INTEGER_BITS);
            if (keys[key] == 0)
            {
                CheckProgramHelper* helper = planned + result->count;
                const uint32_t count = compare ? 6 : operation == 0 ? 2 : 3;
                *helper = (CheckProgramHelper){
                    .id = id,
                    .bytes = 1u << w,
                    .parameter_count = count,
                    .result_type = operation == 1 ? BUSTER_TYPE_VOID : BUSTER_TYPE_ULONG,
                    .compiler_barrier = true,
                    .required_alignment = 1u << w,
                    .rmw_format = CBUILTIN_ATOMIC_RMW_INTEGER_BITS,
                    .rmw = rmw,
                };
                for (uint32_t p = 0; p < count; p += 1)
                {
                    helper->parameter_types[p] = p >= (compare ? 3 : operation == 0 ? 1 : 2) ? BUSTER_TYPE_INT : BUSTER_TYPE_ULONG;
                }
                first_nodes[result->count] = implicit_node;
                keys[key] = ++result->count;
            }
        }
    }
    uint32_t atomic = 0;
    uint32_t misc = 0;
    uint32_t floating = 0;
    uint32_t x86 = 0;
    for (uint32_t i = 0; result->status == CHECK_PROGRAM_OK && i < program->builtin_count; i += 1)
    {
        const CheckGraph* graph = &program->input->graph;
        const CheckStream* stream = program->stream;
        const uint32_t node = program->builtin_nodes[i];
        if (node >= graph->count || (i != 0 && node <= program->builtin_nodes[i - 1]))
        {
            result->status = CHECK_PROGRAM_INVALID;
        }
        else
        {
            const CheckOp op = graph->ops[node];
            const CBuiltinId id = (CBuiltinId)graph->members[node];
            const bool terminal = op == CHECK_OP_BUILTIN_MISC && (id == CBUILTIN_TRAP || id == CBUILTIN_UNREACHABLE);
            const bool rounds = op == CHECK_OP_BUILTIN_FLOAT && id == CBUILTIN_FLT_ROUNDS;
            const bool math = op == CHECK_OP_BUILTIN_FLOAT && check_builtin_math_kind(id) != CHECK_BUILTIN_MATH_NONE;
            // An x86 builtin's leaf, by its name.
            CBuiltinX86HelperKind leaf = CBUILTIN_X86_HELPER_NONE;
            if (op == CHECK_OP_BUILTIN_X86)
            {
                while (x86 < stream->x86_count && stream->x86[x86].node < node)
                {
                    x86 += 1;
                }
                const BuiltinIntrinsicInfo* info = x86 < stream->x86_count && stream->x86[x86].node == node ? stream->x86[x86].info : NULL;
                leaf = info != NULL ? cbuiltin_x86_helper_kind(info->name, info->name_length) : CBUILTIN_X86_HELPER_NONE;
            }
            if (op == CHECK_OP_BUILTIN_ATOMIC || terminal || rounds || math || leaf != CBUILTIN_X86_HELPER_NONE)
            {
                CheckProgramHelper helper = { 0 };
                bool folded = false;
                if (leaf != CBUILTIN_X86_HELPER_NONE)
                {
                    // Its arguments as unsigned longs, its result one or none, but its builtin's 16-byte vectors where
                    // the leaf takes or gives them (CBuiltinX86HelperInfo.vectors), the arguments' types at the
                    // builtin's prototype's (one for every use).
                    const CBuiltinX86HelperInfo* info = cbuiltin_x86_helper_infos + leaf;
                    const bool output = info->amx != 0 && info->output_bytes != 0;
                    if (graph->argument_counts == 0 || graph->argument_counts[node] + output != info->parameter_count ||
                        info->parameter_count > BUSTER_ARRAY_LENGTH(helper.parameter_types))
                    {
                        result->status = CHECK_PROGRAM_INVALID;
                    }
                    else
                    {
                        const uint32_t first = graph->argument_offsets[node];
                        const bool vector_result = (info->vectors & BUSTER_CBUILTIN_X86_VECTOR_RESULT) != 0;
                        helper = (CheckProgramHelper){
                            .id = id,
                            .parameter_count = info->parameter_count,
                            .result_type = vector_result ? check_stream_type(stream, node) : info->result ? BUSTER_TYPE_ULONG : BUSTER_TYPE_VOID,
                            .compiler_barrier = true,
                            .x86 = leaf,
                        };
                        bool vectors = !vector_result || check_program_helper_vector(program->types, helper.result_type);
                        for (uint32_t a = 0; a < BUSTER_ARRAY_LENGTH(helper.parameter_types); a += 1)
                        {
                            const bool parameter = a < info->parameter_count;
                            const bool vector = (info->vectors >> a & 1) != 0;
                            const TypeId type = !parameter ? BUSTER_TYPE_VOID : vector ? stream->argument_conversions[first + a].type : BUSTER_TYPE_ULONG;
                            helper.parameter_types[a] = type;
                            vectors &= !parameter || !vector || check_program_helper_vector(program->types, type);
                        }
                        result->status = vectors ? CHECK_PROGRAM_OK : CHECK_PROGRAM_INVALID;
                    }
                }
                else if (terminal)
                {
                    while (misc < stream->misc_count && stream->misc[misc].node < node)
                    {
                        misc += 1;
                    }
                    if (misc == stream->misc_count || stream->misc[misc].node != node || stream->misc[misc].runtime_builtin != id ||
                        !stream->misc[misc].terminates || stream->misc[misc].evaluated_operands != 0 || stream->misc[misc].unsupported != 0)
                    {
                        result->status = CHECK_PROGRAM_INVALID;
                    }
                    else
                    {
                        helper = (CheckProgramHelper){
                            .id = id,
                            .result_type = BUSTER_TYPE_VOID,
                            .terminates = true,
                        };
                    }
                }
                else if (rounds || math)
                {
                    while (floating < stream->floating_count && stream->floating[floating].node < node)
                    {
                        floating += 1;
                    }
                    const CheckBuiltinFloatEffect* effect = floating < stream->floating_count ? stream->floating + floating : 0;
                    if (math)
                    {
                        if (effect == NULL || effect->node != node)
                        {
                            result->status = CHECK_PROGRAM_INVALID;
                        }
                        else if (effect->folded && effect->runtime_builtin == CBUILTIN_NONE && effect->math_kind == CHECK_BUILTIN_MATH_COPYSIGN &&
                                 effect->floating_type == check_builtin_math_parameter(id) && effect->unsupported == 0 && effect->evaluated_operands == 3 &&
                                 effect->definite_operands == 3 && !effect->compiler_barrier && !effect->reads_fp_environment && !effect->may_set_errno)
                        {
                            folded = true;
                        }
                        else
                        {
                            result->status = check_program_helper_math(program, effect, id, &helper);
                        }
                    }
                    else if (effect == 0 || effect->node != node || effect->runtime_builtin != id || !effect->compiler_barrier ||
                        !effect->reads_fp_environment || effect->folded || effect->evaluated_operands != 0 ||
                        effect->definite_operands != 0 || effect->floating_type != 0 || effect->unsupported != 0)
                    {
                        result->status = CHECK_PROGRAM_INVALID;
                    }
                    else
                    {
                        helper = (CheckProgramHelper){
                            .id = id,
                            .result_type = BUSTER_TYPE_INT,
                            .compiler_barrier = true,
                            .reads_fp_environment = true,
                        };
                    }
                }
                else
                {
                    while (atomic < stream->atomic_count && stream->atomics[atomic].node < node)
                    {
                        atomic += 1;
                    }
                    if (atomic == stream->atomic_count || stream->atomics[atomic].node != node)
                    {
                        result->status = CHECK_PROGRAM_INVALID;
                    }
                    else
                    {
                        // A scoped family's builtin plans its base's (check_builtin_atomic_base), on the base's arguments: the
                        // lowering evaluates and drops its scope, the last.
                        const CBuiltinId base = check_builtin_atomic_base(id);
                        const uint32_t scoped = check_builtin_atomic_scope(id) != CHECK_BUILTIN_ATOMIC_SCOPE_NONE;
                        const uint32_t count = graph->argument_counts != 0 ? graph->argument_counts[node] : BUSTER_CHECK_NONE;
                        const uint32_t arguments = count != BUSTER_CHECK_NONE && count >= scoped ? count - scoped : count;
                        CheckBuiltinAtomicEffect scope_less = stream->atomics[atomic];
                        if (scoped != 0 && arguments < 32)
                        {
                            scope_less.evaluated_operands &= (UINT32_C(1) << arguments) - 1;
                        }
                        const CheckBuiltinAtomicEffect* effect = &scope_less;
                        if (check_program_direct_fence(program, node, stream->atomics + atomic) != UINT32_MAX)
                        {
                            // The typed stream's FENCE, which the lowering writes in the call's place: no helper.
                            folded = true;
                            result->direct[CHECK_PROGRAM_HELPER_FENCE] += 1;
                        }
                        else if (base == CBUILTIN_ATOMIC_ALWAYS_LOCK_FREE || base == CBUILTIN_ATOMIC_IS_LOCK_FREE || check_builtin_atomic_generic(base))
                        {
                            // A lock-free query is a constant or libatomic's runtime query (check_lower_atomic_query), and GNU's
                            // generic forms use the implicit widths' leaves or libatomic (check_lower_atomic_prepare).
                            folded = true;
                        }
                        else if (check_builtin_atomic_c11(base))
                        {
                            const bool aligned = effect->alignment_known && effect->minimum_alignment >= effect->object_size;
                            const uint8_t kind = check_action_kind(program->types, effect->value_type);
                            const bool representation = kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION || kind == BUSTER_TYPE_KIND_COMPLEX;
                            if (representation || !check_builtin_atomic_c11_native(program->types, base, effect->value_type, effect->object_size, aligned))
                            {
                                // The lowering uses libatomic's generic ABI for representations the native leaves don't take.
                                folded = true;
                            }
                            else
                            {
                                CheckBuiltinAtomicEffect normalized = *effect;
                                const bool compare = base == CBUILTIN_C11_ATOMIC_COMPARE_EXCHANGE_STRONG || base == CBUILTIN_C11_ATOMIC_COMPARE_EXCHANGE_WEAK;
                                const uint32_t parameters = arguments + compare + (base == CBUILTIN_C11_ATOMIC_INIT);
                                normalized.evaluated_operands = (1u << parameters) - 1;
                                normalized.unsupported = 0;
                                if (compare && (effect->value_type == BUSTER_TYPE_FLOAT || effect->value_type == BUSTER_TYPE_DOUBLE))
                                {
                                    normalized.value_type = effect->object_size == 4 ? BUSTER_TYPE_UINT : BUSTER_TYPE_ULONG;
                                }
                                result->status = check_program_helper_atomic(program, &normalized, effect->runtime_builtin, parameters, &helper, &result->reason);
                            }
                        }
                        else
                        {
                            result->status = check_program_helper_atomic(program, effect, base, arguments, &helper, &result->reason);
                        }
                    }
                }
                if (result->status == CHECK_PROGRAM_OK && !folded)
                {
                    const uint32_t key = helper.x86 != CBUILTIN_X86_HELPER_NONE ? BUSTER_CHECK_PROGRAM_HELPER_X86 + (uint32_t)helper.x86 :
                                                                                   check_program_helper_key(helper.id, helper.bytes, helper.rmw, helper.rmw_format);
                    if (key >= BUSTER_CHECK_PROGRAM_HELPER_SIGNATURES)
                    {
                        result->status = CHECK_PROGRAM_UNSUPPORTED;
                    }
                    else
                    {
                        if (keys[key] == 0)
                        {
                            first_nodes[result->count] = node;
                            planned[result->count] = helper;
                            result->count += 1;
                            keys[key] = result->count;
                        }
                        else
                        {
                            CheckProgramHelper* retained = planned + keys[key] - 1;
                            retained->reads_fp_environment |= helper.reads_fp_environment;
                            retained->may_raise_fp_exceptions |= helper.may_raise_fp_exceptions;
                            retained->may_raise_inexact |= helper.may_raise_inexact;
                        }
                        uses[result->use_count++] = (CheckProgramHelperUse){
                            .node = node,
                            .helper = keys[key] - 1,
                        };
                        result->helper[check_program_helper_family(planned + keys[key] - 1)] += 1;
                    }
                }
            }
            if (result->status != CHECK_PROGRAM_OK)
            {
                result->token = graph->tokens[node];
            }
        }
    }
    if (result->status == CHECK_PROGRAM_OK && program->symbol_count > UINT32_MAX - result->count - 1)
    {
        result->status = CHECK_PROGRAM_RESOURCE;
    }
    if (result->status == CHECK_PROGRAM_OK)
    {
        result->items = result->count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckProgramHelper, result->count) : 0;
        result->uses = result->use_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckProgramHelperUse, result->use_count) : 0;
        if ((result->count != 0 && result->items == 0) || (result->use_count != 0 && result->uses == 0))
        {
            result->status = CHECK_PROGRAM_RESOURCE;
        }
    }
    for (uint32_t i = 0; result->status == CHECK_PROGRAM_OK && i < result->count; i += 1)
    {
        CheckProgramHelper* helper = planned + i;
        if (helper->x86 != CBUILTIN_X86_HELPER_NONE)
        {
            const CBuiltinX86Helper native = cbuiltin_x86_helper(helper->x86, output);
            helper->code = native.code;
            const uint32_t result_class = helper->result_type == BUSTER_TYPE_VOID  ? BUSTER_TYPED_CLASS_VOID :
                                          helper->result_type == BUSTER_TYPE_ULONG ? BUSTER_TYPED_CLASS_U64 :
                                                                                     BUSTER_TYPED_CLASS_VECTOR;
            if (native.code.error == TIER0_OK && (native.parameter_count != helper->parameter_count || native.result_class != result_class))
            {
                result->status = CHECK_PROGRAM_INVALID;
                result->token = program->input->graph.tokens[first_nodes[i]];
            }
        }
        else if (helper->terminates)
        {
            const CBuiltinTerminalHelper native = cbuiltin_terminal_helper(helper->id, output);
            helper->code = native.code;
        }
        else if (helper->math_kind != CHECK_BUILTIN_MATH_NONE)
        {
            const CBuiltinMathHelper native = cbuiltin_math_helper(helper->id, output);
            helper->code = native.code;
            if (native.code.error == TIER0_OK &&
                (native.parameter_count != helper->parameter_count || native.result_class != BUSTER_TYPED_CLASS_U64 ||
                 native.parameter_classes[0] != BUSTER_TYPED_CLASS_U64 ||
                 (native.parameter_count > 1 && native.parameter_classes[1] != BUSTER_TYPED_CLASS_U64) ||
                 native.requires_errno_address != helper->requires_errno_address))
            {
                result->status = CHECK_PROGRAM_INVALID;
                result->token = program->input->graph.tokens[first_nodes[i]];
            }
            helper->compiler_barrier |= native.compiler_barrier;
            helper->reads_fp_environment |= native.reads_fp_environment;
            helper->may_raise_fp_exceptions |= native.may_raise_fp_exceptions;
            helper->may_raise_inexact |= native.may_raise_inexact;
        }
        else if (helper->id == CBUILTIN_FLT_ROUNDS)
        {
            const CBuiltinRoundsHelper native = cbuiltin_rounds_helper(helper->id, output);
            helper->code = native.code;
        }
        else if (helper->rmw)
        {
            const CBuiltinAtomicRmwHelper native = cbuiltin_atomic_rmw_helper_emit(helper->id, helper->bytes, helper->rmw_format, output);
            helper->code = native.code;
            bool classes = native.parameter_count == helper->parameter_count &&
                           native.result_class == (helper->result_type == BUSTER_TYPE_VOID ? BUSTER_TYPED_CLASS_VOID : BUSTER_TYPED_CLASS_U64);
            for (uint32_t a = 0; a < helper->parameter_count && a < 6; a += 1)
            {
                classes &= native.parameter_classes[a] == (helper->parameter_types[a] == BUSTER_TYPE_INT ? BUSTER_TYPED_CLASS_I32 : BUSTER_TYPED_CLASS_U64);
            }
            if (native.code.error == TIER0_OK && !classes)
            {
                result->status = CHECK_PROGRAM_INVALID;
                result->token = program->input->graph.tokens[first_nodes[i]];
            }
        }
        else
        {
            const CBuiltinAtomicHelper native = cbuiltin_atomic_helper_emit(helper->id, helper->bytes, output);
            helper->code = native.code;
        }
        if (helper->code.error != TIER0_OK)
        {
            result->status = helper->code.error == TIER0_ERROR_MEMORY ? CHECK_PROGRAM_RESOURCE : CHECK_PROGRAM_BACKEND;
            result->token = program->input->graph.tokens[first_nodes[i]];
        }
        else
        {
            helper->symbol = program->symbol_count + 1 + i;
            result->items[i] = *helper;
        }
    }
    valid = result->status == CHECK_PROGRAM_OK;
    if (valid && result->use_count != 0)
    {
        memcpy(result->uses, uses, (uint64_t)result->use_count * sizeof(*uses));
    }
    if (!valid)
    {
        const CheckProgramStatus status = result->status;
        const uint32_t token = result->token;
        const char* reason = result->reason;
        if (output != 0)
        {
            arena_pop_to(output, mark);
        }
        *result = (CheckProgramHelpers){
            .status = status,
            .token = token,
            .reason = reason,
        };
    }
    if (scratch != 0)
    {
        arena_pop_to(scratch, temporary);
    }
    return valid;
}

#endif
