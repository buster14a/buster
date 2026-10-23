// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original source plans retain contextual overload identities before publication. The low-level Link fixture
// supplies its two independently named external symbols; it does not claim that the source emitter mangles names.

static void check_init_test_overload_parts(CheckTest* test, const CheckInput* input, const CheckStream* stream,
                                           const Parser* parser, const InternTable* identifiers, TypeTable* types,
                                           const CheckInitializers* initializers, const char* source, const char* clang)
{
    static const char* const names[] = {
        "overload_early",
        "overload_i",
        "overload_d",
        "overload_pair",
        "overload_rows",
        "overload_after",
        "overload_auto",
    };
    uint32_t values = 0;
    uint32_t pending = 0;
    for (uint32_t i = 0; i < initializers->count; i += 1)
    {
        const uint32_t declaration = initializers->declaration_tokens[i];
        const uint32_t name = parser->tokens.names[declaration];
        uint32_t fixture = BUSTER_ARRAY_LENGTH(names);
        for (uint32_t j = 0; j < BUSTER_ARRAY_LENGTH(names); j += 1)
        {
            if (intern_length(identifiers, name) == strlen(names[j]) && memcmp(intern_bytes(identifiers, name), names[j], strlen(names[j])) == 0)
            {
                fixture = j;
            }
        }
        if (fixture < BUSTER_ARRAY_LENGTH(names))
        {
            const CheckInitializer* plan = initializers->plans + i;
            const bool candidates = stream->graph == &input->graph && input->graph.overload_candidate_count >= 3;
            check_test_expect(test, candidates, "overload initializer retains the authoritative checked graph");
            for (uint32_t j = 0; candidates && j < plan->count; j += 1)
            {
                const CheckInitPart* part = plan->parts + j;
                if (part->kind == CHECK_INIT_VALUE)
                {
                    const bool wide = fixture == 2 || (fixture >= 3 && part->bit_offset % 128 == 64);
                    const uint32_t selected = wide ? 1 : fixture >= 5 ? 2 : 0;
                    const CheckOverloadCandidate* candidate = input->graph.overload_candidates + selected;
                    const CheckValue original = stream->values[part->node];
                    check_test_expect(test, part->value.object == candidate->object && part->value.overload_head == 0 &&
                                           part->value.constant_kind == CHECK_CONSTANT_ADDRESS && part->value.offset == 0 &&
                                           (part->value.flags & BUSTER_CHECK_STATIC_OBJECT) != 0,
                                      "initializer edge retains the independently expected int/double function object");
                    check_test_expect(test, part->overload_candidate == (original.overload_head != 0 ? selected + 1 : 0),
                                      "initializer selection retains its exact source-time declaration and attributes");
                    values += 1;
                    if (original.overload_head != 0)
                    {
                        pending += 1;
                        check_test_expect(test, original.overload_head <= input->graph.overload_candidate_count,
                                          "resolving an initializer edge leaves its original pending leaf intact");
                        if (fixture == 3)
                        {
                            Arena tiny = { 0 };
                            CheckInitWalker walker = {
                                .types = types,
                                .stream = stream,
                                .scratch = &tiny,
                            };
                            CheckAction action = {
                                .op = CHECK_OP_INITIALIZE,
                                .type = part->type,
                                .node = part->node,
                                .operands = { original },
                            };
                            uint32_t rejected = UINT32_MAX;
                            const CheckDiagnostic diagnostic = check_init_overload(&walker, &action, &rejected);
                            check_test_expect(test, diagnostic == CHECK_DIAG_RESOURCE && rejected == 0 && tiny.length == 0 &&
                                                   action.operands[0].object == original.object && action.operands[0].overload_head == original.overload_head,
                                              "exhausted contextual-address scratch publishes no selected value or candidate");
                            CheckStream orphan = *stream;
                            orphan.graph = NULL;
                            walker.stream = &orphan;
                            check_test_expect(test, check_init_overload(&walker, &action, &rejected) == CHECK_DIAG_INCOMPATIBLE && rejected == 0 &&
                                                   action.operands[0].object == original.object && action.operands[0].overload_head == original.overload_head,
                                              "a pending initializer cannot guess an overload without its authoritative graph");
                        }
                    }
                }
            }
            if (fixture == 3 && candidates)
            {
                CheckDataTestLink emitted = { 0 };
                bool ready = check_data_test_open(&emitted, 3);
                SymbolId* map = ready ? BUSTER_ARENA_PUSH_ARRAY(&emitted.scratch, SymbolId, (uint64_t)input->token_count + 1) : NULL;
                ready &= map != NULL;
                if (ready)
                {
                    memset(map, 0, ((uint64_t)input->token_count + 1) * sizeof(*map));
                    map[input->graph.overload_candidates[0].object] = check_data_test_symbol(&emitted.link, "_Z6choosei", BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_FUNCTION);
                    map[input->graph.overload_candidates[1].object] = check_data_test_symbol(&emitted.link, "_Z6choosed", BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_FUNCTION);
                    map[input->objects[declaration]] = check_data_test_symbol(&emitted.link, "overload_pair", BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_OBJECT);
                    const CheckDataObject object = {
                        .type = input->declared_types[declaration],
                        .object = input->objects[declaration],
                        .initializer = BUSTER_CHECK_NONE,
                        .token = declaration,
                        .initializer_plan = plan,
                    };
                    CheckDataResult result = { 0 };
                    ready = check_data_emit(types, stream, &object, 1, map, input->token_count + 1, &emitted.link, 0,
                                            &emitted.bytes, &emitted.scratch, &result);
                    check_test_expect(test, ready && result.fixup_count == 2, "resolved source fields emit exactly two independent ABS64 references");
                    if (ready)
                    {
                        static const CheckInitExpected expected = {
                            .name = "overload_pair",
                            .size = 16,
                            .class = BUSTER_LINK_CLASS_DATA,
                        };
                        check_init_test_objects(test, &emitted.link, &expected, 1, source, clang);
                    }
                }
                check_test_expect(test, ready, "source overload initializer uses the caller's explicit object-to-symbol map");
                check_data_test_close(&emitted);
            }
        }
    }
    if (values != 0)
    {
        check_test_expect(test, values == 13 && pending >= 10, "scalar, aggregate, generic, address and automatic contexts all retain selected identities");
    }
}
