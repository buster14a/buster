// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original bounded qualification launcher. Run one mode per exclusive.sh acquisition; this never takes or
// bypasses the host lock itself. Each checker command is pinned to CPU 2 and bounded to two minutes. Optional
// one-based shard/count arguments partition a frozen inventory exactly once across separate lock acquisitions.
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static int run(char* const* arguments, const char* directory)
{
    printf("cwd=%s\nargv:", directory != NULL ? directory : "inherited");
    for (unsigned i = 0; arguments[i] != NULL; i += 1)
    {
        printf(" [%s]", arguments[i]);
    }
    putchar('\n');
    fflush(stdout);
    const pid_t child = fork();
    if (child == 0)
    {
        if (directory != NULL && chdir(directory) != 0)
        {
            _exit(126);
        }
        execvp(arguments[0], arguments);
        _exit(127);
    }
    int status = 0;
    pid_t waited = -1;
    do
    {
        waited = child > 0 ? waitpid(child, &status, 0) : -1;
    }
    while (waited < 0 && errno == EINTR);
    return waited == child && WIFEXITED(status) ? WEXITSTATUS(status) : 125;
}

static bool identity(const char* path, const char* expected)
{
    int descriptors[2];
    bool valid = pipe(descriptors) == 0;
    const pid_t child = valid ? fork() : -1;
    if (child == 0)
    {
        close(descriptors[0]);
        if (dup2(descriptors[1], STDOUT_FILENO) < 0)
        {
            _exit(126);
        }
        close(descriptors[1]);
        execlp("sha256sum", "sha256sum", path, (char*)NULL);
        _exit(127);
    }
    char digest[65] = { 0 };
    if (valid)
    {
        close(descriptors[1]);
        FILE* stream = fdopen(descriptors[0], "r");
        valid = stream != NULL && fread(digest, 1, 64, stream) == 64;
        if (stream != NULL)
        {
            while (fgetc(stream) != EOF)
            {
            }
            valid = fclose(stream) == 0 && valid;
        }
        else
        {
            close(descriptors[0]);
        }
    }
    int status = 0;
    pid_t waited = -1;
    do
    {
        waited = child > 0 ? waitpid(child, &status, 0) : -1;
    }
    while (waited < 0 && errno == EINTR);
    valid = valid && waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0 && strcmp(digest, expected) == 0;
    printf("identity path=%s sha256=%s expected=%s match=%u\n", path, digest, expected, valid);
    return valid;
}

static bool c_manifest(const char* source, const char* destination)
{
    FILE* input = fopen(source, "r");
    FILE* output = fopen(destination, "wx");
    bool valid = input != NULL && output != NULL;
    char* line = NULL;
    size_t capacity = 0;
    ssize_t length = 0;
    unsigned count = 0;
    while (valid && (length = getline(&line, &capacity, input)) >= 0)
    {
        while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r'))
        {
            line[--length] = 0;
        }
        if (length > 2 && strcmp(line + length - 2, ".c") == 0)
        {
            valid = fprintf(output, "%s\n", line) > 0;
            count += 1;
        }
    }
    valid = valid && count == 2315 && !ferror(input);
    if (input != NULL)
    {
        valid = fclose(input) == 0 && valid;
    }
    if (output != NULL)
    {
        valid = fclose(output) == 0 && valid;
    }
    free(line);
    printf("C manifest: count=%u expected=2315 source=%s output=%s\n", count, source, destination);
    return valid;
}

static bool shard_manifest(const char* source, const char* destination, unsigned shard, unsigned shards, unsigned expected)
{
    FILE* input = fopen(source, "r");
    FILE* output = fopen(destination, "wx");
    bool valid = input != NULL && output != NULL && shard != 0 && shard <= shards;
    char* line = NULL;
    size_t capacity = 0;
    ssize_t length = 0;
    unsigned total = 0;
    unsigned written = 0;
    while (valid && (length = getline(&line, &capacity, input)) >= 0)
    {
        while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r'))
        {
            line[--length] = 0;
        }
        valid = length != 0;
        if (valid && total % shards == shard - 1)
        {
            valid = fprintf(output, "%s\n", line) > 0;
            written += 1;
        }
        total += 1;
    }
    valid = valid && total == expected && written != 0 && !ferror(input);
    if (input != NULL)
    {
        valid = fclose(input) == 0 && valid;
    }
    if (output != NULL)
    {
        valid = fclose(output) == 0 && valid;
    }
    free(line);
    printf("shard: index=%u count=%u selected=%u inventory=%u expected=%u source=%s output=%s\n", shard, shards, written, total, expected, source, destination);
    return valid;
}

int main(int argc, char** argv)
{
    bool valid = argc == 7 || argc == 9;
    unsigned shard = 1;
    unsigned shards = 1;
    if (valid && argc == 9)
    {
        char* end = NULL;
        const unsigned long parsed_shard = strtoul(argv[7], &end, 10);
        valid = argv[7][0] != 0 && *end == 0 && parsed_shard != 0 && parsed_shard <= 8983;
        const unsigned long parsed_shards = strtoul(argv[8], &end, 10);
        valid = valid && argv[8][0] != 0 && *end == 0 && parsed_shards >= parsed_shard && parsed_shards <= 8983;
        shard = (unsigned)parsed_shard;
        shards = (unsigned)parsed_shards;
    }
    const char* mode = valid ? argv[1] : "";
    const char* bundle = valid ? argv[4] : "";
    const char* prefix = valid ? argv[6] : "";
    char manifest[4096];
    char source_list[4096];
    char c_list[4096];
    char shard_list[4096];
    char selected[4096];
    char report[4096];
    char sqlite[4096];
    char libc[4096];
    char predefines[4096];
    char headers[3][4096];
    char musl_headers[7][4096];
    char musl_list[4096];
    char musl_config[4096];
    char repository_config[4096];
    char repository_source[4096];
    char repository_include[4096];
    snprintf(manifest, sizeof(manifest), "%s/MANIFESTS.sha256", bundle);
    snprintf(source_list, sizeof(source_list), "%s/config/libc-ch.txt", bundle);
    snprintf(c_list, sizeof(c_list), "%s.c-list", prefix);
    snprintf(shard_list, sizeof(shard_list), "%s.shard.list", prefix);
    snprintf(selected, sizeof(selected), "%s.accepted.list", prefix);
    snprintf(report, sizeof(report), "%s.tsv", prefix);
    snprintf(sqlite, sizeof(sqlite), "%s/sqlite/sqlite3.c", bundle);
    snprintf(libc, sizeof(libc), "%s/libc", bundle);
    snprintf(predefines, sizeof(predefines), "%s/provenance/gnu17-pinned-predefines.txt", bundle);
    snprintf(repository_config, sizeof(repository_config), "%s/config/repository-pinned.args", bundle);
    snprintf(repository_source, sizeof(repository_source), "%s/repository/src/buster/compiler/main.c", bundle);
    snprintf(repository_include, sizeof(repository_include), "%s/repository/src", bundle);
    snprintf(headers[0], sizeof(headers[0]), "%s/headers/usr/lib/clang/23/include", bundle);
    snprintf(headers[1], sizeof(headers[1]), "%s/headers/usr/local/include", bundle);
    snprintf(headers[2], sizeof(headers[2]), "%s/headers/usr/include", bundle);
    static const char* const musl_directories[] =
    {
        "libc/musl/arch/x86_64",
        "libc/musl/arch/generic",
        "musl-generated/obj/src/internal",
        "libc/musl/src/include",
        "libc/musl/src/internal",
        "musl-generated/obj/include",
        "libc/musl/include",
    };
    static const char* const musl_modes[] =
    {
        "musl-ordinary",
        "musl-crt",
        "musl-crt-pic",
        "musl-ldso",
    };
    static const char* const musl_list_hashes[] =
    {
        "bf47f8562fd1df039a81464164df642c0efb7653e7cb91b0be964978929bb4dc",
        "662cff594ce6115ecf5f584085c6d507975b2832107aa24871395ba0870a0305",
        "ef0f00ab940cd141fe4306ed55d363cc6f9ccc44be48bdfc7cb21898d3778b11",
        "ed0112b067aec3e6d3f0514d4c07ff2303a3ff2623c70fdc6490801526a9c3ca",
    };
    static const char* const musl_config_hashes[] =
    {
        "2870486caabcb797e47aa998b91065f8bdfcc60c9ed2c7a24c114ec9bec2fe46",
        "712ce5eb750f58dc9540d32da56f891f5d250e997460e244583a4f89163dafec",
        "570b2f219a841189fb394112aaf863a7c11769c7d514d9b976d469db1da333a6",
        "b0e773fc62a84c22bc9887dc196144b5dd5ae89193adc80ad19b7757d155c9f9",
    };
    unsigned musl = 4;
    bool rejections = strcmp(mode, "libc-rejections") == 0;
    for (unsigned i = 0; i < 4; i += 1)
    {
        char rejected_mode[64];
        snprintf(rejected_mode, sizeof(rejected_mode), "%s-rejections", musl_modes[i]);
        const bool reject = strcmp(mode, rejected_mode) == 0;
        musl = strcmp(mode, musl_modes[i]) == 0 || reject ? i : musl;
        rejections |= reject;
    }
    if (musl < 4)
    {
        snprintf(musl_list, sizeof(musl_list), "%s/config/%s.txt", bundle, musl_modes[musl]);
        snprintf(musl_config, sizeof(musl_config), "%s/config/%s.args", bundle, musl == 0 ? "musl" : musl_modes[musl]);
        snprintf(libc, sizeof(libc), "%s/libc/musl", bundle);
        for (unsigned i = 0; i < 7; i += 1)
        {
            snprintf(musl_headers[i], sizeof(musl_headers[i]), "%s/%s", bundle, musl_directories[i]);
        }
    }
    const bool inventory = musl < 4 || strcmp(mode, "libc") == 0 || strcmp(mode, "libc-all") == 0 || strcmp(mode, "libc-rejections") == 0;
    valid = valid && (argc != 9 || inventory) && identity(manifest, "391e729af6587355050881752558e790299cfe638c7dc7803bd028a8535ebcbd") &&
            identity(argv[3], "939a882527432ec23b094c289e7f170bf2d6ec282e74dde75e31b08602fa3eae") &&
            identity(source_list, "1c1f215dd07bb2235a098e25a7d05ae74b53a65f2939457ffb50a806cf76f904") &&
            identity(predefines, "a39036d8215977b5a205f7fd3c4e7dfe6576b58eaa76a7bb0b0e214a164e8d33") &&
            unsetenv("CPATH") == 0 && unsetenv("C_INCLUDE_PATH") == 0 && setenv("SOURCE_DATE_EPOCH", "1792454400", 1) == 0;
    valid = valid && (musl == 4 || (identity(musl_list, musl_list_hashes[musl]) && identity(musl_config, musl_config_hashes[musl])));
    valid = valid && (strcmp(mode, "own-main") != 0 || identity(repository_config, "eac3c103da50ca516d75db3ca071046dbdb5d26ba12ba9dcf36cc148efaf8a2f"));
    int result = valid ? 0 : 2;
    if (valid && strcmp(mode, "verify") == 0)
    {
        const char* const names[] =
        {
            "sqlite",
            "libc",
            "headers-bundle",
            "musl-generated",
            "repository",
        };
        const char* const hashes[] =
        {
            "9179b7ad7db7366d12a214b562e478ac703e7ec0376f38f4516756bcf7ee0047",
            "bf593a94954f3ed9431dd12e60233ec0229f7226a1fc270a701614a20f86baeb",
            "e307b836448ef20e603a617a4b5023831c72ff5f307f614c50f4f8833088717c",
            "89dbe35f0324cf08d0cb9cf31ecf15092fc20783a9e470e80e5f41b1d48251d3",
            "c8417247adaf7eec721d2fca844d5681e8620c075214649a9f606350567cbd4c",
        };
        for (unsigned i = 0; result == 0 && i < 5; i += 1)
        {
            char path[4096];
            char directory[4096];
            snprintf(path, sizeof(path), "%s/manifests/%s.sha256", bundle, names[i]);
            snprintf(directory, sizeof(directory), "%s/%s", bundle, i == 2 ? "headers" : names[i]);
            char* arguments[] =
            {
                "sha256sum",
                "--check",
                "--quiet",
                path,
                NULL,
            };
            result = identity(path, hashes[i]) ? run(arguments, directory) : 2;
        }
    }
    else if (valid)
    {
        const bool corpus = musl < 4 || strcmp(mode, "libc") == 0 || strcmp(mode, "libc-all") == 0 || rejections ||
                            strcmp(mode, "ctests") == 0 || strcmp(mode, "sqlite") == 0 || strcmp(mode, "own-main") == 0;
        const bool benchmark = strcmp(mode, "bodies") == 0 || strcmp(mode, "scalar") == 0 || strcmp(mode, "vector") == 0 || strcmp(mode, "after") == 0;
        valid = corpus || benchmark;
        if (valid && strcmp(mode, "libc") == 0)
        {
            valid = c_manifest(source_list, c_list);
        }
        const char* inputs = musl < 4 ? musl_list : strcmp(mode, "libc") == 0 ? c_list : source_list;
        if (valid && argc == 9)
        {
            static const unsigned musl_counts[] = { 1587, 1, 2, 1 };
            const unsigned expected = musl < 4 ? musl_counts[musl] : strcmp(mode, "libc") == 0 ? 2315 : 8983;
            valid = shard_manifest(inputs, shard_list, shard, shards, expected);
            inputs = shard_list;
        }
        char* arguments[112];
        unsigned count = 0;
        arguments[count++] = "timeout";
        arguments[count++] = "--signal=TERM";
        arguments[count++] = "--kill-after=5s";
        arguments[count++] = "115s";
        arguments[count++] = "taskset";
        arguments[count++] = "-c";
        arguments[count++] = "2";
        arguments[count++] = argv[2];
        arguments[count++] = corpus ? "test-checker-corpus" : "benchmark-checker-source";
        if (corpus)
        {
            arguments[count++] = "--clang";
            arguments[count++] = argv[3];
            arguments[count++] = "--report";
            arguments[count++] = report;
            arguments[count++] = "--reports";
            arguments[count++] = "20";
            if (strcmp(mode, "libc") == 0 || strcmp(mode, "libc-all") == 0 || rejections || musl < 4)
            {
                arguments[count++] = "--files-from";
                arguments[count++] = (char*)inputs;
                arguments[count++] = "--root";
                arguments[count++] = libc;
                arguments[count++] = rejections ? "--semantic-rejections" : "--select-accepted";
                arguments[count++] = selected;
            }
            else
            {
                arguments[count++] = strcmp(mode, "ctests") == 0 ? argv[5] : strcmp(mode, "own-main") == 0 ? repository_source : sqlite;
                if (strcmp(mode, "ctests") == 0)
                {
                    arguments[count++] = "--random";
                    arguments[count++] = "128";
                    arguments[count++] = "--mutations";
                    arguments[count++] = "40";
                    arguments[count++] = "--semantic-mutations";
                    arguments[count++] = "40";
                    arguments[count++] = "--seed";
                    arguments[count++] = "133";
                }
            }
        }
        else
        {
            arguments[count++] = "--predefines";
            arguments[count++] = predefines;
            arguments[count++] = "--clang-features";
            arguments[count++] = "--passes";
            arguments[count++] = "21";
            arguments[count++] = sqlite;
            if (strcmp(mode, "bodies") != 0)
            {
                arguments[count++] = "--only";
                arguments[count++] = strcmp(mode, "scalar") == 0 ? "scalar" : "vector";
            }
            if (strcmp(mode, "after") == 0)
            {
                arguments[count++] = "--after-scalar";
                arguments[count++] = "100";
            }
        }
        arguments[count++] = "--no-default-config";
        arguments[count++] = "--target=x86_64-linux-gnu";
        arguments[count++] = "-march=znver5";
        arguments[count++] = musl < 4 ? "-std=c99" : "-std=gnu17";
        arguments[count++] = "-nostdinc";
        if (musl < 4)
        {
            arguments[count++] = "-ffreestanding";
            arguments[count++] = "-D_XOPEN_SOURCE=700";
            arguments[count++] = "-Os";
            arguments[count++] = "-pipe";
            for (unsigned i = 0; i < 7; i += 1)
            {
                arguments[count++] = "-I";
                arguments[count++] = musl_headers[i];
            }
            if (musl == 1 || musl == 2)
            {
                arguments[count++] = "-DCRT";
            }
            if (musl == 2 || musl == 3)
            {
                arguments[count++] = "-fPIC";
            }
        }
        for (unsigned i = 0; musl == 4 && i < 3; i += 1)
        {
            arguments[count++] = "-isystem";
            arguments[count++] = headers[i];
        }
        if (strcmp(mode, "own-main") == 0)
        {
            arguments[count++] = "-I";
            arguments[count++] = repository_include;
        }
        arguments[count] = NULL;
        result = valid ? run(arguments, NULL) : 2;
    }
    if (!valid)
    {
        fprintf(stderr, "usage: checker-target-run verify|libc|libc-all|libc-rejections|musl-ordinary|musl-crt|musl-crt-pic|musl-ldso|musl-*-rejections|ctests|sqlite|own-main|bodies|scalar|vector|after COMPILER CLANG FROZEN_BUNDLE C_TESTSUITE_SINGLE_EXEC NEW_OUTPUT_PREFIX [SHARD_INDEX SHARD_COUNT]\n");
    }
    return result;
}
