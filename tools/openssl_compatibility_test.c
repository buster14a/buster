// Independent pristine-input fixture; all Git mutations are confined to its owned directory.
BUSTER_GLOBAL_LOCAL ProcessResult openssl_pristine_self_test(Arena* arena)
{
    bool stopped = false;
    String8 git = executable_resolve_in_path(arena, S8("git"));
    make_directory_recursive(arena, S8("build"));
    String8 directory = string_format_z(arena, S8("build/openssl-pristine-controls-{u64}-{u64}"), os_get_current_process_id(), os_now_microseconds());
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
            bool pristine = raddebugger_command_ok(head) && openssl_git_verify(arena, git, directory, fixture_commit, &stopped);
            bool wrong_revision = !openssl_git_verify(arena, git, directory, S8(RADDEBUGGER_COMPATIBILITY_COMMIT), &stopped);
            bool dirty = file_write(fixture, BUSTER_SLICE_TO_BYTE_SLICE(S8("changed\n"))) && !openssl_git_verify(arena, git, directory, fixture_commit, &stopped);
            bool restored = file_write(fixture, BUSTER_SLICE_TO_BYTE_SLICE(S8("original\n")));
            bool untracked_rejected = file_write(untracked, BUSTER_SLICE_TO_BYTE_SLICE(S8("untracked\n"))) &&
                                      !openssl_git_verify(arena, git, directory, fixture_commit, &stopped);
            bool untracked_removed = os_file_delete(untracked);
            bool ignored_rejected = file_write(ignored, BUSTER_SLICE_TO_BYTE_SLICE(S8("ignored input\n"))) &&
                                    !openssl_git_verify(arena, git, directory, fixture_commit, &stopped);
            bool ignored_removed = os_file_delete(ignored);
            String8 assume[] = {git, S8("update-index"), S8("--assume-unchanged"), S8("fixture.txt")};
            String8 normal[] = {git, S8("update-index"), S8("--no-assume-unchanged"), S8("fixture.txt")};
            String8 skip[] = {git, S8("update-index"), S8("--skip-worktree"), S8("fixture.txt")};
            bool assume_rejected = raddebugger_command_ok(raddebugger_command(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(assume), directory, (String8){0}, RADDEBUGGER_ENVIRONMENT_GIT, &stopped)) &&
                                   !openssl_git_verify(arena, git, directory, fixture_commit, &stopped);
            bool normal_restored = raddebugger_command_ok(raddebugger_command(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(normal), directory, (String8){0}, RADDEBUGGER_ENVIRONMENT_GIT, &stopped));
            bool skip_rejected = raddebugger_command_ok(raddebugger_command(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(skip), directory, (String8){0}, RADDEBUGGER_ENVIRONMENT_GIT, &stopped)) &&
                                 !openssl_git_verify(arena, git, directory, fixture_commit, &stopped);
            String8 clear_skip[] = {git, S8("update-index"), S8("--no-skip-worktree"), S8("fixture.txt")};
            bool skip_restored = raddebugger_command_ok(raddebugger_command(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(clear_skip), directory, (String8){0}, RADDEBUGGER_ENVIRONMENT_GIT, &stopped));
            bool replacement_written = skip_restored && file_write(fixture, BUSTER_SLICE_TO_BYTE_SLICE(S8("replacement\n"))) &&
                                       raddebugger_command_ok(raddebugger_command(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(add), directory, (String8){0}, RADDEBUGGER_ENVIRONMENT_GIT, &stopped)) &&
                                       raddebugger_command_ok(raddebugger_command(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(commit), directory, (String8){0}, RADDEBUGGER_ENVIRONMENT_GIT, &stopped));
            RaddebuggerCommandResult replacement_head = raddebugger_command(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(head_arguments), directory, (String8){0}, RADDEBUGGER_ENVIRONMENT_GIT, &stopped);
            String8 replacement_commit = compat_trim_ascii_space(replacement_head.output);
            String8 replace[] = {git, S8("replace"), fixture_commit, replacement_commit};
            String8 checkout[] = {git, S8("checkout"), S8("--quiet"), S8("--detach"), fixture_commit};
            bool replacement_rejected = replacement_written && raddebugger_command_ok(replacement_head) &&
                                        raddebugger_command_ok(raddebugger_command(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(replace), directory, (String8){0}, RADDEBUGGER_ENVIRONMENT_GIT, &stopped)) &&
                                        raddebugger_command_ok(raddebugger_command(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(checkout), directory, (String8){0}, RADDEBUGGER_ENVIRONMENT_GIT, &stopped)) &&
                                        !openssl_git_verify(arena, git, directory, fixture_commit, &stopped);
            passed = replacement_rejected && pristine && wrong_revision && dirty && restored && untracked_rejected && untracked_removed && ignored_rejected && ignored_removed &&
                     assume_rejected && normal_restored && skip_rejected;
            string_print(S8("OPENSSL_PRISTINE_CONTROLS pristine={u32} wrong_revision={u32} dirty={u32} untracked={u32} ignored={u32} "
                            "assume_unchanged={u32} skip_worktree={u32} replaced_commit={u32} compiler_invocations=0 status={S8}\n"),
                         (u32)pristine, (u32)wrong_revision, (u32)dirty, (u32)untracked_rejected, (u32)ignored_rejected, (u32)assume_rejected,
                         (u32)skip_rejected, (u32)replacement_rejected, passed ? S8("pass") : S8("fail"));
        }
    }
    bool removed = !created.created || os_directory_delete(directory);
    ProcessResult result = passed && removed && !stopped ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
    return result;
}

// Independent compiler-routing and optional-input controls for #3184.
// Included after the intake helpers by openssl_compatibility.c.
BUSTER_GLOBAL_LOCAL bool openssl_self_test(Arena* arena)
{
    String8 c[] = {S8("-c"), S8("x.c"), S8("-o"), S8("result.S")};
    String8 assembly[] = {S8("-c"), S8("x.S"), S8("-o"), S8("result.c")};
    String8 link[] = {S8("x.o"), S8("libcrypto.a"), S8("-o"), S8("result.c")};
    String8 mixed[] = {S8("x.c"), S8("x.S")};
    String8 probe[] = {S8("--version")};
    bool routes = openssl_route((SliceString8)BUSTER_ARRAY_TO_SLICE(c)) == OPENSSL_ROUTE_C &&
                  openssl_route((SliceString8)BUSTER_ARRAY_TO_SLICE(assembly)) == OPENSSL_ROUTE_ASSEMBLY &&
                  openssl_route((SliceString8)BUSTER_ARRAY_TO_SLICE(link)) == OPENSSL_ROUTE_LINK &&
                  openssl_route((SliceString8)BUSTER_ARRAY_TO_SLICE(mixed)) == OPENSSL_ROUTE_INVALID &&
                  openssl_route((SliceString8)BUSTER_ARRAY_TO_SLICE(probe)) == OPENSSL_ROUTE_PROBE;
    make_directory_recursive(arena, S8("build"));
    String8 root = string_format_z(arena, S8("build/openssl-input-controls-{u64}-{u64}"), os_get_current_process_id(), os_now_microseconds());
    bool claimed = os_make_directory_exclusive(root).created;
    bool inputs = false;
    if (claimed)
    {
        root = os_path_absolute(arena, root, true);
        String8 entry = S8("H 160000 7e33b0e7739d633c77b8c478620167f693ed13f4 0\tpyca-cryptography");
        String8 wrong = S8("H 160000 0000000000000000000000000000000000000000 0\tpyca-cryptography");
        String8 unknown = S8("H 160000 7e33b0e7739d633c77b8c478620167f693ed13f4 0\tunknown-input");
        String8 nested = path_join(arena, root, S8("pyca-cryptography"));
        String8 payload = path_join(arena, nested, S8("unexpected.h"));
        bool absent = openssl_gitlink_admitted(arena, root, entry);
        bool wrong_rejected = !openssl_gitlink_admitted(arena, root, wrong);
        bool unknown_rejected = !openssl_gitlink_admitted(arena, root, unknown);
        bool empty = os_make_directory_exclusive(nested).created && openssl_gitlink_admitted(arena, root, entry);
        bool populated_rejected = file_write(payload, BUSTER_SLICE_TO_BYTE_SLICE(S8("unqualified input\n"))) &&
                                  !openssl_gitlink_admitted(arena, root, entry);
        inputs = absent && wrong_rejected && unknown_rejected && empty && populated_rejected;
        string_print(S8("OPENSSL_INPUT_CONTROLS absent={u32} empty={u32} wrong_pin={u32} unknown_path={u32} populated={u32}\n"),
                     (u32)absent, (u32)empty, (u32)wrong_rejected, (u32)unknown_rejected, (u32)populated_rejected);
    }
    bool archive_control = false;
    if (claimed)
    {
        bool stopped = false;
        String8 tar = executable_resolve_in_path(arena, S8("tar"));
        String8 colon = path_join(arena, root, S8("file:server.pem"));
        bool written = file_write(colon, BUSTER_SLICE_TO_BYTE_SLICE(S8("isolated filename fixture\n")));
        bool packed = written && openssl_archive(arena, root, &stopped);
        String8 list[] = {tar, S8("-tzf"), path_join(arena, root, S8("evidence.tar.gz"))};
        RaddebuggerCommandResult listed = raddebugger_command(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(list), root, (String8){0}, RADDEBUGGER_ENVIRONMENT_INHERIT, &stopped);
        String8 expected = S8("./file:server.pem\n");
        bool found = false;
        for (u64 i = 0; i + expected.length <= listed.output.length; i += 1)
        {
            found = string_equal(string_slice(listed.output, i, i + expected.length), expected) || found;
        }
        archive_control = packed && raddebugger_command_ok(listed) && found && !stopped;
        string_print(S8("OPENSSL_ARCHIVE_CONTROL colon_filename={u32}\n"), (u32)archive_control);
    }
    bool removed = !claimed || os_directory_delete(root);
    bool pristine = openssl_pristine_self_test(arena) == PROCESS_RESULT_SUCCESS;
    bool result = routes && inputs && archive_control && removed && pristine;
    string_print(S8("OPENSSL_SELF_TEST status={S8}\n"), result ? S8("pass") : S8("fail"));
    return result;
}
