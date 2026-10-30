// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// The mask's byte boundaries and a complete control-walk comparison against the original scalar traversal.
#ifndef BUSTER_C_CHECK_CONTROL_INPUT_TEST_INCLUDED
#define BUSTER_C_CHECK_CONTROL_INPUT_TEST_INCLUDED

// Count only possible insertions, including diagnostic-only address rows, at every byte and prefix boundary.
static void check_test_label_capacity(CheckTest* test)
{
    const long page_size = sysconf(_SC_PAGESIZE);
    const uint64_t page = page_size > 0 ? (uint64_t)page_size : 0;
    uint8_t* mappings[2] = { MAP_FAILED, MAP_FAILED };
    bool ready = page >= 193;
    for (uint32_t column = 0; ready && column < 2; column += 1)
    {
        mappings[column] = mmap(0, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        ready = mappings[column] != MAP_FAILED && mprotect(mappings[column] + page, page, PROT_NONE) == 0;
    }
    check_test_expect(test, ready, "label capacity columns have protected tails");
    for (uint32_t count = 0; ready && count <= 193; count += 1)
    {
        uint8_t* kinds = mappings[0] + page - count;
        uint8_t* roles = mappings[1] + page - count;
        for (uint32_t shape = 0; shape < 4; shape += 1)
        {
            uint64_t definitions = 0;
            for (uint32_t token = 0; token < count; token += 1)
            {
                const uint32_t selected = shape == 0 ? 3 : shape == 1 ? 0 : shape == 2 ? 1 : token % 4;
                roles[token] = selected == 0 ? PARSE_ROLE_LABEL : selected == 3 ? PARSE_ROLE_LOGICAL_AND : PARSE_ROLE_PREFIX;
                kinds[token] = selected == 0 ? PARSE_KIND_COLON : selected == 2 ? PARSE_KIND_NUMBER : PARSE_KIND_AMPAMP;
                definitions += selected == 0;
            }
            uint64_t entries = definitions;
            for (uint32_t prefix = 0; prefix <= count; prefix += 1)
            {
                if (prefix != 0 && roles[prefix - 1] == PARSE_ROLE_PREFIX && kinds[prefix - 1] == PARSE_KIND_AMPAMP)
                {
                    entries += 1;
                }
                uint64_t expected = 1;
                while (expected <= entries * 2)
                {
                    expected *= 2;
                }
                Parser parser = { 0 };
                parser.tokens.count = count;
                // A full-unit count must not read the kinds column at all.
                parser.tokens.kinds = prefix != 0 ? kinds : NULL;
                parser.tree.roles = roles;
                for (uint32_t path = 0; path < 2; path += 1)
                {
                    parser.options.scalar = path == 0;
                    check_test_expect(test, check_input_label_capacity(&parser, prefix) == expected,
                                      "label table capacity matches independent insertion counts at guarded tails and prefix cuts");
                }
            }
        }
    }
    for (uint32_t column = 0; column < 2; column += 1)
    {
        if (mappings[column] != MAP_FAILED)
        {
            munmap(mappings[column], page * 2);
        }
    }
}

// With no definitions the full-unit table needs one empty slot, but a prefix must have room for synthetic
// &&name entries. Sixteen distinct colliding names and four repeats exercise insertion, probing and canonical reuse.
static void check_test_label_prefix_capacity(CheckTest* test)
{
    enum
    {
        count = 128,
        addresses = 20,
        distinct = 16,
    };
    uint8_t kinds[count];
    uint8_t roles[count];
    uint32_t parents[count];
    uint32_t names[count] = { 0 };
    uint32_t first[count];
    uint32_t asm_ids[count] = { 0 };
    uint32_t regions[count] = { 0 };
    CheckInputAttributes* attributes[count] = { 0 };
    memset(kinds, PARSE_KIND_IDENTIFIER, sizeof(kinds));
    memset(roles, PARSE_ROLE_OPERAND, sizeof(roles));
    memset(parents, 0xff, sizeof(parents));
    memset(first, 0xff, sizeof(first));
    roles[4] = PARSE_ROLE_OPEN_BODY;
    roles[count - 1] = PARSE_ROLE_CLOSE_BODY;
    parents[4] = count - 1;
    for (uint32_t i = 0; i < addresses; i += 1)
    {
        const uint32_t token = 8 + 4 * i;
        kinds[token] = PARSE_KIND_AMPAMP;
        roles[token] = PARSE_ROLE_PREFIX;
        first[token] = token + 1;
        names[token + 1] = 1 + 64 * (i % distinct);
    }
    Parser parser = { 0 };
    parser.tokens.count = count;
    parser.tokens.kinds = kinds;
    parser.tokens.names = names;
    parser.tree.roles = roles;
    parser.tree.parents = parents;
    for (uint32_t prefix = 0; prefix < 2; prefix += 1)
    {
        for (uint32_t path = 0; path < 2; path += 1)
        {
            parser.options.scalar = path == 0;
            uint32_t bodies[count];
            uint32_t targets[count];
            CheckInputReport reports[32] = { 0 };
            memset(bodies, 0xff, sizeof(bodies));
            memset(targets, 0xff, sizeof(targets));
            CheckInput input =
            {
                .reports = reports,
                .report_capacity = BUSTER_ARRAY_LENGTH(reports),
                .declaration_bodies = bodies,
                .label_targets = targets,
                .asm_ids = asm_ids,
                .expression_regions = regions,
                .attributes = attributes,
            };
            Arena scratch = arena_reserve(8192);
            const uint64_t capacity = scratch.capacity;
            // The old token-sized table cannot fit; the precise insertion bound can.
            scratch.capacity = prefix != 0 ? 64 * sizeof(uint32_t) : sizeof(uint32_t);
            CheckInputReader reader =
            {
                .output = &input,
                .parser = &parser,
                .scratch = &scratch,
                .first_child = first,
                .fragment_end = count,
            };
            check_test_expect(test, scratch.base != NULL, "label prefix fixture has scratch space");
            if (scratch.base != NULL)
            {
                if (prefix != 0)
                {
                    check_input_controls_prefix(&reader, 96);
                }
                else
                {
                    check_input_controls(&reader);
                }
                check_test_expect(test, !input.resource && scratch.length == 0 && reader.fragment_end == count,
                                  "label capacity fits the insertion bound and restores scratch and fragment limits");
                check_test_expect(test, input.report_count == (prefix != 0 ? 0 : addresses) && input.label_address_count == (prefix != 0 ? addresses : 0),
                                  "prefix reserves synthetic labels while full completion diagnoses missing definitions");
                for (uint32_t i = 0; i < addresses; i += 1)
                {
                    const uint32_t token = 8 + 4 * i;
                    const uint32_t expected = prefix != 0 ? 8 + 4 * (i % distinct) : BUSTER_CHECK_NONE;
                    check_test_expect(test, targets[token] == expected, "colliding label references retain their first canonical target");
                    if (prefix == 0)
                    {
                        check_test_expect(test, reports[i].token == token + 1 && reports[i].message == CHECK_INPUT_LABEL_UNDECLARED,
                                          "missing labels retain source-ordered diagnostics with a one-slot empty table");
                    }
                }
            }
            scratch.capacity = capacity;
            arena_release(&scratch);
        }
    }
}

static void check_test_control_masks(CheckTest* test)
{
    check_test_label_capacity(test);
    static const struct
    {
        uint8_t kind;
        uint8_t role;
        uint8_t passes;
    } cases[] =
    {
        { PARSE_KIND_IDENTIFIER, PARSE_ROLE_OPERAND, 0 },
        { PARSE_KIND_END, PARSE_ROLE_OPEN_BODY, 1 },
        { PARSE_KIND_COLON, PARSE_ROLE_LABEL, 1 },
        { PARSE_KIND_CASE, PARSE_ROLE_OPEN_CASE, 1 },
        { PARSE_KIND_GOTO, PARSE_ROLE_NONE, 3 },
        { PARSE_KIND_AMPAMP, PARSE_ROLE_PREFIX, 3 },
        { PARSE_KIND_AMPAMP, PARSE_ROLE_LOGICAL_AND, 0 },
        { PARSE_KIND_SEMI, PARSE_ROLE_TERMINATOR, 2 },
        { PARSE_KIND_NUMBER, PARSE_ROLE_PREFIX, 0 },
        { PARSE_KIND_GOTO, PARSE_ROLE_OPEN_CASE, 3 },
        { PARSE_KIND_SEMI, PARSE_ROLE_LABEL, 3 },
        { PARSE_KIND_AMPAMP, PARSE_ROLE_LABEL, 1 },
        { PARSE_KIND_GOTO, PARSE_ROLE_PREFIX, 3 },
        { PARSE_KIND_SEMI, PARSE_ROLE_PREFIX, 2 },
        { PARSE_KIND_AMPAMP, PARSE_ROLE_OPEN_BODY, 1 },
        // A `__label__` list, which the first scan pushes (#147).
        { PARSE_KIND_LABEL, PARSE_ROLE_NONE, 1 },
        { PARSE_KIND_LABEL, PARSE_ROLE_OPERAND, 1 },
    };
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    uint8_t* mappings[2] = { MAP_FAILED, MAP_FAILED };
    bool ready = true;
    for (uint32_t column = 0; column < 2; column += 1)
    {
        mappings[column] = mmap(0, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        ready = mappings[column] != MAP_FAILED && ready;
        if (mappings[column] != MAP_FAILED)
        {
            ready = mprotect(mappings[column] + page, page, PROT_NONE) == 0 && ready;
        }
    }
    check_test_expect(test, ready, "control event columns have protected tails");
    for (uint32_t count = 0; ready && count <= BUSTER_CHECK_CONTROL_TOKENS; count += 1)
    {
        uint8_t* kinds = mappings[0] + page - count;
        uint8_t* roles = mappings[1] + page - count;
        for (uint32_t offset = 0; offset < BUSTER_ARRAY_LENGTH(cases); offset += 1)
        {
            uint64_t expected[2] = { 0 };
            for (uint32_t token = 0; token < count; token += 1)
            {
                const uint32_t selected = (token + offset) % BUSTER_ARRAY_LENGTH(cases);
                kinds[token] = cases[selected].kind;
                roles[token] = cases[selected].role;
                for (uint32_t pass = 0; pass < 2; pass += 1)
                {
                    expected[pass] |= (cases[selected].passes & (1u << pass)) != 0 ? UINT64_C(1) << token : 0;
                }
            }
            for (uint32_t pass = 0; pass < 2; pass += 1)
            {
                check_test_expect(test, check_input_control_mask(kinds, roles, count, pass != 0) == expected[pass],
                                  "control event mask matches independently listed cases, overlapping roles and every guarded tail");
            }
        }
    }
    if (ready)
    {
        static const uint32_t events[] = { 0, 63, 64, 127, 128, 191, 192 };
        uint8_t* kinds = mappings[0] + page - 193;
        uint8_t* roles = mappings[1] + page - 193;
        memset(kinds, PARSE_KIND_IDENTIFIER, 193);
        memset(roles, PARSE_ROLE_OPERAND, 193);
        for (uint32_t event = 0; event < BUSTER_ARRAY_LENGTH(events); event += 1)
        {
            kinds[events[event]] = PARSE_KIND_GOTO;
        }
        Parser parser = { 0 };
        parser.tokens.count = 193;
        parser.tokens.kinds = kinds;
        parser.tree.roles = roles;
        for (uint32_t pass = 0; pass < 2; pass += 1)
        {
            CheckInputControlScan scan = { 0 };
            for (uint32_t event = 0; event < BUSTER_ARRAY_LENGTH(events); event += 1)
            {
                check_test_expect(test, check_input_control_next(&parser, &scan, pass != 0) == events[event],
                                  "control events keep source order across empty spans and word boundaries");
            }
            check_test_expect(test, check_input_control_next(&parser, &scan, pass != 0) == 193, "control event iterator ends at the exact count");
        }
        // A preloaded final block needs no column access; the next call must terminate without wrapping u32.
        parser.tokens.count = UINT32_MAX;
        CheckInputControlScan last = { .selected = UINT64_C(1) << 63, .first = UINT32_MAX - 64, .after = UINT32_MAX };
        check_test_expect(test, check_input_control_next(&parser, &last, false) == UINT32_MAX - 1 &&
                               check_input_control_next(&parser, &last, false) == UINT32_MAX, "control event iterator does not wrap at the u32 limit");
    }
    for (uint32_t column = 0; column < 2; column += 1)
    {
        if (mappings[column] != MAP_FAILED)
        {
            munmap(mappings[column], page * 2);
        }
    }
}

static void check_test_control_walks(CheckTest* test)
{
    check_test_label_prefix_capacity(test);
    enum { count = 200 };
    uint8_t kinds[count];
    uint8_t roles[count];
    uint8_t deferred[count];
    uint32_t parents[count];
    uint32_t names[count] = { 0 };
    uint32_t first[count];
    uint32_t asm_ids[count] = { 0 };
    uint32_t regions[count] = { 0 };
    CheckInputAttributes attribute = { 0 };
    CheckInputAttributes* attributes[count] = { 0 };
    memset(kinds, PARSE_KIND_IDENTIFIER, sizeof(kinds));
    memset(roles, PARSE_ROLE_OPERAND, sizeof(roles));
    memset(parents, 0xff, sizeof(parents));
    memset(first, 0xff, sizeof(first));
    roles[4] = roles[84] = PARSE_ROLE_OPEN_BODY;
    roles[70] = roles[159] = PARSE_ROLE_CLOSE_BODY;
    parents[4] = 70;
    parents[84] = 159;
    roles[20] = roles[85] = PARSE_ROLE_LABEL;
    kinds[20] = kinds[85] = PARSE_KIND_COLON;
    first[20] = 21;
    first[85] = 86;
    names[21] = names[40] = names[55] = names[86] = names[101] = names[191] = 42;
    kinds[30] = kinds[39] = kinds[100] = kinds[108] = PARSE_KIND_GOTO;
    kinds[31] = PARSE_KIND_STAR;
    roles[31] = PARSE_ROLE_RETURN;
    parents[31] = 30;
    first[30] = 31;
    first[39] = 40;
    first[100] = 101;
    first[108] = 109;
    asm_ids[108] = 1;
    kinds[54] = kinds[190] = PARSE_KIND_AMPAMP;
    roles[54] = roles[190] = PARSE_ROLE_PREFIX;
    first[54] = 55;
    first[190] = 191;
    kinds[48] = PARSE_KIND_SWITCH;
    roles[50] = PARSE_ROLE_CLOSE_CONDITION;
    first[50] = 48;
    parents[50] = 70;
    kinds[60] = PARSE_KIND_CASE;
    roles[60] = PARSE_ROLE_OPEN_CASE;
    parents[60] = 65;
    parents[65] = 50;
    kinds[66] = kinds[136] = PARSE_KIND_SEMI;
    parents[66] = 50;
    parents[136] = 84;
    attributes[66] = attributes[136] = &attribute;
    Parser parser = { 0 };
    parser.tokens.count = count;
    parser.tokens.kinds = kinds;
    parser.tokens.names = names;
    parser.tree.roles = roles;
    parser.tree.parents = parents;
    for (uint32_t variant = 0; variant < 6; variant += 1)
    {
        memset(deferred, 0, sizeof(deferred));
        if (variant == 1)
        {
            memset(deferred + 4, 1, 67);
        }
        if (variant == 2)
        {
            memset(deferred + 84, 1, 76);
        }
        if (variant == 3)
        {
            deferred[4] = 1;
        }
        if (variant == 4)
        {
            for (uint32_t token = 0; token < count; token += 2)
            {
                deferred[token] = kinds[token] == PARSE_KIND_IDENTIFIER && roles[token] == PARSE_ROLE_OPERAND;
            }
        }
        if (variant == 5)
        {
            deferred[39] = deferred[54] = 1;
        }
        uint32_t bodies[2][count];
        uint32_t targets[2][count];
        uint32_t owners[2][count];
        uint32_t indirect[2] = { 0 };
        CheckInputReport reports[2][32] = { 0 };
        CheckInput inputs[2] = { 0 };
        memset(bodies, 0xff, sizeof(bodies));
        memset(targets, 0xff, sizeof(targets));
        memset(owners, 0xff, sizeof(owners));
        for (uint32_t path = 0; path < 2; path += 1)
        {
            parser.options.scalar = path == 0;
            Arena scratch = arena_reserve(8192);
            inputs[path] = (CheckInput)
            {
                .deferred_tokens = variant == 0 ? NULL : deferred,
                .reports = reports[path],
                .report_capacity = 32,
                .declaration_bodies = bodies[path],
                .label_targets = targets[path],
                .control_owners = owners[path],
                .asm_ids = asm_ids,
                .expression_regions = regions,
                .attributes = attributes,
            };
            CheckInputReader reader =
            {
                .output = inputs + path,
                .parser = &parser,
                .scratch = &scratch,
                .first_child = first,
                .fragment_end = count,
            };
            check_test_expect(test, scratch.base != NULL, "control walk comparison has scratch space");
            if (scratch.base != NULL)
            {
                check_input_controls(&reader);
            }
            indirect[path] = reader.indirect_gotos;
            arena_release(&scratch);
        }
        bool equal = !inputs[0].resource && !inputs[1].resource && inputs[0].report_count == inputs[1].report_count &&
                     inputs[0].label_address_count == inputs[1].label_address_count && indirect[0] == indirect[1] &&
                     memcmp(bodies[0], bodies[1], sizeof(bodies[0])) == 0 && memcmp(targets[0], targets[1], sizeof(targets[0])) == 0 &&
                     memcmp(owners[0], owners[1], sizeof(owners[0])) == 0;
        for (uint32_t report = 0; equal && report < inputs[0].report_count; report += 1)
        {
            equal = reports[0][report].token == reports[1][report].token && reports[0][report].message == reports[1][report].message &&
                    reports[0][report].pragma == reports[1][report].pragma;
        }
        check_test_expect(test, equal, "filtered control walks match full scalar ownership, targets, counts and diagnostic order under deferred masks");
        if (variant == 0)
        {
            check_test_expect(test, bodies[1][20] == 4 && bodies[1][85] == 84 && targets[1][39] == 20 && targets[1][54] == 20 && targets[1][100] == 85,
                              "same-name labels resolve in their own function across body gaps");
            check_test_expect(test, owners[1][60] == 50 && owners[1][65] == 50 && bodies[1][108] == BUSTER_CHECK_NONE && targets[1][108] == BUSTER_CHECK_NONE,
                              "case ownership and asm-goto exclusion retain their independent meanings");
            check_test_expect(test, bodies[1][190] == BUSTER_CHECK_NONE && inputs[1].label_address_count == 1 && indirect[1] == 1 &&
                                   inputs[1].report_count == 2 && reports[1][0].token == 136 && reports[1][1].token == 191,
                              "body expiry, computed-goto counts and second-pass diagnostic order match independent expectations");
        }
    }
}

#endif
