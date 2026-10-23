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
            atomics[1] = saved;
            floating[0] = saved_float;
            active[0] = 1;
        }
        program.builtin_count = 0;
        CheckProgramHelpers empty = { 0 };
        check_test_expect(test, check_program_helpers(&empty, &program, &output, &scratch) && empty.count == 0 && empty.use_count == 0 &&
                               output.length == 64 && scratch.length == 64,
                          "an empty active set ignores all source-only descriptors and needs no helper storage");
    }
    arena_release(&scratch);
    arena_release(&output);
    check_test_program_math_helpers(test);
}

#endif
