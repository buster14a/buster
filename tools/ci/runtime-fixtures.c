// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Black-box runtime fixture contracts (#3629). Every child uses the same compiler binary from a directory outside
// the checkout. Mutations affect private copies; a missing inventory, ignored root or stale expectation must fail.
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "../../src/buster/compiler/test_paths.h"

typedef struct FixtureTest FixtureTest;
struct FixtureTest
{
    const char* compiler;
    const char* clang;
    const char* repo;
    char work[PATH_MAX];
    unsigned checks;
    unsigned failures;
};

static void fixture_expect(FixtureTest* test, bool passed, const char* label)
{
    test->checks += 1;
    test->failures += !passed;
    printf("runtime-fixtures: %s: %s\n", passed ? "PASS" : "FAIL", label);
    fflush(stdout);
}

static bool fixture_parents(const char* path)
{
    char directory[PATH_MAX];
    const size_t length = strlen(path);
    bool valid = length < sizeof(directory);
    if (valid)
    {
        memcpy(directory, path, length + 1);
        for (size_t i = 1; valid && i < length; i += 1)
        {
            if (directory[i] == '/')
            {
                directory[i] = 0;
                valid = mkdir(directory, 0700) == 0 || errno == EEXIST;
                directory[i] = '/';
            }
        }
    }
    return valid;
}

static bool fixture_write(FixtureTest* test, const char* path, const void* bytes, size_t length)
{
    FILE* file = fixture_parents(path) ? fopen(path, "wb") : NULL;
    bool valid = file != NULL && (length == 0 || fwrite(bytes, 1, length, file) == length);
    if (file != NULL)
    {
        valid = fclose(file) == 0 && valid;
    }
    if (!valid)
    {
        fixture_expect(test, false, "write private fixture data");
        fprintf(stderr, "runtime-fixtures: cannot write %s: %s\n", path, strerror(errno));
    }
    return valid;
}

static bool fixture_copy_at(FixtureTest* test, const char* from, const char* to, unsigned depth)
{
    struct stat info;
    bool valid = depth < 32 && lstat(from, &info) == 0;
    if (valid && S_ISDIR(info.st_mode))
    {
        valid = fixture_parents(to) && (mkdir(to, 0700) == 0 || errno == EEXIST);
        DIR* directory = valid ? opendir(from) : NULL;
        valid = directory != NULL;
        for (struct dirent* item = valid ? readdir(directory) : NULL; valid && item != NULL; item = readdir(directory))
        {
            if (strcmp(item->d_name, ".") != 0 && strcmp(item->d_name, "..") != 0)
            {
                char input[PATH_MAX];
                char output[PATH_MAX];
                valid = test_fixture_path(input, sizeof(input), from, item->d_name) &&
                        test_fixture_path(output, sizeof(output), to, item->d_name) &&
                        fixture_copy_at(test, input, output, depth + 1);
            }
        }
        if (directory != NULL)
        {
            closedir(directory);
        }
    }
    else if (valid && S_ISREG(info.st_mode))
    {
        FILE* input = fopen(from, "rb");
        FILE* output = fixture_parents(to) ? fopen(to, "wb") : NULL;
        valid = input != NULL && output != NULL;
        unsigned char bytes[16384];
        size_t got = 0;
        while (valid && (got = fread(bytes, 1, sizeof(bytes), input)) != 0)
        {
            valid = fwrite(bytes, 1, got, output) == got;
        }
        if (input != NULL)
        {
            valid = !ferror(input) && valid;
            valid = fclose(input) == 0 && valid;
        }
        if (output != NULL)
        {
            valid = fclose(output) == 0 && valid;
        }
    }
    else
    {
        valid = false;
    }
    return valid;
}

static bool fixture_copy_tree(FixtureTest* test, const char* from, const char* to)
{
    const bool valid = fixture_copy_at(test, from, to, 0);
    if (!valid)
    {
        fixture_expect(test, false, "copy private fixture tree");
        fprintf(stderr, "runtime-fixtures: cannot copy %s to %s\n", from, to);
    }
    return valid;
}

static bool fixture_remove_at(const char* path, unsigned depth)
{
    struct stat info;
    bool valid = depth < 32 && lstat(path, &info) == 0;
    if (valid && S_ISDIR(info.st_mode))
    {
        DIR* directory = opendir(path);
        valid = directory != NULL;
        for (struct dirent* item = valid ? readdir(directory) : NULL; valid && item != NULL; item = readdir(directory))
        {
            if (strcmp(item->d_name, ".") != 0 && strcmp(item->d_name, "..") != 0)
            {
                char child[PATH_MAX];
                valid = test_fixture_path(child, sizeof(child), path, item->d_name) && fixture_remove_at(child, depth + 1);
            }
        }
        if (directory != NULL)
        {
            closedir(directory);
        }
        valid = valid && rmdir(path) == 0;
    }
    else if (valid)
    {
        valid = unlink(path) == 0;
    }
    return valid;
}

static bool fixture_remove(FixtureTest* test, const char* path)
{
    const size_t root = strlen(test->work);
    const bool valid = strncmp(path, test->work, root) == 0 && path[root] == '/' && fixture_remove_at(path, 0);
    if (!valid)
    {
        fixture_expect(test, false, "remove private fixture data");
        fprintf(stderr, "runtime-fixtures: cannot remove %s\n", path);
    }
    return valid;
}

static bool fixture_run(FixtureTest* test, const char* label, const char* const* arguments, int expected_exit, const char* required_output)
{
    char log[PATH_MAX];
    const int length = snprintf(log, sizeof(log), "%s/run-%u.log", test->work, test->checks + 1);
    const int output = length > 0 && (size_t)length < sizeof(log) ? open(log, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600) : -1;
    const pid_t child = output >= 0 ? fork() : -1;
    if (child == 0)
    {
        bool ready = chdir(test->work) == 0 && dup2(output, STDOUT_FILENO) >= 0 && dup2(output, STDERR_FILENO) >= 0;
        close(output);
        char clang_path[PATH_MAX];
        const char* slash = strrchr(test->clang, '/');
        const char* inherited = getenv("PATH");
        const int path_length = slash != NULL ? snprintf(clang_path, sizeof(clang_path), "%.*s:%s", (int)(slash - test->clang), test->clang,
                                                         inherited != NULL ? inherited : "/usr/bin:/bin") : -1;
        ready = ready && path_length > 0 && (size_t)path_length < sizeof(clang_path) && setenv("PATH", clang_path, 1) == 0;
        alarm(180);
        if (ready)
        {
            execv(arguments[0], (char* const*)arguments);
        }
        _exit(127);
    }
    if (output >= 0)
    {
        close(output);
    }
    int status = 0;
    pid_t waited = -1;
    do
    {
        waited = child > 0 ? waitpid(child, &status, 0) : -1;
    } while (waited < 0 && errno == EINTR);
    bool valid = waited == child && child > 0 && WIFEXITED(status) && WEXITSTATUS(status) == expected_exit;
    FILE* file = fopen(log, "rb");
    char* text = NULL;
    long size = -1;
    if (file != NULL && fseek(file, 0, SEEK_END) == 0)
    {
        size = ftell(file);
    }
    if (size >= 0 && size < (1 << 24) && fseek(file, 0, SEEK_SET) == 0)
    {
        text = malloc((size_t)size + 1);
        const bool read = text != NULL && fread(text, 1, (size_t)size, file) == (size_t)size;
        if (read)
        {
            text[size] = 0;
        }
        else
        {
            free(text);
            text = NULL;
        }
    }
    if (file != NULL)
    {
        fclose(file);
    }
    valid = valid && text != NULL && (required_output == NULL || strstr(text, required_output) != NULL);
    fixture_expect(test, valid, label);
    if (!valid)
    {
        fprintf(stderr, "runtime-fixtures: expected exit %d, child status %#x; log %s\n%.8000s\n", expected_exit, status, log,
                text != NULL ? text : "<unreadable log>");
    }
    free(text);
    return valid;
}

static void fixture_path_controls(FixtureTest* test)
{
    char path[64];
    fixture_expect(test, test_fixture_path(path, sizeof(path), "/", "cases/input.c") && strcmp(path, "/cases/input.c") == 0, "root slash joins exactly once");
    fixture_expect(test, test_fixture_path(path, sizeof(path), "/tmp/data/", "case.c") && strcmp(path, "/tmp/data/case.c") == 0, "trailing root slash joins exactly once");
    fixture_expect(test, !test_fixture_path(path, 3, "data", "case.c") && path[0] == 0, "short path buffer fails without a partial path");
    fixture_expect(test, !test_fixture_path(path, sizeof(path), "", "case.c") && path[0] == 0, "empty fixture root fails");
    fixture_expect(test, !test_fixture_path(path, sizeof(path), "data", "") && path[0] == 0, "empty fixture relative path fails");
    fixture_expect(test, !test_fixture_path(NULL, 0, "data", "case.c"), "zero-sized fixture path buffer fails");
}

static void fixture_checker_controls(FixtureTest* test);
static void fixture_ir_controls(FixtureTest* test);
static void fixture_driver_controls(FixtureTest* test);
static void fixture_driver_root_controls(FixtureTest* test);

int main(int argc, char** argv)
{
    FixtureTest test = { 0 };
    char compiler[PATH_MAX];
    char clang[PATH_MAX];
    char repository[PATH_MAX];
    bool ready = argc == 4 && realpath(argv[1], compiler) != NULL && realpath(argv[2], clang) != NULL && realpath(argv[3], repository) != NULL;
    if (!ready)
    {
        fprintf(stderr, "usage: runtime-fixtures COMPILER CLANG REPOSITORY\n");
    }
    else
    {
        test.compiler = compiler;
        test.clang = clang;
        test.repo = repository;
        memcpy(test.work, "/tmp/buster-runtime-fixtures-XXXXXX", sizeof("/tmp/buster-runtime-fixtures-XXXXXX"));
        ready = mkdtemp(test.work) != NULL;
        if (ready)
        {
            fixture_path_controls(&test);
            fixture_checker_controls(&test);
            fixture_ir_controls(&test);
            fixture_driver_controls(&test);
            fixture_driver_root_controls(&test);
            printf("runtime-fixtures: %u checks, %u failures\n", test.checks, test.failures);
            if (test.failures == 0)
            {
                fixture_remove_at(test.work, 0);
            }
            else
            {
                fprintf(stderr, "runtime-fixtures: preserved failing data and logs at %s\n", test.work);
            }
        }
    }
    return ready && test.failures == 0 ? 0 : 1;
}
static char* fixture_read(FixtureTest* test, const char* path, size_t* length)
{
    *length = 0;
    FILE* file = fopen(path, "rb");
    bool valid = file != NULL && fseek(file, 0, SEEK_END) == 0;
    const long size = valid ? ftell(file) : -1;
    valid = size >= 0 && (unsigned long)size <= 16UL * 1024 * 1024 && fseek(file, 0, SEEK_SET) == 0;
    char* bytes = valid ? malloc((size_t)size + 1) : NULL;
    valid = bytes != NULL && fread(bytes, 1, (size_t)size, file) == (size_t)size;
    if (file != NULL)
    {
        valid = fclose(file) == 0 && valid;
    }
    if (valid)
    {
        bytes[size] = 0;
        *length = (size_t)size;
    }
    else
    {
        free(bytes);
        bytes = NULL;
    }
    fixture_expect(test, valid, "read original runtime fixture bytes");
    return bytes;
}

static bool fixture_append(FixtureTest* test, const char* path, const char* original, size_t length, const char* row)
{
    const size_t added = strlen(row);
    char* bytes = malloc(length + added);
    bool valid = bytes != NULL;
    if (valid)
    {
        memcpy(bytes, original, length);
        memcpy(bytes + length, row, added);
        valid = fixture_write(test, path, bytes, length + added);
    }
    else
    {
        fixture_expect(test, false, "allocate extended runtime catalog");
    }
    free(bytes);
    return valid;
}

static void fixture_pp_digest_controls(FixtureTest* test, const char* root)
{
    char source[PATH_MAX];
    char destination[PATH_MAX];
    char cases[PATH_MAX];
    char catalog[PATH_MAX];
    bool valid = test_fixture_path(cases, sizeof(cases), root, "preprocessor/cases") &&
                 test_fixture_path(catalog, sizeof(catalog), cases, "catalog.tsv") &&
                 test_fixture_path(source, sizeof(source), test->repo, "tests/preprocessor/trees") &&
                 test_fixture_path(destination, sizeof(destination), root, "preprocessor/trees") &&
                 fixture_copy_tree(test, source, destination);
    static const char* const files[] = { "0002.c", "0002.expected" };
    for (size_t i = 0; valid && i < sizeof(files) / sizeof(files[0]); i += 1)
    {
        char original[PATH_MAX];
        valid = test_fixture_path(original, sizeof(original), test->repo, "tests/preprocessor/cases") &&
                test_fixture_path(source, sizeof(source), original, files[i]) &&
                test_fixture_path(destination, sizeof(destination), cases, files[i]) &&
                fixture_copy_tree(test, source, destination);
    }
    valid = valid && test_fixture_path(source, sizeof(source), test->repo, "tests/preprocessor/cases/catalog.tsv");
    size_t length = 0;
    char* original = valid ? fixture_read(test, source, &length) : NULL;
    char* row = original != NULL ? strstr(original, "\n0002\t") : NULL;
    char* end = row != NULL ? strchr(row + 1, '\n') : NULL;
    valid = valid && row != NULL && end != NULL;
    if (valid)
    {
        end[1] = 0;
        static const char header[] = "buster-fixtures v1 preprocessor-cases\n";
        valid = fixture_append(test, catalog, header, sizeof(header) - 1, row + 1);
    }
    free(original);
    char digests[PATH_MAX];
    valid = valid && test_fixture_path(source, sizeof(source), test->repo, "tests/preprocessor-kernels/digests.tsv") &&
            test_fixture_path(digests, sizeof(digests), root, "preprocessor-kernels/digests.tsv") &&
            fixture_copy_tree(test, source, digests);
    fixture_expect(test, valid, "prepare an existing preprocessor case and its selected frozen digest root");
    // With no Clang-only checks or fuzz modes, the cases' weight is second. This part runs the case inventory
    // alone; the required cases summary also detects a later change to that allocation.
    const char* const command[] = { test->compiler, "test-preprocessor", "--part", "2/128", "--fuzz", "0", "--fixtures", root, NULL };
    if (valid)
    {
        fixture_run(test, "preprocessor frozen digests resolve outside the checkout", command, 0, "cases: 0 of 1 fail");
        size_t digest_length = 0;
        char* bytes = fixture_read(test, digests, &digest_length);
        char* changed = bytes != NULL ? malloc(digest_length) : NULL;
        valid = bytes != NULL && changed != NULL;
        fixture_expect(test, valid, "prepare selected preprocessor digest mutations");
        if (valid)
        {
            memcpy(changed, bytes, digest_length);
            bool digest = false;
            for (size_t at = 0; at < digest_length; at += 1)
            {
                if (changed[at] == '\t')
                {
                    digest = true;
                }
                else if (changed[at] == '\n')
                {
                    digest = false;
                }
                else if (digest && changed[at] != '-')
                {
                    changed[at] = '0';
                }
            }
            fixture_write(test, digests, changed, digest_length);
            fixture_run(test, "selected preprocessor digest mutation fails", command, 1, "aren't their recorded digest");
            fixture_write(test, digests, bytes, digest_length);
            fixture_remove(test, digests);
            fixture_run(test, "selected preprocessor digest file is required", command, 1, "no recorded digest");
            fixture_write(test, digests, bytes, digest_length);
            fixture_run(test, "restored preprocessor digests pass", command, 0, "cases: 0 of 1 fail");
        }
        free(changed);
        free(bytes);
    }
}

static void fixture_ir_controls(FixtureTest* test)
{
    char root[PATH_MAX];
    char source[PATH_MAX];
    char destination[PATH_MAX];
    bool valid = test_fixture_path(root, sizeof(root), test->work, "ir-preproc") &&
                 test_fixture_path(source, sizeof(source), test->repo, "tests/preprocessor/named-ucns") &&
                 test_fixture_path(destination, sizeof(destination), root, "preprocessor/named-ucns");
    fixture_expect(test, valid, "construct preprocessor control root");
    valid = valid && fixture_copy_tree(test, source, destination);
    if (valid)
    {
        fixture_pp_digest_controls(test, root);
    }
    if (valid)
    {
        const char* oracle[] = { test->compiler, "test-preprocessor", "--named-ucns-only", "--clang", "--fixtures", root, NULL };
        const char* command[] = { test->compiler, "test-preprocessor", "--named-ucns-only", "--fixtures", root, NULL };
        fixture_run(test, "UCN absolute root outside checkout", oracle, 0, "named UCNs: 0 of 15 cases fail");
        char catalog[PATH_MAX];
        char input[PATH_MAX];
        char expected[PATH_MAX];
        valid = test_fixture_path(catalog, sizeof(catalog), destination, "catalog.tsv") &&
                test_fixture_path(input, sizeof(input), destination, "0001.c") &&
                test_fixture_path(expected, sizeof(expected), destination, "0001.expected");
        fixture_expect(test, valid, "construct UCN control paths");
        size_t catalog_length = 0;
        size_t input_length = 0;
        size_t expected_length = 0;
        char* catalog_bytes = valid ? fixture_read(test, catalog, &catalog_length) : NULL;
        char* input_bytes = valid ? fixture_read(test, input, &input_length) : NULL;
        char* expected_bytes = valid ? fixture_read(test, expected, &expected_length) : NULL;
        valid = valid && catalog_bytes != NULL && input_bytes != NULL && expected_bytes != NULL;
        if (valid)
        {
            static const char wrong[] = "1:1: error: forged diagnostic\n";
            fixture_write(test, expected, wrong, sizeof(wrong) - 1);
            fixture_run(test, "UCN changed diagnostic golden is live", command, 1, "not the diagnostics written for it");
            fixture_write(test, expected, expected_bytes, expected_length);
            static const char malformed[] = "buster-fixtures v1 preprocessor-named-ucns\n0001\tbogus-standard\t-\t-\tbad standard\n";
            fixture_write(test, catalog, malformed, sizeof(malformed) - 1);
            fixture_run(test, "UCN malformed metadata fails", command, 1, "invalid case metadata");
            static const char empty[] = "buster-fixtures v1 preprocessor-named-ucns\n";
            fixture_write(test, catalog, empty, sizeof(empty) - 1);
            fixture_run(test, "UCN empty catalog fails", command, 1, "empty or invalid fixture table");
            fixture_append(test, catalog, catalog_bytes, catalog_length, "0001\tgnu17\t-\t-\tduplicate stable ID\n");
            fixture_run(test, "UCN duplicate stable ID fails", command, 1, "duplicate case ID");
            fixture_write(test, catalog, catalog_bytes, catalog_length);
            // Keep every other row/file valid so argument truncation cannot be masked by an inventory failure.
            static const char first[] = "\n0001\tgnu17\t-\t";
            const char* options = strstr(catalog_bytes, first);
            options = options != NULL ? options + sizeof(first) - 1 : NULL;
            fixture_expect(test, options != NULL && options[0] == '-' && options[1] == '\t', "find the first UCN option field");
            if (options != NULL && options[0] == '-' && options[1] == '\t')
            {
                const size_t prefix = (size_t)(options - catalog_bytes);
                char oversized[16385];
                for (unsigned mode = 0; mode < 2; mode += 1)
                {
                    size_t added = 0;
                    if (mode == 0)
                    {
                        for (unsigned i = 0; i < 30; i += 1)
                        {
                            memcpy(oversized + added, "-w ", 3);
                            added += 3;
                        }
                        static const char rejected[] = "-funknown-runtime-fixture-option";
                        memcpy(oversized + added, rejected, sizeof(rejected) - 1);
                        added += sizeof(rejected) - 1;
                    }
                    else
                    {
                        memset(oversized, 'a', sizeof(oversized));
                        oversized[0] = '-';
                        oversized[1] = 'W';
                        added = sizeof(oversized);
                    }
                    const size_t length = catalog_length - 1 + added;
                    char* changed_catalog = malloc(length);
                    fixture_expect(test, changed_catalog != NULL, "allocate an over-bound UCN option catalog");
                    if (changed_catalog != NULL)
                    {
                        memcpy(changed_catalog, catalog_bytes, prefix);
                        memcpy(changed_catalog + prefix, oversized, added);
                        memcpy(changed_catalog + prefix + added, options + 1, catalog_length - prefix - 1);
                        fixture_write(test, catalog, changed_catalog, length);
                        fixture_run(test, mode == 0 ? "UCN excessive option counts fail without truncation" : "UCN excessive option bytes fail without truncation",
                                    command, 1, "fixture arguments exceed their bound");
                    }
                    free(changed_catalog);
                }
                fixture_write(test, catalog, catalog_bytes, catalog_length);
            }
            fixture_remove(test, input);
            fixture_run(test, "UCN missing source fails", command, 1, "can't read");
            fixture_write(test, input, input_bytes, input_length);
            char orphan[PATH_MAX];
            valid = test_fixture_path(orphan, sizeof(orphan), destination, "unlisted.raw");
            fixture_expect(test, valid, "construct UCN orphan path");
            if (valid)
            {
                fixture_write(test, orphan, "orphan", 6);
                fixture_run(test, "UCN orphan file fails", command, 1, "is in no catalog line");
                fixture_remove(test, orphan);
            }
            char added_input[PATH_MAX];
            char added_expected[PATH_MAX];
            valid = test_fixture_path(added_input, sizeof(added_input), destination, "runtime-added.c") &&
                    test_fixture_path(added_expected, sizeof(added_expected), destination, "runtime-added.expected");
            fixture_expect(test, valid, "construct added UCN paths");
            if (valid)
            {
                fixture_write(test, added_input, input_bytes, input_length);
                fixture_write(test, added_expected, expected_bytes, expected_length);
                fixture_append(test, catalog, catalog_bytes, catalog_length, "runtime-added\tgnu17\t-\t-\truntime fixture addition\n");
                fixture_run(test, "UCN data-only added case executes with Clang", oracle, 0, "named UCNs: 0 of 16 cases fail");
                fixture_write(test, catalog, catalog_bytes, catalog_length);
                fixture_remove(test, added_input);
                fixture_remove(test, added_expected);
            }
            fixture_run(test, "UCN restored data passes", command, 0, "named UCNs: 0 of 15 cases fail");
        }
        free(catalog_bytes);
        free(input_bytes);
        free(expected_bytes);
    }

    valid = test_fixture_path(source, sizeof(source), test->repo, "tests/ir") && test_fixture_path(destination, sizeof(destination), root, "ir");
    fixture_expect(test, valid, "construct IR control root");
    valid = valid && fixture_copy_tree(test, source, destination);
    if (valid)
    {
        const char* command[] = { test->compiler, "test-ir-parse", "--fixtures", root, NULL };
        fixture_run(test, "IR absolute root outside checkout", command, 0, "53 invalid fixtures");
        char digest[PATH_MAX];
        char catalog[PATH_MAX];
        char input[PATH_MAX];
        valid = test_fixture_path(digest, sizeof(digest), destination, "parser-digest.expected") &&
                test_fixture_path(catalog, sizeof(catalog), destination, "invalid/catalog.tsv") &&
                test_fixture_path(input, sizeof(input), destination, "invalid/0001.ir");
        fixture_expect(test, valid, "construct IR contract paths");
        size_t digest_length = 0;
        size_t catalog_length = 0;
        size_t input_length = 0;
        char* digest_bytes = valid ? fixture_read(test, digest, &digest_length) : NULL;
        char* catalog_bytes = valid ? fixture_read(test, catalog, &catalog_length) : NULL;
        char* input_bytes = valid ? fixture_read(test, input, &input_length) : NULL;
        valid = valid && digest_bytes != NULL && catalog_bytes != NULL && input_bytes != NULL;
        if (valid)
        {
            char wrong_digest[18];
            valid = digest_length == 17;
            fixture_expect(test, valid, "IR digest has canonical byte length");
            if (valid)
            {
                memcpy(wrong_digest, digest_bytes, digest_length);
                wrong_digest[0] = wrong_digest[0] == '0' ? '1' : '0';
                fixture_write(test, digest, wrong_digest, digest_length);
                fixture_run(test, "IR changed frozen expectation is live", command, 1, "the parses' results have their frozen digest");
            }
            fixture_write(test, digest, "bad\n", 4);
            fixture_run(test, "IR malformed frozen expectation fails", command, 1, "sixteen lowercase hexadecimal digits");
            fixture_remove(test, digest);
            fixture_run(test, "IR missing frozen expectation fails", command, 1, "sixteen lowercase hexadecimal digits");
            fixture_write(test, digest, digest_bytes, digest_length);
            static const char empty[] = "buster-fixtures v1 ir-parser-cases\n";
            fixture_write(test, catalog, empty, sizeof(empty) - 1);
            fixture_run(test, "IR empty contract catalog fails", command, 1, "names no case");
            static const char malformed[] = "buster-fixtures v1 ir-parser-cases\n0001\t0001.ir\tend\t-\t99999999999999999999999999\t0\n";
            fixture_write(test, catalog, malformed, sizeof(malformed) - 1);
            fixture_run(test, "IR malformed contract metadata fails", command, 1, "malformed metadata");
            fixture_append(test, catalog, catalog_bytes, catalog_length, "0001\t0001.ir\tend\t-\t0\t0\n");
            fixture_run(test, "IR duplicate contract ID fails", command, 1, "duplicate ID/file");
            fixture_write(test, catalog, catalog_bytes, catalog_length);
            fixture_write(test, input, "function", 8);
            fixture_run(test, "IR changed rejection input is live", command, 1, "invalid fixture returned the wrong error category");
            fixture_remove(test, input);
            fixture_run(test, "IR missing contract input fails", command, 1, "unreadable case");
            fixture_write(test, input, input_bytes, input_length);
            char orphan[PATH_MAX];
            valid = test_fixture_path(orphan, sizeof(orphan), destination, "invalid/unlisted.raw");
            fixture_expect(test, valid, "construct IR orphan path");
            if (valid)
            {
                fixture_write(test, orphan, "orphan", 6);
                fixture_run(test, "IR unlisted non-IR file fails", command, 1, "is in no catalog line");
                fixture_remove(test, orphan);
            }
            fixture_run(test, "IR restored contract data passes", command, 0, "0 failures");
        }
        free(digest_bytes);
        free(catalog_bytes);
        free(input_bytes);

        char canonical_catalog[PATH_MAX];
        char canonical_source[PATH_MAX];
        char added[PATH_MAX];
        valid = test_fixture_path(canonical_catalog, sizeof(canonical_catalog), destination, "catalog.tsv") &&
                test_fixture_path(canonical_source, sizeof(canonical_source), destination, "arithmetic.ir") &&
                test_fixture_path(added, sizeof(added), destination, "runtime-added.ir");
        fixture_expect(test, valid, "construct added canonical IR paths");
        size_t canonical_length = 0;
        size_t source_length = 0;
        char* canonical_bytes = valid ? fixture_read(test, canonical_catalog, &canonical_length) : NULL;
        char* source_bytes = valid ? fixture_read(test, canonical_source, &source_length) : NULL;
        if (valid && canonical_bytes != NULL && source_bytes != NULL)
        {
            fixture_write(test, added, source_bytes, source_length);
            fixture_append(test, canonical_catalog, canonical_bytes, canonical_length, "runtime-added\truntime-added.ir\truntime fixture addition\n");
            // Count the added row and the current catalog's data rows. The first line is the version header.
            size_t rows = 1;
            const char* const end = canonical_bytes + canonical_length;
            for (const char* row = memchr(canonical_bytes, '\n', canonical_length); row != NULL && ++row < end;
                 row = memchr(row, '\n', (size_t)(end - row)))
            {
                rows += *row != '#' && *row != '\n';
            }
            char expected[64];
            snprintf(expected, sizeof(expected), "IR parser: %zu canonical fixtures", rows);
            fixture_run(test, "IR data-only canonical addition executes", command, 0, expected);
            fixture_write(test, canonical_catalog, canonical_bytes, canonical_length);
            fixture_remove(test, added);
        }
        free(canonical_bytes);
        free(source_bytes);
    }
}
// The v2 program catalog resolves shared sources through the explicit root. A narrow CET copy exercises the
// same loader as the larger program suites without requiring an unrelated warning inventory in the private tree.
static void fixture_driver_controls(FixtureTest* test)
{
    char root[PATH_MAX];
    char source[PATH_MAX];
    char shared[PATH_MAX];
    char policies[PATH_MAX];
    char catalog[PATH_MAX];
    char policy_catalog[PATH_MAX];
    const bool paths = test_fixture_path(root, sizeof(root), test->work, "driver-catalog-fixtures") &&
                       test_fixture_path(source, sizeof(source), test->repo, "tests/driver/programs/cf-protection") &&
                       test_fixture_path(shared, sizeof(shared), root, "shared/cet") &&
                       test_fixture_path(policies, sizeof(policies), root, "driver/programs/cf-protection-policy") &&
                       test_fixture_path(catalog, sizeof(catalog), root, "driver/programs/cf-protection/catalog.tsv") &&
                       test_fixture_path(policy_catalog, sizeof(policy_catalog), policies, "catalog.tsv");
    bool ready = paths && fixture_copy_tree(test, source, shared);
    ready = ready && test_fixture_path(source, sizeof(source), test->repo, "tests/driver/programs/cf-protection-policy") &&
            fixture_copy_tree(test, source, policies);
    fixture_expect(test, ready, "prepare a narrow driver catalog tree outside the checkout");
    const char* const arguments[] = {
        test->compiler, "test-checker-driver", "--clang", test->clang, "--executable", test->compiler,
        "--fixtures", root, "--cf-protection-only", NULL,
    };
    static const char shared_catalog[] =
        "buster-fixtures v2 driver-programs\trules.c\tprogram.c\n"
        "shared-inputs\t@shared/cet/rules.c\t@shared/cet/program.c\n";
    if (ready)
    {
        ready = fixture_write(test, catalog, shared_catalog, sizeof(shared_catalog) - 1);
        if (ready)
        {
            fixture_run(test, "driver v2 shared inputs run under the chosen root", arguments, 0, "0 failures");
        }
    }
    static const struct
    {
        const char* label;
        const char* row;
    } invalid[] = {
        { "driver shared references reject parent traversal", "bad\t@../rules.c\t@shared/cet/program.c\n" },
        { "driver shared references reject an absolute path", "bad\t@/shared/cet/rules.c\t@shared/cet/program.c\n" },
        { "driver shared references reject an empty name", "bad\t@\t@shared/cet/program.c\n" },
        { "driver shared references reject an empty component", "bad\t@shared//cet/rules.c\t@shared/cet/program.c\n" },
        { "driver catalogs reject duplicate IDs", "same\t@shared/cet/rules.c\t@shared/cet/program.c\nsame\t@shared/cet/rules.c\t@shared/cet/program.c\n" },
        { "driver catalogs reject an empty inventory", "" },
        { "driver catalogs require every shared source", "missing\t@shared/cet/missing.c\t@shared/cet/program.c\n" },
    };
    for (size_t i = 0; ready && i < sizeof(invalid) / sizeof(invalid[0]); i += 1)
    {
        char text[512];
        const int size = snprintf(text, sizeof(text), "buster-fixtures v2 driver-programs\trules.c\tprogram.c\n%s", invalid[i].row);
        ready = size >= 0 && (size_t)size < sizeof(text) && fixture_write(test, catalog, text, (size_t)size);
        if (ready)
        {
            fixture_run(test, invalid[i].label, arguments, 1, "driver program catalog is complete and well formed");
        }
    }
    char extra[PATH_MAX];
    ready = ready && fixture_write(test, catalog, shared_catalog, sizeof(shared_catalog) - 1) &&
            test_fixture_path(extra, sizeof(extra), root, "driver/programs/cf-protection/orphan.c");
    if (ready)
    {
        static const char orphan[] = "int orphan;\n";
        ready = fixture_write(test, extra, orphan, sizeof(orphan) - 1);
        if (ready)
        {
            fixture_run(test, "driver catalogs reject an unlisted local source", arguments, 1, "driver program catalog is complete and well formed");
            ready = fixture_remove(test, extra);
        }
    }
    // Append a new source/expectation pair without rebuilding the compiler, then change only its expectation.
    // Success followed by a policy-specific failure proves that the added row and its metadata were consumed.
    char expected[PATH_MAX];
    ready = ready && test_fixture_path(source, sizeof(source), policies, "0001.c") &&
            test_fixture_path(extra, sizeof(extra), policies, "added.c") && fixture_copy_tree(test, source, extra) &&
            test_fixture_path(source, sizeof(source), policies, "0001.expected") &&
            test_fixture_path(expected, sizeof(expected), policies, "added.expected") && fixture_copy_tree(test, source, expected);
    if (ready)
    {
        static const char addition[] = "added-policy\tadded.c\tadded.expected\n";
        FILE* file = fopen(policy_catalog, "ab");
        ready = file != NULL && fwrite(addition, 1, sizeof(addition) - 1, file) == sizeof(addition) - 1;
        if (file != NULL)
        {
            ready = fclose(file) == 0 && ready;
        }
        fixture_expect(test, ready, "append a driver policy row without rebuilding");
        if (ready)
        {
            fixture_run(test, "driver catalogs accept an added source and expectation pair", arguments, 0, "0 failures");
            static const char bad_policy[] = "-not-a-cet-option\t1\tnot yet supported\tadded runtime policy\n";
            ready = fixture_write(test, expected, bad_policy, sizeof(bad_policy) - 1);
            if (ready)
            {
                fixture_run(test, "driver policy metadata rejects an unknown option", arguments, 1,
                            "CET's policy specifies a supported option and exit status");
            }
            static const char bad_fields[] = "-fcf-protection\t1\tnot yet supported\n";
            ready = ready && fixture_write(test, expected, bad_fields, sizeof(bad_fields) - 1);
            if (ready)
            {
                fixture_run(test, "driver expectation metadata requires every field", arguments, 1,
                            "the driver expectation row has every required field");
            }
        }
    }
}
static bool fixture_checker_copy(FixtureTest* test, const char* root, const char* relative)
{
    char source[PATH_MAX];
    char destination[PATH_MAX];
    char from_tests[PATH_MAX];
    const bool valid = test_fixture_path(from_tests, sizeof(from_tests), test->repo, "tests") &&
                       test_fixture_path(source, sizeof(source), from_tests, relative) &&
                       test_fixture_path(destination, sizeof(destination), root, relative) && fixture_copy_tree(test, source, destination);
    fixture_expect(test, valid, "copy checker fixture inventory under the selected root");
    return valid;
}

// The attribute recovery focus also reads path-valued suppress-recovery rows. Its private rows keep this contract
// small while exercising a real source from the selected root, including failure and recovery after that file moves.
static void fixture_driver_root_controls(FixtureTest* test)
{
    char root[PATH_MAX];
    char original[PATH_MAX];
    char path[PATH_MAX];
    bool ready = test_fixture_path(root, sizeof(root), test->work, "driver-root-fixtures") &&
                 test_fixture_path(original, sizeof(original), test->repo, "tests") && fixture_copy_tree(test, original, root);
    fixture_expect(test, ready, "copy driver fixtures for an explicit-root recovery contract");
    static const char suppress[] = "driver/suppress_macro_delimiters.c 1 0 0\n";
    static const char label[] = "0 1 1\n";
    static const char declaration[] = "0 1 1\n";
    ready = ready && test_fixture_path(path, sizeof(path), root, "driver/suppress_recovery.cases") &&
            fixture_write(test, path, suppress, sizeof(suppress) - 1) &&
            test_fixture_path(path, sizeof(path), root, "driver/suppress_label_order.cases") &&
            fixture_write(test, path, label, sizeof(label) - 1) &&
            test_fixture_path(path, sizeof(path), root, "driver/attribute_declarations.cases") &&
            fixture_write(test, path, declaration, sizeof(declaration) - 1);
    const char* const arguments[] = {
        test->compiler, "test-checker-driver", "--clang", test->clang, "--executable", test->compiler,
        "--fixtures", root, "--attribute-recovery-only", "--in-process", NULL,
    };
    if (ready)
    {
        fixture_run(test, "driver recovery catalog paths resolve under the explicit root", arguments, 0, "0 failures");
        ready = test_fixture_path(path, sizeof(path), root, "driver/suppress_macro_delimiters.c") && fixture_remove(test, path);
        if (ready)
        {
            fixture_run(test, "driver recovery requires each source beneath the selected root", arguments, 1,
                        "suppress recovery runtime fixture is available and bounded");
            ready = test_fixture_path(original, sizeof(original), test->repo, "tests/driver/suppress_macro_delimiters.c") &&
                    fixture_copy_tree(test, original, path);
            if (ready)
            {
                fixture_run(test, "driver recovery passes after its explicit-root source is restored", arguments, 0, "0 failures");
            }
        }
    }
}

static void fixture_warning_controls(FixtureTest* test, const char* root)
{
    char catalog[PATH_MAX];
    char source[PATH_MAX];
    char companion[PATH_MAX];
    char auxiliary[PATH_MAX];
    static const char program[] = "#include <companion.h>\nint f(void) { return fixture_value; }\n";
    static const char header[] = "enum { fixture_value = 17 };\n";
    static const char plain[] = "int f(void) { return 1; }\n";
    static const char warning_header[] = "buster-fixtures v3 checker-warnings\n";
    static const char good[] = "buster-fixtures v3 checker-warnings\nfirst\tcase.c\t0\t0\t0\t-\t-isystem @suite\tcompanion.h\tselected companion\n";
    static const char auxiliary_catalog[] = "buster-fixtures v2 checker-warnings\nfirst\tcase.c\t0\t0\t0\t-\t-\tplain source\n";
    bool valid = test_fixture_path(catalog, sizeof(catalog), root, "driver/record-extensions/catalog.tsv") &&
                 test_fixture_path(source, sizeof(source), root, "driver/record-extensions/case.c") &&
                 test_fixture_path(companion, sizeof(companion), root, "driver/record-extensions/companion.h") &&
                 test_fixture_path(auxiliary, sizeof(auxiliary), root, "driver/warnings/gnu-empty-struct/catalog.tsv");
    valid = valid && fixture_write(test, catalog, good, sizeof(good) - 1) && fixture_write(test, source, program, sizeof(program) - 1) &&
            fixture_write(test, companion, header, sizeof(header) - 1) &&
            fixture_write(test, auxiliary, auxiliary_catalog, sizeof(auxiliary_catalog) - 1) &&
            test_fixture_path(auxiliary, sizeof(auxiliary), root, "driver/warnings/gnu-empty-struct/case.c") &&
            fixture_write(test, auxiliary, plain, sizeof(plain) - 1);
    fixture_expect(test, valid, "prepare warning catalogs with a selected system header");
    const char* const command[] = {
        test->compiler, "test-checker-driver", "--record-extensions-only", "--in-process", "--fixtures", root,
        "--clang", test->clang, "--executable", test->compiler, NULL,
    };
    if (valid)
    {
        fixture_run(test, "warning v3 resolves companion headers outside the checkout", command, 0, "warning sweep: 2 fixtures");
        static const char added[] = "second\tcase.c\t0\t0\t0\tc23\t-isystem @suite\tcompanion.h\tadded dialect\n";
        fixture_append(test, catalog, good, sizeof(good) - 1, added);
        fixture_run(test, "warning v3 executes an added row without rebuilding", command, 0, "warning sweep: 3 fixtures");
        fixture_write(test, catalog, good, sizeof(good) - 1);
        static const char changed[] = "#error selected companion was changed\n";
        fixture_write(test, companion, changed, sizeof(changed) - 1);
        fixture_run(test, "warning v3 consumes the selected companion bytes", command, 1, NULL);
        fixture_write(test, companion, header, sizeof(header) - 1);
        fixture_remove(test, companion);
        fixture_run(test, "warning v3 requires every companion file", command, 1, "runtime warning catalog is complete and well formed");
        fixture_write(test, companion, header, sizeof(header) - 1);
        static const struct
        {
            const char* label;
            const char* row;
        } invalid[] = {
            { "warning catalogs reject signed decimal metadata", "bad\tcase.c\t+0\t0\t0\t-\t-isystem @suite\tcompanion.h\tbad count\n" },
            { "warning catalogs reject duplicate flags", "bad\tcase.c\t0\t0\t0\tc23,c23\t-isystem @suite\tcompanion.h\tbad flags\n" },
            { "warning catalogs reject duplicate companion names", "bad\tcase.c\t0\t0\t0\t-\t-isystem @suite\tcompanion.h,companion.h\tbad headers\n" },
            { "warning catalogs reject companion traversal", "bad\tcase.c\t0\t0\t0\t-\t-isystem @suite\t../companion.h\tbad path\n" },
            { "warning catalogs require companions for a suite include path", "bad\tcase.c\t0\t0\t0\t-\t-isystem @suite\t-\tbad include\n" },
            { "warning catalogs reject extra columns", "bad\tcase.c\t0\t0\t0\t-\t-isystem @suite\tcompanion.h\tbad columns\textra\n" },
        };
        for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i += 1)
        {
            fixture_append(test, catalog, warning_header, sizeof(warning_header) - 1, invalid[i].row);
            fixture_run(test, invalid[i].label, command, 1, "runtime warning catalog is complete and well formed");
        }
        fixture_write(test, catalog, good, sizeof(good) - 1);
        valid = test_fixture_path(auxiliary, sizeof(auxiliary), root, "driver/record-extensions/orphan.c");
        fixture_expect(test, valid, "construct an unlisted warning source path");
        if (valid)
        {
            fixture_write(test, auxiliary, plain, sizeof(plain) - 1);
            fixture_run(test, "warning catalogs reject an unlisted source", command, 1, "is in no catalog line");
            fixture_remove(test, auxiliary);
        }
    }

    valid = test_fixture_path(catalog, sizeof(catalog), root, "driver/auto-qualifiers/catalog.tsv") &&
            test_fixture_path(source, sizeof(source), root, "driver/auto-qualifiers/case.c");
    valid = valid && fixture_write(test, catalog, auxiliary_catalog, sizeof(auxiliary_catalog) - 1) &&
            fixture_write(test, source, plain, sizeof(plain) - 1);
    fixture_expect(test, valid, "prepare a warning v2 inventory");
    const char* const auto_command[] = {
        test->compiler, "test-checker-driver", "--auto-qualifiers-only", "--in-process", "--fixtures", root,
        "--clang", test->clang, "--executable", test->compiler, NULL,
    };
    if (valid)
    {
        fixture_run(test, "warning v2 uses its selected root", auto_command, 0, "warning sweep: 1 fixtures");
        static const char added[] = "second\tcase.c\t0\t0\t0\tc23\t-\tadded dialect\n";
        fixture_append(test, catalog, auxiliary_catalog, sizeof(auxiliary_catalog) - 1, added);
        fixture_run(test, "warning v2 executes an added policy row", auto_command, 0, "warning sweep: 2 fixtures");
        static const char duplicate[] = "first\tcase.c\t0\t0\t0\tc23\t-\tduplicate identity\n";
        fixture_append(test, catalog, auxiliary_catalog, sizeof(auxiliary_catalog) - 1, duplicate);
        fixture_run(test, "warning v2 rejects duplicate stable IDs", auto_command, 1, "duplicate case ID");
        static const char wrong[] = "buster-fixtures v2 checker-warnings\nfirst\tcase.c\t1\t0\t0\t-\t-\twrong count\n";
        fixture_write(test, catalog, wrong, sizeof(wrong) - 1);
        fixture_run(test, "warning v2 consumes changed expected counts", auto_command, 1, NULL);
        fixture_write(test, catalog, auxiliary_catalog, sizeof(auxiliary_catalog) - 1);
    }
}

static void fixture_native_controls(FixtureTest* test, const char* root)
{
    bool valid = fixture_checker_copy(test, root, "checker/constant-conversions") &&
                 fixture_checker_copy(test, root, "checker/branch-conditions") &&
                 fixture_checker_copy(test, root, "checker/programs/implicit-int");
    const char* const command[] = {
        test->compiler, "test-checker", "--program-fixtures", "--fixtures", root, "--clang", test->clang, NULL,
    };
    if (valid)
    {
        fixture_run(test, "native named fixtures and C89 pairs run outside the checkout", command, 0, "0 failures");
        static const char* const suites[] = { "checker/constant-conversions", "checker/branch-conditions", "checker/programs/implicit-int" };
        char catalogs[3][PATH_MAX];
        char sources[3][PATH_MAX];
        char* bytes[3] = { 0 };
        size_t sizes[3] = { 0 };
        static const char program[] = "int tested(int n) { return n + 1; }\n";
        for (size_t i = 0; valid && i < 3; i += 1)
        {
            char directory[PATH_MAX];
            valid = test_fixture_path(directory, sizeof(directory), root, suites[i]) &&
                    test_fixture_path(catalogs[i], sizeof(catalogs[i]), directory, "catalog.tsv") &&
                    test_fixture_path(sources[i], sizeof(sources[i]), directory, "runtime-added.c");
            bytes[i] = valid ? fixture_read(test, catalogs[i], sizes + i) : NULL;
            valid = valid && bytes[i] != NULL && fixture_write(test, sources[i], program, sizeof(program) - 1);
            const char* row = i == 2 ? "runtime-added\truntime-added.c\tgnu89\truntime-added\n" : "runtime-added\truntime-added.c\t-\truntime-added\n";
            valid = valid && fixture_append(test, catalogs[i], bytes[i], sizes[i], row);
        }
        fixture_expect(test, valid, "append ordinary native programs to all three catalogs");
        if (valid)
        {
            fixture_run(test, "native catalogs execute extra cases beyond their original counts", command, 0, "0 failures");
            static const char rejected[] = "int tested(int n) { return missing_identifier; }\n";
            fixture_write(test, sources[0], rejected, sizeof(rejected) - 1);
            fixture_run(test, "native catalog consumes an added program's bytes", command, 1, "runtime-added");
            fixture_write(test, sources[0], program, sizeof(program) - 1);
            static const char* const required[] = { "constant-conversion-code", "branch-condition-shapes" };
            for (size_t i = 0; i < 2; i += 1)
            {
                char* name = strstr(bytes[i], required[i]);
                fixture_expect(test, name != NULL, "find a required native structural case");
                if (name != NULL)
                {
                    const char original = name[0];
                    name[0] = 'X';
                    fixture_append(test, catalogs[i], bytes[i], sizes[i], "runtime-added\truntime-added.c\t-\truntime-added\n");
                    fixture_run(test, "native catalogs retain their required named structural coverage", command, 1, "occurs exactly once (0 found)");
                    name[0] = original;
                    fixture_append(test, catalogs[i], bytes[i], sizes[i], "runtime-added\truntime-added.c\t-\truntime-added\n");
                }
            }
        }
        for (size_t i = 0; i < 3; i += 1)
        {
            free(bytes[i]);
        }
    }
}

static void fixture_digest_controls(FixtureTest* test, const char* root)
{
    bool valid = fixture_checker_copy(test, root, "checker/float-constants") &&
                 fixture_checker_copy(test, root, "checker/source-digests.tsv") &&
                 fixture_checker_copy(test, root, "checker/stream-digests.tsv");
    const char* const command[] = {
        test->compiler, "test-checker", "--program-float-constants", "--fixtures", root, "--clang", test->clang, NULL,
    };
    if (valid)
    {
        fixture_run(test, "checker reads selected source and stream digests outside the checkout", command, 0, "recorded digests:");
        static const char* const tables[] = { "checker/source-digests.tsv", "checker/stream-digests.tsv" };
        for (size_t i = 0; valid && i < 2; i += 1)
        {
            char path[PATH_MAX];
            valid = test_fixture_path(path, sizeof(path), root, tables[i]);
            size_t length = 0;
            char* original = valid ? fixture_read(test, path, &length) : NULL;
            char* changed = original != NULL ? malloc(length) : NULL;
            valid = valid && original != NULL && changed != NULL;
            fixture_expect(test, valid, "prepare selected digest mutation");
            if (valid)
            {
                memcpy(changed, original, length);
                bool digest = false;
                for (size_t at = 0; at < length; at += 1)
                {
                    if (changed[at] == '\t')
                    {
                        digest = true;
                    }
                    else if (changed[at] == '\n')
                    {
                        digest = false;
                    }
                    else if (digest && changed[at] != '-')
                    {
                        changed[at] = '0';
                    }
                }
                fixture_write(test, path, changed, length);
                fixture_run(test, i == 0 ? "selected source digest mutation fails" : "selected stream digest mutation fails", command, 1,
                            "aren't their recorded digest");
                fixture_write(test, path, original, length);
                fixture_remove(test, path);
                fixture_run(test, i == 0 ? "selected source digest file is required" : "selected stream digest file is required", command, 1,
                            "no recorded digest");
                fixture_write(test, path, original, length);
            }
            free(changed);
            free(original);
        }
    }
}

static void fixture_malformed_controls(FixtureTest* test, const char* root)
{
    char source[PATH_MAX];
    char destination[PATH_MAX];
    char catalog[PATH_MAX];
    bool valid = test_fixture_path(source, sizeof(source), test->repo, "tests/checker/parse-recovery/malformed-calls/malformed-calls.c") &&
                 test_fixture_path(destination, sizeof(destination), root, "checker/parse-recovery/malformed-calls/malformed-calls.c") &&
                 test_fixture_path(catalog, sizeof(catalog), root, "checker/parse-recovery/malformed-calls/catalog.tsv") &&
                 fixture_copy_tree(test, source, destination);
    static const char good[] = "buster-fixtures v1 malformed-calls\n13\tmalformed-calls.c\t0\t1\t2\n";
    valid = valid && fixture_write(test, catalog, good, sizeof(good) - 1);
    fixture_expect(test, valid, "prepare a narrow malformed-call inventory with the original structural source");
    const char* const command[] = {
        test->compiler, "test-checker-driver", "--malformed-calls-only", "--in-process", "--fixtures", root,
        "--clang", test->clang, "--executable", test->compiler, NULL,
    };
    if (valid)
    {
        fixture_run(test, "malformed calls resolve source and inventory from the selected root", command, 0, "0 failures");
        static const char duplicate[] = "13\tmalformed-calls.c\t0\t1\t2\n";
        fixture_append(test, catalog, good, sizeof(good) - 1, duplicate);
        fixture_run(test, "malformed calls reject duplicate numeric identities", command, 1, "invalid or duplicate case metadata");
        static const char bad[] = "buster-fixtures v1 malformed-calls\n13\tmalformed-calls.c\t0\t1\t8\n";
        fixture_write(test, catalog, bad, sizeof(bad) - 1);
        fixture_run(test, "malformed calls reject unsupported dialect bits", command, 1, "invalid or duplicate case metadata");
        fixture_write(test, catalog, good, sizeof(good) - 1);
        char extra[PATH_MAX];
        valid = test_fixture_path(extra, sizeof(extra), root, "checker/parse-recovery/malformed-calls/added.c");
        fixture_expect(test, valid, "construct added malformed-call source path");
        if (valid)
        {
            static const char accepted[] = "int f(void) { return 1; }\n";
            fixture_write(test, extra, accepted, sizeof(accepted) - 1);
            fixture_run(test, "malformed calls reject unlisted input", command, 1, "is in no catalog line");
            fixture_append(test, catalog, good, sizeof(good) - 1, "999\tadded.c\t0\t1\t2\n");
            fixture_run(test, "malformed calls execute an added source and metadata row", command, 0, "0 failures");
            static const char rejected[] = "int f(void) { return after_runtime_added; }\n";
            fixture_write(test, extra, rejected, sizeof(rejected) - 1);
            fixture_run(test, "malformed calls consume added source bytes and expected acceptance", command, 1,
                        "clang status and independent later-error count");
        }
    }
}

static void fixture_checker_controls(FixtureTest* test)
{
    char root[PATH_MAX];
    const bool valid = test_fixture_path(root, sizeof(root), test->work, "checker");
    fixture_expect(test, valid, "construct checker control root");
    if (valid)
    {
        fixture_warning_controls(test, root);
        fixture_native_controls(test, root);
        fixture_digest_controls(test, root);
        fixture_malformed_controls(test, root);
    }
}
