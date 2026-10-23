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
        check_test_expect(test, !valid && result.status == C_BODY_BINDINGS_INVALID && result.declarations == 0 &&
                               output.length == 0 && scratch.length == mark, "body undo-base underflow escaped validation");
    }
    arena_release(&scratch);
    arena_release(&output);
}

static void check_test_body_binding_guards(CheckTest* test)
{
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    uint8_t* mapping = mmap(0, page * 14, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    bool mapped = mapping != MAP_FAILED;
    for (uint32_t field = 0; mapped && field < 7; field += 1)
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
                                                       result.declarations[used] == guarded.declaration[handle - 1],
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
        munmap(mapping, page * 14);
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
    bool ready = scalar != 0 && vector != 0 && results != 0;
    check_test_expect(test, ready, "body binding job-plan allocation");
    uint32_t maximum = 0;
    for (uint32_t slot = 0; ready && slot < count; slot += 1)
    {
        ready = c_body_bindings_span(scalar + slot, environment, slot, true) && c_body_bindings_span(vector + slot, environment, slot, false);
        check_test_expect(test, ready, "production body events have no enclosing file restore");
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
                                const uint32_t declaration = result->declarations[used];
                                check_test_expect(test, result->tokens[used] == token && declaration == input->declarations[token],
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
    arena_release(&plans);
}
