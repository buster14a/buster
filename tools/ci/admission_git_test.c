// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original object-only Git fixtures. No network, source checkout, external fixture or PR program is used.

static void ci_test_blob(CiGit* git, const char* text, char* sha)
{
    const char* hash[] = {"hash-object", "-w", "--stdin", NULL};
    Buffer output = ci_git_ok(git->directory, hash, text, strlen(text));
    ci_git_sha(&output, sha);
}

static void ci_test_tree(CiGit* git, const char* code, const char* note, const char* attributes, char* sha)
{
    char a[BUSTER_CI_SHA], b[BUSTER_CI_SHA], c[BUSTER_CI_SHA];
    ci_test_blob(git, code, a); ci_test_blob(git, note, b);
    Buffer rows = {0};
    if (attributes != NULL)
    {
        ci_test_blob(git, attributes, c);
        buffer_printf(&rows, "100644 blob %s\t.gitattributes\n", c);
    }
    buffer_printf(&rows, "100644 blob %s\tnote.md\n100644 blob %s\tunit.c\n", b, a);
    const char* tree[] = {"mktree", NULL};
    Buffer output = ci_git_ok(git->directory, tree, rows.data, rows.length);
    free(rows.data); ci_git_sha(&output, sha);
}

static void ci_test_git(void)
{
    CiGit fixture = {0}, receiver = {0};
    ci_git_init(&fixture, false); ci_git_init(&receiver, false);
    char root[BUSTER_CI_SHA], base[BUSTER_CI_SHA], head[BUSTER_CI_SHA], tree[BUSTER_CI_SHA], conflict[BUSTER_CI_SHA], repeated[BUSTER_CI_SHA];
    ci_test_tree(&fixture, "int value = 1;\n", "base\n", "unit.c merge=unsafe\n", tree);
    ci_git_commit(&fixture, tree, NULL, NULL, root);
    ci_test_tree(&fixture, "int value = 1;\n", "current base\n", "unit.c merge=unsafe\n", tree);
    ci_git_commit(&fixture, tree, root, NULL, base);
    ci_test_tree(&fixture, "int value = 2;\n", "base\n", "unit.c merge=unsafe\n", tree);
    ci_git_commit(&fixture, tree, root, NULL, head);
    ci_expect(!ci_git_code_in(&fixture, root, base), "Git: the exact immutable diff recognizes a documentation-only change");
    ci_expect(ci_git_code_in(&fixture, root, head), "Git: an actual source change requires qualification");
    ci_expect(!ci_document("docs/performance/report.md"), "Git: performance documentation still requires qualification");
    char renamed[BUSTER_CI_SHA], code_blob[BUSTER_CI_SHA];
    ci_test_blob(&fixture, "int value = 1;\n", code_blob);
    Buffer rename_rows = {0};
    buffer_printf(&rename_rows, "100644 blob %s\tunit.md\n", code_blob);
    const char* make_tree[] = {"mktree", NULL};
    Buffer rename_tree = ci_git_ok(fixture.directory, make_tree, rename_rows.data, rename_rows.length);
    free(rename_rows.data); ci_git_sha(&rename_tree, tree);
    ci_git_commit(&fixture, tree, root, NULL, renamed);
    ci_expect(ci_git_code_in(&fixture, root, renamed), "Git: renaming a source to Markdown cannot hide the removed source path");
    CiLease lease = {.pr = 546};
    ci_copy(lease.base, sizeof(lease.base), base); ci_copy(lease.head, sizeof(lease.head), head);
    ci_expect(ci_git_merge_in(&fixture, base, head, lease.merge) == 1, "Git: a clean behind PR merges with the live base without changing its branch");
    ci_expect(ci_git_merge_in(&fixture, base, head, repeated) == 1 && strcmp(lease.merge, repeated) == 0, "Git: canonical merge identity is deterministic");
    char spec[80];
    snprintf(spec, sizeof(spec), "%s:unit.c", lease.merge);
    const char* show[] = {"show", spec, NULL};
    Buffer output = ci_git_ok(fixture.directory, show, NULL, 0);
    ci_expect(strcmp(output.data, "int value = 2;\n") == 0, "Git: the qualification tree contains the PR's actual code");
    free(output.data);
    snprintf(spec, sizeof(spec), "%s:note.md", lease.merge);
    output = ci_git_ok(fixture.directory, show, NULL, 0);
    ci_expect(strcmp(output.data, "current base\n") == 0, "Git: the qualification tree includes changes from the advanced base");
    free(output.data);
    // Initialize the receiver with exactly the two input histories, then import only newly created merge objects.
    Buffer revisions = {0}; buffer_printf(&revisions, "%s\n%s\n", base, head);
    const char* pack_command[] = {"pack-objects", "--stdout", "--revs", NULL};
    Buffer inputs = ci_git_ok(fixture.directory, pack_command, revisions.data, revisions.length);
    free(revisions.data);
    const char* import[] = {"index-pack", "--stdin", "--strict", NULL};
    output = ci_git_ok(receiver.directory, import, inputs.data, inputs.length); free(output.data); free(inputs.data);
    Buffer pack = ci_git_pack_payload(&fixture, &lease);
    ci_expect(pack.length > 12 && pack.length < 4096, "Git: transfer contains only new canonical merge objects, not the source history");
    output = ci_git_ok(receiver.directory, import, pack.data, pack.length); free(output.data); free(pack.data);
    snprintf(spec, sizeof(spec), "%s:note.md", lease.merge);
    output = ci_git_ok(receiver.directory, show, NULL, 0);
    ci_expect(strcmp(output.data, "current base\n") == 0, "Git: the same frozen merge reconstructs from its authenticated small pack and pinned inputs");
    free(output.data);
    ci_test_tree(&fixture, "int value = 3;\n", "base\n", "unit.c merge=unsafe\n", tree);
    ci_git_commit(&fixture, tree, root, NULL, conflict);
    char driver[512], marker[300];
    snprintf(marker, sizeof(marker), "%s/driver-was-run", fixture.directory);
    snprintf(driver, sizeof(driver), "touch %s", marker);
    setenv("GIT_CONFIG_COUNT", "1", 1);
    setenv("GIT_CONFIG_KEY_0", "merge.unsafe.driver", 1);
    setenv("GIT_CONFIG_VALUE_0", driver, 1);
    ci_expect(ci_git_merge_in(&fixture, conflict, head, repeated) == 0 && *repeated == 0, "Git: a new base conflict is detected even if GitHub's cached test merge was clean");
    ci_expect(access(marker, F_OK) != 0, "Git: attributes cannot invoke an inherited external merge driver");
    unsetenv("GIT_CONFIG_COUNT"); unsetenv("GIT_CONFIG_KEY_0"); unsetenv("GIT_CONFIG_VALUE_0");
    ci_git_dispose(&fixture); ci_git_dispose(&receiver);
}
