// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Real-source independent body binding checks. Include before c_check_environment_test.c: that suite supplies
// successful production readers and their independent whole-unit declaration/type columns. Each worker owns
// replay heads, temporary views and result arenas. Reversed one/eight-lane execution must preserve source slots.

typedef struct CBodyBindingTestJobs CBodyBindingTestJobs;
struct CBodyBindingTestJobs
{
    const CheckEnvironment* environment;
    const CBodyBindingSpan* spans;
    CBodyBindings* results;
    Arena output[8];
    Arena scratch[8];
    uint32_t maximum_events;
    bool scalar;
    bool valid[8];
};

static void check_test_body_binding_jobs(void* argument)
{
    CBodyBindingTestJobs* jobs = argument;
    const uint32_t lane = lane_index();
    CBindingReplay replay = { 0 };
    bool valid = !jobs->scalar || c_bindings_replay_initialize(&replay, &jobs->scratch[lane], jobs->environment->bindings->name_capacity, jobs->maximum_events);
    const uint64_t mark = jobs->scratch[lane].length;
    for (uint32_t reverse = lane; valid && reverse < jobs->environment->body_count; reverse += lane_count())
    {
        const uint32_t slot = jobs->environment->body_count - 1 - reverse;
        valid = c_body_bindings_resolve(&jobs->results[slot], jobs->environment, jobs->spans + slot, jobs->scalar,
                                       &replay, &jobs->output[lane], &jobs->scratch[lane]);
        valid &= jobs->scratch[lane].length == mark;
    }
    for (uint32_t key = 0; valid && jobs->scalar && key < replay.name_capacity * C_BINDING_NAMESPACES; key += 1)
    {
        valid &= replay.heads[key] == 0;
    }
    jobs->valid[lane] = valid;
}

static void check_test_body_binding_failures(CheckTest* test, const CheckEnvironment* environment, const CBodyBindingSpan* span)
{
    Arena output = arena_reserve(1u << 20);
    Arena scratch = arena_reserve(1u << 20);
    Arena exhausted = { 0 };
    const uint64_t mark = scratch.length;
    CBodyBindings result;
    const bool ready = output.base != 0 && scratch.base != 0;
    check_test_expect(test, ready, "body binding failure setup");
    if (ready)
    {
        bool valid = c_body_bindings_resolve(&result, environment, span, false, 0, &exhausted, &scratch);
        check_test_expect(test, (span->uses == 0 ? valid && result.status == C_BODY_BINDINGS_OK : !valid && result.status == C_BODY_BINDINGS_RESOURCE) &&
                               result.count == 0 && result.tokens == 0 && exhausted.length == 0 && scratch.length == mark,
                          "body output failure/empty result did not preserve its contract");
        valid = c_body_bindings_resolve(&result, environment, span, false, 0, &output, &exhausted);
        check_test_expect(test, !valid && result.status == C_BODY_BINDINGS_RESOURCE && output.length == 0 && exhausted.length == 0,
                          "body scratch failure published partial results");
        valid = c_body_bindings_resolve(&result, environment, span, false, 0, &scratch, &scratch);
        check_test_expect(test, !valid && result.status == C_BODY_BINDINGS_INVALID && scratch.length == mark,
                          "body resolver accepted aliased output/scratch");
        valid = c_body_bindings_resolve(&result, environment, span, true, 0, &output, &scratch);
        check_test_expect(test, !valid && result.status == C_BODY_BINDINGS_INVALID && output.length == 0 && scratch.length == mark,
                          "scalar body resolver accepted an absent private workspace");
        CBindingReplay replay;
        const bool initialized = c_bindings_replay_initialize(&replay, &scratch, environment->bindings->name_capacity, 0);
        check_test_expect(test, initialized, "undersized body replay setup");
        if (initialized)
        {
            const uint64_t prefix = scratch.length;
            valid = c_body_bindings_resolve(&result, environment, span, true, &replay, &output, &scratch);
            check_test_expect(test, !valid && result.status == C_BODY_BINDINGS_INVALID && output.length == 0 && scratch.length == prefix,
                              "body replay capacity failure changed its prefix");
        }
        arena_pop_to(&scratch, mark);
        CBodyBindingSpan invalid = *span;
        invalid.base = UINT32_MAX;
        valid = c_body_bindings_resolve(&result, environment, &invalid, false, 0, &output, &scratch);
        check_test_expect(test, !valid && result.status == C_BODY_BINDINGS_INVALID && result.handles == 0 &&
                               output.length == 0 && scratch.length == mark, "body undo-base underflow escaped validation");
    }
    arena_release(&scratch);
    arena_release(&output);
}

static void check_test_body_binding_exit_points(CheckTest* test)
{
    Arena storage = arena_reserve(1u << 20);
    Arena scratch = arena_reserve(1u << 20);
    Arena output = arena_reserve(1u << 20);
    bool ready = storage.base != 0 && scratch.base != 0 && output.base != 0;
    check_test_expect(test, ready, "body exit convention fixture allocation");
    for (uint32_t style = 0; ready && style < 4; style += 1)
    {
        arena_pop_to(&storage, 0);
        arena_pop_to(&scratch, 0);
        arena_pop_to(&output, 0);
        CBindings source;
        bool built = c_bindings_initialize(&source, &storage, 6, 32);
        built = built && c_bindings_declare(&source, C_BINDING_ORDINARY, 5, C_BINDING_TYPEDEF, BUSTER_C_BINDING_NONE, 0, 0) != 0 &&
                c_bindings_declare(&source, C_BINDING_ORDINARY, 1, C_BINDING_OBJECT, 1, 1, 0) != 0 &&
                c_bindings_declare(&source, C_BINDING_ORDINARY, 3, C_BINDING_FUNCTION, 2, style >= 2 ? 1 : 2, 0) != 0;
        if (built && style != 2)
        {
            built = c_bindings_declare(&source, C_BINDING_ORDINARY, 2, C_BINDING_OBJECT, 0, 2, 3) != 0 &&
                    c_bindings_declare(&source, C_BINDING_LABEL, 4, C_BINDING_LABEL_KIND, 7, 2, 3) != 0 &&
                    c_bindings_use(&source, C_BINDING_ORDINARY, 1, 3, 3) != 0 &&
                    c_bindings_use(&source, C_BINDING_ORDINARY, 2, 4, 4) != 0 &&
                    c_bindings_use(&source, C_BINDING_ORDINARY, 5, 5, 5) != 0 &&
                    c_bindings_use(&source, C_BINDING_LABEL, 4, 6, 6) != 0;
            built = built && (style != 3 || c_bindings_use(&source, C_BINDING_ORDINARY, 3, 7, 7) != 0);
            built = built && c_bindings_restore(&source, 3, style == 0 ? 7 : 8);
        }
        built = built && c_bindings_restore(&source, 0, 8);
        CEnvironment file;
        built = built && c_environment_build(&file, &source, &storage, &scratch);
        check_test_expect(test, built, "body early/late/omitted restore trace construction");
        if (built)
        {
            const CBodyRange body =
            {
                .begin = 2,
                .end = 8,
            };
            const CheckEnvironment environment =
            {
                .bindings = &source,
                .file = file,
                .bodies = &body,
                .body_count = 1,
                .token_count = 8,
                .valid = true,
            };
            uint32_t reference[32];
            const bool replayed = c_bindings_scalar(&source, reference, &scratch);
            check_test_expect(test, replayed, "body exit full-stream reference");
            for (uint32_t mode = 0; mode < 2; mode += 1)
            {
                CBodyBindingSpan span;
                uint32_t maximum = UINT32_MAX;
                const bool planned = c_body_bindings_spans(&span, 1, &environment, mode == 0, &maximum);
                check_test_expect(test, planned && span.valid && span.end == source.count - 1 &&
                                       span.base == (style >= 2 ? 3 : 2) && maximum == span.end - span.begin,
                                  "body span included translation-unit cleanup or required an empty restore");
                CBindingReplay replay;
                const bool initialized = planned && c_bindings_replay_initialize(&replay, &scratch, 6, maximum);
                CBodyBindings result;
                const bool resolved = initialized && c_body_bindings_resolve(&result, &environment, &span, mode == 0, &replay, &output, &scratch);
                check_test_expect(test, resolved && result.count == (style == 2 ? 0 : style == 3 ? 5 : 4), "body exit convention changed identifier resolution");
                uint32_t used = 0;
                for (uint32_t event = planned ? span.begin : 0; resolved && event < span.end; event += 1)
                {
                    if (source.operation[event] == C_BINDING_USE)
                    {
                        const bool present = used < result.count;
                        check_test_expect(test, present && result.handles[used] == reference[event],
                                          "body entry parameter/label/builtin activation identity changed");
                        used += 1;
                    }
                }
                if (resolved && style != 2)
                {
                    check_test_expect(test, result.handles[2] == 1 && source.declaration[result.handles[2] - 1] == BUSTER_C_BINDING_NONE &&
                                           source.kind[result.handles[2] - 1] == C_BINDING_TYPEDEF,
                                      "source-less builtin collapsed into an unresolved declaration");
                }
                arena_pop_to(&scratch, 0);
                arena_pop_to(&output, 0);
            }
        }
    }
    arena_release(&output);
    arena_release(&scratch);
    arena_release(&storage);
}

static void check_test_body_binding_guards(CheckTest* test)
{
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    uint8_t* mapping = mmap(0, page * 16, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    bool mapped = mapping != MAP_FAILED;
    for (uint32_t field = 0; mapped && field < 8; field += 1)
    {
        mapped = mprotect(mapping + field * page * 2, page, PROT_READ | PROT_WRITE) == 0;
    }
    Arena storage = arena_reserve(1u << 20);
    Arena output = arena_reserve(1u << 20);
    Arena scratch = arena_reserve(1u << 20);
    CBindings producer;
    const bool ready = mapped && output.base != 0 && scratch.base != 0 && c_bindings_initialize(&producer, &storage, 4, 256);
    check_test_expect(test, ready, "guarded body binding setup");
    const uint64_t prefix = storage.length;
    for (uint32_t count = 0; ready && count <= 129; count += 1)
    {
        c_bindings_reset(&producer);
        arena_pop_to(&storage, prefix);
        arena_pop_to(&output, 0);
        arena_pop_to(&scratch, 0);
        for (uint32_t event = 0; event < count && event < 3; event += 1)
        {
            c_bindings_declare(&producer, C_BINDING_ORDINARY, event + 1, event == 2 ? C_BINDING_FUNCTION : C_BINDING_OBJECT, event, event, 0);
        }
        for (uint32_t event = 3; event + 1 < count; event += 1)
        {
            if (event % 4 == 3)
            {
                c_bindings_declare(&producer, C_BINDING_ORDINARY, 2, C_BINDING_OBJECT, event, event, 1);
            }
            else
            {
                c_bindings_use(&producer, C_BINDING_ORDINARY, event % 4 == 0 ? 1 : 2, event, event);
            }
        }
        if (count >= 4)
        {
            c_bindings_restore(&producer, 3, count - 1);
        }
        CEnvironment file;
        const bool built = producer.count == count && c_environment_build(&file, &producer, &storage, &scratch);
        check_test_expect(test, built, "guard trace construction");
        if (built)
        {
            CBindings guarded = producer;
            guarded.heads = 0;
            guarded.active = 0;
            guarded.previous = 0;
            guarded.active_count = 0;
            guarded.capacity = count;
            guarded.key = (uint32_t*)(mapping + page - count * 4);
            guarded.declaration = (uint32_t*)(mapping + page * 3 - count * 4);
            guarded.point = (uint32_t*)(mapping + page * 5 - count * 4);
            guarded.end = (uint32_t*)(mapping + page * 7 - count * 4);
            guarded.depth = (uint32_t*)(mapping + page * 9 - count * 4);
            guarded.operation = mapping + page * 11 - count;
            guarded.kind = mapping + page * 13 - count;
            if (count != 0)
            {
                memcpy(guarded.key, producer.key, count * 4);
                memcpy(guarded.declaration, producer.declaration, count * 4);
                memcpy(guarded.point, producer.point, count * 4);
                memcpy(guarded.end, producer.end, count * 4);
                memcpy(guarded.depth, producer.depth, count * 4);
                memcpy(guarded.operation, producer.operation, count);
                memcpy(guarded.kind, producer.kind, count);
            }
            file.bindings = &guarded;
            CBodyRange body =
            {
                .begin = 2,
                .end = count != 0 ? count - 1 : 0,
            };
            const CheckEnvironment environment =
            {
                .bindings = &guarded,
                .file = file,
                .bodies = &body,
                .body_count = 1,
                .token_count = count,
                .valid = true,
            };
            uint32_t reference[129];
            const bool replayed = c_bindings_scalar(&guarded, reference, &scratch);
            check_test_expect(test, replayed, "guard trace independent full replay");
            for (uint32_t mode = 0; mode < 2; mode += 1)
            {
                CBodyBindingSpan span;
                const bool planned = c_body_bindings_span(&span, &environment, 0, mode == 0);
                check_test_expect(test, planned == (count >= 4), "guarded body plan accepted/rejected the wrong trace");
                if (planned)
                {
                    // The two materialized columns end exactly at the eighth writable page's boundary too.
                    // This catches an unmasked final store as well as the guarded source loads above.
                    Arena bounded =
                    {
                        .base = mapping + page * 14,
                        .capacity = page,
                        .length = page - (uint64_t)(span.end - span.begin) * 8,
                    };
                    CBindings view;
                    const bool translated = c_body_bindings_view(&view, &guarded, &span, &bounded, mode == 0);
                    check_test_expect(test, translated && bounded.length == page, "guarded body view output tail");
                    for (uint32_t event = 0; translated && event < view.count; event += 1)
                    {
                        const uint32_t original = span.begin + event;
                        const uint8_t operation = guarded.operation[original];
                        const uint32_t declaration = guarded.declaration[original];
                        const uint32_t end = operation == C_BINDING_DECLARE ? guarded.end[original] : BUSTER_C_BINDING_NONE;
                        check_test_expect(test, view.declaration[event] == (operation == C_BINDING_RESTORE ? declaration - span.base : declaration) &&
                                               view.end[event] == (end == BUSTER_C_BINDING_NONE || end >= span.end ? BUSTER_C_BINDING_NONE : end - span.begin),
                                          "guarded body view translation fields");
                    }
                    CBindingReplay replay;
                    const bool initialized = c_bindings_replay_initialize(&replay, &scratch, 4, count);
                    check_test_expect(test, initialized, "guarded private replay setup");
                    CBodyBindings result;
                    const bool resolved = initialized && c_body_bindings_resolve(&result, &environment, &span, mode == 0, &replay, &output, &scratch);
                    check_test_expect(test, resolved && span.begin == 2 && span.end == count && span.base == 2,
                                      "guarded body slice/undo/end translation");
                    uint32_t used = 0;
                    for (uint32_t event = 2; resolved && event < count; event += 1)
                    {
                        if (guarded.operation[event] == C_BINDING_USE)
                        {
                            const bool present = used < result.count;
                            const uint32_t handle = reference[event];
                            check_test_expect(test, present && handle != 0, "guarded body reference/result missing a use");
                            if (present && handle != 0)
                            {
                                check_test_expect(test, result.tokens[used] == guarded.declaration[event] &&
                                                       result.handles[used] == handle,
                                                  "guarded private body differs from full replay");
                            }
                            used += 1;
                        }
                    }
                    check_test_expect(test, !resolved || (used == result.count && (count < 6 || result.file_queries != 0)),
                                      "guarded body trace did not consume its file fallback");
                    arena_pop_to(&output, 0);
                    arena_pop_to(&scratch, 0);
                }
            }
        }
    }
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&storage);
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, page * 16);
    }
}

static void check_test_body_bindings(CheckTest* test, const CheckInput* input, bool fallback)
{
    const CheckEnvironment* environment = &input->environment;
    const CBindings* source = environment->bindings;
    const uint32_t count = environment->body_count;
    Arena plans = arena_reserve(1u << 20);
    CBodyBindingSpan* scalar = BUSTER_ARENA_PUSH_ARRAY(&plans, CBodyBindingSpan, (uint64_t)count + 1);
    CBodyBindingSpan* vector = BUSTER_ARENA_PUSH_ARRAY(&plans, CBodyBindingSpan, (uint64_t)count + 1);
    CBodyBindings* results = BUSTER_ARENA_PUSH_ARRAY(&plans, CBodyBindings, (uint64_t)count + 1);
    uint32_t* reference = BUSTER_ARENA_PUSH_ARRAY(&plans, uint32_t, (uint64_t)source->count + 1);
    Arena reference_scratch = arena_reserve(1u << 20);
    bool ready = scalar != 0 && vector != 0 && results != 0 && reference != 0 && reference_scratch.base != 0 &&
                 c_bindings_scalar(source, reference, &reference_scratch);
    check_test_expect(test, ready, "body binding job-plan allocation");
    uint32_t maximum = 0;
    uint32_t vector_maximum = 0;
    ready = ready && c_body_bindings_spans(scalar, count, environment, true, &maximum) &&
                    c_body_bindings_spans(vector, count, environment, false, &vector_maximum);
    check_test_expect(test, ready && maximum == vector_maximum, "complete body span scalar/vector preparation");
    for (uint32_t slot = 0; ready && slot < count; slot += 1)
    {
        CBodyBindingSpan single;
        ready = c_body_bindings_span(&single, environment, slot, false);
        check_test_expect(test, ready && single.begin == vector[slot].begin && single.end == vector[slot].end && single.base == vector[slot].base,
                          "single body span differs from whole-unit preparation");
        if (ready)
        {
            check_test_expect(test, scalar[slot].begin == vector[slot].begin && scalar[slot].end == vector[slot].end &&
                                   scalar[slot].base == vector[slot].base && scalar[slot].uses == vector[slot].uses &&
                                   scalar[slot].declarations == vector[slot].declarations && scalar[slot].slot == vector[slot].slot,
                              "body span scalar/vector metadata differs");
            const uint32_t events = scalar[slot].end - scalar[slot].begin;
            maximum = events > maximum ? events : maximum;
            uint32_t preceding_file = 0;
            for (uint32_t event = 0; event < scalar[slot].begin; event += 1)
            {
                preceding_file += source->operation[event] == C_BINDING_DECLARE && source->depth[event] == 0;
            }
            check_test_expect(test, scalar[slot].base == preceding_file, "body undo base differs from independently counted preceding file activations");
        }
    }
    CBodyBindingSpan invalid;
    check_test_expect(test, !c_body_bindings_span(&invalid, environment, count, false) && invalid.end == 0,
                      "out-of-range body slot produced a plan");
    if (ready && count != 0)
    {
        const uint32_t lanes[] =
        {
            1,
            8,
        };
        for (uint32_t mode = 0; mode < 2; mode += 1)
        {
            for (uint32_t workers = 0; workers < BUSTER_ARRAY_LENGTH(lanes); workers += 1)
            {
                memset(results, 0, (uint64_t)count * sizeof(CBodyBindings));
                CBodyBindingTestJobs jobs =
                {
                    .environment = environment,
                    .spans = mode == 0 ? scalar : vector,
                    .results = results,
                    .maximum_events = maximum,
                    .scalar = mode == 0,
                };
                bool allocated = true;
                for (uint32_t lane = 0; lane < lanes[workers]; lane += 1)
                {
                    jobs.output[lane] = arena_reserve(1u << 20);
                    jobs.scratch[lane] = arena_reserve(1u << 20);
                    allocated &= jobs.output[lane].base != 0 && jobs.scratch[lane].base != 0;
                }
                const bool ran = allocated && lane_run(lanes[workers], check_test_body_binding_jobs, &jobs, 0);
                check_test_expect(test, ran, "body binding workers failed to start");
                for (uint32_t lane = 0; ran && lane < lanes[workers]; lane += 1)
                {
                    check_test_expect(test, jobs.valid[lane], "private body binding/replay failed");
                }
                uint64_t fallback_uses = 0;
                for (uint32_t slot = 0; ran && slot < count; slot += 1)
                {
                    const CBodyBindings* result = results + slot;
                    const CBodyRange body = environment->bodies[slot];
                    check_test_expect(test, result->status == C_BODY_BINDINGS_OK, "body slot lost its result");
                    uint32_t used = 0;
                    for (uint32_t event = 0; event < source->count; event += 1)
                    {
                        if (source->operation[event] == C_BINDING_USE && source->point[event] >= body.begin && source->point[event] < body.end)
                        {
                            const uint32_t token = source->declaration[event];
                            const bool present = used < result->count;
                            check_test_expect(test, present, "body result omitted a source identifier use");
                            if (present)
                            {
                                const uint32_t handle = result->handles[used];
                                const uint32_t declaration = handle != 0 && handle <= source->count ? source->declaration[handle - 1] : BUSTER_CHECK_NONE;
                                check_test_expect(test, result->tokens[used] == token && handle == reference[event] && declaration == input->declarations[token],
                                                  "independent body binding differs from production sequential resolution");
                                if (declaration < input->token_count && input->declared_types[token] != 0)
                                {
                                    check_test_expect(test, environment->declaration_types[declaration] == input->declared_types[token],
                                                      "body binding acquired a different historical TypeId");
                                }
                            }
                            used += 1;
                        }
                    }
                    check_test_expect(test, used == result->count && result->count == jobs.spans[slot].uses,
                                      "body result added/duplicated source uses");
                    fallback_uses += result->statistics.fallback_uses;
                }
                check_test_expect(test, !fallback || mode == 0 || fallback_uses != 0, "body-local closed scopes did not exercise bounded-walk fallback");
                for (uint32_t lane = 0; lane < lanes[workers]; lane += 1)
                {
                    arena_release(&jobs.scratch[lane]);
                    arena_release(&jobs.output[lane]);
                }
            }
        }
        check_test_body_binding_failures(test, environment, vector);
    }
    arena_release(&reference_scratch);
    arena_release(&plans);
}
