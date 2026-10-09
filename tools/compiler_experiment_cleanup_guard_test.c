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
#endif

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
        result = serial && concurrent && nested && foreign && identity && unknown && killed;
        string_print(S8("COMPILER_EXPERIMENT_CLEANUP_GUARD_SELF_TEST serial={u64} concurrent_once={u64} "
            "nested_ancestor={u64} foreign_release_denied={u64} identity_mutations_denied={u64} "
            "unknown_symlink_failed_terminal_retained={u64} killed_active_retained={u64} "
            "physical_qualification=false\n"),
            (u64)serial, (u64)concurrent, (u64)nested, (u64)foreign, (u64)identity, (u64)unknown, (u64)killed);
    }
    bool restored = claimed && compiler_experiment_cleanup_guard_test_environment(arena, environment,
        BUSTER_ARRAY_LENGTH(environment), false);
    result = result && restored;
    if (claimed && restored)
    {
        // Diagnostic cleanup is restricted to the fixture directory. Production
        // UNKNOWN/ACTIVE have no deletion or retry path in this test or helper.
        bool removed = os_directory_delete(root);
        result = removed && result;
    }
#else
    BUSTER_UNUSED(arena);
#endif
    return result;
}
#endif
