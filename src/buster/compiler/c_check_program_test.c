// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Real C source -> checked bodies -> unit ABI -> Tier0 -> shared code/data Link -> ELF. The independent Clang
// build of the same source is linked beside our object and executed on the same bounded inputs. Both source and
// backend schedules are exercised, and full Tier0 columns (including ABI locations) agree before code emission.
// Include after c_checker_test.c's shared helpers and c_check_program.c.
#ifndef BUSTER_C_CHECK_PROGRAM_TEST_INCLUDED
#define BUSTER_C_CHECK_PROGRAM_TEST_INCLUDED

#include "c_check_program_helpers_test.c"
#include "c_check_program_diagnostic_test.c"
#include "c_check_control_input_test.c"
#include "c_check_program_lifetime_test.c"

static void check_test_program_addressed(CheckTest* test)
{
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    uint8_t* mappings[3] = { 0 };
    bool ready = true;
    for (uint32_t column = 0; column < 3; column += 1)
    {
        mappings[column] = mmap(0, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        ready = mappings[column] != MAP_FAILED && ready;
        if (mappings[column] != MAP_FAILED)
        {
            ready = mprotect(mappings[column] + page, page, PROT_NONE) == 0 && ready;
        }
    }
    check_test_expect(test, ready, "label address columns have protected tails");
    // The sparse label targets (c_check_sparse.c) the guarded columns' values make.
    Arena sparse = arena_reserve(UINT64_C(1) << 20);
    for (uint32_t count = 0; ready && count <= 129; count += 1)
    {
        uint8_t* kinds = mappings[0] + page - count;
        uint8_t* roles = mappings[1] + page - count;
        uint32_t* targets = (uint32_t*)(mappings[2] + page - (uint64_t)count * sizeof(uint32_t));
        uint8_t expected[130] = { 0 };
        uint8_t vector[130];
        for (uint32_t token = 0; token < count; token += 1)
        {
            kinds[token] = token % 2 == 0 ? PARSE_KIND_AMPAMP : PARSE_KIND_IDENTIFIER;
            roles[token] = token % 3 == 0 ? PARSE_ROLE_PREFIX : PARSE_ROLE_LOGICAL_AND;
            targets[token] = token % 5 == 0 ? count : (token * 7) % count;
            if (token % 6 == 0 && token % 5 != 0)
            {
                expected[targets[token]] = 1;
            }
        }
        Parser parser = { 0 };
        parser.tokens.kinds = kinds;
        parser.tree.roles = roles;
        const uint64_t mark = sparse.length;
        const CheckInput input = { .token_count = count, .label_targets = check_sparse_from_values(&sparse, targets, count) };
        check_program_addressed(&parser, &input, vector);
        check_test_expect(test, check_sparse_present(input.label_targets) && memcmp(vector, expected, count + 1) == 0,
                          "label addresses exclude logical && and non-prefix tokens, including every guarded vector tail");
        arena_pop_to(&sparse, mark);
    }
    for (uint32_t column = 0; column < 3; column += 1)
    {
        if (mappings[column] != MAP_FAILED)
        {
            munmap(mappings[column], page * 2);
        }
    }
    arena_release(&sparse);
}

#define BUSTER_CHECK_TEST_SCAN_COLUMNS 24u
#define BUSTER_CHECK_TEST_SCAN_TOKENS 129u

// check_program_plans on a scan's random tree (#877): random initializers, some compound literals' and some whose
// declarations have no statement, each column, plan_first and the `planned` bitset ending at a PROT_NONE page, and
// plan_first and plan_next poisoned. Against an oracle that walks each initializer's parents with a set of the tokens it
// saw: a statement's bit is set exactly when some plan is its, its list is its plans in order, and no other entry of
// plan_first or of plan_next is written; a compound literal's plan joins no list, and one with no statement fails. With
// `listed`, `plan_count` plans: as many as the tokens, at random tokens, or fewer, at ascending tokens as the producer writes
// them, with plan_next sized for them alone. Without, no CheckInitializers and no plan_next, as check_program passes for a
// unit without plans: nothing is written or marked.
static void check_test_program_plans(CheckTest* test, const CheckInput* base, const Parser* parser, uint8_t* const* mappings, uint64_t page, uint32_t plan_count,
                                     bool listed)
{
    const uint32_t count = base->token_count;
    const uint32_t plans = listed ? plan_count : 0;
    const uint32_t words = (count + 64) / 64;
    // The base's declaration bits, with the compound literals' drawn here.
    uint8_t* declaration_bits = mappings[0] + page - count;
    uint32_t* initializer_begins = (uint32_t*)(mappings[1] + page - (uint64_t)count * sizeof(uint32_t));
    uint32_t* declaration_tokens = (uint32_t*)(mappings[2] + page - (uint64_t)plans * sizeof(uint32_t));
    uint32_t* plan_first = (uint32_t*)(mappings[3] + page - ((uint64_t)count + 1) * sizeof(uint32_t));
    uint64_t* planned = (uint64_t*)(mappings[4] + page - (uint64_t)words * sizeof(uint64_t));
    uint32_t* plan_next = (uint32_t*)(mappings[5] + page - ((uint64_t)plans + 1) * sizeof(uint32_t));
    const uint32_t poison = 0xa5a5a5a5u;
    for (uint32_t token = 0; token < count; token += 1)
    {
        const uint32_t draw = check_test_random(test);
        const uint8_t kept = base->declaration_bits != NULL ? (uint8_t)(base->declaration_bits[token] & ~BUSTER_CHECK_BITS_COMPOUND) : 0;
        declaration_bits[token] = (uint8_t)(kept | (draw % 6 == 0 ? BUSTER_CHECK_BITS_COMPOUND : 0));
        initializer_begins[token] = (draw >> 3) % 5 == 0 ? 0 : (draw >> 3) % 5 == 1 ? count + 1 : 1 + (draw >> 6) % count;
    }
    for (uint32_t i = 0; i < plans; i += 1)
    {
        const uint32_t draw = check_test_random(test);
        const uint32_t step = count / plans;
        declaration_tokens[i] = plans < count ? i * step + draw % step : draw % 9 == 0 ? count + draw % 3 : (draw >> 4) % (count != 0 ? count : 1);
    }
    CheckInput input = *base;
    input.declaration_bits = declaration_bits;
    Arena sparse = arena_reserve(UINT64_C(1) << 20);
    input.initializer_begins = check_sparse_from_values(&sparse, initializer_begins, count);
    const CheckInitializers initializers = { .declaration_tokens = declaration_tokens, .count = plans, .token_count = count };
    const CheckInitializers* given = listed ? &initializers : NULL;
    uint32_t* next = listed ? plan_next : NULL;
    for (uint32_t token = 0; token <= count; token += 1)
    {
        plan_first[token] = poison;
    }
    for (uint32_t i = 0; i <= plans; i += 1)
    {
        plan_next[i] = poison;
    }
    memset(planned, 0, (uint64_t)words * sizeof(uint64_t));
    CheckProgram program = { .input = &input, .status = CHECK_PROGRAM_OK, .token = BUSTER_CHECK_NONE };
    uint32_t parents[BUSTER_CHECK_TEST_SCAN_TOKENS + 1];
    parents[count] = poison;
    const bool compound = check_program_plans(&program, parser, given, plan_first, planned, next, parents);
    // The oracle: each plan's statement, or a compound literal's mark, or none.
    uint32_t statements[BUSTER_CHECK_TEST_SCAN_TOKENS + 1];
    bool expected_compound = false;
    CheckProgramStatus expected_status = CHECK_PROGRAM_OK;
    uint32_t expected_token = BUSTER_CHECK_NONE;
    bool same = true;
    for (uint32_t i = plans; i-- > 0;)
    {
        const uint32_t declaration = declaration_tokens[i];
        const bool literal = declaration < count && check_input_bit(declaration_bits, declaration, BUSTER_CHECK_BITS_COMPOUND);
        uint32_t statement = BUSTER_CHECK_NONE;
        if (!literal)
        {
            bool seen[BUSTER_CHECK_TEST_SCAN_TOKENS + 1] = { 0 };
            uint32_t token = declaration < count && initializer_begins[declaration] != 0 ? initializer_begins[declaration] - 1 : BUSTER_CHECK_NONE;
            while (token < count && !seen[token] && parser->tree.roles[token] != PARSE_ROLE_TERMINATOR)
            {
                seen[token] = true;
                token = parser->tree.parents[token];
            }
            statement = token < count && parser->tree.roles[token] == PARSE_ROLE_TERMINATOR ? token : BUSTER_CHECK_NONE;
            if (statement == BUSTER_CHECK_NONE && expected_status == CHECK_PROGRAM_OK)
            {
                expected_status = CHECK_PROGRAM_INVALID;
                expected_token = declaration;
            }
        }
        expected_compound |= literal && check_input_bit(base->declaration_bits, declaration, BUSTER_CHECK_BITS_STATIC) == 0;
        statements[i] = statement;
        same &= !literal || plan_next[i] == BUSTER_CHECK_NONE;
        same &= literal || statement != BUSTER_CHECK_NONE || plan_next[i] == poison;
    }
    for (uint32_t token = 0; token < words * 64; token += 1)
    {
        uint32_t first = BUSTER_CHECK_NONE;
        for (uint32_t i = plans; i-- > 0;)
        {
            first = statements[i] == token ? i : first;
        }
        const bool bit = ((planned[token / 64] >> (token % 64)) & 1) != 0;
        same &= bit == (first != BUSTER_CHECK_NONE);
        same &= token > count || first != BUSTER_CHECK_NONE || plan_first[token] == poison;
        // Its list: every plan of the statement, in order, then its end.
        uint32_t link = first != BUSTER_CHECK_NONE ? plan_first[token] : BUSTER_CHECK_NONE;
        for (uint32_t i = 0; first != BUSTER_CHECK_NONE && i < plans; i += 1)
        {
            if (statements[i] == token)
            {
                same &= link == i;
                link = link < plans ? plan_next[link] : BUSTER_CHECK_NONE;
            }
        }
        same &= link == BUSTER_CHECK_NONE;
    }
    same &= plan_first[count] == poison && plan_next[plans] == poison && parents[count] == poison;
    check_test_expect(test, same && compound == expected_compound && program.status == expected_status && program.token == expected_token,
                      listed ? "check_program's plans link each statement's initializers in order and mark exactly those statements"
                             : "check_program's plans write and mark nothing for a unit without initializer plans");
    arena_release(&sparse);
}

// The cache is earned by accumulated work, not by a single initializer. One and two long walks end before the next
// query could activate it; the third crosses that boundary. Poison distinguishes no-copy from a copied parent column.
static void check_test_program_plan_cache(CheckTest* test)
{
    enum { count = 257, plans = 65, words = (count + 64) / 64 };
    uint32_t parents[count];
    uint8_t roles[count];
    uint8_t declaration_bits[count] = { 0 };
    uint32_t initializer_begins[count];
    uint32_t declarations[plans];
    for (uint32_t token = 0; token < count; token += 1)
    {
        parents[token] = token + 1;
        roles[token] = token + 1 == count ? PARSE_ROLE_TERMINATOR : PARSE_ROLE_SEPARATOR;
        initializer_begins[token] = token + 1;
    }
    for (uint32_t i = 0; i < plans; i += 1)
    {
        declarations[i] = i;
    }
    Parser parser = { 0 };
    parser.tokens.count = count;
    parser.tree.parents = parents;
    parser.tree.roles = roles;
    Arena arena = arena_reserve(UINT64_C(1) << 20);
    CheckInput input = { .token_count = count, .declaration_bits = declaration_bits };
    input.initializer_begins = check_sparse_from_values(&arena, initializer_begins, count);
    const uint32_t sizes[] = { 1, 2, 3, plans };
    for (uint32_t variant = 0; variant < BUSTER_ARRAY_LENGTH(sizes); variant += 1)
    {
        const uint32_t size = sizes[variant];
        uint32_t cached[count + 1];
        uint32_t first[count + 1];
        uint32_t next[plans + 1];
        uint64_t planned[words] = { 0 };
        for (uint32_t token = 0; token <= count; token += 1)
        {
            cached[token] = 0xa5a5a5a5u;
            first[token] = 0xa5a5a5a5u;
        }
        for (uint32_t i = 0; i <= plans; i += 1)
        {
            next[i] = 0xa5a5a5a5u;
        }
        const CheckInitializers initializers = { .declaration_tokens = declarations, .count = size, .token_count = count };
        CheckProgram program = { .input = &input, .status = CHECK_PROGRAM_OK, .token = BUSTER_CHECK_NONE };
        const bool compound = check_program_plans(&program, &parser, &initializers, first, planned, next, cached);
        bool same = check_sparse_present(input.initializer_begins) && !compound && program.status == CHECK_PROGRAM_OK && first[count - 1] == 0;
        for (uint32_t token = 0; token < count; token += 1)
        {
            same &= parents[token] == token + 1 && (size > 2 || cached[token] == 0xa5a5a5a5u);
            same &= check_program_bit(planned, token) == (token + 1 == count);
        }
        for (uint32_t i = 0; i < size; i += 1)
        {
            same &= next[i] == (i + 1 < size ? i + 1 : BUSTER_CHECK_NONE);
        }
        same &= size <= 2 || cached[0] == count - 1;
        check_test_expect(test, same && cached[count] == 0xa5a5a5a5u && next[plans] == 0xa5a5a5a5u,
                          "initializer ancestry leaves cheap scratch untouched and activates compression only after its raw-walk budget");
    }
    arena_release(&arena);
}

// The definitions #530's scans are checked against, a token or a node at a time by the same per-item tests: the bodies'
// openings (the definitions' bodies, but deferred ones that no node producer took), in token order.
static uint32_t check_test_program_openings_definition(const CheckInput* input, uint32_t* openings)
{
    uint32_t count = 0;
    for (uint32_t token = 0; token < input->token_count; token += 1)
    {
        if (check_sparse_get(input->body_declarations, token) != BUSTER_CHECK_NONE &&
            (input->deferred_tokens == NULL || input->deferred_tokens[token] == 0 || input->node_bodies))
        {
            openings[count++] = token;
        }
    }
    return count;
}

// The variable-length arrays' objects (check_program_runtime_object), each one's allocation from the last token back.
static void check_test_program_allocations_definition(CheckProgram* program, const Parser* parser, const TypeTable* types, uint32_t* allocation_first,
                                                      uint32_t* allocation_next, uint32_t* vla_blocks, uint32_t* vla_saves,
                                                      const CheckProgramStatements* statements)
{
    for (uint32_t token = program->input->token_count; token != 0;)
    {
        token -= 1;
        if (check_program_runtime_object(program->input, types, token))
        {
            check_program_allocation(program, parser, token, allocation_first, allocation_next, vla_blocks, vla_saves, statements);
        }
    }
}

// The reached builtins' nodes, in node order, listed where `listed` has room for them: their count.
static uint32_t check_test_program_builtins_definition(const CheckGraph* graph, const uint32_t* nodes, uint32_t* listed)
{
    uint32_t found = 0;
    for (uint32_t node = 0; node < graph->count; node += 1)
    {
        if (nodes[node] != 0 && check_program_builtin_op(graph->ops[node]))
        {
            if (listed != NULL)
            {
                listed[found] = node;
            }
            found += 1;
        }
    }
    return found;
}

// The static declarations (each its own declaration, with the static bit), each one's object in token order.
static bool check_test_program_static_objects_definition(CheckProgram* program, uint8_t* needed, Arena* output, CheckProgramTargetEvents* events)
{
    const CheckInput* input = program->input;
    bool room = true;
    for (uint32_t token = 0; token < input->token_count; token += 1)
    {
        if (input->declarations[token] == token && check_input_bit(input->declaration_bits, token, BUSTER_CHECK_BITS_STATIC))
        {
            room = check_program_static_object(program, needed, token, output, room, events);
        }
    }
    return room;
}

// #530's scans in check_program against their definitions (above): at every count to 129 tokens, each column
// they read ending at a PROT_NONE page, random columns around the values each test separates. The bodies' openings and the
// initializers' storage durations; the initializer plans' statements (check_test_program_plans, #877); the
// variable-length arrays' declarations; the reached builtins; the static objects' declarations,
// needs and naked functions, with room for their column and without (whose symbols #442's kernels number:
// check_test_program_declaring); and at 64 and 128 tokens, the cases' marks, against a test of each token.
static void check_test_program_scans(CheckTest* test)
{
    check_test_program_plan_cache(test);
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    uint8_t* mappings[BUSTER_CHECK_TEST_SCAN_COLUMNS] = { 0 };
    bool ready = page >= (BUSTER_CHECK_TEST_SCAN_TOKENS + 1) * sizeof(uint32_t);
    for (uint32_t column = 0; column < BUSTER_CHECK_TEST_SCAN_COLUMNS; column += 1)
    {
        mappings[column] = mmap(0, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        ready = mappings[column] != MAP_FAILED && ready;
        if (mappings[column] != MAP_FAILED)
        {
            ready = mprotect(mappings[column] + page, page, PROT_NONE) == 0 && ready;
        }
    }
    CheckTestTypes types = { 0 };
    ready = check_test_types_open(&types) && ready;
    // The sparse columns (c_check_sparse.c) the guarded columns' values make, for each count's variants.
    Arena sparse = arena_reserve(UINT64_C(1) << 22);
    ready = sparse.base != NULL && ready;
    check_test_expect(test, ready, "check_program's scanned columns have protected tails");
    // A static declaration's attributes: GNU's naked one, naked but ignored, and another (CheckProgram.object_naked);
    // and a target attribute, one ignored and target_clones (CheckProgramTargetEvents).
    CheckInputAttribute records[6] = {
        {
            .kind = CHECK_ATTRIBUTE_NAKED,
        },
        {
            .kind = CHECK_ATTRIBUTE_NAKED,
            .ignored = true,
        },
        {
            .kind = CHECK_ATTRIBUTE_NO_STACK_PROTECTOR,
        },
        {
            .kind = CHECK_ATTRIBUTE_TARGET,
        },
        {
            .kind = CHECK_ATTRIBUTE_TARGET,
            .ignored = true,
        },
        {
            .kind = CHECK_ATTRIBUTE_TARGET_CLONES,
        },
    };
    CheckInputAttributes kinds_of_attributes[6] = {
        {
            .records = records,
            .count = 1,
        },
        {
            .records = records + 1,
            .count = 1,
        },
        {
            .records = records + 1,
            .count = 2,
        },
        {
            .records = records + 3,
            .count = 1,
        },
        {
            .records = records + 4,
            .count = 1,
        },
        {
            .records = records + 4,
            .count = 2,
        },
    };
    CheckInputAttributes* attributes[BUSTER_CHECK_TEST_SCAN_TOKENS + 1];
    const TypeId runtime = ready ? type_variable_array(&types.table, BUSTER_TYPE_INT, 1) : 0;
    for (uint32_t count = 0; ready && count <= BUSTER_CHECK_TEST_SCAN_TOKENS; count += 1)
    {
        uint32_t* body_declarations = (uint32_t*)(mappings[0] + page - (uint64_t)count * sizeof(uint32_t));
        uint32_t* initializer_nodes = (uint32_t*)(mappings[1] + page - (uint64_t)count * sizeof(uint32_t));
        uint8_t* deferred = mappings[2] + page - count;
        uint32_t* declarations = (uint32_t*)(mappings[3] + page - (uint64_t)count * sizeof(uint32_t));
        uint32_t* declaration_bodies = (uint32_t*)(mappings[4] + page - (uint64_t)count * sizeof(uint32_t));
        uint8_t* declaration_kinds = mappings[5] + page - count;
        uint8_t* declaration_bits = mappings[6] + page - count;
        uint32_t* objects = (uint32_t*)(mappings[7] + page - (uint64_t)count * sizeof(uint32_t));
        CheckOp* ops = (CheckOp*)(mappings[10] + page - (uint64_t)count * sizeof(CheckOp));
        uint32_t* reached = (uint32_t*)(mappings[11] + page - (uint64_t)count * sizeof(uint32_t));
        uint8_t* roles = mappings[12] + page - count;
        uint8_t* kinds = mappings[13] + page - count;
        TypeId* declared_types = (TypeId*)(mappings[14] + page - (uint64_t)count * sizeof(TypeId));
        uint32_t* parents = (uint32_t*)(mappings[15] + page - (uint64_t)count * sizeof(uint32_t));
        const uint32_t node_count = count;
        for (uint32_t token = 0; token < count; token += 1)
        {
            const uint32_t draw = check_test_random(test);
            body_declarations[token] = draw % 7 == 0 ? draw % (count + 1) : BUSTER_CHECK_NONE;
            initializer_nodes[token] = draw % 5 == 0 ? (draw >> 8) % node_count : draw % 5 == 1 ? node_count + draw % 3 : BUSTER_CHECK_NONE;
            deferred[token] = (uint8_t)(draw % 11 == 0);
            declarations[token] = draw % 3 != 0 ? token : (draw >> 4) % count;
            declaration_bodies[token] = draw % 13 != 0 ? (draw >> 6) % count : count + draw % 2;
            declaration_kinds[token] = (uint8_t)(draw % 9 != 0 ? C_BINDING_OBJECT : C_BINDING_OBJECT + 1);
            declaration_bits[token] = (uint8_t)(((draw >> 9) % 3 == 0 ? BUSTER_CHECK_BITS_STATIC : 0) | (((draw >> 11) & 1) != 0 ? BUSTER_CHECK_BITS_DEFINED : 0) |
                                                (((draw >> 12) & 1) != 0 ? BUSTER_CHECK_BITS_TENTATIVE : 0));
            objects[token] = (draw >> 3) % (count + 2);
            ops[token] = (CheckOp)((draw >> 13) % 4 == 0 ? CHECK_OP_BUILTIN_UNARY + (draw >> 15) % (CHECK_OP_COUNT - CHECK_OP_BUILTIN_UNARY) : (draw >> 15) % CHECK_OP_COUNT);
            reached[token] = (draw >> 20) % 3 == 0 ? 0 : 1 + (draw >> 22) % 4;
            roles[token] = (uint8_t)((draw >> 23) % 5 == 0 ? PARSE_ROLE_CLOSE_CASE : (draw >> 23) % 5 == 1 ? PARSE_ROLE_TERMINATOR : PARSE_ROLE_LEAF_RIGHT);
            kinds[token] = (uint8_t)((draw >> 26) % 4 == 0 ? PARSE_KIND_SWITCH : PARSE_KIND_IDENTIFIER);
            declared_types[token] = (draw >> 28) % 2 == 0 ? runtime : BUSTER_TYPE_INT;
            parents[token] = (draw >> 29) % 4 == 0 ? BUSTER_CHECK_NONE : (draw >> 10) % count;
            const uint32_t attribute = (draw >> 16) % 12;
            attributes[token] = attribute < 6 ? &kinds_of_attributes[attribute] : NULL;
        }
        for (uint32_t variant = 0; variant < 2; variant += 1)
        {
            const CheckInput input = {
                .graph = { .count = node_count },
                .token_count = count,
                .deferred_tokens = variant == 0 ? deferred : NULL,
                .declarations = declarations,
                .declared_types = declared_types,
                .declaration_kinds = declaration_kinds,
                .objects = objects,
                .declaration_bits = declaration_bits,
                .initializer_nodes = check_sparse_from_values(&sparse, initializer_nodes, count),
                .body_declarations = check_sparse_from_values(&sparse, body_declarations, count),
                .declaration_bodies = check_sparse_from_values(&sparse, declaration_bodies, count),
                .attributes = check_input_attributes_from(&sparse, attributes, count),
            };
            uint32_t openings[BUSTER_CHECK_TEST_SCAN_TOKENS + 1];
            const uint32_t words = (node_count + 64) / 64;
            uint64_t* statics = (uint64_t*)(mappings[16] + page - (uint64_t)words * sizeof(uint64_t));
            uint32_t defined[BUSTER_CHECK_TEST_SCAN_TOKENS + 1];
            const uint32_t defined_openings = check_test_program_openings_definition(&input, defined);
            const uint32_t vector_openings = check_program_openings(&input, openings);
            memset(statics, 0xa5, (uint64_t)words * sizeof(uint64_t));
            check_program_static_initializers(&input, statics);
            bool marked = true;
            for (uint32_t node = 0; node < words * 64; node += 1)
            {
                uint32_t last = BUSTER_CHECK_NONE;
                for (uint32_t token = 0; token < count; token += 1)
                {
                    last = initializer_nodes[token] == node && node < node_count ? token : last;
                }
                marked &= check_program_bit(statics, node) == (last != BUSTER_CHECK_NONE && check_input_bit(declaration_bits, last, BUSTER_CHECK_BITS_STATIC));
            }
            check_test_expect(test, marked && defined_openings == vector_openings && memcmp(defined, openings, defined_openings * sizeof(uint32_t)) == 0,
                              "the openings agree at every guarded tail and each initializer has its last declaration's static bit");
            Parser parser = { 0 };
            parser.tree.roles = roles;
            parser.tree.parents = parents;
            parser.tokens.kinds = kinds;
            parser.tokens.count = count;
            uint32_t allocation_first[2][BUSTER_CHECK_TEST_SCAN_TOKENS + 1];
            uint32_t allocation_next[2][BUSTER_CHECK_TEST_SCAN_TOKENS + 1];
            uint32_t vla_blocks[2][BUSTER_CHECK_TEST_SCAN_TOKENS + 1];
            uint32_t vla_saves[2][BUSTER_CHECK_TEST_SCAN_TOKENS + 1];
            // Each path's walks to the statements keep their own answers (check_program_statement, #2995).
            uint32_t statement_ends[2][BUSTER_CHECK_TEST_SCAN_TOKENS + 1];
            uint32_t statement_keys[2][BUSTER_CHECK_TEST_SCAN_TOKENS + 1];
            uint64_t statement_known[2][(BUSTER_CHECK_TEST_SCAN_TOKENS + 64) / 64];
            CheckProgram programs[2] = { 0 };
            for (uint32_t path = 0; path < 2; path += 1)
            {
                programs[path] = (CheckProgram){ .input = &input, .types = &types.table, .status = CHECK_PROGRAM_OK, .token = BUSTER_CHECK_NONE };
                memset(allocation_first[path], 0xff, sizeof(allocation_first[path]));
                memset(allocation_next[path], 0xa5, sizeof(allocation_next[path]));
                memset(vla_blocks[path], 0, sizeof(vla_blocks[path]));
                memset(vla_saves[path], 0, sizeof(vla_saves[path]));
                memset(statement_known[path], 0, sizeof(statement_known[path]));
                const CheckProgramStatements statements = {
                    .ends = statement_ends[path],
                    .keys = statement_keys[path],
                    .known = statement_known[path],
                };
                if (path == 1)
                {
                    check_test_program_allocations_definition(&programs[path], &parser, &types.table, allocation_first[path], allocation_next[path], vla_blocks[path],
                                                              vla_saves[path], &statements);
                }
                else
                {
                    check_program_allocations(&programs[path], &parser, &types.table, allocation_first[path], allocation_next[path], vla_blocks[path], vla_saves[path],
                                              &statements);
                }
            }
            check_test_expect(test, programs[1].status == programs[0].status && programs[1].token == programs[0].token &&
                                   memcmp(allocation_first[1], allocation_first[0], sizeof(allocation_first[0])) == 0 &&
                                   memcmp(allocation_next[1], allocation_next[0], sizeof(allocation_next[0])) == 0 &&
                                   memcmp(vla_blocks[1], vla_blocks[0], sizeof(vla_blocks[0])) == 0 && memcmp(vla_saves[1], vla_saves[0], sizeof(vla_saves[0])) == 0,
                              "check_program's variable-length array declarations are their definition's at every guarded tail");
            check_test_program_plans(test, &input, &parser, mappings + 18, page, count, true);
            check_test_program_plans(test, &input, &parser, mappings + 18, page, count / 3, true);
            check_test_program_plans(test, &input, &parser, mappings + 18, page, 0, false);
            const CheckGraph graph = { .ops = ops, .count = node_count };
            uint32_t builtins[2][BUSTER_CHECK_TEST_SCAN_TOKENS + 1];
            uint32_t builtin_counts[2];
            builtin_counts[1] = check_test_program_builtins_definition(&graph, reached, builtins[1]);
            builtin_counts[0] = check_program_builtins(&graph, reached, NULL);
            const uint32_t listed = check_program_builtins(&graph, reached, builtins[0]);
            check_test_expect(test, listed == builtin_counts[0], "check_program's builtins count what they list");
            check_test_expect(test, builtin_counts[0] == builtin_counts[1] && memcmp(builtins[0], builtins[1], builtin_counts[0] * sizeof(uint32_t)) == 0,
                              "check_program's reached builtins are their definition's at every guarded tail");
            uint8_t needed[2][BUSTER_CHECK_TEST_SCAN_TOKENS + 1];
            uint32_t object_declarations[2][BUSTER_CHECK_TEST_SCAN_TOKENS + 1];
            bool rooms[2];
            // The naked column in an arena of its own, every other time one with no room for it; and the target attributes'
            // keys in one of their own, which grows them by doubling, every other time one with no room.
            _Alignas(64) uint8_t naked_bytes[2][(BUSTER_CHECK_TEST_SCAN_TOKENS + 1) * sizeof(uint32_t) + 64];
            _Alignas(64) uint8_t event_bytes[2][4 * (BUSTER_CHECK_TEST_SCAN_TOKENS + 64) * sizeof(uint64_t)];
            CheckProgramTargetEvents events[2];
            for (uint32_t path = 0; path < 2; path += 1)
            {
                Arena naked = {
                    .base = naked_bytes[path],
                    .capacity = variant == 0 ? sizeof(naked_bytes[path]) : 0,
                };
                Arena event_arena = {
                    .base = event_bytes[path],
                    .capacity = variant == 0 ? sizeof(event_bytes[path]) : 0,
                };
                events[path] = (CheckProgramTargetEvents){ .scratch = &event_arena, .room = true };
                programs[path] = (CheckProgram){ .input = &input, .object_declarations = object_declarations[path] };
                memset(needed[path], 0, sizeof(needed[path]));
                memset(object_declarations[path], 0xff, sizeof(object_declarations[path]));
                needed[path][count / 2] = (uint8_t)(count % 2);
                rooms[path] = path == 1 ? check_test_program_static_objects_definition(&programs[path], needed[path], &naked, &events[path]) :
                                          check_program_static_objects(&programs[path], needed[path], &naked, &events[path]);
                events[path].scratch = NULL;
            }
            // Each function's declaration with a target or target_clones attribute not ignored is its object's key, in token
            // order, as the test's own walk finds them.
            uint32_t expected_events = 0;
            bool events_right = true;
            for (uint32_t token = 0; token < count; token += 1)
            {
                const uint32_t object = objects[token];
                const bool event = declarations[token] == token && check_input_bit(declaration_bits, token, BUSTER_CHECK_BITS_STATIC) && object != 0 && object <= count &&
                                   declaration_kinds[token] == C_BINDING_FUNCTION &&
                                   (attributes[token] == &kinds_of_attributes[3] || attributes[token] == &kinds_of_attributes[5]);
                events_right &= !event || expected_events >= events[0].count || events[0].keys[expected_events] == (((uint64_t)object << 32) | token);
                expected_events += event;
            }
            const bool limited = variant != 0 && expected_events != 0;
            events_right &= limited ? !events[0].room && events[0].count == 0 : events[0].room && events[0].count == expected_events;
            const bool defined_naked = (programs[1].object_naked == 0) == (programs[0].object_naked == 0) &&
                                       (programs[1].object_naked == 0 ||
                                        memcmp(programs[1].object_naked, programs[0].object_naked, ((uint64_t)count + 1) * sizeof(uint32_t)) == 0);
            const bool defined_events = events[1].room == events[0].room && events[1].count == events[0].count &&
                                        (events[1].count == 0 || memcmp(events[1].keys, events[0].keys, events[1].count * sizeof(uint64_t)) == 0);
            check_test_expect(test, memcmp(needed[1], needed[0], sizeof(needed[0])) == 0 &&
                                   memcmp(object_declarations[1], object_declarations[0], sizeof(object_declarations[0])) == 0 && rooms[1] == rooms[0] &&
                                   defined_naked && defined_events && events_right,
                              "check_program's static objects, naked functions and target attributes are their definition's at every guarded tail");
            for (uint32_t block = 0; count % 64 == 0 && block < count; block += 64)
            {
                uint64_t cases = 0;
                const uint64_t marked = check_program_case_marks(&parser, &input, block, &cases);
                uint64_t expected_marked = 0;
                uint64_t expected_cases = 0;
                for (uint32_t lane = 0; lane < 64; lane += 1)
                {
                    const uint32_t token = block + lane;
                    const bool kept = input.deferred_tokens == NULL || input.deferred_tokens[token] == 0;
                    expected_marked |= (uint64_t)(kept && (roles[token] == PARSE_ROLE_CLOSE_CASE || kinds[token] == PARSE_KIND_SWITCH)) << lane;
                    expected_cases |= (uint64_t)(kept && roles[token] == PARSE_ROLE_CLOSE_CASE) << lane;
                }
                check_test_expect(test, marked == expected_marked && cases == expected_cases, "check_program's case marks agree with each token's test at a guarded tail");
            }
        }
    }
    check_test_types_close(&types);
    arena_release(&sparse);
    for (uint32_t column = 0; column < BUSTER_CHECK_TEST_SCAN_COLUMNS; column += 1)
    {
        if (mappings[column] != MAP_FAILED)
        {
            munmap(mappings[column], page * 2);
        }
    }
}

// #530's review: bodies whose controls check_control_children's columns decide, several in one unit: switches of their own
// (one case in a nested block, a range, a nested switch), nested blocks, labels of one name in two bodies with gotos
// forward and back, loops' breaks and continues, and a statement expression with a switch, a label and a goto of its own.
static const char check_test_program_bodies_source[] =
    "static int classify(int x){int r=0;switch(x&7){case 0:r=1;break;case 1:case 2:r=2;{int y=x;switch(y%3){case 0:r+=10;break;default:r+=20;}}break;"
    "case 3 ... 5:r=3;goto out;default:{r=4;case 6:r+=5;}}r*=2;out:return r;}"
    "static int walk(int n){int s=0;int i=0;top:if(i>=n)goto out;{int j=0;while(j<i){if(j==3){j++;continue;}s+=j;j++;}}"
    "switch(i%4){case 0:s+=100;break;case 2:i++;goto top;default:s-=1;}i++;goto top;out:do{s+=1;if(s>1000)break;}while(s%3!=0);return s;}"
    "static int region(int n){int t=0;for(int i=0;i<n;i++){t+=({int u=0;switch(i%3){case 0:u=1;break;case 1:u=2;break;default:if(i>7)break;u=3;}"
    "again:u+=1;if(u<3)goto again;u;});if(t>50)break;}return t;}"
    "int tested(int n){int a=n<0?-n:n;return classify(a)+walk(a&15)*3+region(a&15)*7+classify(a+1);}";

// The next of `parent`'s children in the unit-wide lists, from `child` (BUSTER_CHECK_NONE for its first), that is in
// [opening, closing]: the children a body's own lists keep.
static uint32_t check_test_program_inside_child(const uint32_t* first, const uint32_t* next, uint32_t parent, uint32_t child, uint32_t opening, uint32_t closing)
{
    uint32_t at = child == BUSTER_CHECK_NONE ? first[parent] : next[child];
    while (at != BUSTER_CHECK_NONE && (at < opening || at > closing))
    {
        at = next[at];
    }
    return at;
}

// #530's review: check_control_children's columns for each body against the unit-wide construction they replaced, the
// independent expectation (the scalar and vector paths share the builder, so their comparison can't see its bugs). For
// every body of `source`, each column starting poisoned: each token's children in order and in reverse, those outside the
// body left out; the labels and cases numbered from 0 in the body's token order and every other token unlabeled; the cases'
// targets the labels and the body's run of the program's cases exactly its own; and no loop's stop or repeat yet.
static void check_test_program_body_columns(CheckTest* test, const char* source, const char* name)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    const PPOptions pp_options = {
        .language = { .standard = 2017, .gnu = true },
        .no_standard_includes = true,
    };
    const ParseOptions parse_options = { 0 };
    bool ready = intern_initialize(&identifiers) && pp_initialize(&pp, &pp_options, &identifiers) &&
                 pp_run_source(&pp, name, (const uint8_t*)source, (uint32_t)strlen(source)) &&
                 parse_initialize(&parser, &parse_options, pp_options.language) && parse_translation_unit(&parser, &pp) && parser.errors == 0;
    const uint32_t count = ready ? parser.tokens.count : 0;
    const uint64_t slots = (uint64_t)count + 1;
    uint32_t* columns = ready ? malloc(slots * 13 * sizeof(uint32_t)) : 0;
    ready = ready && columns != 0;
    check_test_expect(test, ready, "the body columns' source parses");
    uint32_t bodies = 0;
    uint32_t labels_seen = 0;
    uint32_t cases_seen = 0;
    bool equal = ready;
    if (ready)
    {
        const uint32_t* parents = parser.tree.parents;
        const uint8_t* roles = parser.tree.roles;
        uint32_t* unit_first = columns;
        uint32_t* unit_last = unit_first + slots;
        uint32_t* unit_next = unit_last + slots;
        uint32_t* unit_previous = unit_next + slots;
        uint32_t* case_map = unit_previous + slots;
        memset(columns, 0xff, slots * 5 * sizeof(uint32_t));
        uint32_t case_count = 0;
        for (uint32_t token = 0; token < count; token += 1)
        {
            const uint32_t parent = parents[token];
            if (parent < count)
            {
                unit_previous[token] = unit_last[parent];
                if (unit_last[parent] != BUSTER_CHECK_NONE)
                {
                    unit_next[unit_last[parent]] = token;
                }
                else
                {
                    unit_first[parent] = token;
                }
                unit_last[parent] = token;
            }
            if (roles[token] == PARSE_ROLE_CLOSE_CASE)
            {
                case_map[token] = case_count++;
            }
        }
        CheckProgramCase* cases = calloc((uint64_t)case_count + 1, sizeof(CheckProgramCase));
        CheckProgram program = { .status = CHECK_PROGRAM_OK, .cases = cases, .case_count = case_count, .token = BUSTER_CHECK_NONE };
        uint32_t* body_columns = case_map + slots;
        memset(body_columns, 0xa5, slots * 8 * sizeof(uint32_t));
        CheckControlWriter writer = {
            .program = &program,
            .parser = &parser,
            .first = body_columns,
            .last = body_columns + slots,
            .next = body_columns + slots * 2,
            .previous = body_columns + slots * 3,
            .case_map = case_map,
            .source_labels = body_columns + slots * 4,
            .all_loop_targets = true,
            .stops = body_columns + slots * 5,
            .repeats = body_columns + slots * 6,
            .labels = body_columns + slots * 7,
            .capacity = count + 1,
        };
        equal = cases != 0;
        for (uint32_t opening = 0; equal && opening < count; opening += 1)
        {
            const uint32_t closing = parents[opening];
            if (roles[opening] == PARSE_ROLE_OPEN_BODY && closing < count && closing > opening)
            {
                writer.label_count = 0;
                writer.opening = opening;
                writer.closing = closing;
                check_control_children(&writer, opening, closing);
                bodies += 1;
                uint32_t label = 0;
                uint32_t case_start = BUSTER_CHECK_NONE;
                uint32_t case_end = 0;
                for (uint32_t token = opening; equal && token <= closing; token += 1)
                {
                    // In order, then in reverse, each child the unit's lists give that is in the body.
                    uint32_t ours = writer.first[token];
                    uint32_t expected = check_test_program_inside_child(unit_first, unit_next, token, BUSTER_CHECK_NONE, opening, closing);
                    for (uint32_t steps = 0; equal && steps <= count && (ours != BUSTER_CHECK_NONE || expected != BUSTER_CHECK_NONE); steps += 1)
                    {
                        equal = ours == expected;
                        ours = equal ? writer.next[ours] : ours;
                        expected = equal ? check_test_program_inside_child(unit_first, unit_next, token, expected, opening, closing) : expected;
                    }
                    ours = writer.last[token];
                    expected = check_test_program_inside_child(unit_last, unit_previous, token, BUSTER_CHECK_NONE, opening, closing);
                    for (uint32_t steps = 0; equal && steps <= count && (ours != BUSTER_CHECK_NONE || expected != BUSTER_CHECK_NONE); steps += 1)
                    {
                        equal = ours == expected;
                        ours = equal ? writer.previous[ours] : ours;
                        expected = equal ? check_test_program_inside_child(unit_last, unit_previous, token, expected, opening, closing) : expected;
                    }
                    const bool labeled = token != opening && token != closing && (roles[token] == PARSE_ROLE_LABEL || roles[token] == PARSE_ROLE_CLOSE_CASE);
                    const uint32_t expected_label = labeled ? label : BUSTER_CHECK_NONE;
                    equal = equal && (labeled ? writer.source_labels[token] == expected_label : writer.source_labels[token] == UINT32_C(0xa5a5a5a5)) &&
                            check_control_named_target(&writer, token) == (roles[token] == PARSE_ROLE_LABEL ? expected_label : BUSTER_CHECK_NONE) &&
                            writer.stops[token] == BUSTER_CHECK_NONE &&
                            writer.repeats[token] == BUSTER_CHECK_NONE;
                    if (labeled && roles[token] == PARSE_ROLE_CLOSE_CASE)
                    {
                        equal = equal && cases[case_map[token]].target == expected_label;
                        case_start = case_start == BUSTER_CHECK_NONE ? case_map[token] : case_start;
                        case_end = case_map[token] + 1;
                        cases_seen += 1;
                    }
                    label += labeled;
                }
                equal = equal && writer.label_count == label && writer.case_start == case_start && writer.case_end == case_end &&
                        program.status == CHECK_PROGRAM_OK;
                // The same body again, without loop targets: no consumer requested them, so their poisoned entries stay.
                // The relative numbers of every source label and case stay the same.
                writer.all_loop_targets = false;
                writer.stops[opening] = UINT32_C(0xa5a5a5a5);
                writer.repeats[opening] = UINT32_C(0x5a5a5a5a);
                writer.label_count = 0;
                check_control_children(&writer, opening, closing);
                uint32_t again = 0;
                for (uint32_t token = opening + 1; token < closing; token += 1)
                {
                    if (roles[token] == PARSE_ROLE_LABEL || roles[token] == PARSE_ROLE_CLOSE_CASE)
                    {
                        equal = equal && writer.source_labels[token] == again;
                        again += 1;
                    }
                }
                equal = equal && again == label && writer.label_count == label && writer.case_start == case_start && writer.case_end == case_end &&
                        writer.stops[opening] == UINT32_C(0xa5a5a5a5) && writer.repeats[opening] == UINT32_C(0x5a5a5a5a);
                writer.all_loop_targets = true;
                labels_seen += label;
            }
        }
        free(cases);
    }
    printf("  %s: %u bodies' own columns against the unit-wide lists, with %u labels and %u cases\n", name, bodies, labels_seen, cases_seen);
    char description[256];
    snprintf(description, sizeof(description), "%s: each of %u bodies' own columns are the unit-wide lists' within it, with its %u labels and %u cases",
             name, bodies, labels_seen, cases_seen);
    check_test_expect(test, equal && bodies != 0, description);
    free(columns);
    parse_release(&parser);
    pp_release(&pp);
    intern_release(&identifiers);
}

static void check_test_program_source(CheckTest* test, const char* source, const char* name, const char* clang);
static bool check_test_program_function(const Tier0Function* a, const Tier0Function* b);
// The program fixtures whose lowering the source rows' lowering compared.
static uint32_t check_test_program_expansions;
static void check_test_constant_conversion_lowered(CheckTest* test, const char* name, const CheckProgramLowered lowered[2], const CheckProgram* program, Arena* output,
                                                   Arena* scratch);
static void check_test_branch_condition_lowered(CheckTest* test, const char* name, const CheckProgramLowered lowered[2], const CheckProgram* program, Arena* output,
                                                Arena* scratch);

// The bodies fixture's columns, and those of a unit of many small bodies, each with a switch, its own labels of one name,
// a goto back, and an empty body and a label alone among them; then the bodies fixture compiled and run against clang.
static void check_test_program_bodies(CheckTest* test, const char* clang)
{
    check_test_program_body_columns(test, check_test_program_bodies_source, "bodies-columns");
    char many[16384];
    int length = snprintf(many, sizeof(many), "void empty(void){}void lone(void){only:;}");
    for (uint32_t i = 0; length > 0 && (size_t)length < sizeof(many) && i < 24; i += 1)
    {
        length += snprintf(many + length, sizeof(many) - (size_t)length,
                           "int f%u(int x){int r=0;again:switch(x%%%u){case 0:r+=%u;break;case 1:{case 2:r+=1;}if(r<%u){x++;goto again;}break;default:r--;}"
                           "for(int i=0;i<3;i++){if(i==1)continue;r+=i;}done:return r;}",
                           i, i + 3, i, i + 2);
    }
    check_test_expect(test, length > 0 && (size_t)length < sizeof(many), "the many bodies' source fits");
    check_test_program_body_columns(test, many, "many-bodies-columns");
    check_test_program_source(test, check_test_program_bodies_source, "bodies-controls", clang);
}

// `bytes` at the end of a mapping whose next page is PROT_NONE, so that an access past them faults: the start, or 0.
static uint8_t* check_test_program_guarded(uint8_t** mapping, uint64_t* mapped, uint64_t bytes)
{
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    const uint64_t pages = (bytes + page - 1) / page * page;
    *mapped = pages + page;
    uint8_t* base = mmap(0, *mapped, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const bool guarded = base != MAP_FAILED && mprotect(base + pages, page, PROT_NONE) == 0;
    *mapping = base != MAP_FAILED ? base : 0;
    return guarded ? base + pages - bytes : 0;
}

#include "c_check_runtime_scan_test.c"

// #442's kernels, each path against the test's own reading of its contract, every column (and output) ending at a
// PROT_NONE page: the declaring tokens, without and with the initializers' nodes, for every count to 200 and larger
// ones, the declarations with initializers (#592's check_init_declarations), and the needed objects' count and
// numbering (object 0 never a symbol), at every density.
static void check_test_program_declaring(CheckTest* test)
{
    check_test_runtime_scan(test);
    // The sparse columns (c_check_sparse.c) the guarded columns' values make, for each case.
    Arena sparse = arena_reserve(UINT64_C(1) << 22);
    static const uint32_t larger[] = { 255, 256, 257, 1023, 1024, 1025, 4095, 4096, 4097, 70001 };
    uint64_t checked = 0;
    bool all = true;
    uint64_t state = 0x442;
    for (uint32_t c = 0; c <= 200 + sizeof(larger) / sizeof(larger[0]); c += 1)
    {
        const uint32_t count = c <= 200 ? c : larger[c - 201];
        for (uint32_t density = 0; density < 4; density += 1)
        {
            uint8_t* maps[9] = { 0 };
            uint64_t sizes[9] = { 0 };
            uint32_t* declarations = (uint32_t*)check_test_program_guarded(&maps[0], &sizes[0], (uint64_t)count * sizeof(uint32_t));
            uint32_t* nodes = (uint32_t*)check_test_program_guarded(&maps[1], &sizes[1], (uint64_t)count * sizeof(uint32_t));
            // The tokens' room: count + 16, as the kernel's contract asks, and not one more.
            uint32_t* tokens = (uint32_t*)check_test_program_guarded(&maps[2], &sizes[2], ((uint64_t)count + 16) * sizeof(uint32_t));
            uint8_t* needed = check_test_program_guarded(&maps[3], &sizes[3], (uint64_t)count + 1);
            SymbolId* object_symbols = (SymbolId*)check_test_program_guarded(&maps[4], &sizes[4], ((uint64_t)count + 1) * sizeof(SymbolId));
            uint32_t* expected = (uint32_t*)check_test_program_guarded(&maps[5], &sizes[5], ((uint64_t)count + 1) * sizeof(uint32_t));
            bool ready = declarations != 0 && nodes != 0 && tokens != 0 && needed != 0 && object_symbols != 0 && expected != 0;
            const uint32_t limit = count / 2 + 1;
            for (uint32_t t = 0; ready && t < count; t += 1)
            {
                state = state * 6364136223846793005u + 1442695040888963407u;
                const uint32_t random = (uint32_t)(state >> 33);
                // Density 0 declares nothing, 3 everything; between, about a quarter or a half of the tokens.
                const bool declares = density == 3 || (density != 0 && random % (density == 1 ? 4 : 2) == 0);
                declarations[t] = declares ? t : (random & 1) != 0 ? t + 1 : (random & 2) != 0 ? BUSTER_CHECK_NONE : t - 1;
                nodes[t] = (random >> 8) % 3 == 0 ? (random >> 12) % limit : limit + (random >> 12) % 3;
                needed[t + 1] = density == 3 || (density != 0 && (random >> 4) % (density == 1 ? 8 : 2) == 0);
            }
            if (ready)
            {
                needed[0] = 1;
            }
            for (uint32_t with_nodes = 0; ready && with_nodes < 2; with_nodes += 1)
            {
                uint32_t want = 0;
                for (uint32_t t = 0; t < count; t += 1)
                {
                    if (declarations[t] == t || (with_nodes != 0 && nodes[t] < limit))
                    {
                        expected[want++] = t;
                    }
                }
                const uint64_t mark = sparse.length;
                const CheckSparseColumn sparse_nodes = check_sparse_from_values(&sparse, nodes, count);
                all &= check_sparse_present(sparse_nodes);
                const uint32_t got = check_program_declaring(declarations, with_nodes != 0 ? &sparse_nodes : 0, limit, count, tokens);
                const bool same = got == want && memcmp(tokens, expected, (uint64_t)want * sizeof(uint32_t)) == 0;
                all &= same;
                checked += 1;
                if (!same)
                {
                    printf("  declaring tokens: count %u density %u nodes %u: %u tokens, expected %u\n", count, density, with_nodes, got, want);
                }
                arena_pop_to(&sparse, mark);
            }
            // The declarations with initializers (check_init_declarations, #592): room for exactly `count`.
            uint32_t* begins = (uint32_t*)check_test_program_guarded(&maps[7], &sizes[7], (uint64_t)count * sizeof(uint32_t));
            uint32_t* initialized = (uint32_t*)check_test_program_guarded(&maps[8], &sizes[8], (uint64_t)count * sizeof(uint32_t));
            ready &= begins != 0 && initialized != 0;
            uint32_t begun = 0;
            for (uint32_t t = 0; ready && t < count; t += 1)
            {
                state = state * 6364136223846793005u + 1442695040888963407u;
                const uint32_t random = (uint32_t)(state >> 33);
                const bool has = density == 3 || (density != 0 && random % (density == 1 ? 16 : 2) == 0);
                begins[t] = has ? (random >> 4) % (count + 2) : BUSTER_CHECK_NONE;
                expected[begun] = t;
                begun += has;
            }
            const uint64_t begins_mark = sparse.length;
            const CheckSparseColumn sparse_begins = ready ? check_sparse_from_values(&sparse, begins, count) : (CheckSparseColumn){ 0 };
            ready &= check_sparse_present(sparse_begins);
            for (uint32_t path = 0; ready && path < 2; path += 1)
            {
                const CheckInput input = { .initializer_begins = sparse_begins, .token_count = count };
                const uint32_t got = check_init_declarations(&input, count, initialized, path == 0);
                const bool same = got == begun && memcmp(initialized, expected, (uint64_t)begun * sizeof(uint32_t)) == 0;
                all &= same;
                checked += 1;
                if (!same)
                {
                    printf("  initialized declarations: count %u density %u %s path: %u tokens, expected %u\n", count, density, path == 0 ? "scalar" : "vector", got, begun);
                }
            }
            arena_pop_to(&sparse, begins_mark);
            const uint32_t objects = count + 1;
            uint32_t want = 0;
            for (uint32_t object = 1; ready && object < objects; object += 1)
            {
                want += needed[object] != 0;
            }
            if (ready)
            {
                const uint32_t got = check_program_needed_count(needed, objects);
                // The symbols' objects exactly as many as the needed objects, ending at the protected page.
                uint32_t* symbol_objects = got == want ? (uint32_t*)check_test_program_guarded(&maps[6], &sizes[6], (uint64_t)want * sizeof(uint32_t)) : 0;
                memset(object_symbols, 0, (uint64_t)objects * sizeof(SymbolId));
                if (symbol_objects != 0)
                {
                    check_program_needed_number(needed, objects, object_symbols, symbol_objects);
                }
                bool same = symbol_objects != 0 && object_symbols[0] == 0;
                SymbolId symbol = 0;
                for (uint32_t object = 1; same && object < objects; object += 1)
                {
                    same = needed[object] != 0 ? object_symbols[object] == symbol + 1 && symbol_objects[symbol] == object : object_symbols[object] == 0;
                    symbol += needed[object] != 0;
                }
                all &= same;
                checked += 1;
                if (!same)
                {
                    printf("  needed objects: count %u density %u: %u symbols, expected %u\n", count, density, got, want);
                }
                if (maps[6] != 0)
                {
                    munmap(maps[6], sizes[6]);
                    maps[6] = 0;
                }
            }
            all &= ready;
            for (uint32_t m = 0; m < 9; m += 1)
            {
                if (maps[m] != 0)
                {
                    munmap(maps[m], sizes[m]);
                }
            }
        }
    }
    arena_release(&sparse);
    printf("  declaring tokens, initialized declarations (by both paths) and needed objects: %llu cases at PROT_NONE pages\n", (unsigned long long)checked);
    check_test_expect(test, all, "declaring tokens, initialized declarations (by both paths) and needed objects agree with the contract at every count and density");
}

static bool check_test_program_column(const void* a, const void* b, uint64_t size)
{
    return size == 0 || (a != 0 && b != 0 && memcmp(a, b, size) == 0);
}

static bool check_test_program_function(const Tier0Function* a, const Tier0Function* b)
{
    const bool equal = a->count == b->count && a->local_count == b->local_count && a->parameter_count == b->parameter_count &&
                       a->call_count == b->call_count && a->argument_count == b->argument_count &&
                       check_test_program_column(a->kinds, b->kinds, a->count) &&
                       check_test_program_column(a->classes, b->classes, a->count) &&
                       check_test_program_column(a->values, b->values, (uint64_t)a->count * sizeof(uint64_t)) &&
                       check_test_program_column(a->local_classes, b->local_classes, a->local_count) &&
                       check_test_program_column(a->local_flags, b->local_flags, a->local_count) &&
                       check_test_program_column(a->parameters, b->parameters, (uint64_t)a->parameter_count * sizeof(AbiLocation)) &&
                       memcmp(&a->result, &b->result, sizeof(AbiReturn)) == 0 &&
                       check_test_program_column(a->call_targets, b->call_targets, (uint64_t)a->call_count * sizeof(SymbolId)) &&
                       check_test_program_column(a->call_first, b->call_first, (uint64_t)a->call_count * sizeof(uint32_t)) &&
                       check_test_program_column(a->call_argument_counts, b->call_argument_counts, (uint64_t)a->call_count * sizeof(uint32_t)) &&
                       check_test_program_column(a->call_variadic, b->call_variadic, a->call_count) &&
                       check_test_program_column(a->calls, b->calls, (uint64_t)a->call_count * sizeof(AbiCall)) &&
                       check_test_program_column(a->argument_locations, b->argument_locations, (uint64_t)a->argument_count * sizeof(AbiLocation)) &&
                       check_test_program_column(a->argument_classes, b->argument_classes, a->argument_count) &&
                       a->x87_constant_count == b->x87_constant_count && a->x87 == b->x87 &&
                       check_test_program_column(a->x87_constants, b->x87_constants, (uint64_t)a->x87_constant_count * 2 * sizeof(uint64_t));
    return equal;
}

// Writes the link's object to `path`, and a copy of its bytes to `bytes` and `size` (the caller frees them).
static bool check_test_program_object(Link* link, const char* path, Arena* scratch, uint8_t** bytes, uint64_t* size)
{
    *bytes = 0;
    *size = 0;
    const uint64_t mark = scratch->length;
    ObjectUnits units = { 0 };
    ObjectSymbols symbols = { 0 };
    bool valid = link_check(link) == LINK_ERROR_NONE && object_prepare(&units, link, 1, scratch) && object_map_symbols(&symbols, link, scratch);
    if (valid)
    {
        const Object object = object_build(link, &units, &symbols, 0, (ObjectOptions){ 0 }, BUSTER_S8("checked-source.c"), scratch);
        valid = object.error == OBJECT_ERROR_NONE && object_write_file(path, object.bytes, object.size) == OBJECT_ERROR_NONE;
        *bytes = valid ? malloc(object.size + 1) : 0;
        valid = valid && *bytes != 0;
        if (valid)
        {
            memcpy(*bytes, object.bytes, object.size);
            *size = object.size;
        }
    }
    arena_pop_to(scratch, mark);
    return valid;
}

// The oracle's source: each global the checked object defines (strong or weak) renamed oracle_NAME, then the program, in
// `text`. A name
// that is no identifier (a cpu_specific version's f.M, #1447) is no macro's: the source's own name renames it, as clang
// mangles the renamed function's versions after it.
static bool check_test_program_oracle(const Link* link, const char* source, char** text, size_t* length)
{
    *text = 0;
    *length = 0;
    FILE* file = open_memstream(text, length);
    bool valid = file != 0;
    for (SymbolId symbol = 1; valid && symbol < link->symbols.count; symbol += 1)
    {
        const uint8_t flags = link->symbols.flags[symbol];
        const NameId spelled = link->symbols.name[symbol];
        bool identifier = spelled != 0;
        for (uint32_t i = 0; identifier && i < link->names.length[spelled]; i += 1)
        {
            const uint8_t c = link->names.bytes[spelled][i];
            identifier = c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        }
        const uint8_t binding = flags & BUSTER_LINK_SYMBOL_BINDING;
        if ((binding == BUSTER_LINK_SYMBOL_GLOBAL || binding == BUSTER_LINK_SYMBOL_WEAK) && link->symbols.atom[symbol] != 0 && identifier)
        {
            const NameId name = link->symbols.name[symbol];
            valid = fprintf(file, "#define %.*s oracle_%.*s\n", (int)link->names.length[name], link->names.bytes[name],
                            (int)link->names.length[name], link->names.bytes[name]) > 0;
        }
    }
    if (valid)
    {
        valid = fwrite(source, 1, strlen(source), file) == strlen(source);
    }
    if (file != 0)
    {
        valid = fclose(file) == 0 && valid;
    }
    return valid;
}

// DAZ is optional even with SSE2; FXSAVE's mask says whether MXCSR bit 6 may be written.
static bool check_test_program_daz(void)
{
    uint8_t state[512] __attribute__((aligned(16)));
    uint32_t mask;
    __asm__ volatile("fxsave %0" : "=m"(state));
    memcpy(&mask, state + 28, sizeof(mask));
    return (mask & 64u) != 0;
}

// The transparent unions (#147) that the harness's functions take and c_check_program_transparent_test.c's programs
// declare too, each passed as its first member: a struct of two floats (in xmm0, where the union's own classes take a
// general register), a short and an unsigned short (extended as they are), _Bool, a pointer beside a long and beside a
// double, a struct of a double (xmm0, beside a long), a 128-bit integer (beside a struct and a long double), an array and
// a struct of 24 bytes (on the stack).
#define BUSTER_CHECK_TEST_TRANSPARENT_UNIONS                                                                                                                          \
    "struct external_fp{float x,y;};struct external_ip{int a,b;};struct external_dv{double v;};struct external_w{_Alignas(16) long a;long b;};"                    \
    "struct external_big{long a,b,c;};struct external_big_bytes{long c[3];};"                                                                                       \
    "typedef union __attribute__((transparent_union)){struct external_fp p;struct external_ip q;}ExternalTF;"                                                       \
    "typedef union __attribute__((transparent_union)){short s;unsigned short u;}ExternalTS;"                                                                        \
    "typedef union __attribute__((transparent_union)){unsigned short u;short s;}ExternalTZ;"                                                                        \
    "typedef union __attribute__((transparent_union)){_Bool b;char c;}ExternalTO;"                                                                                  \
    "typedef union __attribute__((transparent_union)){long*p;long l;}ExternalTP;"                                                                                   \
    "typedef union __attribute__((transparent_union)){long*p;double d;}ExternalTQ;"                                                                                 \
    "typedef union __attribute__((transparent_union)){struct external_dv s;long l;}ExternalTD;"                                                                    \
    "typedef union __attribute__((transparent_union)){__int128 w;struct external_w s;long double x;}ExternalTW;"                                                   \
    "typedef union __attribute__((transparent_union)){int a[2];struct external_ip q;}ExternalTA;"                                                                  \
    "typedef union __attribute__((transparent_union)){struct external_big s;struct external_big_bytes t;}ExternalTB;"

// Transparent unions of a stack slot of their own (#1844), aligned otherwise than their first members, which clang's code
// passes in the member's registers or, where it has none for them, as the union's bytes in a byval slot of the union's
// alignment, or the member's where the member is in memory: an aggregate of 8 bytes in a union of 32, one of two floats
// and two ints in a union of 32, 24 bytes in memory in a union of 32, a pointer beside a long and a struct of a double in
// unions of 32, and a packed struct of 5 bytes, in memory, in unions of 16 and 32, which clang passes as an integer
// where it has no general register left; and members of a pair's or a vector's classes: a 128-bit integer in a union of
// 32 and in a packed one, a struct of a 16-byte vector and a complex long, in unions of 32. After BUSTER_CHECK_TEST_TRANSPARENT_UNIONS, whose structs it takes.
#define BUSTER_CHECK_TEST_TRANSPARENT_OWN_UNIONS                                                                                                                      \
    "struct external_l{long a;};struct external_mix{int a,b;float c,d;};struct __attribute__((packed)) external_pk{char c;int i;};"                                \
    "typedef union __attribute__((transparent_union,aligned(32))){struct external_l s;long l;}ExternalOL;"                                                          \
    "typedef union __attribute__((transparent_union,aligned(32))){struct external_mix m;struct external_mix n;}ExternalOM;"                                        \
    "typedef union __attribute__((transparent_union,aligned(32))){struct external_big s;struct external_big_bytes t;}ExternalOB;"                                  \
    "typedef union __attribute__((transparent_union,aligned(32))){long*p;long l;}ExternalOP;"                                                                       \
    "typedef union __attribute__((transparent_union,aligned(32))){struct external_dv s;long l;}ExternalOD;"                                                        \
    "typedef union __attribute__((transparent_union,aligned(16))){struct external_pk p;struct external_pk q;}ExternalOK;"                                          \
    "typedef union __attribute__((transparent_union,aligned(32))){struct external_pk p;struct external_pk q;}ExternalOQ;"                                          \
    "typedef int external_v4 __attribute__((vector_size(16)));struct external_v{external_v4 x;};"                                                                    \
    "typedef union __attribute__((transparent_union,aligned(32))){__int128 w;__int128 v;}ExternalOW;"                                                              \
    "typedef union __attribute__((transparent_union,aligned(32))){struct external_v v;struct external_v u;}ExternalOV;"                                            \
    "typedef union __attribute__((transparent_union,aligned(32))){_Complex long c;_Complex long d;}ExternalOC;"                                                    \
    "typedef union __attribute__((transparent_union,packed)){__int128 w;__int128 v;}ExternalOX;"

// Transparent unions of a LARGER first member in memory (#3400), which clang passes as the member: a packed struct ending
// in an _Atomic of a struct of no size, GROWN (7 bytes in C, its copy 14 to LLVM), in unions aligned to 16 and 32, of no
// attribute and packed; and a 9-byte one, LARGER but not GROWN, in a union aligned to 16.
#define BUSTER_CHECK_TEST_TRANSPARENT_GROWN_UNIONS                                                                                                                    \
    "struct external_z{long x[0];};struct __attribute__((packed)) external_gr{int i;short s;_Atomic(struct external_z) z;};"                                       \
    "struct __attribute__((packed)) external_ge{long l;_Atomic(struct external_z) z;};"                                                                              \
    "typedef union __attribute__((transparent_union,aligned(16))){struct external_gr g;}ExternalGA;"                                                                \
    "typedef union __attribute__((transparent_union,aligned(32))){struct external_gr g;}ExternalGB;"                                                                \
    "typedef union __attribute__((transparent_union)){struct external_gr g;}ExternalGP;"                                                                            \
    "typedef union __attribute__((transparent_union,packed)){struct external_gr g;}ExternalGK;"                                                                     \
    "typedef union __attribute__((transparent_union,aligned(16))){struct external_ge e;}ExternalGE;"

// The program every checked object and its oracle are linked with: it runs both on the same inputs.
static const char check_test_program_harness[] =
    "#include <stdio.h>\n"
    "#include <fenv.h>\n"
    "#include <errno.h>\n"
    "#include <xmmintrin.h>\n"
    "#include <signal.h>\n#include <sys/resource.h>\n#include <sys/wait.h>\n#include <unistd.h>\n#include <stdarg.h>\n"
    "int tested(int); int oracle_tested(int);\n"
    "void terminal_probe(void) __attribute__((weak));\n"
    "int external_data=17; int external_helper(int n){return n*3;}\n"
    "int external_apply(int(*function)(int),int n){return function(n);}\n"
    "int external_unnamed_apply(long(*function)(int,double,int,double,long),int n){return (int)function(17,1.25,n,7.5,29);}\n"
    "int change_rounding(int n){const int modes[]={FE_TONEAREST,FE_TOWARDZERO,FE_UPWARD,FE_DOWNWARD};return fesetround(modes[(unsigned)n&3]);}\n"
    "int clear_fp(void){return feclearexcept(FE_ALL_EXCEPT);}\n"
    "int fp_state(void){int f=fetestexcept(FE_ALL_EXCEPT);return !!(f&FE_INVALID)+2*!!(f&FE_DIVBYZERO)+4*!!(f&FE_OVERFLOW)+8*!!(f&FE_UNDERFLOW)+16*!!(f&FE_INEXACT);}\n"
    "int change_daz(int n){struct {unsigned char b[512];} __attribute__((aligned(16))) s;"
    "__asm__ volatile(\"fxsave %0\":\"=m\"(s));unsigned mask;__builtin_memcpy(&mask,s.b+28,4);"
    "unsigned old=_mm_getcsr();if(mask&64u)_mm_setcsr((old&~64u)|((unsigned)n&1u)*64u);return (old>>6)&1;}\n"
    "double fp_special(int n){const unsigned long b[]={0,0x8000000000000000UL,0x7ff0000000000000UL,0xfff0000000000000UL,"
    "0x7ff8000000000001UL,0x7ff0000000000001UL,1,0x8000000000000001UL,0x3ff8000000000000UL};"
    "union{unsigned long bits;double value;}x={b[(unsigned)n%9]};return x.value;}\n"
    "float fp_specialf(int n){const unsigned b[]={0,0x80000000u,0x7f800000u,0xff800000u,0x7fc00001u,0x7f800001u,1,0x80000001u,0x3fc00000u};"
    "union{unsigned bits;float value;}x={b[(unsigned)n%9]};return x.value;}\n"
    "unsigned long fp_bits(double d){union{double d;unsigned long u;}x={d};return x.u;}\n"
    "unsigned fp_bitsf(float f){union{float f;unsigned u;}x={f};return x.u;}\n"
    "int set_math_errno(int n){errno=n;return 0;}int get_math_errno(void){return errno;}\n"
    // _BitInt values (#148) taken and returned in their classes' registers, as clang extends them, one stored through a
    // pointer, and a function of the program's called with them.
    "unsigned _BitInt(5) external_bit_int(_BitInt(5) a,unsigned _BitInt(13) b,_BitInt(40) c,unsigned _BitInt(5)*p){*p=(unsigned _BitInt(5))b;"
    "return (unsigned _BitInt(5))a+(unsigned _BitInt(5))(c>>35);}\n"
    "int external_bit_apply(unsigned _BitInt(13)(*f)(_BitInt(5),unsigned _BitInt(27)),int n){return (int)f((_BitInt(5))(n%16),(unsigned _BitInt(27))(n*1000003));}\n"
    // A result through a hidden pointer, which clang takes as aligned to the union's 16: w is the pointer's bytes
    // (clang -O0 returns in place), and a misaligned one shows in the first byte.
    "union external_wide{long double x;char c[40];};union external_wide external_wide(int n){union external_wide w;"
    "for(int i=0;i<40;i++)w.c[i]=(char)(n+i);if((unsigned long)&w%16)w.c[0]=99;return w;}\n"
    // Structs aligned to 32 by value (MEMORY, on a stack aligned to 32 at the call), which clang's callee takes in place:
    // an outgoing area off its alignment shows in their addresses.
    "struct external_aligned{_Alignas(32) long a;int b;};int external_aligned_by_value(int k,struct external_aligned x,struct external_aligned y){"
    "return (int)(x.a+x.b+y.a*3+y.b)+k+(int)((((unsigned long)&x)|((unsigned long)&y))&31)*1000;}\n"
    // Structs of a complex long double, which return in st0 and st1: one returned to the program, and the program's
    // own called through a pointer.
    "struct external_cx{_Complex long double z;};struct external_cx external_complex(long double a,long double b){"
    "struct external_cx c;__real__ c.z=a;__imag__ c.z=b;return c;}long double external_complex_apply("
    "struct external_cx(*f)(long double,long double),long double a){struct external_cx c=f(a,-a);return __real__ c.z*4+__imag__ c.z;}\n"
    // Complex binary16's two parts share one SSE eightbyte. Transport their bits in both directions, including the
    // ninth and tenth arguments on the stack and va_arg's, without an arithmetic conversion quieting a NaN.
    "typedef _Float16 _Complex ExternalHC;"
    "static unsigned external_hc_bits(ExternalHC z){unsigned b;__builtin_memcpy(&b,&z,4);return b;}"
    "static ExternalHC external_hc_value(unsigned b){ExternalHC z;__builtin_memcpy(&z,&b,4);return z;}"
    "ExternalHC external_hc(ExternalHC z,unsigned mask){return external_hc_value(external_hc_bits(z)^mask);}"
    "unsigned external_hc_apply(ExternalHC(*f)(ExternalHC,unsigned),unsigned bits){return external_hc_bits(f(external_hc_value(bits),0x80000001u));}"
    "unsigned external_hc_many(ExternalHC a,ExternalHC b,ExternalHC c,ExternalHC d,ExternalHC e,ExternalHC f,ExternalHC g,ExternalHC h,ExternalHC i,ExternalHC j){"
    "ExternalHC z[]={a,b,c,d,e,f,g,h,i,j};unsigned r=2166136261u;for(int k=0;k<10;k++)r=(r^external_hc_bits(z[k]))*16777619u;return r;}"
    "unsigned external_hc_many_apply(unsigned(*f)(ExternalHC,ExternalHC,ExternalHC,ExternalHC,ExternalHC,ExternalHC,ExternalHC,ExternalHC,ExternalHC,ExternalHC),"
    "unsigned bits){ExternalHC a=external_hc_value(bits),b=external_hc_value(bits^0x80008000u);return f(a,b,a,b,a,b,a,b,a,b);}"
    "unsigned external_hc_variadic(int count,...){va_list ap;va_start(ap,count);unsigned h=2166136261u;"
    "for(int i=0;i<count;i++){ExternalHC z=va_arg(ap,ExternalHC);h=(h^external_hc_bits(z))*16777619u;}va_end(ap);return h;}"
    "unsigned external_hc_variadic_apply(unsigned(*f)(int,...),unsigned bits){ExternalHC a=external_hc_value(bits),b=external_hc_value(bits^0x80008000u);"
    "return f(10,a,b,a,b,a,b,a,b,a,b);}\n"
    // The _Complex __bf16 that the usual arithmetic conversions make (C can't spell it) passes as a <2 x bfloat>, its two
    // parts in one SSE eightbyte as complex binary16's do (#137): the same transport of their bits, both ways, but its
    // masks flip signs alone, which make no signaling NaN of an infinity: clang 23's code of a <2 x bfloat> argument or
    // result quiets one, as it does a __bf16 parameter's.
    "typedef __typeof__((_Complex int)0+(__bf16)0) ExternalHB;"
    "static unsigned external_hb_bits(ExternalHB z){unsigned b;__builtin_memcpy(&b,&z,4);return b;}"
    "static ExternalHB external_hb_value(unsigned b){ExternalHB z;__builtin_memcpy(&z,&b,4);return z;}"
    "ExternalHB external_hb(ExternalHB z,unsigned mask){return external_hb_value(external_hb_bits(z)^mask);}"
    "unsigned external_hb_apply(ExternalHB(*f)(ExternalHB,unsigned),unsigned bits){return external_hb_bits(f(external_hb_value(bits),0x80008000u));}"
    "unsigned external_hb_many(ExternalHB a,ExternalHB b,ExternalHB c,ExternalHB d,ExternalHB e,ExternalHB f,ExternalHB g,ExternalHB h,ExternalHB i,ExternalHB j){"
    "ExternalHB z[]={a,b,c,d,e,f,g,h,i,j};unsigned r=2166136261u;for(int k=0;k<10;k++)r=(r^external_hb_bits(z[k]))*16777619u;return r;}"
    "unsigned external_hb_many_apply(unsigned(*f)(ExternalHB,ExternalHB,ExternalHB,ExternalHB,ExternalHB,ExternalHB,ExternalHB,ExternalHB,ExternalHB,ExternalHB),"
    "unsigned bits){ExternalHB a=external_hb_value(bits),b=external_hb_value(bits^0x80008000u);return f(a,b,a,b,a,b,a,b,a,b);}"
    "unsigned external_hb_variadic(int count,...){va_list ap;va_start(ap,count);unsigned h=2166136261u;"
    "for(int i=0;i<count;i++){ExternalHB z=va_arg(ap,ExternalHB);h=(h^external_hb_bits(z))*16777619u;}va_end(ap);return h;}"
    "unsigned external_hb_variadic_apply(unsigned(*f)(int,...),unsigned bits){ExternalHB a=external_hb_value(bits),b=external_hb_value(bits^0x80008000u);"
    "return f(10,a,b,a,b,a,b,a,b,a,b);}\n"
    // _Atomic _Float16 and _Atomic __bf16 pass as their values in an XMM register's low 16 bits, which clang's count of
    // registers leaves out (an _Atomic is MEMORY to its classification, #137): their bits both ways, each loaded and
    // stored atomically, past eight XMM registers onto the stack, with a double and a float among them. __bf16's mask
    // flips its sign alone, as _Complex __bf16's do.
    "typedef _Atomic(_Float16) ExternalAH;typedef _Atomic(__bf16) ExternalAB;"
    "static unsigned short external_ah_bits(ExternalAH*p){_Float16 v=*p;unsigned short b;__builtin_memcpy(&b,&v,2);return b;}"
    "static unsigned short external_ab_bits(ExternalAB*p){__bf16 v=*p;unsigned short b;__builtin_memcpy(&b,&v,2);return b;}"
    "static _Float16 external_ah_value(unsigned b){unsigned short s=(unsigned short)b;_Float16 v;__builtin_memcpy(&v,&s,2);return v;}"
    "static __bf16 external_ab_value(unsigned b){unsigned short s=(unsigned short)b;__bf16 v;__builtin_memcpy(&v,&s,2);return v;}"
    "ExternalAH external_ah(ExternalAH x,unsigned mask){return external_ah_value(external_ah_bits(&x)^mask);}"
    "ExternalAB external_ab(ExternalAB x,unsigned mask){return external_ab_value(external_ab_bits(&x)^mask);}"
    "unsigned external_ah_apply(ExternalAH(*f)(ExternalAH,unsigned),unsigned bits){ExternalAH r=f(external_ah_value(bits),0x8001u);return external_ah_bits(&r);}"
    "unsigned external_ab_apply(ExternalAB(*f)(ExternalAB,unsigned),unsigned bits){ExternalAB r=f(external_ab_value(bits),0x8000u);return external_ab_bits(&r);}"
    "unsigned external_ahb_many(ExternalAH a,ExternalAB b,double c,ExternalAH d,ExternalAB e,float f,ExternalAH g,ExternalAB h,ExternalAH i,ExternalAB j,"
    "ExternalAH k,ExternalAB l){unsigned long cb;unsigned fb;__builtin_memcpy(&cb,&c,8);__builtin_memcpy(&fb,&f,4);"
    "unsigned r=((unsigned)cb^(unsigned)(cb>>32))*16777619u^fb;ExternalAH*hs[]={&a,&d,&g,&i,&k};ExternalAB*bs[]={&b,&e,&h,&j,&l};"
    "for(int m=0;m<5;m++)r=(r^external_ah_bits(hs[m])^((unsigned)external_ab_bits(bs[m])<<16))*16777619u;return r;}"
    "unsigned external_ahb_many_apply(unsigned(*f)(ExternalAH,ExternalAB,double,ExternalAH,ExternalAB,float,ExternalAH,ExternalAB,ExternalAH,ExternalAB,ExternalAH,"
    "ExternalAB),unsigned bits){_Float16 a=external_ah_value(bits);__bf16 b=external_ab_value(bits>>16);"
    "return f(a,b,1.25,a,b,-2.5f,a,b,a,b,a,b);}\n"
    // Mixed Clang/Buster atomic-vector calls use the declared atomic ABI, with underlying vector value bytes.
    "#if defined(__AVX512F__) && defined(__AVX512VL__)\n"
    "typedef int ExternalAtomic4 __attribute__((vector_size(16)));"
    "_Atomic(ExternalAtomic4) external_atomic4(_Atomic(ExternalAtomic4) x){ExternalAtomic4 v=x;return v+3;}"
    "int external_apply_atomic4(_Atomic(ExternalAtomic4)(*f)(_Atomic(ExternalAtomic4)),int n){ExternalAtomic4 v={0};"
    "for(int i=0;i<4;i++)v[i]=n+i;_Atomic(ExternalAtomic4) out=f(v);ExternalAtomic4 r=out;int h=0;for(int i=0;i<4;i++)h+=r[i]*(i+1);return h;}\n"
    "typedef int ExternalAtomic8 __attribute__((vector_size(32)));"
    "_Atomic(ExternalAtomic8) external_atomic8(_Atomic(ExternalAtomic8) x){ExternalAtomic8 v=x;return v+3;}"
    "int external_apply_atomic8(_Atomic(ExternalAtomic8)(*f)(_Atomic(ExternalAtomic8)),int n){ExternalAtomic8 v={0};"
    "for(int i=0;i<8;i++)v[i]=n+i;_Atomic(ExternalAtomic8) out=f(v);ExternalAtomic8 r=out;int h=0;for(int i=0;i<8;i++)h+=r[i]*(i+1);return h;}\n"
    "typedef int ExternalAtomic16 __attribute__((vector_size(64)));"
    "_Atomic(ExternalAtomic16) external_atomic16(_Atomic(ExternalAtomic16) x){ExternalAtomic16 v=x;return v+3;}"
    "int external_apply_atomic16(_Atomic(ExternalAtomic16)(*f)(_Atomic(ExternalAtomic16)),int n){ExternalAtomic16 v={0};"
    "for(int i=0;i<16;i++)v[i]=n+i;_Atomic(ExternalAtomic16) out=f(v);ExternalAtomic16 r=out;int h=0;for(int i=0;i<16;i++)h+=r[i]*(i+1);return h;}\n"
    // Atomic vectors of 8 and 4 bytes (#137), which LLVM passes and returns as their values in XMM registers, as it does
    // a <2 x i16>, which an integer of 4 bytes passes otherwise when it isn't _Atomic.
    "typedef signed char ExternalAtomicQ8 __attribute__((vector_size(8)));"
    "_Atomic(ExternalAtomicQ8) external_atomic_q8(_Atomic(ExternalAtomicQ8) x,int k){ExternalAtomicQ8 v=x;return v+(signed char)k;}"
    "int external_apply_atomic_q8(_Atomic(ExternalAtomicQ8)(*f)(_Atomic(ExternalAtomicQ8),int),int n){ExternalAtomicQ8 v={0};"
    "for(int i=0;i<8;i++)v[i]=(signed char)(n*3+i);_Atomic(ExternalAtomicQ8) out=f(v,5);ExternalAtomicQ8 r=out;int h=0;for(int i=0;i<8;i++)h+=r[i]*(i+1);return h;}\n"
    "typedef short ExternalAtomicH2 __attribute__((vector_size(4)));"
    "_Atomic(ExternalAtomicH2) external_atomic_h2(_Atomic(ExternalAtomicH2) x,int k){ExternalAtomicH2 v=x;return v*(short)k;}"
    "int external_apply_atomic_h2(_Atomic(ExternalAtomicH2)(*f)(_Atomic(ExternalAtomicH2),int),int n){ExternalAtomicH2 v={(short)n,(short)(-7*n)};"
    "_Atomic(ExternalAtomicH2) out=f(v,3);ExternalAtomicH2 r=out;return r[0]*5+r[1];}\n"
    "#endif\n"
    // Vectors of 32 and 64 bytes both ways at every -march (#1193): passed in memory where the target's registers are
    // narrower, and returned split across them as LLVM returns them (xmm0 and xmm1, xmm0 to xmm3, ymm0 and ymm1), or
    // in ymm0 or zmm0 whole. Each element is changed, and the results are folded by their bytes.
    "typedef int ExternalWide8 __attribute__((vector_size(32)));typedef double ExternalWide16 __attribute__((vector_size(64)));"
    "static unsigned external_wide_fold(const void*p,unsigned long n){const unsigned char*b=p;unsigned h=2166136261u;"
    "for(unsigned long i=0;i<n;i++)h=(h^b[i])*16777619u;return h;}"
    "ExternalWide8 external_wide8(ExternalWide8 x,int k){for(int i=0;i<8;i++)x[i]=x[i]*3+k-i;return x;}"
    "ExternalWide16 external_wide16(ExternalWide16 x,int k){for(int i=0;i<8;i++)x[i]=x[i]*0.5+k+i;return x;}"
    "int external_apply_wide8(ExternalWide8(*f)(ExternalWide8,int),int n){ExternalWide8 v;for(int i=0;i<8;i++)v[i]=n*(i+3)-i;"
    "ExternalWide8 r=f(v,n+1);return (int)external_wide_fold(&r,sizeof(r));}"
    "int external_apply_wide16(ExternalWide16(*f)(ExternalWide16,int),int n){ExternalWide16 v;for(int i=0;i<8;i++)v[i]=n*0.25+i;"
    "ExternalWide16 r=f(v,n+2);return (int)external_wide_fold(&r,sizeof(r));}\n"
    // Variadic functions that a program calls through pointers with values past Tier 0's depth tables (#434's
    // review): their arguments as clang's callee reads them, folded into one value. external_variadic's doubles come
    // in xmm0 to xmm7, which its prologue saves for va_arg only when al isn't 0, then on the stack, and its longs in
    // the five registers after n's, then on the stack; external_floats' f8 and f9 come on the stack as floats, after
    // xmm0 to xmm7, and its unnamed longs and doubles all on the stack.
    "static unsigned long external_fold(unsigned long h,unsigned long v){return (h^v)*1099511628211UL;}\n"
    "unsigned long external_variadic(int n,...){va_list a;va_start(a,n);unsigned long h=(unsigned long)n;for(int i=0;i<20;i++){"
    "long l=va_arg(a,long);double d=va_arg(a,double);unsigned long b;__builtin_memcpy(&b,&d,8);h=external_fold(external_fold(h,(unsigned long)l),b);}"
    "va_end(a);return h;}\n"
    "unsigned long external_floats(int n,float f0,float f1,float f2,float f3,float f4,float f5,float f6,float f7,float f8,float f9,int m,...){"
    "const float f[10]={f0,f1,f2,f3,f4,f5,f6,f7,f8,f9};va_list a;va_start(a,m);unsigned long h=(unsigned long)n*31+(unsigned long)m;"
    "for(int i=0;i<10;i++){unsigned b;__builtin_memcpy(&b,&f[i],4);h=external_fold(h,b);}for(int i=0;i<10;i++){long l=va_arg(a,long);"
    "double d=va_arg(a,double);unsigned long b;__builtin_memcpy(&b,&d,8);h=external_fold(external_fold(h,(unsigned long)l),b);}va_end(a);return h;}\n"
    // Transparent unions' arguments (#147), each folded from the bytes of the member its caller set: p's pointer (k's bit
    // 0 clear) or long, q's pointer (k's bit 1 clear) or double, and the others' alike whatever was set. The _bits
    // functions read the register a short, an unsigned short and a _Bool arrive in as 32 bits, as clang's caller
    // extends them. The _apply functions call a program's through a pointer, and _variadic reads the unions with va_arg.
    BUSTER_CHECK_TEST_TRANSPARENT_UNIONS
    "static unsigned long external_tw(ExternalTW w){return (unsigned long)w.s.a+(unsigned long)(w.s.b&0xffff)*7;}"
    "unsigned long external_transparent(int k,ExternalTF f,ExternalTS s,ExternalTP p,ExternalTQ q,ExternalTD d,ExternalTW w,ExternalTA a,ExternalTB b){"
    "unsigned long bits;__builtin_memcpy(&bits,&q,8);unsigned long h=external_fold((unsigned long)k,(unsigned long)(f.q.a+f.q.b*3+s.s*5));"
    "h=external_fold(h,(unsigned long)((k&1)?p.l:*p.p));h=external_fold(h,(k&2)?bits:(unsigned long)*q.p);h=external_fold(h,(unsigned long)d.l);"
    "h=external_fold(h,external_tw(w));h=external_fold(h,(unsigned long)(a.a[0]+a.a[1]*3));return external_fold(h,(unsigned long)(b.s.a+b.s.b*3+b.s.c*5));}\n"
    "int external_short_bits(int bits){return bits;}int external_ushort_bits(int bits){return bits;}int external_bool_bits(int bits){return bits;}\n"
    // An _Atomic one, which isn't transparent: it passes as the atomic type (on the stack, as clang passes this one).
    "unsigned long external_atomic_transparent(_Atomic ExternalTF f){ExternalTF v=f;return (unsigned long)(v.q.a+v.q.b*3);}\n"
    // Of a union of its layout that a program makes transparent itself (check_test_program_transparent_first).
    "unsigned long external_transparent_first(ExternalTF f){return (unsigned long)(f.q.a+f.q.b*7);}\n"
    "unsigned long external_transparent_apply(unsigned long(*f)(int,ExternalTF,ExternalTS,ExternalTP,ExternalTQ,ExternalTD,ExternalTW,ExternalTA,ExternalTB),int n){"
    "struct external_ip ip={n,n*2};struct external_big big={n,n+1,n+2};long x=n*5,y=n+6;struct external_w ws={n,n+9};"
    "return f(0,ip,(short)-n,&x,&y,(struct external_dv){n*0.25},(__int128)n*8,ip,big)"
    "^f(3,(struct external_fp){n*0.5f,1.5f},(unsigned short)(n*3),(long)n*7,n*0.75,(long)n*11,ws,ip,(struct external_big_bytes){{1,2,n}})"
    "^f(1,ip,(short)n,(long)n,&y,(long)n,(long double)n,ip,big);}\n"
    "unsigned long external_transparent_variadic(int count,...){va_list a;va_start(a,count);unsigned long h=(unsigned long)count;"
    "for(int i=0;i<count;i++){ExternalTF f=va_arg(a,ExternalTF);ExternalTS s=va_arg(a,ExternalTS);ExternalTD d=va_arg(a,ExternalTD);"
    "ExternalTW w=va_arg(a,ExternalTW);ExternalTA r=va_arg(a,ExternalTA);ExternalTB b=va_arg(a,ExternalTB);"
    "h=external_fold(h,(unsigned long)(f.q.a+f.q.b*3+s.s*5));h=external_fold(h,(unsigned long)d.l);h=external_fold(h,external_tw(w));"
    "h=external_fold(h,(unsigned long)(r.a[0]+r.a[1]*3));h=external_fold(h,(unsigned long)(b.s.a+b.s.b*3+b.s.c*5));}va_end(a);return h;}\n"
    "unsigned long external_transparent_variadic_apply(unsigned long(*f)(int,...),int n){ExternalTF tf={.q={n,-n}};ExternalTS ts={.s=(short)(n-7)};"
    "ExternalTD td={.l=(long)n*1000003};ExternalTW tw={.w=(__int128)n*3};ExternalTA ta={.q={n,5}};ExternalTB tb={.s={n,2,3}};"
    "return f(2,tf,ts,td,tw,ta,tb,tf,ts,td,tw,ta,tb);}\n"
    // Unions of a stack slot of their own (#1844), in registers and after 6 longs and 8 doubles, where every one takes the
    // stack, passed to the program's functions through pointers, and the register-classified ones read with va_arg (clang
    // reads one whose member is in memory at the union's alignment, where its caller aligned it as the member).
    BUSTER_CHECK_TEST_TRANSPARENT_OWN_UNIONS
    "static unsigned long external_own_fold(unsigned long h,int k,ExternalOL l,ExternalOM m,ExternalOB b,ExternalOP p,ExternalOD d,ExternalOK s,ExternalOQ q){"
    "h=external_fold(external_fold(h,(unsigned long)k),(unsigned long)l.s.a);h=external_fold(h,(unsigned long)(m.m.a+m.m.b*3+(long)(m.m.c*4)+(long)(m.m.d*8)));"
    "h=external_fold(h,(unsigned long)(b.s.a+b.s.b*3+b.s.c*5));h=external_fold(h,(unsigned long)((k&1)?p.l:*p.p));h=external_fold(h,(unsigned long)(long)(d.s.v*4));"
    "h=external_fold(h,(unsigned long)(s.p.c+s.p.i*3));return external_fold(h,(unsigned long)(q.p.c+q.p.i*5));}\n"
    "unsigned long external_own(int k,ExternalOL l,ExternalOM m,ExternalOB b,ExternalOP p,ExternalOD d,ExternalOK s,ExternalOQ q){"
    "return external_own_fold(0,k,l,m,b,p,d,s,q);}\n"
    "unsigned long external_own_late(long l0,long l1,long l2,long l3,long l4,long l5,double d0,double d1,double d2,double d3,double d4,double d5,double d6,"
    "double d7,int k,ExternalOL l,ExternalOM m,ExternalOB b,ExternalOP p,ExternalOD d,ExternalOK s,ExternalOQ q){"
    "return external_own_fold((unsigned long)(l0+l1+l2+l3+l4+l5*3)+(unsigned long)(long)(d0+d1+d2+d3+d4+d5+d6+d7*3),k,l,m,b,p,d,s,q);}\n"
    "unsigned long external_own_apply(unsigned long(*f)(int,ExternalOL,ExternalOM,ExternalOB,ExternalOP,ExternalOD,ExternalOK,ExternalOQ),"
    "unsigned long(*g)(long,long,long,long,long,long,double,double,double,double,double,double,double,double,int,ExternalOL,ExternalOM,ExternalOB,"
    "ExternalOP,ExternalOD,ExternalOK,ExternalOQ),int n){long x=n*3;struct external_mix m={n,2,1.5f,n*0.5f};struct external_pk k={(char)n,n*7};"
    "struct external_big big={n,n+1,n+2};ExternalOL ol={.s={n*5}};"
    "return f(0,(struct external_l){n},m,big,&x,(struct external_dv){n*0.25},k,k)^f(1,ol,m,(struct external_big_bytes){{1,2,n}},(long)n,(struct external_dv){2.5},k,k)"
    "^g(1,2,3,4,5,n,0.5,1,2,3,4,5,6,n*0.5,0,ol,m,big,&x,(struct external_dv){n*0.75},k,k);}\n"
    "unsigned long external_own_variadic(int count,...){va_list a;va_start(a,count);unsigned long h=(unsigned long)count;"
    "for(int i=0;i<count;i++){ExternalOL l=va_arg(a,ExternalOL);ExternalOM m=va_arg(a,ExternalOM);ExternalOP p=va_arg(a,ExternalOP);"
    "ExternalOD d=va_arg(a,ExternalOD);h=external_fold(h,(unsigned long)l.s.a);h=external_fold(h,(unsigned long)(m.m.a+m.m.b*3+(long)(m.m.c*4)+(long)(m.m.d*8)));"
    "h=external_fold(h,(unsigned long)p.l);h=external_fold(h,(unsigned long)(long)(d.s.v*4));}va_end(a);return h;}\n"
    "unsigned long external_own_variadic_apply(unsigned long(*f)(int,...),int n){ExternalOL l={.s={n}};ExternalOM m={.m={n,-n,0.5f,n*0.25f}};"
    "ExternalOP p={.l=n*9L};ExternalOD d={.s={n*1.5}};return f(3,l,m,p,d,l,m,p,d,l,m,p,d);}\n"
    // The pairs' and the vector's unions of a stack slot of their own (#1844), in registers, after 5 longs and 8 doubles,
    // where every one takes the stack, through a pointer and with va_arg.
    "static unsigned long external_pair_fold(unsigned long h,int k,ExternalOW w,ExternalOV v,ExternalOC c,ExternalOX x){"
    "h=external_fold(external_fold(h,(unsigned long)k),(unsigned long)w.w^(unsigned long)(w.w>>64));h=external_fold(h,(unsigned long)(v.v.x[0]+v.v.x[3]*7));"
    "h=external_fold(h,(unsigned long)(__real__ c.c+__imag__ c.c*9));return external_fold(h,(unsigned long)x.w^(unsigned long)(x.w>>64));}\n"
    "unsigned long external_own_pairs(int k,ExternalOW w,ExternalOV v,ExternalOC c,ExternalOX x){return external_pair_fold(0,k,w,v,c,x);}\n"
    "unsigned long external_own_pairs_late(long l0,long l1,long l2,long l3,long l4,double d0,double d1,double d2,double d3,double d4,double d5,double d6,"
    "double d7,int k,ExternalOW w,ExternalOV v,ExternalOC c,ExternalOX x){"
    "return external_pair_fold((unsigned long)(l0+l1+l2+l3+l4*3)+(unsigned long)(long)(d0+d1+d2+d3+d4+d5+d6+d7*3),k,w,v,c,x);}\n"
    "unsigned long external_own_pairs_apply(unsigned long(*f)(int,ExternalOW,ExternalOV,ExternalOC,ExternalOX),"
    "unsigned long(*g)(long,long,long,long,long,double,double,double,double,double,double,double,double,int,ExternalOW,ExternalOV,ExternalOC,ExternalOX),int n){"
    "__int128 w=((__int128)n<<64)|(unsigned)(n*3);struct external_v v={{n,2,3,n*4}};_Complex long c=n;__imag__ c=n*2;ExternalOW ow={.w=w};"
    "return f(0,w,v,c,w)^f(1,ow,v,c,(__int128)n)^g(1,2,3,4,n,0.5,1,2,3,4,5,6,n*0.5,2,w,v,c,w);}\n"
    "unsigned long external_own_pairs_variadic(int count,...){va_list a;va_start(a,count);unsigned long h=(unsigned long)count;"
    "for(int i=0;i<count;i++){ExternalOW w=va_arg(a,ExternalOW);ExternalOV v=va_arg(a,ExternalOV);ExternalOC c=va_arg(a,ExternalOC);"
    "h=external_fold(h,(unsigned long)w.w^(unsigned long)(w.w>>64));h=external_fold(h,(unsigned long)(v.v.x[0]+v.v.x[3]*7));"
    "h=external_fold(h,(unsigned long)(__real__ c.c+__imag__ c.c*9));}va_end(a);return h;}\n"
    "unsigned long external_own_pairs_variadic_apply(unsigned long(*f)(int,...),int n){ExternalOW w={.w=(__int128)n*5};"
    "ExternalOV v={.v={{n,-n,7,n*2}}};ExternalOC c={.c=n};return f(4,w,v,c,w,v,c,w,v,c,w,v,c);}\n"
    // The unions of a LARGER member in memory (#3400): a byval slot of the union's bytes at the member's 8 where clang has
    // a general register, else the member's i56 (after four longs, an _Atomic long, which clang doesn't count and LLVM
    // gives r8, and an __int128, which clang counts two for and LLVM puts on the stack): in r9, then in 8-byte slots;
    // the program's through pointers, and va_arg's, at offsets of the union's alignment, where clang's reads them.
    BUSTER_CHECK_TEST_TRANSPARENT_GROWN_UNIONS
    "static unsigned long external_grown_fold(unsigned long h,int k,ExternalGA a,ExternalGB b,ExternalGP p,ExternalGK q,ExternalGE e){"
    "h=external_fold(external_fold(h,(unsigned long)k),(unsigned long)(a.g.i+a.g.s*3));h=external_fold(h,(unsigned long)(b.g.i*5+b.g.s));"
    "h=external_fold(h,(unsigned long)(p.g.i+p.g.s*7));h=external_fold(h,(unsigned long)(q.g.i*9+q.g.s));return external_fold(h,(unsigned long)e.e.l);}\n"
    "unsigned long external_grown(int k,ExternalGA a,ExternalGB b,ExternalGP p,ExternalGK q,ExternalGE e){return external_grown_fold(0,k,a,b,p,q,e);}\n"
    "unsigned long external_grown_tight(long l0,long l1,long l2,long l3,_Atomic long u,__int128 w,ExternalGB b,ExternalGA a,long z){"
    "unsigned long h=external_fold((unsigned long)(l0+l1*3+l2*5+l3*7+u),(unsigned long)w^(unsigned long)(w>>64));"
    "h=external_fold(h,(unsigned long)(b.g.i*5+b.g.s));h=external_fold(h,(unsigned long)(a.g.i+a.g.s*3));return external_fold(h,(unsigned long)z);}\n"
    "unsigned long external_grown_late(long l0,long l1,long l2,long l3,long l4,long l5,long l6,int k,ExternalGA a,ExternalGB b,ExternalGP p,ExternalGK q,"
    "ExternalGE e){return external_grown_fold((unsigned long)(l0+l1+l2+l3+l4+l5+l6*3),k,a,b,p,q,e);}\n"
    "unsigned long external_grown_apply(unsigned long(*f)(int,ExternalGA,ExternalGB,ExternalGP,ExternalGK,ExternalGE),"
    "unsigned long(*t)(long,long,long,long,_Atomic long,__int128,ExternalGB,ExternalGA,long),"
    "unsigned long(*g)(long,long,long,long,long,long,long,int,ExternalGA,ExternalGB,ExternalGP,ExternalGK,ExternalGE),int n){"
    "struct external_gr r={n*3,(short)(n-2)};struct external_ge e={(long)n*1000003};ExternalGA ga={.g={n,5}};"
    "return f(0,r,r,r,r,e)^f(1,ga,r,r,r,e)^t(1,2,3,n,(long)n*9,(__int128)n<<64,r,ga,n*7)^g(1,2,3,4,5,6,n,2,ga,r,r,r,e);}\n"
    "unsigned long external_grown_variadic(int count,...){va_list a;va_start(a,count);unsigned long h=(unsigned long)count;"
    "for(int i=0;i<count;i++){ExternalGP p=va_arg(a,ExternalGP);ExternalGK q=va_arg(a,ExternalGK);ExternalGA r=va_arg(a,ExternalGA);"
    "h=external_fold(h,(unsigned long)(p.g.i+p.g.s*7));h=external_fold(h,(unsigned long)(q.g.i*9+q.g.s));h=external_fold(h,(unsigned long)(r.g.i+r.g.s*3));}"
    "va_end(a);return h;}\n"
    "unsigned long external_grown_variadic_apply(unsigned long(*f)(int,...),int n){ExternalGP p={.g={n,-3}};ExternalGK q={.g={-n,4}};ExternalGA r={.g={n*7,1}};"
    "return f(3,p,q,r,p,q,r,p,q,r);}\n"
    // After 6 longs and 8 doubles, every union on the stack.
    "unsigned long external_transparent_late(long l0,long l1,long l2,long l3,long l4,long l5,double d0,double d1,double d2,double d3,double d4,double d5,"
    "double d6,double d7,ExternalTF f,ExternalTS s,ExternalTP p,ExternalTD d,ExternalTW w,ExternalTA a,ExternalTB b){"
    "unsigned long h=external_fold((unsigned long)(l0+l1+l2+l3+l4+l5*3),(unsigned long)(long)(d0+d1+d2+d3+d4+d5+d6+d7*3));"
    "h=external_fold(h,(unsigned long)(f.q.a+f.q.b*3+s.s*5));h=external_fold(h,(unsigned long)p.l);h=external_fold(h,(unsigned long)d.l);"
    "h=external_fold(h,external_tw(w));return external_fold(external_fold(h,(unsigned long)(a.a[0]+a.a[1]*3)),(unsigned long)(b.s.a+b.s.b*3+b.s.c*5));}\n"
    "unsigned long external_transparent_late_apply(unsigned long(*f)(long,long,long,long,long,long,double,double,double,double,double,double,double,double,"
    "ExternalTF,ExternalTS,ExternalTP,ExternalTD,ExternalTW,ExternalTA,ExternalTB),int n){struct external_ip ip={n,7};"
    "return f(1,2,3,4,5,n,0.5,1,2,3,4,5,6,n*0.5,ip,(short)-n,(long)n*9,(long)n*3,(__int128)n*5,ip,(struct external_big){n,n,n});}\n"
    "int main(void) { for (int repeat=0;repeat<3;repeat++) for (int n=-3;n<=19;n++) {\n"
    "int a=tested(n), b=oracle_tested(n); if(a!=b) { fprintf(stderr,\"n=%d repeat=%d ours=%d oracle=%d\\n\",n,repeat,a,b); return 1; } }\n"
    "if(terminal_probe){int p=fork();if(p<0)return 2;if(!p){struct rlimit r={0,0};setrlimit(RLIMIT_CORE,&r);terminal_probe();_exit(91);}\n"
    "int s=0;if(waitpid(p,&s,0)!=p||!WIFSIGNALED(s)||WTERMSIG(s)!=SIGILL)return 3;}return 0;}\n";

// What every program's build shares (#465): the harness, the same for every program, compiled once per run for each
// signedness of plain char that the programs are read with, and the last oracle, which the scalar and vector paths' programs of one source share, compiled once per distinct text. Each
// compiles with -c and the flags of the one-step build, which gives the code that build's compiles give, so a program
// links the same harness, oracle and checked object as before. The oracle's key is its text and plain char's
// signedness. And the last program that linked and ran as expected against its oracle (alike, or with `mismatch`
// differently): its checked object's bytes. A program whose checked object, oracle and expectation are that one's links
// the same files into the same executable, which runs alike, so it takes that run's result (the vector path's program,
// mostly, after the scalar path's).
typedef struct CheckTestProgramBuild CheckTestProgramBuild;
struct CheckTestProgramBuild
{
    char directory[40];
    bool harness[2];
    char* oracle;
    size_t oracle_length;
    bool oracle_unsigned;
    // The clang mode the cached oracle and checked object's source were built with.
    const char* oracle_standard;
    uint8_t* object;
    uint64_t object_size;
    const char* object_standard;
    bool mismatch;
};

static CheckTestProgramBuild check_test_program_build;

// The -march of the oracle's and the harness's compiles and of the link: x86-64's, or a section's own while it runs
// (check_test_program_vectors, which releases the build around it).
static const char* check_test_program_march = "-march=x86-64";

// The target options a program's functions' target attributes resolve against (CheckProgramOptions.predefines): the
// oracle's -march, whose X86Target the section gives the harness (x86_target_parse's profile of the same name). The
// source graph also takes the oracle's explicit contraction-off policy; the dedicated driver tests on/default.
static PredefinesOptions check_test_program_predefines(void)
{
    static uint8_t floats[] = { PREDEFINES_FLOAT_CONTRACT_OFF };
    return (PredefinesOptions){
        .march = check_test_program_march + strlen("-march="),
        .floats = floats,
        .float_count = BUSTER_ARRAY_LENGTH(floats),
    };
}

// Whether a program's preprocessing searches the system's headers, as the oracle's compile does: off, or while a
// section's programs include them (the intrinsics' headers, check_test_program_x86's).
static bool check_test_program_includes = false;

// Whether a program has a target_clones body that is compiled once per version (CheckProgramTargetGroup.copies). Only
// check_program_select makes those copies, as -c does: lowered without it, the body is its first version's alone, and
// every other version's name is a symbol at that code (#2516).
static bool check_test_program_cloned(const CheckProgram* program)
{
    bool copies = false;
    for (uint32_t g = 0; g < program->target_group_count; g += 1)
    {
        copies |= program->target_groups[g].copies;
    }
    return copies;
}

// The flags of every compile and link of a program, with plain char as the checker read it (`char_unsigned`), for the
// oracle and the harness alike. These existing fixtures request contraction off on both sides; tests/driver/fp_contract.c
// separately checks default/on contraction, its lexical overrides and the target boundary.
#define BUSTER_CHECK_TEST_PROGRAM_FLAGS(char_unsigned) \
    "-std=gnu17", \
    "-O0", \
    (char*)check_test_program_march, \
    "-ffp-contract=off", \
    "-fno-common", \
    (char_unsigned) ? "-funsigned-char" : "-fsigned-char", \
    "-Wno-unused-value", \
    "-no-pie"

// The harness's name in the build's directory for plain char's signedness.
static const char* check_test_program_harness_name(bool char_unsigned)
{
    return char_unsigned ? "harness-unsigned" : "harness-signed";
}

static void check_test_program_build_path(char* path, size_t size, const char* name)
{
    snprintf(path, size, "%s/%s", check_test_program_build.directory, name);
}

// Compiles `name`.c, holding `text`, to `name`.o in the build's directory with clang's `standard` mode, its diagnostics
// in `out` and `err`.
static bool check_test_program_compile(const char* clang, const char* name, const char* text, size_t length, bool char_unsigned, const char* standard,
                                       const char* out, const char* err)
{
    char source[96];
    char object[96];
    char file[32];
    snprintf(file, sizeof(file), "%s.c", name);
    check_test_program_build_path(source, sizeof(source), file);
    snprintf(file, sizeof(file), "%s.o", name);
    check_test_program_build_path(object, sizeof(object), file);
    char* compile[] = {
        (char*)clang,
        BUSTER_CHECK_TEST_PROGRAM_FLAGS(char_unsigned),
        (char*)standard,
        "-Wno-unused-command-line-argument",
        "-c",
        source,
        "-o",
        object,
        0,
    };
    return parse_test_write(source, text, length) && object_test_run(compile, out, err) == 0;
}

// The build's directory, made on first use, with the harness of plain char's signedness compiled into it, and the
// oracle of `text` compiled in `standard` unless it is the last one's, with the same signedness (`*same_oracle`).
static bool check_test_program_prepare(const char* clang, const char* text, size_t length, bool char_unsigned, const char* standard, const char* out,
                                       const char* err, bool* same_oracle)
{
    CheckTestProgramBuild* build = &check_test_program_build;
    if (build->directory[0] == 0)
    {
        snprintf(build->directory, sizeof(build->directory), "/tmp/buster-check-build-XXXXXX");
        if (mkdtemp(build->directory) == 0)
        {
            build->directory[0] = 0;
        }
    }
    bool valid = build->directory[0] != 0;
    if (valid && !build->harness[char_unsigned])
    {
        build->harness[char_unsigned] = check_test_program_compile(clang, check_test_program_harness_name(char_unsigned), check_test_program_harness,
                                                                   sizeof(check_test_program_harness) - 1, char_unsigned, "-std=gnu17", out, err);
        valid = build->harness[char_unsigned];
    }
    const bool same = build->oracle != 0 && build->oracle_unsigned == char_unsigned && build->oracle_length == length &&
                      strcmp(build->oracle_standard, standard) == 0 && memcmp(build->oracle, text, length) == 0;
    *same_oracle = same;
    if (valid && !same)
    {
        free(build->oracle);
        build->oracle = 0;
        valid = check_test_program_compile(clang, "oracle", text, length, char_unsigned, standard, out, err);
        build->oracle = valid ? malloc(length) : 0;
        valid = valid && build->oracle != 0;
        if (valid)
        {
            memcpy(build->oracle, text, length);
            build->oracle_length = length;
            build->oracle_unsigned = char_unsigned;
            build->oracle_standard = standard;
        }
    }
    return valid;
}

// Removes the build's directory and forgets what it held: run after the last program of a section, and once the
// run's sections are done (run_checker_tests), so that no mode leaves the directory behind.
static void check_test_program_build_release(void)
{
    CheckTestProgramBuild* build = &check_test_program_build;
    if (build->directory[0] != 0)
    {
        static const char* const names[] = { "harness-signed.c", "harness-signed.o", "harness-unsigned.c", "harness-unsigned.o", "oracle.c", "oracle.o" };
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(names); i += 1)
        {
            char path[96];
            check_test_program_build_path(path, sizeof(path), names[i]);
            unlink(path);
        }
        rmdir(build->directory);
    }
    free(build->oracle);
    free(build->object);
    *build = (CheckTestProgramBuild){ 0 };
}

// Compile and link every fixture. A CPU-ineligible fixture omits only the final launch and must not publish an
// execution-result cache entry that a later eligible comparison could mistake for a successful run. The oracle is
// clang's build of `oracle_source`, or of the program's own source where it is null; with `mismatch`, the run must find
// the two different on some input rather than alike.
static bool check_test_program_execute_with_alignment(Link* link, const char* source, const char* oracle_source, bool mismatch, const char* clang,
                                                       bool char_unsigned, bool alignment_oracle, bool run_native, const char* standard, Arena* scratch)
{
    const char* profile_variable;
    const char* profile_directory = test_profile_directory(&profile_variable);
    const bool profiling = profile_directory != 0;
    char directory[PATH_MAX - sizeof("/checked.o") + 1];
    const int directory_length = snprintf(directory, sizeof(directory), "%s/buster-check-program-XXXXXX",
                                          profiling ? profile_directory : "/tmp");
    const bool fits = directory_length >= 0 && (size_t)directory_length < sizeof(directory);
    const bool created = fits && directory[0] == '/' && mkdtemp(directory) != 0;
    if (profiling && !created)
    {
        fprintf(stderr, "test-checker: %s must be an existing writable absolute directory: %s (%s)\n",
                profile_variable, profile_directory, !fits ? "path too long" : directory[0] != '/' ? "relative path" : strerror(errno));
    }
    char object[PATH_MAX];
    char executable[PATH_MAX];
    char out[PATH_MAX];
    char err[PATH_MAX];
    char harness[96];
    char oracle[96];
    snprintf(object, sizeof(object), "%s/checked.o", directory);
    snprintf(executable, sizeof(executable), "%s/run", directory);
    snprintf(out, sizeof(out), "%s/out", directory);
    snprintf(err, sizeof(err), "%s/err", directory);
    char* text = 0;
    size_t length = 0;
    uint8_t* bytes = 0;
    uint64_t size = 0;
    bool same_oracle = false;
    bool valid = created && check_test_program_object(link, object, scratch, &bytes, &size);
    if (valid && alignment_oracle)
    {
        // The direct temporary's stronger alignment is Buster's contract; Clang may give the aligned(1) return
        // type a less-aligned temporary. Compare against the independently calculated result instead.
        const char expected[] = "int oracle_tested(int n){return 11*n+52;}\n";
        length = sizeof(expected) - 1;
        text = malloc(length + 1);
        valid = text != 0;
        if (valid)
        {
            memcpy(text, expected, length + 1);
        }
    }
    else if (valid)
    {
        valid = check_test_program_oracle(link, oracle_source != 0 ? oracle_source : source, &text, &length);
    }
    valid = valid && check_test_program_prepare(clang, text, length, char_unsigned, standard, out, err, &same_oracle);
    CheckTestProgramBuild* build = &check_test_program_build;
    const bool same = run_native && valid && same_oracle && build->object != 0 && build->mismatch == mismatch && build->object_size == size &&
                      strcmp(build->object_standard, standard) == 0 && memcmp(build->object, bytes, size) == 0;
    char harness_file[32];
    snprintf(harness_file, sizeof(harness_file), "%s.o", check_test_program_harness_name(char_unsigned));
    check_test_program_build_path(harness, sizeof(harness), harness_file);
    check_test_program_build_path(oracle, sizeof(oracle), "oracle.o");
    char* link_command[] = {
        (char*)clang,
        BUSTER_CHECK_TEST_PROGRAM_FLAGS(char_unsigned),
        harness,
        oracle,
        object,
        "-lm",
        "-latomic",
        "-o",
        executable,
        0,
    };
    char* run[] = {
        executable,
        0,
    };
    // The harness's status: 0 when the program and its oracle agree on every input, 1 at the first input they don't.
    const int status = mismatch ? 1 : 0;
    valid = valid && (same || (object_test_run(link_command, out, err) == 0 && (!run_native || object_test_run(run, out, err) == status)));
    if (!same)
    {
        free(build->object);
        build->object = valid && run_native ? bytes : 0;
        build->object_size = valid && run_native ? size : 0;
        build->object_standard = standard;
        build->mismatch = mismatch;
        bytes = valid && run_native ? 0 : bytes;
    }
    if (!valid && created)
    {
        // The sources go beside the object, as the one-step build had them.
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/driver.c", directory);
        parse_test_write(path, check_test_program_harness, sizeof(check_test_program_harness) - 1);
        snprintf(path, sizeof(path), "%s/oracle.c", directory);
        if (text != 0)
        {
            parse_test_write(path, text, length);
        }
        fprintf(stderr, "test-checker: program oracle files retained in %s\n", directory);
        FILE* diagnostics = fopen(err, "rb");
        if (diagnostics != 0)
        {
            char line[256];
            while (fgets(line, sizeof(line), diagnostics) != 0)
            {
                fputs(line, stderr);
            }
            fclose(diagnostics);
        }
    }
    free(text);
    free(bytes);
    if (valid && created)
    {
        unlink(object);
        unlink(out);
        unlink(err);
        // Keep the actual path the profiler saw. A cache hit and a link-only fixture have no new executed image.
        if (!profiling || same || !run_native)
        {
            unlink(executable);
            rmdir(directory);
        }
    }
    return valid;
}

static bool check_test_program_execute(Link* link, const char* source, const char* clang, bool char_unsigned, Arena* scratch)
{
    return check_test_program_execute_with_alignment(link, source, 0, false, clang, char_unsigned, false, true, "-std=gnu17", scratch);
}

// check_data_prepare before #592, with a token-sized column of each object's last declaration: the oracle that
// check_test_program_sparse compares check_data_prepare's records and check_data_latest with.
typedef struct CheckTestDataPrepared CheckTestDataPrepared;
struct CheckTestDataPrepared
{
    CheckDataObject* objects;
    uint32_t count;
    uint32_t* references;
    uint32_t reference_count;
    uint32_t* reference_offsets;
    uint32_t* reference_counts;
    uint32_t* latest_declarations;
    uint32_t object_count;
    LinkCapacity capacity;
    uint32_t token;
    CheckDataInputStatus status;
};

static uint32_t check_test_initializer_index_dense(const CheckInitializers* initializers, uint32_t token)
{
    uint32_t index = 0;
    for (uint32_t i = 0; i < initializers->count; i += 1)
    {
        index = initializers->declaration_tokens[i] == token ? i + 1 : index;
    }
    return index;
}

static bool check_test_data_prepare_dense(CheckTestDataPrepared* result, const CheckInput* input, const CheckStream* stream,
                                          const CheckInitializers* initializers, TypeTable* types, Arena* output, Arena* scratch)
{
    *result = (CheckTestDataPrepared){ .token = BUSTER_CHECK_NONE };
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    const uint32_t count = input->token_count;
    bool valid = output != scratch && count < UINT32_MAX && stream->count == input->graph.count &&
                 stream->status == CHECK_STATUS_OK && stream->report_count == 0 && input->report_count == 0 && !input->resource &&
                 initializers->status == CHECK_INIT_OK && initializers->token_count == count && (initializers->count == 0 || (initializers->plans != 0 && initializers->declaration_tokens != 0));
    result->status = valid ? CHECK_DATA_INPUT_OK : CHECK_DATA_INPUT_INVALID;
    result->latest_declarations = valid ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, (uint64_t)count + 1) : 0;
    uint32_t* definition = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    uint8_t* alignments = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)count + 1) : 0;
    if (valid && (result->latest_declarations == 0 || definition == 0 || alignments == 0))
    {
        valid = false;
        result->status = CHECK_DATA_INPUT_RESOURCE;
    }
    for (uint32_t object = 0; valid && object <= count; object += 1)
    {
        result->latest_declarations[object] = BUSTER_CHECK_NONE;
        definition[object] = BUSTER_CHECK_NONE;
        alignments[object] = 0;
    }
    for (uint32_t token = 0; valid && token < count; token += 1)
    {
        if (input->declarations[token] == token && check_input_bit(input->declaration_bits, token, BUSTER_CHECK_BITS_STATIC))
        {
            const uint32_t object = input->objects[token];
            valid = object != 0 && object <= count;
            if (valid)
            {
                result->latest_declarations[object] = token;
                const CheckInputAttributes* attributes = check_sparse_wide_present(input->attributes) ? check_input_attributes_at(input, token) : 0;
                if (attributes != 0 && attributes->alignment_log2 > alignments[object])
                {
                    alignments[object] = attributes->alignment_log2;
                }
                if (input->declaration_kinds[token] == C_BINDING_OBJECT && (check_input_bit(input->declaration_bits, token, BUSTER_CHECK_BITS_DEFINED) || check_input_bit(input->declaration_bits, token, BUSTER_CHECK_BITS_TENTATIVE)))
                {
                    const uint32_t previous = definition[object];
                    if (previous == BUSTER_CHECK_NONE || !check_input_bit(input->declaration_bits, previous, BUSTER_CHECK_BITS_DEFINED))
                    {
                        definition[object] = token;
                    }
                }
            }
            else
            {
                result->status = CHECK_DATA_INPUT_INVALID;
                result->token = token;
            }
        }
    }
    uint64_t object_capacity = input->string_count;
    uint64_t reference_capacity = 0;
    for (uint32_t object = 1; valid && object <= count; object += 1)
    {
        const uint32_t token = definition[object];
        if (token != BUSTER_CHECK_NONE)
        {
            object_capacity += 1;
            const uint32_t plan_index = check_test_initializer_index_dense(initializers, token);
            valid = plan_index <= initializers->count &&
                    (plan_index == 0 || (initializers->declaration_tokens != 0 && initializers->declaration_tokens[plan_index - 1] == token));
            // A label difference's part references two objects (#1447), as check_data_input_prepared counts it.
            const CheckInitializer* counted = valid && plan_index != 0 ? initializers->plans + plan_index - 1 : NULL;
            reference_capacity += counted != NULL ? (uint64_t)counted->count * (counted->label_differences ? 2u : 1u) : 0;
            if (!valid)
            {
                result->status = CHECK_DATA_INPUT_INVALID;
                result->token = token;
            }
        }
    }
    if (valid && (object_capacity > UINT32_MAX || reference_capacity > UINT32_MAX))
    {
        valid = false;
        result->status = CHECK_DATA_INPUT_RESOURCE;
    }
    if (valid)
    {
        result->objects = object_capacity != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckDataObject, object_capacity) : 0;
        result->reference_offsets = object_capacity != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, object_capacity) : 0;
        result->reference_counts = object_capacity != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, object_capacity) : 0;
        result->references = reference_capacity != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, reference_capacity) : 0;
        valid = (object_capacity == 0 || (result->objects != 0 && result->reference_offsets != 0 && result->reference_counts != 0)) &&
                (reference_capacity == 0 || result->references != 0);
        result->status = valid ? CHECK_DATA_INPUT_OK : CHECK_DATA_INPUT_RESOURCE;
    }
    for (uint32_t object = 1; valid && object <= count; object += 1)
    {
        const uint32_t selected = definition[object];
        if (selected != BUSTER_CHECK_NONE)
        {
            const uint32_t latest = result->latest_declarations[object];
            const uint32_t plan_index = check_test_initializer_index_dense(initializers, selected);
            const CheckInitializer* plan = plan_index != 0 ? initializers->plans + plan_index - 1 : 0;
            TypeId type = input->declared_types[latest];
            const TypeId stripped = type_strip_aligned(types, type);
            if (plan == 0 && check_input_bit(input->declaration_bits, selected, BUSTER_CHECK_BITS_TENTATIVE) && type_kind(types, stripped) == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY)
            {
                // Tentative arrays acquire one element at translation-unit end; earlier uses retain their
                // historical incomplete TypeIds and cannot gain a retroactive sizeof result.
                type = type_array(types, types->types.base[type_index(stripped)], 1);
                if (type != 0)
                {
                    type |= type_qualifiers(stripped);
                    if (type_kind(types, input->declared_types[latest]) == BUSTER_TYPE_KIND_ALIGNED)
                    {
                        type = type_aligned(types, type, type_alignment_log2(types, input->declared_types[latest]));
                    }
                }
                valid = type != 0;
                if (!valid)
                {
                    result->status = types->last_error == TYPE_ERROR_FULL ? CHECK_DATA_INPUT_RESOURCE : CHECK_DATA_INPUT_INVALID;
                    result->token = selected;
                }
            }
            if (valid)
            {
                const uint32_t at = result->count;
                result->objects[at] = (CheckDataObject){
                    .type = type,
                    .object = object,
                    .initializer = check_sparse_get(input->initializer_nodes, selected),
                    .token = selected,
                    .initializer_plan = plan,
                    .alignment_log2 = check_data_declared_alignment(input, types, object, type, alignments[object]),
                };
                result->reference_offsets[at] = result->reference_count;
                const uint64_t reference_mark = scratch->length;
                uint32_t* references = 0;
                uint32_t reference_count = 0;
                if (plan != 0)
                {
                    // Use the opposite arena roles for this temporary result. The helper restores its scratch
                    // suffix in output, and we copy the surviving references before releasing its result suffix.
                    const CheckInitStatus status = check_initializer_references(types, plan, scratch, output, &references, &reference_count);
                    valid = status == CHECK_INIT_OK;
                    if (!valid)
                    {
                        result->status = status == CHECK_INIT_RESOURCE ? CHECK_DATA_INPUT_RESOURCE : CHECK_DATA_INPUT_INVALID;
                        result->token = selected;
                    }
                }
                valid = valid && reference_count <= reference_capacity - result->reference_count;
                for (uint32_t reference = 0; valid && reference < reference_count; reference += 1)
                {
                    valid = references[reference] != 0 && references[reference] <= count;
                    if (valid)
                    {
                        result->references[result->reference_count++] = references[reference];
                    }
                }
                // Each label difference's two labels, after the parts' references (#1447).
                for (uint32_t part = 0; valid && plan != 0 && part < plan->count; part += 1)
                {
                    const CheckValue* value = &plan->parts[part].value;
                    if (plan->parts[part].kind == CHECK_INIT_VALUE && value->constant_kind == CHECK_CONSTANT_LABEL_DIFFERENCE)
                    {
                        valid = result->reference_count + 2 <= reference_capacity;
                        if (valid)
                        {
                            result->references[result->reference_count++] = value->object;
                            result->references[result->reference_count++] = (uint32_t)value->integer;
                        }
                    }
                }
                if (!valid && result->status == CHECK_DATA_INPUT_OK)
                {
                    result->status = CHECK_DATA_INPUT_INVALID;
                    result->token = selected;
                }
                arena_pop_to(scratch, reference_mark);
                result->reference_counts[at] = result->reference_count - result->reference_offsets[at];
                result->count += 1;
            }
        }
    }
    for (uint32_t i = 0; valid && i < input->string_count; i += 1)
    {
        const CheckInitString* string = input->strings + i;
        valid = string->object != 0 && string->object <= count && result->latest_declarations[string->object] == BUSTER_CHECK_NONE;
        if (valid)
        {
            const uint32_t at = result->count++;
            result->objects[at] = (CheckDataObject){
                .type = string->type,
                .object = string->object,
                .initializer = BUSTER_CHECK_NONE,
                .token = string->token,
                .literal_bytes = string->bytes,
                .literal_size = string->byte_count,
                .string_pool = true,
                .mergeable_string = !string->predefined,
            };
            result->reference_offsets[at] = result->reference_count;
            result->reference_counts[at] = 0;
        }
        else
        {
            result->status = CHECK_DATA_INPUT_INVALID;
            result->token = string->token;
        }
    }
    if (valid)
    {
        result->object_count = count + 1;
        result->capacity = (LinkCapacity){
            .atoms = result->count,
            .fixups = result->reference_count,
            .wide_addends = result->reference_count,
        };
    }
    else
    {
        const CheckDataInputStatus status = result->status;
        const uint32_t token = result->token;
        arena_pop_to(output, output_mark);
        *result = (CheckTestDataPrepared){ .status = status, .token = token };
    }
    arena_pop_to(scratch, scratch_mark);
    return valid;
}

static int check_test_program_key_order(const void* a, const void* b)
{
    const uint64_t left = *(const uint64_t*)a;
    const uint64_t right = *(const uint64_t*)b;
    return left < right ? -1 : left > right ? 1 : 0;
}

// The passes #592 made sparse, on a checked program, against what they did before: check_data_prepare (`data`, of
// this program's unit) against the dense oracle above, record for record, with check_data_latest of every object, on
// both paths, check_program_token_body of every token against the column of every body's tokens the selection filled,
// and the objects check_program_object_access finds the unit defines, by both paths, against a walk of every token and
// the aliases and ifuncs (#147).
static bool check_test_program_sparse(const CheckProgram* program, const CheckDataPrepared* data, const CheckInput* input, const CheckStream* stream,
                                      const CheckInitializers* initializers, TypeTable* types, bool scalar, Arena* output, Arena* scratch)
{
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    const uint32_t count = input->token_count;
    CheckTestDataPrepared dense = { 0 };
    CheckDataPrepared other = { 0 };
    bool same = check_test_data_prepare_dense(&dense, input, stream, initializers, types, output, scratch) &&
                check_data_prepare(&other, input, stream, initializers, types, !scalar, output, scratch);
    for (uint32_t path = 0; same && path < 2; path += 1)
    {
        const CheckDataPrepared* prepared = path == 0 ? data : &other;
        same = prepared->status == dense.status && prepared->count == dense.count && prepared->reference_count == dense.reference_count &&
               prepared->object_count == dense.object_count && memcmp(&prepared->capacity, &dense.capacity, sizeof(dense.capacity)) == 0 &&
               (dense.count == 0 || (memcmp(prepared->reference_offsets, dense.reference_offsets, (uint64_t)dense.count * sizeof(uint32_t)) == 0 &&
                                     memcmp(prepared->reference_counts, dense.reference_counts, (uint64_t)dense.count * sizeof(uint32_t)) == 0)) &&
               (dense.reference_count == 0 || memcmp(prepared->references, dense.references, (uint64_t)dense.reference_count * sizeof(uint32_t)) == 0);
        for (uint32_t i = 0; same && i < dense.count; i += 1)
        {
            const CheckDataObject* a = prepared->objects + i;
            const CheckDataObject* b = dense.objects + i;
            same = a->type == b->type && a->object == b->object && a->initializer == b->initializer && a->token == b->token &&
                   a->initializer_plan == b->initializer_plan && a->literal_bytes == b->literal_bytes && a->literal_size == b->literal_size &&
                   a->string_pool == b->string_pool && a->mergeable_string == b->mergeable_string && a->alignment_log2 == b->alignment_log2;
        }
        for (uint32_t object = 0; same && object <= count + 1; object += 1)
        {
            same = check_data_latest(prepared, object) == (object <= count ? dense.latest_declarations[object] : BUSTER_CHECK_NONE);
        }
        // The static declarations' keys: each static declaration's object and token, in object and then token order.
        uint64_t* keys = same ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, (uint64_t)count + 1) : 0;
        uint32_t key_count = 0;
        for (uint32_t token = 0; keys != 0 && token < count; token += 1)
        {
            if (input->declarations[token] == token && check_input_bit(input->declaration_bits, token, BUSTER_CHECK_BITS_STATIC))
            {
                keys[key_count++] = (uint64_t)input->objects[token] << 32 | token;
            }
        }
        if (keys != 0)
        {
            qsort(keys, key_count, sizeof(uint64_t), check_test_program_key_order);
        }
        same = keys != 0 && key_count == prepared->declaration_count &&
               (key_count == 0 || memcmp(keys, prepared->declaration_keys, (uint64_t)key_count * sizeof(uint64_t)) == 0);
    }
    uint32_t* token_bodies = same ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 2) : 0;
    same = same && token_bodies != 0 && check_program_bodies_ordered(program);
    if (same)
    {
        memset(token_bodies, 0, ((uint64_t)count + 2) * sizeof(uint32_t));
        for (uint32_t body = 0; body < program->body_count; body += 1)
        {
            const uint32_t close = program->parser->tree.parents[program->bodies[body].opening];
            for (uint32_t token = program->bodies[body].opening; token <= close && token < count; token += 1)
            {
                token_bodies[token] = body + 1;
            }
        }
    }
    for (uint32_t token = 0; same && token <= count + 1; token += 1)
    {
        same = check_program_token_body(program, token) == token_bodies[token];
    }
    // The objects the unit defines, by both paths, against the walk of every declaring token that found them before.
    uint8_t* defined = same ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)program->object_count + 1) : 0;
    uint8_t* expected = defined != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)program->object_count + 1) : 0;
    uint8_t* access = expected != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)program->symbol_count + 1) : 0;
    same = same && access != 0;
    if (same)
    {
        memset(expected, 0, (uint64_t)program->object_count + 1);
        for (uint32_t b = 0; b < program->body_count; b += 1)
        {
            expected[program->bodies[b].object] = 1;
        }
        for (uint32_t token = 0; token < count; token += 1)
        {
            const uint32_t object = input->objects[token];
            if (input->declarations[token] == token && object < program->object_count && input->declaration_kinds[token] == C_BINDING_OBJECT &&
                (check_input_bit(input->declaration_bits, token, BUSTER_CHECK_BITS_DEFINED) || check_input_bit(input->declaration_bits, token, BUSTER_CHECK_BITS_TENTATIVE)))
            {
                expected[object] = 1;
            }
        }
        // GNU's aliases and ifuncs, which the unit defines at their definitions' places (#147); a weakref defines nothing.
        for (uint32_t k = 0; k < program->alias_count; k += 1)
        {
            expected[program->aliases[k].object] |= program->aliases[k].kind != CHECK_PROGRAM_WEAKREF;
        }
        // A cpu_dispatch group's f.ifunc and f, which the unit defines at its resolver (#1447).
        for (uint32_t g = 0; g < program->cpu_group_count; g += 1)
        {
            expected[program->cpu_groups[g].reference] |= program->cpu_groups[g].dispatch != 0;
            expected[program->cpu_groups[g].dispatch] |= program->cpu_groups[g].dispatch != 0;
        }
        // A resolved group's R (or target_clones' f) of multiversioning by target strings, at its resolver, and a
        // target_clones body's own symbol's object, at its body (#1838).
        for (uint32_t g = 0; g < program->target_group_count; g += 1)
        {
            const CheckProgramTargetGroup* group = program->target_groups + g;
            expected[group->reference] |= group->resolved;
            expected[group->body] |= group->body != 0 && expected[group->clones];
            expected[group->clones] = group->clones != 0 ? group->resolved : expected[group->clones];
        }
    }
    if (same)
    {
        memset(defined, 0xa5, (uint64_t)program->object_count + 1);
        check_program_object_access(program, access, defined);
        for (uint32_t s = 0; same && s < program->symbol_count; s += 1)
        {
            const uint32_t object = program->symbol_objects[s];
            same = defined[object] == expected[object];
        }
    }
    uint8_t* needs[2] = { 0 };
    CheckProgram reduced[2] = { *program, *program };
    bool rooms[2] = { false, false };
    CheckProgramTargetEvents events[2] = { { .scratch = scratch, .room = true }, { .scratch = scratch, .room = true } };
    for (uint32_t path = 0; same && path < 2; path += 1)
    {
        needs[path] = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)count + 1);
        reduced[path].object_declarations = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1);
        reduced[path].object_naked = NULL;
        same = needs[path] != NULL && reduced[path].object_declarations != NULL;
        if (same)
        {
            memset(needs[path], 0, (uint64_t)count + 1);
            memset(reduced[path].object_declarations, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
            rooms[path] = path == 0 ? check_program_static_objects(reduced + path, needs[path], output, &events[path]) :
                                     check_program_static_prepared(reduced + path, data, needs[path], output, &events[path]);
        }
    }
    // The target attributes' keys: in token order and in each object's declarations' order, the same once sorted by
    // object, as check_program_targets sorts them.
    uint64_t* sorted[2] = { events[0].keys, events[1].keys };
    for (uint32_t path = 0; same && path < 2 && events[0].count == events[1].count && events[path].count != 0; path += 1)
    {
        uint64_t* temporary = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, events[path].count);
        same = temporary != NULL;
        sorted[path] = same ? c_bindings_sort(events[path].keys, temporary, events[path].count, UINT32_MAX) : NULL;
    }
    same = same && rooms[0] == rooms[1] && memcmp(needs[0], needs[1], (uint64_t)count + 1) == 0 &&
           memcmp(reduced[0].object_declarations, reduced[1].object_declarations, ((uint64_t)count + 1) * sizeof(uint32_t)) == 0 &&
           (reduced[0].object_naked == NULL) == (reduced[1].object_naked == NULL) &&
           (reduced[0].object_naked == NULL || memcmp(reduced[0].object_naked, reduced[1].object_naked, ((uint64_t)count + 1) * sizeof(uint32_t)) == 0) &&
           events[0].room && events[1].room && events[0].count == events[1].count &&
           (events[0].count == 0 || memcmp(sorted[0], sorted[1], events[0].count * sizeof(uint64_t)) == 0);
    arena_pop_to(output, output_mark);
    arena_pop_to(scratch, scratch_mark);
    return same;
}

// The return facts' definition (#2924): check_program_return_node at every node in order, with the unknown declarations
// and the library names check_program_return_facts gives it, a node's operands' facts made before it. Its `count` + 1
// entries end with the sink, zero; null where scratch runs out.
static uint8_t* check_test_return_facts_definition(CheckProgram* program, Arena* scratch)
{
    const uint32_t count = program->input->graph.count;
    uint8_t* facts = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)count + 1) : NULL;
    uint32_t* unknowns = NULL;
    const bool ready = facts != NULL && check_program_return_unknowns(program, scratch, &unknowns);
    const uint8_t* names = ready ? check_program_return_library_names(program->identifiers, scratch) : NULL;
    for (uint32_t node = 0; ready && node < count; node += 1)
    {
        facts[node] = check_program_return_node(program, facts, node, unknowns, names);
    }
    if (ready)
    {
        facts[count] = 0;
    }
    return ready ? facts : NULL;
}

// A program fixture, checked and compiled with plain char signed or unsigned (-funsigned-char), for a target (null for
// x86-64's), against clang's build of it with the same, or of `oracle_source` where it isn't null, which must differ
// from it with `mismatch` (check_test_program_execute_with_alignment); its executable runs where `run_native` says the
// host can run it.
static void check_test_program_source_native_bounds(CheckTest* test, const char* source, const char* name, const char* clang, bool char_unsigned,
                                                    int32_t expected_pools, uint32_t entry_limit, const X86Target* target, bool run_native, uint32_t local_limit,
                                                    const char* oracle_source, bool mismatch, uint32_t standard, bool gnu, const char* standard_flag)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    PredefinesOptions predefines = check_test_program_predefines();
    predefines.char_unsigned = char_unsigned;
    const PPOptions pp_options = {
        .language = { .standard = standard, .gnu = gnu },
        .no_standard_includes = !check_test_program_includes,
        .predefines = predefines,
        .target = target != NULL ? *target : (X86Target){ 0 },
    };
    bool preprocessed = intern_initialize(&identifiers) && pp_initialize(&pp, &pp_options, &identifiers);
    preprocessed = preprocessed && pp_run_source(&pp, name, (const uint8_t*)source, (uint32_t)strlen(source));
    check_test_expect(test, preprocessed, "executable source preprocessing");
    for (uint32_t path = 0; preprocessed && path < 2; path += 1)
    {
        CheckTestTypes types = { 0 };
        // The complete immintrin.h before selection needs more checked-node storage than header-free fixtures.
        const uint64_t plan_room = UINT64_C(1) << (check_test_program_includes ? 28 : 26);
        Arena input_arena = arena_reserve(plan_room);
        Arena output = arena_reserve(plan_room);
        Arena bytes = arena_reserve(UINT64_C(1) << 22);
        Arena scratch = arena_reserve(plan_room);
        const ParseOptions parse_options = { .scalar = path == 0 };
        const CheckOptions options = { 0 };
        bool ready = input_arena.base != 0 && output.base != 0 && bytes.base != 0 && scratch.base != 0 && check_test_types_open(&types) &&
                     parse_initialize(&parser, &parse_options, pp_options.language) && parse_translation_unit(&parser, &pp) && parser.errors == 0;
        CheckInput input = { 0 };
        CheckStream stream = { 0 };
        CheckInitializers initializers = { 0 };
        CheckAsms asms = { 0 };
        CheckDataPrepared data = { 0 };
        CheckProgram program = { 0 };
        ready = ready && check_input(&input, &parser, &pp, &types.table, &input_arena, &scratch) &&
                check_graph_run(&types.table, &input.graph, &input_arena, &scratch, &options, &stream) && stream.report_count == 0 &&
                check_initializers_run(&initializers, &input, &stream, &parser, &types.table, &input_arena, &scratch) &&
                check_asms_run(&asms, &input, &stream, &types.table, &input_arena, &scratch) &&
                check_data_prepare(&data, &input, &stream, &initializers, &types.table, parser.options.scalar, &input_arena, &scratch);
        uint64_t return_flow_work = 0;
        uint64_t region_target_work = 0;
        uint64_t statement_walk_work = 0;
        uint64_t enterable_work = 0;
        const CheckProgramOptions program_options = {
            .initializers = &initializers,
            .asms = &asms,
            .prepared_data = &data,
            .identifiers = &identifiers,
            .initializer_references = data.references,
            .initializer_reference_count = data.reference_count,
            .scalar = path == 0,
            .return_flow_work = &return_flow_work,
            .region_target_work = &region_target_work,
            .statement_walk_work = &statement_walk_work,
            .enterable_work = &enterable_work,
            .target = pp_options.target,
            .predefines = &predefines,
        };
        ready = ready && check_program(&program, &parser, &input, &stream, &types.table, program_options, &input_arena, &scratch);
        if (ready && strcmp(name, "many-aggregate-return-bodies") == 0)
        {
            check_test_expect(test, program.body_count >= 65 && return_flow_work >= input.graph.count &&
                                   return_flow_work <= (uint64_t)input.graph.count * 8 + 1,
                              "aggregate return graph-mark initialization and visited work are linear in the unit graph");
        }
        // #2995: the walks from statement expressions to their break targets and from variable-length arrays to their
        // statements keep their answers, so in every program they pass a token at most once a body, and twice for the
        // arrays' two walks; walks that kept none pass a region's or a bound's ancestors again for each, which the checker
        // sources' deep shapes take far past these bounds.
        if (ready)
        {
            const bool regions = strcmp(name, "quadratic-statement-expressions") == 0;
            const bool statements = strcmp(name, "quadratic-vla-bounds") == 0;
            char walks[320];
            snprintf(walks, sizeof(walks), "%s: %s walks to break targets and statements take %llu and %llu parent steps for %u tokens", name,
                     path == 0 ? "scalar" : "vector", (unsigned long long)region_target_work, (unsigned long long)statement_walk_work, input.token_count);
            check_test_expect(test,
                              region_target_work <= input.token_count && statement_walk_work <= 2 * (uint64_t)input.token_count &&
                                  (!regions || region_target_work != 0) && (!statements || statement_walk_work != 0),
                              walks);
            // check_control_enterable's subtree facts: statements nested 200 deep pass their tokens about once, where
            // walking and scanning each arm again passed tens of times as many.
            if (strcmp(name, "quadratic-switches") == 0)
            {
                snprintf(walks, sizeof(walks), "%s: %s enterable arms take %llu steps for %u tokens", name, path == 0 ? "scalar" : "vector",
                         (unsigned long long)enterable_work, input.token_count);
                check_test_expect(test, enterable_work != 0 && enterable_work <= 2 * (uint64_t)input.token_count, walks);
            }
        }
        char description[256];
        snprintf(description, sizeof(description), "%s: %s source produces checked executable control and static-data plans", name, path == 0 ? "scalar" : "vector");
        check_test_expect(test, ready, description);
        if (ready)
        {
            snprintf(description, sizeof(description), "%s: %s static data and token bodies are what the dense passes made", name, path == 0 ? "scalar" : "vector");
            check_test_expect(test, check_test_program_sparse(&program, &data, &input, &stream, &initializers, &types.table, path == 0, &output, &scratch), description);
            // The return facts' walk visits only the nodes that can have facts (check_program_return_sparse): every
            // node's must be the definition's, check_program_return_node at every node in order.
            const uint64_t facts_mark = scratch.length;
            const uint8_t* exact = check_test_return_facts_definition(&program, &scratch);
            const uint8_t* sparse = check_program_return_facts(&program, &scratch, NULL, false);
            snprintf(description, sizeof(description), "%s: the return facts' walk gives every node the definition's facts", name);
            check_test_expect(test, input.graph.count == 0 || (exact != NULL && sparse != NULL && memcmp(exact, sparse, input.graph.count) == 0), description);
            CheckReturnFacts cached = { 0 };
            bool same = exact != NULL || input.graph.count == 0;
            for (uint32_t node = 0; same && node < input.graph.count; node += 1)
            {
                same = check_program_return_cached(&program, &cached, node) == exact[node];
            }
            for (uint32_t end = input.graph.count; same && end != 0; end -= 1)
            {
                same = check_program_return_cached(&program, &cached, end - 1) == exact[end - 1];
            }
            snprintf(description, sizeof(description), "%s: incremental and cached return facts match every scalar node", name);
            check_test_expect(test, same, description);
            arena_release(&cached.storage);
            arena_pop_to(&scratch, facts_mark);
        }
        // A program that includes the system's headers lowers what its roots reach, as -c does: their other functions
        // (MMX's, whose builtins no row takes) are no part of it. So does one with target_clones copies, which only the
        // selection makes.
        if (ready && (check_test_program_includes || check_test_program_cloned(&program)))
        {
            CheckProgramSelection selected;
            ready = check_program_select(&selected, &program, &data, path == 0, &output, &scratch);
            snprintf(description, sizeof(description), "%s: %s definitions the roots reach are selected", name, path == 0 ? "scalar" : "vector");
            check_test_expect(test, ready, description);
            program = selected.program;
            data = selected.data;
        }
        if (!ready)
        {
            fprintf(stderr, "test-checker: program source parser=%u input=%u resource=%u stream=%u init=%u data=%u program=%u token=%u\n",
                    parser.errors, input.report_count, input.resource, stream.report_count, initializers.status, data.status, program.status, program.token);
        }
        CheckProgramLowered lower[2] = { 0 };
        bool lowered = ready && check_program_lower(lower, &program, true, &output, &scratch) && check_program_lower(lower + 1, &program, false, &output, &scratch);
        snprintf(description, sizeof(description), "%s: both unit ABI batches lower to valid Tier0 functions", name);
        check_test_expect(test, lowered, description);
        if (ready && !lowered)
        {
            fprintf(stderr, "test-checker: program lower status=%u/%u token=%u/%u backend=%u/%u\n", lower[0].status, lower[1].status,
                    lower[0].token, lower[1].token, lower[0].backend_node, lower[1].backend_node);
            fprintf(stderr, "test-checker: lower reason=%s / %s\n", lower[0].reason != NULL ? lower[0].reason : "", lower[1].reason != NULL ? lower[1].reason : "");
            for (uint32_t b = 0; lower[0].functions != NULL && b < lower[0].count; b += 1)
            {
                const Tier0Function* f = lower[0].functions + b;
                for (uint32_t r = 0; r < f->operation_count; r += 1)
                {
                    const Tier0Operation* row = f->operations + r;
                    if (!tier0_operation_valid(row))
                    {
                        fprintf(stderr, "test-checker: invalid row function=%u row=%u operands=%u result=%u/%u\n", b, r, row->operand_count, row->result_class, row->result_bytes);
                        for (uint32_t i = 0; i < row->record_count; i += 1)
                        {
                            const X86Instruction x = row->records[i];
                            fprintf(stderr, "  record %u valid-form=%u reg=%u rm=%u vvvv=%u mode=%u imm=%u\n", x.form, tier0_operation_form_valid(x.form), x.reg, x.rm, x.vvvv, x.mode, x.immediate);
                        }
                    }
                }
                if (f->count != 0 && (!tier0_frame_input_valid(f) || !tier0_calls_valid(f) || !tier0_parameters_valid(f)))
                {
                    fprintf(stderr, "test-checker: invalid metadata function=%u frame=%u calls=%u parameters=%u\n", b, tier0_frame_input_valid(f), tier0_calls_valid(f), tier0_parameters_valid(f));
                    for (uint32_t c = 0; c < f->call_count; c += 1)
                    {
                        if (!tier0_call_valid(f, c))
                        {
                            const AbiReturn r = f->calls[c].result;
                            fprintf(stderr, "  call %u stack=%u object=%u result=%u parts=%u registers=%u,%u,%u,%u\n", c, f->calls[c].stack_bytes, f->call_result_objects != NULL ? f->call_result_objects[c] : 0, r.kind, r.part_bytes, r.registers[0], r.registers[1], r.registers[2], r.registers[3]);
                            for (uint32_t a = f->call_first[c]; a < f->call_first[c] + f->call_argument_counts[c]; a += 1)
                            {
                                const AbiLocation l = f->argument_locations[a];
                                fprintf(stderr, "    argument %u size=%u class=%u kind=%u offset=%u flags=%u regs=%u,%u\n", a, f->argument_sizes != NULL ? f->argument_sizes[a] : 0, f->argument_classes[a], l.kind, l.stack_offset, l.flags, l.registers[0], l.registers[1]);
                            }
                        }
                    }
                }
            }
        }
        if (lowered)
        {
            if (strcmp(name, "library-folds-beside-overloads") == 0 || strcmp(name, "library-folds-selected-ordinary") == 0)
            {
                const uint32_t expected = strcmp(name, "library-folds-selected-ordinary") == 0 ? 1 : 0;
                for (uint32_t p = 0; p < 2; p += 1)
                {
                    uint32_t calls = 0;
                    for (uint32_t b = 0; b < lower[p].count; b += 1)
                    {
                        const Tier0Function* function = lower[p].functions + b;
                        for (uint32_t e = 0; e < function->count; e += 1)
                        {
                            const uint32_t kind = function->kinds[e];
                            calls += kind == BUSTER_TYPED_CALL || kind == BUSTER_TYPED_TAIL_CALL;
                        }
                    }
                    check_test_expect(test, calls == expected, "ordinary library folds leave only the true overload's call on both paths");
                }
            }
            if (strcmp(name, "bits-classic-boundary-values") == 0 || strcmp(name, "bits-generic-boundary-values") == 0)
            {
                // The classic and generic/C23 bit builtins' rows (#1374/#1652): each count's form the target's, on both paths.
                const uint64_t features = target != NULL ? target->features : 0;
                static const uint16_t forms[][2] = {
                    { X86_FORM_TZCNT_R32_RM32, X86_FORM_TZCNT_R64_RM64 }, { X86_FORM_BSF_R32_RM32, X86_FORM_BSF_R64_RM64 },
                    { X86_FORM_LZCNT_R32_RM32, X86_FORM_LZCNT_R64_RM64 }, { X86_FORM_BSR_R32_RM32, X86_FORM_BSR_R64_RM64 },
                    { X86_FORM_POPCNT_R32_RM32, X86_FORM_POPCNT_R64_RM64 }, { X86_FORM_BSWAP_R32, X86_FORM_BSWAP_R64 },
                };
                const bool bmi = (features & BUSTER_X86_FEATURE(BMI)) != 0;
                const bool lzcnt = (features & BUSTER_X86_FEATURE(LZCNT)) != 0;
                const bool popcnt = (features & BUSTER_X86_FEATURE(POPCNT)) != 0;
                const bool expected[BUSTER_ARRAY_LENGTH(forms)] = { bmi, !bmi, lzcnt, !lzcnt, popcnt, true };
                for (uint32_t p = 0; p < 2; p += 1)
                {
                    uint32_t found[BUSTER_ARRAY_LENGTH(forms)][2] = { { 0 } };
                    for (uint32_t b = 0; b < lower[p].count; b += 1)
                    {
                        const Tier0Function* function = lower[p].functions + b;
                        for (uint32_t r = 0; r < function->operation_count; r += 1)
                        {
                            for (uint32_t k = 0; k < function->operations[r].record_count; k += 1)
                            {
                                for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(forms); f += 1)
                                {
                                    found[f][0] += function->operations[r].records[k].form == forms[f][0];
                                    found[f][1] += function->operations[r].records[k].form == forms[f][1];
                                }
                            }
                        }
                    }
                    bool exact = true;
                    for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(forms); f += 1)
                    {
                        exact &= expected[f] ? found[f][0] != 0 && found[f][1] != 0 : found[f][0] == 0 && found[f][1] == 0;
                    }
                    check_test_expect(test, exact, "the bit builtins' rows are the target's forms, of both widths, on both paths");
                }
            }
            if (strcmp(name, "overflow-instruction-rows") == 0)
            {
                // The signed longs' overflow builtins' rows (#1385, check_lower_overflow_row): a sum's add, a product's imul and a
                // difference's cmp, each with seto, on both paths, and no 32-bit form, which ints' recipes don't take.
                for (uint32_t p = 0; p < 2; p += 1)
                {
                    uint32_t found[3] = { 0 };
                    uint32_t narrow = 0;
                    for (uint32_t b = 0; b < lower[p].count; b += 1)
                    {
                        const Tier0Function* function = lower[p].functions + b;
                        for (uint32_t r = 0; r < function->operation_count; r += 1)
                        {
                            const Tier0Operation* row = function->operations + r;
                            const bool flag = row->record_count >= 3 && row->records[row->record_count - 2].form == X86_FORM_SETO_RM8;
                            for (uint32_t k = 0; flag && k < row->record_count; k += 1)
                            {
                                const uint32_t form = row->records[k].form;
                                found[0] += form == X86_FORM_ADD_R64_RM64;
                                found[1] += form == X86_FORM_IMUL_R64_RM64;
                                found[2] += form == X86_FORM_CMP_R64_RM64;
                                narrow += form == X86_FORM_ADD_R32_RM32 || form == X86_FORM_IMUL_R32_RM32 || form == X86_FORM_CMP_R32_RM32;
                            }
                        }
                    }
                    check_test_expect(test, found[0] != 0 && found[1] != 0 && found[2] != 0 && narrow == 0,
                                      "the signed longs' overflow builtins are add, imul and cmp rows with seto, on both paths");
                }
            }
            // #1386's power-of-two fixtures divide nowhere: their quotients and remainders are shifts and masks, the
            // signed one's with DUPs (its bias sequence), and its pointer differences shifts.
            const bool signed_powers = strcmp(name, "signed-power-two-divmod") == 0;
            if (signed_powers || strcmp(name, "unsigned-power-two-divmod") == 0)
            {
                uint32_t divides = 0;
                uint32_t shifts = 0;
                uint32_t masks = 0;
                uint32_t dups = 0;
                for (uint32_t p = 0; p < 2; p += 1)
                {
                    for (uint32_t b = 0; b < lower[p].count; b += 1)
                    {
                        const Tier0Function* function = lower[p].functions + b;
                        for (uint32_t e = 0; e < function->count; e += 1)
                        {
                            divides += function->kinds[e] == BUSTER_TYPED_DIV || function->kinds[e] == BUSTER_TYPED_MOD;
                            shifts += function->kinds[e] == BUSTER_TYPED_SHR;
                            masks += function->kinds[e] == BUSTER_TYPED_AND;
                            dups += function->kinds[e] == BUSTER_TYPED_DUP;
                        }
                    }
                }
                check_test_expect(test, divides == 0 && shifts != 0 && masks != 0 && (!signed_powers || dups != 0),
                                  signed_powers ? "signed constant powers of two and pointer differences lower without division on both paths"
                                                : "unsigned constant powers of two lower to shifts/masks on both paths without division");
                CheckProgramLowered templated = { 0 };
                bool same = check_program_lower_templates(&templated, &program, true, &output, &scratch) && templated.count == lower[0].count &&
                            templated.template_mismatches == 0;
                for (uint32_t b = 0; same && b < templated.count; b += 1)
                {
                    same = check_test_program_function(templated.functions + b, lower[0].functions + b);
                }
                check_test_expect(test, same, "experimental node templates retain the exact power-of-two task stream");
            }
            // #2678's fixtures: their constants' conversions folded, on both lowerings and by the node templates.
            if (strcmp(name, "constant-conversions") == 0 || strcmp(name, "constant-conversion-code") == 0 || strcmp(name, "constant-conversion-controls") == 0)
            {
                check_test_constant_conversion_lowered(test, name, lower, &program, &output, &scratch);
            }
            // #2883's fixtures: their conditions' `&&` and `||` written as branches, on both lowerings and by the node templates.
            if (strncmp(name, "branch-condition-", sizeof("branch-condition-") - 1) == 0)
            {
                check_test_branch_condition_lowered(test, name, lower, &program, &output, &scratch);
            }
            for (uint32_t p = 0; p < 2; p += 1)
            {
                check_test_expect(test, expected_pools < 0 || lower[p].initializers.count == (uint32_t)expected_pools,
                                  "automatic initializer pooling matches the independently expected threshold and constant eligibility");
                for (uint32_t b = 0; entry_limit != 0 && b < program.body_count; b += 1)
                {
                    check_test_expect(test, lower[p].functions[b].count <= entry_limit,
                                      "initializer code stays within a fixed typed-entry bound as payload grows");
                }
            }
            check_test_expect(test, lower[0].initializers.count == lower[1].initializers.count,
                              "automatic initializer pool counts agree across lowering paths");
            for (uint32_t p = 0; p < lower[0].initializers.count && p < lower[1].initializers.count; p += 1)
            {
                const CheckLowerInitPool* a = lower[0].initializers.items + p;
                const CheckLowerInitPool* b = lower[1].initializers.items + p;
                check_test_expect(test, a->size == b->size && a->part == b->part && memcmp(a->bytes, b->bytes, a->size) == 0,
                                  "automatic initializer byte images agree across lowering paths");
            }
            check_test_expect(test, lower[0].errno_symbol == lower[1].errno_symbol && lower[0].errno_pointer_type == lower[1].errno_pointer_type &&
                                   lower[0].runtime_symbol_count == lower[1].runtime_symbol_count,
                              "runtime accessor identity and genuine pointer ABI agree across unit batches");
            if (lower[0].errno_symbol != 0)
            {
                const bool fresh = lower[0].errno_symbol > program.symbol_count;
                const uint64_t first_runtime = (uint64_t)program.symbol_count + lower[0].helpers.count + lower[0].initializers.count + 1;
                check_test_expect(test, lower[0].runtime_symbols[CHECK_LOWER_RUNTIME_ERRNO] == lower[0].errno_symbol &&
                                       type_kind(program.types, lower[0].errno_pointer_type) == BUSTER_TYPE_KIND_POINTER &&
                                       type_element(program.types, lower[0].errno_pointer_type) == BUSTER_TYPE_INT &&
                                       (fresh ? lower[0].errno_symbol >= first_runtime && lower[0].errno_symbol < first_runtime + lower[0].runtime_symbol_count :
                                                lower[0].errno_symbol <= program.symbol_count),
                                  "errno accessor uses one reserved runtime symbol or an existing compatible global");
            }
        }
        for (uint32_t b = 0; lowered && b < program.body_count; b += 1)
        {
            check_test_expect(test, local_limit == 0 || (lower[0].functions[b].local_count <= local_limit && lower[1].functions[b].local_count <= local_limit),
                              "builtin scratch locals stay bounded across repeated recipes");
            check_test_expect(test, check_test_program_function(lower[0].functions + b, lower[1].functions + b),
                              "complete typed function and ABI locations agree across unit batches");
        }
        // The lowering's fast paths (the plain expressions, the batched fragments and the types' templates) against the
        // general expansion, which the source rows' lowering (-g's) takes alone: every function's entries the same. Its
        // arenas are reservations of their own, large enough for the largest vector fixtures' expansion.
        CheckProgramLowered expanded = { 0 };
        Arena expanded_output = arena_reserve(UINT64_C(1) << 34);
        Arena expanded_scratch = arena_reserve(UINT64_C(1) << 34);
        const bool expansion = lowered && expanded_output.base != NULL && expanded_scratch.base != NULL &&
                               check_program_lower_sources(&expanded, &program, path == 0, &expanded_output, &expanded_scratch);
        snprintf(description, sizeof(description), "%s: the source rows' lowering takes every body", name);
        check_test_expect(test, !lowered || (expansion && expanded.count == lower[1].count), description);
        if (lowered && !expansion)
        {
            fprintf(stderr, "test-checker: %s: the source rows' lowering status=%u token=%u reason=%s\n", name, expanded.status, expanded.token,
                    expanded.reason != NULL ? expanded.reason : "");
        }
        check_test_program_expansions += lowered && expansion && path == 1;
        for (uint32_t b = 0; expansion && expanded.count == lower[1].count && b < program.body_count; b += 1)
        {
            snprintf(description, sizeof(description), "%s: function %u's entries by the fast paths are the general expansion's", name, b);
            check_test_expect(test, check_test_program_function(expanded.functions + b, lower[1].functions + b), description);
        }
        arena_release(&expanded_output);
        arena_release(&expanded_scratch);
        CheckProgramCode code = { 0 };
        CheckDataResult emission = { 0 };
        // Every atom in place, the aliases' and ifuncs' symbols at their definitions (#147; -c's check_program_sections).
        const bool emitted = lowered && check_program_emit(&code, &program, &parser, &identifiers, data.capacity, path == 0, &output, &bytes, &scratch, NULL, NULL, false, true) &&
                             check_data_publish(&data, &types.table, &stream, program.object_symbols, program.object_count, &code.link, 0, &bytes, &scratch, &emission) &&
                             check_program_define_aliases(&code, &program);
        snprintf(description, sizeof(description), "%s: shared code/data Link emits through real ABI/Tier0/encoder consumers", name);
        check_test_expect(test, emitted, description);
        if (lowered && !emitted)
        {
            fprintf(stderr, "test-checker: program emission status=%u token=%u backend=%u node=%u data=%u\n", code.status, code.token, code.backend_error, code.backend_node, emission.status);
        }
        if (emitted)
        {
            check_test_expect(test, entry_limit == 0 || code.code_bytes <= 2048,
                              "initializer native code stays bounded independently of payload size");
            // (A unit's __builtin_dump_struct strings and __builtin_clear_padding tables are pools too, which no memcpy reads.)
            bool tables = false;
            for (uint32_t k = 0; k < code.lowered.initializers.padding_count; k += 1)
            {
                tables |= code.lowered.initializers.paddings[k].pool != BUSTER_CHECK_NONE;
            }
            if (code.lowered.initializers.count != 0 && code.lowered.initializers.dump_count == 0 && !tables)
            {
                const uint64_t access_mark = scratch.length;
                uint8_t* defined = NULL;
                const bool rebuilt = check_program_access(&code, &program, &output, &scratch, &defined);
                const SymbolId copy = code.lowered.runtime_symbols[CHECK_LOWER_RUNTIME_MEMCPY];
                const Tier0Function* first = code.lowered.functions;
                check_test_expect(test, rebuilt && program.body_count != 0 && copy != 0 && copy < first->symbol_count &&
                                       first->symbol_access[copy] == BUSTER_TIER0_ACCESS_GOT &&
                                       first->symbol_access[code.lowered.initializers.symbol_base] == BUSTER_TIER0_ACCESS_DIRECT,
                                  "backend consumers retain final pooled-data and external memcpy access after runtime symbol numbering");
                arena_pop_to(&scratch, access_mark);
            }
            const bool alignment_oracle = strcmp(name, "hidden-result-direct-temporary-alignment") == 0;
            if (run_native && mismatch)
            {
                snprintf(description, sizeof(description), "%s: generated executable differs from %s's build of its oracle on some input", name, clang);
            }
            else if (run_native)
            {
                snprintf(description, sizeof(description), "%s: generated executable agrees with %s on all repeated inputs", name,
                         alignment_oracle ? "the independent alignment expectation" : clang);
            }
            else
            {
                snprintf(description, sizeof(description), "%s: generated object and oracle compile and link; native execution is unavailable on this CPU", name);
            }
            check_test_expect(test,
                              check_test_program_execute_with_alignment(&code.link, source, oracle_source, mismatch, clang, char_unsigned, alignment_oracle,
                                                                        run_native, standard_flag, &scratch),
                              description);
        }
        check_test_expect(test, scratch.length == 0, "program preparation and all consumers restore scratch lifetime");
        parse_release(&parser);
        check_test_types_close(&types);
        arena_release(&scratch);
        arena_release(&bytes);
        arena_release(&output);
        arena_release(&input_arena);
    }
    pp_release(&pp);
    intern_release(&identifiers);
}

static void check_test_program_source_native_limits(CheckTest* test, const char* source, const char* name, const char* clang, bool char_unsigned,
                                                    int32_t expected_pools, uint32_t entry_limit, const X86Target* target, bool run_native)
{
    check_test_program_source_native_bounds(test, source, name, clang, char_unsigned, expected_pools, entry_limit, target, run_native, 0, NULL, false,
                                            2017, true, "-std=gnu17");
}

static void check_test_program_source_bounds(CheckTest* test, const char* source, const char* name, const char* clang, bool char_unsigned,
                                            int32_t expected_pools, uint32_t entry_limit, uint32_t local_limit)
{
    check_test_program_source_native_bounds(test, source, name, clang, char_unsigned, expected_pools, entry_limit, NULL, true, local_limit, NULL, false, 2017,
                                            true, "-std=gnu17");
}

static void check_test_program_source_limits(CheckTest* test, const char* source, const char* name, const char* clang, bool char_unsigned,
                                             int32_t expected_pools, uint32_t entry_limit)
{
    check_test_program_source_native_limits(test, source, name, clang, char_unsigned, expected_pools, entry_limit, NULL, true);
}

static void check_test_program_source_native(CheckTest* test, const char* source, const char* name, const char* clang, bool run_native)
{
    check_test_program_source_native_limits(test, source, name, clang, false, -1, 0, NULL, run_native);
}

static void check_test_program_source_char(CheckTest* test, const char* source, const char* name, const char* clang, bool char_unsigned)
{
    check_test_program_source_limits(test, source, name, clang, char_unsigned, -1, 0);
}

// A program fixture with plain char signed, x86-64's.
static void check_test_program_source(CheckTest* test, const char* source, const char* name, const char* clang)
{
    check_test_program_source_char(test, source, name, clang, false);
}

// Each catalog is one contiguous group at the original call site. Its caller still selects the feature,
// shard and mode. The native harness uses the case name for its per-program structural checks.
static void check_test_program_suite(CheckTest* test, const char* suite, const char* clang)
{
    CheckTestFixtures catalog;
    const bool loaded = check_test_fixtures_load(&catalog, suite);
    char description[768];
    snprintf(description, sizeof(description), "%s: %s", suite, loaded ? "native programs loaded" : catalog.problem);
    check_test_expect(test, loaded, description);
    for (uint32_t i = 0; loaded && i < catalog.count; i += 1)
    {
        const CheckSourceFixture* fixture = catalog.cases + i;
        const bool valid = check_test_fixture_word(fixture->name, 128) && !fixture->rejected && !fixture->unsupported && !fixture->extension &&
                           !fixture->undecided && fixture->standard == 0 && fixture->capacity == NULL && fixture->overload_tables == 0;
        if (valid)
        {
            check_test_program_source_char(test, fixture->source, fixture->name, clang, fixture->char_unsigned);
        }
        else
        {
            snprintf(description, sizeof(description), "%s: invalid native program name or flags for '%s'", suite, fixture->name);
            check_test_expect(test, false, description);
        }
    }
    arena_release(&catalog.storage);
}

// A checker source (tests/checker/sources/NAME.c) as a program fixture, against `clang`'s build of it: its `tested` runs.
static void check_test_program_checker_source(CheckTest* test, const char* name, const char* clang)
{
    CheckTestFixtures fixture = { .storage = arena_reserve(UINT64_C(1) << 20) };
    char path[PATH_MAX + 128];
    snprintf(path, sizeof(path), "%s/checker/sources/%s.c", check_test_fixture_root, name);
    uint64_t length = 0;
    const char* source = fixture.storage.base != NULL ? check_test_fixture_read(&fixture, path, &length) : NULL;
    char description[PATH_MAX + 160];
    snprintf(description, sizeof(description), "%s: the checker source %s is read", name, path);
    check_test_expect(test, source != NULL && length != 0, description);
    if (source != NULL && length != 0)
    {
        check_test_program_source(test, source, name, clang);
    }
    arena_release(&fixture.storage);
}

// A program fixture in another language mode, with clang reading the same source and mode as the oracle.
static void check_test_program_source_standard(CheckTest* test, const char* source, const char* name, const char* clang, uint32_t standard, bool gnu,
                                               const char* standard_flag)
{
    check_test_program_source_native_bounds(test, source, name, clang, false, -1, 0, NULL, true, 0, NULL, false, standard, gnu, standard_flag);
}

// A program fixture with plain char signed, x86-64's, against clang's build of another source of the same names, a
// model of what the program computes or a mutant of it: the program must agree with it, or with `mismatch` differ from
// it on some input.
static void check_test_program_against(CheckTest* test, const char* source, const char* oracle_source, bool mismatch, const char* name, const char* clang)
{
    check_test_program_source_native_bounds(test, source, name, clang, false, -1, 0, NULL, true, 0, oracle_source, mismatch, 2017, true, "-std=gnu17");
}

static void check_test_program_initializer_facts(CheckTest* test)
{
    enum
    {
        COUNT = 32,
        SIZE = 131,
    };
    CheckTestTypes types = { 0 };
    Arena output = arena_reserve(65536);
    Arena scratch = arena_reserve(65536);
    bool ready = check_test_types_open(&types) && output.base != NULL && scratch.base != NULL;
    for (uint32_t i = 0; ready && i < 256; i += 1)
    {
        ready = type_tag(&types.table, BUSTER_TYPE_KIND_STRUCT, i + 100) != 0;
    }
    const TypeId array = ready ? type_array(&types.table, BUSTER_TYPE_CHAR, SIZE) : 0;
    const TypeId record = ready ? type_tag(&types.table, BUSTER_TYPE_KIND_STRUCT, 1) : 0;
    const TypeMember member = { .name = 1, .type = array, .bit_width = BUSTER_TYPE_NOT_BIT_FIELD };
    ready = ready && record != 0 && type_complete_record(&types.table, record, &member, 1, (TypeRecordAttributes){ 0 });
    uint8_t* facts = ready ? BUSTER_ARENA_PUSH_ARRAY(&output, uint8_t, types.table.types.count) : NULL;
    ready = ready && facts != NULL;
    if (ready)
    {
        memset(facts, 0, types.table.types.count);
        ready = check_lower_copy_type_facts(&types.table, types.table.types.count, facts, &scratch);
    }
    check_test_expect(test, ready, "initializer pool containment facts are prepared once for a large frozen type table");
    if (ready)
    {
        uint8_t literal[SIZE];
        memset(literal, 1, sizeof(literal));
        CheckInitPart part = { .kind = CHECK_INIT_BYTES, .type = array, .bytes = literal, .byte_count = SIZE };
        CheckInitializer plans[COUNT];
        CheckControl controls[COUNT];
        uint32_t declarations[COUNT];
        uint8_t flags[COUNT] = { 0 };
        for (uint32_t i = 0; i < COUNT; i += 1)
        {
            plans[i] = (CheckInitializer){ .type = record, .parts = &part, .count = 1, .object_size = SIZE, .status = CHECK_INIT_OK };
            controls[i] = (CheckControl){ .kind = CHECK_CONTROL_INITIALIZE, .target = i };
            declarations[i] = i;
        }
        const CheckInput input = { .token_count = COUNT, .declaration_bits = flags };
        const CheckInitializers initializers = { .plans = plans, .count = COUNT, .declaration_tokens = declarations };
        CheckProgramBody bodies[3] = { { .first = 0, .count = COUNT } };
        CheckProgram program = { .types = &types.table, .input = &input, .initializers = &initializers,
                                 .controls = controls, .control_count = COUNT, .bodies = bodies, .body_count = 1 };
        scratch.capacity = 256;
        CheckLowerInitializers pools = { 0 };
        bool pooled = check_lower_init_pools(&pools, &program, facts, types.table.types.count, &output, &scratch) && pools.count == COUNT;
        for (uint32_t i = 0; pooled && i < COUNT; i += 1)
        {
            pooled = pools.items[i].part == BUSTER_CHECK_NONE && pools.items[i].size == SIZE &&
                     memcmp(pools.items[i].bytes, literal, SIZE) == 0;
        }
        check_test_expect(test, pooled && scratch.length == 0,
                          "many dense record pools need only plan-sized scratch, never a type-sized traversal per initializer");
        bodies[0] = (CheckProgramBody){ .first = 0, .count = 10 };
        bodies[1] = (CheckProgramBody){ .first = 20, .count = 12 };
        bodies[2] = bodies[0]; // A target-clone copy shares its source body's controls.
        program.body_count = 3;
        pools = (CheckLowerInitializers){ 0 };
        pooled = check_lower_init_pools(&pools, &program, facts, types.table.types.count, &output, &scratch) && pools.count == 22;
        for (uint32_t i = 0; pooled && i < COUNT; i += 1)
        {
            pooled = pools.first[i + 1] - pools.first[i] == (i < 10 || i >= 20);
        }
        check_test_expect(test, pooled && scratch.length == 0,
                          "initializer pools follow selected body controls, including shared clone controls");
        const uint32_t listed[] = { 10, 11, 11, COUNT };
        program.initialize_list = listed;
        program.initialize_list_count = 4;
        pools = (CheckLowerInitializers){ 0 };
        pooled = check_lower_init_pools(&pools, &program, facts, types.table.types.count, &output, &scratch) && pools.count == 2;
        for (uint32_t i = 0; pooled && i < COUNT; i += 1)
        {
            pooled = pools.first[i + 1] - pools.first[i] == (i == 10 || i == 11);
        }
        check_test_expect(test, pooled && scratch.length == 0,
                          "a provisional initializer list replaces body controls and deduplicates its plans");
        scratch.capacity = 65536;
    }
    arena_release(&scratch);
    arena_release(&output);
    check_test_types_close(&types);
}

static void check_test_program_clears(CheckTest* test, const char* clang);
static void check_test_program_named_results(CheckTest* test, const char* clang);
static void check_test_program_forwarded_results(CheckTest* test, const char* clang);

static void check_test_program_initializers(CheckTest* test, const char* clang)
{
    check_test_program_initializer_facts(test);
    check_test_program_clears(test, clang);
    check_test_program_named_results(test, clang);
    check_test_program_forwarded_results(test, clang);
    check_test_program_suite(test, "checker/programs/program-initializers", clang);

    const uint32_t sizes[] = { 127, 128, 129, 135, 136, 137, 257, 512, 2049, 8193, 32769 };
    char* source = malloc(40000);
    check_test_expect(test, source != NULL, "large automatic initializer fixture allocation");
    for (uint32_t k = 0; source != NULL && k < BUSTER_ARRAY_LENGTH(sizes); k += 1)
    {
        const uint32_t size = sizes[k];
        uint32_t length = (uint32_t)snprintf(source, 40000, "int tested(int n){char a[]=\"");
        for (uint32_t i = 0; i + 1 < size; i += 1)
        {
            source[length++] = (char)('a' + i % 26);
        }
        snprintf(source + length, 40000 - length,
                 "\";int bad=0;for(int i=0;i<%u;i++)bad|=a[i]!=('a'+i%%26);bad|=a[%u];a[(unsigned)n%%%u]='!';return bad?123456:n;}",
                 size - 1, size - 1, size - 1);
        char name[64];
        snprintf(name, sizeof(name), "automatic-string-%u", size);
        check_test_program_source_limits(test, source, name, clang, false, size > BUSTER_CHECK_LOWER_INIT_UNROLLED, 320);
    }
    if (source != NULL)
    {
        uint32_t length = (uint32_t)snprintf(source, 40000, "int tested(int n){_Alignas(64) int a[1025]={");
        for (uint32_t i = 0; i < 1025; i += 1)
        {
            length += (uint32_t)snprintf(source + length, 40000 - length, "%s%d", i != 0 ? "," : "", (int)i * 7 - 2000);
        }
        snprintf(source + length, 40000 - length,
                 "};int bad=(unsigned long)a%%64;for(int i=0;i<1025;i++)bad|=a[i]!=i*7-2000;a[(unsigned)n%%1025]=n;return bad?123456:n;}");
        check_test_program_source_limits(test, source, "automatic-constant-list", clang, false, 1, 320);
    }
    free(source);
    const char* letters = "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz";
    char text[4096];
    snprintf(text, sizeof(text),
             "int tested(int n){char a[130]=\"%s\";char b[257]=\"%s\\0XY\";"
             "int bad=0;for(int i=0;i<130;i++)bad|=a[i]!=('a'+i%%26);for(int i=0;i<130;i++)bad|=b[i]!=a[i];"
             "bad|=b[130]|(b[131]!='X')|(b[132]!='Y');for(int i=133;i<257;i++)bad|=b[i];return bad?123456:n;}", letters, letters);
    check_test_program_source_limits(test, text, "automatic-truncated-embedded-nul-tail", clang, false, 2, 320);
    snprintf(text, sizeof(text),
             "int tested(int n){int a[257]=L\"%s\\0XY\\u1234\";unsigned short b[257]=u\"%s\\0XY\\u1234\";"
             "int bad=0;for(int i=0;i<130;i++)bad|=(a[i]!=('a'+i%%26))|(b[i]!=a[i]);"
             "for(int i=130;i<257;i++)bad|=a[i]!=b[i];bad|=a[130]|(a[131]!='X')|(a[132]!='Y');"
             "bad|=a[133]!=0x1234;for(int i=134;i<257;i++)bad|=a[i];return bad?123456:n;}", letters, letters);
    check_test_program_source_limits(test, text, "automatic-wide-embedded-nul-tail", clang, false, 2, 320);
    snprintf(text, sizeof(text),
             "struct __attribute__((packed)) S{char x;char a[131];int y;};int tested(int n){int k=n;struct S s={(char)k++,\"%s\",k++};"
             "int bad=(s.x!=(char)n)|(s.y!=n+1)|(k!=n+2);for(int i=0;i<130;i++)bad|=s.a[i]!=('a'+i%%26);"
             "bad|=s.a[130];return bad?123456:n;}", letters);
    check_test_program_source_limits(test, text, "automatic-mixed-runtime-string", clang, false, 1, 320);
    snprintf(text, sizeof(text),
             "int tested(int n){char a[]=\"%s\";double x=__builtin_sqrt((double)(n&3)+1.0);return a[(unsigned)n%%130]+(int)x;}", letters);
    check_test_program_source_limits(test, text, "automatic-pool-synthesized-errno-symbol", clang, false, 1, 320);
    snprintf(text, sizeof(text),
             "int*__errno_location(void);int tested(int n){char a[]=\"%s\";*__errno_location()=7;"
             "double x=__builtin_sqrt((double)(n&3)+1.0);return a[(unsigned)n%%130]+(int)x+*__errno_location();}", letters);
    check_test_program_source_limits(test, text, "automatic-pool-existing-errno-symbol", clang, false, 1, 320);
    snprintf(text, sizeof(text),
             "struct S{char bytes[513];};int tested(int n){struct S a={\"%s\"};struct S b;b=a;"
             "double x=__builtin_sqrt((double)(n&3)+1.0);return b.bytes[(unsigned)n%%130]+(int)x+1000*(a.bytes[512]!=0||b.bytes[512]!=0);}", letters);
    check_test_program_source_limits(test, text, "automatic-pool-copy-errno-and-zero-tail", clang, false, 1, 320);
    check_test_program_source_limits(test,
        "int tested(int n){int a[257]={[0 ... 256]=3,[31]=9,[256]=17};int bad=0;"
        "for(int i=0;i<257;i++)bad|=a[i]!=(i==31?9:i==256?17:3);return bad?123456:n;}",
        "automatic-designated-overrides", clang, false, 1, 320);
    check_test_program_source_limits(test,
        "int tested(int n){int* a=(int[65]){[0 ... 64]=7};a[0]=n;return a[0]==n&&a[64]==7?n:123456;}",
        "automatic-compound-literal-image", clang, false, 1, 320);
    check_test_program_source_limits(test,
        "int tested(int n){return sizeof((int[65]){[0 ... 64]=7})==260?n:123456;}",
        "automatic-unevaluated-compound-no-image", clang, false, 0, 320);
    check_test_program_source_limits(test,
        "int tested(int n){int k=n;int a[40]={k++,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,k++};"
        "return a[0]==n&&a[39]==n+1&&k==n+2?n:123456;}", "automatic-runtime-list", clang, false, 0, 0);
    check_test_program_source_limits(test,
        "int tested(int n){volatile int a[40]={[0 ... 39]=7};int bad=0;for(int i=0;i<40;i++)bad|=a[i]!=7;return bad?123456:n;}",
        "automatic-volatile-list", clang, false, 0, 0);
    check_test_program_source_limits(test,
        "struct S{int a[65];volatile int flag;};int tested(int n){struct S s={{[0 ... 64]=7}};"
        "return s.a[0]==7&&s.a[64]==7&&s.flag==0?n:123456;}", "automatic-omitted-volatile-member", clang, false, 0, 0);
    check_test_program_source_limits(test,
        "int tested(int n){int a[8193]={[0 ... 32]=7,[8192]=9};return a[0]==7&&a[32]==7&&a[8191]==0&&a[8192]==9?n:123456;}",
        "automatic-sparse-no-large-image", clang, false, 0, 0);
}

// check_lower_prepare's local_map (#877): every entry none before any body is lowered, the last object's included, and
// read back through check_lower_local_of, which every read of a body's locals goes through.
static bool check_test_program_locals(const CheckProgram* program, const CheckLowerShared* shared)
{
    const CheckLowerWriter writer = { .local_map = shared->local_map };
    bool same = true;
    for (uint32_t object = 0; object <= program->input->token_count; object += 1)
    {
        same &= shared->local_map[object] == BUSTER_CHECK_NONE && check_lower_local_of(&writer, object) == BUSTER_CHECK_NONE;
    }
    return same;
}

// Selection keeps source order and emits target clones immediately after their original.
static void check_test_lower_opening_owner(CheckTest* test)
{
    CheckProgramBody bodies[] = { { .opening = 0 }, { .opening = 11 }, { .opening = 11 }, { .opening = 32 } };
    CheckProgram program = { .bodies = bodies, .body_count = BUSTER_ARRAY_LENGTH(bodies) };
    const uint32_t openings[] = { 32, 7, 11, 11, 0, 32, 33, 1 };
    const uint32_t expected[] = { 3, BUSTER_CHECK_NONE, 1, 1, 0, 3, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE };
    CheckProgramBodyCursor cursor = { 0 };
    bool same = true;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(openings); i += 1)
    {
        same &= check_lower_opening_owner(&program, openings[i], &cursor) == expected[i];
    }
    program.body_count = 0;
    cursor = (CheckProgramBodyCursor){ 0 };
    check_test_expect(test, same && check_lower_opening_owner(&program, 0, &cursor) == BUSTER_CHECK_NONE,
                      "local ownership finds the original selected body and skips omitted openings");
}

// Every type's class and facts by their definition, the rules (check_lower_type_class and check_lower_type_facts), one
// type at a time, `count` of each from index 0: what check_lower_classify_types' columns are checked against.
static void check_test_classify_types_definition(const TypeTable* types, uint32_t count, uint8_t* classes, uint8_t* facts)
{
    for (uint32_t index = 0; index < count; index += 1)
    {
        const TypeId type = (TypeId)index << BUSTER_TYPE_INDEX_SHIFT;
        classes[index] = check_lower_type_class(types, type);
        facts[index] = check_lower_type_facts(types, type);
    }
}

// The named result's and the forwarded calls' scans of a body's controls, 16 at a time (check_lower_named_result,
// check_lower_forwards), with the deciding control at every lane: functions of 0 to 20 expression statements (one
// EVALUATE control each) before it. Each fN builds `a` in its result's storage unless a return of `b` (lane N's RETURN)
// forbids it; each gN passes its result's pointer on to `make` unless a return kept before a cleanup (its RESULT)
// forbids it. clang's build of the same source is the oracle.
static void check_test_program_result_lanes(CheckTest* test, const char* clang)
{
    static char source[65536];
    int length = snprintf(source, sizeof(source),
                          "struct S{long v[4];};static void clean(long*p){*p=0;}\n"
                          "static __attribute__((noinline)) struct S make(long n){struct S s={{n,n*3,n+7,n^5}};return s;}\n");
    for (uint32_t k = 0; length > 0 && (size_t)length < sizeof(source) && k <= 20; k += 1)
    {
        length += snprintf(source + length, sizeof(source) - (size_t)length, "struct S f%u(long n){struct S a={{n,1,2,3}};struct S b={{n+10,4,5,6}};", k);
        for (uint32_t e = 0; length > 0 && (size_t)length < sizeof(source) && e < k; e += 1)
        {
            length += snprintf(source + length, sizeof(source) - (size_t)length, "a.v[%u]+=n;", e % 4);
        }
        length += snprintf(source + length, sizeof(source) - (size_t)length, "if(n&1)return b;return a;}\nstruct S g%u(long n){", k);
        for (uint32_t e = 0; length > 0 && (size_t)length < sizeof(source) && e < k; e += 1)
        {
            length += snprintf(source + length, sizeof(source) - (size_t)length, "n+=%u;", e + 1);
        }
        length += snprintf(source + length, sizeof(source) - (size_t)length,
                           "{__attribute__((cleanup(clean)))long c=n;if(n&1)return make(c+1);}return make(n+5);}\n");
    }
    length += snprintf(source + length, sizeof(source) - (size_t)length, "int tested(int n){long h=0;struct S r;");
    for (uint32_t k = 0; length > 0 && (size_t)length < sizeof(source) && k <= 20; k += 1)
    {
        length += snprintf(source + length, sizeof(source) - (size_t)length,
                           "r=f%u(n);h=h*31+r.v[0]+r.v[1]*3+r.v[2]*5+r.v[3]*7;r=g%u(n);h=h*31+r.v[0]+r.v[1]*3+r.v[2]*5+r.v[3]*7;", k, k);
    }
    length += snprintf(source + length, sizeof(source) - (size_t)length, "return (int)(h^(h>>32));}\n");
    check_test_expect(test, length > 0 && (size_t)length < sizeof(source), "the result lanes' source fits");
    if (length > 0 && (size_t)length < sizeof(source))
    {
        check_test_program_source(test, source, "result-control-lanes", clang);
    }
}

// The returns-twice library names' compare (check_lower_calls_twice), the eight interned ids at once: a callee named
// each of them returns twice, and one named none of them, or unnamed, doesn't.
static void check_test_lower_calls_twice(CheckTest* test)
{
    IdentId names[4] = { 0 };
    uint32_t object_declarations[4] = { 0, 1, 2, 3 };
    Parser parser = { 0 };
    parser.tokens.names = names;
    const CheckInput input = { .token_count = 4 };
    const CheckProgram program = { .input = &input, .parser = &parser, .object_declarations = object_declarations, .object_count = 4 };
    IdentId twice[BUSTER_CHECK_LOWER_TWICE_NAMES];
    for (uint32_t k = 0; k < BUSTER_CHECK_LOWER_TWICE_NAMES; k += 1)
    {
        twice[k] = 100 + k * 3;
    }
    const CheckLowerWriter writer = { .program = &program, .twice_names = twice };
    bool right = true;
    for (uint32_t k = 0; k < BUSTER_CHECK_LOWER_TWICE_NAMES; k += 1)
    {
        names[1] = 100 + k * 3;
        names[2] = 101 + k * 3;
        right &= check_lower_calls_twice(&writer, 1) && !check_lower_calls_twice(&writer, 2) && !check_lower_calls_twice(&writer, 0);
    }
    check_test_expect(test, right, "a callee named any of the eight returns-twice names returns twice, and no other");
}

// The branches' targets by their definition: each entry in order whose kind is one of the four branches' takes its
// label's entry, until the first whose label is out of `label_count` or has no entry, which stays as it was.
static bool check_test_resolve_labels_definition(const uint8_t* kinds, uint64_t* values, uint32_t count, const uint32_t* labels, uint32_t label_count)
{
    bool valid = true;
    for (uint32_t i = 0; valid && i < count; i += 1)
    {
        const bool branch = kinds[i] == BUSTER_TYPED_BRANCH_FALSE || kinds[i] == BUSTER_TYPED_BRANCH_FALSE + 1 || kinds[i] == BUSTER_TYPED_BRANCH_FALSE + 2 ||
                            kinds[i] == BUSTER_TYPED_JUMP_VALUE;
        valid = !branch || (values[i] < label_count && labels[values[i]] != BUSTER_CHECK_NONE);
        values[i] = branch && valid ? labels[values[i]] : values[i];
    }
    return valid;
}

// The branches' targets (check_lower_resolve_labels): by 64 kinds at a time as by their definition, the same targets
// and the same first failure, for counts across the masks' tails, the kinds at a page's end, the four branch kinds among
// the kinds beside them, and every label with an entry, or some out of range or without one.
static void check_test_program_resolve_labels(CheckTest* test)
{
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    uint8_t* mapping = mmap(0, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const bool ready = mapping != MAP_FAILED && page >= 200 && mprotect(mapping + page, page, PROT_NONE) == 0;
    check_test_expect(test, ready, "the branches' kinds have a protected tail");
    uint64_t values[200];
    uint64_t defined_values[200];
    uint32_t labels[16];
    bool same = true;
    uint32_t resolved = 0;
    uint32_t failed = 0;
    for (uint32_t round = 0; ready && round < 600; round += 1)
    {
        const uint32_t count = round % 200;
        // Every label with an entry in a third of the rounds; some out of range or without one in the others.
        const bool whole = round % 3 == 0;
        uint8_t* kinds = mapping + page - count;
        const uint32_t label_count = 1 + check_test_random(test) % 16;
        for (uint32_t l = 0; l < label_count; l += 1)
        {
            labels[l] = !whole && check_test_random(test) % 8 == 0 ? BUSTER_CHECK_NONE : check_test_random(test) % 1000;
        }
        for (uint32_t i = 0; i < count; i += 1)
        {
            kinds[i] = (uint8_t)(BUSTER_TYPED_BRANCH_FALSE - 2 + check_test_random(test) % 8);
            values[i] = check_test_random(test) % (whole ? label_count : label_count + 2);
        }
        memcpy(defined_values, values, (uint64_t)count * sizeof(uint64_t));
        const bool defined = check_test_resolve_labels_definition(kinds, defined_values, count, labels, label_count);
        const bool vector = check_lower_resolve_labels(kinds, values, count, labels, label_count);
        same &= defined == vector && memcmp(values, defined_values, (uint64_t)count * sizeof(uint64_t)) == 0;
        resolved += defined;
        failed += !defined;
    }
    check_test_expect(test, same && resolved != 0 && failed != 0, "the branches' targets by 64 kinds at a time are their definition's, and so is the first failure");
    if (mapping != MAP_FAILED && mapping != 0)
    {
        munmap(mapping, page * 2);
    }
}

// A program fixture checked and lowered by both paths for a target (null for x86-64's): accepted or not, lowered or
// refused, with the references and initializations it keeps; and, for a refusal with `reason`, the feature the refusal
// names (CheckProgramLowered.reason, which the driver prints as "unsupported: FEATURE").
static void check_test_program_boundary_target(CheckTest* test, const char* source, const char* name, bool accepted, bool machine, uint32_t references,
                                               uint32_t initializations, const char* reason, const X86Target* target)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    const PPOptions pp_options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true,
                                   .target = target != NULL ? *target : (X86Target){ 0 } };
    const PredefinesOptions predefines = check_test_program_predefines();
    bool preprocessed = intern_initialize(&identifiers) && pp_initialize(&pp, &pp_options, &identifiers) &&
                        pp_run_source(&pp, name, (const uint8_t*)source, (uint32_t)strlen(source));
    check_test_expect(test, preprocessed, "program boundary source preprocessing");
    for (uint32_t path = 0; preprocessed && path < 2; path += 1)
    {
        CheckTestTypes types = { 0 };
        Arena input_arena = arena_reserve(UINT64_C(1) << 26);
        Arena output = arena_reserve(UINT64_C(1) << 26);
        Arena scratch = arena_reserve(UINT64_C(1) << 26);
        const ParseOptions parsing = { .scalar = path == 0 };
        const CheckOptions checking = { 0 };
        CheckInput input = { 0 };
        CheckStream stream = { 0 };
        CheckInitializers plans = { 0 };
        CheckAsms asms = { 0 };
        bool ready = input_arena.base != 0 && output.base != 0 && scratch.base != 0 && check_test_types_open(&types) &&
                     parse_initialize(&parser, &parsing, pp_options.language) && parse_translation_unit(&parser, &pp) && parser.errors == 0 &&
                     check_input(&input, &parser, &pp, &types.table, &input_arena, &scratch) &&
                     check_graph_run(&types.table, &input.graph, &input_arena, &scratch, &checking, &stream) && stream.report_count == 0 &&
                     check_initializers_run(&plans, &input, &stream, &parser, &types.table, &input_arena, &scratch) &&
                     check_asms_run(&asms, &input, &stream, &types.table, &input_arena, &scratch);
        char description[256];
        snprintf(description, sizeof(description), "%s: source and typed expression/initializer input is ready", name);
        check_test_expect(test, ready, description);
        if (!ready)
        {
            fprintf(stderr, "test-checker: %s input=%u stream=%u plans=%u token=%u\n", name, input.report_count, stream.report_count, plans.status, plans.token);
        }
        if (ready)
        {
            const CheckProgramOptions options = { .initializers = &plans, .asms = &asms, .identifiers = &identifiers, .scalar = path == 0,
                                                  .target = pp_options.target, .predefines = &predefines };
            CheckProgram program = { 0 };
            const bool built = check_program(&program, &parser, &input, &stream, &types.table, options, &output, &scratch);
            snprintf(description, sizeof(description), "%s: checked control acceptance matches the independent C constraint", name);
            check_test_expect(test, built == accepted && program.status == (accepted ? CHECK_PROGRAM_OK : CHECK_PROGRAM_INVALID), description);
            if (built)
            {
                // The lowering's bounds' node counts (check_lower_node_counts), by 16 ops at a time, are their scalar twin's.
                // A fixture named for the ops' masks has no _Atomic kind in its type table, so that they, not the rules,
                // take it.
                const CheckLowerNodeCounts twin = check_lower_node_counts_scalar(&program);
                const CheckLowerNodeCounts counted = check_lower_node_counts(&program);
                const bool atomic_kinds = memchr(types.table.types.kind, BUSTER_TYPE_KIND_ATOMIC, types.table.types.count) != NULL;
                check_test_expect(test, counted.builtins == twin.builtins && counted.integer_builtins == twin.integer_builtins && counted.atomics == twin.atomics &&
                                            counted.lane_calls == twin.lane_calls && counted.lane_arguments == twin.lane_arguments &&
                                            counted.transport_entries == twin.transport_entries && counted.transport_locals == twin.transport_locals &&
                                            (strstr(name, "node-counts-masks") == NULL || (!atomic_kinds && twin.atomics != 0)),
                                  "the lowering's node counts are their scalar twin's");
                uint32_t writes = 0;
                for (uint32_t i = 0; i < program.control_count; i += 1)
                {
                    writes += program.controls[i].kind == CHECK_CONTROL_INITIALIZE;
                }
                check_test_expect(test, program.reference_count == references && writes == initializations,
                                  "checked body preserves exact live reference and ordered initializer counts");
                {
                    const uint64_t output_mark = output.length;
                    const uint64_t scratch_mark = scratch.length;
                    CheckLowerShared shared = { 0 };
                    CheckProgramLowered prepared = { 0 };
                    const bool prepared_ok = check_lower_prepare(&shared, &prepared, &program, path == 0, false, false, false, &output, &scratch);
                    if (program.body_count == 0)
                    {
                        check_test_expect(test, prepared_ok && shared.table.capacity == 0 && shared.table.rows == 0,
                                          "a unit without function bodies allocates no ABI table, including one with object initializers");
                    }
                    check_test_expect(test, !machine || prepared_ok, "a program that lowers prepares its lowering");
                    if (prepared_ok)
                    {
                        check_test_expect(test, check_test_program_locals(&program, &shared), "the lowering's local_map reads none for every object before any body");
                    }
                    arena_pop_to(&output, output_mark);
                    arena_pop_to(&scratch, scratch_mark);
                }
                CheckProgramLowered lowered = { 0 };
                const uint64_t mark = output.length;
                const CheckControl* controls = program.controls;
                const bool lowered_ok = check_program_lower(&lowered, &program, path == 0, &output, &scratch);
                snprintf(description, sizeof(description), "%s: typed program support is independent of current Tier0 machine classes", name);
                check_test_expect(test, lowered_ok == machine && lowered.status == (machine ? CHECK_PROGRAM_OK : CHECK_PROGRAM_UNSUPPORTED), description);
                snprintf(description, sizeof(description), "%s: the refusal names its feature", name);
                const bool named = reason == NULL || (!lowered_ok && lowered.reason != NULL && strcmp(lowered.reason, reason) == 0);
                check_test_expect(test, named, description);
                if (!named || lowered_ok != machine)
                {
                    fprintf(stderr, "test-checker: %s: lowered=%d status=%u reason=\"%s\", expected \"%s\"\n", name, lowered_ok, lowered.status,
                            lowered.reason != NULL ? lowered.reason : "", reason != NULL ? reason : "");
                }
                if (!lowered_ok)
                {
                    check_test_expect(test, output.length == mark && lowered.functions == 0 && lowered.count == 0 && program.controls == controls && program.status == CHECK_PROGRAM_OK,
                                      "machine refusal rolls back only its output and retains the complete checked program");
                }
                if (machine && program.body_count != 0)
                {
                    Arena empty = arena_reserve(4096);
                    empty.length = 17;
                    empty.capacity = 17;
                    CheckProgram failed = { 0 };
                    const bool failed_ok = check_program(&failed, &parser, &input, &stream, &types.table, options, &empty, &scratch);
                    check_test_expect(test, !failed_ok && failed.status == CHECK_PROGRAM_RESOURCE && failed.controls == 0 && failed.bodies == 0 &&
                                           empty.length == 17 && scratch.length == 0, "program output exhaustion is atomic and preserves live prefixes");
                    CheckProgramLowered failed_lower = { 0 };
                    const bool failed_lower_ok = check_program_lower(&failed_lower, &program, path == 0, &empty, &scratch);
                    check_test_expect(test, !failed_lower_ok && failed_lower.status == CHECK_PROGRAM_RESOURCE && failed_lower.functions == 0 &&
                                           empty.length == 17 && scratch.length == 0, "unit ABI/lowering exhaustion is atomic");
                    CheckProgramCode failed_code = { 0 };
                    const uint64_t emission_mark = output.length;
                    const bool failed_code_ok = check_program_emit(&failed_code, &program, &parser, &identifiers, (LinkCapacity){ 0 }, path == 0,
                                                                   &output, &empty, &scratch, NULL, NULL, false, true);
                    check_test_expect(test, !failed_code_ok && failed_code.status == CHECK_PROGRAM_RESOURCE && failed_code.link.symbols.count == 0 &&
                                           failed_code.lowered.functions == 0 && output.length == emission_mark && empty.length == 17 && scratch.length == 0,
                                      "code byte exhaustion rolls back Link, ABI, atoms and bytes together");
                    empty.capacity = 4096;
                    arena_release(&empty);
                }
            }
            else
            {
                check_test_expect(test, output.length == 0 && program.controls == 0 && program.cases == 0 && program.bodies == 0,
                                  "invalid control constraints publish no partial program");
            }
        }
        check_test_expect(test, scratch.length == 0, "program boundary checks restore scratch");
        parse_release(&parser);
        check_test_types_close(&types);
        arena_release(&scratch);
        arena_release(&output);
        arena_release(&input_arena);
    }
    pp_release(&pp);
    intern_release(&identifiers);
}

// A program fixture, checked and lowered by both paths for x86-64 (check_test_program_boundary_target).
static void check_test_program_boundary_reason(CheckTest* test, const char* source, const char* name, bool accepted, bool machine, uint32_t references,
                                               uint32_t initializations, const char* reason)
{
    check_test_program_boundary_target(test, source, name, accepted, machine, references, initializations, reason, NULL);
}

static void check_test_program_boundary(CheckTest* test, const char* source, const char* name, bool accepted, bool machine, uint32_t references, uint32_t initializations)
{
    check_test_program_boundary_reason(test, source, name, accepted, machine, references, initializations, NULL);
}

// The jump tables the lowering makes of `source`'s switches (#136's item 7), by both paths: `tables` of them, with
// `entries` entries in all, as clang -O0's rule gives them (check_lower_switch_table). The program must list exactly
// its SWITCH controls with four CASE controls or more after them, in the controls' order (CheckProgram.switches), as a
// scan of its controls finds them. Each table's address is made once and duplicated for its load and signed-offset
// addition (#1217), with one symbol patch even when several tables share a function.
static void check_test_program_tables(CheckTest* test, const char* source, const char* name, uint32_t tables, uint32_t entries)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    const PPOptions pp_options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
    const bool preprocessed = intern_initialize(&identifiers) && pp_initialize(&pp, &pp_options, &identifiers) &&
                              pp_run_source(&pp, name, (const uint8_t*)source, (uint32_t)strlen(source));
    check_test_expect(test, preprocessed, "jump table source preprocessing");
    for (uint32_t path = 0; preprocessed && path < 2; path += 1)
    {
        CheckTestTypes types = { 0 };
        Arena input_arena = arena_reserve(UINT64_C(1) << 26);
        Arena output = arena_reserve(UINT64_C(1) << 26);
        Arena scratch = arena_reserve(UINT64_C(1) << 26);
        const ParseOptions parsing = { .scalar = path == 0 };
        const CheckOptions checking = { 0 };
        CheckInput input = { 0 };
        CheckStream stream = { 0 };
        CheckInitializers plans = { 0 };
        CheckAsms asms = { 0 };
        CheckProgram program = { 0 };
        CheckProgramLowered lowered = { 0 };
        const bool ready = input_arena.base != 0 && output.base != 0 && scratch.base != 0 && check_test_types_open(&types) &&
                           parse_initialize(&parser, &parsing, pp_options.language) && parse_translation_unit(&parser, &pp) && parser.errors == 0 &&
                           check_input(&input, &parser, &pp, &types.table, &input_arena, &scratch) &&
                           check_graph_run(&types.table, &input.graph, &input_arena, &scratch, &checking, &stream) && stream.report_count == 0 &&
                           check_initializers_run(&plans, &input, &stream, &parser, &types.table, &input_arena, &scratch) &&
                           check_asms_run(&asms, &input, &stream, &types.table, &input_arena, &scratch);
        const CheckProgramOptions options = { .initializers = &plans, .asms = &asms, .identifiers = &identifiers, .scalar = path == 0 };
        const bool lowered_ok = ready && check_program(&program, &parser, &input, &stream, &types.table, options, &output, &scratch) &&
                                check_program_lower(&lowered, &program, path == 0, &output, &scratch);
        char description[256];
        uint32_t total = 0;
        for (uint32_t t = 0; lowered_ok && t < lowered.table_count; t += 1)
        {
            const CheckLowerTable* table = lowered.tables + t;
            const Tier0Function* function = table->function < program.body_count ? lowered.functions + table->function : NULL;
            const uint32_t address = table->address;
            const uint64_t symbol = (uint64_t)lowered.case_symbol_base + t;
            const bool shared = function != NULL && address < function->count && function->count - address >= 2 &&
                                function->kinds[address] == BUSTER_TYPED_GLOBAL_ADDRESS && function->values[address] == symbol &&
                                function->kinds[address + 1] == BUSTER_TYPED_DUP && function->classes[address + 1] == BUSTER_TYPED_CLASS_U64;
            uint32_t references = 0;
            for (uint32_t i = 0; function != NULL && i < function->count; i += 1)
            {
                references += function->kinds[i] == BUSTER_TYPED_GLOBAL_ADDRESS && function->values[i] == symbol;
            }
            uint32_t listed_addresses = 0;
            for (uint32_t k = 0; function != NULL && function->table_addresses != NULL && k < function->table_count; k += 1)
            {
                listed_addresses += function->table_addresses[k] == address && address < function->table_jumps[k];
            }
            snprintf(description, sizeof(description), "%s: the %s path shares table %u's one patched address", name, path == 0 ? "scalar" : "vector", t);
            check_test_expect(test, shared && references == 1 && listed_addresses == 1, description);
            total += table->count;
        }
        snprintf(description, sizeof(description), "%s: %u jump tables of %u entries by the %s path (clang -O0's rule: %u of %u)", name,
                 lowered_ok ? lowered.table_count : 0, total, path == 0 ? "scalar" : "vector", tables, entries);
        check_test_expect(test, lowered_ok && lowered.table_count == tables && total == entries, description);
        uint32_t listed = 0;
        bool switches = lowered_ok;
        for (uint32_t c = 0; switches && c < program.control_count; c += 1)
        {
            const bool control_switch = program.controls[c].kind == CHECK_CONTROL_SWITCH;
            uint32_t cases = 0;
            while (control_switch && c + 1 + cases < program.control_count && program.controls[c + 1 + cases].kind == CHECK_CONTROL_CASE)
            {
                cases += 1;
            }
            switches = !control_switch || cases < 4 || (listed < program.switch_count && program.switches[listed] == c);
            listed += control_switch && cases >= 4;
        }
        snprintf(description, sizeof(description), "%s: the %s path's program lists %u switches of four cases or more, as its controls have them", name,
                 path == 0 ? "scalar" : "vector", lowered_ok ? program.switch_count : 0);
        check_test_expect(test, switches && listed == program.switch_count, description);
        parse_release(&parser);
        check_test_types_close(&types);
        arena_release(&scratch);
        arena_release(&output);
        arena_release(&input_arena);
    }
    pp_release(&pp);
    intern_release(&identifiers);
}

// The dispatch oracle uses C's signed/unsigned comparisons on the original target bits, independently of the
// planner's sortable keys. Narrow values are truncated before comparing, as the generated 32-bit instructions do.
static bool check_test_program_switch_less(uint64_t left, uint64_t right, uint8_t class)
{
    return class == BUSTER_TYPED_CLASS_I32 ? (int32_t)(uint32_t)left < (int32_t)(uint32_t)right :
           class == BUSTER_TYPED_CLASS_U32 ? (uint32_t)left < (uint32_t)right :
           class == BUSTER_TYPED_CLASS_I64 ? (int64_t)left < (int64_t)right : left < right;
}

// Interpret the actual emitted dispatch, not the tree plan. Its external labels are the 17 case destinations and
// default (0); other labels must point to an emitted entry. A hard step/comparison budget rejects a loop or a linear
// dispatch even when it would eventually return the right destination. The surrounding operand is never read here.
static bool check_test_program_switch_run(const CheckLowerWriter* writer, uint64_t input, uint32_t expected, uint32_t budget)
{
    uint64_t stack[2] = { 0 };
    uint8_t classes[2] = { 0 };
    uint32_t depth = 0;
    uint32_t entry = 0;
    uint32_t comparisons = 0;
    uint32_t destination = BUSTER_CHECK_NONE;
    bool valid = true;
    for (uint32_t step = 0; valid && destination == BUSTER_CHECK_NONE && step < writer->count && entry < writer->count; step += 1)
    {
        const uint32_t at = entry++;
        const uint8_t kind = writer->kinds[at];
        const uint8_t class = writer->classes[at];
        const uint64_t value = writer->values[at];
        if (kind == BUSTER_TYPED_LOCAL || kind == BUSTER_TYPED_CONST)
        {
            valid = depth < 2 && (kind != BUSTER_TYPED_LOCAL || value == 0) &&
                    class >= BUSTER_TYPED_CLASS_I32 && class <= BUSTER_TYPED_CLASS_U64;
            if (valid)
            {
                const uint64_t bits = kind == BUSTER_TYPED_LOCAL ? input : value;
                stack[depth] = class == BUSTER_TYPED_CLASS_I32 || class == BUSTER_TYPED_CLASS_U32 ? (uint32_t)bits : bits;
                classes[depth++] = class;
            }
        }
        else if (kind == BUSTER_TYPED_LT || kind == BUSTER_TYPED_EQ || kind == BUSTER_TYPED_GE || kind == BUSTER_TYPED_LE)
        {
            valid = depth == 2 && classes[0] == classes[1] && class == BUSTER_TYPED_CLASS_I32 && comparisons < budget;
            if (valid)
            {
                const bool less = check_test_program_switch_less(stack[0], stack[1], classes[0]);
                const bool greater = check_test_program_switch_less(stack[1], stack[0], classes[0]);
                stack[0] = kind == BUSTER_TYPED_LT ? less : kind == BUSTER_TYPED_EQ ? stack[0] == stack[1] :
                           kind == BUSTER_TYPED_GE ? !less : !greater;
                classes[0] = BUSTER_TYPED_CLASS_I32;
                depth = 1;
                comparisons += 1;
            }
        }
        else if (kind == BUSTER_TYPED_BRANCH_TRUE || kind == BUSTER_TYPED_BRANCH_FALSE || kind == BUSTER_TYPED_JUMP)
        {
            const bool conditional = kind != BUSTER_TYPED_JUMP;
            valid = depth == (conditional ? 1u : 0u) && value < writer->label_count;
            const bool taken = valid && (!conditional || (stack[0] != 0) == (kind == BUSTER_TYPED_BRANCH_TRUE));
            depth = 0;
            if (taken)
            {
                if (value < 18)
                {
                    destination = (uint32_t)value;
                }
                else
                {
                    entry = writer->labels[value];
                    valid = entry < writer->count;
                }
            }
        }
        else
        {
            valid = false;
        }
    }
    return valid && destination == expected && depth == 0;
}

// Boundaries may fall more than once inside one target_clones body. All of its copies must belong to one part, even
// when moving the boundaries makes some parts empty; unrelated bodies retain their original boundaries.
static void check_test_program_clone_parts(CheckTest* test)
{
    CheckProgramBody bodies[8] = {
        { .opening = 1 }, { .opening = 2 }, { .opening = 2 }, { .opening = 2 },
        { .opening = 3 }, { .opening = 4 }, { .opening = 4 }, { .opening = 5 },
    };
    uint64_t boundaries[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8 };
    const uint64_t expected[] = { 0, 1, 4, 4, 4, 5, 7, 7, 8 };
    const CheckProgram program = { .bodies = bodies };
    CheckEmitRun run = { .program = &program, .body_count = 8, .boundaries = boundaries };
    check_program_emit_group_copies(&run, 8);
    check_test_expect(test, memcmp(boundaries, expected, sizeof(expected)) == 0, "clone copies stay together across empty parts");
}

// #1371's checked-case producer contract, without another large source string in the compiler. The source-order
// records are permuted, contain empty intervals and repeated destinations, and span both ends of each integer type.
// Real source-to-native coverage, including selector effects and control flow, lives in tests/driver/tier1_sparse_switch.c.
static void check_test_program_sparse_switches(CheckTest* test)
{
    CheckTestTypes types = { 0 };
    Arena output = arena_reserve(UINT64_C(1) << 20);
    Arena scratch = arena_reserve(UINT64_C(1) << 20);
    bool ready = output.base != NULL && scratch.base != NULL && check_test_types_open(&types);
    const TypeId unusual = ready ? type_bit_int(&types.table, 17, false) : 0;
    const uint32_t type_count = ready ? types.table.types.count : 0;
    uint8_t* type_classes = ready ? BUSTER_ARENA_PUSH_ARRAY(&scratch, uint8_t, type_count) : NULL;
    uint8_t* type_facts = ready ? BUSTER_ARENA_PUSH_ARRAY(&scratch, uint8_t, type_count) : NULL;
    ready = ready && unusual != 0 && type_classes != NULL && type_facts != NULL;
    check_test_expect(test, ready, "sparse-switch planner fixture storage");
    if (ready)
    {
        check_test_classify_types_definition(&types.table, type_count, type_classes, type_facts);
    }
    const uint64_t scratch_mark = scratch.length;
    const TypeId builtin_types[] = { BUSTER_TYPE_INT, BUSTER_TYPE_UINT, BUSTER_TYPE_LONG, BUSTER_TYPE_ULONG };
    for (uint32_t variant = 0; ready && variant < 9; variant += 1)
    {
        const bool dense = variant == 7;
        const bool enabled = variant != 8;
        TypeId type = variant < 4 ? builtin_types[variant] : variant == 5 ? BUSTER_TYPE_INT128 : variant == 6 ? unusual : BUSTER_TYPE_INT;
        const uint8_t class = check_lower_type_class(&types.table, type);
        const uint32_t count = variant == 4 ? 127 : variant == 1 || variant == 3 ? 129 : 128;
        const uint32_t case_count = count + 2;
        const uint32_t copies = variant == 0 ? 2 : 1;
        const bool signed_type = type == BUSTER_TYPE_INT || type == BUSTER_TYPE_LONG;
        const uint64_t mask = class == BUSTER_TYPED_CLASS_I32 || class == BUSTER_TYPED_CLASS_U32 ? UINT32_MAX : UINT64_MAX;
        const uint64_t sign = signed_type ? (mask >> 1) + 1 : 0;
        CheckProgramCase cases[131] = { 0 };
        CheckProgramCase original[131];
        CheckControl controls[133] = { 0 };
        uint32_t ranks[131] = { 0 };
        controls[0] = (CheckControl){ .node = 0, .kind = CHECK_CONTROL_SWITCH };
        for (uint32_t i = 0; i < case_count; i += 1)
        {
            const bool empty = i == 0 || i + 1 == case_count;
            const uint32_t rank = empty ? 0 : ((i - 1) * 73) % count;
            const uint64_t key = dense ? rank : rank == 0 ? 0 : rank + 1 == count ? mask :
                                 (mask >> 1) - (uint64_t)(count / 2) * 100003 + (uint64_t)rank * 100003;
            const uint64_t span = !dense && rank != 0 && rank + 1 != count && rank % 4 == 0 ? 2 : 0;
            const uint64_t low = variant == 6 ? (uint64_t)rank * 997 : key ^ sign;
            const uint64_t high = variant == 6 ? low : (key + span) ^ sign;
            const unsigned __int128 high_bits = variant == 5 ? (unsigned __int128)1 << 100 : 0;
            cases[i] = (CheckProgramCase){
                .lower = empty ? 1 : high_bits | low,
                .upper = empty ? 0 : high_bits | high,
                .type = type,
                .token = i + 1,
                .owner = 0,
                .target = 1 + rank % 17,
                .range = empty || span != 0,
            };
            controls[i + 1] = (CheckControl){ .token = i + 1, .target = i, .kind = CHECK_CONTROL_CASE };
            ranks[i] = rank;
        }
        controls[case_count + 1] = (CheckControl){ .target = 0, .kind = CHECK_CONTROL_JUMP };
        memcpy(original, cases, sizeof(cases));
        CheckProgramBody bodies[2] = {
            { .count = case_count + 2 },
            { .count = case_count + 2 },
        };
        uint32_t body_options[2] = { 0, 1 };
        uint32_t switches[1] = { 0 };
        const CheckStream stream = { .count = 1, .value_types = &type };
        const CheckProgram program = {
            .types = &types.table,
            .stream = &stream,
            .controls = controls,
            .bodies = bodies,
            .body_options = copies == 2 ? body_options : NULL,
            .cases = cases,
            .case_count = case_count,
            .body_count = copies,
            .switches = switches,
            .switch_count = 1,
            .status = CHECK_PROGRAM_OK,
        };
        CheckProgramLowered lowered = { 0 };
        CheckLowerSwitchTrees trees = { 0 };
        const bool planned = check_lower_switch_tables(&lowered, &trees, &program, enabled, &output, &scratch);
        const uint32_t expected_trees = variant < 4 ? copies : 0;
        bool same = planned && trees.count == expected_trees && lowered.table_count == (dense ? 1u : 0u) &&
                    memcmp(cases, original, sizeof(cases)) == 0;
        for (uint32_t b = 0; same && b < expected_trees; b += 1)
        {
            const CheckLowerSwitchTree* tree = check_lower_switch_tree_of(&trees, b, 0);
            same = tree != NULL && tree->count == count && tree->controls == case_count && tree->fallback == 0;
            for (uint32_t i = 0; same && i < count; i += 1)
            {
                const CheckLowerCaseRange item = tree->cases[i];
                same = item.item < case_count && ranks[item.item] == i && item.label == cases[item.item].target && item.lower <= item.upper;
            }
        }
        same = same && check_lower_switch_tree_of(&trees, copies, 0) == NULL && check_lower_switch_tree_of(&trees, 0, 1) == NULL;
        char description[192];
        snprintf(description, sizeof(description), "sparse-switch plan %u: exact ordering, source identity, empty intervals, policy and clone ownership", variant);
        check_test_expect(test, same, description);
        if (same && expected_trees != 0)
        {
            const uint32_t capacity = 4096;
            uint32_t local = 0;
            CheckLowerWriter writer = {
                .program = &program,
                .output = &lowered,
                .capacity = capacity,
                .class_limit = BUSTER_TYPED_CLASSES,
                .switch_locals = &local,
                .type_classes = type_classes,
                .type_facts = type_facts,
                .type_count = type_count,
                .label_count = 18,
                .track = true,
                .reachable = true,
                .stack_head = 1,
                .stack_count = 1,
                .scalar = true,
            };
            bool room = true;
#define BUSTER_CHECK_TEST_SWITCH_COLUMN(field, element) writer.field = BUSTER_ARENA_PUSH_ARRAY(&scratch, element, capacity); room &= writer.field != NULL
            BUSTER_CHECK_TEST_SWITCH_COLUMN(kinds, uint8_t);
            BUSTER_CHECK_TEST_SWITCH_COLUMN(classes, uint8_t);
            BUSTER_CHECK_TEST_SWITCH_COLUMN(values, uint64_t);
            BUSTER_CHECK_TEST_SWITCH_COLUMN(labels, uint32_t);
            BUSTER_CHECK_TEST_SWITCH_COLUMN(label_heads, uint32_t);
            BUSTER_CHECK_TEST_SWITCH_COLUMN(stack_parents, uint32_t);
            BUSTER_CHECK_TEST_SWITCH_COLUMN(stack_classes, uint8_t);
            BUSTER_CHECK_TEST_SWITCH_COLUMN(stack_bytes, uint8_t);
#undef BUSTER_CHECK_TEST_SWITCH_COLUMN
            check_test_expect(test, room, "sparse-switch dispatch fixture storage");
            if (room)
            {
                memset(writer.label_heads, 0xff, 18 * sizeof(uint32_t));
                writer.stack_parents[1] = 0;
                writer.stack_classes[1] = BUSTER_TYPED_CLASS_U64;
                writer.stack_bytes[1] = 0;
                check_lower_switch_tree(&writer, trees.items, 0);
                bool executed = lowered.status == CHECK_PROGRAM_OK && writer.count < capacity && !writer.reachable && writer.stack_head == 1;
                for (uint32_t label = 0; label < 18; label += 1)
                {
                    executed = writer.label_heads[label] == 1 && executed;
                }
                uint32_t budget = 2 * BUSTER_CHECK_LOWER_TREE_LEAF_CASES;
                for (uint32_t covered = BUSTER_CHECK_LOWER_TREE_LEAF_CASES; covered < count; covered *= 2)
                {
                    budget += 1;
                }
                for (uint32_t sample = 0; executed && sample < case_count * 4 + 128; sample += 1)
                {
                    const uint32_t item = sample / 4;
                    const uint64_t input = item < case_count ? (uint64_t)(sample % 4 < 2 ? cases[item].lower : cases[item].upper) +
                                                                  (sample % 4 == 0 ? UINT64_MAX : sample % 4 == 3 ? 1 : 0) :
                                           ((uint64_t)check_test_random(test) << 32) | check_test_random(test);
                    uint32_t expected = 0;
                    for (uint32_t i = 0; expected == 0 && i < case_count; i += 1)
                    {
                        const bool within = !check_test_program_switch_less(input, (uint64_t)cases[i].lower, class) &&
                                            !check_test_program_switch_less((uint64_t)cases[i].upper, input, class);
                        expected = within ? cases[i].target : 0;
                    }
                    executed = check_test_program_switch_run(&writer, input, expected, budget);
                }
                check_test_expect(test, executed, "sparse-switch emitted branches match an independent linear oracle within logarithmic depth and keep pending operands");
            }
            // Failure in the retained-interval allocation must publish no partial plan or modify the checked cases.
            CheckLowerCaseRange case_room[131];
            CheckLowerCaseRange sorted_room[131];
            CheckLowerSwitchTree failed_tree[1];
            CheckLowerSwitchTrees failed_trees = { .items = failed_tree, .capacity = 1 };
            CheckLowerTable failed_table[1];
            CheckProgramLowered failed = { .tables = failed_table };
            const uint64_t old_capacity = scratch.capacity;
            const uint64_t failed_mark = scratch.length;
            const uint64_t output_mark = output.length;
            scratch.capacity = failed_mark;
            const bool failed_ok = check_lower_switch_table(&failed, 1, &failed_trees, &program, 0, 0, class, case_room, sorted_room, &output, &scratch);
            check_test_expect(test, !failed_ok && failed_trees.count == 0 && failed.table_count == 0 && scratch.length == failed_mark &&
                                   output.length == output_mark && memcmp(cases, original, sizeof(cases)) == 0,
                              "sparse-switch interval exhaustion publishes no plan and preserves live prefixes and checked cases");
            scratch.capacity = old_capacity;
        }
        arena_pop_to(&scratch, scratch_mark);
        arena_pop_to(&output, 0);
    }
    check_test_types_close(&types);
    arena_release(&scratch);
    arena_release(&output);
}

#include "c_check_program_select_test.c"
#include "c_check_program_overload_test.c"
#include "c_check_program_query_test.c"
#include "c_check_region_input_test.c"
#include "c_check_program_atomic_test.c"
#include "c_check_program_asm_test.c"
#include "c_check_program_copy_test.c"
#include "c_check_program_clear_test.c"
#include "c_check_program_named_result_test.c"
#include "c_check_program_forward_test.c"
#include "c_check_program_folds_test.c"
#include "c_check_program_fence_test.c"
#include "c_check_program_atomic_access_test.c"
#include "c_check_program_atomic_rmw_test.c"
#include "c_check_program_bit_unit_test.c"
#include "c_check_program_vector_test.c"
#include "c_check_program_x86_test.c"
#include "c_check_program_x86_rest_test.c"
#include "c_check_program_amx_test.c"
#include "c_check_program_transparent_test.c"
#include "c_check_program_union_cast_test.c"
#include "c_check_program_alias_test.c"
#include "c_check_program_vector_builtins_test.c"
#include "c_check_program_fp16_test.c"
#include "c_check_program_bf16_test.c"
#include "c_check_program_parameters_test.c"
#include "c_check_program_half_test.c"

// A program the checker refused, which may have no stream or input: lowering refuses it by its status, on both paths,
// before reading either, and writes nothing.
static void check_test_program_refused(CheckTest* test)
{
    Arena output = arena_reserve(1ull << 16);
    Arena scratch = arena_reserve(1ull << 16);
    for (uint32_t path = 0; path < 2; path += 1)
    {
        const CheckProgram program = { .status = CHECK_PROGRAM_INVALID };
        CheckProgramLowered lowered = { 0 };
        const bool ready = output.base != 0 && scratch.base != 0;
        const bool lowered_ok = ready && check_program_lower(&lowered, &program, path == 0, &output, &scratch);
        check_test_expect(test, ready && !lowered_ok && lowered.status == CHECK_PROGRAM_INVALID && lowered.functions == 0 && output.length == 0 && scratch.length == 0,
                          "a refused program without its stream or input lowers to nothing");
    }
    arena_release(&output);
    arena_release(&scratch);
}

// The table of every type's class and facts that the bodies read (check_lower_classify_types) against its twin's, on the
// types of random worlds of every kind (the type table's tests' world: pointers, arrays, functions, vectors, atomic
// types, aligned typedefs, complex types of _BitInts, enums and records; and the calling convention's tests' world:
// complex types of every floating type among them): of the whole world and of every count of its types to 300, with plain
// char signed and unsigned; then with the kinds' column and both tables ending at a PROT_NONE page, for every count to
// 200, so that a read or a write past the last type faults. And a function's memory classes from its entries'
// (check_lower_entry_classes), at the page's end too.
static __attribute__((noinline)) void check_test_program_type_facts(CheckTest* test)
{
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    uint8_t* mappings[3] = { 0 };
    bool guarded = true;
    for (uint32_t i = 0; i < 3; i += 1)
    {
        mappings[i] = mmap(0, 2 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        guarded = guarded && mappings[i] != MAP_FAILED && mprotect(mappings[i] + page, page, PROT_NONE) == 0;
    }
    check_test_expect(test, guarded, "the guarded columns of the types' classes");
    for (uint32_t round = 0; round < 2; round += 1)
    {
        AbiTestWorld world;
        const bool opened = abi_test_world_open(&world, 8192);
        check_test_expect(test, opened, "a world of types to classify for the lowering");
        if (opened)
        {
            if (round == 0)
            {
                type_test_world_build(&world.types, 531, 1000);
            }
            else
            {
                abi_test_world_build(&world, 531, 1000);
            }
            const TypeTable* table = &world.types.test.table;
            const uint32_t count = table->types.count;
            uint8_t* columns = malloc(4 * ((uint64_t)count + 1));
            check_test_expect(test, columns != 0 && count > 300, "the world's types and their columns");
            // In both char modes: the vector path copies the builtins' rows, plain char's class by the mode, which the
            // definition classifies by the rules (#442).
            for (uint32_t mode = 0; columns != 0 && mode < 2; mode += 1)
            {
                TypeTable flipped = *table;
                flipped.char_unsigned = mode != 0;
                for (uint32_t prefix = 0; prefix <= count; prefix = prefix < 300 ? prefix + 1 : prefix < count ? count : prefix + 1)
                {
                    uint8_t* scalar_classes = columns;
                    uint8_t* scalar_facts = columns + count + 1;
                    uint8_t* vector_classes = columns + 2 * ((uint64_t)count + 1);
                    uint8_t* vector_facts = columns + 3 * ((uint64_t)count + 1);
                    memset(vector_classes, 0xa5, prefix + 1);
                    memset(vector_facts, 0xa5, prefix + 1);
                    check_test_classify_types_definition(&flipped, prefix, scalar_classes, scalar_facts);
                    check_lower_classify_types(&flipped, prefix, vector_classes, vector_facts);
                    const bool same = memcmp(scalar_classes, vector_classes, prefix) == 0 && memcmp(scalar_facts, vector_facts, prefix) == 0 &&
                                      vector_classes[prefix] == 0xa5 && vector_facts[prefix] == 0xa5;
                    char description[192];
                    snprintf(description, sizeof(description), "the first %u of world %u's %u types, plain char %s: their classes and facts by the vector path and the rules", prefix,
                             round, count, mode != 0 ? "unsigned" : "signed");
                    check_test_expect(test, same, description);
                }
            }
            for (uint32_t prefix = 0; columns != 0 && guarded && prefix <= 200 && prefix <= count; prefix += 1)
            {
                // The kinds copied to end at the guard page, read through a copy of the table that has them.
                TypeTable copy = *table;
                uint8_t* kinds = mappings[0] + page - prefix;
                memcpy(kinds, table->types.kind, prefix);
                copy.types.kind = kinds;
                uint8_t* classes = mappings[1] + page - prefix;
                uint8_t* facts = mappings[2] + page - prefix;
                check_test_classify_types_definition(table, prefix, columns, columns + count + 1);
                check_lower_classify_types(&copy, prefix, classes, facts);
                char description[160];
                snprintf(description, sizeof(description), "world %u's first %u types' classes and facts, at a page's end", round, prefix);
                check_test_expect(test, memcmp(columns, classes, prefix) == 0 && memcmp(columns + count + 1, facts, prefix) == 0, description);
            }
            free(columns);
            abi_test_world_close(&world);
        }
    }
    // A function's memory classes from its entries' classes (check_lower_entry_classes) against their definition's: columns of every count to 200 ending at the guard page, of random classes below F80, with one of F80,
    // I128, U128 and VECTOR at a random place in three columns of four, and none in the others.
    uint32_t found = 0;
    for (uint32_t count = 0; guarded && count <= 200; count += 1)
    {
        for (uint32_t round = 0; round < 8; round += 1)
        {
            uint8_t* classes = mappings[0] + page - count;
            for (uint32_t i = 0; i < count; i += 1)
            {
                classes[i] = (uint8_t)(check_test_random(test) % BUSTER_TYPED_CLASS_F80);
            }
            const uint8_t memory[4] = { BUSTER_TYPED_CLASS_F80, BUSTER_TYPED_CLASS_I128, BUSTER_TYPED_CLASS_U128, BUSTER_TYPED_CLASS_VECTOR };
            if (count != 0 && round % 4 != 0)
            {
                classes[check_test_random(test) % count] = memory[(round + round / 4) % 4];
            }
            const uint32_t vector = check_lower_entry_classes(classes, count);
            // The definition: each class of the entries in turn.
            uint32_t expected = 0;
            for (uint32_t i = 0; i < count; i += 1)
            {
                expected |= classes[i] == BUSTER_TYPED_CLASS_F80 ? BUSTER_CHECK_LOWER_ENTRIES_X87 : 0;
                expected |= classes[i] == BUSTER_TYPED_CLASS_I128 || classes[i] == BUSTER_TYPED_CLASS_U128 ? BUSTER_CHECK_LOWER_ENTRIES_WIDE : 0;
                expected |= classes[i] == BUSTER_TYPED_CLASS_VECTOR ? BUSTER_CHECK_LOWER_ENTRIES_VECTOR : 0;
            }
            found |= expected;
            char description[160];
            snprintf(description, sizeof(description), "the memory classes of %u entries' classes, round %u: %u, for %u", count, round, vector, expected);
            check_test_expect(test, vector == expected, description);
        }
    }
    check_test_expect(test, !guarded || found == (BUSTER_CHECK_LOWER_ENTRIES_X87 | BUSTER_CHECK_LOWER_ENTRIES_WIDE | BUSTER_CHECK_LOWER_ENTRIES_VECTOR),
                      "the entries' classes take each memory class");
    for (uint32_t i = 0; i < 3; i += 1)
    {
        if (mappings[i] != MAP_FAILED && mappings[i] != 0)
        {
            munmap(mappings[i], 2 * page);
        }
    }
}

// The x87's encodings, from n: zero, negative zero, a denormal, two pseudo-denormals, one, an unnormal, a pseudo-zero,
// the infinities, a pseudo-infinity, quiet and signaling NaNs, a pseudo-NaN, the largest value and the largest unnormal.

// The builtins of long double that this compiler's own main.c calls (#148, #129): clang -O0 classifies an x87 value by
// the x87's compares or by its bits, by builtin, which the encodings tell apart. The fpclassify's normal and subnormal
// selectors are constants, since clang evaluates the two in an order its own build decides (check_lower_floating_prepare).
static __attribute__((noinline)) void check_test_program_long_double_builtins(CheckTest* test, const char* clang)
{
    check_test_program_suite(test, "checker/programs/program-long-double-builtins", clang);
}

// fpclassify's selectors (#148), on a double that is each category in turn and on one known to be normal after the
// argument's own effect. mark counts each selector's evaluations in a 3-bit field of its own (the argument's seen++ in
// the lowest), not their order, since clang evaluates the normal and subnormal ones in an order its own build decides
// (check_lower_floating_prepare). The order-recording mark, whose decimal digits are the selectors in the order they ran,
// is the encoding these programs had, kept for the controls below.
#define BUSTER_CHECK_TEST_FPCLASSIFY_COUNTS "int seen;int mark(int n){seen+=1<<(3*n);return n;}"
#define BUSTER_CHECK_TEST_FPCLASSIFY_ORDER "int seen;int mark(int n){seen=seen*10+n;return n;}"
#define BUSTER_CHECK_TEST_FPCLASSIFY_CATEGORIES \
    "int tested(int n){seen=0;" \
    "double x=n<0?__builtin_nan(\"\"):n==0?0.0:n==1?__builtin_inf():n==2?0x1p-1074:(double)n;" \
    "int r=__builtin_fpclassify(mark(1),mark(2),mark(3),mark(4),mark(5),x);return seen*10+r;}"
#define BUSTER_CHECK_TEST_FPCLASSIFY_KNOWN_CATEGORY \
    "int tested(int n){seen=0;" \
    "int r=__builtin_fpclassify(mark(1),mark(2),mark(3),mark(4),mark(5),(seen++,1.0));return seen*10+r+n;}"

// The controls' oracles, each __builtin_fpclassify for clang's build of a program: what clang's code computes for a
// double alone (the model compares with a double's smallest normal value), with the normal and subnormal selectors in
// the order C's sequencing gives it (the normal one first, as check_lower_floating_prepare emits them, or the subnormal
// one first, as a GCC-built clang runs them); and mutants that evaluate the normal selector twice, the subnormal one
// never or the argument twice.
#define BUSTER_CHECK_TEST_FPCLASSIFY_DOUBLE_MODEL(first, second) \
    "#define __builtin_fpclassify(nan,infinite,normal,subnormal,zero,value) ({double v_=(value);int z_=(zero);v_==0?z_:" \
    "({int n_=(nan);v_!=v_?n_:({int i_=(infinite);__builtin_fabs(v_)==__builtin_inf()?i_:" \
    "({int " first ";int " second ";__builtin_fabs(v_)>=0x1p-1022?m_:s_;});});});})\n"
#define BUSTER_CHECK_TEST_FPCLASSIFY_DOUBLE_NORMAL_FIRST BUSTER_CHECK_TEST_FPCLASSIFY_DOUBLE_MODEL("m_=(normal)", "s_=(subnormal)")
#define BUSTER_CHECK_TEST_FPCLASSIFY_DOUBLE_SUBNORMAL_FIRST BUSTER_CHECK_TEST_FPCLASSIFY_DOUBLE_MODEL("s_=(subnormal)", "m_=(normal)")
#define BUSTER_CHECK_TEST_FPCLASSIFY_NORMAL_TWICE \
    "#define __builtin_fpclassify(nan,infinite,normal,subnormal,zero,value) " \
    "__builtin_fpclassify(nan,infinite,((normal),(normal)),subnormal,zero,value)\n"
#define BUSTER_CHECK_TEST_FPCLASSIFY_SUBNORMAL_NEVER \
    "#define __builtin_fpclassify(nan,infinite,normal,subnormal,zero,value) __builtin_fpclassify(nan,infinite,normal,4,zero,value)\n"
#define BUSTER_CHECK_TEST_FPCLASSIFY_ARGUMENT_TWICE \
    "#define __builtin_fpclassify(nan,infinite,normal,subnormal,zero,value) " \
    "__builtin_fpclassify(nan,infinite,normal,subnormal,zero,((value),(value)))\n"

// fpclassify's selector programs against clang's build, and the controls of their encoding: both agree with the model
// that runs the subnormal selector first, and neither with a mutant. With the order-recording encoding, ours must agree
// with the model that runs the normal selector first and differ from the other, so that a change to
// check_lower_floating_prepare's order changes these two controls.
static __attribute__((noinline)) void check_test_program_fpclassify(CheckTest* test, const char* clang)
{
    check_test_program_suite(test, "checker/programs/program-fpclassify", clang);
    check_test_program_against(test, BUSTER_CHECK_TEST_FPCLASSIFY_COUNTS BUSTER_CHECK_TEST_FPCLASSIFY_CATEGORIES,
                               BUSTER_CHECK_TEST_FPCLASSIFY_DOUBLE_SUBNORMAL_FIRST BUSTER_CHECK_TEST_FPCLASSIFY_COUNTS
                               BUSTER_CHECK_TEST_FPCLASSIFY_CATEGORIES,
                               false, "fpclassify-conditional-selector-effects-subnormal-first", clang);
    check_test_program_against(test, BUSTER_CHECK_TEST_FPCLASSIFY_COUNTS BUSTER_CHECK_TEST_FPCLASSIFY_KNOWN_CATEGORY,
                               BUSTER_CHECK_TEST_FPCLASSIFY_DOUBLE_SUBNORMAL_FIRST BUSTER_CHECK_TEST_FPCLASSIFY_COUNTS
                               BUSTER_CHECK_TEST_FPCLASSIFY_KNOWN_CATEGORY,
                               false, "fpclassify-known-category-effects-subnormal-first", clang);
    check_test_program_against(test, BUSTER_CHECK_TEST_FPCLASSIFY_ORDER BUSTER_CHECK_TEST_FPCLASSIFY_CATEGORIES,
                               BUSTER_CHECK_TEST_FPCLASSIFY_DOUBLE_NORMAL_FIRST BUSTER_CHECK_TEST_FPCLASSIFY_ORDER
                               BUSTER_CHECK_TEST_FPCLASSIFY_CATEGORIES,
                               false, "fpclassify-selector-order-normal-first", clang);
    check_test_program_against(test, BUSTER_CHECK_TEST_FPCLASSIFY_ORDER BUSTER_CHECK_TEST_FPCLASSIFY_CATEGORIES,
                               BUSTER_CHECK_TEST_FPCLASSIFY_DOUBLE_SUBNORMAL_FIRST BUSTER_CHECK_TEST_FPCLASSIFY_ORDER
                               BUSTER_CHECK_TEST_FPCLASSIFY_CATEGORIES,
                               true, "fpclassify-selector-order-subnormal-first", clang);
    check_test_program_against(test, BUSTER_CHECK_TEST_FPCLASSIFY_COUNTS BUSTER_CHECK_TEST_FPCLASSIFY_CATEGORIES,
                               BUSTER_CHECK_TEST_FPCLASSIFY_NORMAL_TWICE BUSTER_CHECK_TEST_FPCLASSIFY_COUNTS BUSTER_CHECK_TEST_FPCLASSIFY_CATEGORIES,
                               true, "fpclassify-normal-selector-twice", clang);
    check_test_program_against(test, BUSTER_CHECK_TEST_FPCLASSIFY_COUNTS BUSTER_CHECK_TEST_FPCLASSIFY_CATEGORIES,
                               BUSTER_CHECK_TEST_FPCLASSIFY_SUBNORMAL_NEVER BUSTER_CHECK_TEST_FPCLASSIFY_COUNTS BUSTER_CHECK_TEST_FPCLASSIFY_CATEGORIES,
                               true, "fpclassify-subnormal-selector-never", clang);
    check_test_program_against(test, BUSTER_CHECK_TEST_FPCLASSIFY_COUNTS BUSTER_CHECK_TEST_FPCLASSIFY_KNOWN_CATEGORY,
                               BUSTER_CHECK_TEST_FPCLASSIFY_ARGUMENT_TWICE BUSTER_CHECK_TEST_FPCLASSIFY_COUNTS BUSTER_CHECK_TEST_FPCLASSIFY_KNOWN_CATEGORY,
                               true, "fpclassify-argument-twice", clang);
}

// The warning walk borrows only control marks and the label queue. Poison every suffix and retain facts at every
// capacity remainder, including conservative and proven terminating edges. No graph marks or alignment ledger are
// borrowed by this walk now; only the body's two control bytes and queue entries may change.
static void check_test_program_return_marks(CheckTest* test)
{
    enum { TOKENS = 1024, BASE_CAPACITY = 8 * TOKENS + 16, CAPACITY = BASE_CAPACITY + 3, NODES = 4096 };
    CheckTestTypes types = { 0 };
    Arena scratch = arena_reserve(UINT64_C(1) << 20);
    bool ready = scratch.base != NULL && check_test_types_open(&types);
    const TypeId record = ready ? type_tag(&types.table, BUSTER_TYPE_KIND_STRUCT, 1) : 0;
    const TypeMember member = { .name = 1, .type = BUSTER_TYPE_LONG, .bit_width = BUSTER_TYPE_NOT_BIT_FIELD };
    ready = ready && record != 0 && type_complete_record(&types.table, record, &member, 1, (TypeRecordAttributes){ 0 });
    TypeId function = ready ? type_function(&types.table, record, NULL, 0, 0) : 0;
    uint8_t* facts = BUSTER_ARENA_PUSH_ARRAY(&scratch, uint8_t, NODES + 1);
    uint32_t* labels = BUSTER_ARENA_PUSH_ARRAY(&scratch, uint32_t, CAPACITY + 1);
    CheckControlTask* tasks = BUSTER_ARENA_PUSH_ARRAY(&scratch, CheckControlTask, CAPACITY);
    ready = ready && function != 0 && facts != NULL && labels != NULL && tasks != NULL;
    check_test_expect(test, ready, "return warning fixture has bounded facts and control scratch");
    static const uint8_t contracts[] = { 0, BUSTER_CHECK_RETURN_UNKNOWN, BUSTER_CHECK_RETURN_STOPS };
    for (uint32_t capacity = BASE_CAPACITY; ready && capacity <= CAPACITY; capacity += 1)
    {
        for (uint32_t contract = 0; contract < BUSTER_ARRAY_LENGTH(contracts); contract += 1)
        {
            for (uint32_t node = 0; node <= NODES; node += 1)
            {
                facts[node] = node == 0 ? contracts[contract] : 0x73;
            }
            for (uint32_t label = 0; label <= CAPACITY; label += 1)
            {
                labels[label] = UINT32_C(0x71625344);
            }
            memset(tasks, 0xa5, (uint64_t)CAPACITY * sizeof(*tasks));
            const CheckInput input = { .token_count = TOKENS, .declared_types = &function, .graph = { .count = NODES } };
            CheckProgram program = { .status = CHECK_PROGRAM_OK, .types = &types.table, .input = &input, .body_count = 2 };
            CheckControl entries[] = { { .kind = CHECK_CONTROL_EVALUATE, .node = 0 }, { .kind = CHECK_CONTROL_END, .node = BUSTER_CHECK_NONE } };
            CheckControlWriter writer = { .program = &program, .entries = entries, .tasks = tasks, .labels = labels, .capacity = capacity };
            CheckProgramBody body = { .count = BUSTER_ARRAY_LENGTH(entries), .entry_count = BUSTER_ARRAY_LENGTH(entries) };
            CheckReturnFacts cached = { .facts = facts, .computed = NODES };
            check_program_return_flow(&writer, &body, &cached, true, true);
            bool same = body.return_flow && body.falls && body.return_warning == (contract == 0 ? 1 : 0);
            for (uint32_t node = 0; same && node <= NODES; node += 1)
            {
                same = facts[node] == (node == 0 ? contracts[contract] : 0x73);
            }
            for (uint32_t label = BUSTER_ARRAY_LENGTH(entries); same && label <= CAPACITY; label += 1)
            {
                same = labels[label] == UINT32_C(0x71625344);
            }
            const uint8_t* bytes = (const uint8_t*)tasks;
            for (uint32_t byte = BUSTER_ARRAY_LENGTH(entries); same && byte < (uint64_t)CAPACITY * sizeof(*tasks); byte += 1)
            {
                same = bytes[byte] == 0xa5;
            }
            check_test_expect(test, same, "return warning walks retain immutable facts and every unused control/queue byte at each capacity remainder");
        }
    }
    if (ready)
    {
        // A query returns, as clang's CFG takes a builtin's call (#1393): __builtin_constant_p's operand is visited, so a
        // terminating one ends the path whatever the runtime plan; the object size queries' operands are left out
        // (VisitCallExpr's OmitArguments), so theirs doesn't.
        CheckOp ops[] = { CHECK_OP_BUILTIN_MISC, CHECK_OP_BUILTIN_QUERY };
        uint32_t absent[] = { BUSTER_CHECK_NONE, BUSTER_CHECK_NONE };
        uint32_t members[] = { CBUILTIN_TRAP, CBUILTIN_CONSTANT_P };
        uint32_t offsets[] = { 0, 0 };
        uint32_t counts[] = { 0, 1 };
        uint32_t arguments[] = { 0 };
        CheckBuiltinMiscEffect misc = { .node = 0, .terminates = true };
        CheckBuiltinQueryEffect query = {
            .node = 1,
            .runtime_value = { .type = BUSTER_TYPE_INT, .category = CHECK_RVALUE, .constant_kind = CHECK_CONSTANT_INTEGER,
                               .flags = BUSTER_CHECK_SIDE_EFFECT },
            .evaluated_operands = 1,
            .known = true,
            .source_proof = CHECK_BUILTIN_QUERY_PROOF_ZERO,
            .runtime_plan = CHECK_BUILTIN_QUERY_RUNTIME_EVALUATE_KNOWN,
            .runtime_effects = { .flags = BUSTER_CHECK_SIDE_EFFECT },
        };
        const CheckInput input = { .graph = { .count = BUSTER_ARRAY_LENGTH(ops), .ops = ops, .operand0 = absent,
                                            .operand1 = absent, .operand2 = absent, .members = members,
                                            .argument_offsets = offsets, .argument_counts = counts, .argument_nodes = arguments } };
        const CheckStream stream = { .misc = &misc, .misc_count = 1, .queries = &query, .query_count = 1 };
        CheckProgram program = { .status = CHECK_PROGRAM_OK, .types = &types.table, .input = &input, .stream = &stream };
        for (uint32_t path = 0; path < 4; path += 1)
        {
            const bool size = path >= 2;
            members[1] = size ? CBUILTIN_OBJECT_SIZE : CBUILTIN_CONSTANT_P;
            const uint8_t* query_facts = check_program_return_facts(&program, &scratch, NULL, (path & 1) == 0);
            check_test_expect(test, query_facts != NULL && query_facts[0] == BUSTER_CHECK_RETURN_STOPS &&
                                   query_facts[1] == (size ? 0 : BUSTER_CHECK_RETURN_STOPS),
                              "a query returns: constant_p's terminating operand ends the path, object_size's is left out");
        }
    }
    if (ready)
    {
        // Reader-independent inputs may have a checked function designator without source declaration bindings.
        // Missing optional provenance keeps the call conservative instead of indexing an absent column.
        CheckOp ops[] = { CHECK_OP_VALUE, CHECK_OP_CALL };
        uint32_t tokens[] = { 0, 0 };
        uint32_t first[] = { BUSTER_CHECK_NONE, 0 };
        uint32_t absent[] = { BUSTER_CHECK_NONE, BUSTER_CHECK_NONE };
        CheckValue values[] = { { .type = function, .category = CHECK_FUNCTION, .object = 1 },
                                { .type = record, .category = CHECK_RVALUE } };
        const CheckInput input = { .token_count = 1, .graph = { .count = BUSTER_ARRAY_LENGTH(ops), .ops = ops,
                                       .tokens = tokens, .operand0 = first, .operand1 = absent, .operand2 = absent } };
        const CheckStream stream = { .dense_values = values };
        const CheckProgram program = { .status = CHECK_PROGRAM_OK, .types = &types.table, .input = &input,
                                       .stream = &stream, .object_count = 2 };
        check_test_expect(test, check_program_return_call(&program, 1, NULL, NULL) == BUSTER_CHECK_RETURN_UNKNOWN,
                          "a call without optional source declaration bindings remains conservative");
    }

    arena_release(&scratch);
    check_test_types_close(&types);
}

// #1709: the return flow reads a byte per interned name instead of searching the catalogue for each call. Its table is
// exactly the catalogue's entries with the 'r' attribute, and over every catalogue name, near misses and the
// predefined names, the bytes equal the search's answers.
static void check_test_program_return_library(CheckTest* test)
{
    uint32_t matched = 0;
    bool ordered = true;
    for (uint32_t i = 0; i < BUSTER_BUILTIN_INTRINSIC_COUNT; i += 1)
    {
        const BuiltinIntrinsicInfo* info = builtin_intrinsic_infos + i;
        const bool listed = memchr(info->attributes, 'r', info->attributes_length) != NULL;
        const BuiltinIntrinsicName* entry = listed && matched < BUSTER_ARRAY_LENGTH(check_program_return_library_entries) ?
                                            check_program_return_library_entries + matched : NULL;
        ordered = ordered && (!listed || (entry != NULL && entry->length == info->name_length && memcmp(entry->name, info->name, entry->length) == 0));
        matched += listed ? 1 : 0;
    }
    check_test_expect(test, ordered && matched == BUSTER_ARRAY_LENGTH(check_program_return_library_entries),
                      "the return flow's library names are exactly the catalogue's entries with the 'r' attribute");
    InternTable identifiers = { 0 };
    Arena scratch = arena_reserve(UINT64_C(1) << 20);
    bool ready = scratch.base != NULL && intern_initialize(&identifiers);
    static const char* const misses[] = { "exi", "exits", "abort_", "longjm", "_Exit_", "main", "" };
    for (uint32_t i = 0; ready && i < BUSTER_BUILTIN_INTRINSIC_COUNT; i += 1)
    {
        ready = intern_find_or_add(&identifiers, (const uint8_t*)builtin_intrinsic_infos[i].name, builtin_intrinsic_infos[i].name_length) != 0;
    }
    for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(misses) - 1; i += 1)
    {
        ready = intern_find_or_add(&identifiers, (const uint8_t*)misses[i], (uint32_t)strlen(misses[i])) != 0;
    }
    const uint8_t* names = ready ? check_program_return_library_names(&identifiers, &scratch) : NULL;
    check_test_expect(test, names != NULL, "the return flow's library-name fixture interns the catalogue");
    uint32_t equal = 0;
    uint32_t unknown = 0;
    for (uint32_t id = 1; names != NULL && id < identifiers.count; id += 1)
    {
        equal += names[id] == check_program_return_library_search(&identifiers, id) ? 1 : 0;
        unknown += names[id] == BUSTER_CHECK_RETURN_UNKNOWN ? 1 : 0;
    }
    check_test_expect(test, names != NULL && equal == identifiers.count - 1 && unknown == BUSTER_ARRAY_LENGTH(check_program_return_library_entries) && names[0] == 0,
                      "each interned name's return-flow byte equals the catalogue search's answer");
    Arena empty = { 0 };
    check_test_expect(test, check_program_return_library_names(NULL, &scratch) == NULL && check_program_return_library_names(&identifiers, &empty) == NULL,
                      "without identifiers or room the return flow searches, as before");
    intern_release(&identifiers);
    arena_release(&scratch);
}

#include "c_check_program_batch_test.c"
#include "c_check_literal_table_test.c"
#include "c_check_power_divmod_test.c"
#include "c_check_constant_conversion_test.c"
#include "c_check_branch_condition_test.c"

static void check_test_program_many_arguments(CheckTest* test, const char* clang)
{
    static const uint32_t counts[] = { 65, 127, 130, 238, 239 };
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(counts); c += 1)
    {
        char* source = NULL;
        size_t length = 0;
        FILE* text = open_memstream(&source, &length);
        check_test_expect(test, text != NULL, "large call source has a bounded output stream");
        if (text != NULL)
        {
            fprintf(text, "long many(");
            for (uint32_t a = 0; a < counts[c]; a += 1)
            {
                fprintf(text, "%slong a%u", a != 0 ? "," : "", a);
            }
            fprintf(text, "){return ");
            for (uint32_t a = 0; a < counts[c]; a += 1)
            {
                fprintf(text, "%sa%u*%u", a != 0 ? "+" : "", a, a + 1);
            }
            fprintf(text, ";}int tested(int n){");
            if (counts[c] <= 238)
            {
                // Keep the direct result in a local: holding it on the value stack would exhaust the indirect limit.
                fprintf(text, "__typeof__(many)*volatile p=many;long direct=many(");
            }
            else
            {
                fprintf(text, "return (int)(many(");
            }
            for (uint32_t a = 0; a < counts[c]; a += 1)
            {
                if (counts[c] <= 127)
                {
                    fprintf(text, "%s(long)n+%u", a != 0 ? "," : "", a);
                }
                else
                {
                    fprintf(text, "%s(long)n", a != 0 ? "," : "");
                }
            }
            if (counts[c] <= 238)
            {
                fprintf(text, ");long indirect=p(");
                for (uint32_t a = 0; a < counts[c]; a += 1)
                {
                    if (counts[c] <= 127)
                    {
                        fprintf(text, "%s(long)n-%u", a != 0 ? "," : "", a);
                    }
                    else
                    {
                        fprintf(text, "%s(long)n", a != 0 ? "," : "");
                    }
                }
                fprintf(text, ");");
                if (counts[c] >= 130)
                {
                    // A wide call must refill the live prefix, even when its compact negative stack effect wraps.
                    fprintf(text, "long held=(long)n-many(");
                    for (uint32_t a = 0; a < counts[c]; a += 1)
                    {
                        fprintf(text, "%s(long)n", a != 0 ? "," : "");
                    }
                    fprintf(text, ");return (int)((direct+indirect+held)%%1000003);}");
                }
                else
                {
                    fprintf(text, "return (int)((direct+indirect)%%1000003);}");
                }
            }
            else
            {
                fprintf(text, ")%%1000003);}");
            }
            const bool written = fclose(text) == 0;
            check_test_expect(test, written, "large call source completes without truncation");
            if (written)
            {
                char name[80];
                snprintf(name, sizeof(name), "calls-of-%u-arguments", counts[c]);
                check_test_program_source(test, source, name, clang);
            }
        }
        free(source);
    }
    char* variadic = NULL;
    size_t variadic_length = 0;
    FILE* text = open_memstream(&variadic, &variadic_length);
    check_test_expect(test, text != NULL, "127-argument variadic source has output storage");
    if (text != NULL)
    {
        fputs("struct P{float a,b,c,d;};long var(int count,...){__builtin_va_list ap;__builtin_va_start(ap,count);long r=0;"
              "for(int i=0;i<count;i++){r+=__builtin_va_arg(ap,long);r+=(long)__builtin_va_arg(ap,double);"
              "struct P p=__builtin_va_arg(ap,struct P);r+=(long)(p.a+p.b+p.c+p.d);}__builtin_va_end(ap);return r;}"
              "int tested(int n){struct P p={n,n+1,n+2,n+3};return (int)(var(42", text);
        for (uint32_t a = 0; a < 42; a += 1)
        {
            fprintf(text, ",(long)n+%u,(double)n*0.25+%u,p", a, a);
        }
        fputs(")%1000003);}", text);
        const bool written = fclose(text) == 0;
        check_test_expect(test, written, "127-argument variadic source completes without truncation");
        if (written)
        {
            check_test_program_source(test, variadic, "variadic-127-mixed-aggregate-arguments", clang);
        }
    }
    free(variadic);
    check_test_program_suite(test, "checker/programs/program-many-arguments", clang);
}

// _BitInt values narrower than their classes (#148): kept extended from their widths, a narrow one's operations computed
// in 32 bits, against clang -O0. Unsigned ones are spelled out, since a typedef of one loses its unsignedness on main.

static __attribute__((noinline)) void check_test_program_bit_ints(CheckTest* test, const char* clang)
{
    check_test_program_suite(test, "checker/programs/program-bit-ints", clang);
}

static void check_test_program_many_aggregate_returns(CheckTest* test, const char* clang)
{
    for (uint32_t bodies = 64; bodies <= 128; bodies *= 2)
    {
        char* source = NULL;
        size_t length = 0;
        FILE* text = open_memstream(&source, &length);
        check_test_expect(test, text != NULL, "many aggregate bodies have bounded source output storage");
        if (text != NULL)
        {
            fputs("struct S{long a,b;};", text);
            for (uint32_t b = 0; b < bodies; b += 1)
            {
                fprintf(text, "struct S f%u(int n,struct S s){if(n)return s;}", b);
            }
            fprintf(text, "int tested(int n){struct S s={n,n+1};struct S a=f0(1,s),b=f%u(1,s);return a.a+a.b+b.a+b.b;}", bodies - 1);
            const bool written = fclose(text) == 0;
            check_test_expect(test, written, "many aggregate bodies complete without truncation");
            if (written)
            {
                check_test_program_source(test, source, "many-aggregate-return-bodies", clang);
            }
        }
        free(source);
    }
}

static void check_test_program_builtin_scratch(CheckTest* test, const char* clang)
{
    for (uint32_t limit = CBUILTIN_LOWER_LIMIT_RECIPE; limit <= CBUILTIN_LOWER_LIMIT_LOCALS; limit += 1)
    {
        CheckProgramLowered output = { 0 };
        CheckLowerWriter writer = { .output = &output, .token = 17 };
        const CBuiltinLowerBuffer buffer = { .capacity = BUSTER_TIER0_MAX_ENTRIES, .local_capacity = BUSTER_TIER0_MAX_LOCALS };
        const CBuiltinLowerResult result = { .error = CBUILTIN_LOWER_CAPACITY, .limit = (CBuiltinLowerLimit)limit };
        const char* expected = limit == CBUILTIN_LOWER_LIMIT_RECIPE ? "builtin recipe limit" : limit == CBUILTIN_LOWER_LIMIT_ENTRIES ?
                               "Tier 0 per-function entry limit" : "Tier 0 per-function local limit";
        check_lower_builtin_finish(&writer, &buffer, result, BUSTER_TYPE_INT);
        check_lower_resource(&writer, "a later failure");
        check_test_expect(test, output.status == CHECK_PROGRAM_RESOURCE && output.token == 17 && output.reason != NULL && strcmp(output.reason, expected) == 0,
                          "builtin resource failures name the actual first limit");
    }
    static const char* unary[] = {
        "__builtin_bswap16((unsigned short)x)",
        "__builtin_bswap32(x)",
        "__builtin_bswap64(z)",
        "__builtin_clz(x)",
        "__builtin_clzl(z)",
        "__builtin_clzll(z)",
        "__builtin_ctz(x)",
        "__builtin_ctzl(z)",
        "__builtin_ctzll(z)",
        "__builtin_popcount(x)",
        "__builtin_popcountl(z)",
        "__builtin_popcountll(z)",
        "__builtin_parity(x)",
        "__builtin_parityl(z)",
        "__builtin_parityll(z)",
        "__builtin_ffs((int)x)",
        "__builtin_ffsl((long)z)",
        "__builtin_ffsll((long long)z)",
    };
    char source[32768];
    uint32_t used = (uint32_t)snprintf(source, sizeof(source),
        "int other(int n){unsigned long a;return __builtin_add_overflow((unsigned long)n,3UL,&a)+(int)(a%%97);}"
        "int tested(int n){unsigned x=(unsigned)n|1;unsigned long z=((unsigned long)x<<33)|x;unsigned y=0;");
    for (uint32_t repeat = 0; repeat < 8; repeat += 1)
    {
        for (uint32_t id = 0; id < BUSTER_ARRAY_LENGTH(unary); id += 1)
        {
            used += (uint32_t)snprintf(source + used, sizeof(source) - used, "y+=(unsigned)(%s)%%97;", unary[id]);
        }
        static const char* operation[] = { "add", "sub", "mul" };
        for (uint32_t id = 0; id < BUSTER_ARRAY_LENGTH(operation); id += 1)
        {
            used += (uint32_t)snprintf(source + used, sizeof(source) - used,
                "{signed char r;unsigned __int128 w;"
                "y+=__builtin_%s_overflow((long)n,z,&r);y+=(unsigned char)r;"
                "y+=__builtin_%s_overflow((__int128)n*((__int128)1<<64),(unsigned __int128)z,&w);y+=(unsigned)(w%%101);}", operation[id], operation[id]);
        }
    }
    snprintf(source + used, sizeof(source) - used, "return (int)(y%%1000000007)+other(n);}");
    check_test_program_source_bounds(test, source, "repeated-integer-recipes-reuse-locals", clang, false, -1, 0, 160);
    check_test_program_source_bounds(test,
        "int tested(int n){unsigned long a=0,b=0,c=0;unsigned long x=(unsigned long)(unsigned)n*0x100000001UL;"
        "int r=__builtin_add_overflow(x,x+17,(__builtin_mul_overflow(x,x,&b),&a));"
        "int s=__builtin_sub_overflow(x,__builtin_add_overflow(x,19UL,&c),&b);"
        "unsigned t=__builtin_popcount((unsigned)n)+__builtin_clz((unsigned)n|1)+__builtin_parityll(x);"
        "return (int)((a%97)+(b%101)+(c%103))+r*1000+s*2000+(int)t;}",
        "nested-operand-destination-and-sibling-result-lifetimes", clang, false, -1, 0, 80);
    check_test_program_source_bounds(test,
        "int tested(int n){__int128 a=0,c=0;unsigned __int128 b=0;"
        "__int128 x=(__int128)n*0x1000000000000000UL;unsigned __int128 y=(unsigned __int128)(unsigned)n<<96;"
        "int r=__builtin_mul_overflow(x,(__builtin_sub_overflow(y,(unsigned __int128)17,&b),x+1),"
        "(__builtin_add_overflow(x,x,&c),&a));"
        "int s=__builtin_add_overflow(y,(__builtin_mul_overflow(x,x,&c),(unsigned __int128)c),&b);"
        "return (int)((unsigned __int128)a%97+b%101+(unsigned __int128)c%103)+r*1000+s*2000;}",
        "nested-wide-operand-objects-and-signedness", clang, false, -1, 0, 96);
    check_test_program_suite(test, "checker/programs/program-builtin-scratch", clang);
}

// #978: arrays of 16 bytes or more aligned to 16, as the x86-64 psABI and clang align them, a char before each so that
// an alignment of 1 would misalign them: global, initialized, two-dimensional, static, record arrays, a thread-local and a
// static local one, and in the frame a local array, a two-dimensional one, an array of records and a VLA; an attribute's
// 4 alone; and __alignof__ of such an array still its element's (clang's getDeclAlign for alignof).
static void check_test_program_array_alignment(CheckTest* test, const char* clang)
{
    check_test_program_suite(test, "checker/programs/program-array-alignment", clang);
}

static void check_test_program_many_declarators(CheckTest* test, const char* clang)
{
    char source[32768];
    uint32_t length = 0;
    bool valid = true;
    for (uint32_t kind = 0; valid && kind < 2; kind += 1)
    {
        const int prefix = snprintf(source + length, sizeof(source) - length, "%s", kind == 0 ? "static int " : "int tested(int n){int ");
        valid = prefix > 0 && (uint32_t)prefix < sizeof(source) - length;
        length += valid ? (uint32_t)prefix : 0;
        for (uint32_t i = 0; valid && i < 257; i += 1)
        {
            const int written = snprintf(source + length, sizeof(source) - length, "%s%c%u=%s%u", i != 0 ? "," : "", kind == 0 ? 'g' : 'a', i,
                                         kind == 0 ? "" : "n+", i);
            valid = written > 0 && (uint32_t)written < sizeof(source) - length;
            length += valid ? (uint32_t)written : 0;
        }
        const int suffix = valid ? snprintf(source + length, sizeof(source) - length, "%s", kind == 0 ? ";" : ";return a0+a128+a256+g0+g128+g256;}") : -1;
        valid = suffix > 0 && (uint32_t)suffix < sizeof(source) - length;
        length += valid ? (uint32_t)suffix : 0;
    }
    check_test_expect(test, valid, "257 file-scope and local declarators fit the generated source buffer");
    if (valid)
    {
        check_test_program_source(test, source, "many-declarators", clang);
    }
}

// #1847: the lowering's library functions that return twice (check_lower_twice_entries), whose callers Tier 1 leaves to
// Tier 0, are exactly the catalogue's entries with the 'j' attribute but __builtin_setjmp, in its order.
static void check_test_program_twice_library(CheckTest* test)
{
    uint32_t matched = 0;
    bool ordered = true;
    for (uint32_t i = 0; i < BUSTER_BUILTIN_INTRINSIC_COUNT; i += 1)
    {
        const BuiltinIntrinsicInfo* info = builtin_intrinsic_infos + i;
        const bool own = info->name_length == sizeof("__builtin_setjmp") - 1 && memcmp(info->name, "__builtin_setjmp", info->name_length) == 0;
        const bool listed = !own && memchr(info->attributes, 'j', info->attributes_length) != NULL;
        const BuiltinIntrinsicName* entry = listed && matched < BUSTER_CHECK_LOWER_TWICE_NAMES ? check_lower_twice_entries + matched : NULL;
        ordered = ordered && (!listed || (entry != NULL && entry->length == info->name_length && memcmp(entry->name, info->name, entry->length) == 0));
        matched += listed ? 1 : 0;
    }
    check_test_expect(test, ordered && matched == BUSTER_CHECK_LOWER_TWICE_NAMES,
                      "the lowering's returns-twice library names are exactly the catalogue's entries with the 'j' attribute but __builtin_setjmp");
}

static __attribute__((noinline)) void check_test_programs(CheckTest* test, const char* clang)
{
    if (check_test_takes())
    {
        check_test_program_array_alignment(test, clang);
        check_test_program_return_marks(test);
        check_test_program_return_library(test);
        check_test_program_twice_library(test);
        check_test_literal_tables(test, clang);
        check_test_program_builtin_scratch(test, clang);
        check_test_program_copies(test, clang);
        check_test_program_folds(test, clang);
        check_test_program_fences(test, clang);
        check_test_program_atomic_accesses(test, clang);
        check_test_program_atomic_rmws(test, clang);
        check_test_program_batch(test);
        check_test_power_divmod(test, clang);
        check_test_constant_conversions(test, clang);
        check_test_branch_conditions(test, clang);
        check_test_program_many_arguments(test, clang);
        check_test_program_many_aggregate_returns(test, clang);
        check_test_program_suite(test, "checker/programs/implemented-feature-query-native", clang);
        check_test_program_unnamed_parameters(test, clang);
        // The independent Clang build selects these effects too; a stale zero must change observable behavior.
        check_test_program_suite(test, "checker/programs/cleanup-feature-query-native", clang);
    }
    if (check_test_takes())
    {
        check_test_program_suite(test, "checker/programs/overloadable-feature-query-native", clang);
        check_test_program_long_double_builtins(test, clang);
        check_test_program_bit_ints(test, clang);
        check_test_control_masks(test);
        check_test_control_walks(test);
        check_test_program_addressed(test);
        check_test_program_lifetime_planning(test);
        check_test_program_lifetime_jumps(test);
        check_test_program_scans(test);
        check_test_program_many_declarators(test, clang);
        check_test_program_suite(test, "checker/programs/multi-declarator-plan-scopes", clang);
        check_test_lower_opening_owner(test);
        check_test_program_resolve_labels(test);
        check_test_lower_calls_twice(test);
        check_test_program_result_lanes(test, clang);
        check_test_program_bodies(test, clang);
        // C89's implicit int in the runtime program harness (#1121): the declarations' types and qualifiers decide what
        // `tested` computes, and clang's build of the same source in the same mode is the oracle the executable compares.
        static const char implicit_int_source[] =
            "x = 3;\n"
            "typedef t;\n"
            "t y = 1;\n"
            "int q = 7;\n"
            "*const p = &q;\n"
            "*__restrict r = &q;\n"
            "*__attribute__((unused)) a = &q;\n"
            "struct S { const x; };\n"
            "int tested(int n)\n"
            "{\n"
            "    return x == 3 && sizeof(x) == sizeof(int) && y == 1 && sizeof(y) == sizeof(int) && q == 7 && *p == 7 && *a == 7 &&\n"
            "           *r == 7 && p == &q && a == &q && r == &q && sizeof(*p) == sizeof(int) && sizeof(const) == sizeof(int) &&\n"
            "           sizeof(volatile) == sizeof(int) && sizeof(struct S) == sizeof(int) && (const)1 == 1 ? 0 : 1;\n"
            "}\n";
        check_test_program_source_standard(test, implicit_int_source, "implicit-int-c89", clang, 1989, false, "-std=c89");
        check_test_program_source_standard(test, implicit_int_source, "implicit-int-gnu89", clang, 1989, true, "-std=gnu89");
        // The issue's whole old-style program, run (#1121): a file-scope static and `helper`'s old-style definition
        // read implicit int, `tested`'s `n` is the undeclared identifier-list kind and `i` a block-scope register —
        // the call and the declarations' types decide the value clang's build of the same source computes.
        static const char implicit_int_program_source[] =
            "static x = 3;\n"
            "helper(a) int a; { return a + 1; }\n"
            "tested(n) { register i; i = helper(x + n); return i; }\n";
        check_test_program_source_standard(test, implicit_int_program_source, "implicit-int-program-c89", clang, 1989, false, "-std=c89");
        check_test_program_source_standard(test, implicit_int_program_source, "implicit-int-program-gnu89", clang, 1989, true, "-std=gnu89");
        // Prototype parameters without a type specifier (#1121, after #1240's parser): a pointer declarator, a const, a
        // register and a volatile object, and a pointer after a typed parameter, whose declaration the definition's
        // `int *` must match — each parameter's type decides the value clang's build computes.
        static const char implicit_int_parameter_source[] =
            "static f(*p) { return *p + (int)sizeof(*p); }\n"
            "static g(const x) { return x * 2; }\n"
            "static h(register r) { return r + 1; }\n"
            "static m(const *p, volatile v) { return *p - v; }\n"
            "int k(int a, *p);\n"
            "int k(int a, int *p) { return a + *p; }\n"
            "int tested(int n) { int v = 5; return f(&v) + g(n) + h(n) + m(&v, n) + k(n, &v); }\n";
        check_test_program_source_standard(test, implicit_int_parameter_source, "implicit-int-parameters-c89", clang, 1989, false, "-std=c89");
        check_test_program_source_standard(test, implicit_int_parameter_source, "implicit-int-parameters-gnu89", clang, 1989, true, "-std=gnu89");
    }
    if (check_test_takes())
    {
        check_test_program_declaring(test);
        check_test_program_refused(test);
        check_test_program_clone_parts(test);
        check_test_program_sparse_switches(test);
        check_test_program_type_facts(test);
        check_test_program_selection(test, clang);
        check_test_program_overloads(test, clang);
        check_test_program_queries(test, clang);
        check_test_program_region_proofs(test, clang);
        check_test_program_helpers(test);
        check_test_program_atomics(test, clang);
        check_test_program_diagnostics(test, clang);
        check_test_program_vectors(test, clang);
        check_test_program_x86(test, clang);
    }
    if (check_test_takes())
    {
        check_test_program_x86_rest(test, clang);
        check_test_program_amx(test, clang);
        check_test_program_transparent(test, clang);
        check_test_program_union_casts(test, clang);
        check_test_program_aliases(test, clang);
        check_test_program_vector_builtins(test, clang);
        check_test_program_fp16(test, clang);
        check_test_program_bf16(test, clang);
        check_test_program_halves(test, clang);
        check_test_program_suite(test, "checker/programs/structured-loops", clang);
    }
    if (check_test_takes())
    {
        check_test_program_suite(test, "checker/programs/external-variadic-call-abi", clang);
    }
    // Plain char unsigned (-funsigned-char) and signed, from one source: stores and loads of values past 0x7f,
    // conversions, division, shifts and compares, a switch, character constants, and a char parameter and result.
    static const char plain_char[] =
        "static char table[]={'\\x80','\\xff','a','\\x7f',0};static char pass(char c){return c;}static int widen(char c){return c;}"
        "int tested(int n){char c=(char)(n*37+128);int r=c/3+(c>>1)+(c>100)+widen(pass(c))+'\\xff'+(char)-1;"
        "r+=table[(unsigned)n%5]+(table[(unsigned)n%5]<0)*1000;switch(c){case '\\xff':r+=7;break;case '\\x80':r+=11;break;default:break;}"
        "table[(unsigned)n%5]=c;return r;}";
    if (check_test_takes())
    {
        check_test_program_source_char(test, plain_char, "plain-char-unsigned", clang, true);
        check_test_program_source_char(test, plain_char, "plain-char-signed", clang, false);
        check_test_program_suite(test, "checker/programs/shared-code-and-data", clang);
    }
    if (check_test_takes())
    {
        check_test_program_suite(test, "checker/programs/switch-table-long-ends", clang);
    }
    // A table of 300 values with holes, every seventh value the default's.
    char big[16384];
    int length = snprintf(big, sizeof(big), "static int big(int x){switch(x){");
    for (int k = 0; k < 300 && length > 0 && (size_t)length < sizeof(big); k += 1)
    {
        length += k % 7 == 3 ? 0 : snprintf(big + length, sizeof(big) - (size_t)length, "case %d:return %d;", k, (k * k) % 1000 + 1);
    }
    length += length > 0 && (size_t)length < sizeof(big) ?
              snprintf(big + length, sizeof(big) - (size_t)length,
                       "default:return -7;}}int tested(int n){unsigned s=0;for(int i=-5;i<305;i++)s=s*31+(unsigned)big(i+n);return (int)s;}") : 0;
    check_test_expect(test, length > 0 && (size_t)length < sizeof(big), "the 300-value switch's source fits");
    if (check_test_takes())
    {
        check_test_program_source(test, big, "switch-table-300", clang);
        check_test_program_tables(test, big, "switch-table-300-tables", 1, 300);
        // The rule's edges: four clusters or more, a tenth of the range, cases at one place one cluster (`case 1: case 2:`, as
        // LLVM's one block), classes of 32 and 64 bits, ranges of BUSTER_CHECK_LOWER_MAX_TABLE values at most.
        check_test_program_tables(test, "int f(int x){switch(x){case 0:return 3;case 1:return 5;case 2:return 7;case 3:return 11;case 5:return 13;"
                                        "case 7:return 17;default:return -1;}}", "switch-tables-dense", 1, 8);
        check_test_program_tables(test, "int f(int x){switch(x){case 0:return 3;case 1:return 5;case 2:return 7;default:return -1;}}", "switch-tables-three", 0, 0);
        check_test_program_tables(test, "int f(int x){switch(x){case 0:return 3;case 13:return 5;case 26:return 7;case 39:return 9;default:return -1;}}",
                                  "switch-tables-tenth", 1, 40);
        check_test_program_tables(test, "int f(int x){switch(x){case 0:return 3;case 14:return 5;case 28:return 7;case 42:return 9;default:return -1;}}",
                                  "switch-tables-sparse", 0, 0);
        check_test_program_tables(test, "int f(int x){switch(x){case 1:case 2:case 3:case 4:return 1;default:return 0;}}", "switch-tables-one-place", 0, 0);
        check_test_program_tables(test, "int f(int x){switch(x){case 1:case 2:return 1;case 3:case 4:return 2;case 5:return 3;case 6:return 4;case 7:return 5;default:return 0;}}",
                                  "switch-tables-shared-places", 1, 7);
        check_test_program_tables(test, "int f(__int128 x){switch(x){case 0:return 3;case 1:return 5;case 2:return 7;case 3:return 11;default:return -1;}}",
                                  "switch-tables-int128", 0, 0);
        check_test_program_tables(test, "int f(int x){switch(x){case 0 ... 70000:return 1;case 70001:return 2;case 70002:return 3;case 70003:return 4;default:return 0;}}",
                                  "switch-tables-past-the-most", 0, 0);
        check_test_program_tables(test, "int f(int x){switch(x){case 0 ... 65531:return 1;case 65532:return 2;case 65533:return 3;case 65534:return 4;"
                                        "case 65535:return 5;default:return 0;}}", "switch-tables-the-most", 1, 65536);
        check_test_program_tables(test, "int f(int a,int b){int r=0;switch(a){case 0:switch(b){case 0:r=1;break;case 1:r=2;break;case 2:r=3;break;case 3:r=4;break;"
                                        "default:r=5;}break;case 1:r=10;break;case 2:r=20;case 3:r+=30;break;default:r=-1;}return r;}", "switch-tables-nested", 2, 8);
    }
    // Empty GNU ranges (#536's review), which match nothing and which clang drops before LLVM sees the switch: last by
    // their lower values, where one sized the table short of the case below it and its entry was written past the table's
    // end; first, below the table's lowest value; as the fourth case, which they don't make; and an unsigned one past
    // INT32_MAX. Then the same switches swept past their cases on both sides against clang's build.
    if (check_test_takes())
    {
        check_test_program_tables(test, "int f(int x){switch(x){case 0:return 3;case 1:return 5;case 2:return 7;case 4:return 9;case 5 ... 3:return 11;default:return -1;}}",
                                  "switch-tables-empty-range-last", 1, 5);
        check_test_program_tables(test, "int f(int x){switch(x){case -5 ... -9:return 1;case 0:return 3;case 1:return 5;case 2:return 7;case 3:return 9;default:return -1;}}",
                                  "switch-tables-empty-range-first", 1, 4);
        check_test_program_tables(test, "int f(int x){switch(x){case 0:return 3;case 1:return 5;case 2:return 7;case 9 ... 4:return 9;default:return -1;}}",
                                  "switch-tables-empty-range-fourth", 0, 0);
        check_test_program_tables(test, "int f(unsigned x){switch(x){case 4294967295u ... 0u:return 1;case 7:return 3;case 8:return 5;case 9:return 7;case 11:return 9;"
                                        "default:return -1;}}", "switch-tables-empty-range-unsigned", 1, 5);
        check_test_program_suite(test, "checker/programs/switch-table-empty-ranges", clang);
    }
    if (check_test_takes())
    {
        check_test_program_suite(test, "checker/programs/aggregate-data-body-addresses", clang);
    }
    if (check_test_takes())
    {
        check_test_program_suite(test, "checker/programs/bit-field-initializers-and-neighbors", clang);
        check_test_program_bit_units(test, clang);
        check_test_program_suite(test, "checker/programs/integer-builtin-runtime", clang);
    }
    if (check_test_takes())
    {
        check_test_program_suite(test, "checker/programs/vla-loop-lifetime-recaptures", clang);
    }
    // Parameters of types aligned past 8 bytes: objects of their alignment, from a register's part or an 8-byte stack slot
    // (an aligned typedef passes as its base, so a slot is aligned to 8 alone), called at two stack depths 16 bytes apart.
    if (check_test_takes())
    {
        check_test_program_suite(test, "checker/programs/parameters-aligned-past-8", clang);
        // A caller's frame, which clang -O0's code finds through the frame pointers' chain, is refused by name.
        check_test_program_boundary_reason(test, "int tested(int n){return (__builtin_frame_address(1)!=0)+n;}", "frame-address-caller", true, false, 0, 0,
                                           "__builtin_frame_address and __builtin_return_address of a caller's frame");
    }
    if (check_test_takes())
    {
        check_test_program_boundary_reason(test, "int tested(int n){return (__builtin_return_address(2)!=0)+n;}", "return-address-caller", true, false, 0, 0,
                                           "__builtin_frame_address and __builtin_return_address of a caller's frame");
        check_test_program_suite(test, "checker/programs/floating-builtin-runtime-classification", clang);
        check_test_program_fpclassify(test, clang);
        // K&R definitions (C17 6.9.1, #154's decision: as clang): their declaration lists' types, the parameters' order the
        // identifier list's, a struct and a register parameter, a call through the prototype of their promoted types, which
        // converts its arguments as clang's does, and an empty list's definition after its declaration.
        check_test_program_suite(test, "checker/programs/kr-definitions", clang);
        check_test_program_source_char(test,
            "int f(x,y)char x;volatile unsigned char y;{char *p=&x;*p+=1;y+=2;return x+y;}"
            "int tested(int n){return f(128+(n&127),257);}", "kr-promoted-unsigned-char", clang, true);
        check_test_program_suite(test, "checker/programs/kr-float-entry-rounding-and-stack", clang);
    }
    // _Atomic parameters are not subject to the ordinary parameter promotions. Keep their ABI types and storage
    // padding alongside a promoted ordinary float, and their declared objects addressable for atomic accesses.
    if (check_test_takes())
    {
        check_test_program_suite(test, "checker/programs/kr-atomic-and-promoted-parameters", clang);
    }
    if (!check_test_program_daz())
    {
        printf("  fenv classification: DAZ toggle skipped (MXCSR does not support it)\n");
    }
    if (check_test_takes())
    {
        check_test_program_suite(test, "checker/programs/fenv-strict-classification-exceptions-and-daz", clang);
    }
    if (check_test_takes())
    {
        check_test_program_suite(test, "checker/programs/math-copysign-runtime-prototype-narrowing", clang);
    }
    if (check_test_takes())
    {
        check_test_program_suite(test, "checker/programs/ignored-excess-initializer-does-not-execute", clang);
        // Clang 23 accepts this outer-loop context but its generated break targets the inner while (10/10/14).
        // GNU's specified outer-loop target and GCC give 4/4/6 for n=-1/0/1; keep this focused independent oracle.
        check_test_program_source(test,
            "int tested(int n){int x=0;for(int i=0;i<5;i++){while(({if(i==2)break;n>0;})){x++;break;}x+=2;}return x;}",
            "statement-region-loop-condition-escape-gcc", "gcc");
        check_test_program_suite(test, "checker/programs/nested-statement-regions-and-loop", clang);
        // The same in a while condition, whose break and continue are its enclosing loop's: GCC's, as above.
        check_test_program_source(test,
            "int tested(int n){int x=0;for(int i=0;i<6;i++){int k=0;"
            "while(({int c=({if(i==2)continue;k<3+(n&1);});c;})){k++;x+=({({if(k==2&&i==4)break;k;});});}"
            "x+=k*10+({int s=0;while(({({if(s>2+(n&1)&&i==4)break;s<3;});})){s++;}s;});}return x;}",
            "statement-regions-nested-loop-condition-targets-gcc", "gcc");
        // Variable-length arrays of several bounds in nested blocks, a loop's, a declaration of several, one of 31 bounds
        // and one in a statement expression that a break leaves (#2995: their statements found by walks that share paths).
        check_test_program_suite(test, "checker/programs/vla-several-bounds-nested-blocks", clang);
        // The checker sources' deep shapes of both (#2995), whose walks the program harness counts: GCC's for the break in
        // a while condition, as above.
        check_test_program_checker_source(test, "quadratic-statement-expressions", "gcc");
        check_test_program_checker_source(test, "quadratic-vla-bounds", clang);
        check_test_program_checker_source(test, "quadratic-switches", clang);
        check_test_program_suite(test, "checker/programs/statement-region-floating-pending-value", clang);
    }
    if (check_test_takes())
    {
        check_test_program_suite(test, "checker/programs/statement-region-void-and-typeof", clang);
    }
    // Bit-fields of the 128-bit types, whose bits stay within 8 bytes: read, stored, updated and initialized as others are.
    if (check_test_takes())
    {
        check_test_program_suite(test, "checker/programs/int128-bit-fields", clang);
    }
    // _Complex __float128 (#137): a frame object of two __float128s, whose parts are values as a __float128 is. Each part's
    // sum, difference, negation, product with and quotient by a real value is __float128's (libgcc's __addtf3 ...), a
    // product and a quotient of complex values libgcc's __multc3 and __divtc3, a real operand kept real; equality and
    // truth by parts, conversions to and from the other complex and real types, compound assignments of complex and real
    // destinations, a global, and calls passing and returning it in memory, va_arg among them. It is clean under clang's
    // UBSan on every input.
    if (check_test_takes())
    {
        check_test_program_suite(test, "checker/programs/complex-float128", clang);
        {
            // Values past Tier 0's depth tables (tier0_select_deep): a call of 40 unsigned long and 24 double arguments, the
            // most a call takes (BUSTER_TIER0_MAX_CALL_ARGUMENTS), each an expression evaluated over the arguments before it,
            // with ?:, a division, a shift and a call among the later ones; a call through a pointer of 18; and a product
            // nested 24 deep. Run against clang's.
            char source[8192];
            uint32_t length = 0;
            length += (uint32_t)snprintf(source + length, sizeof(source) - length, "unsigned long many(");
            for (uint32_t i = 0; i < 40; i += 1)
            {
                length += (uint32_t)snprintf(source + length, sizeof(source) - length, "unsigned long a%u,", i);
            }
            for (uint32_t i = 0; i < 24; i += 1)
            {
                length += (uint32_t)snprintf(source + length, sizeof(source) - length, "double d%u%s", i, i + 1 < 24 ? "," : "){unsigned long s=0;double t=0;");
            }
            for (uint32_t i = 0; i < 40; i += 1)
            {
                length += (uint32_t)snprintf(source + length, sizeof(source) - length, "s=s*31+a%u;", i);
            }
            for (uint32_t i = 0; i < 24; i += 1)
            {
                length += (uint32_t)snprintf(source + length, sizeof(source) - length, "t=t*1.5+d%u;", i);
            }
            length += (uint32_t)snprintf(source + length, sizeof(source) - length, "return s^(unsigned long)(long)(t*16);}unsigned long eighteen(");
            for (uint32_t i = 0; i < 18; i += 1)
            {
                length += (uint32_t)snprintf(source + length, sizeof(source) - length, "unsigned long b%u%s", i, i + 1 < 18 ? "," : "){return 0");
            }
            for (uint32_t i = 0; i < 18; i += 1)
            {
                length += (uint32_t)snprintf(source + length, sizeof(source) - length, "+b%u*%u", i, 2 * i + 1);
            }
            length += (uint32_t)snprintf(source + length, sizeof(source) - length, ";}int tested(int n){unsigned long x=(unsigned long)n+3;unsigned long r=many(");
            for (uint32_t i = 0; i < 40; i += 1)
            {
                static const char* const shapes[4] = { "x+%u", "(x&1?x*%u:x-%u)", "x/%u+(x<<%u%%7)", "eighteen(x,%u,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x)" };
                char argument[96];
                snprintf(argument, sizeof(argument), shapes[i % 4], i + 2, i + 1);
                length += (uint32_t)snprintf(source + length, sizeof(source) - length, "%s,", argument);
            }
            for (uint32_t i = 0; i < 24; i += 1)
            {
                length += (uint32_t)snprintf(source + length, sizeof(source) - length, "(double)x*%u.5-%u%s", i, i, i + 1 < 24 ? "," : ");");
            }
            length += (uint32_t)snprintf(source + length, sizeof(source) - length, "unsigned long(*f)(");
            for (uint32_t i = 0; i < 18; i += 1)
            {
                length += (uint32_t)snprintf(source + length, sizeof(source) - length, "unsigned long%s", i + 1 < 18 ? "," : ")=eighteen;unsigned long q=f(");
            }
            for (uint32_t i = 0; i < 18; i += 1)
            {
                length += (uint32_t)snprintf(source + length, sizeof(source) - length, "x*%u+(x>>%u)%s", i + 1, i % 3, i + 1 < 18 ? "," : ");unsigned long deep=");
            }
            for (uint32_t i = 0; i < 24; i += 1)
            {
                length += (uint32_t)snprintf(source + length, sizeof(source) - length, "(x+%u)*(", i);
            }
            length += (uint32_t)snprintf(source + length, sizeof(source) - length, "x");
            for (uint32_t i = 0; i < 24; i += 1)
            {
                length += (uint32_t)snprintf(source + length, sizeof(source) - length, ")");
            }
            snprintf(source + length, sizeof(source) - length, ";return (int)((r^q^deep)%%100003);}");
            check_test_program_source(test, source, "values-past-the-depth-tables", clang);
        }
        {
            // Values past the depth tables beside long double and __int128 values (#780, tier0_memory_uses): calls of 16 int
            // arguments that return one of each, calls of 16 int arguments and one of each, whose arithmetic and compares run
            // past the integer table, and additions nested 18 deep beside each. Run against clang's.
            static const char* const types[2] = { "long double", "__int128" };
            static const char* const names[2] = { "wide_ld", "wide_ti" };
            static const char* const arguments[2] = { "x*2-a15+a0", "x*3+a0-a15" };
            char source[8192];
            char sum[512];
            char sixteen[512];
            char nested[512];
            uint32_t sum_length = 0;
            uint32_t sixteen_length = 0;
            uint32_t nested_length = 0;
            for (uint32_t i = 0; i < 16; i += 1)
            {
                sum_length += (uint32_t)snprintf(sum + sum_length, sizeof(sum) - sum_length, "+a%u*%u", i, i + 1);
                sixteen_length += (uint32_t)snprintf(sixteen + sixteen_length, sizeof(sixteen) - sixteen_length, "%sn+%u", i == 0 ? "" : ",", i);
            }
            for (uint32_t i = 0; i < 18; i += 1)
            {
                nested_length += (uint32_t)snprintf(nested + nested_length, sizeof(nested) - nested_length, "n+%u+(", i);
            }
            nested_length += (uint32_t)snprintf(nested + nested_length, sizeof(nested) - nested_length, "n");
            for (uint32_t i = 0; i < 18; i += 1)
            {
                nested_length += (uint32_t)snprintf(nested + nested_length, sizeof(nested) - nested_length, ")");
            }
            uint32_t length = 0;
            for (uint32_t t = 0; t < 2; t += 1)
            {
                char result[600];
                if (t == 0)
                {
                    snprintf(result, sizeof(result), "(long double)(0%s)/3.0L+a15", sum);
                }
                else
                {
                    snprintf(result, sizeof(result), "(__int128)(0%s)*((__int128)1<<70)+a15", sum);
                }
                length += (uint32_t)snprintf(source + length, sizeof(source) - length, "%s %s_result(", types[t], names[t]);
                for (uint32_t i = 0; i < 16; i += 1)
                {
                    length += (uint32_t)snprintf(source + length, sizeof(source) - length, "int a%u,", i);
                }
                source[length - 1] = ')';
                length += (uint32_t)snprintf(source + length, sizeof(source) - length, "{return %s;}%s %s_argument(", result, types[t], names[t]);
                for (uint32_t i = 0; i < 16; i += 1)
                {
                    length += (uint32_t)snprintf(source + length, sizeof(source) - length, "int a%u,", i);
                }
                length += (uint32_t)snprintf(source + length, sizeof(source) - length, "%s x){return %s;}", types[t], arguments[t]);
            }
            length += (uint32_t)snprintf(source + length, sizeof(source) - length, "int wide_compares(");
            for (uint32_t i = 0; i < 16; i += 1)
            {
                length += (uint32_t)snprintf(source + length, sizeof(source) - length, "int a%u,", i);
            }
            length += (uint32_t)snprintf(source + length, sizeof(source) - length,
                                         "int p,int q,int r,int s,int t){return a0*64+p+q*2+r*4+s*8+t*16;}"
                                         "int tested(int n){long double r=wide_ld_result(%s);__int128 w=wide_ti_result(%s);"
                                         "long double s=wide_ld_argument(%s,(long double)n*1.5L+r*r);__int128 u=wide_ti_argument(%s,w*(__int128)(n+3)+(w>>3));"
                                         "int c=wide_compares(%s,r<s,w<u,s==r,(long double)n+r>=s,u!=w);long double nested=r+(%s);__int128 nested2=w+(%s);",
                                         sixteen, sixteen, sixteen, sixteen, sixteen, nested, nested);
            snprintf(source + length, sizeof(source) - length,
                     "long long x=(long long)(s*16)^(long long)(nested*4)^(long long)(u%%100003)^(long long)(nested2%%99991)^c;return (int)(x%%100003);}");
            check_test_program_source(test, source, "memory-classes-past-the-depth-tables", clang);
        }
        {
            // Indirect variadic calls past the depth tables (#434's review), to the harness's functions, which clang compiles:
            // after a variable-length array, which gives the function its frame pointer (an integer table of 14) and moves
            // rsp, one of 20 unsigned long and 20 double arguments in turn, past both tables, whose al the callee's prologue
            // reads; and one of 10 floats, the last two on the stack, an int and 10 more of each, past the floating table.
            char source[4096];
            uint32_t length = 0;
            length += (uint32_t)snprintf(source + length, sizeof(source) - length,
                                         "unsigned long external_variadic(int n,...);unsigned long external_floats(int n,float,float,float,float,float,float,float,"
                                         "float,float,float,int m,...);int tested(int n){unsigned long x=(unsigned long)n+3;unsigned long buffer[(x&7)+1];"
                                         "for(unsigned long i=0;i<=(x&7);i++)buffer[i]=x*i+1;unsigned long(*v)(int,...)=external_variadic;"
                                         "unsigned long(*f)(int,float,float,float,float,float,float,float,float,float,float,int,...)=external_floats;"
                                         "unsigned long a=v(n");
            for (uint32_t i = 0; i < 20; i += 1)
            {
                length += (uint32_t)snprintf(source + length, sizeof(source) - length, ",x*%u+%u,(double)x*0.5-%u", i + 1, i, i);
            }
            length += (uint32_t)snprintf(source + length, sizeof(source) - length, ");unsigned long b=f(n");
            for (uint32_t i = 0; i < 10; i += 1)
            {
                length += (uint32_t)snprintf(source + length, sizeof(source) - length, ",(float)x*0.25f+%u", i);
            }
            length += (uint32_t)snprintf(source + length, sizeof(source) - length, ",n+7");
            for (uint32_t i = 0; i < 10; i += 1)
            {
                length += (uint32_t)snprintf(source + length, sizeof(source) - length, ",x*3+%u,(double)x/4+%u", i, i);
            }
            snprintf(source + length, sizeof(source) - length, ");return (int)((a^b^buffer[x&7])%%100003);}");
            check_test_program_source(test, source, "variadic-calls-past-the-depth-tables", clang);
        }
        check_test_program_suite(test, "checker/programs/statement-region-static-selection", clang);
    }
    // GNU C's __auto_type (#147): a macro's maximum, nested; a file scope's and a static's; structures from a call and a
    // compound literal, an array's compound literal, a statement expression's value whose variable has its name, a
    // loop's, and the specifiers' const over a char's value.
    if (check_test_takes())
    {
        check_test_program_suite(test, "checker/programs/auto-type", clang);
        {
            char source[4096];
            uint32_t length = 0;
            for (uint32_t i = 0; i < 33; i += 1)
            {
                length += (uint32_t)snprintf(source + length, sizeof(source) - length, "int global%u=%u;", i, i + 1);
            }
            length += (uint32_t)snprintf(source + length, sizeof(source) - length, "int tested(int n){return n");
            for (uint32_t i = 33; i-- > 0;)
            {
                length += (uint32_t)snprintf(source + length, sizeof(source) - length, "+global%u", i);
            }
            snprintf(source + length, sizeof(source) - length, ";}");
            check_test_program_source(test, source, "vector-reference-batch", clang);
        }
        for (uint32_t fixture = 0; fixture < 4; fixture += 1)
        {
            char source[1024];
            char name[64];
            const uint32_t multiplier = 2 + check_test_random(test) % 7;
            const uint32_t modulus = 3 + check_test_random(test) % 11;
            const uint32_t bias = 1 + check_test_random(test) % 31;
            snprintf(source, sizeof(source), "int tested(int n){int x=%u;for(int i=0;i<n+4;i++){if(i%%3==1)continue;x=(x*%u+i)%%%u;}return n?x+n:x-%u;}",
                     bias, multiplier, modulus, bias);
            snprintf(name, sizeof(name), "generated-control-%u", fixture);
            check_test_program_source(test, source, name, clang);
        }
        check_test_program_boundary(test, "", "empty-program", true, true, 0, 0);
        check_test_program_boundary(test, "struct S0{int x;};struct S1{struct S0 x;};", "types-only-no-abi", true, true, 0, 0);
        check_test_program_boundary(test, "struct S{int x;};struct S value={42};int values[3]={1,2,3};", "objects-only-no-abi", true, true, 0, 0);
        check_test_program_boundary(test, "int main(void){}", "implicit-main-return", true, true, 0, 0);
        check_test_program_boundary(test, "int tested(int n){int x=n+1;return x*2;}", "program-rollback", true, true, 0, 1);
        // The node counts' terms (check_lower_node_counts): the builtins of each kind, a vector's libm builtins, whose lanes
        // call the C library, and past 16 nodes the masks' partial tails; and with an _Atomic type, updates that need atomic storage.
        check_test_program_boundary(test,
            "typedef float V __attribute__((vector_size(16)));"
            "int tested(int n){V v={n,2,3,4};V w=__builtin_elementwise_sin(v);V p=__builtin_elementwise_pow(v,w);"
            "int o;return (int)(w[0]+p[1])+__builtin_popcount(n)+__builtin_add_overflow(n,1,&o)+(int)__builtin_fabs((double)n);}",
            "node-counts-vector-libm", true, true, 0, 3);
        // A scalar result of a transport vector's subscript still needs the vector operand's entries and locals.
        check_test_program_boundary(test, "typedef int V __attribute__((vector_size(128)));int tested(V v,int i){return v[i];}",
                                    "node-counts-transport-subscript", true, true, 0, 0);
        check_test_program_boundary(test, "_Atomic int a;int tested(int n){a+=n;int x=a;return x+__builtin_popcount(n);}", "node-counts-atomic-type", true, true, 1, 1);
        // The atomic builtins on plain objects, which the ops' masks count (no _Atomic kind in the table).
        check_test_program_boundary(test,
            "signed char a;unsigned long d;int tested(int n){__atomic_store_n(&a,n-120,5);__atomic_store_n(&d,(unsigned long)n+9,3);"
            "__atomic_thread_fence(5);__sync_synchronize();int x=__atomic_load_n(&a,0);return x+(int)__atomic_fetch_add(&d,1,5);}",
            "node-counts-masks-atomic-builtins", true, true, 2, 1);
        check_test_program_boundary(test, "int tested(int n){return (int)__builtin_sqrt(n+4);}", "math-runtime-symbol-rollback", true, true, 0, 0);
        check_test_program_boundary(test, "double __errno_location(void);int tested(int n){return (int)__builtin_sqrt(n+4);}",
                                    "incompatible-errno-accessor-function", true, false, 0, 0);
    }
    if (check_test_takes())
    {
        check_test_program_boundary(test, "extern int __errno_location;int tested(int n){return (int)__builtin_sqrt(n+4);}",
                                    "incompatible-errno-accessor-object", true, false, 0, 0);
        check_test_program_boundary(test, "extern int __errno_location;int tested(int n){return n+(0&&__builtin_sqrt(-1.0));}",
                                    "inactive-math-has-no-runtime-accessor", true, true, 0, 0);
        check_test_program_boundary(test, "__int128 wide(__int128 x){return x+1;}", "wide-primary-program", true, true, 0, 0);
        check_test_program_boundary(test, "extern int(*callback)(int);int tested(int n){return callback(n);}",
                                    "indirect-function-pointer-call", true, true, 1, 0);
        check_test_program_boundary(test,
            "typedef union __attribute__((transparent_union)){int value;unsigned bits;} U;int consume(U);int tested(int n){return consume(n);}",
            "transparent-union-call-machine-boundary", true, true, 1, 0);
        check_test_program_boundary(test,
            "typedef int V __attribute__((vector_size(16)));extern V global;int tested(int n){return __builtin_reduce_add(global);}",
            "vector-builtin-live-reference-machine-boundary", true, true, 1, 0);
        check_test_program_boundary(test,
            "typedef int V __attribute__((vector_size(16)));extern V global;int tested(int n){return __builtin_reduce_add(__builtin_nondeterministic_value(global));}",
            "vector-builtin-unevaluated-type-argument", true, true, 0, 0);
        check_test_program_boundary(test,
            "typedef int V __attribute__((vector_size(16)));extern V global;int tested(int n){return __builtin_reduce_add(__builtin_shufflevector(global,global,0,1,2,3));}",
            "vector-builtin-shuffle-prefix-reference-mask", true, true, 1, 0);
        check_test_program_boundary(test, "int tested(int n){int values[n+10];return sizeof values;}", "variable-length-array-program", true, true, 0, 0);
        check_test_program_boundary(test, "int extent;int tested(int n){typedef int A[extent+10];return sizeof(A);}",
                                    "vla-bound-reference", true, true, 1, 0);
        check_test_program_boundary(test, "int global;int tested(int n){asm volatile(\"\":\"=r\"(global):\"r\"(n):\"memory\");return global;}",
                                    "typed-asm-operands-and-machine-boundary", true, true, 1, 0);
        check_test_program_boundary(test, "int tested(int n){asm goto(\"\"::::target);return n;target:return n+1;}",
                                    "typed-asm-goto-target", true, true, 0, 0);
    }
    // File-scope assembly lowers no code: the planning assembles it (check_program_asm_plan_file, #619).
    if (check_test_takes())
    {
        check_test_program_boundary(test, "asm(\".byte 0\");int tested(int n){return n;}",
                                    "file-assembly-lowers-no-code", true, true, 0, 0);
        check_test_program_boundary(test,
            "extern int missing;int tested(int n,...){__builtin_va_list ap;__builtin_va_start(ap,(missing++,n));"
            "int x=__builtin_va_arg(ap,int);__builtin_va_end(ap);return x;}", "varargs-unevaluated-final-parameter", true, true, 0, 1);
        check_test_program_boundary(test, "_Thread_local int g;int tested(int n){return g+n;}", "executable-tls-reference", true, true, 1, 0);
        check_test_program_boundary(test, "int tested(int n){_Alignas(32) int x=n;return x;}", "automatic-alignment-boundary", true, true, 0, 1);

        check_test_program_boundary(test, "int global;struct S{int x;int*p;};int tested(int n){struct S s={n,&global};return s.x;}",
                                    "aggregate-primary-initialization", true, true, 1, 1);
        check_test_program_boundary(test, "struct S{int x;};struct S make(int n);int tested(int n){return make(n).x;}",
                                    "aggregate-result-lowering", true, true, 1, 0);
        check_test_program_boundary(test, "struct S{int x;};int take(struct S);int tested(int n){struct S s={n};return take(s);}",
                                    "aggregate-argument-lowering", true, true, 1, 1);
        check_test_program_boundary(test, "struct B{unsigned x:3;};int tested(int n){struct B b;b.x=n;return b.x;}",
                                    "bit-field-lowering", true, true, 0, 0);
        // Long double: lowered, with its constants' table, its values in temporaries (frame objects of their own) too.
        check_test_program_boundary(test, "long double g;int tested(int n){long double x=n+0.5L;g=x;return g>1;}", "long-double-lowering", true, true, 1, 1);
        check_test_program_boundary(test, "long double tested(int n){return ({long double t=n;t;});}", "long-double-statement-region-value", true, true, 0, 1);
        check_test_program_boundary(test, "int tested(int n){long double x=n;return (x?:2.0L)>1;}", "long-double-omitted-conditional-operand", true, true, 0, 1);
        check_test_program_boundary(test, "int tested(int n){long double x=n;return __builtin_isnan(x);}", "long-double-floating-builtin", true, true, 0, 1);
    }
    if (check_test_takes())
    {
        check_test_program_boundary(test, "int tested(int n){_Complex long double z=n;return __real__ z>1;}", "complex-long-double", true, true, 0, 1);
        // GNU's complex integer types of __int128 and the complex _BitInts, which the usual arithmetic conversions make, and
        // _Complex __float128 (#137) are lowered; complex _Float16's storage and conversions are lowered too. The complex
        // types check_lower_complex doesn't take are refused by what they need (clang 23 has no
        // _Float128 in C; __float128 is its spelling).
        check_test_program_boundary(test, "int tested(int n){_Complex int z=n;return __real__ z>1;}", "complex-int", true, true, 0, 1);
        check_test_program_boundary(test, "int tested(int n){_Complex int z=n;__int128 w=n;__typeof__(z+w) v=z+w;return __real__ v>1;}", "complex-int128", true, true, 0, 3);
        check_test_program_boundary(test, "int tested(int n){_Complex char z=n;_BitInt(100) w=n;__typeof__(z+w) v=z*w;return __real__ v>1;}", "complex-bit-int-100", true, true,
                                    0, 3);
        check_test_program_boundary(test, "int tested(int n){_Complex char z=n;_BitInt(129) w=n;__typeof__(z+w) v=z+w;return __real__ v>1;}", "complex-bit-int-129", true,
                                    true, 0, 3);
        // Their operations' room (check_lower_limb_room): a chain of sums of complex _BitInt(2100)s, each part 33 limbs, and
        // every operation on them, a quotient's and an update's computations a part among them, all lowered.
        check_test_program_boundary(test, "typedef __typeof__((_Complex char)0+(unsigned _BitInt(2100))0) C;C f(C a,C b){return a+b+a+b+a+b+a+b+a+b+a+b+a+b+a+b+a+b+a+b;}",
                                    "complex-bit-int-2100-chain", true, true, 0, 0);
        check_test_program_boundary(test,
                                    "typedef __typeof__((_Complex char)0+(_BitInt(2100))0) C;"
                                    "C g(C a,C b,int n){C x=a*b;x/=a;x-=b;x*=b;x=-x;x=~x;x+=n;return x==a?a/b:x*a-b/a;}",
                                    "complex-bit-int-2100-operations", true, true, 0, 1);
        // A promoted K&R parameter now converts from double to its declared float on entry.
        check_test_program_boundary(test, "float half(x)float x;{return x/2;}int tested(int n){return (int)half(n);}", "kr-promoted-parameter", true, true, 1, 0);
        check_test_program_boundary(test, "__float128 g;int tested(int n){__float128 x=n;g=x*x;return g>1;}", "float128-lowering", true, true, 1, 1);
        check_test_program_boundary(test, "int tested(int n){__float128 x=n;x++;return x>1;}", "float128-increment", true, true, 0, 1);
        check_test_program_boundary(test, "int tested(int n){_Complex __float128 z=n;return __real__ z>1;}", "complex-float128", true, true, 0, 1);
        check_test_program_boundary(test, "int tested(int n){_Complex _Float16 z=n;return __real__ z>1;}", "complex-float16", true, true, 0, 1);
    }
    // The 128-bit integers, _BitInt(65) to _BitInt(128) and those past 128 bits (in memory, #137): lowered, the overflow
    // builtins of a _BitInt of 65 to 127 bits too (#1447), but a bit-field of them wider than 64 bits, or past 128 bits.
    if (check_test_takes())
    {
        check_test_program_boundary(test, "__int128 g;int tested(int n){__int128 x=n;g=x*x;return g>1;}", "int128-lowering", true, true, 1, 1);
        check_test_program_boundary(test, "int tested(int n){_BitInt(128) x=n;return x>1;}", "bit-int-128", true, true, 0, 1);
        check_test_program_boundary(test, "int tested(int n){_BitInt(129) x=n;return x>1;}", "bit-int-129", true, true, 0, 1);
        check_test_program_boundary(test, "_BitInt(200) g;int tested(int n){_BitInt(200) x=n;g=x*x;return g>1;}", "bit-int-200-global", true, true, 1, 1);
        check_test_program_boundary_reason(test, "struct W{unsigned _BitInt(200) x:150;};int tested(int n){struct W w;w.x=n;return w.x>1;}", "bit-int-200-bit-field", true,
                                           false, 0, 0, "bit-fields of this type or wider than 64 bits");
        check_test_program_boundary(test, "int tested(int n){_BitInt(100) r;return __builtin_add_overflow(n,n,&r);}", "bit-int-100-overflow", true, true, 0, 0);
        // A result SSE, SSEUP of 22 bytes where the target's vectors are 32 bytes, which clang returns as <2 x double> in
        // xmm0 (#2113). At x86-64's 16 bytes it is MEMORY, through a hidden pointer.
        X86Target v3 = { 0 };
        if (x86_target_parse("x86-64-v3", 9, &v3))
        {
            check_test_program_boundary_target(test, "union __attribute__((packed)) U{float v __attribute__((vector_size(16)));char c[22];};"
                                                     "union U make(int n){union U u={0};u.c[0]=(char)n;return u;}int tested(int n){return make(n).c[0];}",
                                               "odd-vector-union-result", true, true, 1, 1, NULL, &v3);
            // At 24 to 31 bytes clang returns <3 x double> in ymm0, Tier 0's payload of 24 bytes (#2113).
            check_test_program_boundary_target(test, "union __attribute__((packed)) U{float v __attribute__((vector_size(16)));char c[24];};"
                                                     "union U make(int n){union U u={0};u.c[0]=(char)n;return u;}int tested(int n){return make(n).c[0];}",
                                               "odd-vector-union-result-24", true, true, 1, 1, NULL, &v3);
        }
        // And at x86-64-v4 from 33 bytes in a ymm or a zmm register: <4 x double> at 39 bytes, <5 x double> at 47.
        X86Target v4 = { 0 };
        if (x86_target_parse("x86-64-v4", 9, &v4))
        {
            check_test_program_boundary_target(test, "union __attribute__((packed)) U{float v __attribute__((vector_size(16)));char c[39];};"
                                                     "union U make(int n){union U u={0};u.c[0]=(char)n;return u;}int tested(int n){return make(n).c[0];}",
                                               "odd-vector-union-result-39", true, true, 1, 1, NULL, &v4);
            check_test_program_boundary_target(test, "union __attribute__((packed)) U{float v __attribute__((vector_size(16)));char c[47];};"
                                                     "union U make(int n){union U u={0};u.c[0]=(char)n;return u;}int tested(int n){return make(n).c[0];}",
                                               "odd-vector-union-result-47", true, true, 1, 1, NULL, &v4);
        }
        check_test_program_boundary(test, "struct W{unsigned __int128 x:100;};int tested(int n){struct W w;w.x=n;return w.x>1;}", "int128-bit-field-past-8-bytes", true,
                                    false, 0, 0);
        // A packed field in 9 bytes lowers, as its first 8 and its ninth (#2999).
        check_test_program_boundary(test,
                                    "struct __attribute__((packed)) P{unsigned char c:4;unsigned long long x:64;};"
                                    "int tested(int n){struct P p;p.x=(unsigned long long)n;return (int)p.x;}",
                                    "bit-field-nine-byte-machine-boundary", true, true, 0, 0);
        check_test_program_boundary(test, "int tested(unsigned n){switch(n){case -1:return 1;case 4294967295U:return 2;}return 0;}",
                                    "case-conversion-collision", false, false, 0, 0);
        check_test_program_boundary(test, "int tested(int n){switch(n){case 1 ... 5:return 1;case 4 ... 7:return 2;}return 0;}",
                                    "overlapping-case-ranges", false, false, 0, 0);
        check_test_program_boundary(test, "int tested(int n){switch(n){default:return 1;default:return 2;}}", "duplicate-default", false, false, 0, 0);
        check_test_program_boundary(test, "int tested(int n){switch((double)n){case 1:return 1;}return 0;}", "noninteger-switch", false, false, 0, 0);
    }
    if (check_test_takes())
    {
        check_test_program_asm(test, clang);
    }
    check_test_program_build_release();
}

#endif

