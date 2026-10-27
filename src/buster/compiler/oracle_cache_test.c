// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// test-oracle-cache: the oracle cache's checks (oracle_cache.c), in a directory of their own under $TMPDIR (or /tmp)
// and two more of one template beside it, with sh and clang as the tools:
//   - SHA-256: FIPS 180-4's examples (the empty message, "abc", the 448-bit message, a million a's), and every length
//     from 0 to 300 bytes hashed whole and in random pieces, which must agree;
//   - roots: a directory's, an option's and a file's (its random characters before its suffix), names that aren't
//     roots, two in order, more than BUSTER_ORACLE_ROOTS, a root followed by a name's character (not it), two roots
//     replaced by each other in one pass, and keys that are alike only where the random characters differ;
//   - runs: a run's status, output, errors and written file, taken by the same run again and by one in the other
//     directory, whose output and file (NUL bytes among them) name its own directory; each part of a key (the input's
//     bytes, an argument's byte, where the arguments split, a variable's value, one set to nothing against one that
//     isn't set, the purpose, the tool's path and its file's times) making a new one, and HOME not; a tool not on
//     PATH; a file the run didn't write removed when it is taken;
//   - dependencies (clang -fsyntax-only): taken in the same and the other directory, again after a header changes,
//     a file is added to the directory, #embed's resource or what __has_include found changes; errors naming
//     absolute paths, taken with the other directory's; a missing #include and -M options, not kept;
//   - identities: a hard link replaced by a copy of its bytes, which #pragma once takes again; __TIMESTAMP__'s files'
//     times; __DATE__ without SOURCE_DATE_EPOCH, not kept, and with it, kept; shared directories, a file added beside
//     the source taken and a dependency changed not; a run past its time limit, killed and not kept;
//   - check mode: an entry made stale (by a file its key doesn't have) fails the run and is written again; an entry
//     whose run read other bytes isn't stale;
//   - damaged entries: a changed magic, key, body length, root or dependency count, one cut short, cut inside its
//     header or one byte longer, run again and written whole;
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

// A run of clang -fsyntax-only with `arguments` after it, in `directory` (null: this process's), its dependencies
// listed: whether it ran, whether it took its entry, and its status and errors.
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

static void oracle_test_dependencies(OracleTest* test)
{
    OracleCache cache;
    oracle_test_open(test, &cache, "use", "dependencies");
    bool ready = true;
    for (uint32_t d = 0; d < 2; d += 1)
    {
        ready &= oracle_test_write_text(test->runs[d], "main.c", "#include \"h.h\"\nint x = X;\n") && oracle_test_write_text(test->runs[d], "h.h", "#define X 1\n");
        ready &= oracle_test_write_text(test->runs[d], "bad.c", "int y = ;\n");
        ready &= oracle_test_write_text(test->runs[d], "data.bin", "abc");
        ready &= oracle_test_write_text(test->runs[d], "embed.c", "#if __has_include(\"h.h\")\n#endif\nconst char d[] = {\n#embed \"data.bin\"\n};\n");
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
    oracle_test_write_text(test->runs[1], "new.h", "\n");
    const bool added = oracle_test_clang(&cache, test->runs[1], main_source, &taken, &status, errors, sizeof(errors)) && !taken;
    oracle_test_expect(test, added, "dependencies: a file added to the directory");
    char added_path[BUSTER_ORACLE_PATH_BYTES + 16];
    snprintf(added_path, sizeof(added_path), "%s/new.h", test->runs[1]);
    unlink(added_path);
    char* embed_source[] = { "-std=c23", "embed.c", 0 };
    const bool embedded = oracle_test_clang(&cache, test->runs[0], embed_source, &taken, &status, errors, sizeof(errors)) && !taken && status == 0 &&
                          oracle_test_clang(&cache, test->runs[0], embed_source, &taken, &status, errors, sizeof(errors)) && taken;
    oracle_test_write_text(test->runs[0], "data.bin", "abd");
    const bool resource = oracle_test_clang(&cache, test->runs[0], embed_source, &taken, &status, errors, sizeof(errors)) && !taken;
    oracle_test_write_text(test->runs[0], "h.h", "#define X 3\n");
    const bool probed = oracle_test_clang(&cache, test->runs[0], embed_source, &taken, &status, errors, sizeof(errors)) && !taken;
    oracle_test_expect(test, embedded && resource && probed, "dependencies: #embed's resource and what __has_include found");
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
    // A missing #include writes no dependencies, and -M options can't be added to: neither is kept.
    oracle_test_write_text(test->runs[0], "missing.c", "#include \"missing.h\"\n");
    char* missing_source[] = { "missing.c", 0 };
    char* asked[] = { "-MD", "main.c", 0 };
    const uint64_t unkept = cache.counts->unkept;
    bool not_kept = true;
    for (uint32_t i = 0; i < 2; i += 1)
    {
        not_kept &= oracle_test_clang(&cache, test->runs[0], missing_source, &taken, &status, errors, sizeof(errors)) && !taken && status != 0;
        not_kept &= oracle_test_clang(&cache, test->runs[0], asked, &taken, &status, errors, sizeof(errors)) && !taken && status == 0;
    }
    oracle_test_expect(test, not_kept && cache.counts->unkept == unkept + 4, "dependencies: a missing #include and -M options, not kept");
    oracle_cache_close(&cache);
}

// What a run reads of its files besides their bytes: which of them are one file (a hard link, which #pragma once takes
// once), their times where __TIMESTAMP__ reads them, and whether __DATE__ and __TIME__ make it depend on when it runs.
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
                 oracle_test_write_text(directory, "dated.c", "const char* date = __DATE__;\n");
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
    const struct timespec first[2] = { { .tv_sec = 946684800 }, { .tv_sec = 946684800 } };
    const struct timespec second[2] = { { .tv_sec = 978307200 }, { .tv_sec = 978307200 } };
    char* stamp[] = { "stamped.c", 0 };
    const bool timed = utimensat(AT_FDCWD, stamped, first, 0) == 0 && oracle_test_clang(&cache, directory, stamp, &taken, &status, errors, sizeof(errors)) && !taken &&
                       oracle_test_clang(&cache, directory, stamp, &taken, &status, errors, sizeof(errors)) && taken && utimensat(AT_FDCWD, stamped, second, 0) == 0 &&
                       oracle_test_clang(&cache, directory, stamp, &taken, &status, errors, sizeof(errors)) && !taken;
    oracle_test_expect(test, timed, "identities: __TIMESTAMP__ makes its files' times part of what the run read");
    const char* saved = getenv("SOURCE_DATE_EPOCH");
    char* kept = saved != 0 ? strdup(saved) : 0;
    unsetenv("SOURCE_DATE_EPOCH");
    char* date[] = { "dated.c", 0 };
    const uint64_t unkept = cache.counts->unkept;
    bool dated = true;
    for (uint32_t i = 0; i < 2; i += 1)
    {
        dated &= oracle_test_clang(&cache, directory, date, &taken, &status, errors, sizeof(errors)) && !taken;
    }
    dated &= cache.counts->unkept == unkept + 2;
    setenv("SOURCE_DATE_EPOCH", "0", 1);
    dated &= oracle_test_clang(&cache, directory, date, &taken, &status, errors, sizeof(errors)) && !taken &&
             oracle_test_clang(&cache, directory, date, &taken, &status, errors, sizeof(errors)) && taken;
    if (kept != 0)
    {
        setenv("SOURCE_DATE_EPOCH", kept, 1);
    }
    else
    {
        unsetenv("SOURCE_DATE_EPOCH");
    }
    free(kept);
    oracle_test_expect(test, dated, "identities: __DATE__ without SOURCE_DATE_EPOCH isn't kept, and with it is");
    // A run whose directories hold other runs' files as it runs keeps its dependencies but not their names.
    char* argv[] = { "clang", "-fsyntax-only", "-fno-color-diagnostics", "links.c", 0 };
    OracleRun run = { .cache = &cache, .purpose = "oracle test: shared", .arguments = argv, .directory = directory, .dependencies = true, .shared_directories = true };
    bool shared = oracle_run(&run) && !run.taken;
    oracle_run_release(&run);
    shared &= oracle_test_write_text(directory, "beside.c", "\n");
    run = (OracleRun){ .cache = &cache, .purpose = "oracle test: shared", .arguments = argv, .directory = directory, .dependencies = true, .shared_directories = true };
    shared &= oracle_run(&run) && run.taken;
    oracle_run_release(&run);
    shared &= oracle_test_write_text(directory, "b.h", "#pragma once\nint b;\n");
    run = (OracleRun){ .cache = &cache, .purpose = "oracle test: shared", .arguments = argv, .directory = directory, .dependencies = true, .shared_directories = true };
    shared &= oracle_run(&run) && !run.taken;
    oracle_run_release(&run);
    oracle_test_expect(test, shared, "identities: shared directories, their names left out and the dependencies kept");
    // A dependency in no root (a directory whose name isn't mkdtemp's) is checked by its identity: rewritten, even with
    // its own bytes, it is another file.
    const char* temporary = getenv("TMPDIR") != 0 && getenv("TMPDIR")[0] != 0 ? getenv("TMPDIR") : "/tmp";
    char fixed[BUSTER_ORACLE_PATH_BYTES];
    char option[BUSTER_ORACLE_PATH_BYTES + 8];
    char header[BUSTER_ORACLE_PATH_BYTES + 16];
    snprintf(fixed, sizeof(fixed), "%s/buster_oracle_identity_%ld_x", temporary, (long)getpid());
    snprintf(option, sizeof(option), "-I%s", fixed);
    snprintf(header, sizeof(header), "%s/fixed.h", fixed);
    char* identified[] = { "clang", "-fsyntax-only", "-fno-color-diagnostics", option, "fixed.c", 0 };
    bool identity = (mkdir(fixed, 0700) == 0 || errno == EEXIST) && oracle_test_write_text(fixed, "fixed.h", "int fixed;\n") &&
                    oracle_test_write_text(directory, "fixed.c", "#include \"fixed.h\"\n");
    for (uint32_t i = 0; i < 5; i += 1)
    {
        // Kept, taken, rewritten with other bytes (run), with the same bytes (run), and taken.
        const bool rewrite = i == 2 || i == 3;
        identity &= !rewrite || oracle_test_write_text(fixed, "fixed.h", "int fixed2;\n");
        run = (OracleRun){ .cache = &cache, .purpose = "oracle test: identity", .arguments = identified, .directory = directory, .dependencies = true };
        identity &= oracle_run(&run) && run.status == 0 && run.taken == (i == 1 || i == 4);
        oracle_run_release(&run);
    }
    unlink(header);
    rmdir(fixed);
    oracle_test_expect(test, identity, "identities: a dependency in no root, checked by its identity, a rewrite another file");
    // A run past its time limit is killed, fails, and isn't kept.
    char* slow[] = { "sleep", "20", 0 };
    const uint64_t before = cache.counts->unkept;
    struct timespec start;
    struct timespec end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    run = (OracleRun){ .cache = &cache, .purpose = "oracle test: slow", .arguments = slow, .timeout = 1 };
    const bool failed = !oracle_run(&run) && run.killed;
    oracle_run_release(&run);
    clock_gettime(CLOCK_MONOTONIC, &end);
    const double seconds = (double)(end.tv_sec - start.tv_sec) + (double)(end.tv_nsec - start.tv_nsec) / 1e9;
    oracle_test_expect(test, failed && seconds < 10 && cache.counts->unkept == before + 1, "runs: a run past its time limit, killed and not kept");
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
    if (runs && tools)
    {
        oracle_test_sha(&test);
        oracle_test_roots(&test);
        oracle_test_runs(&test);
        oracle_test_dependencies(&test);
        oracle_test_identities(&test);
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
    static const char* const directories[] = { "runs/oracle", "dependencies/oracle", "identities/oracle", "check/oracle", "damaged/oracle", "writers/oracle", "process/one/two/buster/oracle", "" };
    for (uint32_t i = 0; made && i < sizeof(directories) / sizeof(directories[0]); i += 1)
    {
        oracle_test_remove(test.base, directories[i]);
    }
    for (uint32_t r = 0; runs && r < 2; r += 1)
    {
        oracle_test_remove(test.runs[r], "");
    }
    return runs && tools && test.failures == 0 ? 0 : 1;
}

#endif
