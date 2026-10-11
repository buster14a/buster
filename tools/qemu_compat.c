/* Opt-in #3185 QEMU host compatibility census, included by build.c.
 * qemu_git_verify / qemu_inputs_verify own immutable input admission.
 * qemu_self_test owns refusal controls; qemu_compat_main drives unmodified
 * configure/Ninja and records independent reference/candidate stage outcomes.
 * This is a blocker census, not guest or performance qualification.
 */
#define BUSTER_QEMU_COMMIT "b2995afed254e4804611df52210067a4d5dbdf0b"
#define BUSTER_QEMU_KEYMAP_COMMIT "f5772a62ec52591ff6870b7e8ef32482371f22c6"
#define BUSTER_QEMU_SOFTFLOAT_COMMIT "b64af41c3276f97f0e181920400ee056b9c88037"
#define BUSTER_QEMU_TESTFLOAT_COMMIT "e7af9751d9f9fd3b47911f51a5cfd08af256a9ab"

BUSTER_GLOBAL_LOCAL bool qemu_status_allowed(String8 text, String8 expected)
{
    bool root = string_equal(expected, S8(BUSTER_QEMU_COMMIT));
    bool generated = string_equal(expected, S8(BUSTER_QEMU_SOFTFLOAT_COMMIT)) || string_equal(expected, S8(BUSTER_QEMU_TESTFLOAT_COMMIT));
    bool valid = true;
    for (u64 offset = 0; valid && offset < text.length;)
    {
        u64 end = offset;
        while (end < text.length && text.pointer[end] != '\n') { end += 1; }
        String8 line = string_slice(text, offset, end);
        valid = (root && (string_equal(line, S8("!! subprojects/keycodemapdb/")) ||
                          string_equal(line, S8("!! subprojects/berkeley-softfloat-3/")) ||
                          string_equal(line, S8("!! subprojects/berkeley-testfloat-3/")) || string_equal(line, S8("?? subprojects/.wraplock")))) ||
                (generated && (string_equal(line, S8("?? meson.build")) || string_equal(line, S8("?? meson_options.txt"))));
        offset = end + 1;
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool qemu_git_verify(Arena* arena, String8 git, String8 source, String8 expected, bool* stopped)
{
    String8 root_args[] = {git, S8("--no-replace-objects"), S8("-c"), S8("core.fsmonitor=false"), S8("rev-parse"), S8("--show-toplevel")};
    RaddebuggerCommandResult root = raddebugger_command(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(root_args), source, (String8){0}, RADDEBUGGER_ENVIRONMENT_GIT, stopped);
    String8 actual_root = os_path_absolute(arena, compat_trim_ascii_space(root.output), true);
    bool valid = raddebugger_command_ok(root) && actual_root.length && string_equal(actual_root, source);
    if (valid)
    {
        String8 head_args[] = {git, S8("--no-replace-objects"), S8("rev-parse"), S8("HEAD")};
        RaddebuggerCommandResult head = raddebugger_command(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(head_args), source, (String8){0}, RADDEBUGGER_ENVIRONMENT_GIT, stopped);
        valid = raddebugger_command_ok(head) && string_equal(compat_trim_ascii_space(head.output), expected);
    }
    if (valid)
    {
        String8 status_args[] = {git, S8("--no-replace-objects"), S8("-c"), S8("core.fsmonitor=false"), S8("status"), S8("--porcelain"), S8("--untracked-files=normal"), S8("--ignored=matching"), S8("--ignore-submodules=none")};
        RaddebuggerCommandResult status = raddebugger_command(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(status_args), source, (String8){0}, RADDEBUGGER_ENVIRONMENT_GIT, stopped);
        valid = raddebugger_command_ok(status) && qemu_status_allowed(status.output, expected);
        if (!valid) { string_print(S8("QEMU_SOURCE_STATUS path={S8}\n{S8}"), source, status.output); }
    }
    if (valid)
    {
        String8 immutable[] = {git, S8("--no-replace-objects"), S8("-c"), S8("core.fsmonitor=false"),
                               S8("diff-index"), S8("--cached"), S8("--quiet"), S8("--no-ext-diff"), S8("HEAD"), S8("--")};
        valid = raddebugger_command_ok(raddebugger_command(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(immutable), source, (String8){0}, RADDEBUGGER_ENVIRONMENT_GIT, stopped));
    }
    if (valid)
    {
        String8 index_args[] = {git, S8("--no-replace-objects"), S8("-c"), S8("core.fsmonitor=false"), S8("ls-files"), S8("-v"), S8("--stage"), S8("-z")};
        RaddebuggerCommandResult index = raddebugger_command(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(index_args), source, (String8){0}, RADDEBUGGER_ENVIRONMENT_GIT, stopped);
        valid = raddebugger_command_ok(index) && index.output.length;
        for (u64 offset = 0; valid && offset < index.output.length;)
        {
            u64 end = offset;
            while (end < index.output.length && index.output.pointer[end]) { end += 1; }
            String8 entry = string_slice(index.output, offset, end);
            String8 mode = entry.length > 9 ? string_slice(entry, 2, 8) : (String8){0};
            valid = end < index.output.length && entry.length > 9 && entry.pointer[0] == 'H' && entry.pointer[1] == ' ' &&
                    (string_equal(mode, S8("100644")) || string_equal(mode, S8("100755")) || string_equal(mode, S8("120000")) ||
                     (string_equal(expected, S8(BUSTER_QEMU_COMMIT)) && string_equal(mode, S8("160000"))));
            offset = end + 1;
        }
    }
    if (valid)
    {
        String8 sparse_args[] = {git, S8("--no-replace-objects"), S8("config"), S8("--bool"), S8("--default=false"), S8("--get"), S8("core.sparseCheckout")};
        RaddebuggerCommandResult sparse = raddebugger_command(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(sparse_args), source, (String8){0}, RADDEBUGGER_ENVIRONMENT_GIT, stopped);
        valid = raddebugger_command_ok(sparse) && string_equal(compat_trim_ascii_space(sparse.output), S8("false"));
    }
    string_print(S8("QEMU_SOURCE expected={S8} pristine={u32} path={S8}\n"), expected, (u32)valid, source);
    return valid;
}

BUSTER_GLOBAL_LOCAL bool qemu_inputs_verify(Arena* arena, String8 git, String8 source, bool* stopped)
{
    String8 names[] = {S8("keycodemapdb"), S8("berkeley-softfloat-3"), S8("berkeley-testfloat-3")};
    String8 commits[] = {S8(BUSTER_QEMU_KEYMAP_COMMIT), S8(BUSTER_QEMU_SOFTFLOAT_COMMIT), S8(BUSTER_QEMU_TESTFLOAT_COMMIT)};
    bool valid = qemu_git_verify(arena, git, source, S8(BUSTER_QEMU_COMMIT), stopped);
    String8 lock = path_join(arena, source, S8("subprojects/.wraplock"));
    FileStats lock_stats = os_path_followed_stats(lock);
    valid = valid && lock_stats.valid && (lock_stats.kind == OS_FILE_KIND_MISSING ||
            (lock_stats.kind == OS_FILE_KIND_REGULAR && !lock_stats.size && string_equal(os_path_absolute(arena, lock, true), lock)));
    for (u64 index = 0; valid && index < BUSTER_ARRAY_LENGTH(names); index += 1)
    {
        String8 dependency = string_format_z(arena, S8("{S8}/subprojects/{S8}"), source, names[index]);
        valid = qemu_git_verify(arena, git, dependency, commits[index], stopped);
        // Meson's upstream wrap mechanism copies these two build descriptions.
        // Only byte-identical copies of the pinned packagefiles are admitted.
        if (valid && index)
        {
            String8 files[] = {S8("meson.build"), S8("meson_options.txt")};
            for (u64 file_i = 0; valid && file_i < BUSTER_ARRAY_LENGTH(files); file_i += 1)
            {
                String8 generated = path_join(arena, dependency, files[file_i]);
                FileStats generated_stats = os_path_followed_stats(generated);
                valid = generated_stats.valid;
                if (valid && generated_stats.kind != OS_FILE_KIND_MISSING)
                {
                    String8 original = string_format_z(arena, S8("{S8}/subprojects/packagefiles/{S8}/{S8}"), source, names[index], files[file_i]);
                    String8 cmp_args[] = {executable_resolve_in_path(arena, S8("cmp")), original, generated};
                    valid = raddebugger_command_ok(raddebugger_command(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(cmp_args), source, (String8){0}, RADDEBUGGER_ENVIRONMENT_INHERIT, stopped));
                }
            }
        }
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool qemu_packagefiles_prepare(Arena* arena, String8 source, String8 output, bool* stopped)
{
    String8 names[] = {S8("berkeley-softfloat-3"), S8("berkeley-testfloat-3")};
    String8 files[] = {S8("meson.build"), S8("meson_options.txt")};
    bool valid = true;
    for (u64 index = 0; valid && index < BUSTER_ARRAY_LENGTH(names); index += 1)
    {
        for (u64 file_i = 0; valid && file_i < BUSTER_ARRAY_LENGTH(files); file_i += 1)
        {
            String8 original = string_format_z(arena, S8("{S8}/subprojects/packagefiles/{S8}/{S8}"), source, names[index], files[file_i]);
            String8 generated = string_format_z(arena, S8("{S8}/subprojects/{S8}/{S8}"), source, names[index], files[file_i]);
            String8 copy[] = {executable_resolve_in_path(arena, S8("cp")), original, generated};
            String8 prefix = string_format(arena, S8("{S8}/packagefiles-{u64}-{u64}"), output, index, file_i);
            valid = raddebugger_command_ok(raddebugger_command(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(copy), source, prefix, RADDEBUGGER_ENVIRONMENT_INHERIT, stopped));
        }
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL ProcessResult qemu_self_test(Arena* arena)
{
    bool stopped = false;
    String8 git = executable_resolve_in_path(arena, S8("git"));
    make_directory_recursive(arena, S8("build"));
    String8 directory = string_format_z(arena, S8("build/qemu-self-test-{u64}-{u64}"), os_get_current_process_id(), os_now_microseconds());
    OsDirectoryCreateResult created = os_make_directory_exclusive(directory);
    bool passed = git.length && created.created;
    if (passed)
    {
        directory = os_path_absolute(arena, directory, true);
        String8 fixture = path_join(arena, directory, S8("fixture.txt"));
        String8 ignored = path_join(arena, directory, S8("ignored.txt"));
        String8 untracked = path_join(arena, directory, S8("untracked.txt"));
        bool files_written = file_write(fixture, BUSTER_SLICE_TO_BYTE_SLICE(S8("original\n"))) &&
                             file_write(path_join(arena, directory, S8(".gitignore")), BUSTER_SLICE_TO_BYTE_SLICE(S8("ignored.txt\n")));
        String8 initialize[] = {git, S8("init"), S8("--quiet")};
        String8 add[] = {git, S8("add"), S8("fixture.txt"), S8(".gitignore")};
        String8 commit[] = {git, S8("-c"), S8("user.name=Buster Harness"), S8("-c"), S8("user.email=harness@example.invalid"),
                            S8("-c"), S8("commit.gpgsign=false"), S8("-c"), S8("core.hooksPath=/dev/null"), S8("commit"), S8("--quiet"), S8("-m"), S8("isolated provenance fixture")};
        passed = files_written && raddebugger_command_ok(raddebugger_command(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(initialize), directory, (String8){0}, RADDEBUGGER_ENVIRONMENT_GIT, &stopped)) &&
                 raddebugger_command_ok(raddebugger_command(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(add), directory, (String8){0}, RADDEBUGGER_ENVIRONMENT_GIT, &stopped)) &&
                 raddebugger_command_ok(raddebugger_command(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(commit), directory, (String8){0}, RADDEBUGGER_ENVIRONMENT_GIT, &stopped));
        if (passed)
        {
            String8 head_arguments[] = {git, S8("rev-parse"), S8("HEAD")};
            RaddebuggerCommandResult head = raddebugger_command(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(head_arguments), directory, (String8){0}, RADDEBUGGER_ENVIRONMENT_GIT, &stopped);
            String8 fixture_commit = compat_trim_ascii_space(head.output);
            bool pristine = raddebugger_command_ok(head) && qemu_git_verify(arena, git, directory, fixture_commit, &stopped);
            bool wrong_revision = !qemu_git_verify(arena, git, directory, S8(BUSTER_QEMU_COMMIT), &stopped);
            bool dirty = file_write(fixture, BUSTER_SLICE_TO_BYTE_SLICE(S8("changed\n"))) && !qemu_git_verify(arena, git, directory, fixture_commit, &stopped);
            bool restored = file_write(fixture, BUSTER_SLICE_TO_BYTE_SLICE(S8("original\n")));
            bool untracked_rejected = file_write(untracked, BUSTER_SLICE_TO_BYTE_SLICE(S8("untracked\n"))) &&
                                      !qemu_git_verify(arena, git, directory, fixture_commit, &stopped);
            bool untracked_removed = os_file_delete(untracked);
            bool ignored_rejected = file_write(ignored, BUSTER_SLICE_TO_BYTE_SLICE(S8("ignored input\n"))) &&
                                    !qemu_git_verify(arena, git, directory, fixture_commit, &stopped);
            bool ignored_removed = os_file_delete(ignored);
            String8 assume[] = {git, S8("update-index"), S8("--assume-unchanged"), S8("fixture.txt")};
            String8 normal[] = {git, S8("update-index"), S8("--no-assume-unchanged"), S8("fixture.txt")};
            String8 skip[] = {git, S8("update-index"), S8("--skip-worktree"), S8("fixture.txt")};
            bool assume_rejected = raddebugger_command_ok(raddebugger_command(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(assume), directory, (String8){0}, RADDEBUGGER_ENVIRONMENT_GIT, &stopped)) &&
                                   !qemu_git_verify(arena, git, directory, fixture_commit, &stopped);
            bool normal_restored = raddebugger_command_ok(raddebugger_command(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(normal), directory, (String8){0}, RADDEBUGGER_ENVIRONMENT_GIT, &stopped));
            bool skip_rejected = raddebugger_command_ok(raddebugger_command(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(skip), directory, (String8){0}, RADDEBUGGER_ENVIRONMENT_GIT, &stopped)) &&
                                 !qemu_git_verify(arena, git, directory, fixture_commit, &stopped);
            bool exceptions = qemu_status_allowed(S8("!! subprojects/keycodemapdb/\n!! subprojects/berkeley-softfloat-3/\n!! subprojects/berkeley-testfloat-3/\n"), S8(BUSTER_QEMU_COMMIT)) &&
                              !qemu_status_allowed(S8("!! ignored-input.c\n"), S8(BUSTER_QEMU_COMMIT)) &&
                              qemu_status_allowed(S8("?? meson.build\n?? meson_options.txt\n"), S8(BUSTER_QEMU_SOFTFLOAT_COMMIT)) &&
                              !qemu_status_allowed(S8("?? extra.c\n"), S8(BUSTER_QEMU_SOFTFLOAT_COMMIT));
            // Reproduce a clean-looking replaced pin, then require immutable
            // provenance to refuse its altered index and worktree.
            String8 clear_skip[] = {git, S8("update-index"), S8("--no-skip-worktree"), S8("fixture.txt")};
            bool replacement_setup = raddebugger_command_ok(raddebugger_command(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(clear_skip), directory, (String8){0}, RADDEBUGGER_ENVIRONMENT_GIT, &stopped)) &&
                file_write(fixture, BUSTER_SLICE_TO_BYTE_SLICE(S8("replaced\n"))) &&
                raddebugger_command_ok(raddebugger_command(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(add), directory, (String8){0}, RADDEBUGGER_ENVIRONMENT_GIT, &stopped)) &&
                raddebugger_command_ok(raddebugger_command(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(commit), directory, (String8){0}, RADDEBUGGER_ENVIRONMENT_GIT, &stopped));
            RaddebuggerCommandResult altered_head = raddebugger_command(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(head_arguments), directory, (String8){0}, RADDEBUGGER_ENVIRONMENT_GIT, &stopped);
            String8 replace[] = {git, S8("replace"), fixture_commit, compat_trim_ascii_space(altered_head.output)};
            String8 checkout[] = {git, S8("checkout"), S8("--quiet"), S8("--detach"), S8("--force"), fixture_commit};
            String8 shadow_status[] = {git, S8("status"), S8("--porcelain")};
            replacement_setup = replacement_setup && raddebugger_command_ok(altered_head) &&
                raddebugger_command_ok(raddebugger_command(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(replace), directory, (String8){0}, RADDEBUGGER_ENVIRONMENT_GIT, &stopped)) &&
                raddebugger_command_ok(raddebugger_command(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(checkout), directory, (String8){0}, RADDEBUGGER_ENVIRONMENT_GIT, &stopped));
            RaddebuggerCommandResult shadow = raddebugger_command(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(shadow_status), directory, (String8){0}, RADDEBUGGER_ENVIRONMENT_GIT, &stopped);
            ByteSlice altered_bytes = file_read(arena, fixture, (FileReadOptions){.map_required = 0});
            bool replacement_rejected = replacement_setup && raddebugger_command_ok(shadow) && !shadow.output.length &&
                string_equal(BYTE_SLICE_TO_STRING(8, altered_bytes), S8("replaced\n")) && !qemu_git_verify(arena, git, directory, fixture_commit, &stopped);
            passed = replacement_rejected && exceptions && pristine && wrong_revision && dirty && restored && untracked_rejected && untracked_removed && ignored_rejected && ignored_removed &&
                     assume_rejected && normal_restored && skip_rejected;
            string_print(S8("QEMU_SELF_TEST pristine={u32} wrong_revision={u32} dirty={u32} untracked={u32} ignored={u32} "
                            "assume_unchanged={u32} skip_worktree={u32} replacement={u32} compiler_invocations=0 status={S8}\n"),
                         (u32)pristine, (u32)wrong_revision, (u32)dirty, (u32)untracked_rejected, (u32)ignored_rejected, (u32)assume_rejected,
                         (u32)skip_rejected, (u32)replacement_rejected, passed ? S8("pass") : S8("fail"));
        }
    }
    bool removed = !created.created || os_directory_delete(directory);
    ProcessResult result = passed && removed && !stopped ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
    return result;
}


// Suppress source-tree Python cache writes without changing any feature probe.
BUSTER_GLOBAL_LOCAL RaddebuggerCommandResult qemu_upstream_command(Arena* arena, SliceString8 arguments, String8 directory, String8 prefix, bool* stopped)
{
    String8* wrapped = arena_allocate(arena, String8, arguments.length + 2);
    wrapped[0] = executable_resolve_in_path(arena, S8("env"));
    wrapped[1] = S8("PYTHONDONTWRITEBYTECODE=1");
    for (u64 index = 0; index < arguments.length; index += 1) { wrapped[index + 2] = arguments.pointer[index]; }
    RaddebuggerCommandResult result = raddebugger_command(arena, (SliceString8){.pointer = wrapped, .length = arguments.length + 2},
                                                        directory, prefix, RADDEBUGGER_ENVIRONMENT_INHERIT, stopped);
    return result;
}

BUSTER_GLOBAL_LOCAL ProcessResult qemu_compat_main(Arena* arena, SliceString8 arguments)
{
    ProcessResult result = PROCESS_RESULT_FAILED;
    if (arguments.length == 1 && string_equal(arguments.pointer[0], S8("--self-test")))
    {
        result = qemu_self_test(arena);
    }
    else if (arguments.length == 2)
    {
#if BUSTER_LINUX && BUSTER_CPU_ARCH_X86_64
        bool stopped = false;
        String8 source = os_path_absolute(arena, arguments.pointer[0], true);
        String8 ide = os_path_absolute(arena, arguments.pointer[1], true);
        String8 git = executable_resolve_in_path(arena, S8("git"));
        String8 clang = executable_resolve_in_path(arena, S8("clang"));
        String8 ninja = executable_resolve_in_path(arena, S8("ninja"));
        String8 meson = executable_resolve_in_path(arena, S8("meson"));
        String8 guest_cc = executable_resolve_in_path(arena, S8("x86_64-linux-gnu-gcc"));
        String8 parent = os_path_absolute(arena, S8("build"), true);
        FileStats ide_stats = os_path_followed_stats(ide);
        bool admitted = ide_stats.valid && source.length && ide.length && git.length && clang.length && ninja.length && meson.length && guest_cc.length && parent.length &&
                        ide_stats.kind == OS_FILE_KIND_REGULAR && !string_equal(parent, source) && !string_starts_with_sequence(parent, string_format(arena, S8("{S8}/"), source));
        if (admitted) { admitted = qemu_inputs_verify(arena, git, source, &stopped); }
        if (admitted)
        {
            String8 output = string_format_z(arena, S8("{S8}/qemu-{u64}-{u64}"), parent, os_get_current_process_id(), os_now_microseconds());
            OsDirectoryCreateResult created = os_make_directory_exclusive(output);
            bool retained = created.created;
            String8List summary = {0};
            string8_list_push(arena, &summary, S8("variant\tconfigure\tbuild\tunit\tqtest\tguest\tfunctional\tperformance\n"));
            String8 compilers[] = {clang, string_format(arena, S8("{S8} cc"), ide)};
            String8 variants[] = {S8("reference"), S8("buster")};
            bool prepared = created.created && qemu_packagefiles_prepare(arena, source, output, &stopped) && qemu_inputs_verify(arena, git, source, &stopped);
            bool all_passed = prepared;
            for (u64 variant = 0; created.created && variant < BUSTER_ARRAY_LENGTH(variants); variant += 1)
            {
                String8 directory = path_join(arena, output, variants[variant]);
                bool variant_admitted = prepared && qemu_inputs_verify(arena, git, source, &stopped);
                bool directory_created = os_make_directory_exclusive(directory).created;
                String8 configure[] = {path_join(arena, source, S8("configure")), string_format(arena, S8("--cc={S8}"), compilers[variant]),
                    string_format(arena, S8("--host-cc={S8}"), compilers[variant]), string_format(arena, S8("--cross-cc-x86_64={S8}"), guest_cc),
                    S8("--target-list=x86_64-softmmu"), S8("--enable-tcg"), S8("--disable-kvm"), S8("--without-default-features"),
                    S8("--disable-download"), S8("--disable-docs"), S8("--disable-guest-agent"), S8("--disable-tools"), S8("--disable-fdt"),
                    S8("--disable-strip"), S8("--enable-install-blobs"),
                    S8("--disable-rust"), S8("--extra-cflags=-O0 -g")};
                String8 prefix = string_format(arena, S8("{S8}/{S8}-configure"), output, variants[variant]);
                bool configured = variant_admitted && directory_created && raddebugger_command_ok(qemu_upstream_command(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(configure), directory, prefix, &stopped));
                bool built = false;
                bool unit = false;
                bool qtest = false;
                if (configured)
                {
                    String8 commands[] = {ninja, S8("-t"), S8("commands"), S8("qemu-system-x86_64")};
                    retained = raddebugger_command_ok(qemu_upstream_command(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(commands), directory,
                        string_format(arena, S8("{S8}/{S8}-planned-actions"), output, variants[variant]), &stopped)) && retained;
                    String8 build[] = {ninja, S8("-j2"), S8("-v"), S8("qemu-system-x86_64")};
                    built = raddebugger_command_ok(qemu_upstream_command(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(build), directory,
                        string_format(arena, S8("{S8}/{S8}-build"), output, variants[variant]), &stopped));
                }
                if (built)
                {
                    String8 inventory[] = {path_join(arena, directory, S8("pyvenv/bin/meson")), S8("introspect"), S8("--tests"), directory};
                    retained = raddebugger_command_ok(qemu_upstream_command(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(inventory), directory,
                        string_format(arena, S8("{S8}/{S8}-test-inventory"), output, variants[variant]), &stopped)) && retained;
                    String8 unit_args[] = {executable_resolve_in_path(arena, S8("make")), S8("-j2"), S8("check-unit")};
                    unit = raddebugger_command_ok(qemu_upstream_command(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(unit_args), directory,
                        string_format(arena, S8("{S8}/{S8}-unit"), output, variants[variant]), &stopped));
                    String8 qtest_args[] = {executable_resolve_in_path(arena, S8("make")), S8("-j2"), S8("check-qtest")};
                    qtest = raddebugger_command_ok(qemu_upstream_command(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(qtest_args), directory,
                        string_format(arena, S8("{S8}/{S8}-qtest"), output, variants[variant]), &stopped));
                }
                string8_list_push(arena, &summary, string_format(arena,
                    S8("{S8}\t{S8}\t{S8}\t{S8}\t{S8}\tpending\tpending\tpending\n"), variants[variant],
                    !variant_admitted ? S8("not-attempted:inputs") : configured ? S8("pass") : S8("fail"), !variant_admitted ? S8("not-attempted:inputs") : !configured ? S8("not-attempted:configure") : built ? S8("pass") : S8("fail"),
                    !built ? S8("not-attempted:build") : unit ? S8("pass") : S8("fail"),
                    !built ? S8("not-attempted:build") : qtest ? S8("pass") : S8("fail")));
                all_passed = configured && built && unit && qtest && all_passed;
            }
            bool pristine = qemu_inputs_verify(arena, git, source, &stopped);
            String8 text = string_join_arena(arena, string8_list_to_slice(arena, summary), false);
            retained = file_write(path_join(arena, output, S8("summary.tsv")), BUSTER_SLICE_TO_BYTE_SLICE(text)) && retained;
            string_print(S8("QEMU_CENSUS output={S8} pristine_after={u32} retained={u32}\n{S8}"), output, (u32)pristine, (u32)retained, text);
            result = all_passed && pristine && retained && !stopped ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
        }
        else
        {
            string_print(S8("error: test_qemu requires a pristine pinned QEMU/dependency tree, built ide, Clang, Meson and Ninja; see docs/agents/compatibility/qemu.md\n"));
        }
#else
        string_print(S8("error: test_qemu census requires Linux x86-64\n"));
#endif
    }
    else
    {
        string_print(S8("usage: test_qemu --self-test | <absolute-qemu-source> <absolute-ide>\n"));
    }
    return result;
}
