// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// test-oracle-cache: the oracle cache's checks (oracle_cache.c), in a directory of their own under $TMPDIR (or /tmp):
//   - SHA-256: FIPS 180-4's examples (the empty message, "abc", the 448-bit message, a million a's), and every length
//     from 0 to 300 bytes hashed whole and in random pieces, which must agree;
//   - keys: each part changes the key (the purpose, an argument's byte, where the arguments split, a variable's value,
//     a variable set to nothing against one that isn't set, an input's byte, the tool's file), and the same parts give
//     the same key;
//   - entries: what is written is read back (an empty output, one with NUL bytes, a large one, each status); a missing
//     key, an entry with a changed magic, key or length, cut short or one byte longer, isn't read; and no file is left
//     beside the entries;
//   - modes: off keeps and takes nothing; `use` takes what it kept; `check` takes nothing, and keeping a result unlike
//     its entry's is stale and writes the new result; a mode that names no mode is off;
//   - writers at once: 8 processes writing the same 64 keys, each key's entry readable whole after;
//   - the process's cache: BUSTER_ORACLE_CACHE and XDG_CACHE_HOME, a directory made private, three levels deep.
// It prints its checks and failures and exits with status 1 if any fail.

#ifndef BUSTER_ORACLE_CACHE_TEST_INCLUDED
#define BUSTER_ORACLE_CACHE_TEST_INCLUDED

#include <dirent.h>
#include <stddef.h>
#include <sys/wait.h>

typedef struct OracleTest OracleTest;
struct OracleTest
{
    uint32_t checks;
    uint32_t failures;
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
    oracle_test_expect(test, oracle_test_hex(oracle_test_sha256("abc", 3), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"), "SHA-256 of abc");
    static const char two_blocks[] = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    oracle_test_expect(test, oracle_test_hex(oracle_test_sha256(two_blocks, sizeof(two_blocks) - 1), "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"),
                       "SHA-256 of the 448-bit message");
    char* million = malloc(1000000);
    if (million != 0)
    {
        memset(million, 'a', 1000000);
        oracle_test_expect(test, oracle_test_hex(oracle_test_sha256(million, 1000000), "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"),
                           "SHA-256 of a million a's");
        free(million);
    }
    // Every length whole and in random pieces.
    uint8_t bytes[300];
    uint64_t state = 542;
    for (uint32_t i = 0; i < sizeof(bytes); i += 1)
    {
        bytes[i] = (uint8_t)oracle_test_random(&state);
    }
    uint32_t differ = 0;
    for (uint32_t length = 0; length <= sizeof(bytes); length += 1)
    {
        const OracleKey whole = oracle_test_sha256(bytes, length);
        OracleHash hash;
        oracle_sha256_begin(&hash);
        uint32_t done = 0;
        while (done < length)
        {
            const uint32_t piece = 1 + (uint32_t)(oracle_test_random(&state) % 70);
            const uint32_t taken = piece < length - done ? piece : length - done;
            oracle_hash_raw(&hash, bytes + done, taken);
            done += taken;
        }
        differ += !oracle_test_same(whole, oracle_hash_end(&hash));
    }
    oracle_test_expect(test, differ == 0, "SHA-256 of every length to 300, whole and in pieces");
}

// A key of the given parts, as a caller makes one.
static OracleKey oracle_test_key(const OracleCache* cache, const char* purpose, const char* const* arguments, char* const* environment, const char* input)
{
    OracleHash hash;
    oracle_hash_begin(&hash, cache, purpose);
    for (uint32_t i = 0; arguments[i] != 0; i += 1)
    {
        oracle_hash_text(&hash, arguments[i]);
    }
    oracle_hash_environment(&hash, environment);
    if (input != 0 && !oracle_hash_file(&hash, input))
    {
        oracle_hash_text(&hash, "(unreadable)");
    }
    return oracle_hash_end(&hash);
}

static bool oracle_test_write_file(const char* path, const char* text, size_t length)
{
    FILE* file = fopen(path, "w");
    const bool result = file != 0 && fwrite(text, 1, length, file) == length;
    if (file != 0)
    {
        fclose(file);
    }
    return result;
}

static void oracle_test_keys(OracleTest* test, const OracleCache* cache, const char* directory)
{
    char input[BUSTER_ORACLE_PATH_BYTES];
    char tool_directory[BUSTER_ORACLE_PATH_BYTES];
    snprintf(input, sizeof(input), "%s/input.c", directory);
    snprintf(tool_directory, sizeof(tool_directory), "%s/bin", directory);
    oracle_test_expect(test, oracle_test_write_file(input, "int x;\n", 7), "an input written");
    static const char* const arguments[] = { "-dM", "-E", "-x", "c", "-", 0 };
    static const char* const changed[] = { "-dM", "-E", "-x", "d", "-", 0 };
    static const char* const resplit_a[] = { "-dM", "-E", "-xc", "-", 0 };
    static const char* const resplit_b[] = { "-dM", "-E-x", "c", "-", 0 };
    char* environment[] = { "PATH=/usr/bin", "SOURCE_DATE_EPOCH=1", "HOME=/somewhere", 0 };
    char* other_value[] = { "PATH=/usr/bin", "SOURCE_DATE_EPOCH=2", "HOME=/somewhere", 0 };
    char* empty_value[] = { "PATH=/usr/bin", "SOURCE_DATE_EPOCH=", "HOME=/somewhere", 0 };
    char* unset[] = { "PATH=/usr/bin", "HOME=/somewhere", 0 };
    char* other_home[] = { "PATH=/usr/bin", "SOURCE_DATE_EPOCH=1", "HOME=/elsewhere", 0 };
    const OracleKey key = oracle_test_key(cache, "test", arguments, environment, input);
    oracle_test_expect(test, oracle_test_same(key, oracle_test_key(cache, "test", arguments, environment, input)), "the same parts give the same key");
    oracle_test_expect(test, !oracle_test_same(key, oracle_test_key(cache, "other", arguments, environment, input)), "the purpose changes the key");
    oracle_test_expect(test, !oracle_test_same(key, oracle_test_key(cache, "test", changed, environment, input)), "an argument's byte changes the key");
    oracle_test_expect(test, !oracle_test_same(oracle_test_key(cache, "test", resplit_a, environment, input), oracle_test_key(cache, "test", resplit_b, environment, input)),
                       "where the arguments split changes the key");
    oracle_test_expect(test, !oracle_test_same(key, oracle_test_key(cache, "test", arguments, other_value, input)), "a variable's value changes the key");
    oracle_test_expect(test, !oracle_test_same(oracle_test_key(cache, "test", arguments, empty_value, input), oracle_test_key(cache, "test", arguments, unset, input)),
                       "a variable set to nothing isn't one that isn't set");
    oracle_test_expect(test, oracle_test_same(key, oracle_test_key(cache, "test", arguments, other_home, input)), "a variable no compiler reads doesn't change the key");
    oracle_test_expect(test, oracle_test_write_file(input, "int y;\n", 7), "the input changed");
    oracle_test_expect(test, !oracle_test_same(key, oracle_test_key(cache, "test", arguments, environment, input)), "an input's byte changes the key");
    // The tool: a file of its own on PATH, changed.
    mkdir(tool_directory, 0700);
    char tool[BUSTER_ORACLE_PATH_BYTES + 32];
    snprintf(tool, sizeof(tool), "%s/oracle-test-tool", tool_directory);
    oracle_test_expect(test, oracle_test_write_file(tool, "#!/bin/sh\n", 10) && chmod(tool, 0700) == 0, "a tool written");
    const char* path = getenv("PATH");
    char* saved = path != 0 ? strdup(path) : 0;
    setenv("PATH", tool_directory, 1);
    OracleHash hash;
    oracle_hash_begin(&hash, cache, "tool");
    const bool found = oracle_hash_tool(&hash, "oracle-test-tool");
    const OracleKey before = oracle_hash_end(&hash);
    oracle_test_expect(test, found, "the tool found on PATH");
    oracle_test_expect(test, oracle_test_write_file(tool, "#!/bin/sh\nexit 0\n", 17), "the tool changed");
    oracle_hash_begin(&hash, cache, "tool");
    oracle_hash_tool(&hash, "oracle-test-tool");
    oracle_test_expect(test, !oracle_test_same(before, oracle_hash_end(&hash)), "the tool's file changes the key");
    oracle_hash_begin(&hash, cache, "tool");
    oracle_test_expect(test, !oracle_hash_tool(&hash, "oracle-test-missing"), "a tool that isn't on PATH");
    if (saved != 0)
    {
        setenv("PATH", saved, 1);
        free(saved);
    }
    else
    {
        unsetenv("PATH");
    }
    unlink(tool);
    rmdir(tool_directory);
    unlink(input);
}

// The files in the cache's directory, and of them those that aren't entries' names (64 hex digits).
static void oracle_test_count(const OracleCache* cache, uint32_t* files, uint32_t* others)
{
    *files = 0;
    *others = 0;
    DIR* directory = opendir(cache->directory);
    for (struct dirent* entry = directory != 0 ? readdir(directory) : 0; entry != 0; entry = readdir(directory))
    {
        if (entry->d_name[0] != '.')
        {
            *files += 1;
            bool name = strlen(entry->d_name) == 2 * BUSTER_ORACLE_KEY_BYTES;
            for (uint32_t i = 0; name && i < 2 * BUSTER_ORACLE_KEY_BYTES; i += 1)
            {
                name = (entry->d_name[i] >= '0' && entry->d_name[i] <= '9') || (entry->d_name[i] >= 'a' && entry->d_name[i] <= 'f');
            }
            *others += !name;
        }
    }
    if (directory != 0)
    {
        closedir(directory);
    }
}

static OracleKey oracle_test_numbered(const OracleCache* cache, uint32_t number)
{
    OracleHash hash;
    oracle_hash_begin(&hash, cache, "numbered");
    oracle_hash_bytes(&hash, &number, sizeof(number));
    return oracle_hash_end(&hash);
}

// Rewrites an entry's file with its byte at `offset` changed, or cut to `length` bytes, or with a byte more.
static bool oracle_test_damage(const OracleCache* cache, OracleKey key, int64_t offset, int64_t length, bool longer)
{
    char path[BUSTER_ORACLE_PATH_BYTES + 80];
    oracle_entry_path(cache, key, path, sizeof(path));
    FILE* file = fopen(path, "r+");
    bool result = file != 0;
    if (result && offset >= 0)
    {
        result = fseek(file, offset, SEEK_SET) == 0;
        const int byte = result ? fgetc(file) : EOF;
        result = byte != EOF && fseek(file, offset, SEEK_SET) == 0 && fputc(byte ^ 0x5a, file) != EOF;
    }
    if (result && longer)
    {
        result = fseek(file, 0, SEEK_END) == 0 && fputc(0, file) != EOF;
    }
    if (file != 0)
    {
        fclose(file);
    }
    if (result && length >= 0)
    {
        result = truncate(path, length) == 0;
    }
    return result;
}

static void oracle_test_entries(OracleTest* test, OracleCache* cache)
{
    static const char with_nul[] = { 'a', 0, 'b', 0, 0, 'c' };
    const size_t large_length = 3u << 20;
    char* large = malloc(large_length);
    uint64_t state = 7;
    for (size_t i = 0; large != 0 && i < large_length; i += 1)
    {
        large[i] = (char)oracle_test_random(&state);
    }
    static const int statuses[] = { 0, 1, 2, 128, 255 };
    uint32_t round_trips = 0;
    for (uint32_t s = 0; s < sizeof(statuses) / sizeof(statuses[0]); s += 1)
    {
        for (uint32_t kind = 0; kind < 4; kind += 1)
        {
            const char* output = kind == 0 ? "" : kind == 1 ? "#define X 1\n" : kind == 2 ? with_nul : large;
            const size_t length = kind == 0 ? 0 : kind == 1 ? 12 : kind == 2 ? sizeof(with_nul) : large_length;
            if (output != 0)
            {
                const OracleKey key = oracle_test_numbered(cache, 100 * s + kind);
                int status = -1;
                char* read = 0;
                size_t read_length = 0;
                const bool written = oracle_cache_write(cache, key, statuses[s], output, length);
                const bool same = written && oracle_cache_read(cache, key, &status, &read, &read_length) && status == statuses[s] && read_length == length &&
                                  memcmp(read, output, length) == 0 && read[length] == 0;
                round_trips += same;
                free(read);
            }
        }
    }
    oracle_test_expect(test, round_trips == 5 * (large != 0 ? 4 : 3), "entries read back as written");
    int status = 0;
    char* read = 0;
    size_t length = 0;
    oracle_test_expect(test, !oracle_cache_read(cache, oracle_test_numbered(cache, 999999), &status, &read, &length), "a missing key isn't read");
    // Damaged entries: the magic, the key, the length's field, cut short, a byte longer.
    const OracleKey damaged = oracle_test_numbered(cache, 1);
    static const struct
    {
        int64_t offset;
        int64_t length;
        bool longer;
        const char* what;
    } damages[] = {
        { 0, -1, false, "an entry with another magic isn't read" },
        { 8, -1, false, "an entry with another key isn't read" },
        { (int64_t)offsetof(OracleEntryHeader, length), -1, false, "an entry with another length isn't read" },
        { -1, (int64_t)sizeof(OracleEntryHeader) + 5, false, "an entry cut short isn't read" },
        { -1, (int64_t)sizeof(OracleEntryHeader) - 1, false, "an entry cut inside its header isn't read" },
        { -1, -1, true, "an entry a byte longer isn't read" },
    };
    for (uint32_t d = 0; d < sizeof(damages) / sizeof(damages[0]); d += 1)
    {
        const bool written = oracle_cache_write(cache, damaged, 0, "#define X 1\n", 12);
        const bool changed = written && oracle_test_damage(cache, damaged, damages[d].offset, damages[d].length, damages[d].longer);
        read = 0;
        oracle_test_expect(test, changed && !oracle_cache_read(cache, damaged, &status, &read, &length), damages[d].what);
        free(read);
    }
    uint32_t files;
    uint32_t others;
    oracle_test_count(cache, &files, &others);
    oracle_test_expect(test, files != 0 && others == 0, "no file left beside the entries");
    free(large);
}

static void oracle_test_modes(OracleTest* test, const char* base)
{
    OracleCache cache;
    int status = 0;
    char* output = 0;
    size_t length = 0;
    // Off: nothing taken, kept or made.
    char off_base[BUSTER_ORACLE_PATH_BYTES];
    snprintf(off_base, sizeof(off_base), "%s/off", base);
    oracle_cache_open(&cache, 0, off_base);
    struct stat status_of;
    oracle_test_expect(test, !oracle_cache_active(&cache) && stat(off_base, &status_of) != 0, "unset is off and makes no directory");
    const OracleKey key = oracle_test_numbered(&cache, 5);
    oracle_test_expect(test, oracle_cache_keep(&cache, key, 0, "x", 1) && !oracle_cache_take(&cache, key, &status, &output, &length), "off keeps and takes nothing");
    oracle_cache_open(&cache, "uses", off_base);
    oracle_test_expect(test, !oracle_cache_active(&cache), "a mode that names no mode is off");
    oracle_cache_open(&cache, "use", 0);
    oracle_test_expect(test, !oracle_cache_active(&cache), "a cache without a directory is off");
    // Use: a miss, then what was kept.
    char modes_base[BUSTER_ORACLE_PATH_BYTES];
    snprintf(modes_base, sizeof(modes_base), "%s/modes", base);
    oracle_cache_open(&cache, "use", modes_base);
    const OracleKey used = oracle_test_numbered(&cache, 6);
    oracle_test_expect(test, oracle_cache_active(&cache) && !oracle_cache_take(&cache, used, &status, &output, &length), "use misses an entry that isn't there");
    oracle_test_expect(test, oracle_cache_keep(&cache, used, 3, "result", 6), "use keeps a result");
    output = 0;
    oracle_test_expect(test, oracle_cache_take(&cache, used, &status, &output, &length) && status == 3 && length == 6 && memcmp(output, "result", 6) == 0,
                       "use takes what it kept");
    free(output);
    oracle_test_expect(test, cache.hits == 1 && cache.runs == 1 && cache.stale == 0 && cache.unwritten == 0, "use's counts");
    // Check: takes nothing; the same result is current, another is stale and written.
    oracle_cache_open(&cache, "check", modes_base);
    output = 0;
    oracle_test_expect(test, !oracle_cache_take(&cache, used, &status, &output, &length), "check takes nothing");
    oracle_test_expect(test, oracle_cache_keep(&cache, used, 3, "result", 6), "check finds the same result current");
    oracle_test_expect(test, !oracle_cache_keep(&cache, used, 3, "other!", 6), "check finds another output stale");
    oracle_test_expect(test, !oracle_cache_keep(&cache, used, 4, "other!", 6), "check finds another status stale");
    oracle_test_expect(test, oracle_cache_keep(&cache, used, 4, "other!", 6), "check wrote the run's result");
    oracle_test_expect(test, oracle_cache_keep(&cache, oracle_test_numbered(&cache, 7), 0, "", 0), "check of a key without an entry");
    oracle_test_expect(test, cache.runs == 5 && cache.stale == 2 && cache.hits == 0, "check's counts");
}

// 8 processes writing the same 64 keys at once: each key's entry must be whole after.
static void oracle_test_writers(OracleTest* test, const char* base)
{
    char writers_base[BUSTER_ORACLE_PATH_BYTES];
    snprintf(writers_base, sizeof(writers_base), "%s/writers", base);
    OracleCache cache;
    oracle_cache_open(&cache, "use", writers_base);
    static char output[1 << 14];
    for (uint32_t i = 0; i < sizeof(output); i += 1)
    {
        output[i] = (char)('a' + i % 26);
    }
    fflush(stdout);
    pid_t children[8];
    for (uint32_t c = 0; c < 8; c += 1)
    {
        children[c] = fork();
        if (children[c] == 0)
        {
            bool written = true;
            for (uint32_t round = 0; round < 4; round += 1)
            {
                for (uint32_t k = 0; k < 64; k += 1)
                {
                    written &= oracle_cache_write(&cache, oracle_test_numbered(&cache, 1000 + k), (int)k, output, sizeof(output) - k);
                }
            }
            _exit(written ? 0 : 1);
        }
    }
    uint32_t succeeded = 0;
    for (uint32_t c = 0; c < 8; c += 1)
    {
        int status = 0;
        succeeded += children[c] > 0 && waitpid(children[c], &status, 0) == children[c] && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    }
    oracle_test_expect(test, succeeded == 8, "8 writers at once each wrote every entry");
    uint32_t whole = 0;
    for (uint32_t k = 0; k < 64; k += 1)
    {
        int status = -1;
        char* read = 0;
        size_t length = 0;
        whole += oracle_cache_read(&cache, oracle_test_numbered(&cache, 1000 + k), &status, &read, &length) && status == (int)k && length == sizeof(output) - k &&
                 memcmp(read, output, length) == 0;
        free(read);
    }
    oracle_test_expect(test, whole == 64, "every entry whole after the writers");
    uint32_t files;
    uint32_t others;
    oracle_test_count(&cache, &files, &others);
    oracle_test_expect(test, files == 64 && others == 0, "the writers left their entries alone");
}

// The process's cache: from BUSTER_ORACLE_CACHE and XDG_CACHE_HOME, its directory made private three levels deep.
static void oracle_test_process(OracleTest* test, const char* base)
{
    char cache_home[BUSTER_ORACLE_PATH_BYTES];
    snprintf(cache_home, sizeof(cache_home), "%s/process/one/two", base);
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
        _exit(right ? 0 : 1);
    }
    int status = 0;
    oracle_test_expect(test, child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0,
                       "the process's cache from BUSTER_ORACLE_CACHE and XDG_CACHE_HOME, made private");
}

// Removes the files of `base`/`relative` and then each directory of `relative`, deepest first (the tests' own tree,
// whose directories hold files alone).
static void oracle_test_remove(const char* base, const char* relative)
{
    char path[BUSTER_ORACLE_PATH_BYTES];
    snprintf(path, sizeof(path), "%s/%s", base, relative);
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
    for (char* end = path + strlen(path); end > path + strlen(base); end -= 1)
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
    char base[BUSTER_ORACLE_PATH_BYTES];
    const char* temporary = getenv("TMPDIR");
    snprintf(base, sizeof(base), "%s/buster-oracle-test-XXXXXX", temporary != 0 ? temporary : "/tmp");
    const bool made = argc == 0 && mkdtemp(base) != 0;
    if (made)
    {
        oracle_test_sha(&test);
        OracleCache cache;
        char entries_base[BUSTER_ORACLE_PATH_BYTES];
        snprintf(entries_base, sizeof(entries_base), "%s/entries", base);
        oracle_cache_open(&cache, "use", entries_base);
        oracle_test_expect(&test, oracle_cache_active(&cache), "a cache in a new directory");
        oracle_test_keys(&test, &cache, base);
        oracle_test_entries(&test, &cache);
        oracle_test_modes(&test, base);
        oracle_test_writers(&test, base);
        oracle_test_process(&test, base);
        static const char* const directories[] = { "entries/oracle", "modes/oracle", "writers/oracle", "process/one/two/buster/oracle" };
        for (uint32_t i = 0; i < sizeof(directories) / sizeof(directories[0]); i += 1)
        {
            oracle_test_remove(base, directories[i]);
        }
        rmdir(base);
        printf("oracle cache: %u checks, %u failures\n", test.checks, test.failures);
    }
    else
    {
        fprintf(stderr, "usage: test-oracle-cache\n");
    }
    return made && test.failures == 0 ? 0 : 1;
}

#endif
