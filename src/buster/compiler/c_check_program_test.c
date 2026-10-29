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
    for (uint32_t count = 0; ready && count <= 129; count += 1)
    {
        uint8_t* kinds = mappings[0] + page - count;
        uint8_t* roles = mappings[1] + page - count;
        uint32_t* targets = (uint32_t*)(mappings[2] + page - (uint64_t)count * sizeof(uint32_t));
        uint8_t expected[130] = { 0 };
        uint8_t scalar[130];
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
        const CheckInput input = { .token_count = count, .label_targets = targets };
        check_program_addressed(&parser, &input, scalar, true);
        check_program_addressed(&parser, &input, vector, false);
        check_test_expect(test, memcmp(scalar, expected, count + 1) == 0 && memcmp(vector, expected, count + 1) == 0,
                          "label addresses exclude logical && and non-prefix tokens, including every guarded vector tail");
    }
    for (uint32_t column = 0; column < 3; column += 1)
    {
        if (mappings[column] != MAP_FAILED)
        {
            munmap(mappings[column], page * 2);
        }
    }
}

#define BUSTER_CHECK_TEST_SCAN_COLUMNS 16u
#define BUSTER_CHECK_TEST_SCAN_TOKENS 129u

// #530's scans in check_program against their exact twins, their `scalar` paths: at every count to 129 tokens, each column
// they read ending at a PROT_NONE page, random columns around the values each test separates. The bodies' openings and the
// initializers' tokens; the variable-length arrays' declarations; the reached builtins; the static objects' declarations,
// needs and naked functions, with room for their column and without (whose symbols #442's kernels number:
// check_test_program_declaring); and at 64 and 128 tokens, the cases' marks, against a test of each token.
static void check_test_program_scans(CheckTest* test)
{
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    uint8_t* mappings[BUSTER_CHECK_TEST_SCAN_COLUMNS] = { 0 };
    bool ready = page >= BUSTER_CHECK_TEST_SCAN_TOKENS * sizeof(uint32_t);
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
        uint8_t* static_objects = mappings[6] + page - count;
        uint32_t* objects = (uint32_t*)(mappings[7] + page - (uint64_t)count * sizeof(uint32_t));
        uint8_t* defined = mappings[8] + page - count;
        uint8_t* tentative = mappings[9] + page - count;
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
            static_objects[token] = (uint8_t)((draw >> 9) % 3 == 0);
            objects[token] = (draw >> 3) % (count + 2);
            defined[token] = (uint8_t)((draw >> 11) & 1);
            tentative[token] = (uint8_t)((draw >> 12) & 1);
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
                .token_count = count,
                .deferred_tokens = variant == 0 ? deferred : NULL,
                .declarations = declarations,
                .declared_types = declared_types,
                .declaration_kinds = declaration_kinds,
                .objects = objects,
                .static_objects = static_objects,
                .defined = defined,
                .tentative = tentative,
                .initializer_nodes = initializer_nodes,
                .body_declarations = body_declarations,
                .declaration_bodies = declaration_bodies,
                .attributes = attributes,
            };
            uint32_t openings[2][BUSTER_CHECK_TEST_SCAN_TOKENS + 1];
            uint32_t initializers[2][BUSTER_CHECK_TEST_SCAN_TOKENS + 1];
            uint32_t bodies[2];
            for (uint32_t path = 0; path < 2; path += 1)
            {
                memset(initializers[path], 0xff, sizeof(initializers[path]));
                bodies[path] = check_program_openings(&input, node_count, openings[path], initializers[path], path == 0);
            }
            check_test_expect(test, bodies[0] == bodies[1] && memcmp(openings[0], openings[1], bodies[0] * sizeof(uint32_t)) == 0 &&
                                   memcmp(initializers[0], initializers[1], sizeof(initializers[0])) == 0,
                              "check_program's openings and initializers agree with their scalar twin at every guarded tail");
            Parser parser = { 0 };
            parser.tree.roles = roles;
            parser.tree.parents = parents;
            parser.tokens.kinds = kinds;
            parser.tokens.count = count;
            uint32_t allocation_first[2][BUSTER_CHECK_TEST_SCAN_TOKENS + 1];
            uint32_t allocation_next[2][BUSTER_CHECK_TEST_SCAN_TOKENS + 1];
            uint32_t vla_blocks[2][BUSTER_CHECK_TEST_SCAN_TOKENS + 1];
            uint32_t vla_saves[2][BUSTER_CHECK_TEST_SCAN_TOKENS + 1];
            CheckProgram programs[2] = { 0 };
            for (uint32_t path = 0; path < 2; path += 1)
            {
                programs[path] = (CheckProgram){ .input = &input, .types = &types.table, .status = CHECK_PROGRAM_OK, .token = BUSTER_CHECK_NONE };
                memset(allocation_first[path], 0xff, sizeof(allocation_first[path]));
                memset(allocation_next[path], 0xa5, sizeof(allocation_next[path]));
                memset(vla_blocks[path], 0, sizeof(vla_blocks[path]));
                memset(vla_saves[path], 0, sizeof(vla_saves[path]));
                check_program_allocations(&programs[path], &parser, &types.table, allocation_first[path], allocation_next[path], vla_blocks[path], vla_saves[path], path == 0);
            }
            check_test_expect(test, programs[0].status == programs[1].status && programs[0].token == programs[1].token &&
                                   memcmp(allocation_first[0], allocation_first[1], sizeof(allocation_first[0])) == 0 &&
                                   memcmp(allocation_next[0], allocation_next[1], sizeof(allocation_next[0])) == 0 &&
                                   memcmp(vla_blocks[0], vla_blocks[1], sizeof(vla_blocks[0])) == 0 && memcmp(vla_saves[0], vla_saves[1], sizeof(vla_saves[0])) == 0,
                              "check_program's variable-length array declarations agree with their scalar twin at every guarded tail");
            const CheckGraph graph = { .ops = ops, .count = node_count };
            uint32_t builtins[2][BUSTER_CHECK_TEST_SCAN_TOKENS + 1];
            uint32_t builtin_counts[2];
            for (uint32_t path = 0; path < 2; path += 1)
            {
                builtin_counts[path] = check_program_builtins(&graph, reached, NULL, path == 0);
                const uint32_t listed = check_program_builtins(&graph, reached, builtins[path], path == 0);
                check_test_expect(test, listed == builtin_counts[path], "check_program's builtins count what they list");
            }
            check_test_expect(test, builtin_counts[0] == builtin_counts[1] && memcmp(builtins[0], builtins[1], builtin_counts[0] * sizeof(uint32_t)) == 0,
                              "check_program's reached builtins agree with their scalar twin at every guarded tail");
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
                rooms[path] = check_program_static_objects(&programs[path], needed[path], &naked, path == 0, &events[path]);
                events[path].scratch = NULL;
            }
            const bool naked_same = (programs[0].object_naked == 0) == (programs[1].object_naked == 0) &&
                                    (programs[0].object_naked == 0 || memcmp(programs[0].object_naked, programs[1].object_naked, ((uint64_t)count + 1) * sizeof(uint32_t)) == 0);
            // Each function's declaration with a target or target_clones attribute not ignored is its object's key, in token
            // order, as the test's own walk finds them.
            uint32_t expected_events = 0;
            bool events_right = true;
            for (uint32_t token = 0; token < count; token += 1)
            {
                const uint32_t object = objects[token];
                const bool event = declarations[token] == token && static_objects[token] && object != 0 && object <= count &&
                                   declaration_kinds[token] == C_BINDING_FUNCTION &&
                                   (attributes[token] == &kinds_of_attributes[3] || attributes[token] == &kinds_of_attributes[5]);
                events_right &= !event || expected_events >= events[0].count || events[0].keys[expected_events] == (((uint64_t)object << 32) | token);
                expected_events += event;
            }
            const bool limited = variant != 0 && expected_events != 0;
            events_right &= limited ? !events[0].room && events[0].count == 0 : events[0].room && events[0].count == expected_events;
            const bool events_same = events[0].room == events[1].room && events[0].count == events[1].count &&
                                     (events[0].count == 0 || memcmp(events[0].keys, events[1].keys, events[0].count * sizeof(uint64_t)) == 0);
            check_test_expect(test, memcmp(needed[0], needed[1], sizeof(needed[0])) == 0 && memcmp(object_declarations[0], object_declarations[1], sizeof(object_declarations[0])) == 0 &&
                                   rooms[0] == rooms[1] && naked_same && events_same && events_right,
                              "check_program's static objects, naked functions and target attributes agree with their scalar twin at every guarded tail");
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
                    equal = equal && writer.source_labels[token] == expected_label && writer.stops[token] == BUSTER_CHECK_NONE &&
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

// #442's kernels, each path against the test's own reading of its contract, every column (and output) ending at a
// PROT_NONE page: the declaring tokens, without and with the initializers' nodes, for every count to 200 and larger
// ones, the declarations with initializers (#592's check_init_declarations), and the needed objects' count and
// numbering (object 0 never a symbol), at every density.
static void check_test_program_declaring(CheckTest* test)
{
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
                for (uint32_t path = 0; path < 2; path += 1)
                {
                    const uint32_t got = check_program_declaring(declarations, with_nodes != 0 ? nodes : 0, limit, count, tokens, path == 0);
                    const bool same = got == want && memcmp(tokens, expected, (uint64_t)want * sizeof(uint32_t)) == 0;
                    all &= same;
                    checked += 1;
                    if (!same)
                    {
                        printf("  declaring tokens: count %u density %u nodes %u %s path: %u tokens, expected %u\n", count, density, with_nodes,
                               path == 0 ? "scalar" : "vector", got, want);
                    }
                }
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
            for (uint32_t path = 0; ready && path < 2; path += 1)
            {
                const CheckInput input = { .initializer_begins = begins, .token_count = count };
                const uint32_t got = check_init_declarations(&input, count, initialized, path == 0);
                const bool same = got == begun && memcmp(initialized, expected, (uint64_t)begun * sizeof(uint32_t)) == 0;
                all &= same;
                checked += 1;
                if (!same)
                {
                    printf("  initialized declarations: count %u density %u %s path: %u tokens, expected %u\n", count, density, path == 0 ? "scalar" : "vector", got, begun);
                }
            }
            const uint32_t objects = count + 1;
            uint32_t want = 0;
            for (uint32_t object = 1; ready && object < objects; object += 1)
            {
                want += needed[object] != 0;
            }
            for (uint32_t path = 0; ready && path < 2; path += 1)
            {
                const uint32_t got = check_program_needed_count(needed, objects, path == 0);
                // The symbols' objects exactly as many as the needed objects, ending at the protected page.
                uint32_t* symbol_objects = got == want ? (uint32_t*)check_test_program_guarded(&maps[6], &sizes[6], (uint64_t)want * sizeof(uint32_t)) : 0;
                memset(object_symbols, 0, (uint64_t)objects * sizeof(SymbolId));
                if (symbol_objects != 0)
                {
                    check_program_needed_number(needed, objects, object_symbols, symbol_objects, path == 0);
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
                    printf("  needed objects: count %u density %u %s path: %u symbols, expected %u\n", count, density, path == 0 ? "scalar" : "vector", got, want);
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
    printf("  declaring tokens and needed objects: %llu cases, both paths at PROT_NONE pages\n", (unsigned long long)checked);
    check_test_expect(test, all, "declaring tokens and needed objects agree with the contract by both paths, at every count and density");
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

// The oracle's source: each global the checked object defines renamed oracle_NAME, then the program, in `text`.
static bool check_test_program_oracle(const Link* link, const char* source, char** text, size_t* length)
{
    *text = 0;
    *length = 0;
    FILE* file = open_memstream(text, length);
    bool valid = file != 0;
    for (SymbolId symbol = 1; valid && symbol < link->symbols.count; symbol += 1)
    {
        const uint8_t flags = link->symbols.flags[symbol];
        if ((flags & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_GLOBAL && link->symbols.atom[symbol] != 0)
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
    "#endif\n"
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
    "int main(void) { for (int repeat=0;repeat<3;repeat++) for (int n=-3;n<=19;n++) {\n"
    "int a=tested(n), b=oracle_tested(n); if(a!=b) { fprintf(stderr,\"n=%d repeat=%d ours=%d oracle=%d\\n\",n,repeat,a,b); return 1; } }\n"
    "if(terminal_probe){int p=fork();if(p<0)return 2;if(!p){struct rlimit r={0,0};setrlimit(RLIMIT_CORE,&r);terminal_probe();_exit(91);}\n"
    "int s=0;if(waitpid(p,&s,0)!=p||!WIFSIGNALED(s)||WTERMSIG(s)!=SIGILL)return 3;}return 0;}\n";

// What every program's build shares (#465): the harness, the same for every program, compiled once per run for each
// signedness of plain char that the programs are read with, and the last oracle, which the scalar and vector paths' programs of one source share, compiled once per distinct text. Each
// compiles with -c and the flags of the one-step build, which gives the code that build's compiles give, so a program
// links the same harness, oracle and checked object as before. The oracle's key is its text and plain char's
// signedness. And the last program that linked and ran as the oracle
// did: its checked object's bytes. A program whose checked object and oracle are that one's links the same files into
// the same executable, which runs alike, so it takes that run's result (the vector path's program, mostly, after the
// scalar path's).
typedef struct CheckTestProgramBuild CheckTestProgramBuild;
struct CheckTestProgramBuild
{
    char directory[40];
    bool harness[2];
    char* oracle;
    size_t oracle_length;
    bool oracle_unsigned;
    uint8_t* object;
    uint64_t object_size;
};

static CheckTestProgramBuild check_test_program_build;

// The -march of the oracle's and the harness's compiles and of the link: x86-64's, or a section's own while it runs
// (check_test_program_vectors, which releases the build around it).
static const char* check_test_program_march = "-march=x86-64";

// The target options a program's functions' target attributes resolve against (CheckProgramOptions.predefines): the
// oracle's -march, whose X86Target the section gives the harness (x86_target_parse's profile of the same name).
static PredefinesOptions check_test_program_predefines(void)
{
    return (PredefinesOptions){ .march = check_test_program_march + strlen("-march=") };
}

// Whether a program's preprocessing searches the system's headers, as the oracle's compile does: off, or while a
// section's programs include them (the intrinsics' headers, check_test_program_x86's).
static bool check_test_program_includes = false;

// The flags of every compile and link of a program, with plain char as the checker read it (`char_unsigned`), for the
// oracle and the harness alike. Nothing here contracts a * b + c into one fused operation (docs/c-checker.md), which
// clang's default (-ffp-contract=on) does even at -O0 where the target has FMA (x86-64-v4's vector programs).
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

// Compiles `name`.c, holding `text`, to `name`.o in the build's directory, its diagnostics in `out` and `err`.
static bool check_test_program_compile(const char* clang, const char* name, const char* text, size_t length, bool char_unsigned, const char* out,
                                       const char* err)
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
// oracle of `text` compiled unless it is the last one's, with the same signedness (`*same_oracle`).
static bool check_test_program_prepare(const char* clang, const char* text, size_t length, bool char_unsigned, const char* out, const char* err,
                                       bool* same_oracle)
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
                                                                   sizeof(check_test_program_harness) - 1, char_unsigned, out, err);
        valid = build->harness[char_unsigned];
    }
    const bool same = build->oracle != 0 && build->oracle_unsigned == char_unsigned && build->oracle_length == length &&
                      memcmp(build->oracle, text, length) == 0;
    *same_oracle = same;
    if (valid && !same)
    {
        free(build->oracle);
        build->oracle = 0;
        valid = check_test_program_compile(clang, "oracle", text, length, char_unsigned, out, err);
        build->oracle = valid ? malloc(length) : 0;
        valid = valid && build->oracle != 0;
        if (valid)
        {
            memcpy(build->oracle, text, length);
            build->oracle_length = length;
            build->oracle_unsigned = char_unsigned;
        }
    }
    return valid;
}

// Removes the build's directory and forgets what it held: run after the last program of a section.
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
// execution-result cache entry that a later eligible comparison could mistake for a successful run.
static bool check_test_program_execute_with_alignment(Link* link, const char* source, const char* clang, bool char_unsigned, bool alignment_oracle,
                                                       bool run_native, Arena* scratch)
{
    char directory[] = "/tmp/buster-check-program-XXXXXX";
    const bool created = mkdtemp(directory) != 0;
    char object[160];
    char executable[160];
    char out[160];
    char err[160];
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
        valid = check_test_program_oracle(link, source, &text, &length);
    }
    valid = valid && check_test_program_prepare(clang, text, length, char_unsigned, out, err, &same_oracle);
    CheckTestProgramBuild* build = &check_test_program_build;
    const bool same = run_native && valid && same_oracle && build->object != 0 && build->object_size == size && memcmp(build->object, bytes, size) == 0;
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
    valid = valid && (same || (object_test_run(link_command, out, err) == 0 && (!run_native || object_test_run(run, out, err) == 0)));
    if (!same)
    {
        free(build->object);
        build->object = valid && run_native ? bytes : 0;
        build->object_size = valid && run_native ? size : 0;
        bytes = valid && run_native ? 0 : bytes;
    }
    if (!valid && created)
    {
        // The sources go beside the object, as the one-step build had them.
        char path[160];
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
        unlink(executable);
        unlink(out);
        unlink(err);
        rmdir(directory);
    }
    return valid;
}

static bool check_test_program_execute(Link* link, const char* source, const char* clang, bool char_unsigned, Arena* scratch)
{
    return check_test_program_execute_with_alignment(link, source, clang, char_unsigned, false, true, scratch);
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
        if (input->declarations[token] == token && input->static_objects[token])
        {
            const uint32_t object = input->objects[token];
            valid = object != 0 && object <= count;
            if (valid)
            {
                result->latest_declarations[object] = token;
                const CheckInputAttributes* attributes = input->attributes != 0 ? input->attributes[token] : 0;
                if (attributes != 0 && attributes->alignment_log2 > alignments[object])
                {
                    alignments[object] = attributes->alignment_log2;
                }
                if (input->declaration_kinds[token] == C_BINDING_OBJECT && (input->defined[token] || input->tentative[token]))
                {
                    const uint32_t previous = definition[object];
                    if (previous == BUSTER_CHECK_NONE || !input->defined[previous])
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
            reference_capacity += valid && plan_index != 0 ? initializers->plans[plan_index - 1].count : 0;
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
            if (plan == 0 && input->tentative[selected] && type_kind(types, stripped) == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY)
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
                    .initializer = input->initializer_nodes[selected],
                    .token = selected,
                    .initializer_plan = plan,
                    .alignment_log2 = alignments[object] != 0 ? (uint8_t)(alignments[object] - 1) : 0,
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
// and the objects check_program_object_access finds the unit defines, by both paths, against a walk of every token.
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
                   a->string_pool == b->string_pool && a->alignment_log2 == b->alignment_log2;
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
            if (input->declarations[token] == token && input->static_objects[token])
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
                (input->defined[token] || input->tentative[token]))
            {
                expected[object] = 1;
            }
        }
    }
    for (uint32_t path = 0; same && path < 2; path += 1)
    {
        memset(defined, 0xa5, (uint64_t)program->object_count + 1);
        check_program_object_access(program, access, defined, path == 0);
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
            rooms[path] = path == 0 ? check_program_static_objects(reduced + path, needs[path], output, true, &events[path]) :
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
        sorted[path] = same ? c_bindings_sort(events[path].keys, temporary, events[path].count, false, UINT32_MAX) : NULL;
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

// A program fixture, checked and compiled with plain char signed or unsigned (-funsigned-char), for a target (null for
// x86-64's), against clang's build of it with the same; its executable runs where `run_native` says the host can run it.
static void check_test_program_source_native_bounds(CheckTest* test, const char* source, const char* name, const char* clang, bool char_unsigned,
                                                    int32_t expected_pools, uint32_t entry_limit, const X86Target* target, bool run_native, uint32_t local_limit)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    const PPOptions pp_options = {
        .language = { .standard = 2017, .gnu = true },
        .no_standard_includes = !check_test_program_includes,
        .predefines.char_unsigned = char_unsigned,
        .target = target != NULL ? *target : (X86Target){ 0 },
    };
    const PredefinesOptions predefines = check_test_program_predefines();
    bool preprocessed = intern_initialize(&identifiers) && pp_initialize(&pp, &pp_options, &identifiers);
    preprocessed = preprocessed && pp_run_source(&pp, name, (const uint8_t*)source, (uint32_t)strlen(source));
    check_test_expect(test, preprocessed, "executable source preprocessing");
    for (uint32_t path = 0; preprocessed && path < 2; path += 1)
    {
        CheckTestTypes types = { 0 };
        Arena input_arena = arena_reserve(UINT64_C(1) << 26);
        Arena output = arena_reserve(UINT64_C(1) << 26);
        Arena bytes = arena_reserve(UINT64_C(1) << 22);
        Arena scratch = arena_reserve(UINT64_C(1) << 26);
        const ParseOptions parse_options = { .scalar = path == 0 };
        const CheckOptions options = { .scalar = path == 0 };
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
        const CheckProgramOptions program_options = {
            .initializers = &initializers,
            .asms = &asms,
            .prepared_data = &data,
            .identifiers = &identifiers,
            .initializer_references = data.references,
            .initializer_reference_count = data.reference_count,
            .scalar = path == 0,
            .return_flow_work = &return_flow_work,
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
        char description[256];
        snprintf(description, sizeof(description), "%s: %s source produces checked executable control and static-data plans", name, path == 0 ? "scalar" : "vector");
        check_test_expect(test, ready, description);
        if (ready)
        {
            snprintf(description, sizeof(description), "%s: %s static data and token bodies are what the dense passes made", name, path == 0 ? "scalar" : "vector");
            check_test_expect(test, check_test_program_sparse(&program, &data, &input, &stream, &initializers, &types.table, path == 0, &output, &scratch), description);
        }
        // A program that includes the system's headers lowers what its roots reach, as -c does: their other functions
        // (MMX's, whose builtins no row takes) are no part of it.
        if (ready && check_test_program_includes)
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
            fprintf(stderr, "test-checker: program source input=%u stream=%u init=%u data=%u program=%u token=%u\n",
                    input.report_count, stream.report_count, initializers.status, data.status, program.status, program.token);
        }
        CheckProgramLowered lower[2] = { 0 };
        bool lowered = ready && check_program_lower(lower, &program, true, &output, &scratch) && check_program_lower(lower + 1, &program, false, &output, &scratch);
        snprintf(description, sizeof(description), "%s: both unit ABI batches lower to valid Tier0 functions", name);
        check_test_expect(test, lowered, description);
        if (ready && !lowered)
        {
            fprintf(stderr, "test-checker: program lower status=%u/%u token=%u/%u backend=%u/%u\n", lower[0].status, lower[1].status,
                    lower[0].token, lower[1].token, lower[0].backend_node, lower[1].backend_node);
        }
        if (lowered)
        {
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
        CheckProgramCode code = { 0 };
        CheckDataResult emission = { 0 };
        const bool emitted = lowered && check_program_emit(&code, &program, &parser, &identifiers, data.capacity, path == 0, &output, &bytes, &scratch, NULL, NULL, false, true) &&
                             check_data_publish(&data, &types.table, &stream, program.object_symbols, program.object_count, &code.link, 0, &bytes, &scratch, &emission);
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
            if (code.lowered.initializers.count != 0)
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
            if (run_native)
            {
                snprintf(description, sizeof(description), "%s: generated executable agrees with %s on all repeated inputs", name,
                         alignment_oracle ? "the independent alignment expectation" : clang);
            }
            else
            {
                snprintf(description, sizeof(description), "%s: generated object and oracle compile and link; native execution is unavailable on this CPU", name);
            }
            check_test_expect(test, check_test_program_execute_with_alignment(&code.link, source, clang, char_unsigned, alignment_oracle, run_native, &scratch), description);
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
    check_test_program_source_native_bounds(test, source, name, clang, char_unsigned, expected_pools, entry_limit, target, run_native, 0);
}

static void check_test_program_source_bounds(CheckTest* test, const char* source, const char* name, const char* clang, bool char_unsigned,
                                            int32_t expected_pools, uint32_t entry_limit, uint32_t local_limit)
{
    check_test_program_source_native_bounds(test, source, name, clang, char_unsigned, expected_pools, entry_limit, NULL, true, local_limit);
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
        ready = check_lower_copy_type_facts(&types.table, types.table.types.count, facts, &scratch, false);
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
        const CheckInput input = { .token_count = COUNT, .static_objects = flags };
        const CheckInitializers initializers = { .plans = plans, .count = COUNT, .declaration_tokens = declarations };
        const CheckProgram program = { .types = &types.table, .input = &input, .initializers = &initializers,
                                       .controls = controls, .control_count = COUNT };
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
        scratch.capacity = 65536;
    }
    arena_release(&scratch);
    arena_release(&output);
    check_test_types_close(&types);
}

static void check_test_program_initializers(CheckTest* test, const char* clang)
{
    check_test_program_initializer_facts(test);
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
        const CheckOptions checking = { .scalar = path == 0 };
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
                uint32_t writes = 0;
                for (uint32_t i = 0; i < program.control_count; i += 1)
                {
                    writes += program.controls[i].kind == CHECK_CONTROL_INITIALIZE;
                }
                check_test_expect(test, program.reference_count == references && writes == initializations,
                                  "checked body preserves exact live reference and ordered initializer counts");
                if (program.body_count == 0)
                {
                    const uint64_t output_mark = output.length;
                    const uint64_t scratch_mark = scratch.length;
                    CheckLowerShared shared = { 0 };
                    CheckProgramLowered prepared = { 0 };
                    const bool prepared_ok = check_lower_prepare(&shared, &prepared, &program, path == 0, false, &output, &scratch);
                    check_test_expect(test, prepared_ok && shared.table.capacity == 0 && shared.table.rows == 0,
                                      "a unit without function bodies allocates no ABI table, including one with object initializers");
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
                check_test_expect(test, reason == NULL || (!lowered_ok && lowered.reason != NULL && strcmp(lowered.reason, reason) == 0), description);
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
// scan of its controls finds them.
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
        const CheckOptions checking = { .scalar = path == 0 };
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
        uint32_t total = 0;
        for (uint32_t t = 0; lowered_ok && t < lowered.table_count; t += 1)
        {
            total += lowered.tables[t].count;
        }
        char description[256];
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

#include "c_check_program_select_test.c"
#include "c_check_program_query_test.c"
#include "c_check_region_input_test.c"
#include "c_check_program_atomic_test.c"
#include "c_check_program_asm_test.c"
#include "c_check_program_copy_test.c"
#include "c_check_program_folds_test.c"
#include "c_check_program_vector_test.c"
#include "c_check_program_x86_test.c"
#include "c_check_program_vector_builtins_test.c"
#include "c_check_program_fp16_test.c"

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
// complex types of every floating type among them): of the whole world and of every count of its types to 300; then with
// the kinds' column and both tables ending at a PROT_NONE page, for every count to 200, so that a read or a write past the
// last type faults. And a function's memory classes from its entries' (check_lower_entry_classes), at the page's end too.
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
            for (uint32_t prefix = 0; columns != 0 && prefix <= count; prefix = prefix < 300 ? prefix + 1 : prefix < count ? count : prefix + 1)
            {
                uint8_t* scalar_classes = columns;
                uint8_t* scalar_facts = columns + count + 1;
                uint8_t* vector_classes = columns + 2 * ((uint64_t)count + 1);
                uint8_t* vector_facts = columns + 3 * ((uint64_t)count + 1);
                memset(vector_classes, 0xa5, prefix + 1);
                memset(vector_facts, 0xa5, prefix + 1);
                check_lower_classify_types_scalar(table, prefix, scalar_classes, scalar_facts);
                check_lower_classify_types(table, prefix, vector_classes, vector_facts);
                const bool same = memcmp(scalar_classes, vector_classes, prefix) == 0 && memcmp(scalar_facts, vector_facts, prefix) == 0 &&
                                  vector_classes[prefix] == 0xa5 && vector_facts[prefix] == 0xa5;
                char description[160];
                snprintf(description, sizeof(description), "the first %u of world %u's %u types: their classes and facts by both paths", prefix, round, count);
                check_test_expect(test, same, description);
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
                check_lower_classify_types_scalar(table, prefix, columns, columns + count + 1);
                check_lower_classify_types(&copy, prefix, classes, facts);
                char description[160];
                snprintf(description, sizeof(description), "world %u's first %u types' classes and facts, at a page's end", round, prefix);
                check_test_expect(test, memcmp(columns, classes, prefix) == 0 && memcmp(columns + count + 1, facts, prefix) == 0, description);
            }
            free(columns);
            abi_test_world_close(&world);
        }
    }
    // A function's memory classes from its entries' classes (check_lower_entry_classes) against its twin's: columns of
    // every count to 200 ending at the guard page, of random classes but F80, I128 and U128, with one of those at a random
    // place in one column of four, and none in the others.
    for (uint32_t count = 0; guarded && count <= 200; count += 1)
    {
        for (uint32_t round = 0; round < 8; round += 1)
        {
            uint8_t* classes = mappings[0] + page - count;
            for (uint32_t i = 0; i < count; i += 1)
            {
                classes[i] = (uint8_t)(check_test_random(test) % BUSTER_TYPED_CLASS_F80);
            }
            const uint8_t memory[3] = { BUSTER_TYPED_CLASS_F80, BUSTER_TYPED_CLASS_I128, BUSTER_TYPED_CLASS_U128 };
            if (count != 0 && round % 4 != 0)
            {
                classes[check_test_random(test) % count] = memory[round % 3];
            }
            const uint32_t vector = check_lower_entry_classes(classes, count);
            const uint32_t scalar = check_lower_entry_classes_scalar(classes, count);
            const uint32_t expected = count == 0 || round % 4 == 0 ? 0 : round % 3 == 0 ? BUSTER_CHECK_LOWER_ENTRIES_X87 : BUSTER_CHECK_LOWER_ENTRIES_WIDE;
            char description[160];
            snprintf(description, sizeof(description), "the memory classes of %u entries' classes, round %u: %u by the vector path, %u by the scalar one, for %u", count,
                     round, vector, scalar, expected);
            check_test_expect(test, vector == expected && scalar == expected, description);
        }
    }
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
#define BUSTER_CHECK_TEST_X87_ENCODINGS \
    "typedef union{struct{unsigned long m;unsigned short e;}b;long double x;}L;" \
    "static const unsigned long ms[16]={0,0,1,0x8000000000000000UL,0x8000000000000001UL,0x8000000000000000UL,0x4000000000000000UL,0," \
    "0x8000000000000000UL,0x8000000000000000UL,0,0xc000000000000000UL,0x8000000000000001UL,0x4000000000000000UL,0xffffffffffffffffUL," \
    "0x7fffffffffffffffUL};" \
    "static const unsigned short es[16]={0,0x8000,0,0,0x8000,0x3fff,0x3fff,0x3fff,0x7fff,0xffff,0x7fff,0x7fff,0x7fff,0x7fff,0x7ffe,0x7ffe};" \
    "long double make(int n){L l;l.b.m=ms[(unsigned)n%16];l.b.e=es[(unsigned)n%16];return l.x;}"

// The builtins of long double that this compiler's own main.c calls (#148, #129): clang -O0 classifies an x87 value by
// the x87's compares or by its bits, by builtin, which the encodings tell apart. The fpclassify's normal and subnormal
// selectors are constants, since clang evaluates the two in an order its own build decides (check_lower_floating_prepare).
static __attribute__((noinline)) void check_test_program_long_double_builtins(CheckTest* test, const char* clang)
{
    check_test_program_source(test,
        BUSTER_CHECK_TEST_X87_ENCODINGS
        "int tested(int n){long double x=make(n);return __builtin_isnan(x)+2*__builtin_isinf(x)+4*__builtin_isfinite(x)+8*__builtin_signbit(x)+"
        "16*__builtin_fpclassify(0,1,2,3,4,x)+128*(__builtin_isinf_sign(x)+1);}", "long-double-classification-encodings", clang);
    check_test_program_source(test,
        BUSTER_CHECK_TEST_X87_ENCODINGS
        "int seen;int mark(int v){seen=seen*10+v;return v;}"
        "int tested(int n){seen=0;long double x=make(n);L l;l.x=__builtin_fabsl(x);int r=__builtin_fpclassify(mark(1),mark(2),3,4,mark(5),x);"
        "return (int)l.b.e+(int)(l.b.m>>48)*3+r*100000+seen+__builtin_signbit(x)*500000;}", "long-double-fabsl-and-fpclassify-selectors", clang);
    check_test_program_source(test,
        "#pragma STDC FENV_ACCESS ON\n"
        "int clear_fp(void);int fp_state(void);"
        BUSTER_CHECK_TEST_X87_ENCODINGS
        "int tested(int n){long double x=make(n);clear_fp();int a=__builtin_fpclassify(11,22,33,44,55,x);int f=fp_state();"
        "clear_fp();int b=__builtin_isinf_sign(x);int g=fp_state();clear_fp();int c=__builtin_isnan(x);int h=fp_state();"
        "return a*10000+f*1000+(b+1)*100+g*10+h+c*2;}", "long-double-fenv-classification-and-exceptions", clang);
}

// A small aggregate body must not clear its unit's unrelated graph. Poison the dead control-task suffix, as the
// next body's builder may, and retain reference epochs for nodes reached by earlier bodies. Both a completed walk
// and its conservative early stop must restore those marks before builtin/reference selection reads them.
static void check_test_program_return_marks(CheckTest* test)
{
    enum { TOKENS = 1024, CAPACITY = 8 * TOKENS + 16, NODES = 4096 };
    CheckTestTypes types = { 0 };
    Arena scratch = arena_reserve(UINT64_C(1) << 20);
    bool ready = scratch.base != NULL && check_test_types_open(&types);
    const TypeId record = ready ? type_tag(&types.table, BUSTER_TYPE_KIND_STRUCT, 1) : 0;
    const TypeMember member = { .name = 1, .type = BUSTER_TYPE_LONG, .bit_width = BUSTER_TYPE_NOT_BIT_FIELD };
    ready = ready && record != 0 && type_complete_record(&types.table, record, &member, 1, (TypeRecordAttributes){ 0 });
    TypeId function = ready ? type_function(&types.table, record, NULL, 0, 0) : 0;
    CheckOp* ops = BUSTER_ARENA_PUSH_ARRAY(&scratch, CheckOp, NODES);
    uint32_t* operands = BUSTER_ARENA_PUSH_ARRAY(&scratch, uint32_t, NODES);
    uint32_t* seconds = BUSTER_ARENA_PUSH_ARRAY(&scratch, uint32_t, NODES);
    uint32_t* absent = BUSTER_ARENA_PUSH_ARRAY(&scratch, uint32_t, NODES);
    uint32_t* marks = BUSTER_ARENA_PUSH_ARRAY(&scratch, uint32_t, NODES + 1);
    uint32_t* stack = BUSTER_ARENA_PUSH_ARRAY(&scratch, uint32_t, NODES + 1);
    uint32_t* labels = BUSTER_ARENA_PUSH_ARRAY(&scratch, uint32_t, CAPACITY + 1);
    CheckControlTask* tasks = BUSTER_ARENA_PUSH_ARRAY(&scratch, CheckControlTask, CAPACITY);
    ready = ready && function != 0 && ops != NULL && operands != NULL && seconds != NULL && absent != NULL && marks != NULL && stack != NULL && labels != NULL && tasks != NULL;
    check_test_expect(test, ready, "return warning mark fixture has bounded graph and control scratch");
    for (uint32_t unsafe = 0; ready && unsafe < 2; unsafe += 1)
    {
        for (uint32_t node = 0; node < NODES; node += 1)
        {
            ops[node] = CHECK_OP_VALUE;
            operands[node] = seconds[node] = absent[node] = BUSTER_CHECK_NONE;
            marks[node] = node == 0 ? unsafe : node % 2;
        }
        ops[0] = unsafe ? CHECK_OP_COMMA : CHECK_OP_VALUE;
        ops[1] = unsafe ? CHECK_OP_CALL : CHECK_OP_VALUE;
        operands[0] = unsafe ? 2 : BUSTER_CHECK_NONE;
        seconds[0] = unsafe ? 1 : BUSTER_CHECK_NONE;
        marks[NODES] = stack[NODES] = labels[CAPACITY] = UINT32_C(0x71625344);
        memset(tasks, 0xa5, (uint64_t)CAPACITY * sizeof(*tasks));
        const CheckInput input = { .token_count = TOKENS, .declared_types = &function,
                                   .graph = { .count = NODES, .ops = ops, .operand0 = operands, .operand1 = seconds, .operand2 = absent } };
        CheckProgram program = { .status = CHECK_PROGRAM_OK, .types = &types.table, .input = &input, .body_count = 2 };
        CheckControl entries[] = { { .kind = CHECK_CONTROL_RETURN, .node = 0 }, { .kind = CHECK_CONTROL_END, .node = BUSTER_CHECK_NONE } };
        CheckControlWriter writer = { .program = &program, .entries = entries, .tasks = tasks, .labels = labels, .capacity = CAPACITY };
        CheckProgramBody body = { .count = BUSTER_ARRAY_LENGTH(entries), .entry_count = BUSTER_ARRAY_LENGTH(entries) };
        check_program_return_flow(&writer, &body, marks, stack, NULL);
        bool same = body.return_flow && !body.falls && body.return_warning == 0 &&
                    marks[NODES] == UINT32_C(0x71625344) && stack[NODES] == UINT32_C(0x71625344) && labels[CAPACITY] == UINT32_C(0x71625344);
        for (uint32_t node = 0; same && node < NODES; node += 1)
        {
            same = marks[node] == (node == 0 ? unsafe : node % 2);
        }
        // The call stops its comma expression with the other operand still queued. Only one or three ledger words change.
        const uint8_t* bytes = (const uint8_t*)tasks;
        for (uint32_t byte = CAPACITY + (1 + 2 * unsafe) * sizeof(uint32_t); same && byte < CAPACITY + NODES; byte += 1)
        {
            same = bytes[byte] == 0xa5;
        }
        check_test_expect(test, same, "small return warning walks preserve liveness epochs and touch only visited-node scratch, including early stops");
    }
    arena_release(&scratch);
    check_test_types_close(&types);
}

#include "c_check_program_batch_test.c"
#include "c_check_literal_table_test.c"

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
    check_test_program_source(test,
        "struct P{float a,b,c,d;};struct Q{long a;double b;};struct B{long a,b,c,d;};"
        "long mixed(long a,long b,long c,long d,long e,long f,long g,long h,long i,long j,long k,long l,long m,long o,long p,long q,"
        "struct P x,struct Q y,struct B z,double r,double s){return a+b+c+d+e+f+g+h+i+j+k+l+m+o+p+q+(long)(x.a+x.b+x.c+x.d+y.b+r+s)+y.a+z.a+z.b+z.c+z.d;}"
        "struct B hidden(long a,long b,long c,long d,long e,long f,long g,long h,long i,long j,long k,long l,long m,long o,long p,long q,struct P x)"
        "{struct B r={a+b+c+d,e+f+g+h,i+j+k+l,m+o+p+q+(long)x.d};return r;}"
        "long var(int count,...){__builtin_va_list ap;__builtin_va_start(ap,count);long r=0;for(int i=0;i<count;i++){"
        "r+=__builtin_va_arg(ap,long);r+=(long)__builtin_va_arg(ap,double);struct P p=__builtin_va_arg(ap,struct P);r+=(long)(p.a+p.b+p.c+p.d);}"
        "__builtin_va_end(ap);return r;}"
        "int tested(int n){struct P p={n,n+1,n+2,n+3};struct Q q={n,n+0.5};struct B b={n,n+1,n+2,n+3};"
        "struct B r=hidden(n,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,p);"
        "return (int)(mixed(n,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,p,q,b,2.5,3.5)+r.a+r.b+r.c+r.d+"
        "var(7,(long)n,1.0,p,2L,3.0,p,4L,5.0,p,6L,7.0,p,8L,9.0,p,10L,11.0,p,12L,13.0,p));}",
        "deep-aggregate-mixed-and-variadic-calls", clang);
}

// _BitInt values narrower than their classes (#148): kept extended from their widths, a narrow one's operations computed
// in 32 bits, against clang -O0. Unsigned ones are spelled out, since a typedef of one loses its unsignedness on main.
#define BUSTER_CHECK_TEST_BIT_INTS \
    "typedef _BitInt(5) S5;typedef _BitInt(13) S13;typedef _BitInt(40) S40;\n#define U5 unsigned _BitInt(5)\n" \
    "#define U13 unsigned _BitInt(13)\n#define U27 unsigned _BitInt(27)\n#define U40 unsigned _BitInt(40)\n" \
    "static S5 table[3]={-3,7,-16};U5 u5(int n){return (U5)n;}S5 s5(int n){return (S5)(n%16);}"

static __attribute__((noinline)) void check_test_program_bit_ints(CheckTest* test, const char* clang)
{
    check_test_program_source(test,
        BUSTER_CHECK_TEST_BIT_INTS
        "int tested(int n){U5 a=u5(n*3),b=u5(n+29);U5 c=a+b,d=a*b,e=a-b,f=~a,g=a<<2,h=a>>1,i=a/(U5)(b|1u),j=a%(U5)(b|1u);"
        "S5 p=s5(n),q=s5(n+3);S5 r=-p,s=p&q,t=p^q,v=(S5)(p|q),k=p>>1;"
        "U13 big=(U13)(n*1000);big*=7;big<<=3;big-=(U13)n;big++;--big;big^=0x155;U27 m=(U27)n*1000003u;m+=(U27)0x7ffffff;"
        "U40 w=(U40)n*(U40)123456789;w+=w;w>>=3;S40 x=(S40)n*(S40)1000000;"
        "unsigned acc=(unsigned)c+3u*d+5u*e+7u*f+11u*g+13u*h+17u*(unsigned)(int)r+19u*(unsigned)(int)s+23u*i+29u*j;"
        "acc=acc*31u+(unsigned)(int)t+(unsigned)(int)v*3u+(unsigned)(int)k*5u+big*3u+m%1000u+(unsigned)(w%1000)+(unsigned)(x%997);"
        "return (int)(acc%1000003)+(a<b)+(p<q)*2+(big>4000)*4+(w>=x)*8+(a==0)*16+!p*32;}",
        "bit-int-arithmetic", clang);
    check_test_program_source(test,
        BUSTER_CHECK_TEST_BIT_INTS
        "struct F{S5 x:3;U13 y:9;S40 z:20;};"
        "int tested(int n){U5 a=u5(n*3);S5 p=s5(n);int sw=0;"
        "switch(a){case 3:sw=1;break;case 30:sw=2;break;case 9 ... 12:sw=4;break;default:sw=3;}"
        "switch(p){case -1:sw+=10;break;case -16 ... -10:sw+=20;break;default:sw+=30;}"
        "struct F fs={0};fs.x=(S5)(n&3);fs.y=(U13)(n*37);fs.z=(S40)(n-7)*1000;fs.y+=300;fs.x-=1;"
        "S13 dst=0;int o=__builtin_mul_overflow(n*1000,7,&dst);U5 small=0;int o2=__builtin_add_overflow(n,20,&small);"
        "S5 kk=(S5)-3;S5 z=(S5)(kk+(S5)1);"
        "return sw+(int)fs.x*100+(int)fs.y*1000+(int)(fs.z%1000)*7+o*13+o2*17+(int)dst*3+(int)small*19+(int)table[n&1]*23+(int)z*29;}",
        "bit-int-switches-fields-and-overflow-destinations", clang);
    check_test_program_source(test,
        BUSTER_CHECK_TEST_BIT_INTS
        "unsigned _BitInt(5) external_bit_int(_BitInt(5),unsigned _BitInt(13),_BitInt(40),unsigned _BitInt(5)*);"
        "int external_bit_apply(unsigned _BitInt(13)(*)(_BitInt(5),unsigned _BitInt(27)),int);"
        "U13 mine(S5 a,U27 b){return (U13)(b%8000)+(U13)a;}"
        "int tested(int n){union{unsigned char c;U5 v;}pu;pu.c=(unsigned char)(0xe0|n);union{unsigned char c;S5 v;}ps;ps.c=(unsigned char)(0xa0+n);"
        "union{unsigned long l;U40 v;S40 s;}pw;pw.l=0xfedcba9876543210UL+(unsigned long)n;"
        "static S5 t2[2]={-3,9};static U13 t3[2]={8000,3};static S40 t4=-5;"
        "const unsigned char*b=(const unsigned char*)t2;const unsigned char*c=(const unsigned char*)t3;const unsigned char*d=(const unsigned char*)&t4;"
        "union{S5 v;unsigned char c;}u1;u1.v=s5(n);union{U13 v;unsigned short s;}u2;u2.v=(U13)(n*1000);u2.v+=7000;"
        "U5 q=0;U5 r=external_bit_int(s5(n),(U13)(n*700),(S40)n*1073741824,&q);"
        "return (int)pu.v+(int)ps.v*7+(int)(pw.v%1009)+(int)(pw.s%1013)*3+b[n&1]+c[n&3]*3+d[n&7]*5+u1.c*11+u2.s*13+"
        "(int)r*100+(int)q+external_bit_apply(mine,n)*1000;}",
        "bit-int-memory-and-calls", clang);
    // Updates of storage whose bits past the width a union's other member or a character type set: the old value is read
    // as clang reads it, from its width alone, through a member, an address-taken local and a global, by shifts,
    // division, remainder and postfix increments, whose old value is the result (#534's review).
    check_test_program_source(test,
        BUSTER_CHECK_TEST_BIT_INTS
        "static U5 g5;static S13 g13;"
        "static void set13(S13*p,unsigned v){((unsigned char*)p)[0]=(unsigned char)v;((unsigned char*)p)[1]=(unsigned char)(v>>8);}"
        "int tested(int n){union{unsigned char raw;U5 value;}u;u.raw=(unsigned char)(0xe0|(n&1));u.value>>=1;int r=(int)u.value;"
        "union{unsigned char raw;S5 value;}s;s.raw=(unsigned char)(0x10|(n&15));s.value>>=1;r=r*3+(int)s.value;"
        "u.raw=(unsigned char)(0xe0|(n&31));u.value/=3;r=r*5+(int)u.value;u.raw=(unsigned char)(0xa0|(n&31));u.value%=7;r=r*7+(int)u.value;"
        "u.raw=(unsigned char)(0xe0|(n&31));r=r*11+(int)u.value++;r=r*13+(int)u.value;s.raw=(unsigned char)(0x60|(n&31));r=r*17+(int)s.value--;"
        "U5 local=0;unsigned char*p=(unsigned char*)&local;*p=(unsigned char)(0xc0|(n&31));local>>=2;r=r*19+(int)local;"
        "*p=(unsigned char)(0xa0|(n&31));r=r*37+(int)local++;r=r*43+(int)local;"
        "*(unsigned char*)&g5=(unsigned char)(0xe0|(n&31));g5/=5;r=r*23+(int)g5;*(unsigned char*)&g5=(unsigned char)(0x60|(n&31));r=r*41+(int)g5++;"
        "set13(&g13,0xe000u|(unsigned)(n*97&0x1fff));g13>>=3;r=r*29+(int)g13;set13(&g13,0x6000u|(unsigned)(n*89&0x1fff));g13%=1000;"
        "return r*31+(int)g13;}",
        "bit-int-updates-of-noncanonical-storage", clang);
    // Of classes as wide as int and long (_BitInt(30), unsigned _BitInt(30) and _BitInt(60)): casts of locals whose bits past
    // the width a character type set, which the batch's templates (c_check_program_batch.c) would read as the class's, and
    // conversions to one from a type of its class, which change its bits though their classes are the same.
    check_test_program_source(test,
        BUSTER_CHECK_TEST_BIT_INTS
        "typedef _BitInt(30) S30;typedef _BitInt(60) S60;\n#define U30 unsigned _BitInt(30)\n"
        "int tested(int n){S30 d=0;unsigned char*p=(unsigned char*)&d;p[0]=(unsigned char)n;p[3]=(unsigned char)(0x80|(n&0x7f));"
        "U30 e=0;unsigned char*q=(unsigned char*)&e;q[0]=(unsigned char)(n*3);q[3]=(unsigned char)(0xc0|(n&0x3f));"
        "S60 f=0;unsigned char*r=(unsigned char*)&f;r[0]=(unsigned char)(n*5);r[7]=(unsigned char)(0x90|(n&0xf));"
        "int x=(int)d;unsigned y=(unsigned)e;long z=(long)f;"
        "long g=(long)(S30)(n*0x10000000);unsigned long h=(unsigned long)(U30)((unsigned)n*0x50000000u);long k=(long)(S60)((long)n<<58);"
        "return (int)((x+(long)y*3+z%1000003*5+g*7+(long)(h%1000003)*11+k/(1L<<40)*13)%1000003);}",
        "bit-int-class-wide-casts-and-conversions", clang);
    // Of 65 to 128 bits, in the 128-bit classes (#137): every operation, kept extended from the width as the narrow ones
    // are, libgcc's division and conversions to and from double, constants past 64 bits, storage whose bits past the
    // width a union's bytes set, a switch, a pointer's update, and calls: an argument split between r9 and the stack.
    check_test_program_source(test,
        BUSTER_CHECK_TEST_BIT_INTS
        "typedef _BitInt(65) S65;typedef _BitInt(100) S100;typedef _BitInt(128) S128;\n#define U65 unsigned _BitInt(65)\n"
        "#define U80 unsigned _BitInt(80)\n#define U127 unsigned _BitInt(127)\n"
        "static S100 mul(S100 a,S100 b){return a*b;}static U127 shl(U127 a,int s){return a<<s;}"
        "static S65 split(long a,long b,long c,long d,long e,S65 f){return f+(S65)(a+b+c+d+e);}"
        "static U80 back(U80*p,int n){U80 r=*p;*p=r*(U80)n+(U80)7;return r;}"
        "int tested(int n){S65 a=(S65)n*(S65)0x123456789abcdefLL;S65 b=-a;S65 c=a>>3;S65 d=a/(S65)(n|1);S65 e=a%(S65)(n+7);S65 k=a*(S65)(n&7);"
        "U65 p=(U65)a;p=p*p;p+=(U65)1<<64;p=~p;"
        "S100 f=(S100)n*((S100)1<<80);f+=(S100)0x7fffffffffffffffLL;S100 g=mul((S100)n*((S100)1<<40),(S100)0x123456789LL);S100 h=~g^f;"
        "U127 u=(U127)n*(U127)0xfedcba9876543210ULL;u=u*u;u=u*u;U127 v=shl(u,n&63);U127 w=u/(U127)(n+4);U127 r=u%(U127)1000003;"
        "U80 x=(U80)-1;x+=(U80)n;x<<=3;U80 xb=back(&x,n);S128 y=(S128)a*(S128)(n+1);double z=(double)f;S100 fz=(S100)(z*0.5);"
        "union{unsigned char raw[16];S100 v;U80 w;}m;for(int i=0;i<16;i++)m.raw[i]=(unsigned char)(0xa5^(i*n));S100 mv=m.v;U80 mw=m.w;"
        "S65 s=split(1,2,3,4,(long)n,a);int sw=0;switch(f>>80){case 0:sw=1;break;case 1:sw=2;break;case 2:sw=4;break;default:sw=3;}"
        "unsigned long acc=(unsigned long)a^(unsigned long)(a>>40)*3+(unsigned long)b*5+(unsigned long)c*7+(unsigned long)d*11+(unsigned long)e*13+(unsigned long)k*17;"
        "acc=acc*29+(unsigned long)p+(unsigned long)(p>>60)*3+(unsigned long)(f>>50)+(unsigned long)g*3+(unsigned long)(g>>70)*5+(unsigned long)h*7+(unsigned long)(h>>64)*9;"
        "acc=acc*37+(unsigned long)u+(unsigned long)(u>>64)*3+(unsigned long)v*5+(unsigned long)(v>>100)*7+(unsigned long)w*11+(unsigned long)r*13;"
        "acc=acc*41+(unsigned long)x+(unsigned long)(x>>40)*3+(unsigned long)xb*5+(unsigned long)y+(unsigned long)(y>>64)*7+(unsigned long)fz*11;"
        "acc=acc*43+(unsigned long)mv+(unsigned long)(mv>>64)*3+(unsigned long)(mw>>16)*5+(unsigned long)s*7+(unsigned long)(s>>64)*11;"
        "return (int)(acc%1000003)+sw*7+(a<f)*2+(u>=(U127)n)*4+(mv==0)*8+!p*16;}",
        "bit-int-128-bit-classes", clang);
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
    check_test_program_source(test,
        "int tested(int n){_Atomic unsigned long cell=(unsigned long)(unsigned)n+1;"
        "unsigned shift=__builtin_ctzll(__c11_atomic_load(&cell,5)|8UL);"
        "unsigned long value=__builtin_bswap64(__c11_atomic_load(&cell,5)+1);"
        "cell+=__builtin_popcountll(value);"
        "unsigned long old=__c11_atomic_fetch_xor(&cell,__builtin_bswap64(value),5);"
        "return (int)((old^__c11_atomic_load(&cell,5))&0x7fffff00UL)+(int)shift;}",
        "atomic-captures-and-integer-recipe-scratch", clang);
}

static __attribute__((noinline)) void check_test_programs(CheckTest* test, const char* clang)
{
    check_test_program_return_marks(test);
    check_test_literal_tables(test, clang);
    check_test_program_builtin_scratch(test, clang);
    check_test_program_copies(test, clang);
    check_test_program_folds(test, clang);
    check_test_program_batch(test);
    check_test_program_many_arguments(test, clang);
    check_test_program_many_aggregate_returns(test, clang);
    check_test_program_source(test,
        "struct S{long a,b;};struct L{long a,b,c,d;};struct D{double a,b;};"
        "struct S choose(int n,struct S s){if(n>0)return s;else{struct S z={n,n+1};return z;}}"
        "struct L chain(int n,struct L s){if(n>0)return s;else if(n<0){s.b+=3;return s;}else{s.d+=5;return s;}}"
        "struct D select(int n,struct D s){switch(n&3){case 0:return s;case 1:s.a+=1;return s;default:s.b+=2;return s;}}"
        "struct S loop(int n,struct S s){for(;;){if(n&1)return s;else{s.a+=3;return s;}}}"
        "struct L nested(int n,struct L s){if(n){if(n&1)return s;s.c+=7;}else{return s;}return s;}"
        "struct S partial(int n,struct S s){if(n)return s;}struct L wide_partial(int n,struct L s){if(n)return s;}"
        "struct S labels(int n,struct S s){if(n)goto live;return s;live:s.a+=11;return s;}"
        "int tested(int n){struct S s={n,n+2};struct L l={n,n+1,n+2,n+3};struct D d={n*0.5,n*0.25};"
        "struct S a=choose(n,s),b=loop(n,s),c=partial(1,s),e=labels(n,s);"
        "struct L p=chain(n,l),q=nested(n,l),r=wide_partial(1,l);struct D v=select(n,d);"
        "partial(0,s);wide_partial(0,l);return a.a+a.b+b.a+b.b+c.a+c.b+e.a+e.b+p.a+p.b+p.c+p.d+q.c+r.d+(int)(v.a*4+v.b*8);}",
        "aggregate-return-control-flow", clang);
    check_test_program_source(test,
        "static volatile int seen;static int touch(int n){seen+=(n&3);return seen;}"
        "int tested(int n){signed char a=n;unsigned short b=n+5;double d=(double)(n-3)*0.25;"
        "int v=((a^b)+(int)d)*3;v+=(n&&touch(n))+(n?touch(2):touch(3));"
        "v+=(int)((unsigned)n>>((n&7)+1));v+=(n%13)/((n&7)+1);return v+(_Bool)n;}",
        "batch-ordinary-and-ordered-fallback", clang);
    check_test_program_source(test,
        "struct part{char tag;int value;double real;struct part*next;};"
        "static volatile int side;static int touch(int n){side+=n&3;return side;}"
        "int tested(int n){struct part b={2,n+7,(double)n*0.5,0},a={1,n-3,0.25,&b};"
        "struct part*p=&a;p->value=(short)(unsigned char)(n+9);"
        "int v=(short)(unsigned char)p->value+(int)p->next->real+(int)(double)p->next->value;"
        "v+=(_Bool)p->next->value;v+=n?touch(p->value):touch(p->next->value);return v+side;}",
        "batch-member-address-load-conversion-chains", clang);
    check_test_program_long_double_builtins(test, clang);
    check_test_program_bit_ints(test, clang);
    check_test_control_masks(test);
    check_test_control_walks(test);
    check_test_program_addressed(test);
    check_test_program_scans(test);
    check_test_program_bodies(test, clang);
    check_test_program_declaring(test);
    check_test_program_refused(test);
    check_test_program_type_facts(test);
    check_test_program_selection(test, clang);
    check_test_program_queries(test, clang);
    check_test_program_region_proofs(test, clang);
    check_test_program_helpers(test);
    check_test_program_atomics(test, clang);
    check_test_program_diagnostics(test, clang);
    check_test_program_vectors(test, clang);
    check_test_program_x86(test, clang);
    check_test_program_vector_builtins(test, clang);
    check_test_program_fp16(test, clang);
    check_test_program_source(test,
        "int tested(int n){int x=0,y=1;if(n){x=2;}else x=3;while(n){if(x)break;n--;}do x++;while(x<4);"
        "for(int i=0;i<n;i++){if(i==1)continue;x+=i;}return x+y;}", "structured-loops", clang);
    check_test_program_source(test,
        "int tested(int n){int x=2;int y=(n>0&&++x);int z=(n<0||++x);int q=n?x++:++x;"
        "return x*1000+y*100+z*10+q;}", "short-circuit-updates", clang);
    check_test_program_source(test,
        "int helper(int a,int b,int c,int d,int e,int f,int g,int h){return a+b*2+c*3+d*4+e*5+f*6+g*7+h*8;}"
        "int tested(int n){return helper(n,1,2,3,4,5,6,7)+helper(1,2,3,4,5,6,7,n);}", "stack-argument-calls", clang);
    check_test_program_source(test,
        "double helper(double a,float b,int c){return a*2.0+b+c;}"
        "int tested(int n){double d=helper(n+0.25,1.5f,n);return (d?7:3)+(int)d;}", "floating-call-conversions", clang);
    check_test_program_source(test,
        "int snprintf(char*,unsigned long,const char*,...);char text[64];int tested(int n){"
        "int length=snprintf(text,64,\"%d %.1f\",n,(float)n+0.5f);return length*100+text[0];}", "external-variadic-call-abi", clang);
    check_test_program_source(test,
        "double helper(double a,double b,double c,double d,double e,double f,double g,double h,double i,double j)"
        "{return a+b+c+d+e+f+g+h+i+j;}int tested(int n){return (int)helper(n,1,2,3,4,5,6,7,8,9);}", "floating-stack-arguments", clang);
    check_test_program_source(test,
        "int tested(int n){int x=n;int*p=&x;int a=(*p)++;int b=++*p;*p+=3;return a+2*b+4*x;}", "addressed-local-updates", clang);
    // An address constant cast to an integer type, of a class other than a pointer's (musl's syscalls take one).
    check_test_program_source(test,
        "volatile int lock;long sys(long n,long a){return n*3+(a&7);}long base(void){return (long)&lock;}"
        "int tested(int n){long a=(long)&lock;long b=(long)(&lock+2);int c=(int)(long)&lock;return (int)(b-a)+(c==(int)a)+(base()==a)+(int)sys(n,(long)&lock)-(int)((long)&lock&7);}",
        "address-constants-as-integers", clang);
    check_test_program_source(test,
        "int tested(int n){signed char a=(signed char)(n+125);unsigned char b=(unsigned char)(n+250);"
        "_Bool c=(_Bool)(n*256);return (int)a+(int)b*3+c*700;}", "narrow-values-and-bool", clang);
    // Plain char unsigned (-funsigned-char) and signed, from one source: stores and loads of values past 0x7f,
    // conversions, division, shifts and compares, a switch, character constants, and a char parameter and result.
    static const char plain_char[] =
        "static char table[]={'\\x80','\\xff','a','\\x7f',0};static char pass(char c){return c;}static int widen(char c){return c;}"
        "int tested(int n){char c=(char)(n*37+128);int r=c/3+(c>>1)+(c>100)+widen(pass(c))+'\\xff'+(char)-1;"
        "r+=table[(unsigned)n%5]+(table[(unsigned)n%5]<0)*1000;switch(c){case '\\xff':r+=7;break;case '\\x80':r+=11;break;default:break;}"
        "table[(unsigned)n%5]=c;return r;}";
    check_test_program_source_char(test, plain_char, "plain-char-unsigned", clang, true);
    check_test_program_source_char(test, plain_char, "plain-char-signed", clang, false);
    check_test_program_source(test,
        "int global=7;int helper(int x){global+=x;return global;}"
        "int tested(int n){int a=helper(n);global+=2;return a*3+global;}", "shared-code-and-data", clang);
    check_test_program_source(test,
        "extern int external_data;int external_helper(int);"
        "int tested(int n){return external_helper(n)+external_data;}", "external-code-and-data", clang);
    check_test_program_source(test,
        "_Thread_local int saved=9;int tested(int n){return n+1;}", "unreferenced-tls-definition", clang);
    check_test_program_source(test,
        "_Thread_local int counter=9;_Thread_local long wide[4];int tested(int n){counter+=n;wide[n&3]+=counter;int*p=&counter;"
        "return *p*2+(int)wide[(n+1)&3];}", "thread-local-references", clang);
    check_test_program_source(test,
        "int missing(void);int tested(int n){return (0&&missing())+(1? n+2:missing())+(int)sizeof(missing());}", "unevaluated-references", clang);
    check_test_program_source(test,
        "int tested(int n){void*p=(void*)100;p++;p+=3;return (char*)p-(char*)100+n;}", "gnu-byte-pointer-stride", clang);
    check_test_program_source(test,
        "int tested(int n){int x=7;switch(n++){case -2 ... 0:x=1;break;case 1:x=2;case 2:x+=4;break;default:x=9;}return x*10+n;}",
        "switch-range-and-fallthrough", clang);
    // Jump tables (#136's item 7), each switch swept past its cases on both sides: holes, defaults in the middle and
    // none, fallthrough, GNU ranges, negative and unsigned values at their types' ends, nested tables, and in loops.
    check_test_program_source(test,
        "static int dense(int x){switch(x){case 0:return 3;case 1:return 5;case 2:return 7;case 3:return 11;case 5:return 13;case 7:return 17;default:return -1;}}"
        "static int nodefault(int x){int r=7;switch(x){case 1:r=1;break;case 2:r=2;break;case 3:r=3;break;case 4:r=4;break;}return r;}"
        "int tested(int n){unsigned s=0;for(int i=-6;i<14;i++){s=s*3+(unsigned)dense(i+n);s=s*5+(unsigned)nodefault(i-n);}return (int)s;}",
        "switch-table-dense", clang);
    check_test_program_source(test,
        "static int ranged(int x){int r=0;switch(x){case -7 ... -5:r+=1;case -4:r+=2;break;default:r+=100;case -1:r+=4;break;"
        "case 0 ... 2:r+=8;break;case 4:r+=16;case 5:r+=32;break;}return r;}"
        "int tested(int n){unsigned s=0;for(int i=-10;i<10;i++)s=s*7+(unsigned)ranged(i+n-5);return (int)s;}",
        "switch-table-ranges", clang);
    check_test_program_source(test,
        "static int top(unsigned x){switch(x){case 4294967290u:return 1;case 4294967291u:return 2;case 4294967292u:return 3;"
        "case 4294967294u:return 4;case 4294967295u:return 5;default:return 0;}}"
        "static int utop(unsigned long long x){switch(x){case 18446744073709551610ull:return 1;case 18446744073709551611ull:return 2;"
        "case 18446744073709551613ull:return 3;case 18446744073709551614ull:return 4;case 18446744073709551615ull:return 5;default:return 0;}}"
        "int tested(int n){unsigned s=0;for(int i=-8;i<8;i++){s=s*5+(unsigned)top((unsigned)(i+n-12));s=s*7+(unsigned)utop((unsigned long long)(long long)(i+n-12));}"
        "return (int)s;}",
        "switch-table-unsigned-ends", clang);
    check_test_program_source(test,
        "static int wide(long x){switch(x){case -9223372036854775807L-1:return 1;case -9223372036854775807L:return 2;case -9223372036854775806L:return 3;"
        "case -9223372036854775805L:return 4;case -9223372036854775803L:return 5;default:return 6;}}"
        "static int high(long x){switch(x){case 9223372036854775803L:return 1;case 9223372036854775804L:return 2;case 9223372036854775805L:return 3;"
        "case 9223372036854775806L:return 4;case 9223372036854775807L:return 5;default:return 6;}}"
        "int tested(int n){unsigned s=0;for(int i=0;i<10;i++){s=s*7+(unsigned)wide((-9223372036854775807L-1)+(long)(i+n+3));"
        "s=s*7+(unsigned)high(9223372036854775807L-(long)(i+n+3));s=s*7+(unsigned)wide((long)(i+n));}return (int)s;}",
        "switch-table-long-ends", clang);
    check_test_program_source(test,
        "static int nested(int a,int b){int r=0;switch(a){case 0:switch(b){case 0:r=1;break;case 1:r=2;break;case 2:r=3;break;case 3:r=4;break;default:r=5;}break;"
        "case 1:r=10;break;case 2:r=20;case 3:r+=30;break;default:r=-1;}return r;}"
        "int tested(int n){unsigned s=0;for(int i=0;i<30;i++){if(i%7==6)continue;switch(i%5){case 0:s+=1;break;case 1:s*=3;"
        "case 2:s+=(unsigned)nested(i%4,(i+n)%6);break;case 3:if(i>20)break;s^=0x55;break;case 4:s+=(unsigned)n;break;}}return (int)s;}",
        "switch-table-nested-loop", clang);
    check_test_program_source(test,
        "enum color{red,green,blue,cyan,magenta,yellow};"
        "static int named(enum color c){switch(c){case red:return 1;case green:return 2;case blue:return 3;case cyan:return 4;case yellow:return 6;default:return 0;}}"
        "static int chars(char c){switch(c){case 'a':return 1;case 'b':return 2;case 'c':return 3;case 'e':return 5;case 'f':return 6;default:return 0;}}"
        "int tested(int n){unsigned s=0;for(int i=-3;i<9;i++)s=s*11+(unsigned)named((enum color)(i+n))+(unsigned)chars((char)('a'+i+n));return (int)s;}",
        "switch-table-enum-char", clang);
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
    // Empty GNU ranges (#536's review), which match nothing and which clang drops before LLVM sees the switch: last by
    // their lower values, where one sized the table short of the case below it and its entry was written past the table's
    // end; first, below the table's lowest value; as the fourth case, which they don't make; and an unsigned one past
    // INT32_MAX. Then the same switches swept past their cases on both sides against clang's build.
    check_test_program_tables(test, "int f(int x){switch(x){case 0:return 3;case 1:return 5;case 2:return 7;case 4:return 9;case 5 ... 3:return 11;default:return -1;}}",
                              "switch-tables-empty-range-last", 1, 5);
    check_test_program_tables(test, "int f(int x){switch(x){case -5 ... -9:return 1;case 0:return 3;case 1:return 5;case 2:return 7;case 3:return 9;default:return -1;}}",
                              "switch-tables-empty-range-first", 1, 4);
    check_test_program_tables(test, "int f(int x){switch(x){case 0:return 3;case 1:return 5;case 2:return 7;case 9 ... 4:return 9;default:return -1;}}",
                              "switch-tables-empty-range-fourth", 0, 0);
    check_test_program_tables(test, "int f(unsigned x){switch(x){case 4294967295u ... 0u:return 1;case 7:return 3;case 8:return 5;case 9:return 7;case 11:return 9;"
                                    "default:return -1;}}", "switch-tables-empty-range-unsigned", 1, 5);
    check_test_program_source(test,
        "static int last(int x){switch(x){case 0:return 3;case 1:return 5;case 2:return 7;case 4:return 9;case 5 ... 3:return 11;default:return -1;}}"
        "static int first(int x){switch(x){case -5 ... -9:return 1;case 0:return 3;case 1:return 5;case 2:return 7;case 3:return 9;default:return -1;}}"
        "static int fourth(int x){switch(x){case 0:return 3;case 1:return 5;case 2:return 7;case 9 ... 4:return 9;default:return -1;}}"
        "static int high(unsigned x){switch(x){case 4294967295u ... 0u:return 1;case 7:return 3;case 8:return 5;case 9:return 7;case 11:return 9;default:return -1;}}"
        "int tested(int n){unsigned s=0;for(int i=-12;i<14;i++){s=s*7+(unsigned)last(i+n);s=s*5+(unsigned)first(i-n);s=s*3+(unsigned)fourth(i+n);"
        "s=s*11+(unsigned)high((unsigned)(i+n));}return (int)s;}",
        "switch-table-empty-ranges", clang);
    check_test_program_source(test,
        "int tested(int n){int x=1;switch(n){case 3 ... 1:x=7;break;case 2:x=5;break;default:x=9;}return x;}",
        "empty-gnu-case-range-is-warning-only", clang);
    check_test_program_source(test,
        "int tested(int n){int x=0;for(int i=0;i<n;i++){switch(i%4){case 0:continue;case 1:x+=3;break;default:switch(i%2){case 0:x++;break;default:x+=7;}}x+=2;}return x;}",
        "nested-switch-continue", clang);
    check_test_program_source(test,
        "int tested(int n){int x=0;if(n<0)n=-n;again:x+=n;if(--n>0)goto again;goto done;x=999;done:return x;}", "forward-and-backward-goto", clang);
    check_test_program_source(test,
        "int helper(int n){return n+2;}int(*saved)(int)=helper;int tested(int n){return (saved==helper)+n;}", "function-address-data-reference", clang);
    check_test_program_source(test,
        "char*text=\"abc\";int tested(int n){return text[1]+n;}", "body-used-string-object", clang);
    // The function names' arrays (__func__, __FUNCTION__, __PRETTY_FUNCTION__), private constants as a literal's are:
    // returned, indexed, taken whole, in a static's initializer and at file scope (an empty one). In static functions,
    // whose names the oracle's source keeps (it renames the global ones).
    check_test_program_source(test,
        "static unsigned fold(const char*s){unsigned h=0;while(*s)h=h*31+(unsigned char)*s++;return h;}"
        "static const char*top=__func__;static const char*named(void){return __func__;}"
        "static const char*pretty(int k,const char*(*f)(void)){static const char*own=__FUNCTION__;return k&1?__PRETTY_FUNCTION__:k&2?own:f();}"
        "static int sized(int n){const char(*p)[6]=&__func__;return (int)sizeof(__PRETTY_FUNCTION__)*100+(*p)[n&3]+__func__[4];}"
        "int tested(int n){return (int)(fold(top)^fold(named())*3^fold(pretty(n,named))*5)+sized(n)+n;}",
        "function-name-arrays", clang);
    check_test_program_source(test,
        "int tested(int n){int x=0;int y=n?(int){++x}:(int){x+=3};return x*10+y+(int)sizeof((int){++x});}", "scalar-compound-evaluation", clang);
    check_test_program_source(test,
        "int values[4]={3,7,11,19};struct S{int x;int*p;};struct S saved={5,values};"
        "int tested(volatile int n){struct S*p=&saved;return p->x+saved.p[(unsigned)n%4]+values[2];}", "aggregate-data-body-addresses", clang);
    // Frame objects: local arrays, structs and unions (#145's c-testsuite programs). dirty() first leaves nonzero bytes
    // where inner's frame goes, so that an initializer's implicit zeros must be written.
    check_test_program_source(test,
        "int tested(int n){int a[8];for(int i=0;i<8;i++)a[i]=i*n;short s[3]={1,2};char c[5]=\"ab\";int m[2][3]={{1},{4,5,6}};"
        "return a[n&7]+s[2]+s[1]*10+c[1]+c[3]*7+(int)sizeof a+m[0][1]+m[1][2]*3;}", "local-arrays", clang);
    check_test_program_source(test,
        "struct P{int x;long y;char z[3];struct{short h;}w;};int dirty(int n){volatile char b[512];for(int i=0;i<512;i++)b[i]=(char)(i|n|1);return b[n&511];}"
        "int inner(int n){struct P p={.y=n};struct P q={n,2,{3},{4}};char big[300]={'a'};int r[40]={[20]=n};"
        "return p.x+(int)p.y+p.z[2]+p.w.h+q.z[0]*10+q.z[1]+q.w.h+(int)q.y+big[299]+big[0]+r[19]+r[20]+r[39];}"
        "int tested(int n){return dirty(n)*0+inner(n);}", "local-initializer-zeros", clang);
    check_test_program_source(test,
        "struct A{int v[3];};struct B{unsigned char c[300];};union U{int i;unsigned char c[6];};"
        "int tested(int n){struct A a={{n,2,3}},b;b=a;a.v[0]=9;struct B x,y;for(int i=0;i<300;i++)x.c[i]=(char)(i+n);y=x;"
        "union U u,w;u.i=n;w=u;struct A c=b;struct A d;int v=(d=c).v[1];return b.v[0]+b.v[2]+a.v[0]+y.c[299]+y.c[n&255]+w.c[0]+c.v[0]+d.v[2]+v;}",
        "aggregate-assignment-copies", clang);
    check_test_program_source(test,
        "struct S{int a,b;};struct T{struct S s;int c;};int sum(struct S*q){return q->a*10+q->b;}"
        "int tested(int n){int t=0;for(int i=0;i<3;i++)t+=sum(&(struct S){i,n});struct S s={n,2};struct T u={s,3};struct S arr[3]={{1,2},{3}};"
        "struct S z=n&1?s:arr[0];struct S e=({struct S w={n,n+1};w;});"
        "return t+((int[]){5,6,7})[n&1]+u.s.a*7+u.c+arr[1].a+arr[1].b+arr[2].a+z.a+e.b;}", "aggregate-values-and-compounds", clang);
    // A discarded comma discards its right operand too: musl's vfwprintf.c copies a union with a long double so, and
    // a folded comma's constant is nothing (#440).
    check_test_program_source(test,
        "union A{unsigned long i;long double f;void*p;};"
        "int tested(int n){union A arg,list[4];int type[4]={0};for(int i=0;i<4;i++)list[i].f=n*0.5L+i;"
        "int k=n&3;arg.f=0;if(n>=0)type[k]=n,arg=list[k];else (k,5);return (int)(arg.f*4)+type[k]*100;}", "comma-discards-aggregate-copy", clang);
    // An aggregate assignment's value, the destination's address, read after its copy by another copy, a load at
    // offset 0, a return, a conditional's arm, and after the pieces of a copy past 256 bytes (#440).
    check_test_program_source(test,
        "struct S{long a,b;};struct F{long double v;int k;};struct L{long a;unsigned char c[300];};"
        "struct S pick(struct S*t,int n){struct S r;return r=t[n&1];}"
        "int tested(int n){struct S s,t,w[2]={{n,2},{3,n}};struct F f,g[2]={{n*0.25L,1},{2.5L,n}};struct L x,y,z;"
        "s=(t=w[n&1]);long a=(t=w[1]).a;long double v=(f=g[n&1]).v;struct S r=pick(w,n);struct S c=n>3?(t=w[0]):w[1];"
        "for(int i=0;i<300;i++)z.c[i]=(unsigned char)(i^n);z.a=n;x=(y=z);"
        "return (int)(s.a+s.b*3+t.b*5+a*7+r.a*11+c.b*13+x.a*17+f.k*19)+(int)(v*8)+x.c[n&255]+y.c[299];}",
        "aggregate-assignment-values", clang);
    check_test_program_source(test,
        "int add(int a,int b){return a+b;}int mul(int a,int b){return a*b;}int(*pick(int n))(int,int){return n&1?add:mul;}"
        "int apply(int(*f)(int,int),int x){return (*f)(x,x);}"
        "int tested(int n){int(*f)(int,int)=n<0?add:mul;int(*t[2])(int,int)={add,mul};"
        "return f(n,3)+(*t[n&1])(n,4)+pick(n)(2,n)+apply(t[0],n)+apply(&mul,2);}", "indirect-calls", clang);
    // A statement region makes the lowering track the stack, where an indirect call pops the address it calls too:
    // returns and joins after one must find nothing left behind (#377's review).
    check_test_program_source(test,
        "int inc(int x){return x+1;}int dec(int x){return x-1;}"
        "int tested(int n){int(*f)(int)=n&1?inc:dec;int y=({int t=f(n);t*2;});if(n>5)return f(y);int z=n>2?f(n):({f(y)+1;});return y+z;}",
        "indirect-calls-tracked", clang);
    check_test_program_source(test,
        "int tested(int n){char c=(char)n;unsigned char u=(unsigned char)(n*16);short s=(short)(n*4096);_Bool b=c;_Bool d=s;"
        "return !c*1000+!u*100+!s*10+b+d*2+(c&&u)*4+(s||u)*8+(c?16:0);}", "narrow-truth-values", clang);
    // Bit-fields (#145's 00218): reads sign- or zero-extend, stores truncate and keep the neighbors' bits, whose
    // bytes (3, 5 and 8 of them here) are accessed as pieces; an assignment's value is the field's as it now reads.
    check_test_program_source(test,
        "struct B{unsigned a:3;int b:5;unsigned c:20;char d;long long e:40;unsigned long long f:64;_Bool g:1;};"
        "int tested(int n){struct B s;s.d=99;s.a=(unsigned)n;s.b=n*3;s.c=(unsigned)(n*100003);s.e=(long long)n*-1000000007LL;"
        "s.f=(unsigned long long)n*0x9e3779b97f4a7c15ULL;s.g=n&2;int v=(s.b=n*7);int w=(s.a=n+9);"
        "return s.a+s.b*3+(int)(s.c%1009)+s.d+(int)(s.e%997)+(int)(s.f>>58)+s.g*5+v*11+w*13;}", "bit-field-reads-and-stores", clang);
    check_test_program_source(test,
        "struct C{unsigned x:7;unsigned y:9;int z:13;int w:3;};"
        "int tested(int n){struct C c={1,2,3,-1};struct C*p=&c;c.x++;++c.y;p->z-=n*50;p->w+=n;int a=c.x++;int b=--p->y;int d=(p->z*=3);"
        "int e=(c.w<<=1);c.y+=n*37;c.x^=n;p->z/=n|1;c.w%=3;return c.x+c.y*3+c.z*5+c.w*7+a*11+b*13+d*17+e*19;}", "bit-field-updates", clang);
    check_test_program_source(test,
        "struct D{unsigned char before;unsigned v:20;unsigned char after;unsigned long long big:40;unsigned char tail;};"
        "union U{struct{unsigned lo:4,hi:4;}s;unsigned char b;};"
        "int tested(int n){struct D d={0x5a,(unsigned)n*4099,0xa5,(unsigned long long)n<<30,7};struct D e={.v=3};d.v=(unsigned)n;d.big=~0ULL;"
        "union U u={.s={.lo=n,.hi=(unsigned)n>>4}};u.s.hi=9;struct D f=(struct D){.big=(unsigned long long)n,.after=1};"
        "return d.before+d.after*3+(int)d.v*5+(int)(d.big>>20)+d.tail+e.v*7+e.before+e.after+u.b+(int)f.big+f.after+f.v;}",
        "bit-field-initializers-and-neighbors", clang);
    check_test_program_source(test,
        "enum E{E0=0,E1=200,E2};enum F{F0=-3,F1=3};"
        "struct G{enum E e:8;enum F f:3;unsigned char c:3;signed char s:4;unsigned short h:10;_Bool b:1;};"
        "int tested(int n){struct G g={E2,F0,5,-2,1000,1};g.e=(enum E)(E1+n);g.f=n&1?F0:F1;g.s=(signed char)n;"
        "_Generic(0,int:g.h)=(unsigned short)(n*77);g.c++;int t=(g.b?g.s:0)+g.c*2;"
        "return (int)g.e+(int)g.f*3+g.c+g.s*5+g.h+g.b*7+t*11+(g.e>E1)*13;}", "bit-field-signedness-and-selection", clang);
    check_test_program_source(test,
        "struct __attribute__((packed)) P6{unsigned char k:4;unsigned long long x:44;};"
        "struct __attribute__((packed)) P7{unsigned char k:4;long long y:52;};struct __attribute__((packed)) P3{unsigned char k:4;int z:20;};"
        "int tested(int n){struct P6 a={1,2};struct P7 b={3,-4};struct P3 c[2]={{5,-6},{7,8}};a.x=(unsigned long long)n*0x123456789ULL;"
        "b.y=(long long)n*-0x1234567891LL;c[1].z=n*-30001;a.x+=7;b.y--;int t=(int)(c[1].z++);c[0].z^=n;"
        "return (int)(a.x%1013)+(int)(b.y%1019)+c[1].z+c[0].z*3+a.k+b.k*5+c[0].k+c[1].k*7+t*11;}", "bit-field-packed-spans", clang);
    check_test_program_source(test,
        "int tested(int n){unsigned x=(unsigned)n|1;return __builtin_popcount(x)+__builtin_clz(x)+__builtin_ctz(x)+"
        "__builtin_parity(x)+__builtin_ffs((int)x)+(int)(__builtin_bswap16((unsigned short)x)%101);}", "integer-builtin-runtime", clang);
    check_test_program_source(test,
        "int tested(int n){int x=0;int a=__builtin_add_overflow(n,2147483647,&x);unsigned char y=0;"
        "int b=__builtin_mul_overflow((long)n,111ULL,&y);return a*1000+b*100+y+(int)((unsigned)x%97);}", "overflow-runtime-widths", clang);
    check_test_program_source(test,
        "int saved;int visits;int*slot(void){visits++;return &saved;}int tested(int n){visits=0;"
        "int a=__builtin_sub_overflow(n,7,slot());int b=__builtin_add_overflow(7,8,&saved);return visits*100+a*10+b+saved;}",
        "overflow-destination-once-and-folded-store", clang);
    check_test_program_source(test,
        "int tested(int n){int x=9;int a=0&&__builtin_add_overflow(1,2,(int*)0);"
        "int b=n?__builtin_add_overflow(n,7,&x):__builtin_mul_overflow(3,4,&x);"
        "return a+b+x+(int)sizeof(__builtin_add_overflow(7,8,&x));}", "overflow-selected-effects", clang);
    check_test_program_source(test,
        "int tested(int n){n+=10;typedef int A[n++];int x=n;n+=3;return (int)sizeof(A)+(int)sizeof(A)+n+x;}",
        "vla-typedef-captures-bound-once", clang);
    check_test_program_source(test,
        "int tested(int n){n+=10;typedef int A[n++];A a;int x=n;{A b;__typeof__(int[n++]) c,d;"
        "x+=(int)sizeof b+(int)sizeof c+(int)sizeof d;}int(*p)[n++],(*q)[n++];"
        "return x+n+(int)sizeof a+(int)sizeof *p+(int)sizeof *q;}", "vla-declaration-span-scopes-and-commas", clang);
    check_test_program_source(test,
        "int tested(int n){n+=10;int(*p)[++n],x=(n*=2),(*q)[++n];return (int)sizeof(*p)+(int)sizeof(*q)+n+x;}",
        "vla-declarator-and-initializer-order", clang);
    check_test_program_source(test,
        "int size(int n,int(*p)[n]){n+=7;return (int)sizeof(*p)+n;}int tested(int n){return size(n+10,0);}",
        "vla-parameter-bound-capture", clang);
    check_test_program_source(test,
        "int tested(int n){n+=10;int x=sizeof(int[n++]);return x+(int)sizeof(int[n++])+n;}",
        "vla-inline-typename-captures", clang);
    check_test_program_source(test,
        "int tested(int n){int sum=0;for(int i=1;i<4;i++){typedef int A[n+10+i];sum+=sizeof(A);sum+=sizeof(A);}return sum;}",
        "vla-loop-lifetime-recaptures", clang);
    // Variable-length arrays' objects: their strides and sizes from the extents their declarations captured, and the
    // stack they take released at their scopes' ends, by continue, break and goto out of them, and by a goto back before
    // their declaration. Each loop takes 20,000 arrays of about 4 KB or 10,000 of 8 KB: 80 MB, past the 8 MB stack, if
    // one release is missing.
    check_test_program_source(test,
        "int tested(int n){int m=(n&3)+2;int a[m][m+1];for(int i=0;i<m;i++)for(int j=0;j<=m;j++)a[i][j]=i*10+j;"
        "int(*p)[m+1]=a;p++;int(*q)[m+1]=a+m-1;q-=1;typedef char T[m*3];m=99;T t;t[0]=(char)(n&63);"
        "return (int)sizeof a+(int)sizeof a[0]*10+(*p)[1]*100+q[1][m-97]+(int)(q-p)*1000+(int)sizeof t+t[0];}",
        "vla-objects-strides-and-sizes", clang);
    check_test_program_source(test,
        "long sum(int n,int m,int a[n][m]){long s=0;for(int i=0;i<n;i++)for(int j=0;j<m;j++)s+=a[i][j]*(i+1);return s+(long)sizeof(a[0]);}"
        "int tested(int n){int r=(n&3)+1,c=(n&7)+2;int a[r][c];for(int i=0;i<r;i++)for(int j=0;j<c;j++)a[i][j]=i*c+j-n;"
        "int x=({int y=0;{int b[c];b[c-1]=n;y=b[c-1]*2;}y;});return (int)sum(r,c,a)+x;}",
        "vla-parameter-strides", clang);
    check_test_program_source(test,
        "int tested(int n){long s=0;for(int i=0;i<20000;i++){int a[1000+(n&7)];a[0]=i;a[999]=n;if(i%3==0)continue;"
        "s+=a[0]+a[999];if(i==19990)break;}int k=0;while(k<20000){{char c[4000+(n&3)];c[0]=(char)(k&63);k++;if(k&1)goto next;"
        "s+=c[0];}next:;}do{short d[2000+(n&1)];d[1]=(short)k;s+=d[1];if(--k&1)continue;}while(k>0);"
        "for(int i=0;i<20000;i++)switch((i+n)&1){case 0:{long e[60];long f[500+(n&3)];e[0]=i;f[0]=n;s+=f[0]+e[0];break;}default:s+=1;}"
        "return (int)(s%1000003);}",
        "vla-scope-releases", clang);
    check_test_program_source(test,
        "int tested(int n){int k=0;long s=0;again:;int a[2000+(n&3)];a[1999]=k;s+=a[1999];if(++k<10000)goto again;"
        "return (int)(s%1000003)+(int)sizeof a;}",
        "vla-backward-goto-releases", clang);
    check_test_program_source(test,
        "int tested(int n){long s=0;for(int i=0;i<20000;i++){int g[1000+(n&3)];g[0]=i;"
        "switch((i+n)&3){case 0:continue;case 1:{int h[(n&7)+1];h[0]=i;s+=h[0];break;}default:break;}s+=g[0];}"
        "return (int)(s%1000003);}",
        "vla-continue-through-switch", clang);
    // Objects aligned past 16 bytes, in realigned frames (Tier0Frame.alignment_log2): arrays, structs, a long double and
    // scalars (values aligned past 8 bytes are objects too), their addresses' low bits, which must be 0, in the result.
    check_test_program_source(test,
        "typedef struct{_Alignas(64) int a;long b;}S64;int tested(int n){_Alignas(32) int x=n;_Alignas(4096) char page[100];S64 s;"
        "_Alignas(16) double d=n*0.5;_Alignas(32) long double e=n+0.25L;page[0]=(char)n;page[99]=(char)(n+1);s.a=n*3;s.b=n*5;"
        "unsigned long mask=((unsigned long)&x&31)|((unsigned long)page&4095)|((unsigned long)&s&63)|((unsigned long)&d&15)|((unsigned long)&e&31);"
        "x+=7;d*=3;return x+page[0]+page[99]+s.a+(int)s.b+(int)d+(int)e+(int)mask*1000;}",
        "objects-aligned-past-16", clang);
    // Variable-length arrays aligned past 16 bytes (their storage rounded up within a larger allocation), beside a fixed
    // object aligned to 128 (a realigned frame with a base, r15, since the arrays move rsp), across calls with stack
    // arguments whose outgoing area the allocations keep aligned.
    check_test_program_source(test,
        "long sum8(long a,long b,long c,long d,long e,long f,long g,long h){return a+b+c+d+e+f+g+h;}"
        "typedef struct{_Alignas(32) char c;}T;int tested(int n){_Alignas(128) long fixed[4]={n,n+1,n+2,n+3};long total=0;"
        "for(int round=0;round<3;round++){int count=(n&7)+round+1;_Alignas(64) int v[count];T w[count+1];"
        "for(int i=0;i<count;i++)v[i]=i*n;for(int i=0;i<=count;i++)w[i].c=(char)i;long sum=0;"
        "for(int i=0;i<count;i++)sum+=v[i];for(int i=0;i<=count;i++)sum+=w[i].c;"
        "unsigned long mask=((unsigned long)v&63)|((unsigned long)w&31)|((unsigned long)fixed&127);"
        "total+=sum8(sum,fixed[0],fixed[1],fixed[2],fixed[3],round,(long)sizeof(w),(long)mask*1000);}return (int)(total%1000003);}",
        "vlas-aligned-past-16", clang);
    // A variable-length array aligned to 64 allocated below a guard array, while calls with two stack arguments put the
    // outgoing area 16 bytes past the realigned frame's alignment: its storage, rounded up from the allocation's
    // address, must leave the guard's bytes as they were (the allocation's room, its alignment less 16 bytes more).
    check_test_program_source(test,
        "long sum8(long a,long b,long c,long d,long e,long f,long g,long h){return a+b+c+d+e+f+g+h;}"
        "int tested(int n){long total=0;for(int k=1;k<=4;k++){char guard[16*k];for(int i=0;i<16*k;i++)guard[i]=(char)(i+n);"
        "_Alignas(64) char v[64*k];for(int i=0;i<64*k;i++)v[i]=(char)0x5a;long bad=0;"
        "for(int i=0;i<16*k;i++)bad+=guard[i]!=(char)(i+n);"
        "total+=sum8(bad*100000,v[0],v[64*k-1],k,n,0,0,(long)((unsigned long)v&63)*1000);}return (int)(total%1000003);}",
        "vlas-aligned-past-16-room", clang);
    // Structs aligned to 32 and 64 passed by value (on a stack aligned to them at the call, which realigns the caller's
    // frame) and returned (through a hidden pointer), the parameters' addresses aligned.
    check_test_program_source(test,
        "typedef struct{_Alignas(32) long a;int b;}A32;typedef struct{_Alignas(64) char c;double d;}A64;"
        "A32 make(int n){A32 r={n*2,n+1};return r;}"
        "long take(A32 x,int k,A64 y,A32 z){return x.a+x.b+k+y.c+(long)y.d+z.a*3+z.b+"
        "(long)(((unsigned long)&x&31)|((unsigned long)&y&63)|((unsigned long)&z&31))*1000;}"
        "A64 pass(A64 y,int k){y.c+=(char)k;y.d*=2;return y;}"
        "int tested(int n){A32 p=make(n);A64 q={(char)n,n*0.25};A32 r=make(n+5);A64 t=pass(q,n);return (int)take(p,n,t,r)+(int)t.d;}",
        "aggregates-aligned-past-16-by-value", clang);
    // A variadic definition whose named argument is aligned to 32 (on its caller's stack, aligned to 32 at the call): its
    // incoming area so aligned, and the unnamed arguments after it, each read at its own alignment.
    check_test_program_source(test,
        "typedef struct{_Alignas(32) long a;int b;}A32;typedef struct{_Alignas(64) char c;long d;}A64;"
        "long sum(int count,A32 first,...){__builtin_va_list ap;__builtin_va_start(ap,first);long s=first.a+first.b+(long)((unsigned long)&first&31)*1000;"
        "for(int i=0;i<count;i++){A64 x=__builtin_va_arg(ap,A64);s+=x.c+x.d*(i+1);}long last=__builtin_va_arg(ap,long);__builtin_va_end(ap);return s+last;}"
        "int tested(int n){A32 f={n,n+1};A64 x={(char)n,n*3L};A64 y={(char)(n+1),n*5L};return (int)(sum(2,f,x,y,(long)n*7)%1000003);}",
        "variadic-named-argument-aligned-past-16", clang);
    // Calls whose outgoing areas are aligned to 32 (of clang's callee in the harness, which takes its arguments in place),
    // from functions with no object aligned past 16, whose frames the calls alone realign: two of frames 16 bytes apart,
    // so that a frame left unrealigned would put one of their areas off 32. (After a variable-length array, clang's code
    // rounds the allocation to 16 alone and its call's area may be off 32, where Tier 0's keeps the frame's alignment: a
    // test of its own, tier0_deep_test.c's base, checks that.)
    check_test_program_source(test,
        "struct external_aligned{_Alignas(32) long a;int b;};int external_aligned_by_value(int,struct external_aligned,struct external_aligned);"
        "struct external_aligned gx,gy;int inner(int n){return external_aligned_by_value(n,gx,gy);}"
        "int padded(int n){volatile long pad[2]={n,n};return external_aligned_by_value(n+(int)(pad[1]-pad[0]),gx,gy);}"
        "int tested(int n){gx.a=n;gx.b=n+1;gy.a=n*2;gy.b=n-1;return inner(n)+padded(n+1)*3;}",
        "calls-aligned-past-16", clang);
    // Parameters of types aligned past 8 bytes: objects of their alignment, from a register's part or an 8-byte stack slot
    // (an aligned typedef passes as its base, so a slot is aligned to 8 alone), called at two stack depths 16 bytes apart.
    check_test_program_source(test,
        "typedef int aligned_int __attribute__((aligned(32)));typedef double aligned_double __attribute__((aligned(16)));"
        "long check(aligned_int a,aligned_double b,int c,aligned_int d,long e,long f,long g,aligned_int h){a+=1;h*=2;"
        "return a+(long)b+c+d+e+f+g+h+(long)(((unsigned long)&a&31)|((unsigned long)&b&15)|((unsigned long)&d&31)|((unsigned long)&h&31))*1000;}"
        "int tested(int n){int r=(int)check(n,n*2.5,3,n+1,5,6,7,n*9);{char pad[16+(n&1)*16];pad[0]=0;r+=(int)check(n+pad[0],1.5,n,2,3,4,5,n);}return r;}",
        "parameters-aligned-past-8", clang);
    check_test_program_source(test,
        "char a[24]=\"abcdefghijklmnop\";char b[24];int tested(int n){unsigned k=(unsigned)n%12;"
        "__builtin_memset(b,0,24);__builtin_memcpy(b,a,k);__builtin_memmove(b+2,b,k);"
        "return (int)__builtin_strlen(b)+(__builtin_memcmp(a,b,k)>0)-(__builtin_memcmp(a,b,k)<0)+b[2];}",
        "memory-builtin-runtime-loops", clang);
    check_test_program_source(test,
        "unsigned char a[512],b[512];int tested(int n){unsigned k=(unsigned)n%300;"
        "for(unsigned i=0;i<512;i++)a[i]=(unsigned char)(1+i%255);"
        "__builtin_memset(b,0x180,512);__builtin_memcpy(b+3,a+1,k);__builtin_memmove(b+4,b+3,k);"
        "__builtin_memmove(b+2,b+4,k);b[k+2]=0;"
        "return (int)__builtin_strlen((char*)b+2)+__builtin_memcmp(a+1,b+2,k)+b[400];}",
        "memory-builtin-wide-runtime", clang);
    check_test_program_source(test,
        "unsigned char a[128],b[128];int effects[3];int tested(int n){effects[0]=effects[1]=effects[2]=0;"
        "for(unsigned i=0;i<128;i++)a[i]=(unsigned char)(i+n);"
        "__builtin_memset(b,0x1a5,33);__builtin_memcpy(b,a,32);"
        "__builtin_memmove(b+1,b,15);__builtin_memmove(b,b+1,16);"
        "__builtin_memcpy((effects[0]++,b+40),(effects[1]++,a+40),(effects[2]++,8));"
        "int r=__builtin_memcmp(a+40,b+40,8);__builtin_memset((effects[0]++,b),7,(0));"
        "return r+effects[0]+effects[1]*10+effects[2]*100+b[7]+b[15]+b[32]+__builtin_memcmp(a,b,1)+__builtin_memcmp(a,b,0);}",
        "memory-builtin-small-constants-and-effects", clang);
    check_test_program_source(test,
        "#pragma STDC FENV_ACCESS ON\n"
        "int change_rounding(int);unsigned char a[2]={7,9},b[2];int tested(int n){b[0]=b[1]=0;change_rounding(n);"
        "__builtin_memcpy(b,a,(unsigned long)(1.0+0x1.fffffffffffffp-1));change_rounding(0);return b[0]+b[1]*256;}",
        "memory-builtin-runtime-size-under-fenv", clang);
    check_test_program_source(test,
        "#pragma STDC FENV_ACCESS ON\n"
        "int clear_fp(void);int fp_state(void);unsigned char a[2]={7,9},b[2];int tested(int n){b[0]=b[1]=0;clear_fp();"
        "__builtin_memcpy(b,a,__builtin_constant_p(1.0/3.0));return b[0]+b[1]*256+fp_state()*65536;}",
        "memory-builtin-size-query-runtime-answer", clang);
    check_test_program_source(test,
        "int tested(int n){int x=0;__builtin_memset((void*)0,7,0);__builtin_memcpy((void*)0,(void*)0,0);"
        "long y=__builtin_expect((x++,n),(x++,1));__builtin_prefetch(&x);"
        "return (int)y+x+__builtin_constant_p(n++);}", "builtin-hints-and-unevaluated-query", clang);
    // __builtin_alloca (#148), as clang -O0's dynamic alloca: sizes rounded up to 16 (the second allocation 32 bytes below
    // the first), one among a call's stack arguments, one of no bytes, in a loop, and the first's bytes intact after them.
    check_test_program_source(test,
        "int sink(long a,long b,long c,long d,long e,long f,long g,long h){return (int)(a+b*2+c*3+d*4+e*5+f*6+g*7+h*8);}"
        "int tested(int n){unsigned k=(unsigned)(n+3)*5+1;char*p=__builtin_alloca(k);for(unsigned i=0;i<k;i++)p[i]=(char)(i*7+n);"
        "char*q=__builtin_alloca(17);q[16]=(char)n;int r=(int)(((unsigned long)p|(unsigned long)q)&15)+(int)(p-q)+q[16];"
        "r+=sink(1,2,3,4,5,6,p[0],(long)((unsigned long)__builtin_alloca(32)&15));"
        "for(int i=0;i<3;i++){char*t=__builtin_alloca(64);t[63]=(char)(i+n);r+=t[63];}"
        "long s=0;for(unsigned i=0;i<k;i++)s+=p[i];r+=(int)((unsigned long)__builtin_alloca(0)&15);return r*7+(int)s;}",
        "alloca-runtime", clang);
    // An allocation after a block's first variable-length array is released with it, at the block's end and at a
    // continue, a break or a goto out of it, as clang's stackrestore releases it: each loop of 20,000 allocations of 8 KB
    // would take 160 MB of stack without the releases.
    check_test_program_source(test,
        "int tested(int n){long s=0;for(int i=0;i<20000;i++){char v[(n&7)+1];v[0]=(char)i;char*p=__builtin_alloca(8192);"
        "p[0]=(char)(i*3);p[8191]=v[0];s+=p[0]+p[8191];if(i==n+5)continue;}"
        "for(int i=0;i<30000;i++){char v[(n&3)+2];char*p=__builtin_alloca(8192);p[1]=v[0]=(char)i;s+=p[1];if(i==20000+n)break;}"
        "int j=0;again:{char v[(n&3)+1];char*p=__builtin_alloca(8192);p[2]=(char)j;v[0]=p[2];s+=v[0];if(++j<20000)goto again;}"
        "return (int)(s&0x7fffffff);}",
        "alloca-released-with-variable-length-arrays", clang);
    // Allocations in a realigned frame (an object aligned to 64) and in a variadic definition, whose va_arg reads the
    // incoming areas the allocation moved rsp past.
    check_test_program_source(test,
        "long vsum(int count,...){__builtin_va_list ap;__builtin_va_start(ap,count);char*b=__builtin_alloca((unsigned)count*8+3);long s=0;"
        "for(int i=0;i<count;i++){long x=__builtin_va_arg(ap,long);b[i*8]=(char)x;s+=x+b[i*8];}__builtin_va_end(ap);return s+(long)((unsigned long)b&15);}"
        "int tested(int n){_Alignas(64) char a[64];a[0]=(char)n;char*p=__builtin_alloca((unsigned)(n+3)*3+1);p[0]=a[0];"
        "int r=(int)(((unsigned long)a&63)|((unsigned long)p&15));long v=vsum(9,1L,2L,3L,(long)n,5L,6L,7L,8L,(long)p[0]);"
        "return r+(int)v+(int)((unsigned long)__builtin_alloca(40)&15);}",
        "alloca-realigned-and-variadic", clang);
    // __builtin_frame_address(0) and __builtin_return_address(0) (#148), as clang -O0's frame has them: the frame
    // address a multiple of 16 above the locals, the return address the word above it, a callee's saved rbp the caller's
    // frame address (in a realigned frame too), a callee's frame below its caller's, return addresses the same at one call
    // and different at two, after the caller's start, and the frame address unmoved by an allocation.
    check_test_program_source(test,
        "void*caller_frame(void){return *(void**)__builtin_frame_address(0);}void*where(void){return __builtin_return_address(0);}"
        "char*inner(void){return __builtin_frame_address(0);}"
        "int aligned(int n){_Alignas(64) char a[64];a[0]=(char)n;char*f=__builtin_frame_address(0);"
        "return ((unsigned long)a&63)==0&&caller_frame()==f&&*(void**)(f+8)==__builtin_return_address(0)&&f>a&&a[0]==(char)n;}"
        "int tested(int n){char*f=__builtin_frame_address(0);char local=(char)n;int r=((unsigned long)f&15)==0;r|=(f>&local)<<1;"
        "r|=(*(void**)(f+8)==__builtin_return_address(0))<<2;r|=(inner()<f)<<3;void*a=where();void*b=where();r|=(a!=b)<<4;"
        "void*prev=0;for(int i=0;i<2;i++){void*c=where();if(i)r|=(c==prev)<<5;prev=c;}r|=(caller_frame()==f)<<6;"
        "r|=((char*)where()>(char*)tested)<<7;r|=(__builtin_extract_return_addr(__builtin_return_address(0))==__builtin_return_address(0))<<8;"
        "char*p=__builtin_alloca((unsigned)(n+3)*16);r|=(__builtin_frame_address(0)==f)<<9;r|=(p<f)<<10;r|=aligned(n)<<11;"
        "r|=(__builtin_frob_return_addr(where())!=0)<<12;return r+local;}",
        "frame-builtins", clang);
    // A caller's frame, which clang -O0's code finds through the frame pointers' chain, is refused by name.
    check_test_program_boundary_reason(test, "int tested(int n){return (__builtin_frame_address(1)!=0)+n;}", "frame-address-caller", true, false, 0, 0,
                                       "__builtin_frame_address and __builtin_return_address of a caller's frame");
    check_test_program_boundary_reason(test, "int tested(int n){return (__builtin_return_address(2)!=0)+n;}", "return-address-caller", true, false, 0, 0,
                                       "__builtin_frame_address and __builtin_return_address of a caller's frame");
    check_test_program_source(test,
        "int tested(int n){double x=n<0?__builtin_nan(\"\"):n==0?0.0:n==1?__builtin_inf():n==2?0x1p-1074:(double)n;"
        "return __builtin_isnan(x)*100+__builtin_isinf(x)*10+__builtin_isfinite(x)+__builtin_signbit(-x)*2;}",
        "floating-builtin-runtime-classification", clang);
    // Which of fpclassify's selectors run for each category, and how often, and its argument's effect: mark counts each
    // selector's evaluations in a 3-bit field of its own (the argument's seen++ in the lowest), not their order, since
    // clang evaluates the normal and subnormal ones in an order its own build decides (check_lower_floating_prepare).
    check_test_program_source(test,
        "int seen;int mark(int n){seen+=1<<(3*n);return n;}int tested(int n){seen=0;"
        "double x=n<0?__builtin_nan(\"\"):n==0?0.0:n==1?__builtin_inf():n==2?0x1p-1074:(double)n;"
        "int r=__builtin_fpclassify(mark(1),mark(2),mark(3),mark(4),mark(5),x);return seen*10+r;}",
        "fpclassify-conditional-selector-effects", clang);
    check_test_program_source(test,
        "int seen;int mark(int n){seen+=1<<(3*n);return n;}int tested(int n){seen=0;"
        "int r=__builtin_fpclassify(mark(1),mark(2),mark(3),mark(4),mark(5),(seen++,1.0));return seen*10+r+n;}",
        "fpclassify-known-category-effects", clang);
    // K&R definitions (C17 6.9.1, #154's decision: as clang): their declaration lists' types, the parameters' order the
    // identifier list's, a struct and a register parameter, a call through the prototype of their promoted types, which
    // converts its arguments as clang's does, and an empty list's definition after its declaration.
    check_test_program_source(test,
        "struct P{int x,y;};int add(a,b)int a;long b;{return a+(int)b;}char at(p,n)const char*p;int n;{return p[n];}"
        "int sum(r,q)register unsigned r;struct P q;{return q.x*10+q.y+(int)r;}int count();int count(){return 7;}"
        "int tested(int n){struct P q={n,3};return add(n,7)*3+at(\"abcdef\",n&3)+sum((unsigned)n,q)+count()+add(n,(char)n);}",
        "kr-definitions", clang);
    // The comparison builtins (#148) in each format on the harness's special values (zeros, infinities, quiet and signaling
    // NaNs, subnormals): their results and the exceptions their quiet compares raise, their arguments' conversions and
    // order, and mingw's fmaxl, which the libc corpus's comparison found.
    check_test_program_source(test,
        "double fp_special(int);float fp_specialf(int);int clear_fp(void);int fp_state(void);int seen;"
        "double mark(int k,double v){seen=seen*10+k;return v;}"
        "int bits(double x,double y){return __builtin_isgreater(x,y)|__builtin_isgreaterequal(x,y)<<1|__builtin_isless(x,y)<<2|"
        "__builtin_islessequal(x,y)<<3|__builtin_islessgreater(x,y)<<4|__builtin_isunordered(x,y)<<5;}"
        "int bitsf(float x,float y){return __builtin_isgreater(x,y)|__builtin_isgreaterequal(x,y)<<1|__builtin_isless(x,y)<<2|"
        "__builtin_islessequal(x,y)<<3|__builtin_islessgreater(x,y)<<4|__builtin_isunordered(x,y)<<5;}"
        "int bitsl(long double x,long double y){return __builtin_isgreater(x,y)|__builtin_isgreaterequal(x,y)<<1|__builtin_isless(x,y)<<2|"
        "__builtin_islessequal(x,y)<<3|__builtin_islessgreater(x,y)<<4|__builtin_isunordered(x,y)<<5;}"
        "long double fmaxl_like(long double x,long double y){return (__builtin_isgreaterequal(x,y)||__builtin_isnan(y))?x:y;}"
        "int tested(int n){double x=fp_special(n),y=fp_special(n*7+3);float a=fp_specialf(n),b=fp_specialf(n*5+1);"
        "long double l=x,m=(long double)b;clear_fp();unsigned k=(unsigned)bits(x,y);k=k*32+(unsigned)fp_state();clear_fp();"
        "k=k*64+(unsigned)bitsf(a,b);k=k*32+(unsigned)fp_state();clear_fp();k=k*64+(unsigned)bitsl(l,m);k=k*32+(unsigned)fp_state();"
        "seen=0;k=k*2+(unsigned)__builtin_islessgreater(mark(1,x),mark(2,(double)n));k=k*100+(unsigned)seen;"
        "k=k*8+(unsigned)(__builtin_isunordered(n,b)+__builtin_isgreater(n,2.5f)*2+__builtin_isless(a,y)*4);"
        "long double r=fmaxl_like(l,m);unsigned long u;unsigned short e;__builtin_memcpy(&u,&r,8);__builtin_memcpy(&e,(char*)&r+8,2);"
        "k=k*31+(unsigned)u+(unsigned)(u>>32);k=k*7+e;return (int)k;}",
        "floating-comparison-builtins", clang);
    // The native common format's DAZ policy applies to the ordered comparisons as well as EQ and NE. An x87
    // promotion would keep these subnormals nonzero while clang's ucomiss/ucomisd compare them as zero under DAZ.
    check_test_program_source(test,
        "#pragma STDC FENV_ACCESS ON\n"
        "double fp_special(int);float fp_specialf(int);int change_daz(int);int clear_fp(void);int fp_state(void);"
        "int bits(double x,double y){return __builtin_isgreater(x,y)|__builtin_isgreaterequal(x,y)<<1|__builtin_isless(x,y)<<2|"
        "__builtin_islessequal(x,y)<<3|__builtin_islessgreater(x,y)<<4|__builtin_isunordered(x,y)<<5;}"
        "int bitsf(float x,float y){return __builtin_isgreater(x,y)|__builtin_isgreaterequal(x,y)<<1|__builtin_isless(x,y)<<2|"
        "__builtin_islessequal(x,y)<<3|__builtin_islessgreater(x,y)<<4|__builtin_isunordered(x,y)<<5;}"
        "int tested(int n){int old=change_daz(n);double x=fp_special(6+(((unsigned)n>>1)&1u));float y=fp_specialf(6+(((unsigned)n>>1)&1u));"
        "clear_fp();unsigned k=(unsigned)bits(x,0.0);k=k*32+(unsigned)fp_state();clear_fp();"
        "k=k*64+(unsigned)bitsf(y,0.0f);k=k*32+(unsigned)fp_state();change_daz(old);return (int)k;}",
        "floating-comparison-native-daz", clang);
    // Both arguments must finish before a compare raises FE_INVALID. Converting the first signaling NaN to x87
    // would quiet it before the second argument clears the flags. Both quiet self-tests must run even when the
    // first argument is a quiet NaN and the second signals.
    check_test_program_source(test,
        "#pragma STDC FENV_ACCESS ON\n"
        "double fp_special(int);float fp_specialf(int);int clear_fp(void);int fp_state(void);int seen;"
        "double second(double x){seen++;clear_fp();return x;}float secondf(float x){seen++;clear_fp();return x;}"
        "int tested(int n){double s=fp_special(5),q=fp_special(4);float fs=fp_specialf(5),fq=fp_specialf(4);"
        "seen=0;clear_fp();unsigned k=(unsigned)__builtin_isgreater(s,second(0.0));k|=(unsigned)(fp_state()!=1)<<1;"
        "k|=(unsigned)__builtin_isgreaterequal(s,second(0.0))<<2;k|=(unsigned)(fp_state()!=1)<<3;"
        "k|=(unsigned)__builtin_isless(s,second(0.0))<<4;k|=(unsigned)(fp_state()!=1)<<5;"
        "k|=(unsigned)__builtin_islessequal(s,second(0.0))<<6;k|=(unsigned)(fp_state()!=1)<<7;"
        "k|=(unsigned)__builtin_isgreater(fs,secondf(0.0f))<<8;k|=(unsigned)(fp_state()!=1)<<9;"
        "k|=(unsigned)__builtin_isgreaterequal(fs,secondf(0.0f))<<10;k|=(unsigned)(fp_state()!=1)<<11;"
        "k|=(unsigned)__builtin_isless(fs,secondf(0.0f))<<12;k|=(unsigned)(fp_state()!=1)<<13;"
        "k|=(unsigned)__builtin_islessequal(fs,secondf(0.0f))<<14;k|=(unsigned)(fp_state()!=1)<<15;"
        "k|=(unsigned)__builtin_isgreater(q,second(s))<<16;k|=(unsigned)(fp_state()!=1)<<17;"
        "k|=(unsigned)__builtin_isless(fq,secondf(fs))<<18;k|=(unsigned)(fp_state()!=1)<<19;return (int)(k|((unsigned)seen<<20));}",
        "floating-comparison-argument-exception-order", clang);
    check_test_program_source(test,
        "int calls;double make(int n){calls++;return n<0?-__builtin_inf():n==0?-0.0:(double)(-n)-0.25;}int tested(int n){calls=0;"
        "double d=__builtin_fabs(make(n));float f=__builtin_fabsf((float)n-7.5f);return __builtin_isinf_sign(make(n))*1000+"
        "(__builtin_isinf(d)?31:(int)(d*4))+(int)(f*2)+calls*100;}", "absolute-and-signed-infinity-runtime", clang);
    check_test_program_source(test,
        "int change_rounding(int);int tested(int n){change_rounding(n);int a=__builtin_flt_rounds();"
        "change_rounding(n+1);int b=__builtin_flt_rounds();change_rounding(0);return a*10+b;}", "floating-environment-helper-observes-each-call", clang);
    check_test_program_source(test,
        "#pragma STDC FENV_ACCESS ON\n"
        "int change_rounding(int);int clear_fp(void);int fp_state(void);int tested(int n){change_rounding(n);clear_fp();"
        "double x=1.0+0x1p-53;int a=x>1.0;int f=fp_state();change_rounding(0);return a*100+f;}",
        "fenv-source-constant-runtime-rounding", clang);
    check_test_program_source(test,
        "#pragma STDC FENV_ACCESS ON\n"
        "int change_rounding(int);int clear_fp(void);int fp_state(void);int tested(int n){change_rounding(n);clear_fp();"
        "1.0/3.0;int f=fp_state();clear_fp();int a=(1.0+0x1p-53>1.0)?7:3;"
        "int b=(1.0+0x1p-53>1.0)&&++a;int c=(1.0+0x1p-53>1.0)||++a;"
        "change_rounding(0);return f*1000+a*100+b*10+c;}", "fenv-discarded-effects-and-runtime-branches", clang);
    check_test_program_source(test,
        "#pragma STDC FENV_ACCESS ON\n"
        "int change_rounding(int);int clear_fp(void);int fp_state(void);int tested(int n){change_rounding(n);clear_fp();"
        "float x=1.0+0x1p-24;int a=x>1.0f;int f=fp_state();change_rounding(0);return a*100+f;}",
        "fenv-implicit-narrowing-conversion", clang);
    if (!check_test_program_daz())
    {
        printf("  fenv classification: DAZ toggle skipped (MXCSR does not support it)\n");
    }
    check_test_program_source(test,
        "#pragma STDC FENV_ACCESS ON\n"
        "int clear_fp(void);int fp_state(void);int change_daz(int);double fp_special(int);int tested(int n){"
        "int old=change_daz(n);double x=fp_special(n);clear_fp();int a=__builtin_fpclassify(11,22,33,44,55,x);int f=fp_state();"
        "clear_fp();int b=__builtin_isinf_sign(x);int g=fp_state();change_daz(old);return a*1000+f*100+b*10+g;}",
        "fenv-strict-classification-exceptions-and-daz", clang);
    check_test_program_source(test,
        "#pragma STDC FENV_ACCESS ON\n"
        "_Static_assert(__builtin_constant_p(1.0/3.0)==1,\"source proof\");"
        "int clear_fp(void);int fp_state(void);int tested(int n){clear_fp();"
        "int a=__builtin_constant_p(1.0/3.0);int f=fp_state();return (a+1)*100+f+n;}",
        "fenv-query-source-proof-and-runtime-value", clang);
    check_test_program_source(test,
        "#pragma STDC FENV_ACCESS ON\n"
        "int absent(void);int clear_fp(void);int fp_state(void);int tested(int n){clear_fp();"
        "int a=__builtin_constant_p(1.0/3.0)?absent():n;int f=fp_state();return a+f;}",
        "fenv-query-runtime-truth-keeps-effects-and-selected-references", clang);
    check_test_program_source(test,
        "#pragma STDC FENV_ACCESS ON\n"
        "int visits;double absent(void);double called(void){visits++;return 1.0/3.0;}int clear_fp(void);int fp_state(void);"
        "int tested(int n){int x=n;visits=0;clear_fp();int a=__builtin_constant_p((x++,1.0/3.0));"
        "int b=__builtin_constant_p(called()+1.0);int c=__builtin_constant_p(absent()+1.0);int f=fp_state();"
        "return a+b+c+x+visits*100+f*1000;}", "fenv-query-nonconstant-arguments-remain-unevaluated", clang);
    check_test_program_source(test,
        "#pragma STDC FENV_ACCESS ON\n"
        "int clear_fp(void);int fp_state(void);int tested(int n){clear_fp();int a=__builtin_constant_p(1.0/2.0);int f=fp_state();"
        "clear_fp();int b=__builtin_constant_p(1.0/3.0);int g=fp_state();clear_fp();int c=__builtin_constant_p(n+1.0/3.0);int h=fp_state();"
        "return a*100000+b*10000+c*1000+f*100+g*10+h+n;}", "fenv-query-exact-inexact-and-dynamic-operands", clang);
    check_test_program_source(test,
        "#pragma STDC FENV_ACCESS ON\n"
        "int visits;int next(void){visits++;return 7;}int clear_fp(void);int fp_state(void);int tested(int n){visits=0;clear_fp();"
        "int a=__builtin_constant_p(1?1.0/3.0:next());int b=__builtin_constant_p((0&&next())+1.0/3.0);"
        "int c=__builtin_constant_p(1?1.0/2.0:next());int d=__builtin_constant_p(1?1.0/3.0:n++);int f=fp_state();"
        "return a*100000+b*10000+c*1000+d*100+visits*10+f+n;}", "fenv-query-syntactic-effects-and-exact-selected-proof", clang);
    check_test_program_source(test,
        "#pragma STDC FENV_ACCESS ON\n"
        "int clear_fp(void);int fp_state(void);int tested(int n){clear_fp();int a=__builtin_constant_p(__builtin_constant_p(1.0/3.0));int f=fp_state();"
        "clear_fp();int b=__builtin_constant_p(__builtin_constant_p(1.0/3.0)+1);int g=fp_state();"
        "clear_fp();int c=__builtin_constant_p((n,1.0/2.0));int d=__builtin_constant_p(__imag__ n);int h=fp_state();"
        "return a*100000+b*10000+c*1000+d*100+f*10+g+h+n;}", "fenv-query-nested-runtime-proofs-comma-and-imaginary", clang);
    check_test_program_source(test,
        "#pragma STDC FENV_ACCESS ON\n"
        "int change_rounding(int);int clear_fp(void);int fp_state(void);int external_helper(int);extern int external_data;"
        "int tested(int n){static double fixed=1.0+0x1p-53;change_rounding(n);clear_fp();double a=1.0+0x1p-53;int f=fp_state();"
        "double off;change_rounding(0);{\n#pragma STDC FENV_ACCESS OFF\n off=1.0+0x1p-53;}change_rounding(n);double b=1.0+0x1p-53;"
        "int selected=(1.0+0x1p-53>1.0)?external_helper(n):external_data;change_rounding(0);"
        "return (a>fixed)*1000+(b>off)*100+f+selected;}", "fenv-static-proof-scoped-policy-and-live-references", clang);
    check_test_program_source(test,
        "int tested(int n){double d=(double)n+0.625;float f=(float)n-0.375f;"
        "return (int)__builtin_ceil(d)+(int)__builtin_floor(d)*3+(int)__builtin_trunc(d)*7+"
        "(int)__builtin_ceilf(f)*11+(int)__builtin_floorf(f)*13+(int)__builtin_truncf(f)*17;}",
        "math-fixed-rounding-raw-float-and-double-abi", clang);
    check_test_program_source(test,
        "double fp_special(int);float fp_specialf(int);unsigned long fp_bits(double);unsigned fp_bitsf(float);"
        "int tested(int n){double x=fp_special(n);float y=fp_specialf(n);"
        "return (int)(fp_bits(__builtin_copysign(x,(double)n))%997)+(int)(fp_bitsf(__builtin_copysignf(y,(float)n))%991);}",
        "math-copysign-preserves-raw-representation", clang);
    check_test_program_source(test,
        "#pragma STDC FENV_ACCESS ON\n"
        "double fp_special(int);float fp_specialf(int);int set_math_errno(int);int get_math_errno(void);int clear_fp(void);int fp_state(void);"
        "int tested(int n){double x=fp_special(n);float y=fp_specialf(n);set_math_errno(7);clear_fp();double a=__builtin_sqrt(x);"
        "int e=get_math_errno(),f=fp_state();set_math_errno(7);clear_fp();float b=__builtin_sqrtf(y);int g=get_math_errno(),h=fp_state();"
        "return e*100000+f*10000+g*100+h+__builtin_isnan(a)*3+__builtin_isinf(a)*5+__builtin_signbit(a)*7+"
        "__builtin_isnan(b)*11+__builtin_isinf(b)*13+__builtin_signbit(b)*17;}",
        "math-sqrt-real-errno-address-and-exceptions", clang);
    check_test_program_source(test,
        "#pragma STDC FENV_ACCESS ON\n"
        "int change_rounding(int);int clear_fp(void);int fp_state(void);int tested(int n){change_rounding(n);"
        "double d=(double)n+0.625;float f=(float)n-0.375f;clear_fp();double a=__builtin_nearbyint(d);int ea=fp_state();"
        "clear_fp();double b=__builtin_rint(d);int eb=fp_state();clear_fp();float c=__builtin_nearbyintf(f);int ec=fp_state();"
        "clear_fp();float e=__builtin_rintf(f);int ed=fp_state();change_rounding(0);"
        "return (int)a+(int)b*3+(int)c*7+(int)e*11+ea*1000+eb*100+ec*10000+ed*100000;}",
        "math-current-rounding-nearbyint-and-rint-effects", clang);
    check_test_program_source(test,
        "#pragma STDC FENV_ACCESS ON\n"
        "int change_rounding(int);int clear_fp(void);int fp_state(void);int tested(int n){change_rounding(n);clear_fp();"
        "float x=__builtin_copysignf(1.0+0x1p-24,-1.0);int a=x<-1.0f;int f=fp_state();change_rounding(0);return a*100+f;}",
        "math-copysign-runtime-prototype-narrowing", clang);
    check_test_program_source(test,
        "int*__errno_location(void);int tested(int n){*__errno_location()=7;double x=__builtin_sqrt((double)n);"
        "return *__errno_location()+(__builtin_isnan(x)?100:(int)x);}", "math-reuses-existing-errno-accessor", clang);
    check_test_program_source(test,
        "double sqrt(double x){return __builtin_sqrt(x);}double ceil(double x){return __builtin_ceil(x);}"
        "int tested(int n){return (int)(sqrt((double)(n+4))+ceil((double)n+0.25));}",
        "math-source-definitions-do-not-recurse-through-runtime-helpers", clang);
    check_test_program_source(test,
        "int visits;double next(int n){visits++;return (double)(n+4);}"
        "double mix(double a,int b,float c,double d){return a+b+c+d;}int tested(int n){visits=0;"
        "double a=mix(0.5,2,__builtin_copysignf((float)n,-1.0f),__builtin_sqrt(next(n)));"
        "double b=__builtin_sqrt(__builtin_sqrt((double)(n+4)*16.0));return (int)(a+b)+visits*100;}",
        "math-nested-accessor-calls-preserve-live-arguments", clang);
    check_test_program_source(test,
        "int tested(int n){unsigned long a=(unsigned long)n,b=3,out=0;int overflow=__builtin_umull_overflow(a++,b++,&out);"
        "return (int)(out%97)+overflow*100+(int)(a%13)+(int)b;}", "typed-overflow-prototype-conversions-and-effects", clang);
    check_test_program_source(test,
        "int absent(void);int tested(int n){unsigned long a=__builtin_object_size(\"abc\",0);"
        "unsigned long b=__builtin_object_size(\"abc\",2);int ignored=__builtin_object_size((n++,(char*)0),0)==~0UL;"
        "return n+(int)(a*10+b)+ignored+__builtin_constant_p(absent());}", "deferred-query-runtime-values-remain-unevaluated", clang);
    check_test_program_source(test,
        "signed char a;unsigned short b;int c;unsigned long d;int tested(int n){int order=0;"
        "__atomic_store_n(&a,n-120,order++);__atomic_store_n(&b,n+65530,0);__atomic_store_n(&c,n*7,5);__atomic_store_n(&d,(unsigned long)n+9,3);"
        "__atomic_thread_fence(5);__atomic_signal_fence(5);__sync_synchronize();"
        "return __atomic_load_n(&a,0)+__atomic_load_n(&b,2)+__atomic_load_n(&c,5)+(int)__atomic_load_n(&d,0)+order;}",
        "atomic-shared-helper-widths-orders-and-sign", clang);
    check_test_program_source(test,
        "void terminal_probe(void){__builtin_trap();}"
        "int tested(int n){if(n==999)__builtin_trap();if(n==998)return 7+(__builtin_unreachable(),1);return n+3;}",
        "terminal-helper-has-no-continuation", clang);
    check_test_program_source(test,
        "int visits;int next(int n){visits++;return n;}int tested(int n){visits=0;int x=n;"
        "double a=x++?:2.5;int b=next(n)?:next(7);int c=(visits++,3)?:next(9);return (int)(a*4)+b+c+x+visits*100;}",
        "gnu-omitted-middle-evaluates-once", clang);
    check_test_program_source(test,
        "int visits;int next(int n){visits++;return n;}int tested(int n){visits=0;volatile int x=n;"
        "int a=__imag__(next(n));int b=__imag__(x++);__real__ x+=3;return a+b+x+visits*100;}",
        "scalar-real-imaginary-preserve-evaluation", clang);
    check_test_program_source(test,
        "int g;int bump(void){g++;return 9;}int tested(int n){g=0;int x={n,bump()};return x+g;}",
        "ignored-excess-initializer-does-not-execute", clang);
    check_test_program_source(test,
        "int tested(int n){int x=0;int y=5+({int z=n;x++;z+x;});return x*100+y;}", "statement-region-value-and-effects", clang);
    check_test_program_source(test,
        "int helper(int a,int b,int c){return a*100+b*10+c;}int tested(int n){int x=0;"
        "int z=helper(n,({x++;n+2;}),({int y=n*2;y+3;}));return z+x;}", "statement-region-pending-call-values", clang);
    check_test_program_source(test,
        "int tested(int n){return 7+({if(n<0)return 17;int x=n;x+2;});}", "statement-region-return-escape", clang);
    check_test_program_source(test,
        "int tested(int n){int x=0;int y=7+({x++;if(n<0)goto done;x+2;});x+=y;done:return x+n;}", "statement-region-goto-escape", clang);
    check_test_program_source(test,
        "int tested(int n){int x=0;for(int i=0;i<n+6;i++){x+=7+({if(i==2)continue;if(i==5)break;i;});}return x;}",
        "statement-region-loop-escape", clang);
    // Clang 23 accepts this outer-loop context but its generated break targets the inner while (10/10/14).
    // GNU's specified outer-loop target and GCC give 4/4/6 for n=-1/0/1; keep this focused independent oracle.
    check_test_program_source(test,
        "int tested(int n){int x=0;for(int i=0;i<5;i++){while(({if(i==2)break;n>0;})){x++;break;}x+=2;}return x;}",
        "statement-region-loop-condition-escape-gcc", "gcc");
    check_test_program_source(test,
        "int tested(int n){int x=0;int y=3+({int z=4+({x++;n+1;});for(int i=0;i<3;i++)z+=i;z;});return x*100+y;}",
        "nested-statement-regions-and-loop", clang);
    check_test_program_source(test,
        "int tested(int n){int x=0;double z=0.5+({double y=n+0.25;x++;y;});return (int)(z*4)+x;}", "statement-region-floating-pending-value", clang);
    check_test_program_source(test,
        "int sum(int a,double b,int c,double d,int e){return a+(int)(b*2)+c+(int)(d*4)+e;}"
        "int tested(int n){return sum(n,n+0.5,n+1,n+0.25,({int x=n;x+3;}));}",
        "statement-region-mixed-pending-stack-classes", clang);
    check_test_program_source(test,
        "int tested(int n){int x=0;int r=4+({int y=3+({if(n<0)goto finish;n;});x=y;finish:x+=2;x+1;});return r+x;}",
        "statement-region-inner-goto-outer-region", clang);
    check_test_program_source(test,
        "int missing(void);int tested(int n){int x=0;int y=(n&&({x++;n+2;}))||({x+=2;0;});"
        "return x*100+y+(int)sizeof(({missing();n;}));}", "statement-region-selected-and-unevaluated", clang);
    check_test_program_source(test,
        "int tested(int n){int x=0;({int y=n;if(y)x++;});__typeof__(({x+=50;n;})) t=1;return x+t;}", "statement-region-void-and-typeof", clang);
    check_test_program_source(test,
        "int tested(int n){int x=n;__typeof__(({int y=++x;y;})) t=2;return x+t;}", "statement-region-typeof-local-declarations-unevaluated", clang);
    check_test_program_source(test,
        "int tested(int n){int x=0;({int y=n;if(y)x++;});return x;}", "statement-region-void-prefix", clang);
    // Long double (#136, #137): its locals and parameters frame objects, its values on the x87 through their slots, its
    // constants in each function's table, compares, every conversion (to and from unsigned long past 2^63 too), calls
    // passing it on the stack and returning it in st0 (a struct of one too), va_arg of it, and updates of it in memory.
    check_test_program_source(test,
        "long double scale(long double a,int i,double d){return a*i+d;}"
        "int tested(int n){long double x=n/3.0L;long double y=scale(x,n,0.5);long double z=-y;z+=x;z*=2;z-=n;z/=7;"
        "int c=(x<y)+2*(x<=y)+4*(x>y)+8*(x>=y)+16*(x==y)+32*(x!=y)+64*!z+128*(x&&y)+256*(z||0.0L);"
        "long long l=(long long)(y*1000);unsigned long u=(unsigned long)(x*x*1e15L+1e19L);unsigned char b=(unsigned char)(x+100);"
        "_Bool t=(_Bool)z;float f=(float)x;double d=(double)y;"
        "return c+(int)(l%100000)+(int)(u%9973)+(int)z+b+t+(int)(f*64)+(int)(d*16)+(int)((long double)u/1e18L);}",
        "long-double-arithmetic-and-conversions", clang);
    check_test_program_source(test,
        "long double g=2.5L;long double table[3]={1.5L,-0.25L,3.0L};struct P{long double a;int t;long double b;};struct One{long double x;};"
        "struct One third(long double a){struct One r={a/3};return r;}"
        "struct P make(long double a,int t){struct P p={a,t,a*t};return p;}"
        "long double sum(int count,...){__builtin_va_list ap;__builtin_va_start(ap,count);long double s=0;"
        "for(int i=0;i<count;i++){s=s*2+__builtin_va_arg(ap,long double);s+=__builtin_va_arg(ap,int);}__builtin_va_end(ap);return s;}"
        "int tested(int n){long double v=n;long double old=v++;long double pre=++v;long double down=v--;--v;"
        "table[(unsigned)n%3]+=v;g+=old;struct P p=make(pre,n);struct One o=third(p.b);"
        "long double s=sum(3,v,n,down,2,o.x,-n);long double*q=&table[1];*q-=s/64;long double r=n&1?s:*q;"
        "return (int)(s*16)^(int)(r*8)^(int)(g*2)^(int)(table[(unsigned)n%3]*4)^p.t^(int)(p.a*3);}",
        "long-double-memory-and-calls", clang);
    // A call's result object, which a hidden pointer passes, aligned to the result's canonical type (16 for a union of a
    // long double) though an aligned typedef says 1: va_arg's 8-byte object between the calls' objects puts one of them 8
    // bytes off a multiple of 16 if their alignment were the typedef's. (Clang passes a variable of the typedef's type as
    // the pointer itself, at the typedef's alignment, so the variables here are of the union's.)
    check_test_program_source(test,
        "union wide{long double x;signed char c[40];};typedef union wide W __attribute__((aligned(1)));struct eight{signed char c[8];};W external_wide(int n);"
        "int shifted(int n,...){__builtin_va_list ap;__builtin_va_start(ap,n);union wide a=external_wide(n);struct eight e=__builtin_va_arg(ap,struct eight);"
        "union wide b=external_wide(n+1);union wide c=external_wide(n+2);__builtin_va_end(ap);return a.c[0]+b.c[0]*3+c.c[0]*5+a.c[39]+e.c[7];}"
        "int tested(int n){struct eight e={{1,2,3,4,5,6,7,(char)n}};return shifted(n,e);}",
        "hidden-result-canonical-alignment", clang);
    // The direct member access needs a canonical-aligned return temporary too. The helper reports a misaligned
    // hidden result in its first byte; its Buster-only expected result is independent of Clang's weaker alignment.
    check_test_program_source(test,
        "union wide{long double x;signed char c[40];};typedef union wide W __attribute__((aligned(1)));struct eight{signed char c[8];};W external_wide(int n);"
        "int shifted(int n,...){__builtin_va_list ap;__builtin_va_start(ap,n);union wide a=external_wide(n);struct eight e=__builtin_va_arg(ap,struct eight);"
        "union wide b=external_wide(n+1);int c=external_wide(n+2).c[0];__builtin_va_end(ap);return a.c[0]+b.c[0]*3+c*5+a.c[39]+e.c[7];}"
        "int tested(int n){struct eight e={{1,2,3,4,5,6,7,(char)n}};return shifted(n,e);}",
        "hidden-result-direct-temporary-alignment", clang);
    check_test_program_source(test,
        "struct cx{_Complex long double z;};struct cx external_complex(long double a,long double b);"
        "long double external_complex_apply(struct cx(*f)(long double,long double),long double a);"
        "struct cx swap(long double a,long double b){struct cx c=external_complex(b,a);return c;}"
        "int tested(int n){struct cx c=external_complex(n,n*0.5L);long double r=__real__ c.z,i=__imag__ c.z;"
        "return (int)(r*8+i*2)+(int)external_complex_apply(swap,n+0.25L);}",
        "long-double-complex-struct-results", clang);
    check_test_program_source(test,
        "long double half();long double twice(long double a){return a*2;}"
        "long double apply(long double(*f)(long double),long double a){return f(a);}"
        "int tested(int n){long double a[4]={n,n+0.5L,-n,0};long double*p=a;p+=2;"
        "long double k=apply(twice,*p)+half(a[1]);a[3]=k;"
        "return (int)(a[3]*4)+(int)(p[-1]*2)+(int)sizeof(long double)+(int)_Alignof(long double)+(int)sizeof a;}"
        "long double half(long double a){return a/2;}",
        "long-double-indirect-and-unprototyped-calls", clang);
    // The 128-bit integers (#133, #136): Tier 0's I128 and U128, their locals and parameters frame objects, every operation
    // and conversion (to and from every integer class and floating type, the floating ones and division as libgcc's calls),
    // constants of every shape (one CONST of either extension, and halves), shifts by counts of every class, updates of
    // locals, globals and memory, calls passing them in register pairs and on the stack and returning them, va_arg of them
    // from registers and the overflow area, and temporaries of them (statement expressions, ?: without its middle operand,
    // a switch on one).
    check_test_program_source(test,
        "typedef __int128 i128;typedef unsigned __int128 u128;static int fold(u128 v){return (int)(unsigned)(v^(v>>29)^(v>>64)^(v>>101));}"
        "int tested(int n){u128 a=((u128)0x0123456789abcdefULL<<64)|(unsigned long long)(n+11)*0x9e3779b97f4a7c15ULL;"
        "i128 b=-(i128)(n+5)*1000000007LL;u128 c=((u128)0xfedcba9876543210ULL<<64)|0xfedcba9876543210ULL;i128 m=-1;"
        "i128 h=(i128)0x8000000000000000ULL;u128 k=(u128)-5;int s=(n&7)*9+5;u128 r=a+c;r^=a*c;r+=a-c;r|=(u128)b&c;"
        "r+=a/(c>>(n&63|1)|1);r^=a%((u128)(n+20)*3|1);i128 q=b/(i128)(n|1)+b%(i128)(n-30|1);r+=(u128)q+(u128)h+k;"
        "r+=a<<s;r^=c>>s;r+=(u128)((i128)c>>s);r+=a<<(i128)s;r+=(u128)1<<(unsigned char)s;r+=~a;r-=-c;"
        "int cmp=(a<c)+2*(b<0)+4*(a>=c)+8*((i128)a>b)+16*(a==a)+32*(b!=m)+64*!b+128*(a&&b)+256*(_Bool)(c-c)+512*(h>m);"
        "double d=(double)a+(double)b;float f=(float)c;long double ld=(long double)b;"
        "r+=(u128)(d*1e-20)+(u128)(f*1e-30f)+(u128)(-ld);i128 back=(i128)(d/3)-(i128)(ld*7)+(i128)(-f*1e-31f);"
        "r+=(u128)back+(u128)(long double)(r>>70);long long l=(long long)b;unsigned u=(unsigned)c;signed char sc=(signed char)a;"
        "unsigned short us=(unsigned short)(a>>3);r+=l+u+sc+us;i128 z=b;z+=n;z-=7;z*=3;z/=-5;z%=1000003;z>>=1;z&=-1;z|=4;z^=2;"
        "u128 w=a;w++;++w;w--;--w;w<<=s;w>>=3;w*=w;w/=(u128)n*n+1;w%=0xfffffffffffffull;"
        "return fold(r)^fold((u128)z)^fold(w)^cmp^fold((u128)back)^(int)(float)(r>>100)^(int)(d*1e-33);}",
        "int128-arithmetic-and-conversions", clang);
    check_test_program_source(test,
        "typedef __int128 i128;typedef unsigned __int128 u128;static int fold(u128 v){return (int)(unsigned)(v^(v>>29)^(v>>64)^(v>>101));}"
        "i128 gi=-12345;u128 gu[3]={1,(u128)1<<100,~(u128)0};struct S{int tag;i128 x;long t;u128 y;};struct S gs={7,-99,9,5};"
        "i128 muladd(i128 a,u128 b,long c){return a*(i128)(b&0xffff)+c;}"
        "u128 stacked(long a,long b,long c,long d,long e,u128 x){return x*7-(u128)(a+2*b+3*c+4*d+5*e);}"
        "u128 sum(int n,...){__builtin_va_list ap;__builtin_va_start(ap,n);u128 r=0;"
        "for(int i=0;i<n;i++){r=r*3+__builtin_va_arg(ap,u128);r+=__builtin_va_arg(ap,int);}__builtin_va_end(ap);return r;}"
        "struct S make(i128 x,int t){struct S s={t,x,t*2,(u128)x*x};return s;}"
        "u128 apply(u128(*f)(long,long,long,long,long,u128),u128 x){return f(1,2,3,4,5,x);}"
        "int tested(int n){u128 v=gu[(unsigned)n%3];gi=gi*7%1000003-n;gu[1]>>=n&7;struct S s=make(gi,n);gs.x+=s.x;gs.y^=s.y;"
        "u128*p=&gu[2];*p-=n;(*p)++;u128 old=(*p)--;i128*q=&gs.x;*q/=3;"
        "u128 r=muladd(gi,v,n)+stacked(n,n,n,n,n,v)+sum(4,v,1,(u128)gi,2,old,3,*p,4)+apply(stacked,gu[1]);"
        "return fold(r)^fold((u128)gs.x)^fold(gs.y)^fold(gu[1])^s.tag^(int)s.t;}",
        "int128-memory-and-calls", clang);
    check_test_program_source(test,
        "typedef __int128 i128;typedef unsigned __int128 u128;static int fold(u128 v){return (int)(unsigned)(v^(v>>29)^(v>>64)^(v>>101));}"
        "u128 pick(u128 a,u128 b){return a?:b;}"
        "int tested(int n){u128 a=(u128)(n+7)<<70;i128 b=-(i128)n*99;u128 x=({u128 t=a;t+=b;t*3;})+({i128 s=b;s-=1;(u128)s;});"
        "i128 y=n>3?b:(i128)a;u128 z=pick(0,a)+pick((u128)n,7);int sw=0;"
        "switch(b){case 0:sw=1;break;case -99:sw=2;break;case -990 ... -100:sw=3;break;default:sw=4;}"
        "u128 c=(u128)(n&1?b:(i128)a)*(n>5?a:(u128)b);u128 e=a*({u128 t=c;t>>=3;t;});"
        "return fold(x)^fold((u128)y)^fold(z)^sw*7^fold(c)^fold(e);}",
        "int128-temporaries", clang);
    // Bit-fields of the 128-bit types, whose bits stay within 8 bytes: read, stored, updated and initialized as others are.
    check_test_program_source(test,
        "typedef __int128 i128;typedef unsigned __int128 u128;struct B{u128 a:20;i128 b:40;int k;u128 c:3;};struct B gb={5,-7,1,2};"
        "int tested(int n){struct B b={(u128)n,(i128)n*-1000,n,1};b.a+=7;b.b*=3;b.c=n;u128 old=b.a++;i128 x=--b.b;gb.b-=n;gb.a^=(u128)n<<5;"
        "return (int)(b.a+old)^(int)(x>>3)^(int)b.c^(int)gb.b^(int)gb.a^b.k;}",
        "int128-bit-fields", clang);
    // Long double values kept in temporaries, which frame objects of their own now hold (as the 128-bit integers').
    check_test_program_source(test,
        "long double pick(long double a,long double b){return a?:b;}"
        "int tested(int n){long double a=n*0.75L;long double x=({long double t=a;t*=3;t;})+({long double s=-a;s+1;});"
        "long double y=pick(0.0L,a)+pick(n,2.5L);long double w=a*({long double q=x;q/=2;q;});"
        "return (int)(x*16)^(int)(y*8)^(int)(w*4);}",
        "long-double-temporaries", clang);
    // Complex values (#133): double's sums, differences, products and quotients (libgcc's __muldc3 and __divdc3 for two
    // complex operands, a real one kept real), negation, the conjugate and equality, with infinities, NaNs and a zero
    // divisor; (inf + inf i) times 1, whose inline product's two NaNs __muldc3 recovers as infinities. Each part's bits
    // folded, so that signed zeros count.
    check_test_program_source(test,
        "typedef _Complex double cd;typedef _Complex float cf;typedef _Complex long double cl;"
        "static unsigned fold(double d){union{double d;unsigned long u;}x;x.d=d;return (unsigned)(x.u^(x.u>>29));}"
        "static unsigned foldz(cd z){return fold(__real__ z)*3u^fold(__imag__ z);}"
        "static unsigned foldf(cf z){return fold(__real__ z)*5u^fold(__imag__ z);}"
        "static unsigned foldl(cl z){return fold((double)__real__ z)*7u^fold((double)__imag__ z)^fold((double)(__real__ z-(double)__real__ z))*11u;}"
        "int tested(int n){volatile double x=n*0.75,zero=0.0;double inf=1.0/zero,nan=zero/zero;"
        "cd a=x+(n-2)*1.0i,b=(n%5-2)+0.5i*n,c=inf+x*1.0i,d=nan+1.0i;"
        "cd s=a+b,t=a-b,u=a*b,v=a/b,w=x+a,y=a-x,o=x-b,p=x*b,q=b/x,r=x/b,m=-a,k=~b;"
        "cd e=c*a,f=a/c,g=d*a,h=c*c,j=a/(b-b),ii=a,kk;__real__ ii=inf;__imag__ ii=inf;kk=ii*(cd)1.0;"
        "unsigned acc=foldz(s)^foldz(t)*5^foldz(u)*7^foldz(v)*11^foldz(w)*13^foldz(y)*17^foldz(p)*19^foldz(q)*23^foldz(r)*29^foldz(m)*31^foldz(k)*37;"
        "acc^=foldz(e)*41^foldz(f)*43^foldz(g)*47^foldz(h)*53^foldz(j)*59^foldz(o)*61^foldz(kk)*67;"
        "acc^=(a==a)+2*(a==b)+4*(a!=b)+8*(d!=d)+16*(x==a)+32*(c==c);"
        "return (int)acc;}",
        "complex-arithmetic", clang);
    // Complex float's and long double's, and mixed: __mulsc3, __divsc3, __mulxc3 and __divxc3, each part's bits folded.
    check_test_program_source(test,
        "typedef _Complex double cd;typedef _Complex float cf;typedef _Complex long double cl;"
        "static unsigned fold(double d){union{double d;unsigned long u;}x;x.d=d;return (unsigned)(x.u^(x.u>>29));}"
        "static unsigned foldz(cd z){return fold(__real__ z)*3u^fold(__imag__ z);}"
        "static unsigned foldf(cf z){return fold(__real__ z)*5u^fold(__imag__ z);}"
        "static unsigned foldl(cl z){return fold((double)__real__ z)*7u^fold((double)__imag__ z)^fold((double)(__real__ z-(double)__real__ z))*11u;}"
        "int tested(int n){volatile float xf=n*0.5f;volatile long double xl=n*0.25L;"
        "cf a=xf+2.0fi,b=(n-3)-1.5fi;cl c=xl+3.0Li,d=1.0L-n*1.0Li;"
        "cf s=a*b,t=a/b,u=a+b*xf,w2=-a;cl v=c*d,w=c/d,y=c-d+xl,z=xl/d;cl mixed=a*c;cd half=b/(cd)c;"
        "return (int)(foldf(s)^foldf(t)*3^foldf(u)*5^foldl(v)*7^foldl(w)*11^foldl(y)*13^foldl(z)*17^foldl(mixed)*19^foldz(half)*23^foldf(w2)*29^(a==b)^(c!=d)*2);}",
        "complex-float-and-long-double", clang);
    // Complex values passed and returned (in xmm registers, in st0 and st1, on the stack), in structs, read by va_arg,
    // through a function pointer, and in arrays.
    check_test_program_source(test,
        "typedef _Complex double cd;typedef _Complex float cf;typedef _Complex long double cl;"
        "static unsigned fold(double d){union{double d;unsigned long u;}x;x.d=d;return (unsigned)(x.u^(x.u>>29));}"
        "static unsigned foldz(cd z){return fold(__real__ z)*3u^fold(__imag__ z);}"
        "static unsigned foldf(cf z){return fold(__real__ z)*5u^fold(__imag__ z);}"
        "static unsigned foldl(cl z){return fold((double)__real__ z)*7u^fold((double)__imag__ z)^fold((double)(__real__ z-(double)__real__ z))*11u;}"
        "struct holder{int tag;cd z;cf w;cl v;};"
        "static cd scale(cd z,double k){return z*k;}"
        "static cf twistf(cf z){return ~z*2.0f;}"
        "static cl twistl(cl z,cl w){return z*w;}"
        "static struct holder make(cd z,int t){struct holder h={t,z,(cf)z,(cl)z*2};return h;}"
        "static cd many(cd a,cd b,cd c,cd d,cd e,cd f,double g,cd h,cf i,cl j){return a+b*c-d/e+f*g+h+i+j;}"
        "static cd sum(int n,...){__builtin_va_list ap;__builtin_va_start(ap,n);cd r=0;for(int i=0;i<n;i++){r+=__builtin_va_arg(ap,cd);r*=1.5;}__builtin_va_end(ap);return r;}"
        "static cl suml(int n,...){__builtin_va_list ap;__builtin_va_start(ap,n);cl r=0;for(int i=0;i<n;i++)r+=__builtin_va_arg(ap,cl);__builtin_va_end(ap);return r;}"
        "static cf sumf(int n,...){__builtin_va_list ap;__builtin_va_start(ap,n);cf r=0;for(int i=0;i<n;i++)r=r*2.0f+__builtin_va_arg(ap,cf);__builtin_va_end(ap);return r;}"
        "static cd mul(cd a,cd b){return a*b;}"
        "static cd quo(cd a,cd b){return a/b;}"
        "static cd(*pick(int k))(cd,cd){return k&1?mul:quo;}"
        "int tested(int n){cd a=n+0.5i,b=1.0-n*1.0i;struct holder h=make(a,n);cd arr[3]={a,b,a*b};arr[n&1]+=arr[2];"
        "unsigned acc=foldz(scale(a,n))^foldf(twistf((cf)b))*3^foldl(twistl(a,b))*5^foldz(h.z)*7^foldf(h.w)*11^foldl(h.v)*13^h.tag;"
        "acc^=foldz(many(a,b,a,b,a,b,n,a,(cf)b,(cl)a))*17^foldz(sum(3,a,b,(cd)n))*19^foldl(suml(2,(cl)a,(cl)b))*23^foldf(sumf(3,(cf)a,(cf)b,(cf)n))*29;"
        "acc^=foldz(pick(n)(a,b))*31^foldz(arr[0])*37^foldz(arr[1])*41;"
        "return (int)acc;}",
        "complex-calls-and-varargs", clang);
    // Compound assignments into complex, double, int, _Bool and long long destinations, conversions between the complex
    // types, ?:, and truth tests in conditions.
    check_test_program_source(test,
        "typedef _Complex double cd;typedef _Complex float cf;typedef _Complex long double cl;"
        "static unsigned fold(double d){union{double d;unsigned long u;}x;x.d=d;return (unsigned)(x.u^(x.u>>29));}"
        "static unsigned foldz(cd z){return fold(__real__ z)*3u^fold(__imag__ z);}"
        "static unsigned foldf(cf z){return fold(__real__ z)*5u^fold(__imag__ z);}"
        "static unsigned foldl(cl z){return fold((double)__real__ z)*7u^fold((double)__imag__ z)^fold((double)(__real__ z-(double)__real__ z))*11u;}"
        "int tested(int n){cd a=n-4.0+(n%3)*1.0i;cf f=a;cl l=a;double d=n;int i=n;_Bool b=0;long long ll=n;"
        "a+=f;a*=l;f/=a;l-=a*1.0i;d*=a;i+=a;b+=a*0.0;ll-=a;"
        "cd z=n>3?a:(cd)f;"
        "int t=0;if(a)t|=1;if(!f)t|=2;if(a&&l)t|=4;if(f||0)t|=8;t|=16*(_Bool)(l-l);while(z){z-=z;t+=32;}"
        "return (int)(foldz(a)^foldf(f)*3^foldl(l)*5^fold(d)*7^i*11^b*13^(unsigned)ll*17^foldz(z)*19^t);}",
        "complex-updates-and-truth", clang);
    // Complex globals, a static local and an array's elements, from static data, updated each call; a compound
    // literal, and folded constants.
    check_test_program_source(test,
        "typedef _Complex double cd;typedef _Complex float cf;typedef _Complex long double cl;"
        "static unsigned fold(double d){union{double d;unsigned long u;}x;x.d=d;return (unsigned)(x.u^(x.u>>29));}"
        "static unsigned foldz(cd z){return fold(__real__ z)*3u^fold(__imag__ z);}"
        "static unsigned foldf(cf z){return fold(__real__ z)*5u^fold(__imag__ z);}"
        "static unsigned foldl(cl z){return fold((double)__real__ z)*7u^fold((double)__imag__ z)^fold((double)(__real__ z-(double)__real__ z))*11u;}"
        "cd ga=1.5+2.5i,garr[2]={1.0i,3.0};static cf gf=-1.0f+0.5fi;cl gl=2.0L-3.0Li;"
        "int tested(int n){static cd counter;counter+=n*1.0i;ga*=1.0+n*0.5i;garr[n&1]+=ga;gf+=n;gl/=1.0L+n;"
        "cd local=(cd){0};local=2.0i*2.0i;cd folded=(1.0+2.0i)*(3.0-1.0i);"
        "return (int)(foldz(ga)^foldz(garr[0])*3^foldz(garr[1])*5^foldf(gf)*7^foldl(gl)*11^foldz(counter)*13^foldz(local)*17^foldz(folded)*19);}",
        "complex-static-data-and-constants", clang);
    // GNU's complex integer types (#137), each from _Complex signed char to _Complex unsigned long long: sums, differences,
    // products (ac - bd, ad + bc), quotients by clang's formula ((ac + bd) / (cc + dd), a real operand's imaginary part 0,
    // wrapped to the element's width before the division), negation, the conjugate, equality and truth, in 8 and 16 bits
    // as clang computes them; compound assignments, conversions to and from the scalar and floating complex types,
    // globals, and calls passing and returning them, va_arg among them.
    check_test_program_source(test,
        "typedef _Complex signed char cc;typedef _Complex unsigned char cuc;typedef _Complex short cs;typedef _Complex unsigned short cus;"
        "typedef _Complex int ci;typedef _Complex unsigned cu;typedef _Complex long cl;typedef _Complex unsigned long long cull;"
        "static unsigned long fold(unsigned long h,long r,long i){return ((h^(unsigned long)r)*1099511628211ul)^((unsigned long)i*31u);}\n"
        "#define FOLD(z) h=fold(h,(long)__real__ (z),(long)__imag__ (z))\n"
        "#define MAKE(t,z,r,i) t z;__real__ z=(r);__imag__ z=(i)\n"
        "static ci mul(ci a,ci b){return a*b;}static cl quo(cl a,cl b){return a/b;}static cc twice(cc a){return a+a;}"
        "static cus seven(cus a,cus b,cus c,cus d,cus e,cus f,cus g){return a+b+c+d+e+f+g;}static cull wide(cull a,cull b){return a*b-a;}"
        "static ci sum(int n,...){__builtin_va_list ap;__builtin_va_start(ap,n);ci r=0;for(int i=0;i<n;i++){r+=__builtin_va_arg(ap,ci);r*=2;}"
        "__builtin_va_end(ap);return r;}"
        "ci gi;static cl gl;"
        "int tested(int n){unsigned long h=2166136261u;"
        "MAKE(cc,a,n*13,n-100);MAKE(cc,b,(n&3)+1,n&1);MAKE(cuc,u,n*29,200-n);MAKE(cuc,v,(n&3)+2,n&1);"
        "MAKE(cs,s,n*1000,-n*999);MAKE(cs,t,(n&3)+1,(n&7)+1);MAKE(cus,w,n*4000u,65000u-n);MAKE(cus,x,(n&7)+1,n&3);"
        "MAKE(ci,i,n*123456,-n*654321);MAKE(ci,j,n-9,(n&7)+2);MAKE(cu,p,n*3000000000u,~0u-n);MAKE(cu,q,(n&7)+1,n&3);"
        "MAKE(cl,l,(long)n*(1L<<40),-(long)n*12345);MAKE(cl,m,n+11,(n&15)+3);"
        "MAKE(cull,y,(unsigned long long)n<<50,~0ull/(n+5));MAKE(cull,z,(n&7)+1,n&3);"
        "cc c1=a+b,c2=a-b,c3=a*b,c4=a/b,c5=-a,c6=~a;FOLD(c1);FOLD(c2);FOLD(c3);FOLD(c4);FOLD(c5);FOLD(c6);"
        "cuc u1=u*v,u2=u/v,u3=u-v;FOLD(u1);FOLD(u2);FOLD(u3);"
        "cs s1=s*t,s2=s/t;ci s3=s+n,s4=s/3;FOLD(s1);FOLD(s2);FOLD(s3);FOLD(s4);"
        "cus w1=w*x,w2=w/x;cu w3=w*7u;FOLD(w1);FOLD(w2);FOLD(w3);"
        "ci i1=mul(i,j),i2=i/j,i3=100/j,i4=i*n,i5=n-i;FOLD(i1);FOLD(i2);FOLD(i3);FOLD(i4);FOLD(i5);"
        "cu p1=p*q,p2=p/q,p3=~p;FOLD(p1);FOLD(p2);FOLD(p3);"
        "cl l1=quo(l,m),l2=l*m,l3=l-m*n;FOLD(l1);FOLD(l2);FOLD(l3);"
        "cull y1=wide(y,z),y2=y/z;FOLD(y1);FOLD(y2);"
        "cc ta=twice(a);cus tw=seven(w,x,w,x,w,x,w);ci ts=sum(3,i,j,i1);FOLD(ta);FOLD(tw);FOLD(ts);"
        "gi+=j;gl*=m;FOLD(gi);FOLD(gl);i+=j;i*=2;i/=j;s-=t;w*=x;FOLD(i);FOLD(s);FOLD(w);"
        "_Complex double d=i;cl from=(cl)d;cc narrow=(cc)l;int r=(int)l;_Bool truth=c4;FOLD(d);FOLD(from);FOLD(narrow);"
        "return (int)(h%1000003)+(a==b)+(u!=v)*2+(i==i)*4+truth*8+(r&15)*16+!c6*32;}",
        "complex-integers", clang);
    // The complex types of __int128 and of _BitInts of up to 128 bits, which the usual arithmetic conversions make (#137):
    // the same operations in Tier 0's 128-bit classes, with libgcc's __divti3 and __udivti3 in their quotients, and a
    // _BitInt's parts kept extended from its width (a narrow one's computed in 32 bits); calls passing and returning
    // them (in memory, and in a register for 4 bytes), va_arg, globals, compound assignments and conversions.
    check_test_program_source(test,
        "typedef __typeof__((_Complex char)0+(__int128)0) ci128;typedef __typeof__((_Complex char)0+(unsigned __int128)0) cu128;"
        "typedef __typeof__((_Complex char)0+(_BitInt(100))0) cb100;typedef __typeof__((_Complex char)0+(unsigned _BitInt(65))0) cu65;"
        "typedef __typeof__((_Complex char)0+(_BitInt(40))0) cb40;typedef __typeof__((_Complex char)0+(unsigned _BitInt(13))0) cu13;"
        "static unsigned long fold(unsigned long h,unsigned __int128 r,unsigned __int128 i)"
        "{return ((h^(unsigned long)r^(unsigned long)(r>>64)*7)*1099511628211ul)^((unsigned long)i*31u)^(unsigned long)(i>>64)*131u;}\n"
        "#define FOLD(z) h=fold(h,(unsigned __int128)__real__ (z),(unsigned __int128)__imag__ (z))\n"
        "#define FOLDD(z) h=fold(h,(unsigned __int128)(__int128)__real__ (z),(unsigned __int128)(__int128)__imag__ (z))\n"
        "#define MAKE(t,z,r,i) t z;__real__ z=(r);__imag__ z=(i)\n"
        "static ci128 mul(ci128 a,ci128 b){return a*b;}static cb100 quo(cb100 a,cb100 b){return a/b;}static cu13 twice(cu13 a){return a+a;}"
        "static cu65 three(long x,long y,long z,cu65 a,cu65 b,cu65 c){return a+b+c+x+y+z;}"
        "static ci128 sum(int n,...){__builtin_va_list ap;__builtin_va_start(ap,n);ci128 r=0;for(int i=0;i<n;i++){r+=__builtin_va_arg(ap,ci128);r*=2;}"
        "__builtin_va_end(ap);return r;}"
        "ci128 gi;static cb40 gb;"
        "int tested(int n){unsigned long h=2166136261u;"
        "MAKE(ci128,a,(__int128)n<<90,-(__int128)n*123456789);MAKE(ci128,b,(n&7)+1,(n&3)+1);"
        "MAKE(cu128,u,(unsigned __int128)n<<100,~(unsigned __int128)0-n);MAKE(cu128,v,(n&7)+2,n&3);"
        "MAKE(cb100,c,(_BitInt(100))n<<80,-(_BitInt(100))n*999);MAKE(cb100,d,(n&15)+1,(n&7)+1);"
        "MAKE(cu65,e,(unsigned _BitInt(65))n<<60,(unsigned _BitInt(65))-n);MAKE(cu65,f,(n&3)+1,n&1);"
        "MAKE(cb40,g,(_BitInt(40))n*100003,-(_BitInt(40))n*99);MAKE(cb40,k,(n&7)+3,(n&3)+1);"
        "MAKE(cu13,p,n*700,8000-n);MAKE(cu13,q,(n&3)+1,n&1);"
        "ci128 a1=a+b,a2=a-b,a3=a*b,a4=a/b,a5=-a,a6=~a,a7=mul(a,b);FOLD(a1);FOLD(a2);FOLD(a3);FOLD(a4);FOLD(a5);FOLD(a6);FOLD(a7);"
        "cu128 u1=u*v,u2=u/v,u3=u-v;FOLD(u1);FOLD(u2);FOLD(u3);"
        "cb100 c1=c*d,c2=quo(c,d),c3=c+n,c4=n-c;FOLD(c1);FOLD(c2);FOLD(c3);FOLD(c4);"
        "cu65 e1=e*f,e2=e/f,e3=three(1,2,3,e,f,e);FOLD(e1);FOLD(e2);FOLD(e3);"
        "cb40 g1=g*k,g2=g/k,g3=g*(cb40)3;FOLD(g1);FOLD(g2);FOLD(g3);"
        "cu13 p1=p*q,p2=p/q,p3=twice(p),p4=~p;FOLD(p1);FOLD(p2);FOLD(p3);FOLD(p4);"
        "ci128 ts=sum(3,a,b,a1);FOLD(ts);"
        "gi+=b;gb*=k;FOLD(gi);FOLD(gb);a+=b;a*=2;a/=b;c-=d;FOLD(a);FOLD(c);"
        "_Complex double dd=g;cb40 back=(cb40)c;ci128 widened=p;__int128 r=(__int128)a;_Bool truth=c2;FOLDD(dd);FOLD(back);FOLD(widened);"
        "return (int)(h%1000003)+(a==b)+(u!=v)*2+(c==c)*4+truth*8+(int)(r&15)*16+!p4*32;}",
        "complex-wide-integers", clang);
    // __float128 (#137): a frame object of its 16 bytes, passed and returned as SSE, SSEUP in a vector register (va_arg
    // too), its sums, differences, products and quotients libgcc's __addtf3 ... __divtf3, its comparisons libgcc's against
    // 0 (a NaN's too), its negation a copy's sign bit, its truth __netf2's, and its conversions libgcc's, to and from
    // float, double and long double and the integers of every width (through int to unsigned __int128, the _BitInts and
    // _Bool among them); a global, and compound assignments of a __float128 and of real destinations.
    check_test_program_source(test,
        "typedef __float128 Q;"
        "static unsigned long fold(unsigned long h,Q q){unsigned long w[2];__builtin_memcpy(w,&q,16);return ((h^w[0])*1099511628211ul)^(w[1]*31u);}\n"
        "#define FOLD(q) h=fold(h,(q))\n"
        "static Q add(Q a,Q b){return a+b;}static Q mix(int n,double d,Q a,long double e,Q b){return a*b-d+(Q)n/(Q)e;}Q gq;"
        "static Q sum(int n,...){__builtin_va_list ap;__builtin_va_start(ap,n);Q r=0;for(int i=0;i<n;i++)r+=__builtin_va_arg(ap,Q);__builtin_va_end(ap);return r;}"
        "int tested(int n){unsigned long h=2166136261u;Q a=n,b=3,c=a/b,m=c<0?-c:c;FOLD(c);FOLD(add(a,b));FOLD(-c);FOLD(mix(n,0.5,a,3.0L,b));"
        "int cmp=(a<b)+(a<=b)*2+(a>b)*4+(a>=b)*8+(a==b)*16+(a!=b)*32+!a*64;"
        "Q z=0,nan=z/z;cmp=cmp*131+(nan==nan)+(nan!=nan)*2+(nan<b)*4+(_Bool)nan*8+(nan>=b)*16;"
        "double d=c;float f=c;long double ld=c;long l=c*1000;unsigned long ul=m*1e18;__int128 i128=c*(Q)1e30;unsigned u=m*100;signed char sc=c*10;_Bool bo=c;"
        "unsigned _BitInt(40) b40=m*1e11;_BitInt(100) b100=-c*(Q)1e25;"
        "Q fromu=(unsigned)n*4000000000u,froml=-(long)n*(1L<<40),fromi128=(__int128)n*((__int128)1<<100),fromu128=(unsigned __int128)n<<127;"
        "Q fromf=1.5f*n,fromld=2.25L*n,fromb=(_Bool)n,fromb40=(unsigned _BitInt(40))n<<35,fromc=(char)n;"
        "FOLD(fromu);FOLD(froml);FOLD(fromi128);FOLD(fromu128);FOLD(fromf);FOLD(fromld);FOLD(fromb);FOLD(fromb40);FOLD(fromc);"
        "gq+=c;gq*=2;FOLD(gq);double dd=1.0;dd+=c;int ii=7;ii*=c+1;FOLD(sum(3,a,b,c));if(c)h+=3;"
        "unsigned long du;__builtin_memcpy(&du,&d,8);unsigned fu;__builtin_memcpy(&fu,&f,4);unsigned long lu;__builtin_memcpy(&lu,&ld,8);"
        "return (int)(h%1000003)^cmp^(int)(du%1000033)^(int)(fu%1000037)^(int)(lu%1000039)^(int)l^(int)(ul%1000081)^(int)(i128%1000099)^(int)u^sc^bo^"
        "(int)(b40%1009)^(int)(b100%1013)^ii^(int)(dd*1e6);}",
        "float128", clang);
    // Conversions between unsigned long and float or double (Tier 0's tier0_emit_u64_convert): to them near 2^53, past
    // 2^63 where the halving's kept bit rounds, and near 2^64; back from values up to 2^64 less 4096, every one in range.
    check_test_program_source(test,
        "unsigned long big(int n){return 0xfffffffffffffc00ul-(unsigned long)(n+3)*0x400ul;}"
        "int tested(int n){unsigned long u=big(n),w=(1ul<<63)+(unsigned long)(n+3)*1024u+1,t=(1ul<<53)+(unsigned long)(n+3);"
        "double d=u,e=t;float f=w,g=t;"
        "unsigned long r=(unsigned long)(d/2)+(unsigned long)(f/4)+(unsigned long)e+(unsigned long)g+(unsigned long)f+(unsigned long)(d-4096.0);"
        "unsigned long hi=(unsigned long)(double)w;return (int)(r%9973)^(int)(hi%10007)^(int)(u%1000)^(int)(t%977);}",
        "unsigned-long-floating-conversions", clang);
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
    check_test_program_source(test,
        "int missing(void);int tested(int n){int x=0;int y=__builtin_choose_expr(0,({missing();n;}),({x++;n+1;}));return x*100+y;}",
        "statement-region-static-selection", clang);
    check_test_program_source(test,
        "int missing(void);int tested(int n){int x=n;"
        "int a=_Generic(__builtin_rotateleft8(x++,missing()),unsigned char:3,default:7);"
        "int b=_Generic(__builtin_ia32_rdpid(),unsigned:5,default:9);"
        "int c=_Generic(__builtin_ia32_pause(),int:11,default:13);return x+a+b+c;}",
        "intrinsic-generic-controls-unevaluated", clang);
    // GNU C's labels as values (#133): an interpreter's dispatch through a static table of labels' addresses, a computed goto
    // through a local and through ?:, the addresses compared, and a computed goto out of a variable-length array's block.
    check_test_program_source(test,
        "static int run(const unsigned char*c,int n){static const void*const t[]={&&add,&&mul,&&jump,&&end,&&dup};"
        "int acc=n;int pc=0;void*last=&&end;goto*t[c[pc]];"
        "add:acc+=c[pc+1];pc+=2;goto*t[c[pc]];"
        "mul:acc*=c[pc+1];pc+=2;goto*t[c[pc]];"
        "jump:pc=c[pc+1];goto*(pc>8?&&end:t[c[pc]]);"
        "dup:acc+=acc;pc+=1;last=&&dup;goto*t[c[pc]];"
        "end:return last==&&dup?acc+1000:acc;}"
        "int tested(int n){static const unsigned char p[]={0,3,1,5,4,2,9,0,100,3};static const unsigned char q[]={1,2,0,7,3};"
        "void*out=&&done;int r=run(p,n)*31+run(q,n);"
        "for(int i=0;i<3;i++){int v[(n&3)+1];v[0]=i;r+=v[0];if(i==1)goto*out;}r=-1;"
        "done:return r;}",
        "labels-as-values-dispatch", clang);
    // A computed goto's operand is the whole expression after its `*` (#431's review): through ?: of an int's condition,
    // a void *'s sum, a comma and an assignment, each where `*` taken as the operand's prefix would read another value.
    check_test_program_source(test,
        "int tested(int n){void*t[2]={&&even,&&odd};void*p=t[n&1];int r=0,k=0;goto*p+0;"
        "even:r+=2;k+=1;goto*k<3?t[0]:&&odd;"
        "odd:r+=3;k+=1;goto*k<5?t[1]+0:&&last;"
        "last:if(r>12)goto*r++,&&done;goto*p=&&done;"
        "done:return r*100+k*10+(p==&&done);}",
        "labels-as-values-whole-operand", clang);
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
    check_test_program_boundary(test, "int tested(int n){return (int)__builtin_sqrt(n+4);}", "math-runtime-symbol-rollback", true, true, 0, 0);
    check_test_program_boundary(test, "double __errno_location(void);int tested(int n){return (int)__builtin_sqrt(n+4);}",
                                "incompatible-errno-accessor-function", true, false, 0, 0);
    check_test_program_boundary(test, "extern int __errno_location;int tested(int n){return (int)__builtin_sqrt(n+4);}",
                                "incompatible-errno-accessor-object", true, false, 0, 0);
    check_test_program_boundary(test, "extern int __errno_location;int tested(int n){return n+(0&&__builtin_sqrt(-1.0));}",
                                "inactive-math-has-no-runtime-accessor", true, true, 0, 0);
    check_test_program_boundary(test, "__int128 wide(__int128 x){return x+1;}", "wide-primary-program", true, true, 0, 0);
    check_test_program_boundary(test, "extern int(*callback)(int);int tested(int n){return callback(n);}",
                                "indirect-function-pointer-call", true, true, 1, 0);
    check_test_program_boundary(test,
        "typedef union __attribute__((transparent_union)){int value;unsigned bits;} U;int consume(U);int tested(int n){return consume(n);}",
        "transparent-union-call-machine-boundary", true, false, 1, 0);
    check_test_program_boundary(test,
        "typedef int V __attribute__((vector_size(16)));extern V global;int tested(int n){return __builtin_reduce_add(global);}",
        "vector-builtin-live-reference-machine-boundary", true, false, 1, 0);
    check_test_program_boundary(test,
        "typedef int V __attribute__((vector_size(16)));extern V global;int tested(int n){return __builtin_reduce_add(__builtin_nondeterministic_value(global));}",
        "vector-builtin-unevaluated-type-argument", true, false, 0, 0);
    check_test_program_boundary(test,
        "typedef int V __attribute__((vector_size(16)));extern V global;int tested(int n){return __builtin_reduce_add(__builtin_shufflevector(global,global,0,1,2,3));}",
        "vector-builtin-shuffle-prefix-reference-mask", true, false, 1, 0);
    check_test_program_boundary(test, "int tested(int n){int values[n+10];return sizeof values;}", "variable-length-array-program", true, true, 0, 0);
    check_test_program_boundary(test, "int extent;int tested(int n){typedef int A[extent+10];return sizeof(A);}",
                                "vla-bound-reference", true, true, 1, 0);
    check_test_program_boundary(test, "int global;int tested(int n){asm volatile(\"\":\"=r\"(global):\"r\"(n):\"memory\");return global;}",
                                "typed-asm-operands-and-machine-boundary", true, true, 1, 0);
    check_test_program_boundary_reason(test, "int tested(int n){asm goto(\"\"::::target);return n;target:return n+1;}",
                                       "typed-asm-goto-target", true, false, 0, 0, "asm goto");
    // File-scope assembly lowers no code: the emission assembles it (check_program_emit_file_asm, #619).
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
    check_test_program_boundary(test, "int tested(int n){_Complex long double z=n;return __real__ z>1;}", "complex-long-double", true, true, 0, 1);
    // GNU's complex integer types of __int128 and the complex _BitInts of up to 128 bits, which the usual arithmetic
    // conversions make (#137), are lowered; the complex types check_lower_complex doesn't take are refused by what they
    // need: a complex _BitInt past 128 bits, and complex types of the floating types other than float, double and long
    // double (clang 23 has no _Float128 in C; __float128 is its spelling).
    check_test_program_boundary(test, "int tested(int n){_Complex int z=n;return __real__ z>1;}", "complex-int", true, true, 0, 1);
    check_test_program_boundary(test, "int tested(int n){_Complex int z=n;__int128 w=n;__typeof__(z+w) v=z+w;return __real__ v>1;}", "complex-int128", true, true, 0, 3);
    check_test_program_boundary(test, "int tested(int n){_Complex char z=n;_BitInt(100) w=n;__typeof__(z+w) v=z*w;return __real__ v>1;}", "complex-bit-int-100", true, true,
                                0, 3);
    check_test_program_boundary_reason(test, "int tested(int n){_Complex char z=n;_BitInt(129) w=n;__typeof__(z+w) v=z+w;return __real__ v>1;}", "complex-bit-int-129", true,
                                       false, 0, 3, "_BitInt");
    // A K&R definition (#154's decision: as clang): its callers pass its parameters' promoted types, which a parameter of a
    // type the promotions change would convert from on entry, not lowered yet.
    check_test_program_boundary_reason(test, "float half(x)float x;{return x/2;}int tested(int n){return (int)half(n);}", "kr-promoted-parameter", true,
                                       false, 1, 0, "K&R parameters the default argument promotions change");
    check_test_program_boundary(test, "__float128 g;int tested(int n){__float128 x=n;g=x*x;return g>1;}", "float128-lowering", true, true, 1, 1);
    check_test_program_boundary_reason(test, "int tested(int n){__float128 x=n;x++;return x>1;}", "float128-increment", true, false, 0, 1,
                                       "increments of __float128 values");
    check_test_program_boundary_reason(test, "int tested(int n){_Complex __float128 z=n;return __real__ z>1;}", "complex-float128", true, false, 0, 1,
                                       "complex types other than _Complex float, double and long double");
    check_test_program_boundary_reason(test, "int tested(int n){_Complex _Float16 z=n;return __real__ z>1;}", "complex-float16", true, false, 0, 1,
                                       "complex types other than _Complex float, double and long double");
    // The 128-bit integers and _BitInt(65) to _BitInt(128): lowered, but a _BitInt past 128 bits, the overflow builtins of
    // a _BitInt of 65 to 127 bits, and a bit-field of them spread over more than 8 bytes.
    check_test_program_boundary(test, "__int128 g;int tested(int n){__int128 x=n;g=x*x;return g>1;}", "int128-lowering", true, true, 1, 1);
    check_test_program_boundary(test, "int tested(int n){_BitInt(128) x=n;return x>1;}", "bit-int-128", true, true, 0, 1);
    check_test_program_boundary_reason(test, "int tested(int n){_BitInt(129) x=n;return x>1;}", "bit-int-129", true, false, 0, 1, "_BitInt");
    check_test_program_boundary_reason(test, "int tested(int n){_BitInt(100) r;return __builtin_add_overflow(n,n,&r);}", "bit-int-100-overflow", true, false, 0, 0,
                                       "overflow builtins of _BitInt of 65 to 127 bits");
    check_test_program_boundary(test, "struct W{unsigned __int128 x:100;};int tested(int n){struct W w;w.x=n;return w.x>1;}", "int128-bit-field-past-8-bytes", true,
                                false, 0, 0);
    check_test_program_boundary(test,
                                "struct __attribute__((packed)) P{unsigned char c:4;unsigned long long x:64;};"
                                "int tested(int n){struct P p;p.x=(unsigned long long)n;return (int)p.x;}",
                                "bit-field-nine-byte-machine-boundary", true, false, 0, 0);
    check_test_program_boundary(test, "int tested(unsigned n){switch(n){case -1:return 1;case 4294967295U:return 2;}return 0;}",
                                "case-conversion-collision", false, false, 0, 0);
    check_test_program_boundary(test, "int tested(int n){switch(n){case 1 ... 5:return 1;case 4 ... 7:return 2;}return 0;}",
                                "overlapping-case-ranges", false, false, 0, 0);
    check_test_program_boundary(test, "int tested(int n){switch(n){default:return 1;default:return 2;}}", "duplicate-default", false, false, 0, 0);
    check_test_program_boundary(test, "int tested(int n){switch((double)n){case 1:return 1;}return 0;}", "noninteger-switch", false, false, 0, 0);
    check_test_program_asm(test, clang);
    check_test_program_build_release();
}

#endif
