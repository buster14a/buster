/* Included by tests.c: qualification has no compiler-performance assertions.
 * Exercise the existing CLI/launcher, same-inode exclusion, retained children,
 * snapshot errors and sealing. Linux observations are not hardware acceptance.
 */
#ifdef __linux__
static void test_host_lock_lifetime(char const* executable, char const* path)
{
    TpHostLock lock = {-1}, other = {-1};
    CHECK(tp_host_lock_acquire(path, &lock) == 0);
    CHECK(lock.descriptor >= 3 && !(fcntl(lock.descriptor, F_GETFD) & FD_CLOEXEC));
    int error = tp_host_lock_acquire(path, &other);
    CHECK((error == EWOULDBLOCK || error == EAGAIN) && other.descriptor == -1);
    int channel[2];
    int piped = pipe(channel) == 0;
    CHECK(piped);
    if (piped)
    {
        pid_t pid = fork();
        CHECK(pid >= 0);
        if (pid == 0)
        {
            close(channel[1]);
            char signal_byte;
            int ready = read(channel[0], &signal_byte, 1) == 1;
            close(channel[0]);
            if (ready)
            {
                char* args[] = {(char*)executable, "child", "host-lock-probe", (char*)path, NULL};
                execv(executable, args);
            }
            _exit(2);
        }
        close(channel[0]);
        tp_host_lock_release(&lock);
        if (pid > 0)
        {
            /* Parent closes first. The child's inherited reference, even
             * after exec, must exclude a freshly opened contender. */
            CHECK(write(channel[1], "x", 1) == 1);
            int status = 0;
            CHECK(waitpid(pid, &status, 0) == pid);
            CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
        }
        close(channel[1]);
    }
    tp_host_lock_release(&lock);
    CHECK(tp_host_lock_acquire(path, &lock) == 0);
    tp_host_lock_release(&lock);
    tp_host_lock_release(&lock);

    piped = pipe(channel) == 0;
    CHECK(piped);
    if (piped)
    {
        pid_t pid = fork();
        CHECK(pid >= 0);
        if (pid == 0)
        {
            close(channel[0]);
            int acquired = tp_host_lock_acquire(path, &lock) == 0;
            char signal_byte = acquired ? 'y' : 'n';
            if (write(channel[1], &signal_byte, 1) != 1) _exit(2);
            close(channel[1]);
            if (acquired) test_delay(2000);
            _exit(acquired ? 0 : 2);
        }
        close(channel[1]);
        if (pid > 0)
        {
            char signal_byte = 0;
            CHECK(read(channel[0], &signal_byte, 1) == 1 && signal_byte == 'y');
            error = tp_host_lock_acquire(path, &other);
            CHECK(error == EWOULDBLOCK || error == EAGAIN);
            tp_host_lock_release(&other);
            CHECK(kill(pid, SIGKILL) == 0);
            int status = 0;
            CHECK(waitpid(pid, &status, 0) == pid && WIFSIGNALED(status));
        }
        close(channel[0]);
    }
    CHECK(tp_host_lock_acquire(path, &lock) == 0);
    tp_host_lock_release(&lock);
}

static void test_inherited_host_lock(char const* root, char const* path)
{
    TpHostLock parent = {-1}, harness = {-1}, other = {-1};
    CHECK(tp_host_lock_acquire(path, &parent) == 0);
    int inherited = dup(parent.descriptor);
    CHECK(inherited >= 3);
    if (inherited >= 3)
    {
        CHECK(tp_host_lock_adopt(path, inherited, &harness) == 0);
        CHECK(harness.descriptor == inherited && (fcntl(inherited, F_GETFD) & FD_CLOEXEC));
        CHECK(!(fcntl(parent.descriptor, F_GETFD) & FD_CLOEXEC));
        int error = tp_host_lock_acquire(path, &other);
        CHECK((error == EWOULDBLOCK || error == EAGAIN) && other.descriptor == -1);

        /* Candidate processes use tp_process. Neither the adopted descriptor
         * nor the parent stand-in may appear after that path's exec. */
        int parent_flags = fcntl(parent.descriptor, F_GETFD);
        CHECK(parent_flags >= 0 && fcntl(parent.descriptor, F_SETFD, parent_flags | FD_CLOEXEC) == 0);
        char script[128], log[TP_PATH_CAP];
        snprintf(script, sizeof(script), "test ! -e /proc/self/fd/%d", inherited);
        CHECK(tp_path(log, root, "inherited-lock-child.log"));
        char* command[] = {"/bin/sh", "-c", script, NULL};
        TpProcess child = tp_process(command, NULL, log, 5, -1, 0);
        CHECK(child.exit_code == 0 && !child.launch_error && !child.timed_out);

        tp_host_lock_release(&harness);
        error = tp_host_lock_acquire(path, &other);
        CHECK(error == EWOULDBLOCK || error == EAGAIN);
        tp_host_lock_release(&other);
        struct stat info;
        CHECK(lstat(path, &info) == 0 && S_ISREG(info.st_mode));

        int cpu = tp_first_allowed_cpu();
        char cpu_text[32], fd_text[32], result_dir[TP_PATH_CAP];
        snprintf(cpu_text, sizeof(cpu_text), "%d", cpu);
        CHECK(tp_path(result_dir, root, "inherited-lock-rejected-run"));
        inherited = dup(parent.descriptor);
        snprintf(fd_text, sizeof(fd_text), "%d", inherited);
        char* rejected[] = {"throughput", "run", "--machine-id", "test", "--lock-file", (char*)path,
                            "--cpu", cpu_text, "--output", result_dir, "--lease-fd", fd_text};
        CHECK(inherited >= 3 && throughput_cli_main(12, rejected) == 2);
        CHECK(fcntl(inherited, F_GETFD) < 0 && errno == EBADF);
        error = tp_host_lock_acquire(path, &other);
        CHECK(error == EWOULDBLOCK || error == EAGAIN);
        tp_host_lock_release(&other);
        CHECK(lstat(path, &info) == 0 && S_ISREG(info.st_mode));
    }
    tp_host_lock_release(&harness);
    tp_host_lock_release(&parent);
    CHECK(tp_host_lock_acquire(path, &other) == 0);
    tp_host_lock_release(&other);
}

static void test_inherited_host_lock_rejections(char const* root, char const* path)
{
    TpHostLock owner = {-1}, adopted = {-1};
    CHECK(tp_host_lock_acquire(path, &owner) == 0);

    int descriptor = dup(owner.descriptor);
    CHECK(descriptor >= 3 && close(descriptor) == 0);
    CHECK(tp_host_lock_adopt(path, descriptor, &adopted) == EBADF && adopted.descriptor == -1);

    descriptor = open(root, O_RDONLY | O_DIRECTORY);
    CHECK(descriptor >= 3 && tp_host_lock_adopt(path, descriptor, &adopted) == EINVAL && adopted.descriptor == -1);

    char different[TP_PATH_CAP], symlink_path[TP_PATH_CAP], moved[TP_PATH_CAP];
    CHECK(tp_path(different, root, "qualification-different.lock"));
    CHECK(test_text(root, "qualification-different.lock", "different\n") && chmod(different, 0600) == 0);
    descriptor = open(different, O_RDWR | O_NOFOLLOW | O_NONBLOCK);
    CHECK(descriptor >= 3 && tp_host_lock_adopt(path, descriptor, &adopted) == EINVAL && adopted.descriptor == -1);

    CHECK(chmod(path, 0640) == 0);
    descriptor = dup(owner.descriptor);
    CHECK(descriptor >= 3 && tp_host_lock_adopt(path, descriptor, &adopted) == EACCES && adopted.descriptor == -1);
    CHECK(chmod(path, 0600) == 0);
    char hardlink[TP_PATH_CAP];
    CHECK(tp_path(hardlink, root, "qualification-inherited-hardlink") && link(path, hardlink) == 0);
    descriptor = dup(owner.descriptor);
    CHECK(descriptor >= 3 && tp_host_lock_adopt(path, descriptor, &adopted) == EINVAL && adopted.descriptor == -1);
    CHECK(unlink(hardlink) == 0);

    CHECK(tp_path(symlink_path, root, "qualification-inherited-symlink") && symlink(path, symlink_path) == 0);
    descriptor = dup(owner.descriptor);
    CHECK(descriptor >= 3 && tp_host_lock_adopt(symlink_path, descriptor, &adopted) != 0 && adopted.descriptor == -1);
    CHECK(unlink(symlink_path) == 0);

    /* A separate open description cannot be mistaken for the supervisor's
     * already-held one, even though it names the right inode. */
    descriptor = open(path, O_RDWR | O_NOFOLLOW | O_NONBLOCK);
    int error = descriptor < 0 ? errno : tp_host_lock_adopt(path, descriptor, &adopted);
    CHECK((error == EWOULDBLOCK || error == EAGAIN) && adopted.descriptor == -1);

    /* Replacing the selected pathname cannot redirect an inherited lease. */
    CHECK(tp_path(moved, root, "qualification-original.lock") && rename(path, moved) == 0);
    CHECK(test_text(root, "qualification.lock", "substitute\n") && chmod(path, 0600) == 0);
    descriptor = dup(owner.descriptor);
    CHECK(descriptor >= 3 && tp_host_lock_adopt(path, descriptor, &adopted) == EINVAL && adopted.descriptor == -1);
    CHECK(unlink(path) == 0 && rename(moved, path) == 0);

    tp_host_lock_release(&owner);
    descriptor = open(path, O_RDWR | O_NOFOLLOW | O_NONBLOCK);
    CHECK(descriptor >= 3 && tp_host_lock_adopt(path, descriptor, &adopted) == EINVAL && adopted.descriptor == -1);
    CHECK(unlink(different) == 0);
}

static void test_inherited_host_lock_process_lifetime(char const* path)
{
    TpHostLock service = {-1}, other = {-1};
    CHECK(tp_host_lock_acquire(path, &service) == 0);
    int ready[2], finish[2];
    int channels = pipe(ready) == 0 && pipe(finish) == 0;
    CHECK(channels);
    if (channels)
    {
        pid_t pid = fork();
        CHECK(pid >= 0);
        if (pid == 0)
        {
            close(ready[0]);
            close(finish[1]);
            TpHostLock harness = {-1};
            int adopted = tp_host_lock_adopt(path, service.descriptor, &harness) == 0;
            char byte = adopted ? 'y' : 'n';
            int signaled = write(ready[1], &byte, 1) == 1;
            close(ready[1]);
            int released = read(finish[0], &byte, 1) == 1;
            close(finish[0]);
            tp_host_lock_release(&harness);
            _exit(adopted && signaled && released ? 0 : 3);
        }
        close(ready[1]);
        close(finish[0]);
        if (pid > 0)
        {
            char byte = 0;
            CHECK(read(ready[0], &byte, 1) == 1 && byte == 'y');
            tp_host_lock_release(&service);
            int error = tp_host_lock_acquire(path, &other);
            CHECK(error == EWOULDBLOCK || error == EAGAIN);
            tp_host_lock_release(&other);
            struct stat info;
            CHECK(lstat(path, &info) == 0 && S_ISREG(info.st_mode));
            CHECK(write(finish[1], "x", 1) == 1);
            int status = 0;
            CHECK(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0);
        }
        close(ready[0]);
        close(finish[1]);
    }
    tp_host_lock_release(&service);
    CHECK(tp_host_lock_acquire(path, &other) == 0);
    tp_host_lock_release(&other);
}
#endif

static void test_host_qualification(char const* executable, char const* root)
{
    TpConfig config;
    char* partial[] = {"throughput", "run", "--machine-id", "test"};
    CHECK(!tp_options(4, partial, &config));
    char* missing[] = {"throughput", "qualify"};
    CHECK(!tp_options(2, missing, &config));
    char* relative[] = {"throughput", "qualify", "--machine-id", "test", "--cpu", "0", "--lock-file", "relative.lock"};
    CHECK(!tp_options(8, relative, &config));
    char* wrong_command[] = {"throughput", "compare", "--machine-id", "test", "--cpu", "0", "--lock-file", "/unused.lock"};
    CHECK(!tp_options(8, wrong_command, &config));
    char* unpaired_lease[] = {"throughput", "qualify", "--machine-id", "test", "--cpu", "0", "--lease-fd", "3"};
    CHECK(!tp_options(8, unpaired_lease, &config));
    char* malformed_lease[] = {"throughput", "qualify", "--machine-id", "test", "--cpu", "0", "--lock-file", "/unused.lock", "--lease-fd", "closed"};
    CHECK(!tp_options(10, malformed_lease, &config));
    char* stdio_lease[] = {"throughput", "qualify", "--machine-id", "test", "--cpu", "0", "--lock-file", "/unused.lock", "--lease-fd", "2"};
    CHECK(!tp_options(10, stdio_lease, &config));
    char* paired_lease[] = {"throughput", "qualify", "--machine-id", "test", "--cpu", "0", "--lock-file", "/unused.lock", "--lease-fd", "3"};
    CHECK(tp_options(10, paired_lease, &config) && config.lease_fd_explicit && config.lease_fd == 3);
    char long_label[TP_HOST_LABEL_CAP + 1];
    memset(long_label, 'a', sizeof(long_label) - 1);
    long_label[sizeof(long_label) - 1] = 0;
    char* too_long[] = {"throughput", "qualify", "--machine-id", long_label, "--cpu", "0", "--lock-file", "/unused.lock"};
    CHECK(!tp_options(8, too_long, &config));
    char* ordinary[] = {"throughput", "run", "--cpu", "0"};
    CHECK(tp_options(4, ordinary, &config) && !config.machine_id && !config.host);
    TpHost host;
    TpHostLock lock = {-1};
#ifdef __linux__
    char path[TP_PATH_CAP], log[TP_PATH_CAP], result_dir[TP_PATH_CAP];
    int paths = tp_path(path, root, "qualification.lock") &&
                tp_path(log, root, "qualification.log") &&
                tp_path(result_dir, root, "qualification-rejected-run");
    CHECK(paths);
    if (paths)
    {
        CHECK(test_text(root, "qualification.lock", "persistent lock contents\n"));
        CHECK(chmod(path, 0600) == 0);
        test_host_lock_lifetime(executable, path);
        test_inherited_host_lock(root, path);
        test_inherited_host_lock_rejections(root, path);
        test_inherited_host_lock_process_lifetime(path);
        TpHostFact fact;
        tp_host_read(&fact, path);
        CHECK(!fact.error && !fact.truncated && !strcmp(fact.value, "persistent lock contents\n"));
        CHECK(tp_host_lock_acquire("relative.lock", &lock) == EINVAL);
        CHECK(tp_host_lock_acquire(root, &lock) != 0 && lock.descriptor == -1);
        char absent[TP_PATH_CAP];
        CHECK(tp_path(absent, result_dir, "no-parent.lock"));
        CHECK(tp_host_lock_acquire(absent, &lock) == ENOENT);
        tp_host_read(&fact, absent);
        CHECK(fact.error == ENOENT && !fact.value[0]);

        CHECK(test_text(root, "qualification-empty", ""));
        CHECK(tp_path(absent, root, "qualification-empty"));
        tp_host_read(&fact, absent);
        CHECK(!fact.error && !fact.truncated && !fact.value[0]);
        char large[TP_HOST_VALUE_CAP + 1];
        memset(large, 'x', sizeof(large) - 1); large[sizeof(large) - 1] = 0;
        CHECK(test_text(root, "qualification-large", large));
        CHECK(tp_path(absent, root, "qualification-large"));
        tp_host_read(&fact, absent);
        CHECK(!fact.error && fact.truncated && strlen(fact.value) == TP_HOST_VALUE_CAP - 1);
        large[TP_HOST_VALUE_CAP - 1] = 0;
        CHECK(test_text(root, "qualification-large", large));
        tp_host_read(&fact, absent);
        CHECK(!fact.error && !fact.truncated && strlen(fact.value) == TP_HOST_VALUE_CAP - 1);
        FILE* binary = fopen(absent, "wb");
        CHECK(binary != NULL);
        if (binary)
        {
            CHECK(fwrite("a\0b", 1, 3, binary) == 3);
            CHECK(fclose(binary) == 0);
        }
        tp_host_read(&fact, absent);
        CHECK(fact.error == EILSEQ && !fact.value[0]);
        CHECK(tp_path(absent, root, "qualification-symlink"));
        CHECK(symlink(path, absent) == 0);
        CHECK(tp_host_lock_acquire(absent, &lock) != 0 && lock.descriptor == -1);
        CHECK(unlink(absent) == 0);

        /* A cooperative lease is meaningful only when every participant can
         * identify the same private regular inode. Existing files are not
         * chmoded as a side effect of admission: reject an accidentally
         * shared lock and leave its contents untouched. */
        CHECK(chmod(path, 0640) == 0);
        CHECK(tp_host_lock_acquire(path, &lock) == EACCES && lock.descriptor == -1);
        tp_host_read(&fact, path);
        CHECK(!fact.error && !fact.truncated && !strcmp(fact.value, "persistent lock contents\n"));
        CHECK(chmod(path, 0600) == 0);
        CHECK(tp_host_lock_acquire(path, &lock) == 0);
        tp_host_lock_release(&lock);

        char hardlink[TP_PATH_CAP];
        CHECK(tp_path(hardlink, root, "qualification-hardlink") && link(path, hardlink) == 0);
        CHECK(tp_host_lock_acquire(path, &lock) == EINVAL && lock.descriptor == -1);
        CHECK(unlink(hardlink) == 0);

        int cpu = tp_first_allowed_cpu();
        CHECK(cpu >= 0);
        CHECK(tp_host_capture(&host, -1, "test", path) == EINVAL);
        CHECK(tp_host_capture(&host, CPU_SETSIZE, "test", path) == EINVAL);
        CHECK(tp_host_capture(&host, cpu, "test", path) == 0);
        char selected[128];
        snprintf(selected, sizeof(selected), "/cpu%d/cpufreq/scaling_governor", cpu);
        CHECK(strstr(host.facts[16].path, selected) != NULL);
        cpu_set_t original, narrowed;
        CHECK(sched_getaffinity(0, sizeof(original), &original) == 0);
        CPU_ZERO(&narrowed); CPU_SET(cpu, &narrowed);
        int pinned = sched_setaffinity(0, sizeof(narrowed), &narrowed) == 0;
        CHECK(pinned);
        if (pinned)
        {
            CHECK(tp_host_capture(&host, cpu == 0 ? 1 : 0, "test", path) == EINVAL);
            CHECK(sched_setaffinity(0, sizeof(original), &original) == 0);
        }
        CHECK(tp_host_lock_acquire(path, &lock) == 0);
        char cpu_text[32]; snprintf(cpu_text, sizeof(cpu_text), "%d", cpu);
        char* rejected[] = {"throughput", "run", "--machine-id", "test", "--lock-file", path,
                            "--cpu", cpu_text, "--output", result_dir};
        CHECK(throughput_cli_main(10, rejected) == 2);
        struct stat info;
        CHECK(stat(result_dir, &info) != 0 && errno == ENOENT);
        tp_host_lock_release(&lock);
        /* Missing compiler arguments fail normally after admission; the lease
         * must be released, and no result directory can be created. */
        CHECK(throughput_cli_main(10, rejected) == 2);
        CHECK(stat(result_dir, &info) != 0 && errno == ENOENT);
        CHECK(tp_host_lock_acquire(path, &lock) == 0);
        tp_host_lock_release(&lock);
        char* qualify[] = {(char*)executable, "child", "throughput", "qualify", "--machine-id", "test\"\\label",
                           "--lock-file", path, "--cpu", cpu_text, NULL};
        TpProcess process = tp_process(qualify, NULL, log, 5, -1, 0);
        CHECK(process.exit_code == 0 && !process.launch_error && !process.timed_out);
        process = tp_process(qualify, NULL, "/dev/full", 5, -1, 0);
        CHECK(process.exit_code == 2 && !process.launch_error && !process.timed_out);
        CHECK(tp_host_lock_acquire(path, &lock) == 0);
        tp_host_lock_release(&lock);

        /* Machine facts are in the already sealed metadata, not an unsealed
         * sidecar. The timings below remain explicitly synthetic fixtures. */
        CHECK(tp_path(result_dir, root, "qualification-seal"));
        CHECK(test_bundle(result_dir, 0));
        TpJob job = {0};
        strcpy(job.workload.name, "fixture");
        memset(job.workload.hash, '0', 64);
        job.mode = 2;
        CHECK(tp_host_capture(&host, cpu, "fixture-not-hardware-acceptance", path) == 0);
        config.host = &host;
        config.cpu = cpu;
        CHECK(tp_metadata(&config, result_dir, executable, executable, &job, 1));
        CHECK(tp_completion(result_dir, 1, 20, 1, 1));
        CHECK(tp_compare(result_dir) == 0);
        CHECK(tp_path(absent, result_dir, "metadata.json"));
        FILE* metadata = fopen(absent, "ab");
        CHECK(metadata != NULL);
        if (metadata) { CHECK(fputc(' ', metadata) != EOF); CHECK(fclose(metadata) == 0); }
        CHECK(tp_compare(result_dir) == 2);
        test_summaries(result_dir, 0);
    }
#else
    (void)executable;
    CHECK(tp_host_lock_acquire(root, &lock) == ENOSYS && lock.descriptor == -1);
    CHECK(tp_host_lock_adopt(root, 3, &lock) == ENOSYS && lock.descriptor == -1);
    CHECK(tp_host_capture(&host, 0, "test", root) == ENOSYS);
    tp_host_lock_release(&lock);
#endif
}
