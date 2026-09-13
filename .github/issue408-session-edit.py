"""One-session source transport; not part of the implementation PR or test framework."""
import hashlib
from pathlib import Path

path = Path("tools/differential.c")
raw = path.read_bytes()
assert hashlib.sha1(b"blob " + str(len(raw)).encode() + b"\0" + raw).hexdigest() == "236be6f8580de1f3d1744c3a7f28e892a4973f91", "unexpected runner base"
source = raw.decode()

def replace(old, new):
    global source
    assert source.count(old) == 1, old
    source = source.replace(old, new)

replace("// d_cases_run validates and publishes case-owned evidence in registry order.\n", "// d_cases_run validates and publishes case-owned evidence in registry order.\n// d_workers_self_test checks live admission, unequal cases and arena cleanup.\n")
replace("    AtomicU64 next;\n    bool self_test;\n};", """    AtomicU64 next;
    bool self_test;
    // Private self-test observations, protected by spawn_mutex while lanes run.
    // Corpus runs neither update these counters nor wait on the test gates.
    u32 test_jobs;
    u32 test_active;
    u32 test_peak;
    u32 test_started;
    u32 test_finished;
    u32 test_lanes;
    u32 test_arenas;
    u32* test_completions;
    bool test_skew;
};""")
replace("BUSTER_GLOBAL_LOCAL void d_case_lane(void* argument)\n", """// Admit an initial full cohort before releasing its short cases. Case zero
// then waits for every other case, proving that a free lane drains the dynamic
// queue while a longer case remains active. This is an ordering control, not a
// timing measurement. Its independent 30-second rendezvous deadline fails the
// control without stranding lanes; child deadlines remain unchanged.
BUSTER_GLOBAL_LOCAL bool d_worker_self_test_enter(DCaseWork* work, u64 index)
{
    os_mutex_lock(work->settings.spawn_mutex);
    work->test_started += 1;
    work->test_active += 1;
    work->test_peak = BUSTER_MAX(work->test_peak, work->test_active);
    os_mutex_unlock(work->settings.spawn_mutex);
    bool ready = false;
    u64 start = os_now_microseconds();
    while (!ready && os_now_microseconds() - start < 30000000)
    {
        os_mutex_lock(work->settings.spawn_mutex);
        ready = work->test_started >= work->test_jobs;
        if (work->test_skew && work->test_jobs > 1 && index == 0)
        {
            ready &= work->test_finished + 1 == work->count;
        }
        os_mutex_unlock(work->settings.spawn_mutex);
        if (!ready)
        {
#if BUSTER_WINDOWS
            Sleep(1);
#else
            struct timespec delay = {.tv_nsec = 1000000};
            nanosleep(&delay, 0);
#endif
        }
    }
    return ready;
}

BUSTER_GLOBAL_LOCAL void d_case_lane(void* argument)
""")
replace("    DCaseWork* work = argument;\n    Arena* arena = arena_create((ArenaCreation){0});\n    u64 start = arena->position;", """    DCaseWork* work = argument;
    Arena* arena = arena_create((ArenaCreation){0});
    u64 start = arena->position;
    if (work->self_test)
    {
        os_mutex_lock(work->settings.spawn_mutex);
        work->test_lanes += 1;
        work->test_arenas += 1;
        os_mutex_unlock(work->settings.spawn_mutex);
    }""")
replace("        DCaseRecord* record = work->records + index;\n        DSettings settings = work->settings;", """        DCaseRecord* record = work->records + index;
        if (work->self_test) { record->failures += !d_worker_self_test_enter(work, index); }
        DSettings settings = work->settings;""")
replace("                    record->failures = observed.kind != D_EXIT || observed.status != 7 ||", "                    record->failures += observed.kind != D_EXIT || observed.status != 7 ||")
replace("        record->io_failed |= !arena_set_position_and_decommit(arena, start);\n    }\n    BUSTER_CHECK(arena_destroy(arena, 1));\n}", """        record->io_failed |= !arena_set_position_and_decommit(arena, start);
        if (work->self_test)
        {
            // A case is no longer active only after its children were waited,
            // streams closed, completion written and scratch cleanup attempted.
            os_mutex_lock(work->settings.spawn_mutex);
            work->test_active -= 1;
            work->test_finished += 1;
            work->test_completions[index] = work->test_finished;
            os_mutex_unlock(work->settings.spawn_mutex);
        }
    }
    BUSTER_CHECK(arena_destroy(arena, 1));
    if (work->self_test)
    {
        os_mutex_lock(work->settings.spawn_mutex);
        work->test_arenas -= 1;
        os_mutex_unlock(work->settings.spawn_mutex);
    }
}""")
start = source.index("BUSTER_GLOBAL_LOCAL u32 d_workers_self_test(")
end = source.index("BUSTER_GLOBAL_LOCAL u32 d_self_test(", start)
source = source[:start] + r'''BUSTER_GLOBAL_LOCAL u32 d_workers_self_test(Arena* arena, String8 root)
{
    u32 errors = 0, jobs = 0;
    errors += !d_jobs(4, 2, S8("3"), &jobs) || jobs != (BUSTER_SINGLE_THREADED ? 1 : 2);
    errors += !d_jobs(4, 8, S8("1"), &jobs) || jobs != 1;
    errors += d_jobs(0, 8, S8(""), &jobs) || d_jobs(65, 8, S8(""), &jobs);
    errors += d_jobs(4, 8, S8("0"), &jobs) || d_jobs(4, 8, S8("2x"), &jobs);
    errors += d_jobs(4, 8, S8("4294967296"), &jobs);
    u32 requested[] = {1, 2, 4, 4};
    for (u32 pass = 0; pass < BUSTER_ARRAY_LENGTH(requested); pass += 1)
    {
        u32 pass_errors = errors, worker_jobs = 0;
        bool failure_control = pass + 1 == BUSTER_ARRAY_LENGTH(requested);
        DCase tests[] = {{.name = S8("first"), .source = S8("exit")},
                         {.name = S8("second"), .source = S8("exit")},
                         {.name = S8("third"), .source = S8("exit")},
                         {.name = S8("fourth"), .source = S8("exit")},
                         {.name = S8("fifth"), .source = S8("exit")},
                         {.name = S8("sixth"), .source = S8("exit")}};
        DCaseRecord records[BUSTER_ARRAY_LENGTH(tests)] = {0};
        u32 completions[BUSTER_ARRAY_LENGTH(tests)] = {0};
        DCaseWork work = {.settings = {.arena = arena, .timeout_seconds = 1}, .tests = tests, .records = records,
                         .count = BUSTER_ARRAY_LENGTH(tests), .config_count = 1, .self_test = true,
                         .test_completions = completions, .test_skew = !failure_control};
        work.settings.out = path_join(arena, root, string_format(arena, S8("workers-{u32}"), pass));
        errors += !d_create_output(arena, work.settings.out);
        String8 report = string_format_z(arena, S8("{S8}/processes.tsv"), work.settings.out);
        String8 log = string_format_z(arena, S8("{S8}/case.log"), work.settings.out);
        work.settings.report = d_file_open((char*)report.pointer, true, false);
        work.settings.log = d_file_open((char*)log.pointer, true, true);
        work.settings.spawn_mutex = os_mutex_create();
        if (!work.settings.report || !work.settings.log || !work.settings.spawn_mutex) { errors += 1; }
        else
        {
            if (failure_control)
            {
                tests[0].source = S8("timeout");
                tests[1].source = S8("crash");
                tests[2].source = S8("sanitizer");
                tests[3].host = path_join(arena, work.settings.out, S8("missing-program"));
                // Both duplicate directory claims must not count as success.
                tests[5].name = tests[4].name;
            }
            errors += !d_jobs(requested[pass], os_get_logical_thread_count(), os_get_environment_variable(S8("BUSTER_TEST_JOBS")), &worker_jobs);
            work.test_jobs = worker_jobs;
            lane_run(worker_jobs, &d_case_lane, &work);
            u32 failures = d_cases_collect(&work);
            errors += failure_control ? failures < 5 : failures != 0;
            errors += work.test_lanes != worker_jobs || work.test_peak != worker_jobs;
            errors += work.test_active != 0 || work.test_arenas != 0;
            errors += work.test_started != work.count || work.test_finished != work.count;
            if (!failure_control && failures)
            {
                for (u32 index = 0; index < work.count; index += 1)
                {
                    string_print(S8("DIFFERENTIAL_SELF_TEST_RECORD pass={u32} case={S8} rows={u32} failures={u32} io_failed={u32} report_bytes={u64} log_bytes={u64}\n"),
                        pass, tests[index].name, records[index].rows, records[index].failures, (u32)records[index].io_failed, records[index].report_size, records[index].log_size);
                }
            }
            for (u32 index = 0; index < work.count; index += 1)
            {
                errors += records[index].completed != 1;
                errors += completions[index] == 0 || completions[index] > work.count;
                for (u32 other = 0; other < index; other += 1) { errors += completions[index] == completions[other]; }
                if (!failure_control && worker_jobs == 1) { errors += completions[index] != index + 1; }
            }
            if (!failure_control)
            {
                if (worker_jobs > 1) { errors += completions[0] != work.count; }
                // Publication remains byte-identical to the one-lane order,
                // even though the first case deliberately completed last.
                // Inspect through the owning stream: a second OS open with
                // read-only sharing conflicts with the live writer on Windows.
                errors += fseek(work.settings.log, 0, SEEK_SET) != 0;
                char8 output[256];
                size_t output_length = fread(output, 1, sizeof(output), work.settings.log);
                errors += ferror(work.settings.log) != 0;
                errors += !string_equal((String8){.pointer = output, .length = output_length},
                    S8("case=first\ncase=second\ncase=third\ncase=fourth\ncase=fifth\ncase=sixth\n"));
                errors += fseek(work.settings.log, 0, SEEK_END) != 0;
                records[0].completed = 0;
                errors += d_cases_collect(&work) == 0;
                records[0].completed = 2;
                errors += d_cases_collect(&work) == 0;
                records[0].completed = 1;
                records[0].rows = 0;
                errors += d_cases_collect(&work) == 0;
                records[0].rows = 1;
                String8 first = path_join(arena, work.settings.out, tests[0].name);
                d_write(&work.settings, path_join(arena, first, S8("case.log")), S8("truncated"));
                errors += d_cases_collect(&work) == 0;
                errors += !os_file_delete(path_join(arena, first, S8("result.txt")));
                errors += d_cases_collect(&work) == 0;
                // A real evidence write into a directory fails closed.
                DSettings broken = work.settings;
                d_write(&broken, first, S8("cannot write"));
                errors += !broken.io_failed;
            }
        }
        if (work.settings.report) { errors += fclose(work.settings.report) != 0; }
        if (work.settings.log) { errors += fclose(work.settings.log) != 0; }
        if (work.settings.spawn_mutex) { os_mutex_destroy(work.settings.spawn_mutex); }
        string_print(S8("DIFFERENTIAL_WORKER_CONTROL version=1 requested={u32} effective={u32} cases={u32} peak={u32} started={u32} finished={u32} active={u32} arenas={u32} failure_control={u32} errors={u32}\n"),
            requested[pass], worker_jobs, work.count, work.test_peak, work.test_started, work.test_finished,
            work.test_active, work.test_arenas, (u32)failure_control, errors - pass_errors);
        if (errors != pass_errors)
        {
            string_print(S8("DIFFERENTIAL_SELF_TEST_FAIL workers_pass={u32} failures={u32}\n"), pass, errors - pass_errors);
        }
    }
    return errors;
}

''' + source[end:]
path.write_bytes(source.encode())

doc = Path("docs/differential-testing.md")
raw = doc.read_bytes()
assert hashlib.sha1(b"blob " + str(len(raw)).encode() + b"\0" + raw).hexdigest() == "5d5ad6fda7ab705abe1bd0b240a83e5020fea7a0", "unexpected guide base"
doc.write_bytes(raw + b"\n### Registered admission controls\n\nThe native `--self-test` requests one, two and four workers, followed by the\nexisting failure-injection pass. Each request is clamped by the same host and\n`BUSTER_TEST_JOBS` policy as the corpus; `DIFFERENTIAL_WORKER_CONTROL` records\nthe requested and effective counts separately. Single-threaded and TCC drivers\ntherefore report one effective lane, not an unexecuted parallel pass.\n\nThe controls rendezvous an initial cohort and keep its first successful case\nactive until all other cases finish. This forces unequal case lifetimes without\na wall-time performance assertion, detects a fixed per-lane partition that\nstrands work behind the long case, and still requires registry-order output.\nAn independent thirty-second rendezvous deadline releases a broken control as\na failure; the existing child deadlines are not enlarged. Live-case peaks must\nequal the effective budget, completion ordinals must be unique and exhaustive,\nand all cases and case-owned arenas must be inactive after the lane barrier.\nThese are self-test-only counters, not process RSS or corpus instrumentation.\nExisting crash, timeout, failed-launch, sanitizer, missing/duplicate completion,\ndamaged-stream and evidence-write controls remain registered.\n\nThese controls are not a full-corpus one/two/four-worker timing cohort. Hosted\npolicy remains one worker, and #408's matched full-CI latency, aggregate runner\ntime and concurrent peak-memory acceptance remain separate requirements.\n")
