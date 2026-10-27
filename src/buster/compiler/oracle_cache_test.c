// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// test-oracle-cache: the oracle cache's checks (oracle_cache.c), in a directory of their own under $TMPDIR (or /tmp)
// and two more of one template beside it, with sh and clang as the tools and SOURCE_DATE_EPOCH fixed where it isn't set:
//   - SHA-256: FIPS 180-4's examples (the empty message, "abc", the 448-bit message, a million a's), and every length
//     from 0 to 300 bytes hashed whole and in random pieces, which must agree;
//   - roots: a directory's, an option's and a file's (its random characters before its suffix), names that aren't
//     roots, two in order, more than BUSTER_ORACLE_ROOTS, a root followed by a name's character (not it), two roots
//     replaced by each other in one pass, and keys that are alike only where the random characters differ;
//   - runs: a run's status, output, errors and written file, taken by the same run again and by one in the other
//     directory, whose output and file (NUL bytes among them) name its own directory; each part of a key (the input's
//     bytes, an argument's byte, where the arguments split, a variable's value, one set to nothing against one that
//     isn't set, the purpose, the time limit, the tool's path and its file's times) making a new one, and HOME not; a
//     tool not on PATH; a file the run didn't write removed when it is taken;
//   - dependencies (traced clang -fsyntax-only): taken in the same and the other directory, again after a header
//     changes, and taken after a file it doesn't look for is added beside it; again after #embed's resource or what
//     __has_include found changes; __has_include's header created where it looked, in a directory other files go
//     into; a nested header created in its includer's directory, before the one -I found; preprocessed text's
//     #include; errors naming absolute paths, taken with the other directory's; a missing #include kept, and run
//     again once its header is there; -M options, not kept;
//   - identities: a hard link replaced by a copy of its bytes, which #pragma once takes again; clang's __TIMESTAMP__,
//     SOURCE_DATE_EPOCH's, its files' times changed and its entry taken; __DATE__, and __TIME__ pasted from two halves
//     in a header in no root and on standard input, none kept without SOURCE_DATE_EPOCH and each with it; a header in
//     no root checked by its identity;
//   - tracing (traced sh): a file a child process read and a path a test found absent, each run again when it
//     changes; two runs at once, finished in the other order; clang's __TIMESTAMP__ following SOURCE_DATE_EPOCH and
//     sh's not, whose run of `stat` has its file's time in its entry;
//   - permissions (traced sh): a directory's mode, which `test -x` reads, and a script's in a root made 0644 from
//     0755, each run again when they change; and as root, a file's owner;
//   - the kernel's paths: the path rule's answers for 42 paths, followed to their ends and not (a root under /usr among
//     them); a source in a root in /dev/shm ($TMPDIR there) and a header in /dev/shm in no root, each compiled again
//     when they change; runs that follow /proc/self/cwd and /dev/fd/3 to a file, not kept; and one that reads
//     /proc/self/fd/3's link and /dev/stdin and writes /dev/null, kept;
//   - changes (traced sh): mkdir's path, which the run made, there the next time; mkdir in a directory made since; and
//     rmdir of a directory whose file goes; each run again;
//   - what the tracing can't follow, with a program the test compiles: openat2 with RESOLVE_IN_ROOT, not kept, and
//     without resolution flags kept and run again when its file changes; chroot, unshare and getxattr, not kept;
//     statfs kept, and run again once its path is there;
//   - process groups: what a run leaves running when it ends and when it is killed at its time limit, killed (markers
//     they would write later aren't); the limit on the monotonic clock, with a pidfd and without one; a run kept
//     without a limit, killed at a limit given later;
//   - check mode: an entry made stale (by a file its key doesn't have) fails the run and is written again; an entry
//     whose run read other bytes isn't stale;
//   - damaged entries: a changed magic, key, body length, root or dependency count, an unknown flag, one cut short, cut
//     inside its header or one byte longer, run again and written whole;
//   - modes: off, a mode that names none, and no directory keep nothing;
//   - writers at once: 8 processes running the same 4 runs 16 times each, whose counts add up in the parent's and
//     whose entries are whole after, with no file left beside them;
//   - the process's cache: BUSTER_ORACLE_CACHE and XDG_CACHE_HOME, a directory made private, three levels deep, and
//     turned off (oracle_cache_disable).
// It prints its checks and failures and exits with status 1 if any fail.

#ifndef BUSTER_ORACLE_CACHE_TEST_INCLUDED
#define BUSTER_ORACLE_CACHE_TEST_INCLUDED

#include <dirent.h>
#include <stddef.h>
#include <sys/stat.h>
#include <sys/wait.h>

typedef struct OracleTest OracleTest;
struct OracleTest
{
    uint32_t checks;
    uint32_t failures;
    // The test's own directory, and two of one template beside it, each a root.
    char base[BUSTER_ORACLE_PATH_BYTES];
    char runs[2][BUSTER_ORACLE_PATH_BYTES];
};

static void oracle_test_expect(OracleTest* test, bool condition, const char* what)
{
    test->checks += 1;
    if (!condition)
    {
        test->failures += 1;
        printf("oracle cache: FAIL: %s\n", what);
    }
}

static OracleKey oracle_test_sha256(const void* bytes, size_t length)
{
    OracleHash hash;
    oracle_sha256_begin(&hash);
    oracle_hash_raw(&hash, bytes, length);
    return oracle_hash_end(&hash);
}

static bool oracle_test_hex(OracleKey key, const char* hex)
{
    char text[2 * BUSTER_ORACLE_KEY_BYTES + 1];
    for (uint32_t i = 0; i < BUSTER_ORACLE_KEY_BYTES; i += 1)
    {
        snprintf(text + 2 * i, 3, "%02x", key.bytes[i]);
    }
    return strcmp(text, hex) == 0;
}

static bool oracle_test_same(OracleKey a, OracleKey b)
{
    return memcmp(a.bytes, b.bytes, sizeof(a.bytes)) == 0;
}

// SplitMix64 (public domain).
static uint64_t oracle_test_random(uint64_t* state)
{
    *state += 0x9e3779b97f4a7c15ull;
    uint64_t z = *state;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

static void oracle_test_sha(OracleTest* test)
{
    oracle_test_expect(test, oracle_test_hex(oracle_test_sha256("", 0), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"), "SHA-256 of the empty message");
    oracle_test_expect(test, oracle_test_hex(oracle_test_sha256("abc", 3), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"), "SHA-256 of \"abc\"");
    static const char two_blocks[] = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    oracle_test_expect(test, oracle_test_hex(oracle_test_sha256(two_blocks, sizeof(two_blocks) - 1), "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"),
                       "SHA-256 of the 448-bit message");
    char* million = malloc(1000000);
    if (million != 0)
    {
        memset(million, 'a', 1000000);
        oracle_test_expect(test, oracle_test_hex(oracle_test_sha256(million, 1000000), "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"),
                           "SHA-256 of a million a's");
    }
    free(million);
    uint64_t state = 542;
    uint8_t bytes[300];
    bool agree = true;
    for (uint32_t length = 0; length <= sizeof(bytes); length += 1)
    {
        for (uint32_t i = 0; i < length; i += 1)
        {
            bytes[i] = (uint8_t)oracle_test_random(&state);
        }
        OracleHash pieces;
        oracle_sha256_begin(&pieces);
        uint32_t done = 0;
        while (done < length)
        {
            const uint32_t piece = 1 + (uint32_t)(oracle_test_random(&state) % 97);
            const uint32_t take = piece < length - done ? piece : length - done;
            oracle_hash_raw(&pieces, bytes + done, take);
            done += take;
        }
        agree &= oracle_test_same(oracle_hash_end(&pieces), oracle_test_sha256(bytes, length));
    }
    oracle_test_expect(test, agree, "SHA-256 of 0 to 300 bytes, whole and in pieces");
}

// --- Roots ---

// A key of `text` alone, its roots found in it.
static OracleKey oracle_test_rooted(const char* text)
{
    OracleRoots roots;
    oracle_roots_begin(&roots);
    oracle_roots_find(&roots, text);
    OracleHash hash;
    oracle_sha256_begin(&hash);
    oracle_hash_rooted(&hash, &roots, text);
    return oracle_hash_end(&hash);
}

static uint32_t oracle_test_root_count(const char* text)
{
    OracleRoots roots;
    oracle_roots_begin(&roots);
    oracle_roots_find(&roots, text);
    return roots.count;
}

static void oracle_test_roots(OracleTest* test)
{
    const char* saved = getenv("TMPDIR");
    char* kept = saved != 0 ? strdup(saved) : 0;
    setenv("TMPDIR", "/b/", 1);
    OracleRoots roots;
    oracle_roots_begin(&roots);
    oracle_test_expect(test, roots.base_length == 2 && strcmp(roots.base, "/b") == 0, "roots: the temporary directory without its trailing slash");
    oracle_roots_find(&roots, "/b/x-Ab3d9Z/c.c");
    oracle_test_expect(test, roots.count == 1 && roots.roots[0].length == 11 && roots.roots[0].random == 5 && memcmp(roots.roots[0].name, "/b/x-Ab3d9Z", 11) == 0,
                       "roots: a directory's");
    oracle_roots_find(&roots, "-I/b/x-Ab3d9Z/include");
    oracle_test_expect(test, roots.count == 1, "roots: the same root in an option");
    oracle_roots_find(&roots, "/b/case-Ab3d9Z.c");
    oracle_test_expect(test, roots.count == 2 && roots.roots[1].length == 16 && roots.roots[1].random == 8, "roots: a file's, its random characters before its suffix");
    static const char* const none[] = { "/b/case.c", "/b/abc", "/b/", "/b", "/bx/y-Ab3d9Z", "/b/y-Ab3d9!", "/b/y-Ab3_9Z", "b/y-Ab3d9Z", "" };
    bool found_none = true;
    for (uint32_t i = 0; i < sizeof(none) / sizeof(none[0]); i += 1)
    {
        found_none &= oracle_test_root_count(none[i]) == 0;
    }
    oracle_test_expect(test, found_none, "roots: names that aren't");
    oracle_roots_begin(&roots);
    oracle_roots_find(&roots, "/b/p-AAAAAA:/b/q-BBBBBB/f");
    oracle_test_expect(test, roots.count == 2 && memcmp(roots.roots[0].name, "/b/p-AAAAAA", 11) == 0 && memcmp(roots.roots[1].name, "/b/q-BBBBBB", 11) == 0,
                       "roots: two, in order");
    oracle_roots_begin(&roots);
    char many[16 * (BUSTER_ORACLE_ROOTS + 1)];
    size_t length = 0;
    for (uint32_t i = 0; i <= BUSTER_ORACLE_ROOTS; i += 1)
    {
        length += (size_t)snprintf(many + length, sizeof(many) - length, "/b/r-AAAAA%c ", 'a' + i);
    }
    oracle_roots_find(&roots, many);
    oracle_test_expect(test, roots.count == BUSTER_ORACLE_ROOTS && roots.overflow, "roots: more than BUSTER_ORACLE_ROOTS");
    oracle_roots_begin(&roots);
    oracle_roots_find(&roots, "/b/x-Ab3d9Z");
    const OracleRoot* root = roots.roots;
    oracle_test_expect(test, oracle_root_in(root, 1, (const uint8_t*)"/b/x-Ab3d9Zq", 12, 0) == 1 && oracle_root_in(root, 1, (const uint8_t*)"/b/x-Ab3d9Z/", 12, 0) == 0 &&
                           oracle_root_in(root, 1, (const uint8_t*)"/b/x-Ab3d9Z", 11, 0) == 0,
                       "roots: one followed by a name's character isn't it");
    const OracleRoot old[2] = { { .name = "/b/x-AAAAAA", .length = 11 }, { .name = "/b/y-BBBBBB", .length = 11 } };
    const OracleRoot new[2] = { { .name = "/b/y-BBBBBB", .length = 11 }, { .name = "/b/x-AAAAAA", .length = 11 } };
    char swapped[] = "/b/x-AAAAAA and /b/y-BBBBBB/f, /b/x-AAAAAAz";
    oracle_roots_replace(old, new, 2, (uint8_t*)swapped, strlen(swapped));
    oracle_test_expect(test, strcmp(swapped, "/b/y-BBBBBB and /b/x-AAAAAA/f, /b/x-AAAAAAz") == 0, "roots: two replaced by each other in one pass");
    oracle_test_expect(test, oracle_test_same(oracle_test_rooted("/b/x-AAAAAA/c.c"), oracle_test_rooted("/b/x-BBBBBB/c.c")) &&
                           !oracle_test_same(oracle_test_rooted("/b/x-AAAAAA/c.c"), oracle_test_rooted("/b/y-AAAAAA/c.c")) &&
                           !oracle_test_same(oracle_test_rooted("/b/x-AAAAAA/c.c"), oracle_test_rooted("/b/x-AAAAAA/d.c")) &&
                           oracle_test_same(oracle_test_rooted("/b/p-AAAAAA /b/q-BBBBBB"), oracle_test_rooted("/b/p-CCCCCC /b/q-DDDDDD")) &&
                           !oracle_test_same(oracle_test_rooted("/b/p-AAAAAA /b/q-BBBBBB"), oracle_test_rooted("/b/p-AAAAAA /b/p-AAAAAA")),
                       "roots: keys alike where the random characters differ, and only there");
    if (kept != 0)
    {
        setenv("TMPDIR", kept, 1);
    }
    else
    {
        unsetenv("TMPDIR");
    }
    free(kept);
}

// --- Files ---

static bool oracle_test_write(const char* path, const void* bytes, size_t length)
{
    FILE* file = fopen(path, "wb");
    bool result = file != 0 && fwrite(bytes, 1, length, file) == length;
    if (file != 0)
    {
        result = fclose(file) == 0 && result;
    }
    return result;
}

static bool oracle_test_write_text(const char* directory, const char* name, const char* text)
{
    char path[BUSTER_ORACLE_PATH_BYTES];
    snprintf(path, sizeof(path), "%s/%s", directory, name);
    return oracle_test_write(path, text, strlen(text));
}

// A file's bytes, malloc's with a NUL after them (null if it can't be read), and their length.
static char* oracle_test_read(const char* path, uint64_t* length)
{
    OracleBuffer buffer = { 0 };
    const bool read = oracle_buffer_file(&buffer, path);
    oracle_buffer_put(&buffer, "", 1);
    const bool result = read && !buffer.failed;
    *length = result ? buffer.length - 1 : 0;
    if (!result)
    {
        free(buffer.bytes);
    }
    return result ? (char*)buffer.bytes : 0;
}

static void oracle_test_open(OracleTest* test, OracleCache* cache, const char* mode, const char* name)
{
    char base[BUSTER_ORACLE_PATH_BYTES];
    snprintf(base, sizeof(base), "%s/%s", test->base, name);
    oracle_cache_open(cache, mode, base);
}

// The files in a cache's directory, and of them those that aren't entries' names (64 hex digits).
static void oracle_test_count(const OracleCache* cache, uint32_t* files, uint32_t* others)
{
    *files = 0;
    *others = 0;
    DIR* directory = opendir(cache->directory);
    for (struct dirent* entry = directory != 0 ? readdir(directory) : 0; entry != 0; entry = readdir(directory))
    {
        const bool own = strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0;
        bool hex = strlen(entry->d_name) == 2 * BUSTER_ORACLE_KEY_BYTES;
        for (uint32_t i = 0; hex && i < 2 * BUSTER_ORACLE_KEY_BYTES; i += 1)
        {
            const char c = entry->d_name[i];
            hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        }
        *files += !own;
        *others += !own && !hex;
    }
    if (directory != 0)
    {
        closedir(directory);
    }
}

// --- Runs ---

// A run of `arguments` through `cache`, with `input` and `environment`, whose result the caller releases: whether it
// ran (or took its entry).
static bool oracle_test_go(OracleRun* run, OracleCache* cache, const char* purpose, char* const* arguments, const char* input, char* const* environment)
{
    *run = (OracleRun){
        .cache = cache,
        .purpose = purpose,
        .arguments = arguments,
        .input = input,
        .environment = environment,
    };
    return oracle_run(run);
}

// Whether a run of `arguments` takes an entry.
static bool oracle_test_takes(OracleCache* cache, const char* purpose, char* const* arguments, const char* input, char* const* environment)
{
    OracleRun run;
    const bool ran = oracle_test_go(&run, cache, purpose, arguments, input, environment);
    oracle_run_release(&run);
    return ran && run.taken;
}

static bool oracle_test_copy(const char* from, const char* to)
{
    uint64_t length = 0;
    char* bytes = oracle_test_read(from, &length);
    const bool result = bytes != 0 && oracle_test_write(to, bytes, length) && chmod(to, 0755) == 0;
    free(bytes);
    return result;
}

// The run's status, output and errors, and its written file's bytes, as `sh -c SCRIPT` in `directory` gives them.
static bool oracle_test_results(const OracleRun* run, const char* directory, const char* written)
{
    char output[BUSTER_ORACLE_PATH_BYTES + 64];
    snprintf(output, sizeof(output), "out %s\ninput bytes\n", directory);
    char file[BUSTER_ORACLE_PATH_BYTES + 64];
    const int file_length = snprintf(file, sizeof(file), "a%c%s%cb", 0, directory, 0);
    uint64_t length = 0;
    char* bytes = oracle_test_read(written, &length);
    const bool result = WIFEXITED(run->status) && WEXITSTATUS(run->status) == 3 && run->output != 0 && strcmp(run->output, output) == 0 && run->errors != 0 &&
                        strcmp(run->errors, "err\n") == 0 && bytes != 0 && length == (uint64_t)file_length && memcmp(bytes, file, length) == 0;
    free(bytes);
    return result;
}

static void oracle_test_runs(OracleTest* test)
{
    OracleCache cache;
    oracle_test_open(test, &cache, "use", "runs");
    char input[BUSTER_ORACLE_PATH_BYTES];
    snprintf(input, sizeof(input), "%s/input", test->base);
    bool ready = oracle_test_write(input, "input bytes\n", 12);
    static char script[] = "printf 'out %s\\n' \"$1\"; printf 'err\\n' >&2; cat; printf 'a\\000%s\\000b' \"$1\" > \"$1/written\"; exit 3";
    // The same run, the same run again, and the run in the other directory.
    static const uint32_t directories[] = { 0, 0, 1 };
    bool right = ready;
    for (uint32_t i = 0; i < 3; i += 1)
    {
        char* directory = test->runs[directories[i]];
        char written[BUSTER_ORACLE_PATH_BYTES + 16];
        snprintf(written, sizeof(written), "%s/written", directory);
        unlink(written);
        char* arguments[] = { "sh", "-c", script, "sh", directory, 0 };
        const char* files[] = { written };
        OracleRun run = {
            .cache = &cache,
            .purpose = "oracle test: sh",
            .arguments = arguments,
            .input = input,
            .files = files,
            .file_count = 1,
        };
        const bool ran = oracle_run(&run);
        right &= ran && run.taken == (i != 0) && oracle_test_results(&run, directory, written);
        oracle_run_release(&run);
    }
    oracle_test_expect(test, right, "runs: a run's result taken by the same run and by one in the other directory, naming its own");
    oracle_test_expect(test, cache.counts->taken == 2 && cache.counts->run == 1, "runs: the counts");
    // Each part of the key.
    char path[BUSTER_ORACLE_PATH_BYTES + 32];
    char tools[BUSTER_ORACLE_PATH_BYTES + 32];
    snprintf(tools, sizeof(tools), "%s/tools", test->base);
    snprintf(path, sizeof(path), "PATH=%s", getenv("PATH") != 0 ? getenv("PATH") : "/usr/bin:/bin");
    char* plain[] = { path, "LANG=C", 0 };
    char* other_value[] = { path, "LANG=POSIX", 0 };
    char* empty[] = { path, "LANG=", 0 };
    char* unset[] = { path, 0 };
    char* home[] = { path, "LANG=C", "HOME=/somewhere", 0 };
    char* whole[] = { "sh", "-c", "echo ab", "x", 0 };
    char* split[] = { "sh", "-c", "echo a", "bx", 0 };
    char* byte[] = { "sh", "-c", "echo ab ", "x", 0 };
    const bool first = oracle_test_takes(&cache, "oracle test: parts", whole, input, plain);
    oracle_test_expect(test, !first && oracle_test_takes(&cache, "oracle test: parts", whole, input, plain), "keys: the same parts, the same key");
    oracle_test_expect(test, !oracle_test_takes(&cache, "oracle test: parts", split, input, plain), "keys: where the arguments split");
    oracle_test_expect(test, !oracle_test_takes(&cache, "oracle test: parts", byte, input, plain), "keys: an argument's byte");
    oracle_test_expect(test, !oracle_test_takes(&cache, "oracle test: other parts", whole, input, plain), "keys: the purpose");
    oracle_test_expect(test, !oracle_test_takes(&cache, "oracle test: parts", whole, input, other_value), "keys: a variable's value");
    const bool empty_first = !oracle_test_takes(&cache, "oracle test: parts", whole, input, empty);
    oracle_test_expect(test, empty_first && !oracle_test_takes(&cache, "oracle test: parts", whole, input, unset), "keys: a variable set to nothing, and not set");
    oracle_test_expect(test, oracle_test_takes(&cache, "oracle test: parts", whole, input, home), "keys: HOME isn't a part");
    // The same run with a time limit, another key, taken with that limit again.
    bool limits = true;
    for (uint32_t i = 0; i < 2; i += 1)
    {
        OracleRun limited = { .cache = &cache, .purpose = "oracle test: parts", .arguments = whole, .input = input, .environment = plain, .timeout = 60 };
        limits &= oracle_run(&limited) && limited.taken == (i == 1);
        oracle_run_release(&limited);
    }
    oracle_test_expect(test, limits, "keys: the time limit");
    oracle_test_write(input, "input bytez\n", 12);
    oracle_test_expect(test, !oracle_test_takes(&cache, "oracle test: parts", whole, input, plain), "keys: the input's bytes");
    // The tool: another sh first on PATH, and that file's times changed.
    char shell[BUSTER_ORACLE_PATH_BYTES];
    char copy[BUSTER_ORACLE_PATH_BYTES + 32];
    snprintf(copy, sizeof(copy), "%s/sh", tools);
    const bool copied = oracle_find_tool("sh", shell, sizeof(shell)) && mkdir(tools, 0700) == 0 && oracle_test_copy(shell, copy);
    const char* saved = getenv("PATH");
    char* kept = saved != 0 ? strdup(saved) : 0;
    char search[2 * BUSTER_ORACLE_PATH_BYTES];
    snprintf(search, sizeof(search), "%s:%s", tools, kept != 0 ? kept : "/usr/bin:/bin");
    setenv("PATH", search, 1);
    const bool moved = copied && !oracle_test_takes(&cache, "oracle test: parts", whole, input, plain);
    const bool again = oracle_test_takes(&cache, "oracle test: parts", whole, input, plain);
    const struct timespec times[2] = { { .tv_sec = 1000000000 }, { .tv_sec = 1000000000 } };
    const bool touched = utimensat(AT_FDCWD, copy, times, 0) == 0 && !oracle_test_takes(&cache, "oracle test: parts", whole, input, plain);
    setenv("PATH", kept != 0 ? kept : "/usr/bin:/bin", 1);
    free(kept);
    oracle_test_expect(test, moved && again && touched, "keys: the tool's path on PATH and its file's times");
    // A tool that isn't on PATH runs without the cache, and doesn't start.
    char* missing[] = { "buster-oracle-no-such-tool", 0 };
    OracleRun run;
    const uint64_t unkept = cache.counts->unkept;
    const bool started = oracle_test_go(&run, &cache, "oracle test: parts", missing, 0, 0);
    oracle_run_release(&run);
    oracle_test_expect(test, !started && cache.counts->unkept == unkept + 1, "runs: a tool that isn't on PATH");
    // A file the run didn't write is removed when its entry is taken.
    char absent[BUSTER_ORACLE_PATH_BYTES + 16];
    snprintf(absent, sizeof(absent), "%s/absent", test->runs[0]);
    unlink(absent);
    char* nothing[] = { "sh", "-c", "true", 0 };
    const char* files[] = { absent };
    bool removed = true;
    for (uint32_t i = 0; i < 2; i += 1)
    {
        run = (OracleRun){ .cache = &cache, .purpose = "oracle test: sh", .arguments = nothing, .files = files, .file_count = 1 };
        removed &= oracle_run(&run) && run.taken == (i == 1);
        oracle_run_release(&run);
        removed &= i == 1 || oracle_test_write(absent, "junk", 4);
    }
    struct stat status;
    oracle_test_expect(test, removed && lstat(absent, &status) != 0 && errno == ENOENT, "runs: a file the run didn't write, removed when it is taken");
    uint32_t count = 0;
    uint32_t others = 0;
    oracle_test_count(&cache, &count, &others);
    oracle_test_expect(test, count != 0 && others == 0, "runs: entries alone in the cache's directory");
    oracle_cache_report(&cache, "test-oracle-cache's runs");
    oracle_cache_close(&cache);
    unlink(copy);
    rmdir(tools);
}

// --- Dependencies ---

// A traced run of clang -fsyntax-only with `arguments` after it, in `directory` (null: this process's): whether it ran,
// whether it took its entry, and its status and errors.
static bool oracle_test_clang(OracleCache* cache, const char* directory, char* const* arguments, bool* taken, int* status, char* errors, size_t capacity)
{
    char* argv[16] = { "clang", "-fsyntax-only", "-fno-color-diagnostics" };
    uint32_t count = 3;
    for (uint32_t i = 0; arguments[i] != 0 && count + 1 < sizeof(argv) / sizeof(argv[0]); i += 1)
    {
        argv[count] = arguments[i];
        count += 1;
    }
    argv[count] = 0;
    OracleRun run = { .cache = cache, .purpose = "oracle test: clang", .arguments = argv, .directory = directory, .dependencies = true };
    const bool result = oracle_run(&run);
    *taken = result && run.taken;
    *status = run.status;
    snprintf(errors, capacity, "%s", run.errors != 0 ? run.errors : "");
    oracle_run_release(&run);
    return result;
}

// `directory`/`name`, made if it isn't there.
static bool oracle_test_directory(const char* directory, const char* name)
{
    char path[BUSTER_ORACLE_PATH_BYTES + 32];
    snprintf(path, sizeof(path), "%s/%s", directory, name);
    return mkdir(path, 0700) == 0 || errno == EEXIST;
}

static void oracle_test_unlink(const char* directory, const char* name)
{
    char path[BUSTER_ORACLE_PATH_BYTES + 32];
    snprintf(path, sizeof(path), "%s/%s", directory, name);
    unlink(path);
}

// Removes the files of `base`/`relative` (or `base` itself) and then each directory of `relative`, deepest first (the
// tests' own trees, whose directories hold files alone).
static void oracle_test_remove(const char* base, const char* relative)
{
    char path[BUSTER_ORACLE_PATH_BYTES];
    snprintf(path, sizeof(path), relative[0] != 0 ? "%s/%s" : "%s%s", base, relative);
    DIR* directory = opendir(path);
    for (struct dirent* entry = directory != 0 ? readdir(directory) : 0; entry != 0; entry = readdir(directory))
    {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
        {
            char file[BUSTER_ORACLE_PATH_BYTES + 256];
            snprintf(file, sizeof(file), "%s/%s", path, entry->d_name);
            unlink(file);
        }
    }
    if (directory != 0)
    {
        closedir(directory);
    }
    for (char* end = path + strlen(path); end >= path + strlen(base); end -= 1)
    {
        if (*end == '/' || *end == 0)
        {
            *end = 0;
            rmdir(path);
        }
    }
}

static void oracle_test_dependencies(OracleTest* test)
{
    OracleCache cache;
    oracle_test_open(test, &cache, "use", "dependencies");
    bool ready = true;
    for (uint32_t d = 0; d < 2; d += 1)
    {
        const char* directory = test->runs[d];
        ready &= oracle_test_write_text(directory, "main.c", "#include \"h.h\"\nint x = X;\n") && oracle_test_write_text(directory, "h.h", "#define X 1\n");
        ready &= oracle_test_write_text(directory, "bad.c", "int y = ;\n");
        ready &= oracle_test_write_text(directory, "data.bin", "abc");
        ready &= oracle_test_write_text(directory, "embed.c", "#if __has_include(\"h.h\")\n#endif\nconst char d[] = {\n#embed \"data.bin\"\n};\n");
        ready &= oracle_test_write_text(directory, "pre.c", "# 1 \"pre.c\"\n#include \"decl.h\"\nint p = sizeof(from_header);\n");
        ready &= oracle_test_write_text(directory, "decl.h", "int from_header;\n");
        ready &= oracle_test_write_text(directory, "has.c", "#if __has_include(\"optional.h\")\nint has;\n#endif\n");
        ready &= oracle_test_directory(directory, "sub") && oracle_test_directory(directory, "other");
        ready &= oracle_test_write_text(directory, "nest.c", "#include \"sub/inner.h\"\nint n = sizeof(deep);\n");
        ready &= oracle_test_write_text(directory, "sub/inner.h", "#include \"deep.h\"\n") && oracle_test_write_text(directory, "other/deep.h", "int deep;\n");
    }
    char errors[16384];
    bool taken = false;
    int status = 0;
    char* main_source[] = { "main.c", 0 };
    const bool first = ready && oracle_test_clang(&cache, test->runs[0], main_source, &taken, &status, errors, sizeof(errors)) && !taken && status == 0;
    const bool same = oracle_test_clang(&cache, test->runs[0], main_source, &taken, &status, errors, sizeof(errors)) && taken && status == 0;
    const bool other = oracle_test_clang(&cache, test->runs[1], main_source, &taken, &status, errors, sizeof(errors)) && taken && status == 0;
    oracle_test_expect(test, first && same && other, "dependencies: a compile taken in its directory and in the other");
    oracle_test_write_text(test->runs[1], "h.h", "#define X 2\n");
    const bool changed = oracle_test_clang(&cache, test->runs[1], main_source, &taken, &status, errors, sizeof(errors)) && !taken;
    const bool again = oracle_test_clang(&cache, test->runs[1], main_source, &taken, &status, errors, sizeof(errors)) && taken;
    oracle_test_expect(test, changed && again, "dependencies: a header that changes");
    // A file beside the source that the run doesn't look for leaves its entry taken.
    oracle_test_write_text(test->runs[1], "new.h", "\n");
    const bool beside = oracle_test_clang(&cache, test->runs[1], main_source, &taken, &status, errors, sizeof(errors)) && taken;
    oracle_test_unlink(test->runs[1], "new.h");
    oracle_test_expect(test, beside, "dependencies: a file added beside the source that it doesn't look for, taken");
    char* embed_source[] = { "-std=c23", "embed.c", 0 };
    const bool embedded = oracle_test_clang(&cache, test->runs[0], embed_source, &taken, &status, errors, sizeof(errors)) && !taken && status == 0 &&
                          oracle_test_clang(&cache, test->runs[0], embed_source, &taken, &status, errors, sizeof(errors)) && taken;
    oracle_test_write_text(test->runs[0], "data.bin", "abd");
    const bool resource = oracle_test_clang(&cache, test->runs[0], embed_source, &taken, &status, errors, sizeof(errors)) && !taken;
    oracle_test_write_text(test->runs[0], "h.h", "#define X 3\n");
    const bool probed = oracle_test_clang(&cache, test->runs[0], embed_source, &taken, &status, errors, sizeof(errors)) && !taken;
    oracle_test_expect(test, embedded && resource && probed, "dependencies: #embed's resource and what __has_include found");
    // __has_include's header, which isn't there, created where it looked for it, in a directory that other files go
    // into meanwhile, as other runs' files go into a directory they share.
    char* has_source[] = { "has.c", 0 };
    const bool has_kept = oracle_test_clang(&cache, test->runs[0], has_source, &taken, &status, errors, sizeof(errors)) && !taken && status == 0;
    oracle_test_write_text(test->runs[0], "sibling.c", "int sibling;\n");
    const bool has_beside = oracle_test_clang(&cache, test->runs[0], has_source, &taken, &status, errors, sizeof(errors)) && taken;
    oracle_test_write_text(test->runs[0], "optional.h", "\n");
    const bool has_created = oracle_test_clang(&cache, test->runs[0], has_source, &taken, &status, errors, sizeof(errors)) && !taken;
    const bool has_again = oracle_test_clang(&cache, test->runs[0], has_source, &taken, &status, errors, sizeof(errors)) && taken;
    oracle_test_expect(test, has_kept && has_beside && has_created && has_again,
                       "dependencies: __has_include's header created where it looked, in a directory other files go into");
    // A header a header includes, found through -Iother, created in the including header's directory, where a quoted
    // #include looks first.
    char* nest_source[] = { "-Iother", "nest.c", 0 };
    const bool nest_kept = oracle_test_clang(&cache, test->runs[0], nest_source, &taken, &status, errors, sizeof(errors)) && !taken && status == 0;
    const bool nest_taken = oracle_test_clang(&cache, test->runs[0], nest_source, &taken, &status, errors, sizeof(errors)) && taken;
    oracle_test_write_text(test->runs[0], "sub/deep.h", "int shadow;\n");
    const bool nest_created = oracle_test_clang(&cache, test->runs[0], nest_source, &taken, &status, errors, sizeof(errors)) && !taken && status != 0;
    oracle_test_expect(test, nest_kept && nest_taken && nest_created, "dependencies: a nested header created in its includer's directory, before the one found");
    // Preprocessed text, whose #include still reads a header (clang's driver writes no dependency file for it).
    char* pre_source[] = { "-x", "cpp-output", "pre.c", 0 };
    const bool pre_first = ready && oracle_test_clang(&cache, test->runs[0], pre_source, &taken, &status, errors, sizeof(errors)) && !taken && status == 0 &&
                           strstr(errors, "unused") == 0;
    const bool pre_same = oracle_test_clang(&cache, test->runs[0], pre_source, &taken, &status, errors, sizeof(errors)) && taken && status == 0;
    oracle_test_write_text(test->runs[0], "decl.h", "char from_header;\n");
    const bool pre_changed = oracle_test_clang(&cache, test->runs[0], pre_source, &taken, &status, errors, sizeof(errors)) && !taken && status == 0;
    oracle_test_expect(test, pre_first && pre_same && pre_changed, "dependencies: preprocessed text's, with the headers its directives include");
    // Errors naming absolute paths, taken with the other directory's.
    char bad[2][BUSTER_ORACLE_PATH_BYTES + 16];
    for (uint32_t d = 0; d < 2; d += 1)
    {
        snprintf(bad[d], sizeof(bad[d]), "%s/bad.c", test->runs[d]);
    }
    bool named = true;
    for (uint32_t d = 0; d < 2; d += 1)
    {
        char* bad_source[] = { bad[d], 0 };
        named &= oracle_test_clang(&cache, 0, bad_source, &taken, &status, errors, sizeof(errors)) && taken == (d == 1) && WIFEXITED(status) && WEXITSTATUS(status) == 1 &&
                 strstr(errors, bad[d]) != 0 && strstr(errors, bad[1 - d]) == 0;
    }
    oracle_test_expect(test, named, "dependencies: errors naming the run's paths, taken with the other directory's");
    // A missing #include is kept, its header looked for where it isn't, and run again once the header is there.
    oracle_test_write_text(test->runs[0], "missing.c", "#include \"missing.h\"\n");
    char* missing_source[] = { "missing.c", 0 };
    const bool missing_kept = oracle_test_clang(&cache, test->runs[0], missing_source, &taken, &status, errors, sizeof(errors)) && !taken && status != 0;
    const bool missing_taken = oracle_test_clang(&cache, test->runs[0], missing_source, &taken, &status, errors, sizeof(errors)) && taken && status != 0;
    oracle_test_write_text(test->runs[0], "missing.h", "\n");
    const bool missing_found = oracle_test_clang(&cache, test->runs[0], missing_source, &taken, &status, errors, sizeof(errors)) && !taken && status == 0;
    oracle_test_expect(test, missing_kept && missing_taken && missing_found, "dependencies: a missing #include kept, and run again once its header is there");
    // -M options write a dependency file the run doesn't name: not kept.
    char* asked[] = { "-MD", "main.c", 0 };
    const uint64_t unkept = cache.counts->unkept;
    bool not_kept = true;
    for (uint32_t i = 0; i < 2; i += 1)
    {
        not_kept &= oracle_test_clang(&cache, test->runs[0], asked, &taken, &status, errors, sizeof(errors)) && !taken && status == 0;
    }
    oracle_test_unlink(test->runs[0], "main.d");
    oracle_test_expect(test, not_kept && cache.counts->unkept == unkept + 2, "dependencies: -M options, not kept");
    oracle_cache_close(&cache);
}

// What a run reads of its files besides their bytes: which of them are one file (a hard link, which #pragma once takes
// once), its time macros, which only SOURCE_DATE_EPOCH makes keepable, however its sources spell them, and the files'
// identities outside its roots.
static void oracle_test_identities(OracleTest* test)
{
    OracleCache cache;
    oracle_test_open(test, &cache, "use", "identities");
    const char* directory = test->runs[0];
    char a[BUSTER_ORACLE_PATH_BYTES + 16];
    char b[BUSTER_ORACLE_PATH_BYTES + 16];
    char stamped[BUSTER_ORACLE_PATH_BYTES + 16];
    snprintf(a, sizeof(a), "%s/a.h", directory);
    snprintf(b, sizeof(b), "%s/b.h", directory);
    snprintf(stamped, sizeof(stamped), "%s/stamped.c", directory);
    unlink(b);
    bool ready = oracle_test_write_text(directory, "a.h", "#pragma once\nint a;\n") && link(a, b) == 0 &&
                 oracle_test_write_text(directory, "links.c", "#include \"a.h\"\n#include \"b.h\"\n") &&
                 oracle_test_write_text(directory, "stamped.c", "const char* stamp = __TIMESTAMP__;\n") &&
                 oracle_test_write_text(directory, "dated.c", "const char* date = __DATE__;\n") &&
                 oracle_test_write_text(directory, "timed.txt", "#define CAT(a, b) a ## b\nconst char* t = CAT(__TI, ME__);\n");
    char errors[4096];
    bool taken = false;
    int status = 0;
    char* links[] = { "links.c", 0 };
    const bool linked = ready && oracle_test_clang(&cache, directory, links, &taken, &status, errors, sizeof(errors)) && !taken && status == 0 &&
                        oracle_test_clang(&cache, directory, links, &taken, &status, errors, sizeof(errors)) && taken;
    // The hard link replaced by a copy of the same bytes, which #pragma once doesn't take for the same file.
    const bool copied = unlink(b) == 0 && oracle_test_write_text(directory, "b.h", "#pragma once\nint a;\n") &&
                        oracle_test_clang(&cache, directory, links, &taken, &status, errors, sizeof(errors)) && !taken;
    oracle_test_expect(test, linked && copied, "identities: a hard link and a copy of the same bytes are other dependencies");
    // clang's __TIMESTAMP__ follows SOURCE_DATE_EPOCH, so that its files' times aren't what its run read.
    const struct timespec first[2] = { { .tv_sec = 946684800 }, { .tv_sec = 946684800 } };
    const struct timespec second[2] = { { .tv_sec = 978307200 }, { .tv_sec = 978307200 } };
    char* stamp[] = { "stamped.c", 0 };
    const bool timed = utimensat(AT_FDCWD, stamped, first, 0) == 0 && oracle_test_clang(&cache, directory, stamp, &taken, &status, errors, sizeof(errors)) && !taken &&
                       oracle_test_clang(&cache, directory, stamp, &taken, &status, errors, sizeof(errors)) && taken && utimensat(AT_FDCWD, stamped, second, 0) == 0 &&
                       oracle_test_clang(&cache, directory, stamp, &taken, &status, errors, sizeof(errors)) && taken;
    oracle_test_expect(test, timed, "identities: clang's __TIMESTAMP__ is SOURCE_DATE_EPOCH's, its files' times not part of the run");
    // __DATE__, and __TIME__ pasted from two halves in a header in no root and in a source on standard input: without
    // SOURCE_DATE_EPOCH none is kept, and with it each is, and gives the epoch's time.
    const char* temporary = getenv("TMPDIR") != 0 && getenv("TMPDIR")[0] != 0 ? getenv("TMPDIR") : "/tmp";
    char fixed[BUSTER_ORACLE_PATH_BYTES];
    char option[BUSTER_ORACLE_PATH_BYTES + 8];
    char input[BUSTER_ORACLE_PATH_BYTES + 16];
    snprintf(fixed, sizeof(fixed), "%s/buster_oracle_identity_%ld_x", temporary, (long)getpid());
    snprintf(option, sizeof(option), "-I%s", fixed);
    snprintf(input, sizeof(input), "%s/timed.txt", directory);
    ready = (mkdir(fixed, 0700) == 0 || errno == EEXIST) && oracle_test_write_text(fixed, "fixed.h", "int fixed;\n") &&
            oracle_test_write_text(fixed, "pasted.h", "#define CAT(a, b) a ## b\nconst char* t = CAT(__TI, ME__);\n") &&
            oracle_test_write_text(directory, "fixed.c", "#include \"fixed.h\"\n") && oracle_test_write_text(directory, "pasted.c", "#include \"pasted.h\"\n");
    char* date[] = { "clang", "-E", "-P", "dated.c", 0 };
    char* pasted[] = { "clang", "-E", "-P", option, "pasted.c", 0 };
    char* piped[] = { "clang", "-E", "-P", "-x", "c", "-", 0 };
    char* const* sources[3] = { date, pasted, piped };
    const char* const inputs[3] = { 0, 0, input };
    const char* saved = getenv("SOURCE_DATE_EPOCH");
    char* kept = saved != 0 ? strdup(saved) : 0;
    bool dated = ready;
    for (uint32_t epoch = 0; epoch < 2; epoch += 1)
    {
        if (epoch == 0)
        {
            unsetenv("SOURCE_DATE_EPOCH");
        }
        else
        {
            setenv("SOURCE_DATE_EPOCH", "0", 1);
        }
        for (uint32_t s = 0; s < 3; s += 1)
        {
            const uint64_t unkept = cache.counts->unkept;
            for (uint32_t i = 0; i < 2; i += 1)
            {
                OracleRun run = { .cache = &cache, .purpose = "oracle test: dated", .arguments = sources[s], .input = inputs[s], .directory = directory, .dependencies = true };
                dated &= oracle_run(&run) && run.status == 0 && run.taken == (epoch == 1 && i == 1);
                dated &= epoch == 0 || (s == 0 ? strstr(run.output, "Jan  1 1970") != 0 : strstr(run.output, "00:00:00") != 0);
                oracle_run_release(&run);
            }
            dated &= epoch == 1 || cache.counts->unkept == unkept + 2;
        }
    }
    if (kept != 0)
    {
        setenv("SOURCE_DATE_EPOCH", kept, 1);
    }
    else
    {
        unsetenv("SOURCE_DATE_EPOCH");
    }
    free(kept);
    oracle_test_expect(test, dated, "identities: __DATE__ and __TIME__ pasted, in no root and on standard input, kept with SOURCE_DATE_EPOCH alone");
    // A dependency in no root (a directory whose name isn't mkdtemp's) is checked by its identity: rewritten, even with
    // its own bytes, it is another file.
    char* identified[] = { "clang", "-fsyntax-only", "-fno-color-diagnostics", option, "fixed.c", 0 };
    bool identity = ready;
    for (uint32_t i = 0; i < 5; i += 1)
    {
        // Kept, taken, rewritten with other bytes (run), with the same bytes (run), and taken.
        const bool rewrite = i == 2 || i == 3;
        identity &= !rewrite || oracle_test_write_text(fixed, "fixed.h", "int fixed2;\n");
        OracleRun run = { .cache = &cache, .purpose = "oracle test: identity", .arguments = identified, .directory = directory, .dependencies = true };
        identity &= oracle_run(&run) && run.status == 0 && run.taken == (i == 1 || i == 4);
        oracle_run_release(&run);
    }
    oracle_test_unlink(fixed, "fixed.h");
    oracle_test_unlink(fixed, "pasted.h");
    rmdir(fixed);
    oracle_test_expect(test, identity, "identities: a dependency in no root, checked by its identity, a rewrite another file");
    oracle_cache_close(&cache);
}

// --- Tracing ---

// A traced run of `sh -c SCRIPT sh DIRECTORY`, through `cache`: whether it ran, took its entry, and printed `output`.
static bool oracle_test_traced(OracleCache* cache, const char* script, const char* directory, bool taken, const char* output)
{
    char* arguments[] = { "sh", "-c", (char*)script, "sh", (char*)directory, 0 };
    OracleRun run = { .cache = cache, .purpose = "oracle test: traced sh", .arguments = arguments, .dependencies = true };
    const bool result = oracle_run(&run) && run.taken == taken && run.output != 0 && strcmp(run.output, output) == 0;
    oracle_run_release(&run);
    return result;
}

// What a traced run looked at, with sh as the tool: a file its child process (cat) read and a path its test found
// absent, each of which runs it again when it changes; two runs at once, finished in the other order, each answered
// while the other is waited for; and the tools' __TIMESTAMP__: clang's follows SOURCE_DATE_EPOCH and sh's (which has
// none) doesn't, so that a sh run that prints a file's time has its files' times in its entry.
static void oracle_test_tracing(OracleTest* test)
{
    OracleCache cache;
    oracle_test_open(test, &cache, "use", "tracing");
    const char* directory = test->runs[0];
    oracle_test_unlink(directory, "maybe");
    static const char script[] = "cat \"$1/read.txt\"; if test -e \"$1/maybe\"; then echo maybe; fi";
    bool looked = oracle_test_write_text(directory, "read.txt", "one\n");
    looked &= oracle_test_traced(&cache, script, directory, false, "one\n") && oracle_test_traced(&cache, script, directory, true, "one\n");
    looked &= oracle_test_write_text(directory, "read.txt", "two\n") && oracle_test_traced(&cache, script, directory, false, "two\n");
    looked &= oracle_test_write_text(directory, "maybe", "") && oracle_test_traced(&cache, script, directory, false, "two\nmaybe\n");
    looked &= oracle_test_traced(&cache, script, directory, true, "two\nmaybe\n");
    oracle_test_expect(test, looked, "tracing: what a child process read and what a test found absent, each run again when it changes");
    // A file the run makes and reads back is one that wasn't there: taken while it isn't, and run again when it is. A
    // file the run changes after it read it isn't kept.
    oracle_test_unlink(directory, "scratch");
    static const char scratch[] = "echo made > \"$1/scratch\"; cat \"$1/scratch\"";
    bool made = oracle_test_traced(&cache, scratch, directory, false, "made\n");
    oracle_test_unlink(directory, "scratch");
    made &= oracle_test_traced(&cache, scratch, directory, true, "made\n") && oracle_test_write_text(directory, "scratch", "there\n");
    made &= oracle_test_traced(&cache, scratch, directory, false, "made\n");
    static const char append[] = "cat \"$1/log\"; echo more >> \"$1/log\"";
    const uint64_t unkept = cache.counts->unkept;
    made &= oracle_test_write_text(directory, "log", "first\n") && oracle_test_traced(&cache, append, directory, false, "first\n");
    made &= oracle_test_write_text(directory, "log", "first\n") && oracle_test_traced(&cache, append, directory, false, "first\n");
    oracle_test_expect(test, made && cache.counts->unkept == unkept + 2, "tracing: a file the run makes, absent before it, and one it changes after it read it, not kept");
    // Two runs at once, each waiting on a file the other writes, finished in the other order (and killed at a time
    // limit, should one wait for the other's answers).
    oracle_test_unlink(directory, "first");
    oracle_test_unlink(directory, "second");
    char* first_arguments[] = { "sh", "-c", "touch \"$1/first\"; while ! test -e \"$1/second\"; do sleep 0.01; done; echo first", "sh", (char*)directory, 0 };
    char* second_arguments[] = { "sh", "-c", "while ! test -e \"$1/first\"; do sleep 0.01; done; touch \"$1/second\"; echo second", "sh", (char*)directory, 0 };
    OracleRun first = { .cache = &cache, .purpose = "oracle test: first", .arguments = first_arguments, .dependencies = true, .timeout = 20 };
    OracleRun second = { .cache = &cache, .purpose = "oracle test: second", .arguments = second_arguments, .dependencies = true, .timeout = 20 };
    const bool started = oracle_run_start(&first) && oracle_run_start(&second);
    const bool finished = oracle_run_finish(&second) && oracle_run_finish(&first);
    oracle_test_expect(test, started && finished && strcmp(first.output, "first\n") == 0 && strcmp(second.output, "second\n") == 0,
                       "tracing: two runs at once, each answered while the other is waited for");
    oracle_run_release(&first);
    oracle_run_release(&second);
    // The tools' __TIMESTAMP__, and a sh run whose entry has its files' times.
    char* clang_arguments[] = { "clang", 0 };
    char* shell_arguments[] = { "sh", 0 };
    OracleRun clang_run = { .cache = &cache, .arguments = clang_arguments };
    OracleRun shell_run = { .cache = &cache, .arguments = shell_arguments };
    oracle_roots_begin(&clang_run.roots);
    oracle_roots_begin(&shell_run.roots);
    const bool clocks = oracle_tool_epoch(&clang_run) && !oracle_tool_epoch(&shell_run);
    char stamped[BUSTER_ORACLE_PATH_BYTES + 16];
    snprintf(stamped, sizeof(stamped), "%s/time.txt", directory);
    const struct timespec earlier[2] = { { .tv_sec = 946684800 }, { .tv_sec = 946684800 } };
    const struct timespec later[2] = { { .tv_sec = 978307200 }, { .tv_sec = 978307200 } };
    static const char stat_script[] = "stat -c %Y \"$1/time.txt\"";
    const bool times = oracle_test_write_text(directory, "time.txt", "same\n") && utimensat(AT_FDCWD, stamped, earlier, 0) == 0 &&
                       oracle_test_traced(&cache, stat_script, directory, false, "946684800\n") && oracle_test_traced(&cache, stat_script, directory, true, "946684800\n") &&
                       utimensat(AT_FDCWD, stamped, later, 0) == 0 && oracle_test_traced(&cache, stat_script, directory, false, "978307200\n");
    oracle_test_expect(test, clocks && times, "tracing: clang's __TIMESTAMP__ follows SOURCE_DATE_EPOCH, sh's doesn't, and its entries have its files' times");
    oracle_cache_close(&cache);
}

// --- Permissions and the kernel's paths ---

// What a run reads of a path besides its bytes, kind and identity, its permissions, with sh as the tool: a directory
// whose mode changes, which `test -x` reads (whose answer root's doesn't change, so that it is this process's own
// access(2) that the run must print), and a script in a root whose mode changes from 0755 to 0644, the same bytes,
// which execve then refuses, each run again; and where this process may chown (as root), a file given another owner.
static void oracle_test_permissions(OracleTest* test)
{
    OracleCache cache;
    oracle_test_open(test, &cache, "use", "permissions");
    const char* directory = test->runs[0];
    char searched[BUSTER_ORACLE_PATH_BYTES + 16];
    char script[BUSTER_ORACLE_PATH_BYTES + 16];
    char owned[BUSTER_ORACLE_PATH_BYTES + 16];
    snprintf(searched, sizeof(searched), "%s/searched", directory);
    snprintf(script, sizeof(script), "%s/script.sh", directory);
    snprintf(owned, sizeof(owned), "%s/owned.txt", directory);
    static const char search[] = "if test -x \"$1/searched\"; then echo yes; else echo no; fi";
    bool searches = oracle_test_directory(directory, "searched");
    for (uint32_t i = 0; searches && i < 4; i += 1)
    {
        // 0700 (run, taken), then 0600 (run, taken).
        searches &= chmod(searched, i < 2 ? 0700 : 0600) == 0;
        searches &= oracle_test_traced(&cache, search, directory, i == 1 || i == 3, access(searched, X_OK) == 0 ? "yes\n" : "no\n");
    }
    chmod(searched, 0700);
    oracle_test_expect(test, searches, "permissions: a directory whose mode changes, run again");
    static const char execute[] = "\"$1/script.sh\"; echo $?";
    bool executes = oracle_test_write_text(directory, "script.sh", "#!/bin/sh\necho ran\n");
    for (uint32_t i = 0; executes && i < 4; i += 1)
    {
        executes &= chmod(script, i < 2 ? 0755 : 0644) == 0;
        executes &= oracle_test_traced(&cache, execute, directory, i == 1 || i == 3, i < 2 ? "ran\n0\n" : "126\n");
    }
    oracle_test_expect(test, executes, "permissions: a script in a root made 0644 from 0755, the same bytes, run again");
    if (geteuid() == 0)
    {
        static const char read_owned[] = "cat \"$1/owned.txt\"";
        bool owners = oracle_test_write_text(directory, "owned.txt", "owned\n");
        owners &= oracle_test_traced(&cache, read_owned, directory, false, "owned\n") && oracle_test_traced(&cache, read_owned, directory, true, "owned\n");
        owners &= chown(owned, 1, 1) == 0 && oracle_test_traced(&cache, read_owned, directory, false, "owned\n");
        owners &= oracle_test_traced(&cache, read_owned, directory, true, "owned\n");
        oracle_test_expect(test, owners, "permissions: a file given another owner, run again");
    }
    else
    {
        printf("oracle cache: permissions: an owner's change not checked (it takes root's chown)\n");
    }
    oracle_cache_close(&cache);
}

// A traced clang run through `cache`, in `directory`, which writes `output` there if it isn't null: whether it ran with
// status 0 and took its entry as `taken` says, with what it wrote into `bytes` (malloc's, null without an output).
static bool oracle_test_compile(OracleCache* cache, char* const* arguments, const char* directory, const char* output, bool taken, char** bytes, uint64_t* length)
{
    const char* files[1] = { output };
    OracleRun run = {
        .cache = cache,
        .purpose = "oracle test: kernel",
        .arguments = arguments,
        .directory = directory,
        .files = files,
        .file_count = output != 0 ? 1 : 0,
        .dependencies = true,
    };
    const bool result = oracle_run(&run) && run.status == 0 && run.taken == taken;
    oracle_run_release(&run);
    char path[BUSTER_ORACLE_PATH_BYTES + 32];
    snprintf(path, sizeof(path), "%s/%s", directory, output != 0 ? output : "");
    *bytes = output != 0 ? oracle_test_read(path, length) : 0;
    return result && (output == 0 || *bytes != 0);
}

// What oracle_path_rule makes of paths, followed to their ends and not: the package manager's and the kernel's left
// out, /dev's kept but for a process's links, which are left out as links and refused when followed to what they lead
// to (but the standard descriptors and exe), and refused past them; ".." kept, or refused under /proc; and a path in a
// root kept even where it would be left out (a root under /usr).
static void oracle_test_path_rules(OracleTest* test)
{
    typedef struct OracleTestPath OracleTestPath;
    struct OracleTestPath
    {
        const char* path;
        OraclePath followed;
        OraclePath unfollowed;
    };
    static const OracleTestPath cases[] = {
        { "/usr/include/stdio.h", ORACLE_PATH_LEFT_OUT, ORACLE_PATH_LEFT_OUT },
        { "/usr/local/include/x.h", ORACLE_PATH_KEPT, ORACLE_PATH_KEPT },
        { "/usr/../home/x.h", ORACLE_PATH_KEPT, ORACLE_PATH_KEPT },
        { "/lib64/ld-linux-x86-64.so.2", ORACLE_PATH_LEFT_OUT, ORACLE_PATH_LEFT_OUT },
        { "/library/x.h", ORACLE_PATH_KEPT, ORACLE_PATH_KEPT },
        { "/etc/clang/clang.cfg", ORACLE_PATH_KEPT, ORACLE_PATH_KEPT },
        { "/proc/cpuinfo", ORACLE_PATH_LEFT_OUT, ORACLE_PATH_LEFT_OUT },
        { "/proc/self", ORACLE_PATH_LEFT_OUT, ORACLE_PATH_LEFT_OUT },
        { "/proc/self/exe", ORACLE_PATH_LEFT_OUT, ORACLE_PATH_LEFT_OUT },
        { "/proc/self/exe/x", ORACLE_PATH_REFUSED, ORACLE_PATH_REFUSED },
        { "/proc/self/maps", ORACLE_PATH_LEFT_OUT, ORACLE_PATH_LEFT_OUT },
        { "/proc/self/fdinfo/3", ORACLE_PATH_LEFT_OUT, ORACLE_PATH_LEFT_OUT },
        { "/proc/self/fd", ORACLE_PATH_LEFT_OUT, ORACLE_PATH_LEFT_OUT },
        { "/proc/self/fd/0", ORACLE_PATH_LEFT_OUT, ORACLE_PATH_LEFT_OUT },
        { "/proc/self/fd/2", ORACLE_PATH_LEFT_OUT, ORACLE_PATH_LEFT_OUT },
        { "/proc/self/fd/3", ORACLE_PATH_REFUSED, ORACLE_PATH_LEFT_OUT },
        { "/proc/self/fd/10", ORACLE_PATH_REFUSED, ORACLE_PATH_LEFT_OUT },
        { "/proc/self/fd/3/x", ORACLE_PATH_REFUSED, ORACLE_PATH_REFUSED },
        { "/proc/self/cwd", ORACLE_PATH_REFUSED, ORACLE_PATH_LEFT_OUT },
        { "/proc/self/cwd/main.c", ORACLE_PATH_REFUSED, ORACLE_PATH_REFUSED },
        { "/proc/1234/root/etc/passwd", ORACLE_PATH_REFUSED, ORACLE_PATH_REFUSED },
        { "/proc/thread-self/fd/7", ORACLE_PATH_REFUSED, ORACLE_PATH_LEFT_OUT },
        { "/proc/self/task/42/fd/9", ORACLE_PATH_REFUSED, ORACLE_PATH_LEFT_OUT },
        { "/proc/self/task/42/cwd/x.h", ORACLE_PATH_REFUSED, ORACLE_PATH_REFUSED },
        { "/proc/self/map_files/1000-2000", ORACLE_PATH_REFUSED, ORACLE_PATH_LEFT_OUT },
        { "/proc/selfish/fd/3", ORACLE_PATH_LEFT_OUT, ORACLE_PATH_LEFT_OUT },
        { "/proc/self/../1/fd/3", ORACLE_PATH_REFUSED, ORACLE_PATH_REFUSED },
        { "/sys/devices/system/cpu/online", ORACLE_PATH_LEFT_OUT, ORACLE_PATH_LEFT_OUT },
        { "/dev/null", ORACLE_PATH_KEPT, ORACLE_PATH_KEPT },
        { "/dev/shm/x.h", ORACLE_PATH_KEPT, ORACLE_PATH_KEPT },
        { "/dev/hugepages/x", ORACLE_PATH_KEPT, ORACLE_PATH_KEPT },
        { "/dev/fd", ORACLE_PATH_LEFT_OUT, ORACLE_PATH_LEFT_OUT },
        { "/dev/fd/1", ORACLE_PATH_LEFT_OUT, ORACLE_PATH_LEFT_OUT },
        { "/dev/fd/3", ORACLE_PATH_REFUSED, ORACLE_PATH_LEFT_OUT },
        { "/dev/stdin", ORACLE_PATH_LEFT_OUT, ORACLE_PATH_LEFT_OUT },
        { "/dev/stderr", ORACLE_PATH_LEFT_OUT, ORACLE_PATH_LEFT_OUT },
        { "/dev/stdin/x", ORACLE_PATH_REFUSED, ORACLE_PATH_REFUSED },
        { "/dev/stdinx", ORACLE_PATH_KEPT, ORACLE_PATH_KEPT },
        { "/dev/fd/../shm/x.h", ORACLE_PATH_REFUSED, ORACLE_PATH_REFUSED },
        { "/usr/buster-root-Ab12Cd/main.c", ORACLE_PATH_KEPT, ORACLE_PATH_KEPT },
        { "/usr/buster-root-Ab12Cd", ORACLE_PATH_KEPT, ORACLE_PATH_KEPT },
        { "/usr/buster-root-Ab12Cdx/main.c", ORACLE_PATH_LEFT_OUT, ORACLE_PATH_LEFT_OUT },
    };
    // A run whose root is under /usr, where $TMPDIR could be.
    static const char root[] = "/usr/buster-root-Ab12Cd";
    OracleRun run = { .arguments = 0 };
    run.roots.roots[0] = (OracleRoot){ .name = root, .length = (uint32_t)strlen(root), .random = (uint32_t)strlen(root) - 6 };
    run.roots.count = 1;
    bool right = true;
    for (uint32_t c = 0; c < sizeof(cases) / sizeof(cases[0]); c += 1)
    {
        const OraclePath followed = oracle_path_rule(&run, cases[c].path, true);
        const OraclePath unfollowed = oracle_path_rule(&run, cases[c].path, false);
        if (followed != cases[c].followed || unfollowed != cases[c].unfollowed)
        {
            fprintf(stderr, "oracle cache: %s: %d followed and %d not, not %d and %d\n", cases[c].path, followed, unfollowed, cases[c].followed, cases[c].unfollowed);
            right = false;
        }
    }
    oracle_test_expect(test, right, "kernel: the path rule's answers, followed and not");
}

// The kernel's paths and /dev's: a source in a root in /dev/shm, an ordinary tmpfs and a common $TMPDIR, which clang
// compiles to an object, changed to give another, and a header in /dev/shm in no root ($TMPDIR elsewhere), changed in
// place, each compiled again; a run that follows a process's links into the file system (cat of /proc/self/cwd/FILE
// and of /dev/fd/3) isn't kept; and one that reads a descriptor's link (readlink of /proc/self/fd/0, as LLVM reads its
// files' real paths), its standard input through /dev/stdin and writes /dev/null, a device node, is.
static void oracle_test_kernel(OracleTest* test)
{
    OracleCache cache;
    oracle_test_open(test, &cache, "use", "kernel");
    struct stat shm;
    if (stat("/dev/shm", &shm) == 0 && S_ISDIR(shm.st_mode) && access("/dev/shm", W_OK | X_OK) == 0)
    {
        const char* saved = getenv("TMPDIR");
        char* kept = saved != 0 ? strdup(saved) : 0;
        setenv("TMPDIR", "/dev/shm", 1);
        char root[] = "/dev/shm/buster-oracle-shm-XXXXXX";
        bool compiled = mkdtemp(root) != 0;
        char* compile[] = { "clang", "-c", "-O0", "main.c", "-o", "main.o", 0 };
        char* objects[3] = { 0 };
        uint64_t lengths[3] = { 0 };
        for (uint32_t i = 0; compiled && i < 3; i += 1)
        {
            // Compiled, taken, and compiled again once the source gives another object.
            compiled &= i == 1 || oracle_test_write_text(root, "main.c", i == 0 ? "int f(void) { return 1; }\n" : "int f(void) { return 2; }\n");
            compiled &= oracle_test_compile(&cache, compile, root, "main.o", i == 1, &objects[i], &lengths[i]);
        }
        compiled &= lengths[0] == lengths[1] && memcmp(objects[0], objects[1], lengths[0]) == 0 && (lengths[2] != lengths[0] || memcmp(objects[0], objects[2], lengths[0]) != 0);
        for (uint32_t i = 0; i < 3; i += 1)
        {
            free(objects[i]);
        }
        oracle_test_unlink(root, "main.c");
        oracle_test_unlink(root, "main.o");
        rmdir(root);
        if (kept != 0)
        {
            setenv("TMPDIR", kept, 1);
        }
        else
        {
            unsetenv("TMPDIR");
        }
        free(kept);
        oracle_test_expect(test, compiled, "kernel: a source in a root in /dev/shm, $TMPDIR, changed, compiled again");
        // A header in /dev/shm, in no root, by its identity.
        char shared[] = "/dev/shm/buster-oracle-header-XXXXXX";
        char option[sizeof(shared) + 8];
        bool included = mkdtemp(shared) != 0 && oracle_test_write_text(shared, "shm.h", "int shm = 1;\n") &&
                        oracle_test_write_text(test->runs[0], "shm.c", "#include \"shm.h\"\nint get(void) { return shm; }\n");
        snprintf(option, sizeof(option), "-I%s", shared);
        char* header[] = { "clang", "-fsyntax-only", option, "shm.c", 0 };
        for (uint32_t i = 0; included && i < 3; i += 1)
        {
            char* none = 0;
            uint64_t length = 0;
            included &= i != 2 || oracle_test_write_text(shared, "shm.h", "int shm = 22;\n");
            included &= oracle_test_compile(&cache, header, test->runs[0], 0, i == 1, &none, &length);
        }
        oracle_test_unlink(shared, "shm.h");
        rmdir(shared);
        oracle_test_expect(test, included, "kernel: a header in /dev/shm in no root, changed, compiled again");
    }
    else
    {
        printf("oracle cache: kernel: /dev/shm not checked (there is none to write)\n");
    }
    const char* directory = test->runs[0];
    static const char through_cwd[] = "cd \"$1\" && cat /proc/self/cwd/linked.txt";
    static const char through_descriptor[] = "cat /dev/fd/3 3< \"$1/linked.txt\"";
    static const char named[] = "readlink /proc/self/fd/3 3< \"$1/linked.txt\" > /dev/null && cat /dev/stdin \"$1/linked.txt\"";
    const uint64_t unkept = cache.counts->unkept;
    bool refused = oracle_test_write_text(directory, "linked.txt", "linked\n");
    for (uint32_t i = 0; refused && i < 2; i += 1)
    {
        refused &= oracle_test_traced(&cache, through_cwd, directory, false, "linked\n") && oracle_test_traced(&cache, through_descriptor, directory, false, "linked\n");
    }
    oracle_test_expect(test, refused && cache.counts->unkept == unkept + 4, "kernel: a run that follows a process's links into the file system, not kept");
    const bool links = oracle_test_traced(&cache, named, directory, false, "linked\n") && oracle_test_traced(&cache, named, directory, true, "linked\n");
    oracle_test_expect(test, links && cache.counts->unkept == unkept + 4, "kernel: a descriptor's link, /dev/stdin and /dev/null, kept");
    oracle_cache_close(&cache);
}

// What a run reads of the paths it makes and removes, with sh as the tool: whether mkdir's path is there, and its
// directory, which isn't yet; and rmdir's directory's names, which must be none; each run again when it changes.
static void oracle_test_changes(OracleTest* test)
{
    OracleCache cache;
    oracle_test_open(test, &cache, "use", "changes");
    const char* directory = test->runs[0];
    char full[BUSTER_ORACLE_PATH_BYTES + 32];
    char parent[BUSTER_ORACLE_PATH_BYTES + 32];
    snprintf(full, sizeof(full), "%s/full", directory);
    snprintf(parent, sizeof(parent), "%s/parent", directory);
    oracle_test_remove(directory, "made");
    oracle_test_remove(directory, "parent/child");
    oracle_test_remove(directory, "full");
    // mkdir makes its directory, which the same run then finds there: run again.
    static const char make[] = "if mkdir \"$1/made\" 2>/dev/null; then echo made; else echo there; fi";
    bool makes = oracle_test_traced(&cache, make, directory, false, "made\n") && oracle_test_traced(&cache, make, directory, false, "there\n");
    makes &= oracle_test_traced(&cache, make, directory, true, "there\n");
    oracle_test_expect(test, makes, "changes: mkdir's path, which it made, there, run again");
    // mkdir in a directory that isn't there, which a path that isn't there either doesn't show, until it is.
    static const char nest[] = "if mkdir \"$1/parent/child\" 2>/dev/null; then echo made; else echo failed; fi";
    bool nests = oracle_test_traced(&cache, nest, directory, false, "failed\n") && oracle_test_traced(&cache, nest, directory, true, "failed\n");
    nests &= mkdir(parent, 0700) == 0 && oracle_test_traced(&cache, nest, directory, false, "made\n");
    oracle_test_expect(test, nests, "changes: mkdir in a directory made since, run again");
    // rmdir of a directory with a file in it, which goes once the file does.
    static const char empty[] = "if rmdir \"$1/full\" 2>/dev/null; then echo removed; else echo kept; fi";
    bool empties = mkdir(full, 0700) == 0 && oracle_test_write_text(full, "file", "x\n");
    empties &= oracle_test_traced(&cache, empty, directory, false, "kept\n") && oracle_test_traced(&cache, empty, directory, true, "kept\n");
    oracle_test_unlink(full, "file");
    empties &= oracle_test_traced(&cache, empty, directory, false, "removed\n");
    oracle_test_expect(test, empties, "changes: rmdir of a directory whose file goes, run again");
    oracle_test_remove(directory, "made");
    oracle_test_remove(directory, "parent/child");
    oracle_test_remove(directory, "full");
    oracle_cache_close(&cache);
}

// A program of the test's own, for the calls sh doesn't make: `openat2 DIRECTORY NAME RESOLVE` prints what openat2 opens
// as NAME from DIRECTORY with RESOLVE's flags ("none" if it can't), and `chroot`, `unshare`, `getxattr PATH` and `statfs
// PATH` make their calls, whatever they return, and print their names.
static const char oracle_test_helper[] =
    "#define _GNU_SOURCE\n"
    "#include <fcntl.h>\n"
    "#include <stdint.h>\n"
    "#include <stdio.h>\n"
    "#include <stdlib.h>\n"
    "#include <string.h>\n"
    "#include <sys/syscall.h>\n"
    "#include <sys/vfs.h>\n"
    "#include <sys/xattr.h>\n"
    "#include <unistd.h>\n"
    "int main(int argc, char** argv)\n"
    "{\n"
    "    if (argc == 5 && strcmp(argv[1], \"openat2\") == 0)\n"
    "    {\n"
    "        struct { uint64_t flags; uint64_t mode; uint64_t resolve; } how = { O_RDONLY, 0, strtoull(argv[4], 0, 0) };\n"
    "        const int directory = open(argv[2], O_PATH | O_DIRECTORY);\n"
    "        const int file = (int)syscall(SYS_openat2, directory, argv[3], &how, sizeof(how));\n"
    "        char bytes[256];\n"
    "        const ssize_t got = file >= 0 ? read(file, bytes, sizeof(bytes)) : -1;\n"
    "        if (got >= 0) fwrite(bytes, 1, (size_t)got, stdout); else puts(\"none\");\n"
    "    }\n"
    "    else if (argc == 2 && strcmp(argv[1], \"chroot\") == 0) { if (chroot(\"/\")) {} puts(\"chroot\"); }\n"
    "    else if (argc == 2 && strcmp(argv[1], \"unshare\") == 0) { syscall(SYS_unshare, 0); puts(\"unshare\"); }\n"
    "    else if (argc == 3 && strcmp(argv[1], \"getxattr\") == 0) { char value[16]; getxattr(argv[2], \"user.oracle\", value, sizeof(value)); puts(\"getxattr\"); }\n"
    "    else if (argc == 3 && strcmp(argv[1], \"statfs\") == 0) { struct statfs figures; statfs(argv[2], &figures); puts(\"statfs\"); }\n"
    "    return 0;\n"
    "}\n";

// A traced run of `arguments` through `cache`: whether it ran, took its entry as `taken` says and printed `output`.
static bool oracle_test_traced_tool(OracleCache* cache, char* const* arguments, bool taken, const char* output)
{
    OracleRun run = { .cache = cache, .purpose = "oracle test: traced tool", .arguments = arguments, .dependencies = true };
    const bool result = oracle_run(&run) && run.taken == taken && run.output != 0 && strcmp(run.output, output) == 0;
    oracle_run_release(&run);
    return result;
}

// What the tracing can't follow, with the test's program as the tool: openat2 with RESOLVE_IN_ROOT, which reads an
// absolute name from its descriptor's directory, isn't kept, and with no resolution flags is, and run again when its
// file changes; chroot, unshare and getxattr aren't kept; statfs, as libselinux calls it, is.
static void oracle_test_unfollowed(OracleTest* test)
{
    OracleCache cache;
    OracleCache off;
    oracle_test_open(test, &cache, "use", "unfollowed");
    oracle_test_open(test, &off, "off", "unfollowed-off");
    char source[BUSTER_ORACLE_PATH_BYTES + 32];
    char helper[BUSTER_ORACLE_PATH_BYTES + 32];
    char header[BUSTER_ORACLE_PATH_BYTES + 32];
    snprintf(source, sizeof(source), "%s/helper.c", test->base);
    snprintf(helper, sizeof(helper), "%s/helper", test->base);
    snprintf(header, sizeof(header), "%s/header.h", test->runs[0]);
    char* compile[] = { "clang", "-O0", "-w", "-o", helper, source, 0 };
    OracleRun build = { .cache = &off, .arguments = compile };
    const bool built = oracle_test_write(source, oracle_test_helper, strlen(oracle_test_helper)) && oracle_run(&build) && WIFEXITED(build.status) &&
                       WEXITSTATUS(build.status) == 0;
    oracle_run_release(&build);
    char* plain[] = { helper, "openat2", "/", header, "0", 0 };
    // RESOLVE_IN_ROOT is 0x10.
    char* rooted[] = { helper, "openat2", test->runs[0], "/header.h", "16", 0 };
    char* rooting[] = { helper, "chroot", 0 };
    char* sharing[] = { helper, "unshare", 0 };
    char* attributes[] = { helper, "getxattr", header, 0 };
    char maybe[BUSTER_ORACLE_PATH_BYTES + 32];
    snprintf(maybe, sizeof(maybe), "%s/maybe-statfs", test->runs[0]);
    unlink(maybe);
    char* figures[] = { helper, "statfs", maybe, 0 };
    const bool opens = built && oracle_test_write_text(test->runs[0], "header.h", "one\n") && oracle_test_traced_tool(&cache, plain, false, "one\n");
    if (opens)
    {
        bool plainly = oracle_test_traced_tool(&cache, plain, true, "one\n");
        plainly &= oracle_test_write_text(test->runs[0], "header.h", "two\n") && oracle_test_traced_tool(&cache, plain, false, "two\n");
        oracle_test_expect(test, plainly, "unfollowed: openat2 without resolution flags kept, and run again when its file changes");
        const uint64_t unkept = cache.counts->unkept;
        bool resolved = oracle_test_traced_tool(&cache, rooted, false, "two\n");
        resolved &= oracle_test_write_text(test->runs[0], "header.h", "three\n") && oracle_test_traced_tool(&cache, rooted, false, "three\n");
        oracle_test_expect(test, resolved && cache.counts->unkept == unkept + 2, "unfollowed: openat2 with RESOLVE_IN_ROOT, not kept");
    }
    else
    {
        printf("oracle cache: unfollowed: openat2 not checked (%s)\n", built ? "the kernel has no openat2" : "the test's program didn't build");
    }
    const uint64_t unkept = cache.counts->unkept;
    bool refused = built && oracle_test_traced_tool(&cache, rooting, false, "chroot\n") && oracle_test_traced_tool(&cache, sharing, false, "unshare\n");
    refused &= oracle_test_traced_tool(&cache, attributes, false, "getxattr\n");
    oracle_test_expect(test, refused && cache.counts->unkept == unkept + 3, "unfollowed: chroot, unshare and getxattr, not kept");
    // statfs of a path that isn't there, kept and taken, then run again once it is.
    bool statfs_kept = built && oracle_test_traced_tool(&cache, figures, false, "statfs\n") && oracle_test_traced_tool(&cache, figures, true, "statfs\n");
    statfs_kept &= oracle_test_write_text(test->runs[0], "maybe-statfs", "") && oracle_test_traced_tool(&cache, figures, false, "statfs\n");
    unlink(maybe);
    oracle_test_expect(test, statfs_kept, "unfollowed: statfs, kept as a path's look, and run again when its path is made");
    unlink(source);
    unlink(helper);
    oracle_test_unlink(test->runs[0], "header.h");
    oracle_cache_close(&cache);
    oracle_cache_close(&off);
}

// --- Process groups ---

// A run's process group: what a run that ends leaves running in it is killed, and so is what a run killed at its time
// limit started; the limit is on the monotonic clock, with a pidfd to wait on and without one.
static void oracle_test_groups(OracleTest* test)
{
    OracleCache cache;
    oracle_test_open(test, &cache, "use", "groups");
    char ended_marker[BUSTER_ORACLE_PATH_BYTES + 16];
    char killed_marker[BUSTER_ORACLE_PATH_BYTES + 16];
    snprintf(ended_marker, sizeof(ended_marker), "%s/ended", test->runs[0]);
    snprintf(killed_marker, sizeof(killed_marker), "%s/killed", test->runs[0]);
    unlink(ended_marker);
    unlink(killed_marker);
    // A run that ends at once, leaving a process that would write its marker a second later.
    char* leaves[] = { "sh", "-c", "(sleep 1; echo late > \"$1\") & exit 0", "sh", ended_marker, 0 };
    OracleRun run = { .cache = &cache, .purpose = "oracle test: a group left running", .arguments = leaves };
    const bool ended = oracle_run(&run) && WIFEXITED(run.status) && WEXITSTATUS(run.status) == 0;
    oracle_run_release(&run);
    // Runs past their time limit, whose processes would write their marker 1.5 s after they started.
    bool limited = true;
    for (uint32_t pidfd = 0; pidfd < 2; pidfd += 1)
    {
        oracle_without_pidfd = pidfd == 1;
        char* slow[] = { "sh", "-c", "(sleep 1.5; echo late > \"$1\") & sleep 20", "sh", killed_marker, 0 };
        const uint64_t unkept = cache.counts->unkept;
        const uint64_t start = oracle_now();
        run = (OracleRun){ .cache = &cache, .purpose = "oracle test: a group past its time", .arguments = slow, .timeout = 1 };
        const bool killed = !oracle_run(&run) && run.killed;
        const uint64_t elapsed = oracle_now() - start;
        oracle_run_release(&run);
        limited &= killed && elapsed >= 1000000000ull && elapsed < 3000000000ull && cache.counts->unkept == unkept + 1;
    }
    oracle_without_pidfd = false;
    // A run kept without a limit, then the same run with a limit it doesn't pass: killed, not taken.
    char* sleeper[] = { "sh", "-c", "sleep 1.1; echo done", 0 };
    run = (OracleRun){ .cache = &cache, .purpose = "oracle test: a limit given later", .arguments = sleeper, .dependencies = true };
    const bool unlimited = oracle_run(&run) && run.output != 0 && strcmp(run.output, "done\n") == 0;
    oracle_run_release(&run);
    run = (OracleRun){ .cache = &cache, .purpose = "oracle test: a limit given later", .arguments = sleeper, .dependencies = true, .timeout = 1 };
    const bool shortened = !oracle_run(&run) && run.killed && !run.taken;
    oracle_run_release(&run);
    oracle_test_expect(test, unlimited && shortened, "process groups: a run kept without a limit, killed at a limit given later");
    // Past both markers' times, from the last run's start.
    const struct timespec pause = { .tv_sec = 0, .tv_nsec = 800000000 };
    nanosleep(&pause, 0);
    struct stat status;
    oracle_test_expect(test, limited, "process groups: runs past their time limit killed on the monotonic clock, with a pidfd and without");
    oracle_test_expect(test, ended && lstat(ended_marker, &status) != 0 && lstat(killed_marker, &status) != 0,
                       "process groups: what a run leaves running in its group killed, when it ends and at its time limit");
    oracle_cache_close(&cache);
}

// --- Check mode ---

static void oracle_test_check(OracleTest* test)
{
    OracleCache use;
    OracleCache check;
    oracle_test_open(test, &use, "use", "check");
    oracle_test_open(test, &check, "check", "check");
    char* value[] = { "sh", "-c", "cat \"$1/value\"", "sh", test->runs[0], 0 };
    OracleRun run;
    bool right = oracle_test_write_text(test->runs[0], "value", "a\n");
    right &= oracle_test_go(&run, &use, "oracle test: check", value, 0, 0) && !run.taken && strcmp(run.output, "a\n") == 0;
    oracle_run_release(&run);
    oracle_test_write_text(test->runs[0], "value", "b\n");
    right &= oracle_test_go(&run, &use, "oracle test: check", value, 0, 0) && run.taken && strcmp(run.output, "a\n") == 0;
    oracle_run_release(&run);
    // The stale entry: the run fails, its result is the tool's, and the entry is written again.
    printf("oracle cache: an entry made stale on purpose, which the next line reports:\n");
    fflush(stdout);
    const bool stale = !oracle_test_go(&run, &check, "oracle test: check", value, 0, 0) && !run.taken && run.output != 0 && strcmp(run.output, "b\n") == 0;
    oracle_run_release(&run);
    right &= oracle_test_go(&run, &use, "oracle test: check", value, 0, 0) && run.taken && strcmp(run.output, "b\n") == 0;
    oracle_run_release(&run);
    right &= oracle_test_go(&run, &check, "oracle test: check", value, 0, 0) && !run.taken;
    oracle_run_release(&run);
    oracle_test_expect(test, right && stale && check.counts->stale == 1, "check: a stale entry fails its run and is written again");
    // An entry whose run read other bytes isn't stale.
    char* main_source[] = { "main.c", 0 };
    char errors[4096];
    bool taken = false;
    int status = 0;
    oracle_test_write_text(test->runs[0], "h.h", "#define X 4\n");
    const bool kept = oracle_test_clang(&use, test->runs[0], main_source, &taken, &status, errors, sizeof(errors)) && !taken;
    oracle_test_write_text(test->runs[0], "h.h", "#define X 5\n");
    const bool current = oracle_test_clang(&check, test->runs[0], main_source, &taken, &status, errors, sizeof(errors)) && !taken;
    oracle_test_expect(test, kept && current && check.counts->stale == 1, "check: an entry whose run read other bytes isn't stale");
    oracle_cache_close(&use);
    oracle_cache_close(&check);
}

// --- Damaged entries ---

// Rewrites an entry's file with the u32 at `offset` set to `value`, or cut to `length` bytes, or with a byte more.
static bool oracle_test_damage(const char* path, int64_t offset, uint32_t value, int64_t length, bool longer)
{
    uint64_t size = 0;
    char* bytes = oracle_test_read(path, &size);
    bool result = bytes != 0 && (offset < 0 || (uint64_t)offset + sizeof(value) <= size);
    if (result && offset >= 0)
    {
        memcpy(bytes + offset, &value, sizeof(value));
    }
    const uint64_t written = length >= 0 ? (uint64_t)length : size;
    char* more = result && longer ? realloc(bytes, size + 1) : 0;
    if (more != 0)
    {
        bytes = more;
        bytes[size] = 'x';
    }
    result = result && (!longer || more != 0) && oracle_test_write(path, bytes, longer ? size + 1 : written);
    free(bytes);
    return result;
}

static void oracle_test_damaged(OracleTest* test)
{
    OracleCache cache;
    oracle_test_open(test, &cache, "use", "damaged");
    char* arguments[] = { "sh", "-c", "printf 'out %s\\n' \"$1\" >&2; printf 'x'", "sh", test->runs[0], 0 };
    OracleRun run;
    const bool made = oracle_test_go(&run, &cache, "oracle test: damage", arguments, 0, 0) && !run.taken;
    char path[BUSTER_ORACLE_PATH_BYTES + 80];
    oracle_entry_path(&cache, run.key, path, sizeof(path));
    oracle_run_release(&run);
    uint32_t key_word = 0;
    memcpy(&key_word, run.key.bytes + 4, sizeof(key_word));
    typedef struct OracleTestDamage OracleTestDamage;
    struct OracleTestDamage
    {
        const char* name;
        int64_t offset;
        uint32_t value;
        int64_t length;
        bool longer;
    };
    const OracleTestDamage damages[] = {
        { "a changed magic", 0, 0x54535543u, -1, false },
        { "a changed key", (int64_t)offsetof(OracleEntryHeader, key) + 4, key_word ^ 1u, -1, false },
        { "a changed body length", (int64_t)offsetof(OracleEntryHeader, body), 7, -1, false },
        { "too many roots", (int64_t)offsetof(OracleEntryHeader, roots), BUSTER_ORACLE_ROOTS + 1, -1, false },
        { "a dependency count past its records", (int64_t)offsetof(OracleEntryHeader, dependencies), 3, -1, false },
        { "a flag it doesn't know", (int64_t)offsetof(OracleEntryHeader, flags), 2, -1, false },
        { "cut short", -1, 0, -2, false },
        { "cut inside its header", -1, 0, 10, false },
        { "a byte longer", -1, 0, -1, true },
    };
    for (uint32_t i = 0; made && i < sizeof(damages) / sizeof(damages[0]); i += 1)
    {
        uint64_t size = 0;
        free(oracle_test_read(path, &size));
        const int64_t length = damages[i].length == -2 ? (int64_t)size - 1 : damages[i].length;
        const bool damaged = oracle_test_damage(path, damages[i].offset, damages[i].value, length, damages[i].longer);
        const bool runs = oracle_test_go(&run, &cache, "oracle test: damage", arguments, 0, 0) && !run.taken;
        oracle_run_release(&run);
        const bool whole = oracle_test_go(&run, &cache, "oracle test: damage", arguments, 0, 0) && run.taken && strcmp(run.output, "x") == 0;
        oracle_run_release(&run);
        char what[128];
        snprintf(what, sizeof(what), "damaged entries: %s, run again and written whole", damages[i].name);
        oracle_test_expect(test, damaged && runs && whole, what);
    }
    oracle_test_expect(test, made, "damaged entries: an entry to damage");
    oracle_cache_close(&cache);
}

// --- Modes ---

static void oracle_test_modes(OracleTest* test)
{
    OracleCache cache;
    char* arguments[] = { "sh", "-c", "printf 'x'", 0 };
    static const char* const modes[] = { 0, "bogus", "use" };
    bool off = true;
    for (uint32_t m = 0; m < sizeof(modes) / sizeof(modes[0]); m += 1)
    {
        char base[BUSTER_ORACLE_PATH_BYTES];
        snprintf(base, sizeof(base), "%s/modes", test->base);
        oracle_cache_open(&cache, modes[m], m < 2 ? base : 0);
        OracleRun run;
        for (uint32_t i = 0; i < 2; i += 1)
        {
            off &= oracle_test_go(&run, &cache, "oracle test: modes", arguments, 0, 0) && !run.taken && strcmp(run.output, "x") == 0;
            oracle_run_release(&run);
        }
        char directory[BUSTER_ORACLE_PATH_BYTES + 16];
        snprintf(directory, sizeof(directory), "%s/oracle", base);
        struct stat status;
        off &= !oracle_cache_active(&cache) && stat(directory, &status) != 0;
        oracle_cache_close(&cache);
    }
    oracle_test_expect(test, off, "modes: off, a mode that names none, and no directory keep nothing");
}

// --- Writers at once ---

static void oracle_test_writers(OracleTest* test)
{
    OracleCache cache;
    oracle_test_open(test, &cache, "use", "writers");
    char scripts[4][64];
    for (uint32_t k = 0; k < 4; k += 1)
    {
        snprintf(scripts[k], sizeof(scripts[k]), "head -c %u /dev/zero | tr '\\0' '%c'", 16384 * (k + 1), 'a' + k);
    }
    fflush(stdout);
    pid_t children[8];
    for (uint32_t c = 0; c < 8; c += 1)
    {
        children[c] = fork();
        if (children[c] == 0)
        {
            bool right = true;
            for (uint32_t round = 0; round < 16; round += 1)
            {
                for (uint32_t k = 0; k < 4; k += 1)
                {
                    char* arguments[] = { "sh", "-c", scripts[(k + c) % 4], 0 };
                    OracleRun run;
                    right &= oracle_test_go(&run, &cache, "oracle test: writers", arguments, 0, 0) && run.output_length == 16384 * ((k + c) % 4 + 1);
                    oracle_run_release(&run);
                }
            }
            _exit(right ? 0 : 1);
        }
    }
    bool right = true;
    for (uint32_t c = 0; c < 8; c += 1)
    {
        int status = 0;
        right &= children[c] > 0 && waitpid(children[c], &status, 0) == children[c] && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    }
    const uint64_t counted = cache.counts->taken + cache.counts->run;
    for (uint32_t k = 0; k < 4; k += 1)
    {
        char* arguments[] = { "sh", "-c", scripts[k], 0 };
        OracleRun run;
        right &= oracle_test_go(&run, &cache, "oracle test: writers", arguments, 0, 0) && run.taken && run.output_length == 16384 * (k + 1) && run.output[0] == 'a' + (char)k;
        oracle_run_release(&run);
    }
    uint32_t files = 0;
    uint32_t others = 0;
    oracle_test_count(&cache, &files, &others);
    oracle_test_expect(test, right && counted == 8 * 16 * 4 && files == 4 && others == 0, "writers at once: counts added up, entries whole, nothing left beside them");
    oracle_cache_close(&cache);
}

// --- The process's cache ---

// From BUSTER_ORACLE_CACHE and XDG_CACHE_HOME, its directory made private three levels deep.
static void oracle_test_process(OracleTest* test)
{
    char cache_home[BUSTER_ORACLE_PATH_BYTES];
    snprintf(cache_home, sizeof(cache_home), "%s/process/one/two", test->base);
    fflush(stdout);
    const pid_t child = fork();
    if (child == 0)
    {
        setenv("BUSTER_ORACLE_CACHE", "use", 1);
        setenv("XDG_CACHE_HOME", cache_home, 1);
        oracle_cache_state = (OracleCache){ 0 };
        const OracleCache* cache = oracle_cache();
        char expected[BUSTER_ORACLE_PATH_BYTES + 32];
        snprintf(expected, sizeof(expected), "%s/buster/oracle", cache_home);
        struct stat status;
        const bool right = cache->mode == ORACLE_MODE_USE && strcmp(cache->directory, expected) == 0 && stat(expected, &status) == 0 && S_ISDIR(status.st_mode) &&
                           (status.st_mode & 0777) == 0700;
        // And turned off, as a harness's test turns it off: nothing kept.
        oracle_cache_disable();
        char* arguments[] = { "sh", "-c", "printf x", 0 };
        OracleRun run = { .purpose = "oracle test: disabled", .arguments = arguments };
        const bool off = !oracle_cache_active(oracle_cache()) && oracle_run(&run) && !run.taken && strcmp(run.output, "x") == 0;
        oracle_run_release(&run);
        _exit(right && off ? 0 : 1);
    }
    int status = 0;
    oracle_test_expect(test, child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0,
                       "the process's cache from BUSTER_ORACLE_CACHE and XDG_CACHE_HOME, made private, and turned off");
}

static int run_oracle_cache_tests(int argc, char** argv)
{
    (void)argv;
    OracleTest test = { 0 };
    const char* temporary = getenv("TMPDIR");
    temporary = temporary != 0 && temporary[0] != 0 ? temporary : "/tmp";
    snprintf(test.base, sizeof(test.base), "%s/buster-oracle-test-XXXXXX", temporary);
    snprintf(test.runs[0], sizeof(test.runs[0]), "%s/buster-oracle-run-XXXXXX", temporary);
    snprintf(test.runs[1], sizeof(test.runs[1]), "%s/buster-oracle-run-XXXXXX", temporary);
    const bool made = argc == 0 && mkdtemp(test.base) != 0;
    const bool runs = made && mkdtemp(test.runs[0]) != 0 && mkdtemp(test.runs[1]) != 0;
    char clang[BUSTER_ORACLE_PATH_BYTES];
    const bool tools = oracle_find_tool("sh", clang, sizeof(clang)) && oracle_find_tool("clang", clang, sizeof(clang));
    // The compilers' runs are kept with SOURCE_DATE_EPOCH alone.
    const bool epoch = oracle_epoch_begin();
    if (runs && tools)
    {
        oracle_test_sha(&test);
        oracle_test_roots(&test);
        oracle_test_runs(&test);
        oracle_test_dependencies(&test);
        oracle_test_identities(&test);
        oracle_test_tracing(&test);
        oracle_test_permissions(&test);
        oracle_test_path_rules(&test);
        oracle_test_kernel(&test);
        oracle_test_changes(&test);
        oracle_test_unfollowed(&test);
        oracle_test_groups(&test);
        oracle_test_check(&test);
        oracle_test_damaged(&test);
        oracle_test_modes(&test);
        oracle_test_writers(&test);
        oracle_test_process(&test);
        printf("oracle cache: %u checks, %u failures\n", test.checks, test.failures);
    }
    else
    {
        fprintf(stderr, "usage: test-oracle-cache (with sh and clang on PATH, and $TMPDIR or /tmp writable)\n");
    }
    oracle_epoch_end(epoch);
    static const char* const directories[] = { "runs/oracle",        "dependencies/oracle", "identities/oracle", "tracing/oracle",
                                               "permissions/oracle", "kernel/oracle",       "changes/oracle",    "unfollowed/oracle",
                                               "groups/oracle",      "check/oracle",        "damaged/oracle",    "writers/oracle",
                                               "process/one/two/buster/oracle",             "" };
    for (uint32_t i = 0; made && i < sizeof(directories) / sizeof(directories[0]); i += 1)
    {
        oracle_test_remove(test.base, directories[i]);
    }
    for (uint32_t r = 0; runs && r < 2; r += 1)
    {
        oracle_test_remove(test.runs[r], "sub");
        oracle_test_remove(test.runs[r], "other");
        oracle_test_remove(test.runs[r], "searched");
        oracle_test_remove(test.runs[r], "");
    }
    return runs && tools && test.failures == 0 ? 0 : 1;
}

#endif
