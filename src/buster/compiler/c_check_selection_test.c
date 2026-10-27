// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Root indexing against an independent parent scan of every slice in two parser trees, including prefix/infix
// operators, casts, calls, comma expressions, ternaries and both selection kinds. No copied oracle implementation.

static void check_test_selection_roots(CheckTest* test)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    const char* source = "int g(int,int);int f(int x){return _Generic(x,int:__builtin_choose_expr(1,"
                         "-(int)(x+g(2,3))+(x?4:5),(x,6)),default:7);}";
    const PPOptions options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
    bool ready = intern_initialize(&identifiers) && pp_initialize(&pp, &options, &identifiers) &&
                 pp_run_source(&pp, "selection-roots.c", (const uint8_t*)source, strlen(source));
    Arena scratch = arena_reserve(UINT64_C(1) << 20);
    ready &= scratch.base != 0;
    check_test_expect(test, ready, "selection root oracle initialization");
    for (uint32_t scalar = 0; ready && scalar < 2; scalar += 1)
    {
        const ParseOptions parse_options = { .scalar = scalar != 0 };
        const bool parsed = parse_initialize(&parser, &parse_options, options.language) && parse_translation_unit(&parser, &pp);
        check_test_expect(test, parsed, "selection root oracle parsing");
        const uint32_t count = parser.tokens.count;
        scratch.length = 0;
        CheckInputReader reader = {
            .parser = &parser,
            .ends = BUSTER_ARENA_PUSH_ARRAY(&scratch, uint32_t, count),
            .postorder_positions = BUSTER_ARENA_PUSH_ARRAY(&scratch, uint32_t, count),
        };
        bool valid = parsed && reader.ends != 0 && reader.postorder_positions != 0;
        for (uint32_t token = 0; valid && token < count; token += 1)
        {
            reader.ends[token] = token;
        }
        for (uint32_t order = 0; valid && order < count; order += 1)
        {
            const uint32_t token = parser.postorder[order];
            const uint32_t parent = parser.tree.parents[token];
            reader.postorder_positions[token] = order;
            if (parent < count && reader.ends[parent] < reader.ends[token])
            {
                reader.ends[parent] = reader.ends[token];
            }
        }
        for (uint32_t first = 0; valid && first <= count; first += 1)
        {
            for (uint32_t end = first; valid && end <= count; end += 1)
            {
                uint32_t roots = 0;
                uint32_t root = BUSTER_CHECK_NONE;
                for (uint32_t token = first; token < end; token += 1)
                {
                    const uint32_t parent = parser.tree.parents[token];
                    if (parent < first || parent >= end)
                    {
                        roots += 1;
                        root = token;
                    }
                }
                const bool complete = roots == 1 && parser.sizes[reader.postorder_positions[root]] == end - first && reader.ends[root] == end - 1;
                const uint32_t expected = complete ? root : BUSTER_CHECK_NONE;
                valid = check_input_range_root(&reader, first, end) == expected;
            }
        }
        check_test_expect(test, valid, "bounded root indexing matches complete-subtree parent oracle for every slice");
        parse_release(&parser);
    }
    arena_release(&scratch);
    pp_release(&pp);
    intern_release(&identifiers);
}
