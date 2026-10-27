// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The oracle cache (#542): the results of the clang and gcc runs that the tests compare with, kept between runs in the
// cache directory (snapshot_directory's: $XDG_CACHE_HOME/buster, or $HOME/.cache/buster) under oracle/, an entry a file
// named by its key. BUSTER_ORACLE_CACHE picks the mode:
//   - `use`: a run whose entry is there takes the entry's result rather than run the tool, and a run that runs writes
//     its entry;
//   - `check`: every run runs, and where its entry is there, the entry must hold the run's result: an entry that differs
//     is stale, which the test that reads it reports and fails on (the 9700X's daily run). Every result is written;
//   - anything else, or unset: every run runs and nothing is kept (local runs).
//
// A key is SHA-256 (FIPS 180-4) of what a run's result depends on, each part after its length:
//   - its purpose, the caller's name for its kind of run;
//   - the tool: the first file of its name on PATH that may be run, as posix_spawnp finds it, and what stat gives of
//     the file it resolves to (device, inode, size, modification and change times, as the runtime snapshot's probes
//     have them); the same of the package database (pacman's, dpkg's or rpm's), which an update of any package
//     changes, so that a new clang, its libraries or its headers make new keys; and the CPU's model and flags, which
//     -march=native reads;
//   - its arguments, exactly;
//   - the environment variables that change a compiler's result (oracle_environment_names), with their values;
//   - the bytes of the file its standard input reads, and of any other file the caller names.
// A run that reads files its key doesn't name (headers) isn't for the cache until its key names them.
//
// An entry: the magic, the key, the exit status, the standard output's length and its bytes. It is written into a file
// of its own beside the entries and renamed over its name, so that a reader sees a whole entry or none, whatever tests
// write at once; two writers of one key write the same bytes.
//
// Test tooling outside the compile path: it hashes a few kilobytes per run and has no vector path.
#ifndef BUSTER_ORACLE_CACHE_INCLUDED
#define BUSTER_ORACLE_CACHE_INCLUDED

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#define BUSTER_ORACLE_KEY_BYTES 32
#define BUSTER_ORACLE_PATH_BYTES 4096

// snapshot.c's: the cache directory.
static bool snapshot_directory(char* path, uint64_t capacity);

typedef enum OracleMode
{
    ORACLE_MODE_OFF,
    ORACLE_MODE_USE,
    ORACLE_MODE_CHECK,
} OracleMode;

// The process's cache: its mode, its directory, the host's part of every key, and what the runs did.
typedef struct OracleCache OracleCache;
struct OracleCache
{
    bool initialized;
    OracleMode mode;
    char directory[BUSTER_ORACLE_PATH_BYTES];
    // SHA-256 of the package database's identity and the CPU's model and flags.
    uint8_t host[BUSTER_ORACLE_KEY_BYTES];
    // Runs that took an entry, ran, found their entry stale, and whose entry couldn't be written.
    uint64_t hits;
    uint64_t runs;
    uint64_t stale;
    uint64_t unwritten;
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

static void oracle_hash_raw(OracleHash* hash, const void* bytes, size_t length)
{
    const uint8_t* from = bytes;
    for (size_t i = 0; i < length; i += 1)
    {
        hash->block[hash->length % 64] = from[i];
        hash->length += 1;
        if (hash->length % 64 == 0)
        {
            oracle_sha256_block(hash->state, hash->block);
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

// A part of the key: its length, then its bytes.
static void oracle_hash_bytes(OracleHash* hash, const void* bytes, size_t length)
{
    uint8_t prefix[8];
    for (uint32_t i = 0; i < 8; i += 1)
    {
        prefix[i] = (uint8_t)((uint64_t)length >> (8 * i));
    }
    oracle_hash_raw(hash, prefix, sizeof(prefix));
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

// A file's bytes, as a part: false if it can't be read.
static bool oracle_hash_file(OracleHash* hash, const char* path)
{
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    struct stat status;
    bool result = fd >= 0 && fstat(fd, &status) == 0 && S_ISREG(status.st_mode);
    char* bytes = result ? malloc((size_t)status.st_size + 1) : 0;
    size_t length = 0;
    result = result && bytes != 0;
    while (result && length < (size_t)status.st_size)
    {
        const ssize_t got = read(fd, bytes + length, (size_t)status.st_size - length);
        result = got > 0 || (got < 0 && errno == EINTR);
        length += got > 0 ? (size_t)got : 0;
    }
    if (result)
    {
        oracle_hash_bytes(hash, bytes, length);
    }
    free(bytes);
    if (fd >= 0)
    {
        close(fd);
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
    while (!found && search != 0 && *search != 0)
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

// The variables of oracle_environment_names in `environment`, as parts: each name, then its value or nothing.
static void oracle_hash_environment(OracleHash* hash, char* const* environment)
{
    for (uint32_t n = 0; n < sizeof(oracle_environment_names) / sizeof(oracle_environment_names[0]); n += 1)
    {
        const char* name = oracle_environment_names[n];
        const size_t length = strlen(name);
        const char* value = 0;
        for (uint32_t i = 0; environment != 0 && environment[i] != 0 && value == 0; i += 1)
        {
            value = strncmp(environment[i], name, length) == 0 && environment[i][length] == '=' ? environment[i] + length + 1 : 0;
        }
        oracle_hash_text(hash, name);
        oracle_hash_bytes(hash, value != 0 ? "=" : "", value != 0 ? 1 : 0);
        oracle_hash_text(hash, value != 0 ? value : "");
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
// null or the directory can't be made.
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
}

// The process's cache, set up on its first use: BUSTER_ORACLE_CACHE's mode, in the cache directory.
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

// A key's start: the format, the purpose and the cache's host part.
static void oracle_hash_begin(OracleHash* hash, const OracleCache* cache, const char* purpose)
{
    oracle_sha256_begin(hash);
    oracle_hash_text(hash, "buster oracle 1");
    oracle_hash_text(hash, purpose);
    oracle_hash_bytes(hash, cache->host, BUSTER_ORACLE_KEY_BYTES);
}

// Whether the cache keeps runs: a caller whose cache doesn't needn't make keys.
static bool oracle_cache_active(const OracleCache* cache)
{
    return cache->mode != ORACLE_MODE_OFF;
}

static void oracle_entry_path(const OracleCache* cache, OracleKey key, char* path, size_t capacity)
{
    char name[2 * BUSTER_ORACLE_KEY_BYTES + 1];
    for (uint32_t i = 0; i < BUSTER_ORACLE_KEY_BYTES; i += 1)
    {
        snprintf(name + 2 * i, 3, "%02x", key.bytes[i]);
    }
    snprintf(path, capacity, "%s/%s", cache->directory, name);
}

static const char oracle_magic[8] = { 'B', 'U', 'S', 'T', 'O', 'R', 'C', '1' };

// An entry's header: the magic, the key, the exit status and the output's length.
typedef struct OracleEntryHeader OracleEntryHeader;
struct OracleEntryHeader
{
    char magic[8];
    uint8_t key[BUSTER_ORACLE_KEY_BYTES];
    int32_t status;
    uint32_t reserved;
    uint64_t length;
};

// The entry of `key`: true if it is there and whole, with its status and a copy of its output (malloc's, with a NUL
// after its bytes) that the caller frees.
static bool oracle_cache_read(const OracleCache* cache, OracleKey key, int* status, char** output, size_t* length)
{
    char path[BUSTER_ORACLE_PATH_BYTES + 80];
    oracle_entry_path(cache, key, path, sizeof(path));
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    struct stat file;
    OracleEntryHeader header;
    bool result = fd >= 0 && fstat(fd, &file) == 0 && S_ISREG(file.st_mode) && (uint64_t)file.st_size >= sizeof(header);
    result = result && pread(fd, &header, sizeof(header), 0) == (ssize_t)sizeof(header);
    result = result && memcmp(header.magic, oracle_magic, sizeof(oracle_magic)) == 0 && memcmp(header.key, key.bytes, sizeof(key.bytes)) == 0 &&
             header.length == (uint64_t)file.st_size - sizeof(header);
    char* bytes = result ? malloc(header.length + 1) : 0;
    result = result && bytes != 0;
    uint64_t read_bytes = 0;
    while (result && read_bytes < header.length)
    {
        const ssize_t got = pread(fd, bytes + read_bytes, header.length - read_bytes, (off_t)(sizeof(header) + read_bytes));
        result = got > 0 || (got < 0 && errno == EINTR);
        read_bytes += got > 0 ? (uint64_t)got : 0;
    }
    if (fd >= 0)
    {
        close(fd);
    }
    if (result)
    {
        bytes[header.length] = 0;
        *status = header.status;
        *output = bytes;
        *length = header.length;
    }
    else
    {
        free(bytes);
    }
    return result;
}

// Writes the entry of `key`, into a file of its own renamed over its name: false if it couldn't.
static bool oracle_cache_write(const OracleCache* cache, OracleKey key, int status, const char* output, size_t length)
{
    char path[BUSTER_ORACLE_PATH_BYTES + 80];
    char temporary[BUSTER_ORACLE_PATH_BYTES + 160];
    oracle_entry_path(cache, key, path, sizeof(path));
    static uint64_t written;
    written += 1;
    snprintf(temporary, sizeof(temporary), "%s.%ld.%llu.new", path, (long)getpid(), (unsigned long long)written);
    OracleEntryHeader header = {
        .status = status,
        .length = length,
    };
    memcpy(header.magic, oracle_magic, sizeof(oracle_magic));
    memcpy(header.key, key.bytes, sizeof(key.bytes));
    const int fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    bool result = fd >= 0 && write(fd, &header, sizeof(header)) == (ssize_t)sizeof(header);
    size_t done = 0;
    while (result && done < length)
    {
        const ssize_t put = write(fd, output + done, length - done);
        result = put > 0 || (put < 0 && errno == EINTR);
        done += put > 0 ? (size_t)put : 0;
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
    return result;
}

// Before a run: in `use` mode, its entry's status and output if it is there (true: don't run the tool). The output is
// malloc's, with a NUL after it.
static bool oracle_cache_take(OracleCache* cache, OracleKey key, int* status, char** output, size_t* length)
{
    const bool taken = cache->mode == ORACLE_MODE_USE && oracle_cache_read(cache, key, status, output, length);
    cache->hits += taken;
    return taken;
}

// After a run: in `check` mode, whether its entry, if it is there, holds this result (false: the entry is stale); in
// either mode, the result written as the entry. True when there is nothing to check.
static bool oracle_cache_keep(OracleCache* cache, OracleKey key, int status, const char* output, size_t length)
{
    bool current = true;
    if (cache->mode == ORACLE_MODE_CHECK)
    {
        int entry_status = 0;
        char* entry = 0;
        size_t entry_length = 0;
        if (oracle_cache_read(cache, key, &entry_status, &entry, &entry_length))
        {
            current = entry_status == status && entry_length == length && (length == 0 || memcmp(entry, output, length) == 0);
        }
        free(entry);
    }
    if (cache->mode != ORACLE_MODE_OFF)
    {
        cache->runs += 1;
        cache->stale += !current;
        cache->unwritten += !oracle_cache_write(cache, key, status, output, length);
    }
    return current;
}

// A line of what the cache did for a test, if it is on: hits, runs, stale entries, entries not written.
static void oracle_cache_report(const OracleCache* cache, const char* test)
{
    if (cache->mode != ORACLE_MODE_OFF)
    {
        printf("%s: oracle cache (%s, %s): %llu taken, %llu run, %llu stale, %llu not written\n", test, cache->mode == ORACLE_MODE_USE ? "use" : "check",
               cache->directory, (unsigned long long)cache->hits, (unsigned long long)cache->runs, (unsigned long long)cache->stale,
               (unsigned long long)cache->unwritten);
    }
}

#endif
