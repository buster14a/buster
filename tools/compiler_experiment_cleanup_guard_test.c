// Hosted-only diagnostics for the durable native cleanup guard. Include after
// cleanup guard, supervisor and summary fixture helpers. No physical path
// selector exists; every mutation below stays inside one owned fixture root.
#ifndef BUSTER_COMPILER_EXPERIMENT_CLEANUP_GUARD_TEST_INCLUDED
#define BUSTER_COMPILER_EXPERIMENT_CLEANUP_GUARD_TEST_INCLUDED

#if BUSTER_LINUX && !BUSTER_ANDROID
typedef struct CompilerExperimentCleanupGuardTestPaths CompilerExperimentCleanupGuardTestPaths;
struct CompilerExperimentCleanupGuardTestPaths { String8 root, unknown, active; };

typedef struct CompilerExperimentCleanupGuardTestEnvironment CompilerExperimentCleanupGuardTestEnvironment;
struct CompilerExperimentCleanupGuardTestEnvironment { char const* name; char8* prior; bool present; };

BUSTER_GLOBAL_LOCAL bool compiler_experiment_cleanup_guard_test_allowed(Arena* arena)
{
    bool physical = true;
    char8 cpu[262145]; u64 cpu_length = 0;
    bool allowed = compiler_experiment_cleanup_physical(&physical) && !physical &&
        compiler_experiment_cleanup_read("/proc/cpuinfo", cpu, sizeof(cpu), &cpu_length) && cpu_length &&
        !string_contains((String8){cpu, cpu_length}, S8("9700X")) &&
        !string_contains((String8){cpu, cpu_length}, S8("9700x"));
    extern char** environ;
    for (char** entry = environ; allowed && entry && *entry; entry += 1)
    {
        char const* value = *entry;
        if (value[0] == 'B' && value[1] == 'Q' && value[2] == '_')
            allowed = strcmp(value, "BQ_REQUIRE_DISTINCT_GROUP=1") == 0;
    }
    BUSTER_UNUSED(arena);
    return allowed;
}

BUSTER_GLOBAL_LOCAL bool compiler_experiment_cleanup_guard_test_environment(Arena* arena,
    CompilerExperimentCleanupGuardTestEnvironment* environment, u64 count, bool install)
{
    char const* names[] = {"BQ_REQUEST_RUN_ID", "GITHUB_RUN_ID", "BQ_RUN_ID", "GITHUB_RUN_ATTEMPT",
        "BQ_HEAD_COMMIT", "GITHUB_REPOSITORY", "GITHUB_JOB", "GITHUB_SHA"};
    char const* values[] = {"70001", "70002", "70002", "1", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
        "buster14a/buster", "utility", "cccccccccccccccccccccccccccccccccccccccc"};
    bool result = count == BUSTER_ARRAY_LENGTH(names);
    if (install)
    {
        for (u64 i = 0; i < count; i += 1)
        {
            environment[i].name = names[i];
            char const* prior = getenv(names[i]);
            environment[i].present = prior != 0;
            u64 length = prior ? (u64)strlen(prior) : 0;
            environment[i].prior = arena_allocate(arena, char8, length + 1);
            if (length) memcpy(environment[i].prior, prior, length);
            environment[i].prior[length] = 0;
        }
        for (u64 i = 0; i < count; i += 1)
            result = setenv(names[i], values[i], 1) == 0 && result;
    }
    else
    {
        // Restore every key even when an earlier restoration fails.
        for (u64 i = 0; i < count; i += 1)
        {
            bool restored = environment[i].present ?
                setenv(environment[i].name, (char const*)environment[i].prior, 1) == 0 :
                unsetenv(environment[i].name) == 0;
            result = restored && result;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL CompilerExperimentCleanupGuardTestPaths compiler_experiment_cleanup_guard_test_paths(
    Arena* arena, String8 root, String8 name)
{
    CompilerExperimentCleanupGuardTestPaths paths = {0};
    paths.root = path_join(arena, root, name);
    OsDirectoryCreateResult made = os_make_directory_exclusive(paths.root);
    if (made.created && !made.error.v)
    {
        paths.unknown = string_format_z(arena, S8("{S8}/unknown"), paths.root);
        paths.active = string_format_z(arena, S8("{S8}/active"), paths.root);
    }
    return paths;
}

BUSTER_GLOBAL_LOCAL bool compiler_experiment_cleanup_guard_test_guard(Arena* arena,
    CompilerExperimentCleanupGuardTestPaths paths)
{
    return paths.unknown.length && paths.active.length &&
        compiler_experiment_cleanup_guard_at(arena, (char const*)paths.unknown.pointer, (char const*)paths.active.pointer);
}

BUSTER_GLOBAL_LOCAL bool compiler_experiment_cleanup_guard_test_begin(Arena* arena,
    CompilerExperimentCleanupGuardTestPaths paths, CompilerExperimentCleanupLease* lease)
{
    return paths.unknown.length && paths.active.length &&
        compiler_experiment_cleanup_begin_at(arena, (char const*)paths.unknown.pointer, (char const*)paths.active.pointer, lease);
}

BUSTER_GLOBAL_LOCAL bool compiler_experiment_cleanup_guard_test_finish(Arena* arena,
    CompilerExperimentCleanupGuardTestPaths paths, CompilerExperimentCleanupLease* lease, bool quiet)
{
    return paths.unknown.length && paths.active.length &&
        compiler_experiment_cleanup_finish_at(arena, (char const*)paths.unknown.pointer,
            (char const*)paths.active.pointer, lease, quiet);
}

BUSTER_GLOBAL_LOCAL bool compiler_experiment_cleanup_guard_test_read(int descriptor, u8* bytes, u64 count, u64 deadline)
{
    u64 used = 0;
    bool valid = descriptor >= 0;
    while (valid && used < count && os_now_microseconds() < deadline)
    {
        struct pollfd event = {.fd = descriptor, .events = POLLIN};
        int ready = poll(&event, 1, 20);
        if (ready < 0) valid = errno == EINTR;
        else if (ready > 0)
        {
            ssize_t got = read(descriptor, bytes + used, (size_t)(count - used));
            if (got > 0) used += (u64)got;
            else valid = got < 0 && errno == EINTR;
        }
    }
    return valid && used == count;
}

// Reap only a PID returned by this fixture's own fork. Exit status is checked;
// a timeout forces that exact PID to stop and is always a failed test.
BUSTER_GLOBAL_LOCAL bool compiler_experiment_cleanup_guard_test_wait(pid_t child, u64 deadline,
    bool killed_expected)
{
    bool valid = child > 1 && child != getpid(), reaped = false, matched = false;
    while (valid && !reaped && os_now_microseconds() < deadline)
    {
        int status = 0;
        pid_t observed = waitpid(child, &status, WNOHANG);
        if (observed == child)
        {
            reaped = true;
            matched = killed_expected ? (WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL) :
                (WIFEXITED(status) && WEXITSTATUS(status) == 0);
        }
        else if (observed < 0) valid = errno == EINTR;
        if (valid && !reaped) poll(0, 0, 1);
    }
    if (valid && !reaped)
    {
        kill(child, SIGKILL);
        u64 cleanup_deadline = os_now_microseconds() + 5000000ull;
        while (!reaped && os_now_microseconds() < cleanup_deadline)
        {
            int status = 0;
            pid_t observed = waitpid(child, &status, WNOHANG);
            reaped = observed == child;
            if (observed < 0 && errno != EINTR) break;
            if (!reaped) poll(0, 0, 1);
        }
    }
    return valid && reaped && matched;
}

BUSTER_GLOBAL_LOCAL bool compiler_experiment_cleanup_guard_test_serial(Arena* arena, String8 root)
{
    CompilerExperimentCleanupGuardTestPaths paths = compiler_experiment_cleanup_guard_test_paths(arena, root, S8("serial"));
    CompilerExperimentCleanupLease first = {0}, nested = {0}, second = {0};
    bool valid = compiler_experiment_cleanup_guard_test_guard(arena, paths) &&
        compiler_experiment_cleanup_guard_test_begin(arena, paths, &first) && first.enabled && first.owned &&
        first.owner_pid == (u64)getpid() && !compiler_experiment_cleanup_guard_test_begin(arena, paths, &first);
    String8 observed = {0};
    valid = valid && compiler_experiment_cleanup_record_read(arena, (char const*)paths.active.pointer, &observed) &&
        string_equal(observed, first.record) && compiler_experiment_cleanup_guard_test_begin(arena, paths, &nested) &&
        nested.enabled && !nested.owned && string_equal(nested.record, first.record) &&
        compiler_experiment_cleanup_guard_test_finish(arena, paths, &nested, true) &&
        !compiler_experiment_cleanup_missing((char const*)paths.active.pointer) &&
        compiler_experiment_cleanup_guard_test_finish(arena, paths, &first, true) &&
        compiler_experiment_cleanup_missing((char const*)paths.active.pointer) &&
        compiler_experiment_cleanup_missing((char const*)paths.unknown.pointer) &&
        compiler_experiment_cleanup_guard_test_begin(arena, paths, &second) && second.owned &&
        compiler_experiment_cleanup_guard_test_finish(arena, paths, &second, true);
    return valid && compiler_experiment_cleanup_guard_test_guard(arena, paths);
}

BUSTER_GLOBAL_LOCAL bool compiler_experiment_cleanup_guard_test_nested(Arena* arena, String8 root, bool foreign_release)
{
    CompilerExperimentCleanupGuardTestPaths paths = compiler_experiment_cleanup_guard_test_paths(arena, root,
        foreign_release ? S8("foreign-release") : S8("nested-ancestor"));
    CompilerExperimentCleanupLease owner = {0};
    bool valid = compiler_experiment_cleanup_guard_test_begin(arena, paths, &owner) && owner.owned;
    int report[2] = {-1, -1};
    valid = pipe2(report, O_CLOEXEC) == 0 && valid;
    CompilerExperimentSupervisor supervisor = {0};
    bool began = valid && compiler_experiment_supervisor_begin(arena, &supervisor);
    pid_t child = began ? fork() : -1;
    if (!child)
    {
        close(report[0]);
        CompilerExperimentCleanupLease nested = {0};
        bool permitted = compiler_experiment_cleanup_guard_test_guard(arena, paths);
        bool tested = permitted;
        if (foreign_release)
            tested = permitted && !compiler_experiment_cleanup_guard_test_finish(arena, paths, &owner, true) &&
                !compiler_experiment_cleanup_missing((char const*)paths.active.pointer) &&
                !compiler_experiment_cleanup_missing((char const*)paths.unknown.pointer);
        else
            tested = permitted && compiler_experiment_cleanup_guard_test_begin(arena, paths, &nested) &&
                nested.enabled && !nested.owned && string_equal(nested.record, owner.record) &&
                compiler_experiment_cleanup_guard_test_finish(arena, paths, &nested, true) &&
                !compiler_experiment_cleanup_missing((char const*)paths.active.pointer);
        u8 answer = tested ? 1 : 0;
        bool sent = write(report[1], &answer, 1) == 1;
        close(report[1]);
        _exit(tested && sent ? 0 : 7);
    }
    if (report[1] >= 0) { close(report[1]); report[1] = -1; }
    u8 answer = 0;
    bool reported = child > 1 && compiler_experiment_cleanup_guard_test_read(report[0], &answer, 1,
        os_now_microseconds() + 10000000ull) && answer == 1;
    bool reaped = child > 1 && compiler_experiment_cleanup_guard_test_wait(child, os_now_microseconds() + 5000000ull, false);
    bool quiet = began && compiler_experiment_supervisor_end(arena, &supervisor) && !supervisor.signalled && !supervisor.reaped;
    if (report[0] >= 0) close(report[0]);
    valid = valid && reported && reaped && quiet;
    if (foreign_release)
        valid = valid && !compiler_experiment_cleanup_guard_test_finish(arena, paths, &owner, true) &&
            !compiler_experiment_cleanup_missing((char const*)paths.active.pointer) &&
            !compiler_experiment_cleanup_missing((char const*)paths.unknown.pointer) &&
            !compiler_experiment_cleanup_guard_test_guard(arena, paths);
    else
        valid = valid && compiler_experiment_cleanup_guard_test_guard(arena, paths) &&
            compiler_experiment_cleanup_guard_test_finish(arena, paths, &owner, true) &&
            compiler_experiment_cleanup_missing((char const*)paths.active.pointer);
    return valid;
}

BUSTER_GLOBAL_LOCAL bool compiler_experiment_cleanup_guard_test_mutation(Arena* arena,
    CompilerExperimentCleanupGuardTestPaths paths, CompilerExperimentCleanupLease owner, u64 field, String8 replacement)
{
    String8 values[11] = {0};
    bool valid = field < BUSTER_ARRAY_LENGTH(values) && compiler_experiment_cleanup_fields(owner.record, values);
    if (valid) values[field] = replacement;
    String8 changed = valid ? string_format(arena,
        S8("schema\t{S8}\nowner_pid\t{S8}\nowner_start_ticks\t{S8}\nboot_id\t{S8}\n"
           "request_run_id\t{S8}\nexecutor_run_id\t{S8}\nexecutor_attempt\t{S8}\nrequest_head\t{S8}\n"
           "repository\t{S8}\njob\t{S8}\npolicy_revision\t{S8}\n"),
        values[0], values[1], values[2], values[3], values[4], values[5], values[6], values[7], values[8], values[9], values[10]) : (String8){0};
    String8 record = path_join(arena, paths.active, S8("owner.tsv"));
    bool rejected = valid && file_write(record, BUSTER_SLICE_TO_BYTE_SLICE(changed)) &&
        !compiler_experiment_cleanup_guard_test_guard(arena, paths);
    bool restored = file_write(record, BUSTER_SLICE_TO_BYTE_SLICE(owner.record)) &&
        compiler_experiment_cleanup_guard_test_guard(arena, paths);
    return rejected && restored;
}

BUSTER_GLOBAL_LOCAL bool compiler_experiment_cleanup_guard_test_identity(Arena* arena, String8 root)
{
    CompilerExperimentCleanupGuardTestPaths paths = compiler_experiment_cleanup_guard_test_paths(arena, root, S8("identity"));
    CompilerExperimentCleanupLease owner = {0};
    bool valid = compiler_experiment_cleanup_guard_test_begin(arena, paths, &owner) && owner.owned;
    u64 parent = 0, started = 0;
    valid = valid && compiler_experiment_cleanup_process(arena, (u64)getpid(), &parent, &started);
    String8 changed_start = string_format(arena, S8("{u64}"), started + 1);
    String8 fields[11] = {0};
    valid = valid && compiler_experiment_cleanup_fields(owner.record, fields);
    String8 changed_boot = valid ? string_duplicate_arena(arena, fields[3], true) : (String8){0};
    if (changed_boot.length) changed_boot.pointer[0] = changed_boot.pointer[0] == 'a' ? 'b' : 'a';
    bool pid = valid && compiler_experiment_cleanup_guard_test_mutation(arena, paths, owner, 1, S8("1"));
    bool start = valid && compiler_experiment_cleanup_guard_test_mutation(arena, paths, owner, 2, changed_start);
    bool boot = valid && compiler_experiment_cleanup_guard_test_mutation(arena, paths, owner, 3, changed_boot);
    char const* names[] = {"BQ_REQUEST_RUN_ID", "GITHUB_RUN_ID", "BQ_RUN_ID", "GITHUB_RUN_ATTEMPT",
        "BQ_HEAD_COMMIT", "GITHUB_REPOSITORY", "GITHUB_JOB", "GITHUB_SHA"};
    char const* ordinary[] = {"70001", "70002", "70002", "1", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
        "buster14a/buster", "utility", "cccccccccccccccccccccccccccccccccccccccc"};
    char const* changed[] = {"70003", "70004", "70004", "2", "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
        "other/repository", "sampling", "dddddddddddddddddddddddddddddddddddddddd"};
    bool context = true;
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(names); i += 1)
    {
        bool rejected = setenv(names[i], changed[i], 1) == 0 &&
            !compiler_experiment_cleanup_guard_test_guard(arena, paths);
        bool restored = setenv(names[i], ordinary[i], 1) == 0 &&
            compiler_experiment_cleanup_guard_test_guard(arena, paths);
        context = rejected && restored && context;
    }
    bool finished = compiler_experiment_cleanup_guard_test_finish(arena, paths, &owner, true);
    return valid && pid && start && boot && context && finished;
}

BUSTER_GLOBAL_LOCAL bool compiler_experiment_cleanup_guard_test_unknown(Arena* arena, String8 root)
{
    CompilerExperimentCleanupGuardTestPaths latch = compiler_experiment_cleanup_guard_test_paths(arena, root, S8("unknown"));
    bool unknown = compiler_experiment_cleanup_latch_at(arena, (char const*)latch.unknown.pointer, S8("diagnostic-retained")) &&
        !compiler_experiment_cleanup_guard_test_guard(arena, latch);
    CompilerExperimentCleanupLease rejected = {0};
    unknown = !compiler_experiment_cleanup_guard_test_begin(arena, latch, &rejected) && unknown;
    CompilerExperimentCleanupGuardTestPaths links[2] = {
        compiler_experiment_cleanup_guard_test_paths(arena, root, S8("dangling-unknown")),
        compiler_experiment_cleanup_guard_test_paths(arena, root, S8("dangling-active"))};
    bool dangling = true;
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(links); i += 1)
    {
        String8 absent = string_format_z(arena, S8("{S8}/absent"), links[i].root);
        String8 link = i ? links[i].active : links[i].unknown;
        bool made = symlink((char const*)absent.pointer, (char const*)link.pointer) == 0;
        CompilerExperimentCleanupLease lease = {0};
        dangling = made && !compiler_experiment_cleanup_guard_test_guard(arena, links[i]) &&
            !compiler_experiment_cleanup_guard_test_begin(arena, links[i], &lease) && dangling;
    }
    CompilerExperimentCleanupGuardTestPaths irregular[2] = {
        compiler_experiment_cleanup_guard_test_paths(arena, root, S8("fifo-record")),
        compiler_experiment_cleanup_guard_test_paths(arena, root, S8("directory-record"))};
    bool nonregular = true;
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(irregular); i += 1)
    {
        String8 owner_file = string_format_z(arena, S8("{S8}/owner.tsv"), irregular[i].active);
        bool active = mkdir((char const*)irregular[i].active.pointer, 0700) == 0;
        bool made = i ? mkdir((char const*)owner_file.pointer, 0600) == 0 :
            mkfifo((char const*)owner_file.pointer, 0600) == 0;
        CompilerExperimentCleanupLease lease = {0};
        nonregular = active && made && !compiler_experiment_cleanup_guard_test_guard(arena, irregular[i]) &&
            !compiler_experiment_cleanup_guard_test_begin(arena, irregular[i], &lease) &&
            !compiler_experiment_cleanup_missing((char const*)irregular[i].active.pointer) && nonregular;
    }
    CompilerExperimentCleanupGuardTestPaths failed = compiler_experiment_cleanup_guard_test_paths(arena, root, S8("failed-terminal"));
    CompilerExperimentCleanupLease owner = {0};
    bool retained = compiler_experiment_cleanup_guard_test_begin(arena, failed, &owner) && owner.owned &&
        !compiler_experiment_cleanup_guard_test_finish(arena, failed, &owner, false) &&
        !compiler_experiment_cleanup_missing((char const*)failed.active.pointer) &&
        !compiler_experiment_cleanup_missing((char const*)failed.unknown.pointer) &&
        !compiler_experiment_cleanup_guard_test_finish(arena, failed, &owner, true) &&
        !compiler_experiment_cleanup_missing((char const*)failed.active.pointer) &&
        !compiler_experiment_cleanup_missing((char const*)failed.unknown.pointer) &&
        !compiler_experiment_cleanup_guard_test_guard(arena, failed);
    CompilerExperimentCleanupGuardTestPaths changed = compiler_experiment_cleanup_guard_test_paths(arena, root, S8("changed-borrowed-context"));
    CompilerExperimentCleanupLease outer = {0}, borrowed = {0};
    bool borrowed_claim = compiler_experiment_cleanup_guard_test_begin(arena, changed, &outer) && outer.owned &&
        compiler_experiment_cleanup_guard_test_begin(arena, changed, &borrowed) && borrowed.enabled && !borrowed.owned;
    bool changed_context = setenv("GITHUB_JOB", "sampling", 1) == 0;
    bool borrowed_denied = borrowed_claim && changed_context &&
        !compiler_experiment_cleanup_guard_test_guard(arena, changed) &&
        !compiler_experiment_cleanup_guard_test_finish(arena, changed, &borrowed, true) &&
        !compiler_experiment_cleanup_missing((char const*)changed.active.pointer) &&
        !compiler_experiment_cleanup_missing((char const*)changed.unknown.pointer);
    bool context_restored = setenv("GITHUB_JOB", "utility", 1) == 0;
    bool still_retained = borrowed_denied && context_restored &&
        !compiler_experiment_cleanup_guard_test_finish(arena, changed, &outer, true) &&
        !compiler_experiment_cleanup_missing((char const*)changed.active.pointer) &&
        !compiler_experiment_cleanup_missing((char const*)changed.unknown.pointer);
    return unknown && dangling && nonregular && retained && still_retained;
}

BUSTER_GLOBAL_LOCAL bool compiler_experiment_cleanup_guard_test_killed(Arena* arena, String8 root)
{
    CompilerExperimentCleanupGuardTestPaths paths = compiler_experiment_cleanup_guard_test_paths(arena, root, S8("killed-active"));
    int report[2] = {-1, -1};
    bool valid = paths.active.length && pipe2(report, O_CLOEXEC) == 0;
    CompilerExperimentSupervisor supervisor = {0};
    bool began = valid && compiler_experiment_supervisor_begin(arena, &supervisor);
    pid_t child = began ? fork() : -1;
    if (!child)
    {
        close(report[0]);
        CompilerExperimentCleanupLease lease = {0};
        bool claimed = compiler_experiment_cleanup_guard_test_begin(arena, paths, &lease) && lease.owned &&
            compiler_experiment_cleanup_guard_test_guard(arena, paths);
        u8 answer = claimed ? 1 : 0;
        bool sent = write(report[1], &answer, 1) == 1;
        close(report[1]);
        if (!claimed || !sent) _exit(7);
        for (;;) poll(0, 0, 20);
    }
    if (report[1] >= 0) { close(report[1]); report[1] = -1; }
    u8 answer = 0;
    bool reported = child > 1 && compiler_experiment_cleanup_guard_test_read(report[0], &answer, 1,
        os_now_microseconds() + 10000000ull) && answer == 1;
    bool killed = child > 1 && kill(child, SIGKILL) == 0;
    bool reaped = child > 1 && compiler_experiment_cleanup_guard_test_wait(child, os_now_microseconds()+5000000ull, killed);
    bool quiet = began && compiler_experiment_supervisor_end(arena, &supervisor) && !supervisor.signalled && !supervisor.reaped;
    if (report[0] >= 0) close(report[0]);
    CompilerExperimentCleanupLease retry = {0};
    return valid && reported && killed && reaped && quiet &&
        !compiler_experiment_cleanup_missing((char const*)paths.active.pointer) &&
        !compiler_experiment_cleanup_guard_test_guard(arena, paths) &&
        !compiler_experiment_cleanup_guard_test_begin(arena, paths, &retry) &&
        !compiler_experiment_cleanup_missing((char const*)paths.active.pointer);
}

BUSTER_GLOBAL_LOCAL bool compiler_experiment_cleanup_guard_test_concurrent(Arena* arena, String8 root)
{
    CompilerExperimentCleanupGuardTestPaths paths = compiler_experiment_cleanup_guard_test_paths(arena, root, S8("concurrent"));
    int report[2] = {-1,-1}, start[2] = {-1,-1}, release[2] = {-1,-1};
    bool valid = paths.active.length && pipe2(report, O_CLOEXEC) == 0 &&
        pipe2(start, O_CLOEXEC) == 0 && pipe2(release, O_CLOEXEC) == 0;
    CompilerExperimentSupervisor supervisor = {0};
    bool began = valid && compiler_experiment_supervisor_begin(arena, &supervisor);
    pid_t children[2] = {-1,-1};
    for (u64 i = 0; began && i < BUSTER_ARRAY_LENGTH(children); i += 1)
    {
        children[i] = fork();
        if (!children[i])
        {
            close(report[0]); close(start[1]); close(release[1]);
            u8 go = 0;
            bool released = compiler_experiment_cleanup_guard_test_read(start[0], &go, 1,
                os_now_microseconds()+10000000ull) && go == 1;
            CompilerExperimentCleanupLease lease = {0};
            bool claimed = released && compiler_experiment_cleanup_guard_test_begin(arena, paths, &lease);
            bool state = released && (claimed ? lease.owned && compiler_experiment_cleanup_guard_test_guard(arena, paths) : !lease.owned);
            u8 claim_report[2] = {(u8)i, claimed ? 1 : 0};
            bool sent = write(report[1], claim_report, sizeof(claim_report)) == (ssize_t)sizeof(claim_report);
            go = 0;
            bool finish = compiler_experiment_cleanup_guard_test_read(release[0], &go, 1,
                os_now_microseconds()+10000000ull) && go == 1;
            bool terminal = state && sent && finish;
            if (claimed) terminal = compiler_experiment_cleanup_guard_test_finish(arena, paths, &lease, true) && terminal;
            u8 terminal_report[2] = {(u8)i, terminal ? 1 : 0};
            bool delivered = write(report[1], terminal_report, sizeof(terminal_report)) == (ssize_t)sizeof(terminal_report);
            close(report[1]); close(start[0]); close(release[0]);
            _exit(terminal && delivered ? 0 : 7);
        }
        if (children[i] < 1) valid = false;
    }
    if (report[1] >= 0) { close(report[1]); report[1] = -1; }
    if (start[0] >= 0) { close(start[0]); start[0] = -1; }
    if (release[0] >= 0) { close(release[0]); release[0] = -1; }
    u8 gates[2] = {1,1}, claims[4] = {0}, terminals[4] = {0};
    bool opened = valid && children[0] > 1 && children[1] > 1 &&
        write(start[1], gates, sizeof(gates)) == (ssize_t)sizeof(gates);
    bool reported = opened && compiler_experiment_cleanup_guard_test_read(report[0], claims, sizeof(claims),
        os_now_microseconds()+10000000ull) && claims[0] < 2 && claims[2] < 2 && claims[0] != claims[2] &&
        claims[1] <= 1 && claims[3] <= 1 && claims[1] + claims[3] == 1;
    String8 record = {0}; String8 fields[11] = {0};
    u64 expected_owner = reported ? (u64)children[claims[1] ? claims[0] : claims[2]] : 0;
    u64 recorded_owner = 0;
    bool identified = reported && compiler_experiment_cleanup_record_read(arena, (char const*)paths.active.pointer, &record) &&
        compiler_experiment_cleanup_fields(record, fields) && compiler_experiment_cleanup_decimal(fields[1], &recorded_owner) &&
        recorded_owner == expected_owner && !compiler_experiment_cleanup_guard_test_guard(arena, paths);
    bool continued = opened && write(release[1], gates, sizeof(gates)) == (ssize_t)sizeof(gates);
    bool finished = continued && compiler_experiment_cleanup_guard_test_read(report[0], terminals, sizeof(terminals),
        os_now_microseconds()+10000000ull) && terminals[0] < 2 && terminals[2] < 2 &&
        terminals[0] != terminals[2] && terminals[1] == 1 && terminals[3] == 1;
    bool reaped = true;
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(children); i += 1)
    {
        bool one = children[i] > 1 && compiler_experiment_cleanup_guard_test_wait(children[i], os_now_microseconds()+5000000ull, false);
        reaped = one && reaped;
    }
    bool quiet = began && compiler_experiment_supervisor_end(arena, &supervisor) && !supervisor.signalled && !supervisor.reaped;
    for (u64 i = 0; i < 2; i += 1)
    {
        if (report[i] >= 0) close(report[i]);
        if (start[i] >= 0) close(start[i]);
        if (release[i] >= 0) close(release[i]);
    }
    return valid && reported && identified && finished && reaped && quiet &&
        compiler_experiment_cleanup_missing((char const*)paths.active.pointer) &&
        compiler_experiment_cleanup_missing((char const*)paths.unknown.pointer) &&
        compiler_experiment_cleanup_guard_test_guard(arena, paths);
}

// The irregular-record control intentionally creates an empty 0600 directory.
// A generic iterative deleter cannot traverse its ".." for ascent. After every
// control has proved its owned children quiet, remove only this exact private
// empty object; the ordinary link-safe deleter handles the remaining tree.
BUSTER_GLOBAL_LOCAL bool compiler_experiment_cleanup_guard_test_remove_irregular(Arena* arena, String8 root)
{
    // summary_self_test_claim_directory returns an owned relative build path.
    // Resolve that spelling lexically, then require its actual canonical match.
    String8 absolute = os_path_absolute_lexical(arena, root, true);
    String8 parent = path_join(arena, absolute, S8("directory-record/active"));
    String8 record = string_format_z(arena, S8("{S8}/owner.tsv"), parent);
    struct stat status = {0};
    bool canonical = absolute.length && string_equal(absolute, os_path_absolute(arena, root, true)) &&
        string_equal(parent, os_path_absolute(arena, parent, true));
    bool identified = canonical && lstat((char const*)record.pointer, &status) == 0 && S_ISDIR(status.st_mode) &&
        status.st_uid == geteuid() && (status.st_mode & 0777) == 0600;
    bool result = identified && rmdir((char const*)record.pointer) == 0;
    u64 remove_error = identified && !result ? (u64)errno : 0;
    string_print(S8("COMPILER_EXPERIMENT_CLEANUP_GUARD_PRIVATE_RECORD canonical={u64} "
        "owned_irregular_directory={u64} removed={u64} remove_errno={u64}\n"),
        (u64)canonical, (u64)identified, (u64)result, remove_error);
    return result;
}
// These extra cases run in a newly exec'd native image, so the once-only outer
// adoption claim is tested with the same PID/start/boot as its publishing child.
BUSTER_GLOBAL_LOCAL bool compiler_experiment_cleanup_guard_test_preentry_publish(Arena* arena,
    CompilerExperimentCleanupGuardTestPaths paths)
{
    String8 values[11] = {0};
    u64 parent = 0, started = 0, parent_parent = 0, parent_started = 0;
    char8 boot[64]; u64 boot_length = 0;
    bool valid = paths.active.length && compiler_experiment_cleanup_context(values) &&
        compiler_experiment_cleanup_process(arena, (u64)getpid(), &parent, &started) &&
        parent == (u64)getppid() &&
        compiler_experiment_cleanup_process(arena, parent, &parent_parent, &parent_started) &&
        compiler_experiment_cleanup_boot(boot, &boot_length);
    String8 record = valid ? string_format(arena,
        S8("schema\tbuster-9700x-preentry-active-v1\nowner_pid\t{u64}\nowner_start_ticks\t{u64}\nboot_id\t{S8}\n"
           "request_run_id\t{S8}\nexecutor_run_id\t{S8}\nexecutor_attempt\t{S8}\nrequest_head\t{S8}\n"
           "repository\t{S8}\njob\t{S8}\npolicy_revision\t{S8}\n"),
        (u64)getpid(), started, ((String8){boot, boot_length}), values[4], values[5], values[6],
        values[7], values[8], values[9], values[10]) : (String8){0};
    String8 marker = valid ? string_format(arena,
        S8("schema\tbuster-cleanup-guard-preentry-exec-v1\nparent_pid\t{u64}\nparent_start_ticks\t{u64}\nboot_id\t{S8}\n"),
        parent, parent_started, ((String8){boot, boot_length})) : (String8){0};
    String8 owner_file = string_format_z(arena, S8("{S8}/owner.tsv"), paths.active);
    String8 marker_file = string_format_z(arena, S8("{S8}/diagnostic-exec.tsv"), paths.root);
    valid = valid && mkdir((char const*)paths.active.pointer, 0700) == 0 &&
        compiler_experiment_cleanup_write_file((char const*)owner_file.pointer, record) &&
        compiler_experiment_cleanup_write_file((char const*)marker_file.pointer, marker);
    int descriptor = valid ? open((char const*)paths.active.pointer,
        O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK) : -1;
    valid = valid && descriptor >= 0 && fsync(descriptor) == 0;
    if (descriptor >= 0) valid = close(descriptor) == 0 && valid;
    return valid;
}

BUSTER_GLOBAL_LOCAL bool compiler_experiment_cleanup_guard_test_preentry_root(Arena* arena, String8 root,
    CompilerExperimentCleanupGuardTestPaths* paths, String8* selected)
{
    bool bounded = root.pointer && root.length && root.length <= 4096;
    String8 absolute = bounded ? os_path_absolute_lexical(arena, root, true) : (String8){0};
    String8 build = os_path_absolute(arena, S8("build"), true);
    u64 parent = (u64)getppid(), parent_parent = 0, parent_started = 0;
    char8 boot[64]; u64 boot_length = 0;
    String8 prefix = string_format(arena, S8("{S8}/cleanup-guard-self-test-{u64}-"), build, parent);
    bool valid = bounded && paths && selected && absolute.length && string_equal(root, absolute) &&
        string_equal(root, os_path_absolute(arena, root, true)) &&
        root.length > prefix.length && memcmp(root.pointer, prefix.pointer, (size_t)prefix.length) == 0;
    u64 at = valid ? prefix.length : 0, timestamp_at = at;
    while (valid && at < root.length && root.pointer[at] >= '0' && root.pointer[at] <= '9') at += 1;
    u64 timestamp = 0;
    valid = valid && at < root.length && root.pointer[at] == '-' &&
        compiler_experiment_cleanup_decimal((String8){root.pointer + timestamp_at, at - timestamp_at}, &timestamp);
    u64 attempt_at = ++at;
    while (valid && at < root.length && root.pointer[at] >= '0' && root.pointer[at] <= '9') at += 1;
    u64 attempt = 0;
    String8 attempt_text = valid ? (String8){root.pointer + attempt_at, at - attempt_at} : (String8){0};
    valid = valid && at < root.length && root.pointer[at] == '/' &&
        (string_equal(attempt_text, S8("0")) || compiler_experiment_cleanup_decimal(attempt_text, &attempt)) && attempt < 32;
    String8 name = valid ? (String8){root.pointer + at + 1, root.length - at - 1} : (String8){0};
    String8 names[] = {S8("preentry-exec"), S8("preentry-replaced-file"), S8("preentry-replaced-directory"),
        S8("preentry-failed"), S8("preentry-killed"), S8("preentry-zombie")};
    bool known = false;
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(names); i += 1) known = known || string_equal(name, names[i]);
    struct stat directory = {0}, file = {0};
    String8 marker_path = string_format_z(arena, S8("{S8}/diagnostic-exec.tsv"), root);
    char8 marker_bytes[512]; u64 marker_length = 0;
    valid = valid && known && compiler_experiment_cleanup_process(arena, parent, &parent_parent, &parent_started) &&
        compiler_experiment_cleanup_boot(boot, &boot_length) &&
        lstat((char const*)root.pointer, &directory) == 0 && S_ISDIR(directory.st_mode) && directory.st_uid == getuid() &&
        lstat((char const*)marker_path.pointer, &file) == 0 && S_ISREG(file.st_mode) &&
        (file.st_mode & 0777) == 0600 && file.st_uid == getuid() &&
        compiler_experiment_cleanup_read((char const*)marker_path.pointer, marker_bytes, sizeof(marker_bytes), &marker_length);
    String8 marker = valid ? string_format(arena,
        S8("schema\tbuster-cleanup-guard-preentry-exec-v1\nparent_pid\t{u64}\nparent_start_ticks\t{u64}\nboot_id\t{S8}\n"),
        parent, parent_started, ((String8){boot, boot_length})) : (String8){0};
    valid = valid && string_equal(marker, (String8){marker_bytes, marker_length});
    if (valid)
    {
        paths->root = root;
        paths->unknown = string_format_z(arena, S8("{S8}/unknown"), root);
        paths->active = string_format_z(arena, S8("{S8}/active"), root);
        *selected = name;
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool compiler_experiment_cleanup_guard_test_preentry_descendant(Arena* arena,
    CompilerExperimentCleanupGuardTestPaths paths, String8 record)
{
    int report[2] = {-1, -1};
    bool valid = pipe2(report, O_CLOEXEC) == 0;
    CompilerExperimentSupervisor supervisor = {0};
    bool began = valid && compiler_experiment_supervisor_begin(arena, &supervisor);
    pid_t child = began ? fork() : -1;
    if (!child)
    {
        close(report[0]);
        CompilerExperimentCleanupLease borrowed = {0}, forbidden = {0};
        bool tested = compiler_experiment_cleanup_begin_at(arena, (char const*)paths.unknown.pointer,
                (char const*)paths.active.pointer, &borrowed) && borrowed.enabled && !borrowed.owned &&
            string_equal(borrowed.record, record) &&
            !compiler_experiment_cleanup_adopt_preentry_at(arena, (char const*)paths.unknown.pointer,
                (char const*)paths.active.pointer, &forbidden) && !forbidden.enabled &&
            compiler_experiment_cleanup_finish_at(arena, (char const*)paths.unknown.pointer,
                (char const*)paths.active.pointer, &borrowed, true) &&
            !compiler_experiment_cleanup_missing((char const*)paths.active.pointer);
        u8 answer = tested ? 1 : 0;
        bool sent = write(report[1], &answer, 1) == 1;
        close(report[1]);
        _exit(tested && sent ? 0 : 7);
    }
    if (report[1] >= 0) close(report[1]);
    u8 answer = 0;
    bool reported = child > 1 && compiler_experiment_cleanup_guard_test_read(report[0], &answer, 1,
        os_now_microseconds() + 5000000ull) && answer == 1;
    bool reaped = child > 1 && compiler_experiment_cleanup_guard_test_wait(child,
        os_now_microseconds() + 5000000ull, false);
    bool quiet = began && compiler_experiment_supervisor_end(arena, &supervisor) &&
        !supervisor.signalled && !supervisor.reaped;
    if (report[0] >= 0) close(report[0]);
    return valid && reported && reaped && quiet;
}

BUSTER_GLOBAL_LOCAL bool compiler_experiment_cleanup_guard_test_preentry_zombie(Arena* arena,
    CompilerExperimentCleanupGuardTestPaths paths, CompilerExperimentCleanupLease* lease)
{
    pid_t child = fork();
    if (!child) _exit(23);
    bool observed_exit = false;
    u64 deadline = os_now_microseconds() + 5000000ull;
    while (child > 1 && !observed_exit && os_now_microseconds() < deadline)
    {
        siginfo_t observed = {0};
        int result = waitid(P_PID, (id_t)child, &observed, WEXITED | WNOHANG | WNOWAIT);
        observed_exit = result == 0 && observed.si_pid == child && observed.si_code == CLD_EXITED && observed.si_status == 23;
        if (!observed_exit) poll(0, 0, 1);
    }
    bool refused = observed_exit && !compiler_experiment_cleanup_finish_at(arena,
        (char const*)paths.unknown.pointer, (char const*)paths.active.pointer, lease, true);
    int status = 0;
    pid_t reaped = child > 1 ? waitpid(child, &status, WNOHANG) : -1;
    bool preserved = reaped == child && WIFEXITED(status) && WEXITSTATUS(status) == 23;
    if (child > 1 && reaped != child)
    {
        kill(child, SIGKILL);
        compiler_experiment_cleanup_guard_test_wait(child, os_now_microseconds() + 5000000ull, true);
    }
    bool valid = refused && preserved && !compiler_experiment_cleanup_missing((char const*)paths.active.pointer) &&
        !compiler_experiment_cleanup_missing((char const*)paths.unknown.pointer);
    string_print(S8("COMPILER_CLEANUP_PREENTRY_RESERVED_STATUS observed={u64} release_refused={u64} status_preserved={u64}\n"),
        (u64)observed_exit, (u64)refused, (u64)preserved);
    return valid;
}

BUSTER_GLOBAL_LOCAL bool compiler_experiment_cleanup_guard_preentry_test_worker_inner(Arena* arena, String8 root)
{
    // The publishing child clears only its synthetic BQ keys before exec.
    // Real public authority is refused before this worker installs test context.
    bool admitted = compiler_experiment_cleanup_guard_test_allowed(arena);
    CompilerExperimentCleanupGuardTestPaths paths = {0};
    String8 selected = {0};
    bool identified = admitted && compiler_experiment_cleanup_guard_test_preentry_root(arena, root, &paths, &selected);
    CompilerExperimentCleanupGuardTestEnvironment environment[8] = {0};
    bool installed = identified && compiler_experiment_cleanup_guard_test_environment(arena, environment,
        BUSTER_ARRAY_LENGTH(environment), true);
    CompilerExperimentCleanupLease lease = {0};
    bool valid = installed && compiler_experiment_cleanup_guard_test_guard(arena, paths);
    String8 before = {0}, fields[11] = {0};
    valid = valid && compiler_experiment_cleanup_record_read(arena, (char const*)paths.active.pointer, &before) &&
        compiler_experiment_cleanup_fields(before, fields) &&
        string_equal(fields[0], S8("buster-9700x-preentry-active-v1")) &&
        compiler_experiment_cleanup_record_matches(arena, before, true);
    bool borrowed = false, duplicate = false, descendant = false, native_schema_refused = false;
    if (valid)
    {
        u64 header_end = 0;
        while (header_end < before.length && before.pointer[header_end] != '\n') header_end += 1;
        String8 native = header_end < before.length ? string_format(arena,
            S8("schema\tbuster-9700x-native-active-v1\n{S8}"),
            string_slice(before, header_end + 1, before.length)) : (String8){0};
        String8 owner_file = path_join(arena, paths.active, S8("owner.tsv"));
        CompilerExperimentCleanupLease forbidden = {0};
        native_schema_refused = native.length && file_write(owner_file, BUSTER_SLICE_TO_BYTE_SLICE(native)) &&
            !compiler_experiment_cleanup_adopt_preentry_at(arena, (char const*)paths.unknown.pointer,
                (char const*)paths.active.pointer, &forbidden) && !forbidden.enabled;
        bool restored_record = file_write(owner_file, BUSTER_SLICE_TO_BYTE_SLICE(before)) &&
            compiler_experiment_cleanup_guard_test_guard(arena, paths);
        valid = native_schema_refused && restored_record;
    }
    if (valid)
    {
        CompilerExperimentCleanupLease same_pid = {0};
        borrowed = compiler_experiment_cleanup_begin_at(arena, (char const*)paths.unknown.pointer,
                (char const*)paths.active.pointer, &same_pid) && same_pid.enabled && !same_pid.owned &&
            compiler_experiment_cleanup_finish_at(arena, (char const*)paths.unknown.pointer,
                (char const*)paths.active.pointer, &same_pid, true) &&
            !compiler_experiment_cleanup_missing((char const*)paths.active.pointer);
        valid = borrowed && compiler_experiment_cleanup_adopt_preentry_at(arena,
            (char const*)paths.unknown.pointer, (char const*)paths.active.pointer, &lease) &&
            lease.enabled && lease.owned && lease.preentry_adopted && lease.owner_pid == (u64)getpid() &&
            string_equal(before, lease.record);
        CompilerExperimentCleanupLease repeated = {0};
        duplicate = valid && !compiler_experiment_cleanup_adopt_preentry_at(arena,
            (char const*)paths.unknown.pointer, (char const*)paths.active.pointer, &repeated) && !repeated.enabled;
        valid = valid && duplicate;
    }
    if (valid && string_equal(selected, S8("preentry-exec")))
    {
        descendant = compiler_experiment_cleanup_guard_test_preentry_descendant(arena, paths, before);
        valid = descendant && compiler_experiment_cleanup_finish_at(arena, (char const*)paths.unknown.pointer,
            (char const*)paths.active.pointer, &lease, true) &&
            compiler_experiment_cleanup_missing((char const*)paths.active.pointer) &&
            compiler_experiment_cleanup_missing((char const*)paths.unknown.pointer);
    }
    else if (valid && (string_equal(selected, S8("preentry-replaced-file")) ||
        string_equal(selected, S8("preentry-replaced-directory"))))
    {
        String8 owner_file = string_format_z(arena, S8("{S8}/owner.tsv"), paths.active);
        if (string_equal(selected, S8("preentry-replaced-directory")))
        {
            String8 retained = string_format_z(arena, S8("{S8}/retained-original-active"), paths.root);
            valid = rename((char const*)paths.active.pointer, (char const*)retained.pointer) == 0 &&
                mkdir((char const*)paths.active.pointer, 0700) == 0;
        }
        else
        {
            // Retaining the old inode guarantees the identical replacement has
            // a distinct identity, independent of timestamp resolution/reuse.
            String8 retained = string_format_z(arena, S8("{S8}/retained-original-owner.tsv"), paths.root);
            valid = rename((char const*)owner_file.pointer, (char const*)retained.pointer) == 0;
        }
        valid = valid && compiler_experiment_cleanup_write_file((char const*)owner_file.pointer, before) &&
            !compiler_experiment_cleanup_finish_at(arena, (char const*)paths.unknown.pointer,
                (char const*)paths.active.pointer, &lease, true) &&
            !compiler_experiment_cleanup_missing((char const*)paths.active.pointer) &&
            !compiler_experiment_cleanup_missing((char const*)paths.unknown.pointer);
        String8 retained_record = {0};
        valid = valid && compiler_experiment_cleanup_record_read(arena, (char const*)paths.active.pointer, &retained_record) &&
            string_equal(retained_record, before);
    }
    else if (valid && string_equal(selected, S8("preentry-failed")))
    {
        valid = !compiler_experiment_cleanup_finish_at(arena, (char const*)paths.unknown.pointer,
                (char const*)paths.active.pointer, &lease, false) &&
            !compiler_experiment_cleanup_finish_at(arena, (char const*)paths.unknown.pointer,
                (char const*)paths.active.pointer, &lease, true) &&
            !compiler_experiment_cleanup_missing((char const*)paths.active.pointer) &&
            !compiler_experiment_cleanup_missing((char const*)paths.unknown.pointer);
    }
    else if (valid && string_equal(selected, S8("preentry-zombie")))
        valid = compiler_experiment_cleanup_guard_test_preentry_zombie(arena, paths, &lease);
    else if (valid && string_equal(selected, S8("preentry-killed")))
    {
        String8 ready_path = string_format_z(arena, S8("{S8}/ready.tsv"), paths.root);
        String8 ready = string_format(arena, S8("schema\tbuster-cleanup-preentry-kill-ready-v1\nowner_pid\t{u64}\n"), (u64)getpid());
        valid = compiler_experiment_cleanup_write_file((char const*)ready_path.pointer, ready);
        if (valid) for (;;) poll(0, 0, 20);
    }
    if (lease.preentry_adopted && lease.preentry_identity.directory_descriptor >= 0)
    {
        valid = close(lease.preentry_identity.directory_descriptor) == 0 && valid;
        lease.preentry_identity.directory_descriptor = -1;
    }
    bool restored = installed && compiler_experiment_cleanup_guard_test_environment(arena, environment,
        BUSTER_ARRAY_LENGTH(environment), false);
    string_print(S8("COMPILER_CLEANUP_PREENTRY_EXEC case={S8} identified={u64} same_pid_borrowed={u64} "
        "native_schema_refused={u64} duplicate_refused={u64} descendant_borrowed={u64} "
        "restored={u64} success={u64} physical_qualification=false\n"),
        selected, (u64)identified, (u64)borrowed, (u64)native_schema_refused,
        (u64)duplicate, (u64)descendant, (u64)restored, (u64)valid);
    return valid && restored;
}

BUSTER_GLOBAL_LOCAL bool compiler_experiment_cleanup_guard_test_preentry_case(Arena* arena, String8 root, String8 name)
{
    CompilerExperimentCleanupGuardTestPaths paths = compiler_experiment_cleanup_guard_test_paths(arena, root, name);
    bool claimed = paths.active.length && paths.unknown.length;
    paths.root = claimed ? os_path_absolute(arena, paths.root, true) : (String8){0};
    paths.active = claimed ? string_format_z(arena, S8("{S8}/active"), paths.root) : (String8){0};
    paths.unknown = claimed ? string_format_z(arena, S8("{S8}/unknown"), paths.root) : (String8){0};
    bool valid = claimed && paths.root.length && paths.active.length && paths.unknown.length;
    CompilerExperimentSupervisor supervisor = {0};
    bool began = valid && compiler_experiment_supervisor_begin(arena, &supervisor);
    pid_t child = began ? fork() : -1;
    if (!child)
    {
        bool published = compiler_experiment_cleanup_guard_test_preentry_publish(arena, paths);
        bool cleared = unsetenv("BQ_REQUEST_RUN_ID") == 0 && unsetenv("BQ_RUN_ID") == 0 && unsetenv("BQ_HEAD_COMMIT") == 0;
        if (published && cleared)
        {
            char const* command[] = {"/proc/self/exe", "compiler_profile_qualification",
                "--owned-cleanup-guard-preentry-fixture-worker", (char const*)paths.root.pointer, 0};
            execv(command[0], (char* const*)command);
        }
        _exit(7);
    }
    bool killed_case = string_equal(name, S8("preentry-killed"));
    bool ready = !killed_case;
    if (child > 1 && killed_case)
    {
        String8 ready_path = string_format_z(arena, S8("{S8}/ready.tsv"), paths.root);
        String8 expected = string_format(arena, S8("schema\tbuster-cleanup-preentry-kill-ready-v1\nowner_pid\t{u64}\n"), (u64)child);
        u64 deadline = os_now_microseconds() + 10000000ull;
        while (!ready && os_now_microseconds() < deadline)
        {
            char8 bytes[256]; u64 length = 0;
            ready = compiler_experiment_cleanup_read((char const*)ready_path.pointer, bytes, sizeof(bytes), &length) &&
                string_equal((String8){bytes, length}, expected);
            if (!ready) poll(0, 0, 1);
        }
    }
    bool killed = child > 1 && killed_case && ready && kill(child, SIGKILL) == 0;
    bool reaped = child > 1 && compiler_experiment_cleanup_guard_test_wait(child,
        os_now_microseconds() + 10000000ull, killed_case && killed);
    bool quiet = began && compiler_experiment_supervisor_end(arena, &supervisor) &&
        !supervisor.signalled && !supervisor.reaped;
    valid = valid && child > 1 && ready && reaped && quiet && (!killed_case || killed);
    if (valid && killed_case)
    {
        CompilerExperimentCleanupLease retry = {0};
        valid = !compiler_experiment_cleanup_guard_test_guard(arena, paths) &&
            !compiler_experiment_cleanup_adopt_preentry_at(arena, (char const*)paths.unknown.pointer,
                (char const*)paths.active.pointer, &retry) && !retry.enabled &&
            !compiler_experiment_cleanup_missing((char const*)paths.active.pointer);
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool compiler_experiment_cleanup_guard_test_preentry(Arena* arena, String8 root)
{
    String8 names[] = {S8("preentry-exec"), S8("preentry-replaced-file"), S8("preentry-replaced-directory"),
        S8("preentry-failed"), S8("preentry-killed"), S8("preentry-zombie")};
    bool results[6] = {0};
    bool valid = true;
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(names); i += 1)
    {
        results[i] = compiler_experiment_cleanup_guard_test_preentry_case(arena, root, names[i]);
        valid = results[i] && valid;
    }
    string_print(S8("COMPILER_CLEANUP_PREENTRY_SELF_TEST actual_exec_same_pid={u64} replaced_file_refused={u64} "
        "replaced_directory_refused={u64} failed_owner_retained={u64} killed_owner_retained={u64} "
        "reserved_child_status_preserved={u64} physical_qualification=false\n"),
        (u64)results[0], (u64)results[1], (u64)results[2], (u64)results[3], (u64)results[4], (u64)results[5]);
    return valid;
}

#endif


BUSTER_GLOBAL_LOCAL ProcessResult compiler_experiment_cleanup_guard_preentry_test_worker(Arena* arena,
    SliceString8 arguments)
{
    ProcessResult result = PROCESS_RESULT_FAILED;
#if BUSTER_LINUX && !BUSTER_ANDROID
    if (arguments.length == 2 && string_equal(arguments.pointer[0], S8("--owned-cleanup-guard-preentry-fixture-worker")) &&
        compiler_experiment_cleanup_guard_preentry_test_worker_inner(arena, arguments.pointer[1]))
        result = PROCESS_RESULT_SUCCESS;
#else
    BUSTER_UNUSED(arena); BUSTER_UNUSED(arguments);
#endif
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_experiment_cleanup_guard_self_test(Arena* arena)
{
    bool result = false;
#if BUSTER_LINUX && !BUSTER_ANDROID
    bool admitted = compiler_experiment_cleanup_guard_test_allowed(arena);
    if (!admitted)
        string_print(S8("COMPILER_EXPERIMENT_CLEANUP_GUARD_SELF_TEST refused=physical-host-or-authority-environment "
            "physical_qualification=false\n"));
    String8 root = {0};
    bool claimed = admitted && summary_self_test_claim_directory(arena, S8("cleanup-guard"), &root);
    CompilerExperimentCleanupGuardTestEnvironment environment[8] = {0};
    bool installed = claimed && compiler_experiment_cleanup_guard_test_environment(arena, environment,
        BUSTER_ARRAY_LENGTH(environment), true);
    if (installed)
    {
        // Each case runs even after a failed case, using distinct private paths.
        bool serial = compiler_experiment_cleanup_guard_test_serial(arena, root);
        bool concurrent = compiler_experiment_cleanup_guard_test_concurrent(arena, root);
        bool nested = compiler_experiment_cleanup_guard_test_nested(arena, root, false);
        bool foreign = compiler_experiment_cleanup_guard_test_nested(arena, root, true);
        bool identity = compiler_experiment_cleanup_guard_test_identity(arena, root);
        bool unknown = compiler_experiment_cleanup_guard_test_unknown(arena, root);
        bool killed = compiler_experiment_cleanup_guard_test_killed(arena, root);
        bool preentry = compiler_experiment_cleanup_guard_test_preentry(arena, root);
        result = serial && concurrent && nested && foreign && identity && unknown && killed && preentry;
        string_print(S8("COMPILER_EXPERIMENT_CLEANUP_GUARD_SELF_TEST serial={u64} concurrent_once={u64} "
            "nested_ancestor={u64} foreign_release_denied={u64} identity_mutations_denied={u64} "
            "unknown_symlink_failed_terminal_retained={u64} killed_active_retained={u64} "
            "physical_qualification=false\n"),
            (u64)serial, (u64)concurrent, (u64)nested, (u64)foreign, (u64)identity, (u64)unknown, (u64)killed);
    }
    bool controls_passed = result;
    bool restored = claimed && compiler_experiment_cleanup_guard_test_environment(arena, environment,
        BUSTER_ARRAY_LENGTH(environment), false);
    // A failed ownership or reap proof retains the whole diagnostic root.
    // Production UNKNOWN/ACTIVE have no deletion or retry path here.
    bool irregular_removed = controls_passed && restored &&
        compiler_experiment_cleanup_guard_test_remove_irregular(arena, root);
    bool removed = irregular_removed && os_directory_delete(root);
    result = controls_passed && restored && irregular_removed && removed;
    if (claimed)
        string_print(S8("COMPILER_EXPERIMENT_CLEANUP_GUARD_SELF_TEST_TERMINAL environment_restored={u64} "
            "controls_quiet={u64} irregular_private_record_removed={u64} private_root_removed={u64} "
            "private_root_retained={u64} physical_qualification=false\n"),
            (u64)restored, (u64)controls_passed, (u64)irregular_removed, (u64)removed, (u64)!removed);
#else
    BUSTER_UNUSED(arena);
#endif
    return result;
}
#endif
