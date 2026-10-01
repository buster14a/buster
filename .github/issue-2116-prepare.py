"""One-shot source materializer for #2116; never part of the repair PR."""
from pathlib import Path
import subprocess

EXPECTED = {
    'tools/throughput/tests.c': '95dc53ba95f7c3e681bb3e818d74ef04ad9da6be',
    'tools/throughput/README.md': 'f285bf527e18e06cd9ceb81a6cc6256183bf87e7',
}
for name, expected in EXPECTED.items():
    actual = subprocess.check_output(['git', 'hash-object', name], text=True).strip()
    if actual != expected:
        raise SystemExit(f'base blob changed: {name}: {actual}')

path = Path('tools/throughput/tests.c')
text = path.read_text()
old = '''    else if (!strcmp(argv[2], "summary-write-failure") && argc == 4)
    {
        /* Restrict only this child so buffered report writes fail at close. */
        struct rlimit limit;
        int ok = getrlimit(RLIMIT_FSIZE, &limit) == 0;
        if (ok)
        {
            limit.rlim_cur = 1;
            ok = signal(SIGXFSZ, SIG_IGN) != SIG_ERR && setrlimit(RLIMIT_FSIZE, &limit) == 0;
        }
        result = ok && tp_compare(argv[3]) == 2 ? 0 : 1;
    }'''
new = '''    else if (argc == 4 && (!strcmp(argv[2], "summary-write-failure") ||
                           !strcmp(argv[2], "summary-write-setup-failure")))
    {
        /* The native one-byte limit also truncates the redirected child log.
         * Save each outcome, restore the limit after comparison closes its
         * streams, then publish diagnostics. Setup failure is not a write test. */
        int resource = !strcmp(argv[2], "summary-write-failure") ? RLIMIT_FSIZE : -1;
        struct rlimit saved;
        char const* stage = "getrlimit";
        int ok = getrlimit(resource, &saved) == 0;
        int setup_error = ok ? 0 : errno;
        int comparison = -1, comparison_error = 0, restore_status = -1, restore_error = 0;
        if (ok)
        {
            stage = "signal";
            ok = signal(SIGXFSZ, SIG_IGN) != SIG_ERR;
            if (!ok) setup_error = errno;
        }
        if (ok)
        {
            struct rlimit limit = saved;
            limit.rlim_cur = 1;
            stage = "setrlimit";
            ok = setrlimit(resource, &limit) == 0;
            if (!ok) setup_error = errno;
        }
        if (ok)
        {
            stage = "compare";
            comparison = tp_compare(argv[3]);
            comparison_error = errno;
            restore_status = setrlimit(resource, &saved);
            if (restore_status != 0) restore_error = errno;
        }
        /* A failed diagnostic write under the injected limit sets stream
         * error flags. Clear only the log streams, never the report streams. */
        clearerr(stdout);
        clearerr(stderr);
        int reported = fprintf(stderr, "\\nSUMMARY_WRITE_FAILURE stage=%s setup_errno=%d compare=%d restore=%d restore_errno=%d last_errno=%d\\n",
                               stage, setup_error, comparison, restore_status, restore_error, comparison_error) >= 0;
        if (fflush(stderr) != 0) reported = 0;
        result = ok && comparison == 2 && restore_status == 0 && reported ? 0 : 1;
    }'''
if text.count(old) != 1:
    raise SystemExit('child source does not match')
text = text.replace(old, new)
old = '''static void test_summary_write_failure(char const* executable, char const* root)
{
    char directory[TP_PATH_CAP], log[TP_PATH_CAP];
    int paths_ok = tp_path(directory, root, "summary-write-failure") &&
                   tp_path(log, root, "summary-write-failure.log");
    CHECK(paths_ok);
    if (paths_ok)
    {
        CHECK(test_bundle(directory, 0));
        char* command[] = {(char*)executable, "child", "summary-write-failure", directory, NULL};
        TpProcess child = tp_process(command, NULL, log, 3, -1, 0);
        CHECK(child.exit_code == 0 && !child.timed_out && !child.launch_error);
        test_summaries(directory, 0);
        CHECK(tp_completion(directory, 1, 20, 1, 0));
    }
}'''
new = '''static void test_summary_write_failure(char const* executable, char const* root)
{
    char directory[TP_PATH_CAP], log[TP_PATH_CAP];
    int paths_ok = tp_path(directory, root, "summary-write-failure");
    CHECK(paths_ok);
    if (paths_ok)
    {
        CHECK(test_bundle(directory, 0));
        CHECK(tp_compare(directory) == 0);
        test_summaries(directory, 1);
        char const* modes[] = {"summary-write-failure", "summary-write-setup-failure"};
        char const* logs[] = {"summary-write-failure.log", "summary-write-setup-failure.log"};
        for (unsigned mode = 0; mode < 2; ++mode)
        {
            int log_ok = tp_path(log, root, logs[mode]);
            CHECK(log_ok);
            if (log_ok)
            {
                char* command[] = {(char*)executable, "child", (char*)modes[mode], directory, NULL};
                TpProcess child = tp_process(command, NULL, log, 3, -1, 0);
                CHECK_CHILD_EXIT(child, mode ? 1 : 0, log);
                /* Require a complete diagnostic after the one-byte write
                 * limit, including for the deliberately invalid setup. */
                FILE* file = fopen(log, "rb");
                CHECK(file != NULL);
                if (file)
                {
                    char diagnostic[1024], expected[256];
                    size_t length = fread(diagnostic, 1, sizeof(diagnostic) - 1, file);
                    diagnostic[length] = 0;
                    CHECK(!ferror(file) && feof(file));
                    CHECK(fclose(file) == 0);
                    snprintf(expected, sizeof(expected),
                             "SUMMARY_WRITE_FAILURE stage=%s setup_errno=%d compare=%d restore=%d restore_errno=0",
                             mode ? "getrlimit" : "compare", mode ? EINVAL : 0, mode ? -1 : 2, mode ? -1 : 0);
                    CHECK(strstr(diagnostic, expected) != NULL);
                }
            }
            /* A real write failure removes reports; a setup failure never
             * enters comparison and must leave the existing reports alone. */
            test_summaries(directory, mode != 0);
            CHECK(tp_completion(directory, 1, 20, 1, 0));
            CHECK(tp_compare(directory) == 0);
            test_summaries(directory, 1);
        }
    }
}'''
if text.count(old) != 1:
    raise SystemExit('parent source does not match')
path.write_text(text.replace(old, new))

path = Path('tools/throughput/README.md')
text = path.read_text()
old = '''test root, which the harness artifacts upload.
'''
new = '''test root, which the harness artifacts upload. The desktop matrix also retains
these parent diagnostics and the child-log tail in `combinations.log`.

The POSIX summary-write fixture keeps its real one-byte `RLIMIT_FSIZE` failure
and three-second child deadline. It restores the saved limit only after
comparison has closed the reports, then emits `SUMMARY_WRITE_FAILURE` with the
setup stage/errno, comparison result and restoration status. Otherwise the
injected limit truncates the child log itself to one byte. The parent requires
the complete diagnostic, absence of partial reports, unchanged sealed evidence,
and successful normal report regeneration. A separate invalid-resource control
must report setup failure without entering comparison or deleting good reports.
This diagnostic coverage does not classify an unreproduced child crash, launch
failure or timeout as a file-size-limit defect, and it never retries the child.
'''
if text.count(old) != 1:
    raise SystemExit('README source does not match')
path.write_text(text.replace(old, new))
