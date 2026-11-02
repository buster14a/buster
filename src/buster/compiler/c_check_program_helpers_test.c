// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Helper selection, ABI/ordering obligations, independent baseline bytes and transactional failures. These
// bounded descriptors isolate the planner contract; the program tests cover source dispatch and native calls.
// Include after c_checker_test.c's shared helpers and c_check_program_helpers.c.
#ifndef BUSTER_C_CHECK_PROGRAM_HELPERS_TEST_INCLUDED
#define BUSTER_C_CHECK_PROGRAM_HELPERS_TEST_INCLUDED

typedef struct CheckProgramHelperExpected CheckProgramHelperExpected;
struct CheckProgramHelperExpected
{
    CBuiltinId id;
    uint32_t width;
    uint32_t parameters;
    uint32_t count;
    uint8_t bytes[32];
};

// A read/modify/write's effect at a load's node: its builtin, the operands it says it evaluates, its rmw bit, and what the
// planner must make of it.
typedef struct CheckProgramHelperRmwCase CheckProgramHelperRmwCase;
struct CheckProgramHelperRmwCase
{
    CBuiltinId id;
    uint32_t operands;
    bool rmw;
    CheckProgramStatus status;
};

static void check_test_program_math_helpers(CheckTest* test)
{
    static const CBuiltinId ids[] = {
        CBUILTIN_SQRT,
        CBUILTIN_SQRTF,
        CBUILTIN_COPYSIGN,
        CBUILTIN_COPYSIGNF,
        CBUILTIN_CEIL,
        CBUILTIN_CEILF,
        CBUILTIN_FLOOR,
        CBUILTIN_FLOORF,
        CBUILTIN_TRUNC,
        CBUILTIN_TRUNCF,
        CBUILTIN_NEARBYINT,
        CBUILTIN_NEARBYINTF,
        CBUILTIN_RINT,
        CBUILTIN_RINTF,
    };
    CheckTestTypes types = { 0 };
    Arena output = arena_reserve(UINT64_C(1) << 20);
    Arena scratch = arena_reserve(UINT64_C(1) << 16);
    const bool ready = check_test_types_open(&types) && output.base != NULL && scratch.base != NULL;
    check_test_expect(test, ready, "math helper planner test resources");
    if (ready)
    {
        const TypeId errno_pointer = type_pointer(&types.table, BUSTER_TYPE_INT);
        CheckOp ops[16];
        uint32_t tokens[16];
        uint32_t members[16];
        uint32_t active[16];
        CheckBuiltinFloatEffect effects[16];
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(ops); i += 1)
        {
            const CBuiltinId id = i < BUSTER_ARRAY_LENGTH(ids) ? ids[i] : CBUILTIN_COPYSIGN;
            const uint32_t kind_index = i < BUSTER_ARRAY_LENGTH(ids) ? i / 2 : 1;
            const CheckBuiltinMathKind kind = (CheckBuiltinMathKind)(CHECK_BUILTIN_MATH_SQRT + kind_index);
            const TypeId floating = i < BUSTER_ARRAY_LENGTH(ids) && (i & 1) != 0 ? BUSTER_TYPE_FLOAT : BUSTER_TYPE_DOUBLE;
            const bool copy = kind_index == 1;
            ops[i] = CHECK_OP_BUILTIN_FLOAT;
            tokens[i] = 133800 + i;
            members[i] = id;
            active[i] = i;
            effects[i] = (CheckBuiltinFloatEffect){
                .node = i,
                .converted_arguments = { { .type = floating }, { .type = floating } },
                .math_kind = kind,
                .errno_pointer_type = kind_index == 0 ? errno_pointer : 0,
                .floating_type = floating,
                .evaluated_operands = copy ? 3 : 1,
                .definite_operands = copy ? 3 : 1,
                .runtime_builtin = id,
                .compiler_barrier = !copy,
                .reads_fp_environment = !copy,
                .may_raise_fp_exceptions = !copy,
                .may_raise_inexact = kind_index == 0 || kind_index == 6,
                .may_set_errno = kind_index == 0,
            };
        }
        effects[14].reads_fp_environment = true;
        effects[14].compiler_barrier = true;
        effects[14].may_raise_fp_exceptions = true;
        effects[15].runtime_builtin = CBUILTIN_NONE;
        effects[15].folded = true;
        CheckInput input = {
            .graph = {
                .ops = ops,
                .tokens = tokens,
                .members = members,
                .count = BUSTER_ARRAY_LENGTH(ops),
            },
        };
        CheckStream stream = {
            .floating = effects,
            .floating_count = BUSTER_ARRAY_LENGTH(effects),
            .count = BUSTER_ARRAY_LENGTH(ops),
        };
        CheckProgram program = {
            .input = &input,
            .stream = &stream,
            .types = &types.table,
            .builtin_nodes = active,
            .builtin_count = BUSTER_ARRAY_LENGTH(active),
            .symbol_count = 4,
        };
        CheckProgramHelpers helpers;
        const bool planned = check_program_helpers(&helpers, &program, &output, &scratch);
        check_test_expect(test, planned && helpers.count == 14 && helpers.use_count == 15 && helpers.uses[14].helper == 2 && scratch.length == 0,
                          "all fourteen math helper signatures deduplicate and folded copysign has no runtime helper");
        for (uint32_t i = 0; planned && i < helpers.count; i += 1)
        {
            const CheckProgramHelper* helper = helpers.items + i;
            const bool square_root = i < 2;
            const bool copy = i == 2 || i == 3;
            const TypeId floating = (i & 1) != 0 ? BUSTER_TYPE_FLOAT : BUSTER_TYPE_DOUBLE;
            check_test_expect(test, helper->id == ids[i] && helper->symbol == 5 + i && helper->floating_type == floating &&
                                   helper->math_kind == (CheckBuiltinMathKind)(CHECK_BUILTIN_MATH_SQRT + i / 2) &&
                                   helper->result_type == BUSTER_TYPE_ULONG && helper->parameter_count == (square_root || copy ? 2 : 1) &&
                                   helper->parameter_types[0] == BUSTER_TYPE_ULONG &&
                                   helper->parameter_types[1] == (square_root || copy ? BUSTER_TYPE_ULONG : BUSTER_TYPE_VOID) &&
                                   helper->requires_errno_address == square_root && helper->compiler_barrier &&
                                   helper->reads_fp_environment == (i != 3) && helper->may_raise_fp_exceptions == (i != 3) &&
                                   helper->may_raise_inexact == (i < 2 || i >= 12) && helper->code.count != 0 &&
                                   helper->code.reference_count == 0 && !helper->terminates,
                              "math helper keeps floating semantics separate from its raw integer bit transport ABI");
        }
        const uint64_t full = output.length;
        arena_pop_to(&output, 0);
        const CheckBuiltinFloatEffect saved = effects[0];
        for (uint32_t mutation = 0; mutation < 8; mutation += 1)
        {
            Arena bounded = output;
            CheckProgramStatus expected = CHECK_PROGRAM_INVALID;
            switch (mutation)
            {
                case 0: effects[0].errno_pointer_type = 0; break;
                case 1: effects[0].floating_type = BUSTER_TYPE_FLOAT; break;
                case 2: effects[0].converted_arguments[0].type = BUSTER_TYPE_INT; break;
                case 3: effects[0].definite_operands = 0; break;
                case 4: effects[0].may_set_errno = false; break;
                case 5: effects[0].may_raise_inexact = false; break;
                case 6:
                {
                    effects[0].unsupported = 1;
                    expected = CHECK_PROGRAM_UNSUPPORTED;
                } break;
                case 7:
                {
                    bounded.capacity = full - 1;
                    expected = CHECK_PROGRAM_RESOURCE;
                } break;
            }
            CheckProgramHelpers rejected;
            check_test_expect(test, !check_program_helpers(&rejected, &program, &bounded, &scratch) && rejected.status == expected &&
                                   rejected.count == 0 && rejected.items == NULL && rejected.uses == NULL && bounded.length == 0 && scratch.length == 0,
                              "math planner rejects malformed obligations or short output without publishing a partial unit");
            effects[0] = saved;
        }
    }
    check_test_types_close(&types);
    arena_release(&scratch);
    arena_release(&output);
}

// A helper's published contract (#1059) against its code: what its records do, read by x86_semantics.c
// (check_program_helper_physical), must be within its effects (check_program_helper_effects), and its effects' claims
// must be its code's. The callee-saved registers its code may write are those its frame saves and restores (its code's
// frame_saved, Tier0Frame.saved's bits, and with frame_pointer rbp: an x86 leaf's frame); every other must stay as the
// call's convention leaves it. Returns whether it holds, and
// says why not into `reason`.
static bool check_test_helper_contract(const CheckProgramHelper* helper, const Tier0Code* code, Arena* scratch, const char** reason)
{
    const CheckProgramHelperFamily family = check_program_helper_family(helper);
    const uint32_t effects = check_program_helper_effects(helper);
    const CheckProgramHelperPhysical physical = check_program_helper_physical(code, scratch);
    const uint64_t callee_saved = BUSTER_X86_RESOURCE_GPR(X86_REGISTER_RBX) | BUSTER_X86_RESOURCE_GPR(X86_REGISTER_RBP) | BUSTER_X86_RESOURCE_GPR(X86_REGISTER_R12) |
                                  BUSTER_X86_RESOURCE_GPR(X86_REGISTER_R13) | BUSTER_X86_RESOURCE_GPR(X86_REGISTER_R14) | BUSTER_X86_RESOURCE_GPR(X86_REGISTER_R15);
    uint64_t restored = code->frame_pointer ? BUSTER_X86_RESOURCE_GPR(X86_REGISTER_RBP) : 0;
    for (uint32_t c = 0; c < BUSTER_TIER0_CALLEE_SAVED; c += 1)
    {
        restored |= (code->frame_saved & 1u << c) != 0 ? BUSTER_X86_RESOURCE_GPR(tier0_callee_saved[c]) : 0;
    }
    const bool unmodeled = (effects & BUSTER_CHECK_HELPER_UNMODELED) != 0;
    const bool orders = (physical.memory & (BUSTER_X86_MEMORY_EFFECT_FENCE | BUSTER_X86_MEMORY_EFFECT_ATOMIC)) != 0;
    const bool environment = (physical.reads & (BUSTER_X86_RESOURCE_MXCSR | BUSTER_X86_RESOURCE_X87_CONTROL)) != 0;
    const bool raises = (physical.properties & BUSTER_X86_EFFECT_FP_EXCEPTION) != 0;
    // The forms the semantics' tables don't cover yet where a family's contract leaves nothing unmodeled: the IEEE
    // maximum's and minimum's sign (c_builtin_atomic_rmw_helpers.c's btr and bts of an immediate), which the test names.
    const uint16_t gap = physical.uncovered_form;
    const bool known_gap = gap == X86_FORM_BTR_RM32_IMM8 || gap == X86_FORM_BTR_RM64_IMM8 || gap == X86_FORM_BTS_RM32_IMM8 || gap == X86_FORM_BTS_RM64_IMM8;
    *reason = NULL;
    if (!physical.valid || code->error != TIER0_OK)
    {
        *reason = "its code doesn't resolve";
    }
    else if (physical.uncovered != 0 && !unmodeled && !known_gap)
    {
        *reason = "its code has a form x86_semantics.c doesn't cover, where its contract models every effect";
    }
    else if ((physical.writes & callee_saved & ~restored) != 0)
    {
        *reason = "its code writes a callee-saved register";
    }
    else if (physical.writes_object && (effects & (BUSTER_CHECK_HELPER_WRITES | BUSTER_CHECK_HELPER_ERRNO)) == 0)
    {
        *reason = "its code writes memory its contract doesn't";
    }
    else if (physical.reads_object && (effects & BUSTER_CHECK_HELPER_READS) == 0 && !unmodeled)
    {
        *reason = "its code reads memory its contract doesn't";
    }
    else if (orders && (effects & BUSTER_CHECK_HELPER_ORDERS) == 0)
    {
        *reason = "its code fences or locks where its contract orders nothing";
    }
    else if (environment && (effects & BUSTER_CHECK_HELPER_FP_ENVIRONMENT) == 0)
    {
        *reason = "its code reads the floating environment where its contract doesn't";
    }
    else if (raises && (effects & BUSTER_CHECK_HELPER_FP_EXCEPTIONS) == 0 && !unmodeled)
    {
        *reason = "its code may raise floating exceptions its contract doesn't";
    }
    else if (((effects & BUSTER_CHECK_HELPER_TERMINATES) != 0) != (physical.traps && !physical.returns))
    {
        *reason = "its code returns or traps otherwise than its contract says";
    }
    else if (family == CHECK_PROGRAM_HELPER_ATOMIC_ACCESS && (physical.reads_object != ((effects & BUSTER_CHECK_HELPER_READS) != 0) ||
                                                               physical.writes_object != ((effects & BUSTER_CHECK_HELPER_WRITES) != 0) ||
                                                               ((effects & BUSTER_CHECK_HELPER_WRITES) != 0 && (physical.memory & BUSTER_X86_MEMORY_EFFECT_FENCE) == 0)))
    {
        // A load reads its object, and a store writes it and fences: the helpers' seq_cst.
        *reason = "an atomic load's or store's code doesn't access its object as its contract says, or a store doesn't fence";
    }
    else if (family == CHECK_PROGRAM_HELPER_ATOMIC_RMW && !physical.writes_object)
    {
        *reason = "an atomic read/modify/write's code doesn't write its object";
    }
    else if (family == CHECK_PROGRAM_HELPER_ATOMIC_RMW && (effects & BUSTER_CHECK_HELPER_READS) != 0 && (physical.memory & BUSTER_X86_MEMORY_EFFECT_ATOMIC) == 0)
    {
        *reason = "an atomic read/modify/write's code has no locked access";
    }
    else if (family == CHECK_PROGRAM_HELPER_FENCE && helper->id != CBUILTIN_ATOMIC_SIGNAL_FENCE && (physical.memory & BUSTER_X86_MEMORY_EFFECT_FENCE) == 0)
    {
        *reason = "a thread fence's code doesn't fence";
    }
    else if (family == CHECK_PROGRAM_HELPER_FENCE && helper->id == CBUILTIN_ATOMIC_SIGNAL_FENCE && (orders || physical.reads_object || physical.writes_object))
    {
        *reason = "a signal fence's code does more than return: its order is the caller's barrier alone";
    }
    else if ((effects & BUSTER_CHECK_HELPER_FP_ENVIRONMENT) != 0 && family != CHECK_PROGRAM_HELPER_X86 && family != CHECK_PROGRAM_HELPER_ASM && !environment)
    {
        *reason = "its contract reads the floating environment where its code doesn't";
    }
    else if ((effects & BUSTER_CHECK_HELPER_ERRNO) != 0 && !physical.writes_object)
    {
        *reason = "its contract sets errno where its code writes no memory";
    }
    return *reason == NULL;
}

// #1059's inventory, against the helpers' code: every family has its contract, only the fences a direct form, and every
// helper each emitter makes (the atomic loads and stores of every width, the fences, every read/modify/write of every
// width and format, the terminal builtins, FLT_ROUNDS, every math builtin and every x86 leaf) keeps its family's
// contract (check_test_helper_contract). An asm statement's helper is its template's, which UNMODELED covers whole.
static void check_test_program_helper_contracts(CheckTest* test)
{
    for (uint32_t f = 0; f < CHECK_PROGRAM_HELPER_FAMILIES; f += 1)
    {
        const CheckProgramHelperContract* contract = &check_program_helper_contracts[f];
        check_test_expect(test, contract->name != NULL && contract->helper != NULL && contract->effects != 0 && (contract->direct != NULL) == (f == CHECK_PROGRAM_HELPER_FENCE),
                          "every helper family publishes its contract, and only the fences a direct form");
    }
    Arena output = arena_reserve(UINT64_C(1) << 22);
    Arena scratch = arena_reserve(UINT64_C(1) << 20);
    const bool ready = output.base != NULL && scratch.base != NULL && x86_encoder_initialize() && (x86_semantics_initialized || x86_semantics_initialize());
    check_test_expect(test, ready, "helper contracts: resources and the semantics' tables");
    uint32_t checked[CHECK_PROGRAM_HELPER_FAMILIES] = { 0 };
    char description[256];
    // The atomic loads and stores, and the fences.
    static const CBuiltinId accesses[] = { CBUILTIN_ATOMIC_LOAD_N, CBUILTIN_ATOMIC_STORE_N, CBUILTIN_ATOMIC_THREAD_FENCE, CBUILTIN_ATOMIC_SIGNAL_FENCE, CBUILTIN_SYNC_SYNCHRONIZE };
    for (uint32_t a = 0; ready && a < BUSTER_ARRAY_LENGTH(accesses); a += 1)
    {
        const bool memory = accesses[a] == CBUILTIN_ATOMIC_LOAD_N || accesses[a] == CBUILTIN_ATOMIC_STORE_N;
        for (uint32_t w = 0; w < (memory ? 4u : 1u); w += 1)
        {
            const uint32_t bytes = memory ? 1u << w : 0;
            const CBuiltinAtomicHelper native = cbuiltin_atomic_helper_emit(accesses[a], bytes, &output);
            const CheckProgramHelper helper = {
                .id = accesses[a],
                .bytes = bytes,
                .result_type = accesses[a] == CBUILTIN_ATOMIC_LOAD_N ? BUSTER_TYPE_ULONG : BUSTER_TYPE_VOID,
                .compiler_barrier = true,
                .required_alignment = bytes,
            };
            const char* reason = NULL;
            const bool kept = check_test_helper_contract(&helper, &native.code, &scratch, &reason);
            checked[check_program_helper_family(&helper)] += kept;
            snprintf(description, sizeof(description), "helper contracts: atomic builtin %u of %u bytes: %s", (uint32_t)accesses[a], bytes, reason != NULL ? reason : "");
            check_test_expect(test, kept, description);
        }
    }
    // Every read/modify/write of every width, in its integer format, and in each other format its rule takes.
    static const CBuiltinAtomicRmwFormat formats[] = { CBUILTIN_ATOMIC_RMW_INTEGER_BITS, CBUILTIN_ATOMIC_RMW_BOOL_BITS, CBUILTIN_ATOMIC_RMW_SIGNED_BITS,
                                                       CBUILTIN_ATOMIC_RMW_F32_BITS, CBUILTIN_ATOMIC_RMW_F64_BITS };
    for (uint32_t r = 0; ready && r < BUSTER_ARRAY_LENGTH(check_builtin_atomic_rmw_rules); r += 1)
    {
        const CheckBuiltinAtomicRmwRule* rule = &check_builtin_atomic_rmw_rules[r];
        const bool release = rule->operation == CHECK_BUILTIN_ATOMIC_RMW_RELEASE || rule->operation == CHECK_BUILTIN_ATOMIC_RMW_CLEAR;
        for (uint32_t w = 0; w < 4; w += 1)
        {
            for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(formats); f += 1)
            {
                const uint64_t mark = output.length;
                const CBuiltinAtomicRmwHelper native = cbuiltin_atomic_rmw_helper_emit(rule->id, 1u << w, formats[f], &output);
                if (native.code.error == TIER0_OK)
                {
                    const CheckProgramHelper helper = {
                        .id = rule->id,
                        .bytes = 1u << w,
                        .result_type = release ? BUSTER_TYPE_VOID : BUSTER_TYPE_ULONG,
                        .rmw_format = formats[f],
                        .rmw = true,
                        .compiler_barrier = true,
                        .required_alignment = 1u << w,
                    };
                    const char* reason = NULL;
                    const bool kept = check_test_helper_contract(&helper, &native.code, &scratch, &reason);
                    checked[CHECK_PROGRAM_HELPER_ATOMIC_RMW] += kept;
                    snprintf(description, sizeof(description), "helper contracts: read/modify/write %u of %u bytes, format %u: %s", (uint32_t)rule->id, 1u << w,
                             (uint32_t)formats[f], reason != NULL ? reason : "");
                    check_test_expect(test, kept, description);
                }
                arena_pop_to(&output, mark);
            }
        }
    }
    // The terminal builtins and FLT_ROUNDS.
    static const CBuiltinId terminals[] = { CBUILTIN_TRAP, CBUILTIN_UNREACHABLE };
    for (uint32_t t = 0; ready && t < BUSTER_ARRAY_LENGTH(terminals); t += 1)
    {
        const CBuiltinTerminalHelper native = cbuiltin_terminal_helper(terminals[t], &output);
        const CheckProgramHelper helper = { .id = terminals[t], .result_type = BUSTER_TYPE_VOID, .terminates = native.terminates };
        const char* reason = NULL;
        const bool kept = check_test_helper_contract(&helper, &native.code, &scratch, &reason);
        checked[CHECK_PROGRAM_HELPER_TERMINAL] += kept;
        snprintf(description, sizeof(description), "helper contracts: terminal builtin %u: %s", (uint32_t)terminals[t], reason != NULL ? reason : "");
        check_test_expect(test, kept && native.terminates, description);
    }
    if (ready)
    {
        const CBuiltinRoundsHelper native = cbuiltin_rounds_helper(CBUILTIN_FLT_ROUNDS, &output);
        const CheckProgramHelper helper = {
            .id = CBUILTIN_FLT_ROUNDS,
            .result_type = BUSTER_TYPE_INT,
            .compiler_barrier = native.compiler_barrier,
            .reads_fp_environment = native.reads_fp_environment,
        };
        const char* reason = NULL;
        const bool kept = check_test_helper_contract(&helper, &native.code, &scratch, &reason);
        checked[CHECK_PROGRAM_HELPER_ROUNDS] += kept;
        snprintf(description, sizeof(description), "helper contracts: FLT_ROUNDS: %s", reason != NULL ? reason : "");
        check_test_expect(test, kept, description);
    }
    // Every math builtin, with the facts its emitter publishes, which the planner merges into its helper.
    for (uint32_t id = CBUILTIN_SQRT; ready && id <= CBUILTIN_RINTF; id += 1)
    {
        const CheckBuiltinMathKind kind = check_builtin_math_kind((CBuiltinId)id);
        const CBuiltinMathHelper native = kind != CHECK_BUILTIN_MATH_NONE ? cbuiltin_math_helper((CBuiltinId)id, &output) : (CBuiltinMathHelper){ 0 };
        if (kind != CHECK_BUILTIN_MATH_NONE)
        {
            const CheckProgramHelper helper = {
                .id = (CBuiltinId)id,
                .result_type = BUSTER_TYPE_ULONG,
                .math_kind = kind,
                .floating_type = check_builtin_math_parameter((CBuiltinId)id),
                .compiler_barrier = native.compiler_barrier,
                .reads_fp_environment = native.reads_fp_environment,
                .may_raise_fp_exceptions = native.may_raise_fp_exceptions,
                .may_raise_inexact = native.may_raise_inexact,
                .requires_errno_address = native.requires_errno_address,
            };
            const char* reason = NULL;
            const bool kept = check_test_helper_contract(&helper, &native.code, &scratch, &reason);
            checked[CHECK_PROGRAM_HELPER_MATH] += kept;
            snprintf(description, sizeof(description), "helper contracts: math builtin %u: %s", id, reason != NULL ? reason : "");
            check_test_expect(test, kept, description);
        }
    }
    // Every x86 leaf (the AMX ones too), with the frame it publishes.
    for (uint32_t k = CBUILTIN_X86_HELPER_NONE + 1; ready && k < CBUILTIN_X86_HELPER_COUNT; k += 1)
    {
        const uint64_t mark = output.length;
        const CBuiltinX86Helper native = cbuiltin_x86_helper((CBuiltinX86HelperKind)k, &output);
        const CheckProgramHelper helper = { .id = CBUILTIN_NONE, .result_type = BUSTER_TYPE_ULONG, .compiler_barrier = true, .x86 = (CBuiltinX86HelperKind)k };
        const char* reason = NULL;
        const bool kept = check_test_helper_contract(&helper, &native.code, &scratch, &reason);
        checked[CHECK_PROGRAM_HELPER_X86] += kept;
        snprintf(description, sizeof(description), "helper contracts: x86 leaf %s: %s", cbuiltin_x86_helper_infos[k].name, reason != NULL ? reason : "");
        check_test_expect(test, kept, description);
        arena_pop_to(&output, mark);
    }
    uint32_t total = 0;
    for (uint32_t f = 0; ready && f < CHECK_PROGRAM_HELPER_FAMILIES; f += 1)
    {
        snprintf(description, sizeof(description), "helper contracts: the %s family has helpers checked against its contract", check_program_helper_contracts[f].name);
        check_test_expect(test, checked[f] != 0 || f == CHECK_PROGRAM_HELPER_ASM, description);
        total += checked[f];
    }
    printf("  helper contracts: %u helpers' code kept their families' contracts (%u atomic accesses, %u read/modify/writes, %u fences, %u terminal, %u FLT_ROUNDS, "
           "%u math, %u x86 leaves)\n",
           total, checked[CHECK_PROGRAM_HELPER_ATOMIC_ACCESS], checked[CHECK_PROGRAM_HELPER_ATOMIC_RMW], checked[CHECK_PROGRAM_HELPER_FENCE], checked[CHECK_PROGRAM_HELPER_TERMINAL],
           checked[CHECK_PROGRAM_HELPER_ROUNDS], checked[CHECK_PROGRAM_HELPER_MATH], checked[CHECK_PROGRAM_HELPER_X86]);
    arena_release(&scratch);
    arena_release(&output);
}

static void check_test_program_helpers(CheckTest* test)
{
    static const CheckProgramHelperExpected expected[] = {
        {
            .id = CBUILTIN_ATOMIC_LOAD_N,
            .width = 1,
            .parameters = 2,
            .count = 4,
            .bytes = {
                0x0f,
                0xb6,
                0x07,
                0xc3,
            },
        },
        {
            .id = CBUILTIN_ATOMIC_LOAD_N,
            .width = 2,
            .parameters = 2,
            .count = 4,
            .bytes = {
                0x0f,
                0xb7,
                0x07,
                0xc3,
            },
        },
        {
            .id = CBUILTIN_ATOMIC_LOAD_N,
            .width = 4,
            .parameters = 2,
            .count = 3,
            .bytes = {
                0x8b,
                0x07,
                0xc3,
            },
        },
        {
            .id = CBUILTIN_ATOMIC_LOAD_N,
            .width = 8,
            .parameters = 2,
            .count = 4,
            .bytes = {
                0x48,
                0x8b,
                0x07,
                0xc3,
            },
        },
        {
            .id = CBUILTIN_ATOMIC_STORE_N,
            .width = 1,
            .parameters = 3,
            .count = 7,
            .bytes = {
                0x40,
                0x88,
                0x37,
                0x0f,
                0xae,
                0xf0,
                0xc3,
            },
        },
        {
            .id = CBUILTIN_ATOMIC_STORE_N,
            .width = 2,
            .parameters = 3,
            .count = 7,
            .bytes = {
                0x66,
                0x89,
                0x37,
                0x0f,
                0xae,
                0xf0,
                0xc3,
            },
        },
        {
            .id = CBUILTIN_ATOMIC_STORE_N,
            .width = 4,
            .parameters = 3,
            .count = 6,
            .bytes = {
                0x89,
                0x37,
                0x0f,
                0xae,
                0xf0,
                0xc3,
            },
        },
        {
            .id = CBUILTIN_ATOMIC_STORE_N,
            .width = 8,
            .parameters = 3,
            .count = 7,
            .bytes = {
                0x48,
                0x89,
                0x37,
                0x0f,
                0xae,
                0xf0,
                0xc3,
            },
        },
        {
            .id = CBUILTIN_ATOMIC_THREAD_FENCE,
            .parameters = 1,
            .count = 4,
            .bytes = {
                0x0f,
                0xae,
                0xf0,
                0xc3,
            },
        },
        {
            .id = CBUILTIN_ATOMIC_SIGNAL_FENCE,
            .parameters = 1,
            .count = 1,
            .bytes = {
                0xc3,
            },
        },
        {
            .id = CBUILTIN_SYNC_SYNCHRONIZE,
            .count = 4,
            .bytes = {
                0x0f,
                0xae,
                0xf0,
                0xc3,
            },
        },
        {
            .id = CBUILTIN_TRAP,
            .count = 2,
            .bytes = {
                0x0f,
                0x0b,
            },
        },
        {
            .id = CBUILTIN_UNREACHABLE,
            .count = 2,
            .bytes = {
                0x0f,
                0x0b,
            },
        },
        {
            .id = CBUILTIN_FLT_ROUNDS,
            .count = 31,
            .bytes = {
                0x48,
                0x83,
                0xec,
                0x08,
                0xd9,
                0x3c,
                0x24,
                0x0f,
                0xb7,
                0x04,
                0x24,
                0xc1,
                0xe8,
                0x0a,
                0x83,
                0xc0,
                0x01,
                0x83,
                0xe0,
                0x03,
                0x8b,
                0xc8,
                0xd1,
                0xe9,
                0x31,
                0xc8,
                0x48,
                0x83,
                0xc4,
                0x08,
                0xc3,
            },
        },
    };
    static const uint32_t selection[] = {
        3,
        11,
        0,
        3,
        4,
        1,
        2,
        5,
        6,
        7,
        8,
        9,
        10,
        12,
        13,
        13,
    };
    CheckOp ops[18] = { 0 };
    uint32_t members[18] = { 0 };
    uint32_t tokens[18] = { 0 };
    uint32_t active[17] = { 0 };
    CheckBuiltinAtomicEffect atomics[13] = { 0 };
    CheckBuiltinMiscEffect misc[3] = { 0 };
    CheckBuiltinFloatEffect floating[2] = { 0 };
    CheckInput input = { 0 };
    input.graph = (CheckGraph){
        .ops = ops,
        .tokens = tokens,
        .members = members,
        .count = BUSTER_ARRAY_LENGTH(ops),
    };
    CheckStream stream = {
        .atomics = atomics,
        .misc = misc,
        .floating = floating,
        .count = BUSTER_ARRAY_LENGTH(ops),
    };
    // A valid-but-unsupported source descriptor outside the reached set must reserve nothing and not fail.
    atomics[0] = (CheckBuiltinAtomicEffect){
        .node = 0,
        .object_size = 16,
        .required_alignment = 16,
        .minimum_alignment = 16,
        .evaluated_operands = 3,
        .unsupported = BUSTER_CHECK_BUILTIN_ATOMIC_UNSUPPORTED_WIDTH,
        .runtime_builtin = CBUILTIN_ATOMIC_LOAD_N,
        .alignment_known = true,
        .compiler_barrier = true,
    };
    stream.atomic_count = 1;
    ops[0] = CHECK_OP_BUILTIN_ATOMIC;
    members[0] = CBUILTIN_ATOMIC_LOAD_N;
    tokens[0] = 100;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(selection); i += 1)
    {
        const uint32_t node = i + 1;
        const CheckProgramHelperExpected* entry = expected + selection[i];
        const bool terminal = entry->id == CBUILTIN_TRAP || entry->id == CBUILTIN_UNREACHABLE;
        const bool rounds = entry->id == CBUILTIN_FLT_ROUNDS;
        active[i] = node;
        ops[node] = terminal ? CHECK_OP_BUILTIN_MISC : rounds ? CHECK_OP_BUILTIN_FLOAT : CHECK_OP_BUILTIN_ATOMIC;
        members[node] = entry->id;
        tokens[node] = 100 + node;
        if (terminal)
        {
            misc[stream.misc_count++] = (CheckBuiltinMiscEffect){
                .node = node,
                .runtime_builtin = entry->id,
                .terminates = true,
            };
        }
        else if (rounds)
        {
            floating[stream.floating_count++] = (CheckBuiltinFloatEffect){
                .node = node,
                .runtime_builtin = entry->id,
                .compiler_barrier = true,
                .reads_fp_environment = true,
            };
        }
        else
        {
            atomics[stream.atomic_count++] = (CheckBuiltinAtomicEffect){
                .node = node,
                .object_size = entry->width,
                .required_alignment = entry->width,
                .minimum_alignment = entry->width,
                .evaluated_operands = (UINT32_C(1) << entry->parameters) - 1,
                .runtime_builtin = entry->id,
                .alignment_known = entry->width != 0,
                .compiler_barrier = true,
            };
        }
    }
    active[16] = 17;
    ops[17] = CHECK_OP_BUILTIN_MISC;
    members[17] = CBUILTIN_EXPECT;
    tokens[17] = 117;
    misc[stream.misc_count++] = (CheckBuiltinMiscEffect){
        .node = 17,
        .runtime_builtin = CBUILTIN_EXPECT,
    };
    CheckProgram program = {
        .input = &input,
        .stream = &stream,
        .builtin_nodes = active,
        .builtin_count = BUSTER_ARRAY_LENGTH(active),
        .symbol_count = 7,
    };
    Arena output = arena_reserve(UINT64_C(1) << 16);
    Arena scratch = arena_reserve(UINT64_C(1) << 16);
    uint8_t* prefix = BUSTER_ARENA_PUSH_ARRAY(&output, uint8_t, 64);
    uint8_t* scratch_prefix = BUSTER_ARENA_PUSH_ARRAY(&scratch, uint8_t, 64);
    const bool ready = prefix != 0 && scratch_prefix != 0 && x86_encoder_initialize();
    check_test_expect(test, ready, "program helper planner resources and encoder initialize");
    if (ready)
    {
        memset(prefix, 0xa5, 64);
        memset(scratch_prefix, 0x5a, 64);
        CheckProgramHelpers helpers = { 0 };
        const bool planned = check_program_helpers(&helpers, &program, &output, &scratch);
        check_test_expect(test, planned && helpers.count == 14 && helpers.use_count == 16 && scratch.length == 64,
                          "only reached helper signatures are retained once per unit");
        if (planned)
        {
            check_test_expect(test, helpers.items[0].id == CBUILTIN_ATOMIC_LOAD_N && helpers.items[0].bytes == 8 &&
                                   helpers.items[1].id == CBUILTIN_TRAP && helpers.uses[0].helper == helpers.uses[3].helper &&
                                   helpers.uses[14].helper == helpers.uses[15].helper,
                              "helper IDs follow first use and duplicate atomic nodes share one definition");
            for (uint32_t i = 0; i < helpers.count; i += 1)
            {
                const CheckProgramHelper* helper = helpers.items + i;
                const CheckProgramHelperExpected* match = 0;
                for (uint32_t j = 0; j < BUSTER_ARRAY_LENGTH(expected); j += 1)
                {
                    if (expected[j].id == helper->id && expected[j].width == helper->bytes)
                    {
                        match = expected + j;
                    }
                }
                const bool terminal = helper->id == CBUILTIN_TRAP || helper->id == CBUILTIN_UNREACHABLE;
                const bool load = helper->id == CBUILTIN_ATOMIC_LOAD_N;
                const bool rounds = helper->id == CBUILTIN_FLT_ROUNDS;
                check_test_expect(test, match != 0 && helper->symbol == program.symbol_count + 1 + i &&
                                       helper->parameter_count == match->parameters && helper->required_alignment == match->width &&
                                       helper->terminates == terminal && helper->compiler_barrier == !terminal &&
                                       helper->reads_fp_environment == rounds && helper->result_type == (load ? BUSTER_TYPE_ULONG : rounds ? BUSTER_TYPE_INT : BUSTER_TYPE_VOID) &&
                                       helper->code.branch_count == 0 && helper->code.reference_count == 0,
                                  "each helper retains its raw ABI, alignment, terminal and compiler-ordering obligations");
                for (uint32_t path = 0; match != 0 && path < 2; path += 1)
                {
                    uint8_t bytes[256] = { 0 };
                    const int64_t count = path == 0 ? x86_encode_scalar(helper->code.records, helper->code.count, bytes, sizeof(bytes), 0, 0) :
                                                     x86_encode(helper->code.records, helper->code.count, bytes, sizeof(bytes), 0, 0);
                    check_test_expect(test, count == match->count && memcmp(bytes, match->bytes, match->count) == 0,
                                      "helper records encode to independently stated baseline x86 bytes");
                }
            }
        }
        const uint64_t full = output.length;
        arena_pop_to(&output, 64);
        for (uint32_t test_case = 0; test_case < 11; test_case += 1)
        {
            CheckProgram changed = program;
            CheckBuiltinAtomicEffect saved = atomics[1];
            const CheckBuiltinFloatEffect saved_float = floating[0];
            Arena bounded = output;
            CheckProgramStatus status = CHECK_PROGRAM_UNSUPPORTED;
            if (test_case == 0)
            {
                atomics[1].alignment_known = false;
            }
            else if (test_case == 1)
            {
                atomics[1].minimum_alignment = 4;
            }
            else if (test_case == 2)
            {
                atomics[1].minimum_alignment = 12;
                status = CHECK_PROGRAM_INVALID;
            }
            else if (test_case == 3)
            {
                atomics[1].compiler_barrier = false;
                status = CHECK_PROGRAM_INVALID;
            }
            else if (test_case == 4)
            {
                bounded.capacity = 64;
                status = CHECK_PROGRAM_RESOURCE;
            }
            else if (test_case == 5)
            {
                bounded.capacity = 64 + 14 * sizeof(CheckProgramHelper);
                status = CHECK_PROGRAM_RESOURCE;
            }
            else if (test_case == 6)
            {
                bounded.capacity = full - 1;
                status = CHECK_PROGRAM_RESOURCE;
            }
            else if (test_case == 7)
            {
                changed.symbol_count = UINT32_MAX - 1;
                status = CHECK_PROGRAM_RESOURCE;
            }
            else if (test_case == 8)
            {
                active[0] = 0;
            }
            else if (test_case == 9)
            {
                floating[0].reads_fp_environment = false;
                status = CHECK_PROGRAM_INVALID;
            }
            else
            {
                floating[0].compiler_barrier = false;
                status = CHECK_PROGRAM_INVALID;
            }
            CheckProgramHelpers rejected = { 0 };
            const bool accepted = check_program_helpers(&rejected, &changed, &bounded, &scratch);
            bool untouched = bounded.length == 64 && scratch.length == 64;
            for (uint32_t i = 0; i < 64; i += 1)
            {
                untouched = untouched && prefix[i] == 0xa5 && scratch_prefix[i] == 0x5a;
            }
            check_test_expect(test, !accepted && rejected.status == status && rejected.items == 0 && rejected.uses == 0 &&
                                   rejected.count == 0 && rejected.use_count == 0 && untouched,
                              "unsupported metadata and partial helper allocation fail without publishing output or touching live prefixes");
            check_test_expect(test, (rejected.reason != NULL) == (status == CHECK_PROGRAM_UNSUPPORTED),
                              "an unsupported helper's refusal names what it lacks, and no other does");
            atomics[1] = saved;
            floating[0] = saved_float;
            active[0] = 1;
        }
        // A read/modify/write's effect at the load's node, with the call's fixed arguments and a long object: only a
        // well-formed one, its rule's fixed arguments evaluated and its rmw bit as its operation has it, is planned, its
        // helper the builtin's leaf; any other is invalid.
        static const CheckProgramHelperRmwCase rmw_cases[] = {
            { CBUILTIN_ATOMIC_FETCH_ADD, 7, true, CHECK_PROGRAM_OK },
            { CBUILTIN_ATOMIC_FETCH_ADD, 0, true, CHECK_PROGRAM_INVALID },
            { CBUILTIN_ATOMIC_FETCH_ADD, 3, true, CHECK_PROGRAM_INVALID },
            { CBUILTIN_ATOMIC_FETCH_ADD, 7, false, CHECK_PROGRAM_INVALID },
            { CBUILTIN_SYNC_FETCH_AND_ADD, 3, true, CHECK_PROGRAM_OK },
            { CBUILTIN_SYNC_FETCH_AND_ADD, 7, true, CHECK_PROGRAM_INVALID },
            { CBUILTIN_ATOMIC_COMPARE_EXCHANGE_N, 63, true, CHECK_PROGRAM_OK },
            { CBUILTIN_ATOMIC_COMPARE_EXCHANGE_N, 7, true, CHECK_PROGRAM_INVALID },
            { CBUILTIN_SYNC_LOCK_RELEASE, 1, false, CHECK_PROGRAM_OK },
            { CBUILTIN_SYNC_LOCK_RELEASE, 1, true, CHECK_PROGRAM_INVALID },
            { CBUILTIN_SYNC_LOCK_RELEASE, 3, false, CHECK_PROGRAM_INVALID },
        };
        CheckTestTypes types = { 0 };
        const bool typed = check_test_types_open(&types);
        check_test_expect(test, typed, "read/modify/write planner test types");
        uint32_t argument_counts[BUSTER_ARRAY_LENGTH(ops)] = { 0 };
        CheckInput counted = input;
        counted.graph.argument_counts = argument_counts;
        for (uint32_t i = 0; typed && i < BUSTER_ARRAY_LENGTH(rmw_cases); i += 1)
        {
            const CheckProgramHelperRmwCase* rmw = rmw_cases + i;
            const CheckBuiltinAtomicRmwRule* rule = check_builtin_atomic_rmw_rule(rmw->id);
            const CheckBuiltinAtomicEffect saved = atomics[1];
            const uint32_t saved_member = members[1];
            CheckProgram changed = program;
            changed.input = &counted;
            changed.types = &types.table;
            argument_counts[1] = rule->fixed_count;
            members[1] = rmw->id;
            atomics[1].runtime_builtin = rmw->id;
            atomics[1].value_type = BUSTER_TYPE_LONG;
            atomics[1].evaluated_operands = rmw->operands;
            atomics[1].rmw = rmw->rmw;
            Arena bounded = output;
            CheckProgramHelpers plan = { 0 };
            const bool accepted = check_program_helpers(&plan, &changed, &bounded, &scratch);
            bool leaf = false;
            for (uint32_t h = 0; accepted && h < plan.count; h += 1)
            {
                const CheckProgramHelper* helper = plan.items + h;
                leaf |= helper->id == rmw->id && helper->rmw && helper->bytes == 8 && helper->parameter_count == rule->fixed_count &&
                        helper->code.count != 0;
            }
            check_test_expect(test, accepted == (rmw->status == CHECK_PROGRAM_OK) && plan.status == rmw->status && plan.reason == NULL &&
                                   (accepted ? leaf : plan.count == 0 && plan.items == 0 && bounded.length == 64) && scratch.length == 64,
                              "a read/modify/write's effect is planned as its leaf only when its shape is its rule's");
            atomics[1] = saved;
            members[1] = saved_member;
            argument_counts[1] = 0;
        }
        check_test_types_close(&types);
        program.builtin_count = 0;
        CheckProgramHelpers empty = { 0 };
        check_test_expect(test, check_program_helpers(&empty, &program, &output, &scratch) && empty.count == 0 && empty.use_count == 0 &&
                               output.length == 64 && scratch.length == 64,
                          "an empty active set ignores all source-only descriptors and needs no helper storage");
    }
    arena_release(&scratch);
    arena_release(&output);
    check_test_program_math_helpers(test);
    check_test_program_helper_contracts(test);
}

#endif
