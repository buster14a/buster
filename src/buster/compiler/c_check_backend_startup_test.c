// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Cold data-only emission, followed by a reusable caller's first body and the same data again (#590).
// Include after check_lanes_test_emit and check_lanes_test_same. Children isolate backend readiness changes.

static void check_lanes_test_backend_startup(CheckLanesTestTally* tally, const char* directory)
{
    static const char* sources[] =
    {
        "",
        "int counter=3;static const char text[]=\"cold\";const char* address=text;\n",
        "static void unused(void){__builtin_trap();}int counter;\n",
    };
    fflush(NULL);
    const pid_t child = fork();
    if (child == 0)
    {
        alarm(30);
        CheckLanesTestTally own = { 0 };
        static InternTable identifiers;
        static Preprocessor pp;
        static Parser parser;
        for (uint32_t scalar = 0; scalar < 2; scalar += 1)
        {
            // The command normally starts cold. Reset markers in this isolated child too, so a caller that already
            // used the backend can run the regression without changing its parent's initialized tables.
            tier0_initialized = false;
            x86_encoder_initialized = false;
            unwind_ready = false;
            x86_effects_count = 0;
            x86_choice_row_count = 0;
            CheckLanesTestEmission cold[BUSTER_ARRAY_LENGTH(sources)][4] = { 0 };
            for (uint32_t phase = 0; phase < 2; phase += 1)
            {
                for (uint32_t fixture = 0; fixture < BUSTER_ARRAY_LENGTH(sources); fixture += 1)
                {
                    char path[PATH_MAX];
                    snprintf(path, sizeof(path), "%s/startup-%u.c", directory, fixture);
                    const bool written = parse_test_write(path, sources[fixture], strlen(sources[fixture]));
                    CheckLanesTestUnit unit;
                    check_lanes_test_compile(&unit, &pp, &parser, &identifiers, path, scalar != 0);
                    check_lanes_test_expect(&own, written && unit.ready && unit.selected.program.body_count == 0,
                                            "startup fixture has no selected bodies", path);
                    for (uint32_t shape = 0; unit.ready && shape < 4; shape += 1)
                    {
                        CheckProgramLanes lanes = { .lanes = 8, .every_lane = true };
                        CheckLanesTestEmission emission = check_lanes_test_emit(&unit, &parser, &pp, &identifiers,
                                                                               shape & 1 ? &lanes : NULL, scalar != 0, (shape & 1) != 0, (shape & 2) != 0);
                        check_lanes_test_expect(&own, emission.emitted && emission.built && emission.active == 1,
                                                "data-only emission still publishes its complete object", path);
                        check_lanes_test_expect(&own, tier0_initialized == (phase != 0) && x86_encoder_initialized == (phase != 0) &&
                                                unwind_ready == (phase != 0) && (x86_effects_count != 0) == (phase != 0) &&
                                                (x86_choice_row_count != 0) == (phase != 0),
                                                "data-only emission preserves backend readiness", path);
                        if (phase == 0)
                        {
                            cold[fixture][shape] = emission;
                        }
                        else
                        {
                            check_lanes_test_expect(&own, check_lanes_test_same(&cold[fixture][shape], &emission),
                                                    "cold data-only object equals its warmed reusable-caller object", path);
                            free(emission.object);
                            free(cold[fixture][shape].object);
                        }
                    }
                    if (unit.ready)
                    {
                        const CheckLanesTestEmission failed = check_lanes_test_emit_limited(&unit, &parser, &pp, &identifiers, NULL,
                                                                                           scalar != 0, true, true, UINT64_MAX, 0);
                        check_lanes_test_expect(&own, !failed.emitted && failed.status == CHECK_PROGRAM_RESOURCE && failed.object == NULL &&
                                                tier0_initialized == (phase != 0) && unwind_ready == (phase != 0),
                                                "a data-only resource failure leaves backend readiness unchanged", path);
                    }
                    check_lanes_test_close(&unit);
                    parse_release(&parser);
                    pp_release(&pp);
                    intern_release(&identifiers);
                    unlink(path);
                }
                if (phase == 0)
                {
                    char path[PATH_MAX];
                    snprintf(path, sizeof(path), "%s/startup-body.c", directory);
                    const char* source = "int first_body(int x){return x+1;}\n";
                    const bool written = parse_test_write(path, source, strlen(source));
                    CheckLanesTestUnit unit;
                    check_lanes_test_compile(&unit, &pp, &parser, &identifiers, path, scalar != 0);
                    check_lanes_test_expect(&own, written && unit.ready && unit.selected.program.body_count == 1,
                                            "a later unit supplies the reusable caller's first body", path);
                    for (uint32_t unwind = 0; unit.ready && unwind < 2; unwind += 1)
                    {
                        CheckLanesTestEmission body = check_lanes_test_emit(&unit, &parser, &pp, &identifiers, NULL, scalar != 0, false, unwind != 0);
                        check_lanes_test_expect(&own, body.emitted && body.built && tier0_initialized && x86_encoder_initialized &&
                                                x86_effects_count != 0 && x86_choice_row_count != 0 && unwind_ready == (unwind != 0),
                                                "the first body initializes backend tables and unwind only when requested", path);
                        free(body.object);
                    }
                    check_lanes_test_close(&unit);
                    parse_release(&parser);
                    pp_release(&pp);
                    intern_release(&identifiers);
                    unlink(path);
                }
            }
        }
        printf("  backend startup: %llu checks, %llu failures, cold data-only objects equal warmed objects on both paths\n",
               (unsigned long long)own.checks, (unsigned long long)own.failures);
        fflush(NULL);
        _exit(own.failures == 0 ? 0 : 1);
    }
    int status = 0;
    const bool waited = child > 0 && waitpid(child, &status, 0) == child;
    check_lanes_test_expect(tally, waited && WIFEXITED(status) && WEXITSTATUS(status) == 0,
                            "data-only startup and later function emission preserve tables, objects and failures", directory);
}
