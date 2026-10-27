// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The oracle cache (#542): the results of the clang and gcc runs that the tests compare with, kept between runs in the
// cache directory (snapshot_directory's: $XDG_CACHE_HOME/buster, or $HOME/.cache/buster) under oracle/, an entry a file
// named by its key. BUSTER_ORACLE_CACHE picks the mode:
//   - `use`: a run whose entry is there, with its inputs as the entry has them, takes the entry's result rather than
//     run the tool, and a run that runs writes its entry;
//   - `check`: every run runs, and where its entry is there with the same inputs, the entry must hold the run's result:
//     one that differs is stale, which fails the run (the 9700X's daily run). Every result is written;
//   - anything else, or unset: every run runs and nothing is kept (local runs).
//
// A test runs a tool through the cache as an OracleRun (oracle_run_start and oracle_run_finish, or oracle_run): the
// tool, its arguments, environment, standard input and working directory, and the files it writes. Its result is its
// wait status, its standard output and errors, and those files' bytes. Its key is SHA-256 (FIPS 180-4) of, each part
// after its length:
//   - its purpose, the caller's name for its kind of run;
//   - the tool: the first file of its name on PATH that may be run, as posix_spawnp finds it, and what stat gives of
//     the file it resolves to (device, inode, size, modification and change times, as the runtime snapshot's probes
//     have them); the same of the package database (pacman's, dpkg's or rpm's), which an update of any package
//     changes, so that a new clang, its libraries or its headers make new keys; and the CPU's model and flags, which
//     -march=native reads;
//   - its arguments, its working directory, the names of the files it writes and the environment variables that change
//     a compiler's result (oracle_environment_names) with their values, each with its roots made alike (below);
//   - the bytes its standard input reads.
// A compiler's run of one source (OracleRun.dependencies) runs with `-MD -MF FILE` too, which its key doesn't have, and
// its entry holds what the run read: each file the compiler's dependency file names (its source, the headers, #embed's
// resources and what __has_include found, which clang 23 lists) with SHA-256 of its bytes, and the names in each
// directory under a root that its arguments name, as they were when it started, so that a file added where the run
// looked for one and found none makes it run again. An entry is taken only if each of those files and directories is
// as it was. A run whose dependencies can't be read (a compiler writes none when an #include fails) isn't kept.
//
// A test makes its files in directories of mkdtemp's under $TMPDIR (or /tmp), whose names differ from one run to the
// next. A root is such a name in a run's strings: $TMPDIR/NAME, NAME a run of letters, digits, '.', '_' and '-' whose
// last 6 characters, or the 6 before its last '.', are letters and digits, as mkdtemp's and mkstemps's random ones are.
// In a key, each root's 6 characters are a placeholder for its place among the run's roots, by first appearance. An
// entry holds the roots it was made with, and when it is taken each is replaced, in the result and in the names of the
// files it lists, by the run's root of the same place, which is as long. So a result that names the run's files names
// the files of the run that takes it, byte for byte, in an object as in text.
//
// An entry is written into a file of its own beside the entries and renamed over its name, so that a reader sees a
// whole entry or none, whatever tests write at once. The counts of what runs did are in shared memory, so that a
// test's parts in the child processes it forks after the cache is set up add to them.
//
// Test tooling outside the compile path: it hashes the files a run read and has no vector path.
#ifndef BUSTER_ORACLE_CACHE_INCLUDED
#define BUSTER_ORACLE_CACHE_INCLUDED

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>

extern char** environ;

#define BUSTER_ORACLE_KEY_BYTES 32
#define BUSTER_ORACLE_PATH_BYTES 4096
// The most roots a run may have: a run with more runs without the cache.
#define BUSTER_ORACLE_ROOTS 8
// A root's random characters, as many as mkdtemp makes.
#define BUSTER_ORACLE_RANDOM 6
// The most files a run may write.
#define BUSTER_ORACLE_FILES 8

// snapshot.c's: the cache directory.
static bool snapshot_directory(char* path, uint64_t capacity);

typedef enum OracleMode
{
    ORACLE_MODE_OFF,
    ORACLE_MODE_USE,
    ORACLE_MODE_CHECK,
} OracleMode;

// What runs through a cache did: took an entry, ran, found their entry stale, and ran without being kept (no
// dependencies, a file that couldn't be read, more roots than BUSTER_ORACLE_ROOTS, an entry that couldn't be written).
typedef struct OracleCounts OracleCounts;
struct OracleCounts
{
    uint64_t taken;
    uint64_t run;
    uint64_t stale;
    uint64_t unkept;
};

// A cache: its mode, its directory, the host's part of every key, and its counts (in shared memory if it could be
// mapped, else `own`).
typedef struct OracleCache OracleCache;
struct OracleCache
{
    bool initialized;
    OracleMode mode;
    char directory[BUSTER_ORACLE_PATH_BYTES];
    // SHA-256 of the package database's identity and the CPU's model and flags.
    uint8_t host[BUSTER_ORACLE_KEY_BYTES];
    OracleCounts* counts;
    OracleCounts own;
};

static OracleCache oracle_cache_state;

typedef struct OracleHash OracleHash;
struct OracleHash
{
    uint32_t state[8];
    uint8_t block[64];
    uint64_t length;
};

typedef struct OracleKey OracleKey;
struct OracleKey
{
    uint8_t bytes[BUSTER_ORACLE_KEY_BYTES];
};

// --- SHA-256 ---

static const uint32_t oracle_sha256_constants[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static uint32_t oracle_rotate(uint32_t value, uint32_t bits)
{
    return (value >> bits) | (value << (32 - bits));
}

static void oracle_sha256_block(uint32_t* state, const uint8_t* block)
{
    uint32_t words[64];
    for (uint32_t i = 0; i < 16; i += 1)
    {
        words[i] = (uint32_t)block[4 * i] << 24 | (uint32_t)block[4 * i + 1] << 16 | (uint32_t)block[4 * i + 2] << 8 | (uint32_t)block[4 * i + 3];
    }
    for (uint32_t i = 16; i < 64; i += 1)
    {
        const uint32_t s0 = oracle_rotate(words[i - 15], 7) ^ oracle_rotate(words[i - 15], 18) ^ (words[i - 15] >> 3);
        const uint32_t s1 = oracle_rotate(words[i - 2], 17) ^ oracle_rotate(words[i - 2], 19) ^ (words[i - 2] >> 10);
        words[i] = words[i - 16] + s0 + words[i - 7] + s1;
    }
    uint32_t a = state[0];
    uint32_t b = state[1];
    uint32_t c = state[2];
    uint32_t d = state[3];
    uint32_t e = state[4];
    uint32_t f = state[5];
    uint32_t g = state[6];
    uint32_t h = state[7];
    for (uint32_t i = 0; i < 64; i += 1)
    {
        const uint32_t s1 = oracle_rotate(e, 6) ^ oracle_rotate(e, 11) ^ oracle_rotate(e, 25);
        const uint32_t choice = (e & f) ^ (~e & g);
        const uint32_t first = h + s1 + choice + oracle_sha256_constants[i] + words[i];
        const uint32_t s0 = oracle_rotate(a, 2) ^ oracle_rotate(a, 13) ^ oracle_rotate(a, 22);
        const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t second = s0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + first;
        d = c;
        c = b;
        b = a;
        a = first + second;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

static void oracle_sha256_begin(OracleHash* hash)
{
    *hash = (OracleHash){
        .state = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 },
    };
}

// The bytes, whole blocks straight from them and the rest through the hash's block.
static void oracle_hash_raw(OracleHash* hash, const void* bytes, size_t length)
{
    const uint8_t* from = bytes;
    size_t i = 0;
    while (i < length)
    {
        if (hash->length % 64 == 0 && length - i >= 64)
        {
            oracle_sha256_block(hash->state, from + i);
            hash->length += 64;
            i += 64;
        }
        else
        {
            hash->block[hash->length % 64] = from[i];
            hash->length += 1;
            i += 1;
            if (hash->length % 64 == 0)
            {
                oracle_sha256_block(hash->state, hash->block);
            }
        }
    }
}

static OracleKey oracle_hash_end(OracleHash* hash)
{
    const uint64_t bits = hash->length * 8;
    const uint8_t one = 0x80;
    const uint8_t zero = 0;
    oracle_hash_raw(hash, &one, 1);
    while (hash->length % 64 != 56)
    {
        oracle_hash_raw(hash, &zero, 1);
    }
    uint8_t length[8];
    for (uint32_t i = 0; i < 8; i += 1)
    {
        length[i] = (uint8_t)(bits >> (56 - 8 * i));
    }
    oracle_hash_raw(hash, length, sizeof(length));
    OracleKey key;
    for (uint32_t i = 0; i < 8; i += 1)
    {
        key.bytes[4 * i] = (uint8_t)(hash->state[i] >> 24);
        key.bytes[4 * i + 1] = (uint8_t)(hash->state[i] >> 16);
        key.bytes[4 * i + 2] = (uint8_t)(hash->state[i] >> 8);
        key.bytes[4 * i + 3] = (uint8_t)hash->state[i];
    }
    return key;
}

// --- Keys ---

// A part's length, the 8 bytes that come before its bytes.
static void oracle_hash_length(OracleHash* hash, uint64_t length)
{
    uint8_t prefix[8];
    for (uint32_t i = 0; i < 8; i += 1)
    {
        prefix[i] = (uint8_t)(length >> (8 * i));
    }
    oracle_hash_raw(hash, prefix, sizeof(prefix));
}

// A part of the key: its length, then its bytes.
static void oracle_hash_bytes(OracleHash* hash, const void* bytes, size_t length)
{
    oracle_hash_length(hash, length);
    oracle_hash_raw(hash, bytes, length);
}

static void oracle_hash_text(OracleHash* hash, const char* text)
{
    oracle_hash_bytes(hash, text, strlen(text));
}

// What stat gives of a path, as a part (and a part of zeros for a path that isn't there).
static void oracle_hash_identity(OracleHash* hash, const char* path)
{
    struct stat status;
    int64_t fields[8] = { 0 };
    if (stat(path, &status) == 0)
    {
        fields[0] = 1;
        fields[1] = (int64_t)status.st_dev;
        fields[2] = (int64_t)status.st_ino;
        fields[3] = (int64_t)status.st_size;
        fields[4] = (int64_t)status.st_mtim.tv_sec;
        fields[5] = (int64_t)status.st_mtim.tv_nsec;
        fields[6] = (int64_t)status.st_ctim.tv_sec;
        fields[7] = (int64_t)status.st_ctim.tv_nsec;
    }
    oracle_hash_text(hash, path);
    oracle_hash_bytes(hash, fields, sizeof(fields));
}

// Whether `needle` is in the `length` bytes at `bytes`.
static bool oracle_contains(const uint8_t* bytes, uint64_t length, const char* needle)
{
    const uint64_t size = strlen(needle);
    bool result = false;
    for (uint64_t at = 0; !result && size <= length && at <= length - size; at += 1)
    {
        result = bytes[at] == (uint8_t)needle[0] && memcmp(bytes + at, needle, size) == 0;
    }
    return result;
}

// The first file named `name` on PATH that may be run, into `path`, as posix_spawnp looks it up: false if there is none.
static bool oracle_find_tool(const char* name, char* path, size_t capacity)
{
    const char* search = getenv("PATH");
    bool found = false;
    if (strchr(name, '/') != 0)
    {
        const int length = snprintf(path, capacity, "%s", name);
        found = length > 0 && (size_t)length < capacity && access(path, X_OK) == 0;
    }
    while (!found && strchr(name, '/') == 0 && search != 0 && *search != 0)
    {
        const char* end = strchr(search, ':');
        const size_t length = end != 0 ? (size_t)(end - search) : strlen(search);
        const int written = snprintf(path, capacity, "%.*s/%s", (int)length, length != 0 ? search : ".", name);
        found = written > 0 && (size_t)written < capacity && access(path, X_OK) == 0;
        search = end != 0 ? end + 1 : 0;
    }
    return found;
}

// The tool, as a part: its path on PATH, the file it resolves to and what stat gives of it. False if it isn't on PATH.
static bool oracle_hash_tool(OracleHash* hash, const char* name)
{
    char path[BUSTER_ORACLE_PATH_BYTES];
    char resolved[BUSTER_ORACLE_PATH_BYTES];
    const bool result = oracle_find_tool(name, path, sizeof(path)) && realpath(path, resolved) != 0;
    if (result)
    {
        oracle_hash_text(hash, name);
        oracle_hash_text(hash, path);
        oracle_hash_identity(hash, resolved);
    }
    return result;
}

// The environment variables that change a compiler's result: clang's and gcc's search paths, options and
// configuration, and what __DATE__, __TIME__ and the locale give.
static const char* const oracle_environment_names[] = {
    "CCC_OVERRIDE_OPTIONS", "CLANG_NO_DEFAULT_CONFIG", "CPATH", "C_INCLUDE_PATH", "CPLUS_INCLUDE_PATH",
    "OBJC_INCLUDE_PATH", "OBJCPLUS_INCLUDE_PATH", "COMPILER_PATH", "LIBRARY_PATH", "GCC_EXEC_PREFIX",
    "DEPENDENCIES_OUTPUT", "SUNPRO_DEPENDENCIES", "SOURCE_DATE_EPOCH", "SDKROOT", "TZ", "LANG", "LC_ALL",
    "LC_CTYPE", "LC_MESSAGES", "LD_LIBRARY_PATH", "LD_PRELOAD", "PATH",
};

// The value of the variable `name` in `environment`, or null if it isn't set.
static const char* oracle_variable(char* const* environment, const char* name)
{
    const size_t length = strlen(name);
    const char* value = 0;
    for (uint32_t i = 0; environment != 0 && environment[i] != 0 && value == 0; i += 1)
    {
        value = strncmp(environment[i], name, length) == 0 && environment[i][length] == '=' ? environment[i] + length + 1 : 0;
    }
    return value;
}

// The value of oracle_environment_names[n] in `environment`, or null if it isn't set.
static const char* oracle_environment_value(char* const* environment, uint32_t n)
{
    return oracle_variable(environment, oracle_environment_names[n]);
}

// --- Roots ---

// A root in one of a run's strings or in an entry: where its name is, its length, and where its random characters
// start in it.
typedef struct OracleRoot OracleRoot;
struct OracleRoot
{
    const char* name;
    uint32_t length;
    uint32_t random;
};

// A run's roots, by first appearance, and the temporary directory they are in ($TMPDIR, or /tmp, without trailing
// slashes; none if that is /).
typedef struct OracleRoots OracleRoots;
struct OracleRoots
{
    OracleRoot roots[BUSTER_ORACLE_ROOTS];
    uint32_t count;
    bool overflow;
    uint32_t base_length;
    char base[BUSTER_ORACLE_PATH_BYTES];
};

// The characters of a root's name: letters, digits, '.', '_' and '-'.
static bool oracle_name_character(uint8_t c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
}

// Whether the BUSTER_ORACLE_RANDOM characters at `text` are letters and digits, as mkdtemp's random ones are.
static bool oracle_random_characters(const char* text)
{
    bool result = true;
    for (uint32_t i = 0; i < BUSTER_ORACLE_RANDOM; i += 1)
    {
        const uint8_t c = (uint8_t)text[i];
        result = result && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'));
    }
    return result;
}

static void oracle_roots_begin(OracleRoots* roots)
{
    const char* base = getenv("TMPDIR");
    base = base != 0 && base[0] != 0 ? base : "/tmp";
    size_t length = strlen(base);
    while (length > 1 && base[length - 1] == '/')
    {
        length -= 1;
    }
    roots->count = 0;
    roots->overflow = false;
    roots->base_length = length < sizeof(roots->base) && !(length == 1 && base[0] == '/') ? (uint32_t)length : 0;
    memcpy(roots->base, base, roots->base_length);
    roots->base[roots->base_length] = 0;
}

// The root that starts at text[at], if one does: its length (0 if none) and where its random characters start.
static uint32_t oracle_root_at(const OracleRoots* roots, const char* text, size_t length, size_t at, uint32_t* random)
{
    uint32_t result = 0;
    const size_t base = roots->base_length;
    if (base != 0 && length - at > base + 1 && memcmp(text + at, roots->base, base) == 0 && text[at + base] == '/')
    {
        const size_t start = at + base + 1;
        size_t end = start;
        while (end < length && oracle_name_character((uint8_t)text[end]))
        {
            end += 1;
        }
        size_t dot = end;
        for (size_t i = start; i < end; i += 1)
        {
            dot = text[i] == '.' ? i : dot;
        }
        const size_t stop = end - start >= BUSTER_ORACLE_RANDOM && oracle_random_characters(text + end - BUSTER_ORACLE_RANDOM) ? end
                            : dot < end && dot - start >= BUSTER_ORACLE_RANDOM && oracle_random_characters(text + dot - BUSTER_ORACLE_RANDOM) ? dot
                                                                                                                                        : 0;
        if (stop != 0 && end - at <= UINT32_MAX)
        {
            result = (uint32_t)(end - at);
            *random = (uint32_t)(stop - BUSTER_ORACLE_RANDOM - at);
        }
    }
    return result;
}

// Adds the roots in `text` that `roots` doesn't have yet, in order.
static void oracle_roots_find(OracleRoots* roots, const char* text)
{
    const size_t length = strlen(text);
    size_t at = 0;
    while (at < length)
    {
        uint32_t random = 0;
        const uint32_t root = oracle_root_at(roots, text, length, at, &random);
        bool known = root == 0;
        for (uint32_t r = 0; r < roots->count && !known; r += 1)
        {
            known = roots->roots[r].length == root && memcmp(roots->roots[r].name, text + at, root) == 0;
        }
        if (!known && roots->count < BUSTER_ORACLE_ROOTS)
        {
            roots->roots[roots->count] = (OracleRoot){ .name = text + at, .length = root, .random = random };
            roots->count += 1;
        }
        else
        {
            roots->overflow |= !known;
        }
        at += root != 0 ? root : 1;
    }
}

// The root of `roots` whose name is at bytes[at] and is followed by a character no name has or by the end: its place,
// or `count` if none is. (So of two roots one of which starts the other's name, only the whole name matches.)
static uint32_t oracle_root_in(const OracleRoot* roots, uint32_t count, const uint8_t* bytes, uint64_t length, uint64_t at)
{
    uint32_t result = count;
    for (uint32_t r = 0; r < count && result == count; r += 1)
    {
        const uint64_t root = roots[r].length;
        const bool whole = length - at >= root && bytes[at] == (uint8_t)roots[r].name[0] && memcmp(bytes + at, roots[r].name, root) == 0 &&
                           (length - at == root || !oracle_name_character(bytes[at + root]));
        result = whole ? r : result;
    }
    return result;
}

// `text` as a part of the key, each of the run's roots in it with its random characters as a placeholder for its
// place: \x01, the place and four \x01, as many as the characters they stand for.
static void oracle_hash_rooted(OracleHash* hash, const OracleRoots* roots, const char* text)
{
    const size_t length = strlen(text);
    oracle_hash_length(hash, length);
    size_t at = 0;
    while (at < length)
    {
        const uint32_t r = oracle_root_in(roots->roots, roots->count, (const uint8_t*)text, length, at);
        if (r < roots->count)
        {
            const OracleRoot* root = &roots->roots[r];
            const uint8_t placeholder[BUSTER_ORACLE_RANDOM] = { 1, (uint8_t)r, 1, 1, 1, 1 };
            oracle_hash_raw(hash, text + at, root->random);
            oracle_hash_raw(hash, placeholder, sizeof(placeholder));
            oracle_hash_raw(hash, text + at + root->random + BUSTER_ORACLE_RANDOM, root->length - root->random - BUSTER_ORACLE_RANDOM);
            at += root->length;
        }
        else
        {
            oracle_hash_raw(hash, text + at, 1);
            at += 1;
        }
    }
}

// Replaces each of `old`'s roots in `bytes` with `new`'s root of the same place, as long, in one pass.
static void oracle_roots_replace(const OracleRoot* old, const OracleRoot* new, uint32_t count, uint8_t* bytes, uint64_t length)
{
    uint64_t at = 0;
    while (at < length)
    {
        const uint32_t r = oracle_root_in(old, count, bytes, length, at);
        if (r < count)
        {
            memcpy(bytes + at, new[r].name, new[r].length);
            at += new[r].length;
        }
        else
        {
            at += 1;
        }
    }
}

// The variables of oracle_environment_names in `environment`, as parts: each name, then whether it is set and its
// value, with the run's roots made alike.
static void oracle_hash_environment(OracleHash* hash, const OracleRoots* roots, char* const* environment)
{
    for (uint32_t n = 0; n < sizeof(oracle_environment_names) / sizeof(oracle_environment_names[0]); n += 1)
    {
        const char* value = oracle_environment_value(environment, n);
        oracle_hash_text(hash, oracle_environment_names[n]);
        oracle_hash_bytes(hash, value != 0 ? "=" : "", value != 0 ? 1 : 0);
        oracle_hash_rooted(hash, roots, value != 0 ? value : "");
    }
}

// --- The cache ---

// The host's part of every key: the package database's identity and /proc/cpuinfo's first model name and flags lines.
static void oracle_host(uint8_t* host)
{
    OracleHash hash;
    oracle_sha256_begin(&hash);
    oracle_hash_text(&hash, "buster oracle host 1");
    static const char* const databases[] = { "/var/lib/pacman/local", "/var/lib/dpkg/status", "/var/lib/rpm" };
    for (uint32_t i = 0; i < sizeof(databases) / sizeof(databases[0]); i += 1)
    {
        oracle_hash_identity(&hash, databases[i]);
    }
    FILE* cpuinfo = fopen("/proc/cpuinfo", "r");
    char line[16384];
    bool model = false;
    bool flags = false;
    while (cpuinfo != 0 && !(model && flags) && fgets(line, sizeof(line), cpuinfo) != 0)
    {
        const bool is_model = !model && strncmp(line, "model name", 10) == 0;
        const bool is_flags = !flags && strncmp(line, "flags", 5) == 0;
        if (is_model || is_flags)
        {
            oracle_hash_text(&hash, line);
        }
        model |= is_model;
        flags |= is_flags;
    }
    if (cpuinfo != 0)
    {
        fclose(cpuinfo);
    }
    const OracleKey key = oracle_hash_end(&hash);
    memcpy(host, key.bytes, sizeof(key.bytes));
}

// Makes each directory of `path` that isn't there, private, as the snapshot's: false if one can't be made.
static bool oracle_make_directories(const char* path)
{
    char prefix[BUSTER_ORACLE_PATH_BYTES];
    const size_t length = strlen(path);
    bool result = length != 0 && length < sizeof(prefix);
    for (size_t end = 1; result && end <= length; end += 1)
    {
        if (end == length || path[end] == '/')
        {
            memcpy(prefix, path, end);
            prefix[end] = 0;
            struct stat status;
            result = (mkdir(prefix, 0700) == 0 || errno == EEXIST) && stat(prefix, &status) == 0 && S_ISDIR(status.st_mode);
        }
    }
    return result;
}

// A cache in mode `mode` (BUSTER_ORACLE_CACHE's values; null is off) in `base`/oracle, made as needed: off if `base` is
// null or the directory can't be made. Its counts are in shared memory where they can be mapped.
static void oracle_cache_open(OracleCache* cache, const char* mode, const char* base)
{
    *cache = (OracleCache){
        .initialized = true,
        .mode = mode == 0 ? ORACLE_MODE_OFF : strcmp(mode, "use") == 0 ? ORACLE_MODE_USE : strcmp(mode, "check") == 0 ? ORACLE_MODE_CHECK : ORACLE_MODE_OFF,
    };
    const int length = cache->mode != ORACLE_MODE_OFF && base != 0 ? snprintf(cache->directory, sizeof(cache->directory), "%s/oracle", base) : -1;
    const bool made = length > 0 && (size_t)length < sizeof(cache->directory) && oracle_make_directories(cache->directory);
    if (!made)
    {
        cache->mode = ORACLE_MODE_OFF;
        cache->directory[0] = 0;
    }
    if (cache->mode != ORACLE_MODE_OFF)
    {
        oracle_host(cache->host);
    }
    void* shared = mmap(0, sizeof(OracleCounts), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    cache->counts = shared != MAP_FAILED ? shared : &cache->own;
}

// Unmaps a cache's shared counts (a cache oracle_cache_open set up, other than the process's).
static void oracle_cache_close(OracleCache* cache)
{
    if (cache->counts != &cache->own && cache->counts != 0)
    {
        munmap(cache->counts, sizeof(OracleCounts));
    }
    cache->counts = &cache->own;
}

// The process's cache, set up on its first use: BUSTER_ORACLE_CACHE's mode, in the cache directory. A test whose parts
// run in child processes sets it up before it forks them, so that their counts reach its report.
static OracleCache* oracle_cache(void)
{
    OracleCache* cache = &oracle_cache_state;
    if (!cache->initialized)
    {
        char base[BUSTER_ORACLE_PATH_BYTES];
        const char* mode = getenv("BUSTER_ORACLE_CACHE");
        oracle_cache_open(cache, mode, mode != 0 && snapshot_directory(base, sizeof(base)) ? base : 0);
    }
    return cache;
}

// Turns the process's cache off, as BUSTER_ORACLE_CACHE unset does: for a test of a harness, whose processes must run.
static void oracle_cache_disable(void)
{
    oracle_cache_close(&oracle_cache_state);
    oracle_cache_open(&oracle_cache_state, 0, 0);
}

// A key's start: the format, the purpose and the cache's host part.
static void oracle_hash_begin(OracleHash* hash, const OracleCache* cache, const char* purpose)
{
    oracle_sha256_begin(hash);
    oracle_hash_text(hash, "buster oracle 2");
    oracle_hash_text(hash, purpose);
    oracle_hash_bytes(hash, cache->host, BUSTER_ORACLE_KEY_BYTES);
}

// Whether the cache keeps runs.
static bool oracle_cache_active(const OracleCache* cache)
{
    return cache->mode != ORACLE_MODE_OFF;
}

static void oracle_count(uint64_t* count)
{
    __atomic_fetch_add(count, 1, __ATOMIC_RELAXED);
}

// A line of what the cache did, if it is on: runs that took an entry, ran, found theirs stale, and weren't kept.
static void oracle_cache_report(const OracleCache* cache, const char* test)
{
    if (cache->mode != ORACLE_MODE_OFF)
    {
        printf("%s: oracle cache (%s, %s): %llu taken, %llu run, %llu stale, %llu not kept\n", test, cache->mode == ORACLE_MODE_USE ? "use" : "check",
               cache->directory, (unsigned long long)__atomic_load_n(&cache->counts->taken, __ATOMIC_RELAXED),
               (unsigned long long)__atomic_load_n(&cache->counts->run, __ATOMIC_RELAXED), (unsigned long long)__atomic_load_n(&cache->counts->stale, __ATOMIC_RELAXED),
               (unsigned long long)__atomic_load_n(&cache->counts->unkept, __ATOMIC_RELAXED));
    }
}

// --- Byte buffers and records ---

typedef struct OracleBuffer OracleBuffer;
struct OracleBuffer
{
    uint8_t* bytes;
    uint64_t length;
    uint64_t capacity;
    bool failed;
};

static void oracle_buffer_put(OracleBuffer* buffer, const void* bytes, uint64_t length)
{
    if (!buffer->failed && buffer->capacity - buffer->length < length)
    {
        uint64_t capacity = buffer->capacity != 0 ? buffer->capacity : 4096;
        for (uint32_t doubling = 0; doubling < 64 && capacity - buffer->length < length; doubling += 1)
        {
            capacity = capacity <= UINT64_MAX / 2 ? capacity * 2 : UINT64_MAX;
        }
        uint8_t* grown = capacity - buffer->length >= length ? realloc(buffer->bytes, capacity) : 0;
        buffer->failed = grown == 0;
        buffer->bytes = grown != 0 ? grown : buffer->bytes;
        buffer->capacity = grown != 0 ? capacity : buffer->capacity;
    }
    if (!buffer->failed && length != 0)
    {
        memcpy(buffer->bytes + buffer->length, bytes, length);
        buffer->length += length;
    }
}

static void oracle_buffer_u32(OracleBuffer* buffer, uint32_t value)
{
    oracle_buffer_put(buffer, &value, sizeof(value));
}

static void oracle_buffer_u64(OracleBuffer* buffer, uint64_t value)
{
    oracle_buffer_put(buffer, &value, sizeof(value));
}

// A file's bytes into `buffer`: false if it can't be read.
static bool oracle_buffer_file(OracleBuffer* buffer, const char* path)
{
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    bool result = fd >= 0;
    bool ended = false;
    while (result && !ended)
    {
        uint8_t chunk[65536];
        const ssize_t got = read(fd, chunk, sizeof(chunk));
        oracle_buffer_put(buffer, chunk, got > 0 ? (uint64_t)got : 0);
        result = !buffer->failed && (got >= 0 || errno == EINTR);
        ended = got == 0;
    }
    if (fd >= 0)
    {
        close(fd);
    }
    return result;
}

// A file a run read, or a directory under its roots, with SHA-256 of its bytes or names.
typedef struct OracleRecord OracleRecord;
struct OracleRecord
{
    char* path;
    uint8_t digest[BUSTER_ORACLE_KEY_BYTES];
};

typedef struct OracleRecords OracleRecords;
struct OracleRecords
{
    OracleRecord* records;
    uint32_t count;
    uint32_t capacity;
    bool failed;
};

static void oracle_records_add(OracleRecords* records, const char* path, size_t length, const uint8_t* digest)
{
    if (!records->failed && records->count == records->capacity)
    {
        const uint32_t capacity = records->capacity != 0 && records->capacity < UINT32_MAX / 2 ? records->capacity * 2 : 16;
        OracleRecord* grown = capacity > records->count ? realloc(records->records, sizeof(OracleRecord) * capacity) : 0;
        records->failed = grown == 0;
        records->records = grown != 0 ? grown : records->records;
        records->capacity = grown != 0 ? capacity : records->capacity;
    }
    char* copy = records->failed ? 0 : malloc(length + 1);
    records->failed |= copy == 0;
    if (copy != 0)
    {
        memcpy(copy, path, length);
        copy[length] = 0;
        records->records[records->count].path = copy;
        memcpy(records->records[records->count].digest, digest, BUSTER_ORACLE_KEY_BYTES);
        records->count += 1;
    }
}

static void oracle_records_release(OracleRecords* records)
{
    for (uint32_t i = 0; i < records->count; i += 1)
    {
        free(records->records[i].path);
    }
    free(records->records);
    *records = (OracleRecords){ 0 };
}

// --- Runs ---

// A run of a tool through a cache: oracle_run_start starts it or takes its entry, oracle_run_finish finishes it, and
// oracle_run_release frees its result.
typedef struct OracleRun OracleRun;
struct OracleRun
{
    // The run, which the caller sets and keeps until the run is finished: the cache (null: the process's), the purpose,
    // the tool (as posix_spawnp takes it) and its arguments with a null after them, its environment (null: this
    // process's), the file its standard input reads (null: /dev/null), its working directory (null: this process's),
    // the files it writes, whether it is a compiler's run of one source whose dependencies its entry lists, whether
    // its directories hold other runs' files as it runs (so that their names aren't what it read: its dependencies
    // are), and the seconds it may run (0: no limit), past which it is killed and fails.
    OracleCache* cache;
    const char* purpose;
    char* const* arguments;
    char* const* environment;
    const char* input;
    const char* directory;
    const char* const* files;
    uint32_t file_count;
    bool dependencies;
    bool shared_directories;
    uint32_t timeout;
    // Its result once finished: its wait status, its standard output and errors (malloc's, each with a NUL after it),
    // whether it came from an entry, and whether it ran past its time limit and was killed.
    int status;
    char* output;
    uint64_t output_length;
    char* errors;
    uint64_t errors_length;
    bool taken;
    bool killed;
    // The run's own state.
    bool keyed;
    OracleKey key;
    OracleRoots roots;
    char working[BUSTER_ORACLE_PATH_BYTES];
    OracleRecords listings;
    pid_t pid;
    int output_descriptor;
    int errors_descriptor;
    char dependency_path[BUSTER_ORACLE_PATH_BYTES];
};

// `path` as the run's tool sees it, from its working directory, into `resolved`: false if it doesn't fit.
static bool oracle_run_path(const OracleRun* run, const char* path, char* resolved, size_t capacity)
{
    const int length = path[0] == '/' ? snprintf(resolved, capacity, "%s", path) : snprintf(resolved, capacity, "%s/%s", run->working, path);
    return length > 0 && (size_t)length < capacity;
}

// The run's key, with its roots found in its strings: false if its cache is off, its tool isn't on PATH, its input
// can't be read, it has more roots than BUSTER_ORACLE_ROOTS or more files than BUSTER_ORACLE_FILES, or its
// dependencies can't be asked for (an argument of -M's family is there).
static bool oracle_run_key(OracleRun* run)
{
    bool result = oracle_cache_active(run->cache) && run->arguments != 0 && run->arguments[0] != 0 && run->file_count <= BUSTER_ORACLE_FILES;
    char here[BUSTER_ORACLE_PATH_BYTES];
    result = result && getcwd(here, sizeof(here)) != 0;
    const int length = !result ? -1
                       : run->directory == 0 ? snprintf(run->working, sizeof(run->working), "%s", here)
                       : run->directory[0] == '/' ? snprintf(run->working, sizeof(run->working), "%s", run->directory)
                                                  : snprintf(run->working, sizeof(run->working), "%s/%s", here, run->directory);
    result = length > 0 && (size_t)length < sizeof(run->working);
    char* const* environment = run->environment != 0 ? run->environment : environ;
    oracle_roots_begin(&run->roots);
    uint64_t count = 0;
    while (result && run->arguments[count] != 0)
    {
        oracle_roots_find(&run->roots, run->arguments[count]);
        result = !(count != 0 && run->dependencies && strncmp(run->arguments[count], "-M", 2) == 0);
        count += 1;
    }
    if (result)
    {
        oracle_roots_find(&run->roots, run->working);
        for (uint32_t f = 0; f < run->file_count; f += 1)
        {
            oracle_roots_find(&run->roots, run->files[f]);
        }
        for (uint32_t n = 0; n < sizeof(oracle_environment_names) / sizeof(oracle_environment_names[0]); n += 1)
        {
            const char* value = oracle_environment_value(environment, n);
            oracle_roots_find(&run->roots, value != 0 ? value : "");
        }
    }
    result = result && !run->roots.overflow;
    OracleHash hash;
    oracle_hash_begin(&hash, run->cache, run->purpose != 0 ? run->purpose : "");
    result = result && oracle_hash_tool(&hash, run->arguments[0]);
    if (result)
    {
        const uint8_t dependencies = run->dependencies;
        oracle_hash_bytes(&hash, &dependencies, sizeof(dependencies));
        oracle_hash_bytes(&hash, &count, sizeof(count));
        for (uint64_t a = 1; a < count; a += 1)
        {
            oracle_hash_rooted(&hash, &run->roots, run->arguments[a]);
        }
        oracle_hash_rooted(&hash, &run->roots, run->working);
        oracle_hash_environment(&hash, &run->roots, environment);
        const uint64_t files = run->file_count;
        oracle_hash_bytes(&hash, &files, sizeof(files));
        for (uint32_t f = 0; f < run->file_count; f += 1)
        {
            oracle_hash_rooted(&hash, &run->roots, run->files[f]);
        }
        // The input's bytes (none for /dev/null), and whether its result depends on when it runs (as a dependency's).
        OracleBuffer input = { 0 };
        result = run->input == 0 || strcmp(run->input, "/dev/null") == 0 || oracle_buffer_file(&input, run->input);
        oracle_hash_bytes(&hash, input.bytes, input.length);
        const bool dated = oracle_contains(input.bytes, input.length, "__DATE__") || oracle_contains(input.bytes, input.length, "__TIME__");
        result = result && !(dated && oracle_variable(environment, "SOURCE_DATE_EPOCH") == 0);
        free(input.bytes);
    }
    run->key = oracle_hash_end(&hash);
    return result;
}

static int oracle_compare_names(const void* a, const void* b)
{
    return strcmp(*(char* const*)a, *(char* const*)b);
}

// SHA-256 of a directory's names, sorted, each with its kind (and a symbolic link's target), into `digest`: false if
// it can't be read.
static bool oracle_listing(const char* path, uint8_t* digest)
{
    DIR* directory = opendir(path);
    char** names = 0;
    uint32_t count = 0;
    uint32_t capacity = 0;
    bool result = directory != 0;
    struct dirent* entry = 0;
    while (result && (entry = readdir(directory)) != 0)
    {
        const bool own = strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0;
        if (!own && count == capacity)
        {
            capacity = capacity != 0 ? capacity * 2 : 64;
            char** grown = realloc(names, sizeof(char*) * capacity);
            result = grown != 0;
            names = grown != 0 ? grown : names;
        }
        if (!own && result)
        {
            names[count] = strdup(entry->d_name);
            result = names[count] != 0;
            count += result;
        }
    }
    if (directory != 0)
    {
        closedir(directory);
    }
    if (result && count > 1)
    {
        qsort(names, count, sizeof(char*), oracle_compare_names);
    }
    OracleHash hash;
    oracle_sha256_begin(&hash);
    for (uint32_t i = 0; result && i < count; i += 1)
    {
        char full[BUSTER_ORACLE_PATH_BYTES];
        const int length = snprintf(full, sizeof(full), "%s/%s", path, names[i]);
        struct stat status;
        result = length > 0 && (size_t)length < sizeof(full) && lstat(full, &status) == 0;
        const char kind = !result ? 0 : S_ISREG(status.st_mode) ? 'f' : S_ISDIR(status.st_mode) ? 'd' : S_ISLNK(status.st_mode) ? 'l' : 'o';
        char target[BUSTER_ORACLE_PATH_BYTES];
        const ssize_t target_length = kind == 'l' ? readlink(full, target, sizeof(target)) : 0;
        result = result && target_length >= 0 && (size_t)target_length < sizeof(target);
        oracle_hash_text(&hash, names[i]);
        oracle_hash_bytes(&hash, &kind, 1);
        oracle_hash_bytes(&hash, target, target_length > 0 ? (size_t)target_length : 0);
    }
    for (uint32_t i = 0; i < count; i += 1)
    {
        free(names[i]);
    }
    free(names);
    const OracleKey key = oracle_hash_end(&hash);
    memcpy(digest, key.bytes, sizeof(key.bytes));
    return result;
}

// What the directories under the run's roots that its arguments name held as it starts: for each place a root starts in
// an argument, the path from there to the argument's end (or a ',' or ':' after the root) if it is a directory, or
// else its directory; and the working directory if a root holds it. False if one can't be read.
static bool oracle_run_listings(OracleRun* run)
{
    bool result = true;
    uint32_t count = 0;
    while (run->arguments[count] != 0)
    {
        count += 1;
    }
    for (uint32_t a = 1; result && a <= count; a += 1)
    {
        const char* text = a < count ? run->arguments[a] : run->working;
        const size_t length = strlen(text);
        for (size_t at = 0; result && at < length; at += 1)
        {
            const uint32_t r = oracle_root_in(run->roots.roots, run->roots.count, (const uint8_t*)text, length, at);
            if (r < run->roots.count && (a < count || at == 0))
            {
                size_t end = at + run->roots.roots[r].length;
                while (end < length && text[end] != ',' && text[end] != ':')
                {
                    end += 1;
                }
                char named[BUSTER_ORACLE_PATH_BYTES];
                char path[BUSTER_ORACLE_PATH_BYTES];
                result = end - at < sizeof(named);
                if (result)
                {
                    memcpy(named, text + at, end - at);
                    named[end - at] = 0;
                    result = oracle_run_path(run, named, path, sizeof(path));
                }
                // Not a directory: its directory, if the root holds it (a root that is a file has none to list).
                struct stat status;
                bool listed = result;
                if (result && !(stat(path, &status) == 0 && S_ISDIR(status.st_mode)))
                {
                    const char* slash = strrchr(path, '/');
                    const size_t kept = slash != 0 ? (size_t)(slash - path) : 0;
                    const size_t root_end = (named[0] == '/' ? 0 : strlen(run->working) + 1) + run->roots.roots[r].length;
                    listed = kept >= root_end;
                    if (listed)
                    {
                        path[kept] = 0;
                    }
                }
                for (uint32_t l = 0; listed && l < run->listings.count; l += 1)
                {
                    listed = strcmp(run->listings.records[l].path, path) != 0;
                }
                uint8_t digest[BUSTER_ORACLE_KEY_BYTES];
                result = result && (!listed || oracle_listing(path, digest));
                if (result && listed)
                {
                    oracle_records_add(&run->listings, path, strlen(path), digest);
                    result = !run->listings.failed;
                }
            }
        }
    }
    return result;
}

// A file for a process's output: in memory (memfd_create), or an unnamed temporary file where the kernel has none;
// closed on exec. -1 if neither can be made.
static int oracle_capture(void)
{
    // MFD_CLOEXEC, which <sys/mman.h> declares only with _GNU_SOURCE.
    const unsigned int close_on_exec = 1;
    int result = (int)syscall(SYS_memfd_create, "buster-oracle", close_on_exec);
    if (result < 0)
    {
        char path[] = "/tmp/buster-oracle-capture-XXXXXX";
        result = mkstemp(path);
        if (result >= 0)
        {
            unlink(path);
            fcntl(result, F_SETFD, FD_CLOEXEC);
        }
    }
    // Never a standard descriptor, which the child's dup2 calls would overwrite where the caller has closed one.
    if (result >= 0 && result <= 2)
    {
        const int low = result;
        result = fcntl(low, F_DUPFD_CLOEXEC, 3);
        close(low);
    }
    return result;
}

// A captured output's bytes, malloc's with a NUL after them: false if they can't be read.
static bool oracle_read_capture(int descriptor, char** bytes, uint64_t* length)
{
    struct stat status;
    bool result = descriptor >= 0 && fstat(descriptor, &status) == 0;
    const uint64_t size = result ? (uint64_t)status.st_size : 0;
    char* text = result ? malloc(size + 1) : 0;
    result = result && text != 0;
    uint64_t done = 0;
    while (result && done < size)
    {
        const ssize_t got = pread(descriptor, text + done, size - done, (off_t)done);
        result = got > 0 || (got < 0 && errno == EINTR);
        done += got > 0 ? (uint64_t)got : 0;
    }
    if (result)
    {
        text[size] = 0;
    }
    else
    {
        free(text);
        text = 0;
    }
    *bytes = text;
    *length = result ? size : 0;
    return result;
}

// Starts the run's tool, with -MD -MF and a file of its own if `dependencies`: false if it can't start.
static bool oracle_run_spawn(OracleRun* run, bool dependencies)
{
    uint32_t count = 0;
    while (run->arguments[count] != 0)
    {
        count += 1;
    }
    char** argv = calloc((size_t)count + 4, sizeof(char*));
    bool result = argv != 0;
    if (result && dependencies)
    {
        const int length = snprintf(run->dependency_path, sizeof(run->dependency_path), "%s/buster-oracle-XXXXXX", run->roots.base_length != 0 ? run->roots.base : "/tmp");
        const int descriptor = length > 0 && (size_t)length < sizeof(run->dependency_path) ? mkstemp(run->dependency_path) : -1;
        result = descriptor >= 0;
        run->dependency_path[0] = result ? run->dependency_path[0] : 0;
        if (descriptor >= 0)
        {
            close(descriptor);
        }
    }
    if (result)
    {
        uint32_t n = 0;
        argv[n++] = run->arguments[0];
        if (dependencies)
        {
            argv[n++] = "-MD";
            argv[n++] = "-MF";
            argv[n++] = run->dependency_path;
        }
        for (uint32_t a = 1; a < count; a += 1)
        {
            argv[n++] = run->arguments[a];
        }
        argv[n] = 0;
    }
    run->output_descriptor = result ? oracle_capture() : -1;
    run->errors_descriptor = result ? oracle_capture() : -1;
    result = result && run->output_descriptor >= 0 && run->errors_descriptor >= 0;
    posix_spawn_file_actions_t actions;
    const bool actions_made = result && posix_spawn_file_actions_init(&actions) == 0;
    result = actions_made && posix_spawn_file_actions_addopen(&actions, 0, run->input != 0 ? run->input : "/dev/null", O_RDONLY, 0) == 0 &&
             posix_spawn_file_actions_adddup2(&actions, run->output_descriptor, 1) == 0 && posix_spawn_file_actions_adddup2(&actions, run->errors_descriptor, 2) == 0 &&
             (run->directory == 0 || posix_spawn_file_actions_addchdir_np(&actions, run->directory) == 0);
    pid_t pid = 0;
    result = result && posix_spawnp(&pid, run->arguments[0], &actions, 0, argv, run->environment != 0 ? run->environment : environ) == 0;
    run->pid = result ? pid : 0;
    if (actions_made)
    {
        posix_spawn_file_actions_destroy(&actions);
    }
    free(argv);
    return result;
}

// --- Entries ---

static const char oracle_magic[8] = { 'B', 'U', 'S', 'T', 'O', 'R', 'C', '2' };

// An entry's header: the magic, the key, the wait status, its records' counts and the bytes after it. Then its roots
// (each a u32 length and its name), its dependencies and its listings (each a u32 length, the path as the run named it
// and SHA-256), its files (each a u64 length, UINT64_MAX for none, and its bytes), its standard output and its errors
// (each a u64 length and its bytes).
typedef struct OracleEntryHeader OracleEntryHeader;
struct OracleEntryHeader
{
    char magic[8];
    uint8_t key[BUSTER_ORACLE_KEY_BYTES];
    int32_t status;
    uint32_t roots;
    uint32_t dependencies;
    uint32_t listings;
    uint32_t files;
    uint32_t reserved;
    uint64_t body;
};

// An entry read: its file's bytes (malloc's) and where its parts are in them.
typedef struct OracleEntry OracleEntry;
struct OracleEntry
{
    uint8_t* bytes;
    int status;
    uint32_t root_count;
    OracleRoot roots[BUSTER_ORACLE_ROOTS];
    uint32_t dependency_count;
    uint64_t dependencies;
    uint32_t listing_count;
    uint64_t listings;
    uint32_t file_count;
    uint64_t files;
    uint8_t* output;
    uint64_t output_length;
    uint8_t* errors;
    uint64_t errors_length;
};

static void oracle_entry_path(const OracleCache* cache, OracleKey key, char* path, size_t capacity)
{
    char name[2 * BUSTER_ORACLE_KEY_BYTES + 1];
    for (uint32_t i = 0; i < BUSTER_ORACLE_KEY_BYTES; i += 1)
    {
        snprintf(name + 2 * i, 3, "%02x", key.bytes[i]);
    }
    snprintf(path, capacity, "%s/%s", cache->directory, name);
}

// A u32 or u64 at bytes[*at] if it is within `end`, *at past it.
static bool oracle_take_u32(const uint8_t* bytes, uint64_t end, uint64_t* at, uint32_t* value)
{
    const bool result = end - *at >= sizeof(*value);
    if (result)
    {
        memcpy(value, bytes + *at, sizeof(*value));
        *at += sizeof(*value);
    }
    return result;
}

static bool oracle_take_u64(const uint8_t* bytes, uint64_t end, uint64_t* at, uint64_t* value)
{
    const bool result = end - *at >= sizeof(*value);
    if (result)
    {
        memcpy(value, bytes + *at, sizeof(*value));
        *at += sizeof(*value);
    }
    return result;
}

// Steps over `count` records of an entry, each a u32 length, its bytes and, with `digests`, SHA-256: false if one
// runs past `end`.
static bool oracle_entry_skip(const uint8_t* bytes, uint64_t end, uint64_t* at, uint32_t count, bool digests)
{
    bool result = true;
    for (uint32_t i = 0; result && i < count; i += 1)
    {
        uint32_t length = 0;
        result = oracle_take_u32(bytes, end, at, &length) && end - *at >= (uint64_t)length + (digests ? BUSTER_ORACLE_KEY_BYTES : 0);
        *at += result ? (uint64_t)length + (digests ? BUSTER_ORACLE_KEY_BYTES : 0) : 0;
    }
    return result;
}

// The record at bytes[*at] of a section an entry's reading checked: its path into `path` (with the entry's roots
// replaced by `roots`, as many and as long) and its SHA-256, *at past it. False if the path doesn't fit.
static bool oracle_entry_record(const OracleEntry* entry, uint64_t* at, const OracleRoot* roots, char* path, size_t capacity, const uint8_t** digest)
{
    uint32_t length = 0;
    memcpy(&length, entry->bytes + *at, sizeof(length));
    const bool result = length < capacity;
    if (result)
    {
        memcpy(path, entry->bytes + *at + sizeof(length), length);
        path[length] = 0;
        oracle_roots_replace(entry->roots, roots, entry->root_count, (uint8_t*)path, length);
    }
    *digest = entry->bytes + *at + sizeof(length) + length;
    *at += sizeof(length) + (uint64_t)length + BUSTER_ORACLE_KEY_BYTES;
    return result;
}

static void oracle_entry_release(OracleEntry* entry)
{
    free(entry->bytes);
    entry->bytes = 0;
}

// The entry of `key`, if it is there and whole: false if not.
static bool oracle_entry_read(const OracleCache* cache, OracleKey key, OracleEntry* entry)
{
    char path[BUSTER_ORACLE_PATH_BYTES + 80];
    oracle_entry_path(cache, key, path, sizeof(path));
    *entry = (OracleEntry){ 0 };
    OracleBuffer buffer = { 0 };
    bool result = oracle_buffer_file(&buffer, path) && buffer.length >= sizeof(OracleEntryHeader);
    OracleEntryHeader header;
    if (result)
    {
        memcpy(&header, buffer.bytes, sizeof(header));
    }
    result = result && memcmp(header.magic, oracle_magic, sizeof(oracle_magic)) == 0 && memcmp(header.key, key.bytes, sizeof(key.bytes)) == 0 &&
             header.body == buffer.length - sizeof(header) && header.roots <= BUSTER_ORACLE_ROOTS && header.files <= BUSTER_ORACLE_FILES;
    const uint64_t end = buffer.length;
    uint64_t at = sizeof(header);
    for (uint32_t r = 0; result && r < header.roots; r += 1)
    {
        uint32_t length = 0;
        result = oracle_take_u32(buffer.bytes, end, &at, &length) && end - at >= length && length > BUSTER_ORACLE_RANDOM;
        if (result)
        {
            entry->roots[r] = (OracleRoot){ .name = (const char*)buffer.bytes + at, .length = length };
            at += length;
        }
    }
    entry->dependencies = at;
    result = result && oracle_entry_skip(buffer.bytes, end, &at, header.dependencies, true);
    entry->listings = at;
    result = result && oracle_entry_skip(buffer.bytes, end, &at, header.listings, true);
    entry->files = at;
    for (uint32_t f = 0; result && f < header.files; f += 1)
    {
        uint64_t length = 0;
        result = oracle_take_u64(buffer.bytes, end, &at, &length) && (length == UINT64_MAX || end - at >= length);
        at += result && length != UINT64_MAX ? length : 0;
    }
    result = result && oracle_take_u64(buffer.bytes, end, &at, &entry->output_length) && end - at >= entry->output_length;
    entry->output = result ? buffer.bytes + at : 0;
    at += result ? entry->output_length : 0;
    result = result && oracle_take_u64(buffer.bytes, end, &at, &entry->errors_length) && end - at >= entry->errors_length;
    entry->errors = result ? buffer.bytes + at : 0;
    at += result ? entry->errors_length : 0;
    result = result && at == end;
    if (result)
    {
        entry->bytes = buffer.bytes;
        entry->status = header.status;
        entry->root_count = header.roots;
        entry->dependency_count = header.dependencies;
        entry->listing_count = header.listings;
        entry->file_count = header.files;
    }
    else
    {
        free(buffer.bytes);
        *entry = (OracleEntry){ 0 };
    }
    return result;
}

// Whether the entry is of the run's shape: as many roots, each as long as the run's, and as many files.
static bool oracle_entry_fits(const OracleEntry* entry, const OracleRun* run)
{
    bool result = entry->root_count == run->roots.count && entry->file_count == run->file_count;
    for (uint32_t r = 0; result && r < entry->root_count; r += 1)
    {
        result = entry->roots[r].length == run->roots.roots[r].length;
    }
    return result;
}

// A dependency's state as the run reads it: SHA-256 of its bytes or of its identity, its device and inode, and its
// modification time.
typedef struct OracleState OracleState;
struct OracleState
{
    uint8_t bytes[BUSTER_ORACLE_KEY_BYTES];
    uint64_t device;
    uint64_t inode;
    int64_t time[2];
};

// Whether `path` is one of the run's roots or is in one.
static bool oracle_run_rooted(const OracleRun* run, const char* path)
{
    bool result = false;
    for (uint32_t r = 0; r < run->roots.count && !result; r += 1)
    {
        const OracleRoot* root = &run->roots.roots[r];
        result = strncmp(path, root->name, root->length) == 0 && (path[root->length] == '/' || path[root->length] == 0);
    }
    return result;
}

// What a run reads of `count` files it names, into `digests` (BUSTER_ORACLE_KEY_BYTES each): SHA-256 of a file's bytes
// if it is in one of the run's roots (which tests write anew for each run), and otherwise of its identity, what stat
// gives of it (device, inode, size, modification and change times), as the runtime snapshot's probes (the system's
// headers, the repository's sources and the frozen corpora, which keep theirs from one run to the next until they
// change); of the place of the first of them that is the same file (by device and inode: #pragma once and include
// guards take a file once, whatever names reach it); and if a rooted one has __TIMESTAMP__ (the modification time of
// the file it expands in), of its modification time. False if a file can't be read, or the run's result depends on when
// it runs: __DATE__ or __TIME__ in a rooted one, without SOURCE_DATE_EPOCH in its environment.
static bool oracle_dependency_digests(const OracleRun* run, char* const* names, uint32_t count, uint8_t* digests)
{
    OracleState* states = calloc((size_t)count + 1, sizeof(OracleState));
    bool result = states != 0;
    bool timestamp = false;
    bool dated = false;
    for (uint32_t i = 0; result && i < count; i += 1)
    {
        char path[BUSTER_ORACLE_PATH_BYTES];
        OracleBuffer bytes = { 0 };
        struct stat status;
        result = oracle_run_path(run, names[i], path, sizeof(path)) && stat(path, &status) == 0;
        const bool rooted = result && oracle_run_rooted(run, path);
        result = result && (!rooted || (S_ISREG(status.st_mode) && oracle_buffer_file(&bytes, path)));
        if (result)
        {
            const int64_t identity[7] = {
                (int64_t)status.st_dev,
                (int64_t)status.st_ino,
                (int64_t)status.st_size,
                (int64_t)status.st_mtim.tv_sec,
                (int64_t)status.st_mtim.tv_nsec,
                (int64_t)status.st_ctim.tv_sec,
                (int64_t)status.st_ctim.tv_nsec,
            };
            OracleHash hash;
            oracle_sha256_begin(&hash);
            oracle_hash_bytes(&hash, rooted ? "bytes" : "identity", rooted ? 5 : 8);
            oracle_hash_bytes(&hash, rooted ? (const void*)bytes.bytes : (const void*)identity, rooted ? bytes.length : sizeof(identity));
            const OracleKey key = oracle_hash_end(&hash);
            memcpy(states[i].bytes, key.bytes, sizeof(key.bytes));
            states[i].device = (uint64_t)status.st_dev;
            states[i].inode = (uint64_t)status.st_ino;
            states[i].time[0] = (int64_t)status.st_mtim.tv_sec;
            states[i].time[1] = (int64_t)status.st_mtim.tv_nsec;
            timestamp |= oracle_contains(bytes.bytes, bytes.length, "__TIMESTAMP__");
            dated |= oracle_contains(bytes.bytes, bytes.length, "__DATE__") || oracle_contains(bytes.bytes, bytes.length, "__TIME__");
        }
        free(bytes.bytes);
    }
    result = result && !(dated && oracle_variable(run->environment != 0 ? run->environment : environ, "SOURCE_DATE_EPOCH") == 0);
    for (uint32_t i = 0; result && i < count; i += 1)
    {
        uint32_t same = i;
        for (uint32_t j = 0; j < i && same == i; j += 1)
        {
            same = states[j].device == states[i].device && states[j].inode == states[i].inode ? j : i;
        }
        OracleHash hash;
        oracle_sha256_begin(&hash);
        oracle_hash_bytes(&hash, states[i].bytes, sizeof(states[i].bytes));
        oracle_hash_bytes(&hash, &same, sizeof(same));
        oracle_hash_bytes(&hash, states[i].time, timestamp ? sizeof(states[i].time) : 0);
        const OracleKey key = oracle_hash_end(&hash);
        memcpy(digests + (size_t)i * BUSTER_ORACLE_KEY_BYTES, key.bytes, sizeof(key.bytes));
    }
    free(states);
    return result;
}

// Whether what the entry's run read is as the run finds it: its dependencies (as `fresh` has them, or as the files are
// now if it is null: oracle_dependency_digests) and its listings (as the run's start found them), the entry's roots
// replaced by the run's. The entry fits the run (oracle_entry_fits).
static bool oracle_entry_inputs(const OracleEntry* entry, const OracleRun* run, const OracleRecords* fresh)
{
    const uint32_t count = entry->dependency_count;
    char** names = calloc((size_t)count + 1, sizeof(char*));
    uint8_t* stored = malloc(((size_t)count + 1) * BUSTER_ORACLE_KEY_BYTES);
    uint8_t* now = fresh == 0 ? malloc(((size_t)count + 1) * BUSTER_ORACLE_KEY_BYTES) : 0;
    bool result = names != 0 && stored != 0 && (fresh != 0 || now != 0) && (fresh == 0 || fresh->count == count);
    uint64_t at = entry->dependencies;
    for (uint32_t d = 0; result && d < count; d += 1)
    {
        char named[BUSTER_ORACLE_PATH_BYTES];
        const uint8_t* digest = 0;
        result = oracle_entry_record(entry, &at, run->roots.roots, named, sizeof(named), &digest);
        names[d] = result ? strdup(named) : 0;
        result = result && names[d] != 0;
        if (result)
        {
            memcpy(stored + (size_t)d * BUSTER_ORACLE_KEY_BYTES, digest, BUSTER_ORACLE_KEY_BYTES);
        }
    }
    for (uint32_t d = 0; result && fresh != 0 && d < count; d += 1)
    {
        result = strcmp(fresh->records[d].path, names[d]) == 0 && memcmp(fresh->records[d].digest, stored + (size_t)d * BUSTER_ORACLE_KEY_BYTES, BUSTER_ORACLE_KEY_BYTES) == 0;
    }
    result = result && (fresh != 0 || (oracle_dependency_digests(run, names, count, now) && memcmp(now, stored, (size_t)count * BUSTER_ORACLE_KEY_BYTES) == 0));
    result = result && run->listings.count == entry->listing_count;
    at = entry->listings;
    for (uint32_t l = 0; result && l < entry->listing_count; l += 1)
    {
        char path[BUSTER_ORACLE_PATH_BYTES];
        const uint8_t* digest = 0;
        result = oracle_entry_record(entry, &at, run->roots.roots, path, sizeof(path), &digest) && strcmp(run->listings.records[l].path, path) == 0 &&
                 memcmp(run->listings.records[l].digest, digest, BUSTER_ORACLE_KEY_BYTES) == 0;
    }
    for (uint32_t d = 0; names != 0 && d < count; d += 1)
    {
        free(names[d]);
    }
    free(names);
    free(stored);
    free(now);
    return result;
}

// A file a run wrote: whether it was there after the run, and its bytes.
typedef struct OracleFile OracleFile;
struct OracleFile
{
    bool present;
    OracleBuffer bytes;
};

// The entry's files' bytes, each with its length (UINT64_MAX for a file that wasn't there) and where its bytes are.
static void oracle_entry_file(const OracleEntry* entry, uint32_t file, uint8_t** bytes, uint64_t* length)
{
    uint64_t at = entry->files;
    for (uint32_t f = 0; f <= file; f += 1)
    {
        memcpy(length, entry->bytes + at, sizeof(*length));
        at += sizeof(*length);
        *bytes = entry->bytes + at;
        at += f < file && *length != UINT64_MAX ? *length : 0;
    }
}

// Replaces the entry's roots with the run's in its output, errors and files.
static void oracle_entry_root(OracleEntry* entry, const OracleRun* run)
{
    oracle_roots_replace(entry->roots, run->roots.roots, entry->root_count, entry->output, entry->output_length);
    oracle_roots_replace(entry->roots, run->roots.roots, entry->root_count, entry->errors, entry->errors_length);
    for (uint32_t f = 0; f < entry->file_count; f += 1)
    {
        uint8_t* bytes = 0;
        uint64_t length = 0;
        oracle_entry_file(entry, f, &bytes, &length);
        oracle_roots_replace(entry->roots, run->roots.roots, entry->root_count, bytes, length != UINT64_MAX ? length : 0);
    }
}

// Whether the entry, its roots replaced with the run's (oracle_entry_root), holds the run's result: what differs, or
// null if nothing does.
static const char* oracle_entry_differs(const OracleEntry* entry, const OracleRun* run, const OracleFile* files)
{
    const char* result = entry->status != run->status ? "its status"
                         : entry->output_length != run->output_length || memcmp(entry->output, run->output, run->output_length) != 0 ? "its standard output"
                         : entry->errors_length != run->errors_length || memcmp(entry->errors, run->errors, run->errors_length) != 0 ? "its errors"
                                                                                                                                         : 0;
    for (uint32_t f = 0; result == 0 && f < entry->file_count; f += 1)
    {
        uint8_t* bytes = 0;
        uint64_t length = 0;
        oracle_entry_file(entry, f, &bytes, &length);
        const bool same = files[f].present ? length == files[f].bytes.length && (length == 0 || memcmp(bytes, files[f].bytes.bytes, length) == 0) : length == UINT64_MAX;
        result = same ? 0 : "a file it writes";
    }
    return result;
}

// Writes the run's entry, into a file of its own renamed over its name: false if it couldn't.
static bool oracle_entry_write(const OracleRun* run, const OracleRecords* dependencies, const OracleFile* files)
{
    OracleBuffer body = { 0 };
    for (uint32_t r = 0; r < run->roots.count; r += 1)
    {
        oracle_buffer_u32(&body, run->roots.roots[r].length);
        oracle_buffer_put(&body, run->roots.roots[r].name, run->roots.roots[r].length);
    }
    const OracleRecords* sections[2] = { dependencies, &run->listings };
    for (uint32_t s = 0; s < 2; s += 1)
    {
        for (uint32_t i = 0; i < sections[s]->count; i += 1)
        {
            const uint32_t length = (uint32_t)strlen(sections[s]->records[i].path);
            oracle_buffer_u32(&body, length);
            oracle_buffer_put(&body, sections[s]->records[i].path, length);
            oracle_buffer_put(&body, sections[s]->records[i].digest, BUSTER_ORACLE_KEY_BYTES);
        }
    }
    for (uint32_t f = 0; f < run->file_count; f += 1)
    {
        oracle_buffer_u64(&body, files[f].present ? files[f].bytes.length : UINT64_MAX);
        oracle_buffer_put(&body, files[f].bytes.bytes, files[f].present ? files[f].bytes.length : 0);
    }
    oracle_buffer_u64(&body, run->output_length);
    oracle_buffer_put(&body, run->output, run->output_length);
    oracle_buffer_u64(&body, run->errors_length);
    oracle_buffer_put(&body, run->errors, run->errors_length);
    OracleEntryHeader header = {
        .status = run->status,
        .roots = run->roots.count,
        .dependencies = dependencies->count,
        .listings = run->listings.count,
        .files = run->file_count,
        .body = body.length,
    };
    memcpy(header.magic, oracle_magic, sizeof(oracle_magic));
    memcpy(header.key, run->key.bytes, sizeof(run->key.bytes));
    char path[BUSTER_ORACLE_PATH_BYTES + 80];
    char temporary[BUSTER_ORACLE_PATH_BYTES + 160];
    oracle_entry_path(run->cache, run->key, path, sizeof(path));
    static uint64_t written;
    written += 1;
    snprintf(temporary, sizeof(temporary), "%s.%ld.%llu.new", path, (long)getpid(), (unsigned long long)written);
    const int fd = body.failed ? -1 : open(temporary, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    bool result = fd >= 0 && write(fd, &header, sizeof(header)) == (ssize_t)sizeof(header);
    uint64_t done = 0;
    while (result && done < body.length)
    {
        const ssize_t put = write(fd, body.bytes + done, body.length - done);
        result = put > 0 || (put < 0 && errno == EINTR);
        done += put > 0 ? (uint64_t)put : 0;
    }
    if (fd >= 0)
    {
        result &= close(fd) == 0;
    }
    result = result && rename(temporary, path) == 0;
    if (!result && fd >= 0)
    {
        unlink(temporary);
    }
    free(body.bytes);
    return result;
}

// The files the run's dependency file names, each with its digest (oracle_dependency_digests): false if it can't be
// read, it writes a name this reader doesn't take (an escaped space, '#' or '$': it reads none), or the digests can't
// be made.
static bool oracle_run_read_dependencies(const OracleRun* run, OracleRecords* records)
{
    OracleBuffer text = { 0 };
    bool result = run->dependency_path[0] != 0 && oracle_buffer_file(&text, run->dependency_path);
    oracle_buffer_put(&text, "", 1);
    result = result && !text.failed;
    const char* bytes = result ? (const char*)text.bytes : "";
    const uint64_t length = result ? text.length - 1 : 0;
    // The target's end: the first ':' before a blank, a line's end, a backslash or the end.
    uint64_t at = 0;
    while (result && at < length && !(bytes[at] == ':' && (at + 1 == length || bytes[at + 1] == ' ' || bytes[at + 1] == '\t' || bytes[at + 1] == '\n' ||
                                                           bytes[at + 1] == '\r' || bytes[at + 1] == '\\')))
    {
        at += 1;
    }
    result = result && at < length;
    at += 1;
    while (result && at < length)
    {
        const char c = bytes[at];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
        {
            at += 1;
        }
        else if (c == '\\' && at + 1 < length && (bytes[at + 1] == '\n' || (bytes[at + 1] == '\r' && at + 2 < length && bytes[at + 2] == '\n')))
        {
            at += bytes[at + 1] == '\n' ? 2 : 3;
        }
        else
        {
            const uint64_t start = at;
            while (at < length && bytes[at] != ' ' && bytes[at] != '\t' && bytes[at] != '\n' && bytes[at] != '\r')
            {
                result = result && bytes[at] != '\\' && bytes[at] != '$' && bytes[at] != '#';
                at += 1;
            }
            const uint8_t none[BUSTER_ORACLE_KEY_BYTES] = { 0 };
            result = result && at - start < BUSTER_ORACLE_PATH_BYTES;
            if (result)
            {
                oracle_records_add(records, bytes + start, at - start, none);
                result = !records->failed;
            }
        }
    }
    free(text.bytes);
    char** names = result ? calloc((size_t)records->count + 1, sizeof(char*)) : 0;
    uint8_t* digests = result ? malloc(((size_t)records->count + 1) * BUSTER_ORACLE_KEY_BYTES) : 0;
    result = result && names != 0 && digests != 0;
    for (uint32_t i = 0; result && i < records->count; i += 1)
    {
        names[i] = records->records[i].path;
    }
    result = result && oracle_dependency_digests(run, names, records->count, digests);
    for (uint32_t i = 0; result && i < records->count; i += 1)
    {
        memcpy(records->records[i].digest, digests + (size_t)i * BUSTER_ORACLE_KEY_BYTES, BUSTER_ORACLE_KEY_BYTES);
    }
    free(names);
    free(digests);
    return result;
}

// After a run that ran: what it read and wrote, and its entry written; in check mode compared first with the entry
// there, if what that entry's run read is what this one read. False if that entry is stale, with a line on the errors
// naming it.
static bool oracle_run_keep(OracleRun* run)
{
    OracleRecords dependencies = { 0 };
    bool keep = !run->dependencies || oracle_run_read_dependencies(run, &dependencies);
    OracleFile files[BUSTER_ORACLE_FILES];
    for (uint32_t f = 0; f < run->file_count; f += 1)
    {
        char path[BUSTER_ORACLE_PATH_BYTES];
        struct stat status;
        files[f] = (OracleFile){ 0 };
        keep = keep && oracle_run_path(run, run->files[f], path, sizeof(path));
        files[f].present = keep && lstat(path, &status) == 0;
        keep = keep && (!files[f].present || (S_ISREG(status.st_mode) && oracle_buffer_file(&files[f].bytes, path)));
    }
    const char* differs = 0;
    OracleEntry entry;
    if (keep && run->cache->mode == ORACLE_MODE_CHECK && oracle_entry_read(run->cache, run->key, &entry))
    {
        if (oracle_entry_fits(&entry, run) && oracle_entry_inputs(&entry, run, &dependencies))
        {
            oracle_entry_root(&entry, run);
            differs = oracle_entry_differs(&entry, run, files);
        }
        oracle_entry_release(&entry);
    }
    if (differs != 0)
    {
        char name[BUSTER_ORACLE_PATH_BYTES + 80];
        oracle_entry_path(run->cache, run->key, name, sizeof(name));
        fprintf(stderr, "oracle cache: the entry %s (%s) is stale: %s differs from the run's:", name, run->purpose != 0 ? run->purpose : "", differs);
        for (uint32_t a = 0; run->arguments[a] != 0; a += 1)
        {
            fprintf(stderr, " %s", run->arguments[a]);
        }
        fprintf(stderr, "%s%s\n", run->input != 0 ? " < " : "", run->input != 0 ? run->input : "");
        oracle_count(&run->cache->counts->stale);
    }
    const bool written = keep && oracle_entry_write(run, &dependencies, files);
    oracle_count(written ? &run->cache->counts->run : &run->cache->counts->unkept);
    for (uint32_t f = 0; f < run->file_count; f += 1)
    {
        free(files[f].bytes.bytes);
    }
    oracle_records_release(&dependencies);
    return differs == 0;
}

// In use mode, before a run: its entry's result, if the entry is there, fits the run and what its run read is as it
// is now, with the files it writes written (true: the tool doesn't run).
static bool oracle_run_take(OracleRun* run)
{
    OracleEntry entry;
    bool result = oracle_entry_read(run->cache, run->key, &entry) && oracle_entry_fits(&entry, run) && oracle_entry_inputs(&entry, run, 0);
    if (result)
    {
        oracle_entry_root(&entry, run);
        for (uint32_t f = 0; result && f < run->file_count; f += 1)
        {
            uint8_t* bytes = 0;
            uint64_t length = 0;
            char path[BUSTER_ORACLE_PATH_BYTES];
            oracle_entry_file(&entry, f, &bytes, &length);
            result = oracle_run_path(run, run->files[f], path, sizeof(path));
            if (result && length == UINT64_MAX)
            {
                result = unlink(path) == 0 || errno == ENOENT;
            }
            else if (result)
            {
                const int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
                uint64_t done = 0;
                result = fd >= 0;
                while (result && done < length)
                {
                    const ssize_t put = write(fd, bytes + done, length - done);
                    result = put > 0 || (put < 0 && errno == EINTR);
                    done += put > 0 ? (uint64_t)put : 0;
                }
                if (fd >= 0)
                {
                    result &= close(fd) == 0;
                }
            }
        }
        run->output = result ? malloc(entry.output_length + 1) : 0;
        run->errors = result ? malloc(entry.errors_length + 1) : 0;
        result = result && run->output != 0 && run->errors != 0;
        if (result)
        {
            memcpy(run->output, entry.output, entry.output_length);
            run->output[entry.output_length] = 0;
            run->output_length = entry.output_length;
            memcpy(run->errors, entry.errors, entry.errors_length);
            run->errors[entry.errors_length] = 0;
            run->errors_length = entry.errors_length;
            run->status = entry.status;
        }
        else
        {
            free(run->output);
            free(run->errors);
            run->output = 0;
            run->errors = 0;
        }
    }
    oracle_entry_release(&entry);
    return result;
}

// Starts a run: in use mode takes its entry if it can (OracleRun.taken), and otherwise starts its tool. False if the
// tool can't start; oracle_run_finish then finishes it all the same.
static bool oracle_run_start(OracleRun* run)
{
    run->cache = run->cache != 0 ? run->cache : oracle_cache();
    run->status = 0;
    run->output = 0;
    run->output_length = 0;
    run->errors = 0;
    run->errors_length = 0;
    run->taken = false;
    run->killed = false;
    run->pid = 0;
    run->output_descriptor = -1;
    run->errors_descriptor = -1;
    run->dependency_path[0] = 0;
    run->listings = (OracleRecords){ 0 };
    run->keyed = oracle_run_key(run);
    run->keyed = run->keyed && (!run->dependencies || run->shared_directories || oracle_run_listings(run));
    run->taken = run->keyed && run->cache->mode == ORACLE_MODE_USE && oracle_run_take(run);
    if (run->taken)
    {
        oracle_count(&run->cache->counts->taken);
    }
    else if (oracle_cache_active(run->cache) && !run->keyed)
    {
        oracle_count(&run->cache->counts->unkept);
    }
    return run->taken || oracle_run_spawn(run, run->keyed && run->dependencies);
}

// Finishes a run: waits for its tool, reads what it wrote, and keeps its entry. False if the tool didn't run or can't
// be waited for, its output can't be read, or (in check mode) its entry is stale; the result is the tool's.
static bool oracle_run_finish(OracleRun* run)
{
    bool result = run->taken;
    if (!run->taken)
    {
        // With a time limit, the wait a millisecond at a time, and past the limit the tool killed and waited for.
        int status = 0;
        pid_t waited = 0;
        bool killed = false;
        uint64_t waits = 0;
        const struct timespec pause = { .tv_sec = 0, .tv_nsec = 1000000 };
        while (run->pid > 0 && waited != run->pid && !(waited < 0 && errno != EINTR))
        {
            const bool expired = run->timeout != 0 && waits >= 1000ull * run->timeout;
            if (expired && !killed)
            {
                kill(run->pid, SIGKILL);
                killed = true;
            }
            waited = waitpid(run->pid, &status, run->timeout != 0 && !expired ? WNOHANG : 0);
            if (waited == 0)
            {
                nanosleep(&pause, 0);
                waits += 1;
            }
        }
        result = run->pid > 0 && waited == run->pid && !killed;
        run->killed = killed;
        run->status = status;
        result = oracle_read_capture(run->output_descriptor, &run->output, &run->output_length) && result;
        result = oracle_read_capture(run->errors_descriptor, &run->errors, &run->errors_length) && result;
        if (run->keyed && !result)
        {
            oracle_count(&run->cache->counts->unkept);
        }
        result = result && (!run->keyed || oracle_run_keep(run));
    }
    if (run->output_descriptor >= 0)
    {
        close(run->output_descriptor);
    }
    if (run->errors_descriptor >= 0)
    {
        close(run->errors_descriptor);
    }
    if (run->dependency_path[0] != 0)
    {
        unlink(run->dependency_path);
    }
    run->output_descriptor = -1;
    run->errors_descriptor = -1;
    run->dependency_path[0] = 0;
    run->pid = 0;
    oracle_records_release(&run->listings);
    return result;
}

// A run started and finished.
static bool oracle_run(OracleRun* run)
{
    const bool started = oracle_run_start(run);
    const bool finished = oracle_run_finish(run);
    return started && finished;
}

// Frees a finished run's output and errors.
static void oracle_run_release(OracleRun* run)
{
    free(run->output);
    free(run->errors);
    run->output = 0;
    run->errors = 0;
}

#endif
