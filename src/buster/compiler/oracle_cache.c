// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The oracle cache (#542): the results of the clang and gcc runs that the tests compare with, kept between runs in the
// cache directory (snapshot_directory's: $XDG_CACHE_HOME/buster, or $HOME/.cache/buster) under oracle/, in a directory
// per key with an entry, a file, for each state of what the key's runs looked at (below). BUSTER_ORACLE_CACHE picks the
// mode:
//   - `use`: a run with an entry whose inputs are as the run finds them takes the entry's result rather than run the
//     tool, and a run that runs writes its entry;
//   - `check`: every run runs, and the entry it would have taken, found before it runs, must hold its result: one that
//     differs is stale, which fails the run (the 9700X's daily run). Every result is written;
//   - anything else, or unset: every run runs and nothing is kept (local runs).
//
// A test runs a tool through the cache as an OracleRun (oracle_run_start and oracle_run_finish, or oracle_run): the
// tool, its arguments, environment, standard input and working directory, and the files it writes. Its result is its
// wait status, its standard output and errors, and those files' bytes. Its key is SHA-256 (FIPS 180-4) of, each part
// after its length:
//   - its purpose, the caller's name for its kind of run;
//   - the tool: the first file of its name on PATH that may be run, as execvp finds it, and what stat gives of the file
//     it resolves to (device, inode, size, modification and change times, as the runtime snapshot's probes have them);
//     the same of the package database (pacman's, dpkg's or rpm's), which an update of any package changes, so that a
//     new clang, its libraries or its headers make new keys; and the CPU's model and flags, which -march=native reads;
//   - its arguments, its working directory, the names of the files it writes and the environment variables that change
//     a compiler's result (oracle_environment_names) with their values, each with its roots made alike (below);
//   - its time limit, and the bytes its standard input reads.
// The tool runs in a process group of its own, killed when it ends and past its time limit on the monotonic clock.
//
// A compiler's run (OracleRun.dependencies) is traced (--- Tracing ---): its entry holds every path its processes
// looked at, made, removed or renamed, those they found and those they didn't, and what getdents gives of each
// directory they listed or needed empty (oracle_listing), each as the run found it when it first looked
// (oracle_probe_look, before the run could change it): a file by SHA-256 of its bytes (oracle_file_digest), a path that
// isn't there as one, a directory as one, each with its permissions and extended attributes and its directory's kind,
// permissions and extended attributes. An entry is taken only if each of them is as it was, so that a header created
// where an #include, __has_include or #embed looked for one, in any directory, makes the run run again. A key keeps up
// to BUSTER_ORACLE_VARIANTS entries, one for each state its runs found (a test that writes a file anew for each of its
// runs, with other bytes and the same arguments, has one key for them all), and a run takes the first that is as it
// finds it. The files a run writes are among what it looked at, as they were before it (their bytes read at its first
// look at each, which a run that reads its output first depends on), and a file it changes after it read it isn't kept;
// nor is a run that does what the tracing can't follow (openat2's resolution flags, chroot, the mount calls, extended
// attributes, a file system's figures: oracle_unfollowed_calls). The package manager's files are left out
// (oracle_path_rule): the package database's identity stands for them. The kernel's (/proc and /sys) change with no
// file changing (a process's id in /proc/self/stat, the time in /proc/uptime, the inode and times stat gives of
// /proc/self): a run that checks for one of them (access, as LLVM checks for /proc/self/fd) is kept, but not one that
// stats, opens, lists or changes one, reads a link there (but its own process's links that name files, as LLVM reads
// its files' real paths), follows a process's link into the file system (/proc/self/cwd, /dev/fd/3) or uses another
// process's paths. /dev's other paths are kept, /dev/shm's files among them, but a run that opens a device (but
// /dev/null, /dev/zero and /dev/full, whose data never change, and /dev/tty, which a run, without a controlling
// terminal, can't open), a FIFO, a socket or, through any other path, a file of the kernel's isn't kept
// (oracle_opens_unseen). Its getrandom calls are answered by the cache (oracle_trace_random), the same each time the
// run runs, and it inherits no descriptor but the standard three (oracle_child). A compiler's run is kept only if its
// environment sets SOURCE_DATE_EPOCH (oracle_run_key), which the key has and its __DATE__ and __TIME__ follow, and
// clang's __TIMESTAMP__; the entries of a tool whose __TIMESTAMP__ doesn't (oracle_tool_epoch) have their files'
// modification times. A test that compares with compilers fixes the epoch (oracle_epoch_begin), and gives the files it
// writes for such a tool the epoch's time (oracle_epoch_file).
//
// What the tracing can't see, a tool's result must not depend on: the clocks (the vDSO's), its process and thread ids,
// its addresses and the kernel's random bytes for its start (AT_RANDOM), the processor's (rdrand, rdtsc), the machine's
// figures (uname, sysinfo, the CPUs it may run on), what other processes send it (sockets, shared memory), and the
// numbers the kernel gives the files it finds and makes, which a path's state doesn't have: their inode and device
// numbers, link and block counts, and times (but the modification times, for a tool whose entries have them), which
// stat gives beside a file's kind, size, owner and permissions, and fstat of any descriptor (a pipe's, a capture's),
// with no path to trace. They differ from one run to the next for every file a test or a run makes anew (clang's driver
// stats its source, cc's link the executable it wrote), so that a state that held them would never be found again. A
// compiler's result depends on none of them: with SOURCE_DATE_EPOCH, clang's and gcc's outputs are the same from run to
// run, which check mode verifies, and a run whose result depends on one fails there, its entry stale.
//
// A test makes its files in directories of mkdtemp's under $TMPDIR (or /tmp), whose names differ from one run to the
// next. A root is such a name in a run's strings: $TMPDIR/NAME, NAME a run of letters, digits, '.', '_' and '-' whose
// last 6 characters, or the 6 before its last '.', are letters and digits, as mkdtemp's and mkstemps's random ones are.
// In a key, each root's 6 characters are a placeholder for its place among the run's roots, by first appearance. An
// entry holds the roots it was made with, and when it is taken each is replaced, in the result and in the names of the
// files it lists, by the run's root of the same place, which is as long. So a result that names the run's files names
// the files of the run that takes it, byte for byte, in an object as in text.
//
// An entry is named by its variant, SHA-256 of what its run looked at with its roots made alike (oracle_entry_variant),
// and written into a file of its own beside its key's entries, then renamed over its name, so that a reader sees a
// whole entry or none, whatever tests write at once; past BUSTER_ORACLE_VARIANTS, the key's oldest go. The counts of
// what runs did are in shared memory, so that a test's parts in the child processes it forks after the cache is set up
// add to them.
//
// Test tooling outside the compile path: it hashes the files a run read and has no vector path.
#ifndef BUSTER_ORACLE_CACHE_INCLUDED
#define BUSTER_ORACLE_CACHE_INCLUDED

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <sys/uio.h>
#include <sys/vfs.h>
#include <sys/wait.h>
#include <sys/xattr.h>

extern char** environ;

#define BUSTER_ORACLE_KEY_BYTES 32
#define BUSTER_ORACLE_PATH_BYTES 4096
// The most roots a run may have: a run with more runs without the cache.
#define BUSTER_ORACLE_ROOTS 8
// A root's random characters, as many as mkdtemp makes.
#define BUSTER_ORACLE_RANDOM 6
// The most files a run may write.
#define BUSTER_ORACLE_FILES 8
// The most paths a traced run may look at: past them it isn't kept.
#define BUSTER_ORACLE_PROBES 65536
// The most runs a process traces at once: a compiler's run past them runs untraced, and isn't kept.
#define BUSTER_ORACLE_TRACED 64
// The most entries a key keeps, each for another state of what its runs looked at: writing one past them removes the
// key's oldest.
#define BUSTER_ORACLE_VARIANTS 32
// The files outside the runs' roots whose bytes' digests a process keeps (oracle_file_digest), a power of two.
#define BUSTER_ORACLE_MEMO 8192
// The most bytes the cache gives a traced run's getrandom call (oracle_trace_random), as many as the kernel always gives
// one call whole: a call for more goes to the kernel, and its run isn't kept.
#define BUSTER_ORACLE_RANDOM_BYTES 256
// The time oracle_epoch_begin fixes for a test, the preprocessor oracle's (c_preprocessor_oracle.c's
// BUSTER_PP_ORACLE_EPOCH).
#define BUSTER_ORACLE_EPOCH "1792454400"

// snapshot.c's: the cache directory.
static bool snapshot_directory(char* path, uint64_t capacity);

typedef enum OracleMode
{
    ORACLE_MODE_OFF,
    ORACLE_MODE_USE,
    ORACLE_MODE_CHECK,
} OracleMode;

// What runs through a cache did: took an entry, ran, found their entry stale, and ran without being kept (a compiler's
// run without SOURCE_DATE_EPOCH or whose tracing missed something, a file that couldn't be read, more roots than
// BUSTER_ORACLE_ROOTS, an entry that couldn't be written).
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

// The tool, as a part: its path on PATH (into `path`), the file it resolves to and what stat gives of it. False if it
// isn't on PATH.
static bool oracle_hash_tool(OracleHash* hash, const char* name, char* path, size_t capacity)
{
    char resolved[BUSTER_ORACLE_PATH_BYTES];
    const bool result = oracle_find_tool(name, path, capacity) && realpath(path, resolved) != 0;
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

// These standalone builtin commands have cheap compiler references: native measurements found USE's validation
// slower than fresh execution. Keep their CHECK path intact, and do not extend this policy by a command prefix.
static bool oracle_test_fresh_builtin(const char* command)
{
    static const char* const commands[] = {
        "test-check-builtins", "test-check-builtins-misc", "test-check-builtins-query", "test-check-builtins-bit-cast",
        "test-check-builtins-float", "test-check-builtins-x86", "test-check-builtins-vector", "test-check-builtins-math",
        "test-check-builtins-atomic", "test-check-builtins-atomic-rmw", "test-check-builtins-typed-overflow", "test-check-builtins-rounds",
    };
    bool selected = false;
    for (uint32_t c = 0; command != 0 && !selected && c < sizeof(commands) / sizeof(commands[0]); c += 1)
    {
        selected = strcmp(command, commands[c]) == 0;
    }
    return selected;
}

// A synchronous test command's USE-only scope. No cache is touched for an unselected command (cache == 0).
// Apply before the command starts children, and restore after it returns, including failure and usage rejection.
static OracleMode oracle_test_fresh_begin(OracleCache* cache)
{
    const OracleMode mode = cache != 0 ? cache->mode : ORACLE_MODE_OFF;
    if (cache != 0 && mode == ORACLE_MODE_USE)
    {
        cache->mode = ORACLE_MODE_OFF;
    }
    return mode;
}

static void oracle_test_fresh_end(OracleCache* cache, OracleMode mode)
{
    if (cache != 0)
    {
        cache->mode = mode;
    }
}

// SOURCE_DATE_EPOCH fixed (BUSTER_ORACLE_EPOCH) for a test that compares with compilers, where it isn't set: their
// __DATE__, __TIME__ and __TIMESTAMP__ and the test's own preprocessor's follow it, so that they agree whenever the test
// runs and the compilers' runs can be kept (oracle_run_key). Before the test starts threads or processes; true if it set
// it, for oracle_epoch_end, which unsets it again.
static bool oracle_epoch_begin(void)
{
    return getenv("SOURCE_DATE_EPOCH") == 0 && setenv("SOURCE_DATE_EPOCH", BUSTER_ORACLE_EPOCH, 1) == 0;
}

static void oracle_epoch_end(bool set)
{
    if (set)
    {
        unsetenv("SOURCE_DATE_EPOCH");
    }
}

// Gives a file a test wrote SOURCE_DATE_EPOCH's time, where the epoch is set: for the sources of a tool whose
// __TIMESTAMP__ is its file's modification time whatever the epoch (GCC's, oracle_tool_epoch), whose entries have their
// files' times, so that a run of a file written anew with the same bytes takes its entry, and the tool's __TIMESTAMP__
// is the epoch's too. False if the epoch is set and the file can't be given its time.
static bool oracle_epoch_file(const char* path)
{
    const char* epoch = getenv("SOURCE_DATE_EPOCH");
    const bool set = epoch != 0 && epoch[0] != 0 && strspn(epoch, "0123456789") == strlen(epoch);
    const time_t seconds = set ? (time_t)strtoll(epoch, 0, 10) : 0;
    const struct timespec times[2] = {
        { .tv_sec = seconds },
        { .tv_sec = seconds },
    };
    return !set || utimensat(AT_FDCWD, path, times, 0) == 0;
}

// A key's start: the format, the purpose and the cache's host part.
static void oracle_hash_begin(OracleHash* hash, const OracleCache* cache, const char* purpose)
{
    oracle_sha256_begin(hash);
    oracle_hash_text(hash, "buster oracle 9");
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

// A path a traced run looked at, as it found it when it first looked (oracle_probe_look): SHA-256 of what lstat and stat
// gave, or of a directory's names; whether it is a file, whose digest has its bytes (oracle_probe_digest), which the run
// must not change, and whether that file is in one of the run's roots, whose bytes are read for every digest; the file's
// identity (what stat gave of it: device, inode, size, modification and change times); and for one of the files the run
// writes, whose bytes it replaces, SHA-256 of the bytes it had, read at that look.
typedef struct OracleLook OracleLook;
struct OracleLook
{
    uint8_t state[BUSTER_ORACLE_KEY_BYTES];
    bool file;
    bool rooted;
    int64_t identity[7];
    bool captured;
    uint8_t content[BUSTER_ORACLE_KEY_BYTES];
};

// The paths a traced run looked at, each after its kind ('p' a path, 'l' a directory's names) with its first look, and
// an index of them by hash (open addressing over a power of two of slots, each a place + 1, or 0 for none).
typedef struct OracleLooks OracleLooks;
struct OracleLooks
{
    char** probes;
    OracleLook* looks;
    uint32_t count;
    uint32_t capacity;
    uint32_t* slots;
    uint32_t slot_count;
    bool failed;
};

// A run of a tool through a cache: oracle_run_start starts it or takes its entry, oracle_run_finish finishes it, and
// oracle_run_release frees its result. A process starts and finishes its runs on one thread, which answers its traced
// runs' calls while it waits for any of them (oracle_run_finish).
typedef struct OracleRun OracleRun;
struct OracleRun
{
    // The run, which the caller sets and keeps until the run is finished: the cache (null: the process's), the purpose,
    // the tool (a name PATH has, or a path) and its arguments with a null after them, its environment (null: this
    // process's), the file its standard input reads (null: /dev/null), its working directory (null: this process's),
    // the files it writes, whether it is a compiler's run, traced for every path it looks at (oracle_trace_answer) and
    // kept only with SOURCE_DATE_EPOCH set (oracle_run_key), and the seconds it may run (0: no limit), past which it is
    // killed with its process group and fails.
    OracleCache* cache;
    const char* purpose;
    char* const* arguments;
    char* const* environment;
    const char* input;
    const char* directory;
    const char* const* files;
    uint32_t file_count;
    bool dependencies;
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
    // The run's own state: its key and roots, its working directory, its tool's path, its process (the leader of its
    // group), a pidfd of it (or -1), when it started, the socket its child reports on (oracle_child) and whether the
    // child said its tool couldn't start, and its captures. Traced, also its filter's listener (or -1), whether no
    // process uses the filter any more, whether the tracing missed something (a call it can't follow, a path it can't
    // read), how many getrandom calls the cache answered (oracle_trace_random), and the paths the run looked at, each
    // with its first look. In check mode, whether it found the entry it would have taken (oracle_entry_find, before it
    // ran), and that entry's variant.
    bool keyed;
    OracleKey key;
    OracleRoots roots;
    char working[BUSTER_ORACLE_PATH_BYTES];
    char tool[BUSTER_ORACLE_PATH_BYTES];
    pid_t pid;
    int process;
    uint64_t started;
    int report;
    bool unstarted;
    int output_descriptor;
    int errors_descriptor;
    int listener;
    bool hung;
    bool untraced;
    uint64_t randoms;
    OracleLooks looks;
    bool checking;
    OracleKey checked;
};

// Nanoseconds on the monotonic clock.
static uint64_t oracle_now(void)
{
    struct timespec now = { 0 };
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000 + (uint64_t)now.tv_nsec;
}

// `path` as the run's tool sees it, from its working directory, into `resolved`: false if it doesn't fit.
static bool oracle_run_path(const OracleRun* run, const char* path, char* resolved, size_t capacity)
{
    const int length = path[0] == '/' ? snprintf(resolved, capacity, "%s", path) : snprintf(resolved, capacity, "%s/%s", run->working, path);
    return length > 0 && (size_t)length < capacity;
}

// Whether `epoch` has SOURCE_DATE_EPOCH's form, a run of decimal digits.
static bool oracle_epoch_valid(const char* epoch)
{
    return epoch != 0 && epoch[0] != 0 && strspn(epoch, "0123456789") == strlen(epoch);
}

// The run's key, with its roots found in its strings and its tool's path: false if its cache is off, its tool isn't on
// PATH, its input can't be read, it has more roots than BUSTER_ORACLE_ROOTS or more files than BUSTER_ORACLE_FILES, or it
// is a compiler's run (OracleRun.dependencies) that writes a dependency file of its own (an argument of -M's family),
// which isn't one of its files, or whose environment doesn't set SOURCE_DATE_EPOCH: its __DATE__ and __TIME__ would be
// when it ran, however its sources spell them (a token pasted from two halves among them), and clang's __TIMESTAMP__
// too. With the epoch, which the key has, they are the epoch's; a tool whose __TIMESTAMP__ doesn't follow it has the
// files' times in its entry (oracle_tool_epoch).
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
    result = result && (!run->dependencies || oracle_epoch_valid(oracle_variable(environment, "SOURCE_DATE_EPOCH")));
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
    result = result && oracle_hash_tool(&hash, run->arguments[0], run->tool, sizeof(run->tool));
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
        // Its time limit, as a run that passed one limit may not pass another: a run with a shorter one mustn't take
        // its result.
        const uint64_t limit = run->timeout;
        oracle_hash_bytes(&hash, &limit, sizeof(limit));
        // The input's bytes (none for /dev/null).
        OracleBuffer input = { 0 };
        result = run->input == 0 || strcmp(run->input, "/dev/null") == 0 || oracle_buffer_file(&input, run->input);
        oracle_hash_bytes(&hash, input.bytes, input.length);
        free(input.bytes);
    }
    run->key = oracle_hash_end(&hash);
    return result;
}

// SHA-256 of what getdents64 gives of a directory, into `digest`: each entry in the order the file system lists it,
// "." and ".." among them, with its inode, its offset, its type and its name, which a run that lists the directory can
// read (an inode a file made anew has, an order its names were made in), and the kind its lstat gives and a symbolic
// link's target. False if it can't be read.
static bool oracle_listing(const char* path, uint8_t* digest)
{
    DIR* directory = opendir(path);
    bool result = directory != 0;
    OracleHash hash;
    oracle_sha256_begin(&hash);
    struct dirent* entry = 0;
    while (result && (entry = readdir(directory)) != 0)
    {
        const uint64_t fields[3] = {
            (uint64_t)entry->d_ino,
            (uint64_t)entry->d_off,
            (uint64_t)entry->d_type,
        };
        struct stat status;
        result = fstatat(dirfd(directory), entry->d_name, &status, AT_SYMLINK_NOFOLLOW) == 0;
        const char kind = !result ? 0 : S_ISREG(status.st_mode) ? 'f' : S_ISDIR(status.st_mode) ? 'd' : S_ISLNK(status.st_mode) ? 'l' : 'o';
        char target[BUSTER_ORACLE_PATH_BYTES];
        const ssize_t target_length = kind == 'l' ? readlinkat(dirfd(directory), entry->d_name, target, sizeof(target)) : 0;
        result = result && target_length >= 0 && (size_t)target_length < sizeof(target);
        oracle_hash_bytes(&hash, fields, sizeof(fields));
        oracle_hash_text(&hash, entry->d_name);
        oracle_hash_bytes(&hash, &kind, 1);
        oracle_hash_bytes(&hash, target, target_length > 0 ? (size_t)target_length : 0);
    }
    if (directory != 0)
    {
        closedir(directory);
    }
    const OracleKey key = oracle_hash_end(&hash);
    memcpy(digest, key.bytes, sizeof(key.bytes));
    return result;
}

// A descriptor at 3 or above, as the sources of the child's dup2 calls must be where the caller has closed a standard
// one: `descriptor`, or a duplicate of it closed on exec, with it closed. -1 stays -1.
static int oracle_high(int descriptor)
{
    int result = descriptor;
    if (descriptor >= 0 && descriptor <= 2)
    {
        result = fcntl(descriptor, F_DUPFD_CLOEXEC, 3);
        close(descriptor);
    }
    return result;
}

// A file for a process's output: in memory (memfd_create), or an unnamed temporary file where the kernel has none;
// closed on exec, and never a standard descriptor. -1 if neither can be made.
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
    return oracle_high(result);
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

// Whether `path` is one of the files the run writes, as its working directory names them (oracle_run_path).
static bool oracle_run_output(const OracleRun* run, const char* path)
{
    bool result = false;
    for (uint32_t f = 0; f < run->file_count && !result; f += 1)
    {
        char output[BUSTER_ORACLE_PATH_BYTES];
        result = oracle_run_path(run, run->files[f], output, sizeof(output)) && strcmp(output, path) == 0;
    }
    return result;
}

static bool oracle_file_digest(int fd, const int64_t* identity, bool rooted, uint8_t* digest);

// The extended attributes of what `path` leads to, into `hash`: the error listing them gave (none, or ENOTSUP for a file
// system without them), then each name the file system lists, in its order, with its value or the error reading it
// gave. An access ACL is one (system.posix_acl_access), which access, execve and open read beside the mode's bits; a
// directory's default ACL another, which the files made in it take; a security label and a file's capabilities others.
static void oracle_hash_attributes(OracleHash* hash, const char* path)
{
    static char names[65536];
    static uint8_t value[65536];
    const ssize_t length = listxattr(path, names, sizeof(names));
    const int32_t error = length < 0 ? errno : 0;
    oracle_hash_bytes(hash, &error, sizeof(error));
    for (ssize_t at = 0; at < length; at += (ssize_t)strlen(names + at) + 1)
    {
        const ssize_t got = getxattr(path, names + at, value, sizeof(value));
        const int32_t got_error = got < 0 ? errno : 0;
        oracle_hash_text(hash, names + at);
        oracle_hash_bytes(hash, &got_error, sizeof(got_error));
        oracle_hash_bytes(hash, value, got > 0 ? (size_t)got : 0);
    }
}

// A probe as the run finds it now, into `look`. A path ('p'): what lstat gives (none and its errno, a symbolic link and
// its target, a directory, a file, another kind) and, through a link, what stat gives, with the permissions of what it
// leads to (its mode's bits, its owner and group) and its extended attributes (oracle_hash_attributes). A file is its
// bytes (oracle_probe_digest), whose identity the look keeps; rooted if it is in one of the run's roots (through a link,
// if its target is), which tests write anew for each run. A directory is only one, with its permissions, as other runs
// may add their files to it; another kind (a device node, a FIFO) is its kind and device number. A directory's names
// ('l'): oracle_listing's. False if a link can't be read.
static bool oracle_probe_look(const OracleRun* run, const char* probe, OracleLook* look)
{
    const char kind = probe[0];
    const char* path = probe + 1;
    *look = (OracleLook){ 0 };
    OracleHash hash;
    oracle_sha256_begin(&hash);
    oracle_hash_bytes(&hash, &kind, 1);
    bool result = true;
    if (kind == 'l')
    {
        uint8_t names[BUSTER_ORACLE_KEY_BYTES] = { 0 };
        const uint8_t listed = oracle_listing(path, names);
        oracle_hash_bytes(&hash, &listed, 1);
        oracle_hash_bytes(&hash, names, sizeof(names));
    }
    else
    {
        struct stat entry;
        const int32_t entry_error = lstat(path, &entry) == 0 ? 0 : errno;
        const bool linked = entry_error == 0 && S_ISLNK(entry.st_mode);
        char target[BUSTER_ORACLE_PATH_BYTES];
        const ssize_t target_length = linked ? readlink(path, target, sizeof(target)) : 0;
        result = target_length >= 0 && (size_t)target_length < sizeof(target);
        struct stat status = entry;
        const int32_t error = entry_error != 0 ? entry_error : linked && stat(path, &status) != 0 ? errno : 0;
        const char type = error != 0 ? '-' : S_ISREG(status.st_mode) ? 'f' : S_ISDIR(status.st_mode) ? 'd' : 'o';
        oracle_hash_bytes(&hash, &entry_error, sizeof(entry_error));
        oracle_hash_bytes(&hash, target, target_length > 0 ? (size_t)target_length : 0);
        oracle_hash_bytes(&hash, &error, sizeof(error));
        oracle_hash_bytes(&hash, &type, 1);
        char resolved[BUSTER_ORACLE_PATH_BYTES];
        look->rooted = type == 'f' && oracle_run_rooted(run, path) && (!linked || (realpath(path, resolved) != 0 && oracle_run_rooted(run, resolved)));
        // What access, execve and a directory's search read of whatever the path leads to: its mode's permission bits,
        // its owner and group and its ACLs, which a file's bytes and a directory's kind don't show.
        const uint64_t permissions[3] = {
            (uint64_t)(status.st_mode & 07777),
            (uint64_t)status.st_uid,
            (uint64_t)status.st_gid,
        };
        oracle_hash_bytes(&hash, permissions, type != '-' ? sizeof(permissions) : 0);
        if (type != '-')
        {
            oracle_hash_attributes(&hash, path);
        }
        // The directory the path is in, through links: its kind, permissions and extended attributes, which making,
        // removing and renaming the path read (a path that isn't there is one whose directory may or may not be), and
        // not its times or names, which other runs' files change.
        char directory[BUSTER_ORACLE_PATH_BYTES];
        const char* slash = strrchr(path, '/');
        const size_t parent = slash == 0 ? 0 : slash == path ? 1 : (size_t)(slash - path);
        memcpy(directory, path, parent);
        directory[parent] = 0;
        struct stat container;
        const int32_t container_error = parent == 0 ? ENOENT : stat(directory, &container) == 0 ? 0 : errno;
        const uint64_t contained[4] = {
            (uint64_t)(uint32_t)container_error,
            container_error == 0 ? (uint64_t)(container.st_mode & (S_IFMT | 07777)) : 0,
            container_error == 0 ? (uint64_t)container.st_uid : 0,
            container_error == 0 ? (uint64_t)container.st_gid : 0,
        };
        oracle_hash_bytes(&hash, contained, sizeof(contained));
        if (container_error == 0)
        {
            oracle_hash_attributes(&hash, directory);
        }
        if (type == 'f')
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
            memcpy(look->identity, identity, sizeof(identity));
            look->file = true;
            // One of the files the run writes, whose bytes it replaces: those it had, read now, while the call that
            // first named the file is stopped, before the kernel makes it, so that a run that reads its output before
            // it writes it (an #embed of it) runs again when those bytes change.
            if (oracle_run_output(run, path))
            {
                const int fd = open(path, O_RDONLY | O_CLOEXEC);
                struct stat opened;
                const bool same = fd >= 0 && fstat(fd, &opened) == 0 && (int64_t)opened.st_dev == identity[0] && (int64_t)opened.st_ino == identity[1] &&
                                  (int64_t)opened.st_size == identity[2] && (int64_t)opened.st_mtim.tv_sec == identity[3] &&
                                  (int64_t)opened.st_mtim.tv_nsec == identity[4] && (int64_t)opened.st_ctim.tv_sec == identity[5] &&
                                  (int64_t)opened.st_ctim.tv_nsec == identity[6];
                look->captured = same && oracle_file_digest(fd, identity, true, look->content);
                result = result && look->captured;
                if (fd >= 0)
                {
                    close(fd);
                }
            }
        }
        else if (type == 'o')
        {
            const uint64_t other[2] = { (uint64_t)(status.st_mode & S_IFMT), (uint64_t)status.st_rdev };
            oracle_hash_bytes(&hash, other, sizeof(other));
        }
    }
    const OracleKey key = oracle_hash_end(&hash);
    memcpy(look->state, key.bytes, sizeof(key.bytes));
    return result;
}

// A file outside the runs' roots whose bytes this process hashed (oracle_file_digest), by its identity.
typedef struct OracleMemo OracleMemo;
struct OracleMemo
{
    int64_t identity[7];
    uint8_t digest[BUSTER_ORACLE_KEY_BYTES];
    bool used;
};

// BUSTER_ORACLE_MEMO slots, each the last file hashed to it, made on first use.
static OracleMemo* oracle_memos;

// SHA-256 of the identity[2] bytes of the file open on `fd`, whose identity (oracle_probe_look's) is `identity`, into
// `digest`. A file in one of a run's roots (`rooted`) is read every time: a test rewrites its files in place from one
// run to the next, and the kernel's coarse clock can give a rewrite of the same size the same times. Another file's
// digest is the one this process made the last time it read the file, if its identity is the same: a write changes the
// size, or the modification and change times, and the files outside the roots (the repository's sources, the frozen
// corpora, a fetched corpus) aren't written while a test runs, which writes its own into its roots. False if the file
// can't be read.
static bool oracle_file_digest(int fd, const int64_t* identity, bool rooted, uint8_t* digest)
{
    if (!rooted && oracle_memos == 0)
    {
        oracle_memos = calloc(BUSTER_ORACLE_MEMO, sizeof(OracleMemo));
    }
    uint64_t slot = 0xcbf29ce484222325ull;
    for (uint32_t i = 0; i < 7; i += 1)
    {
        slot = (slot ^ (uint64_t)identity[i]) * 0x100000001b3ull;
    }
    OracleMemo* memo = !rooted && oracle_memos != 0 ? &oracle_memos[slot & (BUSTER_ORACLE_MEMO - 1)] : 0;
    const bool known = memo != 0 && memo->used && memcmp(memo->identity, identity, sizeof(memo->identity)) == 0;
    bool result = true;
    if (known)
    {
        memcpy(digest, memo->digest, BUSTER_ORACLE_KEY_BYTES);
    }
    else
    {
        OracleHash hash;
        oracle_sha256_begin(&hash);
        uint64_t done = 0;
        while (result && done < (uint64_t)identity[2])
        {
            uint8_t chunk[65536];
            const ssize_t got = read(fd, chunk, sizeof(chunk));
            result = got > 0 || (got < 0 && errno == EINTR);
            oracle_hash_raw(&hash, chunk, got > 0 ? (size_t)got : 0);
            done += got > 0 ? (uint64_t)got : 0;
        }
        const OracleKey key = oracle_hash_end(&hash);
        memcpy(digest, key.bytes, sizeof(key.bytes));
        if (result && memo != 0)
        {
            memcpy(memo->identity, identity, sizeof(memo->identity));
            memcpy(memo->digest, key.bytes, sizeof(key.bytes));
            memo->used = true;
        }
    }
    return result;
}

// A probe's digest from its look, into `digest`: the look's state and, for a file, its size and SHA-256 of its bytes
// (oracle_file_digest, or the look's for a file the run writes), with its modification time if `times`. False if the
// file isn't the one the look found (the run changed it, as a run that writes a file it read does) or can't be read.
static bool oracle_probe_digest(const char* probe, const OracleLook* look, bool times, uint8_t* digest)
{
    OracleHash hash;
    oracle_sha256_begin(&hash);
    oracle_hash_bytes(&hash, look->state, sizeof(look->state));
    bool result = true;
    if (look->file && look->captured)
    {
        oracle_hash_length(&hash, (uint64_t)look->identity[2]);
        oracle_hash_bytes(&hash, look->content, sizeof(look->content));
        oracle_hash_bytes(&hash, look->identity + 3, times ? 2 * sizeof(int64_t) : 0);
    }
    else if (look->file)
    {
        const int fd = open(probe + 1, O_RDONLY | O_CLOEXEC);
        struct stat status;
        result = fd >= 0 && fstat(fd, &status) == 0;
        const int64_t identity[7] = {
            result ? (int64_t)status.st_dev : 0,
            result ? (int64_t)status.st_ino : 0,
            result ? (int64_t)status.st_size : 0,
            result ? (int64_t)status.st_mtim.tv_sec : 0,
            result ? (int64_t)status.st_mtim.tv_nsec : 0,
            result ? (int64_t)status.st_ctim.tv_sec : 0,
            result ? (int64_t)status.st_ctim.tv_nsec : 0,
        };
        result = result && memcmp(identity, look->identity, sizeof(identity)) == 0;
        uint8_t bytes[BUSTER_ORACLE_KEY_BYTES] = { 0 };
        result = result && oracle_file_digest(fd, identity, look->rooted, bytes);
        if (fd >= 0)
        {
            close(fd);
        }
        oracle_hash_length(&hash, (uint64_t)identity[2]);
        oracle_hash_bytes(&hash, bytes, sizeof(bytes));
        oracle_hash_bytes(&hash, look->identity + 3, times ? 2 * sizeof(int64_t) : 0);
    }
    const OracleKey key = oracle_hash_end(&hash);
    memcpy(digest, key.bytes, sizeof(key.bytes));
    return result;
}

// FNV-1a of a probe, for its slot.
static uint64_t oracle_probe_hash(const char* probe)
{
    uint64_t hash = 0xcbf29ce484222325ull;
    for (const char* at = probe; *at != 0; at += 1)
    {
        hash = (hash ^ (uint8_t)*at) * 0x100000001b3ull;
    }
    return hash;
}

// Keeps the probe's first look, taken now (as its call stops, before the kernel makes it): a probe the run looked at
// before keeps the look it had then, which is what the run's tool found of the files before the run touched them (a
// file the run makes is one that wasn't there). Past BUSTER_ORACLE_PROBES, or a list that can't grow, it makes the run
// untraced.
static void oracle_looks_add(OracleRun* run, const char* probe)
{
    OracleLooks* looks = &run->looks;
    if (!looks->failed && 2 * ((uint64_t)looks->count + 1) > looks->slot_count)
    {
        // Twice the slots, the probes placed again.
        const uint32_t slot_count = looks->slot_count != 0 ? 2 * looks->slot_count : 256;
        uint32_t* slots = slot_count > looks->slot_count ? calloc(slot_count, sizeof(uint32_t)) : 0;
        looks->failed = slots == 0;
        for (uint32_t i = 0; slots != 0 && i < looks->count; i += 1)
        {
            uint32_t slot = (uint32_t)oracle_probe_hash(looks->probes[i]) & (slot_count - 1);
            while (slots[slot] != 0)
            {
                slot = (slot + 1) & (slot_count - 1);
            }
            slots[slot] = i + 1;
        }
        if (slots != 0)
        {
            free(looks->slots);
            looks->slots = slots;
            looks->slot_count = slot_count;
        }
    }
    uint32_t slot = looks->failed ? 0 : (uint32_t)oracle_probe_hash(probe) & (looks->slot_count - 1);
    while (!looks->failed && looks->slots[slot] != 0 && strcmp(looks->probes[looks->slots[slot] - 1], probe) != 0)
    {
        slot = (slot + 1) & (looks->slot_count - 1);
    }
    const bool added = !looks->failed && looks->slots[slot] == 0;
    if (added && looks->count == looks->capacity)
    {
        const uint32_t capacity = looks->capacity != 0 ? 2 * looks->capacity : 64;
        char** probes = realloc(looks->probes, sizeof(char*) * capacity);
        looks->probes = probes != 0 ? probes : looks->probes;
        OracleLook* grown = probes != 0 ? realloc(looks->looks, sizeof(OracleLook) * capacity) : 0;
        looks->looks = grown != 0 ? grown : looks->looks;
        looks->capacity = grown != 0 ? capacity : looks->capacity;
        looks->failed = grown == 0;
    }
    char* copy = added && !looks->failed ? strdup(probe) : 0;
    looks->failed |= added && copy == 0;
    if (copy != 0)
    {
        looks->failed |= !oracle_probe_look(run, copy, &looks->looks[looks->count]);
        looks->probes[looks->count] = copy;
        looks->count += 1;
        looks->slots[slot] = looks->count;
    }
    looks->failed |= looks->count > BUSTER_ORACLE_PROBES;
    run->untraced |= looks->failed;
}

static void oracle_looks_release(OracleLooks* looks)
{
    for (uint32_t i = 0; i < looks->count; i += 1)
    {
        free(looks->probes[i]);
    }
    free(looks->probes);
    free(looks->looks);
    free(looks->slots);
    *looks = (OracleLooks){ 0 };
}

// --- Tracing ---
//
// A compiler's run (OracleRun.dependencies) runs under a seccomp filter that stops each call of its processes that looks
// at a path, lists a directory, opens a file, or makes, removes, renames or changes one, for the cache to read the
// paths from the process (process_vm_readv, as its parent may) and let the call through to the kernel
// (SECCOMP_USER_NOTIF_FLAG_CONTINUE). What the run looked at is then every path it found or didn't find: the headers it
// read, each place an #include, __has_include or #embed looked for one and found none, in whatever directory, the run's
// own or one other runs share, and from whatever header, so that a file created there later makes the run run again;
// and each path whose making or removing its results depend on (an mkdir that finds its path there, or its directory
// not there yet), with its directory. A compiler doesn't list directories to find its headers, but a directory a run
// lists is part of what it looked at by its names. What the tracing can't follow makes the run unkept
// (oracle_unfollowed_calls). Its getrandom calls the cache answers itself (oracle_answered_calls).

#ifndef SYS_openat2
#define SYS_openat2 437
#endif
#ifndef SYS_faccessat2
#define SYS_faccessat2 439
#endif
#ifndef SYS_io_uring_setup
#define SYS_io_uring_setup 425
#endif
#ifndef SYS_open_tree
#define SYS_open_tree 428
#endif
#ifndef SYS_move_mount
#define SYS_move_mount 429
#endif
#ifndef SYS_fsopen
#define SYS_fsopen 430
#endif
#ifndef SYS_fsconfig
#define SYS_fsconfig 431
#endif
#ifndef SYS_fsmount
#define SYS_fsmount 432
#endif
#ifndef SYS_fspick
#define SYS_fspick 433
#endif
#ifndef SYS_mount_setattr
#define SYS_mount_setattr 442
#endif
#ifndef SYS_fchmodat2
#define SYS_fchmodat2 452
#endif
#ifndef SYS_statmount
#define SYS_statmount 457
#endif
#ifndef SYS_listmount
#define SYS_listmount 458
#endif
#ifndef SYS_setxattrat
#define SYS_setxattrat 463
#endif
#ifndef SYS_getxattrat
#define SYS_getxattrat 464
#endif
#ifndef SYS_listxattrat
#define SYS_listxattrat 465
#endif
#ifndef SYS_removexattrat
#define SYS_removexattrat 466
#endif
#ifndef SYS_open_tree_attr
#define SYS_open_tree_attr 467
#endif
#ifndef SECCOMP_USER_NOTIF_FLAG_CONTINUE
#define SECCOMP_USER_NOTIF_FLAG_CONTINUE 1
#endif
#ifndef AT_EMPTY_PATH
#define AT_EMPTY_PATH 0x1000
#endif
#ifndef AT_SYMLINK_FOLLOW
#define AT_SYMLINK_FOLLOW 0x400
#endif
#ifndef AT_REMOVEDIR
#define AT_REMOVEDIR 0x200
#endif
#ifndef SYS_close_range
#define SYS_close_range 436
#endif
#ifndef CLOSE_RANGE_CLOEXEC
#define CLOSE_RANGE_CLOEXEC (1u << 2)
#endif
#ifndef GRND_INSECURE
#define GRND_INSECURE 0x0004
#endif

// What a traced call does at its paths (oracle_path_rule): checks for one (whether it is there and what access allows
// of it: access's and chdir's answer), looks at one (what stat gives: its kind, size, owner and permissions, which a
// path's look has, and the numbers the kernel gives it, which it doesn't), opens one (a file's bytes, a directory's
// names, a device's data), reads one (a link's target, a directory's names, a program's bytes), or makes, removes,
// renames or changes them.
typedef enum OracleUse
{
    ORACLE_USE_CHECK,
    ORACLE_USE_LOOK,
    ORACLE_USE_OPEN,
    ORACLE_USE_READ,
    ORACLE_USE_CHANGE,
} OracleUse;

// How the tracing reads a traced call's paths (oracle_trace_answer): up to two, each from the argument that holds its
// name (-1: none) and the one that holds its directory's descriptor (-1: the working directory), or the descriptor's
// own where it names none (a listing's, utimensat's null name, "" with AT_EMPTY_PATH); the argument with the call's AT_
// flags (-1: none); whether it follows a symbolic link at its first path's end (AT_SYMLINK_NOFOLLOW stops it and
// AT_SYMLINK_FOLLOW starts it; a second path is one it makes or replaces, which it doesn't follow); whether it lists
// its path's directory (getdents); whether its last path's directory's names count too, which must be none for it to
// go (rmdir's, unlinkat's with AT_REMOVEDIR, and rename's target's); and what it does at them (OracleUse).
typedef struct OracleCall OracleCall;
struct OracleCall
{
    uint32_t number;
    int8_t paths[2];
    int8_t directories[2];
    int8_t flags;
    bool follows;
    bool lists;
    bool empties;
    uint8_t use;
};

static const OracleCall oracle_calls[] = {
    // Looking at a path, or at a descriptor's directory's names.
    { SYS_open, { 0, -1 }, { -1, -1 }, -1, true, false, false, ORACLE_USE_OPEN },
    { SYS_creat, { 0, -1 }, { -1, -1 }, -1, true, false, false, ORACLE_USE_OPEN },
    { SYS_openat, { 1, -1 }, { 0, -1 }, -1, true, false, false, ORACLE_USE_OPEN },
    { SYS_openat2, { 1, -1 }, { 0, -1 }, -1, true, false, false, ORACLE_USE_OPEN },
    { SYS_stat, { 0, -1 }, { -1, -1 }, -1, true, false, false, ORACLE_USE_LOOK },
    { SYS_lstat, { 0, -1 }, { -1, -1 }, -1, false, false, false, ORACLE_USE_LOOK },
    { SYS_newfstatat, { 1, -1 }, { 0, -1 }, 3, true, false, false, ORACLE_USE_LOOK },
    { SYS_statx, { 1, -1 }, { 0, -1 }, 2, true, false, false, ORACLE_USE_LOOK },
    { SYS_access, { 0, -1 }, { -1, -1 }, -1, true, false, false, ORACLE_USE_CHECK },
    { SYS_faccessat, { 1, -1 }, { 0, -1 }, -1, true, false, false, ORACLE_USE_CHECK },
    { SYS_faccessat2, { 1, -1 }, { 0, -1 }, 3, true, false, false, ORACLE_USE_CHECK },
    { SYS_readlink, { 0, -1 }, { -1, -1 }, -1, false, false, false, ORACLE_USE_READ },
    { SYS_readlinkat, { 1, -1 }, { 0, -1 }, -1, false, false, false, ORACLE_USE_READ },
    { SYS_execve, { 0, -1 }, { -1, -1 }, -1, true, false, false, ORACLE_USE_READ },
    { SYS_execveat, { 1, -1 }, { 0, -1 }, 4, true, false, false, ORACLE_USE_READ },
    { SYS_chdir, { 0, -1 }, { -1, -1 }, -1, true, false, false, ORACLE_USE_CHECK },
    { SYS_getdents, { -1, -1 }, { 0, -1 }, -1, true, true, false, ORACLE_USE_READ },
    { SYS_getdents64, { -1, -1 }, { 0, -1 }, -1, true, true, false, ORACLE_USE_READ },
    // Making, removing, renaming and changing a path, whose results read what is there and its directory.
    { SYS_mkdir, { 0, -1 }, { -1, -1 }, -1, false, false, false, ORACLE_USE_CHANGE },
    { SYS_mkdirat, { 1, -1 }, { 0, -1 }, -1, false, false, false, ORACLE_USE_CHANGE },
    { SYS_mknod, { 0, -1 }, { -1, -1 }, -1, false, false, false, ORACLE_USE_CHANGE },
    { SYS_mknodat, { 1, -1 }, { 0, -1 }, -1, false, false, false, ORACLE_USE_CHANGE },
    { SYS_rmdir, { 0, -1 }, { -1, -1 }, -1, false, false, true, ORACLE_USE_CHANGE },
    { SYS_unlink, { 0, -1 }, { -1, -1 }, -1, false, false, false, ORACLE_USE_CHANGE },
    { SYS_unlinkat, { 1, -1 }, { 0, -1 }, 2, false, false, false, ORACLE_USE_CHANGE },
    { SYS_rename, { 0, 1 }, { -1, -1 }, -1, false, false, true, ORACLE_USE_CHANGE },
    { SYS_renameat, { 1, 3 }, { 0, 2 }, -1, false, false, true, ORACLE_USE_CHANGE },
    { SYS_renameat2, { 1, 3 }, { 0, 2 }, -1, false, false, true, ORACLE_USE_CHANGE },
    { SYS_link, { 0, 1 }, { -1, -1 }, -1, false, false, false, ORACLE_USE_CHANGE },
    { SYS_linkat, { 1, 3 }, { 0, 2 }, 4, false, false, false, ORACLE_USE_CHANGE },
    { SYS_symlink, { 1, -1 }, { -1, -1 }, -1, false, false, false, ORACLE_USE_CHANGE },
    { SYS_symlinkat, { 2, -1 }, { 1, -1 }, -1, false, false, false, ORACLE_USE_CHANGE },
    { SYS_truncate, { 0, -1 }, { -1, -1 }, -1, true, false, false, ORACLE_USE_CHANGE },
    { SYS_chmod, { 0, -1 }, { -1, -1 }, -1, true, false, false, ORACLE_USE_CHANGE },
    { SYS_fchmodat, { 1, -1 }, { 0, -1 }, -1, true, false, false, ORACLE_USE_CHANGE },
    { SYS_fchmodat2, { 1, -1 }, { 0, -1 }, 3, true, false, false, ORACLE_USE_CHANGE },
    { SYS_chown, { 0, -1 }, { -1, -1 }, -1, true, false, false, ORACLE_USE_CHANGE },
    { SYS_lchown, { 0, -1 }, { -1, -1 }, -1, false, false, false, ORACLE_USE_CHANGE },
    { SYS_fchownat, { 1, -1 }, { 0, -1 }, 4, true, false, false, ORACLE_USE_CHANGE },
    { SYS_utime, { 0, -1 }, { -1, -1 }, -1, true, false, false, ORACLE_USE_CHANGE },
    { SYS_utimes, { 0, -1 }, { -1, -1 }, -1, true, false, false, ORACLE_USE_CHANGE },
    { SYS_utimensat, { 1, -1 }, { 0, -1 }, 3, true, false, false, ORACLE_USE_CHANGE },
    { SYS_futimesat, { 1, -1 }, { 0, -1 }, -1, true, false, false, ORACLE_USE_CHANGE },
};

// The calls the filter stops that make their run unkept, as the tracing can't follow them: those that reach files it
// can't name (io_uring's, files opened by handle), that change how their process resolves paths (chroot, pivot_root,
// the mount calls, unshare and setns), that read or change extended attributes (a path's state has them as the run
// first found them, not as it reads them), that list the mounts, and statfs, whose answer is its file system's
// figures (its free blocks and files, which any file written changes), which no path's state has (libselinux's start
// makes one, of /sys/fs/selinux, in a program linked with it, which also reads /proc/filesystems). Calls of another
// architecture, and x32's, are stopped and do the same.
static const uint32_t oracle_unfollowed_calls[] = {
    SYS_statfs,
    SYS_io_uring_setup,
    SYS_name_to_handle_at,
    SYS_open_by_handle_at,
    SYS_chroot,
    SYS_pivot_root,
    SYS_mount,
    SYS_umount2,
    SYS_unshare,
    SYS_setns,
    SYS_open_tree,
    SYS_open_tree_attr,
    SYS_move_mount,
    SYS_fsopen,
    SYS_fsconfig,
    SYS_fsmount,
    SYS_fspick,
    SYS_mount_setattr,
    SYS_statmount,
    SYS_listmount,
    SYS_getxattr,
    SYS_lgetxattr,
    SYS_fgetxattr,
    SYS_listxattr,
    SYS_llistxattr,
    SYS_flistxattr,
    SYS_getxattrat,
    SYS_listxattrat,
    SYS_setxattr,
    SYS_lsetxattr,
    SYS_fsetxattr,
    SYS_setxattrat,
    SYS_removexattr,
    SYS_lremovexattr,
    SYS_fremovexattr,
    SYS_removexattrat,
};

// The calls the filter stops that the cache answers itself, rather than let them through: getrandom, whose bytes it
// gives (oracle_trace_random).
static const uint32_t oracle_answered_calls[] = {
    SYS_getrandom,
};

#define BUSTER_ORACLE_CALLS (sizeof(oracle_calls) / sizeof(oracle_calls[0]))
#define BUSTER_ORACLE_UNFOLLOWED_CALLS (sizeof(oracle_unfollowed_calls) / sizeof(oracle_unfollowed_calls[0]))
#define BUSTER_ORACLE_TRACED_CALLS (BUSTER_ORACLE_CALLS + BUSTER_ORACLE_UNFOLLOWED_CALLS + sizeof(oracle_answered_calls) / sizeof(oracle_answered_calls[0]))
#define BUSTER_ORACLE_FILTER (BUSTER_ORACLE_TRACED_CALLS + 6)
// The filter's jumps count the instructions they skip in a byte.
_Static_assert(BUSTER_ORACLE_TRACED_CALLS + 3 <= 255, "oracle cache: too many traced calls for the filter's jumps");

// The filter's program into `filter` (BUSTER_ORACLE_FILTER instructions): the architecture and x32's bit checked, then
// each of oracle_calls, oracle_unfollowed_calls and oracle_answered_calls, stopped for the listener
// (SECCOMP_RET_USER_NOTIF), and any other call let through.
static void oracle_trace_filter(struct sock_filter* filter, struct sock_fprog* program)
{
    const uint8_t calls = (uint8_t)BUSTER_ORACLE_TRACED_CALLS;
    // Each jump counts the instructions it skips; the last instruction stops the call.
    filter[0] = (struct sock_filter)BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, arch));
    filter[1] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_X86_64, 0, (uint8_t)(calls + 3));
    filter[2] = (struct sock_filter)BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr));
    filter[3] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JGE | BPF_K, 0x40000000, (uint8_t)(calls + 1), 0);
    for (uint8_t c = 0; c < calls; c += 1)
    {
        const uint32_t number = c < BUSTER_ORACLE_CALLS                                    ? oracle_calls[c].number
                                : c < BUSTER_ORACLE_CALLS + BUSTER_ORACLE_UNFOLLOWED_CALLS ? oracle_unfollowed_calls[c - BUSTER_ORACLE_CALLS]
                                                                                           : oracle_answered_calls[c - BUSTER_ORACLE_CALLS - BUSTER_ORACLE_UNFOLLOWED_CALLS];
        filter[4 + c] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, number, (uint8_t)(calls - c), 0);
    }
    filter[4 + calls] = (struct sock_filter)BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW);
    filter[5 + calls] = (struct sock_filter)BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_USER_NOTIF);
    *program = (struct sock_fprog){
        .len = (unsigned short)BUSTER_ORACLE_FILTER,
        .filter = filter,
    };
}

// The runs this process traces until they finish, whose calls oracle_run_finish answers while it waits for any run; a
// child forked from a process that traced some finds them its parent's (oracle_traced_owner) and has none.
static OracleRun* oracle_traced[BUSTER_ORACLE_TRACED];
static uint32_t oracle_traced_count;
static pid_t oracle_traced_owner;

static void oracle_traced_own(void)
{
    const pid_t self = getpid();
    if (oracle_traced_owner != self)
    {
        for (uint32_t t = 0; t < oracle_traced_count; t += 1)
        {
            // The parent's listener, this process's copy of it, which nothing here answers.
            close(oracle_traced[t]->listener);
            oracle_traced[t]->listener = -1;
        }
        oracle_traced_count = 0;
        oracle_traced_owner = self;
    }
}

static void oracle_traced_remove(const OracleRun* run)
{
    oracle_traced_own();
    for (uint32_t t = 0; t < oracle_traced_count; t += 1)
    {
        if (oracle_traced[t] == run)
        {
            oracle_traced_count -= 1;
            oracle_traced[t] = oracle_traced[oracle_traced_count];
        }
    }
}

// The NUL-terminated string at `address` in the process `pid`, into `text` (at most `capacity` bytes with its NUL),
// read up to a page's end at a time, so that a string before an unmapped page is read: false if it can't be read or
// doesn't fit.
static bool oracle_trace_string(pid_t pid, uint64_t address, char* text, size_t capacity)
{
    size_t done = 0;
    bool ended = false;
    bool result = address != 0;
    while (result && !ended)
    {
        const size_t page = 4096 - (size_t)((address + done) % 4096);
        const size_t want = page < capacity - done ? page : capacity - done;
        struct iovec local = {
            .iov_base = text + done,
            .iov_len = want,
        };
        struct iovec remote = {
            .iov_base = (void*)(uintptr_t)(address + done),
            .iov_len = want,
        };
        const ssize_t got = want != 0 ? (ssize_t)syscall(SYS_process_vm_readv, pid, &local, 1ul, &remote, 1ul, 0ul) : -1;
        result = got > 0;
        ended = result && memchr(text + done, 0, (size_t)got) != 0;
        done += result ? (size_t)got : 0;
    }
    return result;
}

// The path the process's descriptor names (AT_FDCWD: its working directory), into `path`: 1 if it is one, 0 if the
// descriptor is something else (a pipe, a socket), and -1 if it can't be read.
static int oracle_trace_descriptor(pid_t pid, int descriptor, char* path, size_t capacity)
{
    char link[64];
    if (descriptor == AT_FDCWD)
    {
        snprintf(link, sizeof(link), "/proc/%ld/cwd", (long)pid);
    }
    else
    {
        snprintf(link, sizeof(link), "/proc/%ld/fd/%d", (long)pid, descriptor);
    }
    const ssize_t length = readlink(link, path, capacity - 1);
    const int result = length <= 0 || (size_t)length >= capacity - 1 ? -1 : path[0] == '/' ? 1 : 0;
    path[result == 1 ? length : 0] = 0;
    return result;
}

// What the cache makes of a path a run looked at (oracle_path_rule).
typedef enum OraclePath
{
    // Among what the run looked at.
    ORACLE_PATH_KEPT,
    // Left out of it.
    ORACLE_PATH_LEFT_OUT,
    // A path the cache can't look at as the run's process sees it: the run isn't kept.
    ORACLE_PATH_REFUSED,
} OraclePath;

// Whether `path` is `directory` or is in it, with what follows the directory ("" or "/...") into `rest`.
static bool oracle_path_under(const char* path, const char* directory, const char** rest)
{
    const size_t length = strlen(directory);
    const bool under = strncmp(path, directory, length) == 0 && (path[length] == 0 || path[length] == '/');
    *rest = under ? path + length : "";
    return under;
}

// What follows a process's directory in `rest`, which follows /proc: "/PID", "/self" or "/thread-self", then a task's
// "/task/TID" if it has one, with whether that process is the one `caller` (a thread's id, as a notification has it)
// is in, into `own`: self and thread-self are, and a PID is if it is `caller`'s or that of the process whose tasks
// `caller` is among. Null if `rest` names no process.
static const char* oracle_process_part(const char* rest, pid_t caller, bool* own)
{
    const char* name = rest[0] == '/' ? rest + 1 : "";
    const size_t digits = strspn(name, "0123456789");
    const size_t length = digits != 0 ? digits : strncmp(name, "thread-self", 11) == 0 ? 11 : strncmp(name, "self", 4) == 0 ? 4 : 0;
    const char* result = length != 0 && (name[length] == 0 || name[length] == '/') ? name + length : 0;
    // A PID longer than any is no process's.
    const long process = result != 0 && digits != 0 && digits < 10 ? strtol(name, 0, 10) : -1;
    char task[64];
    snprintf(task, sizeof(task), "/proc/%ld/task/%ld", process, (long)caller);
    struct stat status;
    *own = result != 0 && (digits == 0 || process == (long)caller || (process > 0 && stat(task, &status) == 0));
    const size_t tasks = result != 0 && strncmp(result, "/task/", 6) == 0 ? strspn(result + 6, "0123456789") : 0;
    result = tasks != 0 && (result[6 + tasks] == 0 || result[6 + tasks] == '/') ? result + 6 + tasks : result;
    return result;
}

// What the cache makes of `link` (a process's path after its directory, as "/fd/3" or "/cwd/x.h"), at which the run's
// call makes `use`, following it to its end if `followed`; `own` if the process is the caller's own. Another process's
// paths are that process's state, and are refused whatever the use. Of the caller's own, what stat gives of any (the
// numbers and times the kernel gives /proc/self, a link, or what a standard descriptor leads to: a capture, made anew
// for each run) and a change are refused. Its links into the file system (cwd, root, exe, fd/N and map_files/RANGE)
// lead to what it sees, which the cache, another process, can't look at: a path past one, or a call that follows one
// but its exe and standard three descriptors (the tool or what it executed, which the tracing saw, and the key's input
// and the run's captures), is refused. A link read names what the process opened: left out (as LLVM reads its files'
// real paths), but a descriptor's that names a pipe's or a socket's inode ("pipe:[N]", read here as `caller` sees it),
// which another run's differs from. What else the kernel has of it (its status, its descriptors' positions, which
// process /proc/self is) changes from one run to the next with no file changing: a check for it (access, as LLVM
// checks for /proc/self/fd) is left out, and any other use refused.
static OraclePath oracle_process_link(const char* link, bool followed, OracleUse use, bool own, pid_t caller)
{
    const char* name = link[0] == '/' ? link + 1 : "";
    const size_t length = strcspn(name, "/");
    const char* after = name + length;
    const bool directory = (length == 3 && strncmp(name, "cwd", 3) == 0) || (length == 4 && strncmp(name, "root", 4) == 0);
    const bool executable = length == 3 && strncmp(name, "exe", 3) == 0;
    const bool descriptors = length == 2 && strncmp(name, "fd", 2) == 0;
    const bool mappings = length == 9 && strncmp(name, "map_files", 9) == 0;
    // A descriptor's or a mapping's own link: "/N" after its directory, and nothing after that.
    const char* entry = after[0] == '/' ? after + 1 : "";
    const bool single = entry[0] != 0 && strchr(entry, '/') == 0;
    const bool standard = descriptors && single && (strcmp(entry, "0") == 0 || strcmp(entry, "1") == 0 || strcmp(entry, "2") == 0);
    const bool linked = ((directory || executable) && after[0] == 0) || ((descriptors || mappings) && single);
    const bool past = (directory || executable) ? after[0] != 0 : (descriptors || mappings) && after[0] != 0 && !single;
    char descriptor[64];
    char target[BUSTER_ORACLE_PATH_BYTES];
    snprintf(descriptor, sizeof(descriptor), "/proc/%ld/fd/%s", (long)caller, entry);
    const bool inode = descriptors && single && own && !followed && use == ORACLE_USE_READ && readlink(descriptor, target, sizeof(target)) > 0 && target[0] != '/';
    const bool refused = !own || past || inode || use == ORACLE_USE_LOOK || use == ORACLE_USE_CHANGE || (linked && followed && !(executable || standard)) ||
                         (!linked && use != ORACLE_USE_CHECK);
    return refused ? ORACLE_PATH_REFUSED : ORACLE_PATH_LEFT_OUT;
}

// What the cache makes of `path`, at which a call of `caller` (a thread's id) makes `use`, following a symbolic link at
// its end if `followed` (all but lstat, readlink, readlinkat, and newfstatat and statx with AT_SYMLINK_NOFOLLOW):
//   - a path in one of the run's roots is kept, wherever $TMPDIR is (/dev/shm, say);
//   - the package manager's files (under /usr but /usr/local, and /bin, /sbin, /lib, /lib32, /lib64 and /libx32, which
//     merged-usr systems link into /usr) are left out: the package database's identity in every key stands for them;
//   - the kernel's (/proc and /sys), which describe the processes and the machine and change with no file changing,
//     are left out where a call checks for them (access, chdir) and refused where it stats, opens, reads or changes
//     them; and a process's paths, which /dev/fd, /dev/stdin, /dev/stdout and /dev/stderr lead to, by
//     oracle_process_link;
//   - any other path is kept, the rest of /dev's among them: a device node by its kind and number (opened, it may be
//     refused: oracle_opens_unseen), and /dev/shm's files, an ordinary tmpfs's, as any other directory's.
// A path with ".." in it may lead out of where it starts, and isn't left out: it is kept, or refused where a process's
// own paths may lead elsewhere (/proc, /sys and /dev/fd).
static OraclePath oracle_path_rule(const OracleRun* run, const char* path, bool followed, OracleUse use, pid_t caller)
{
    static const char* const packaged[] = { "/usr", "/bin", "/sbin", "/lib", "/lib32", "/lib64", "/libx32" };
    static const char* const streams[] = { "/dev/stdin", "/dev/stdout", "/dev/stderr" };
    static const char* const descriptors[] = { "/fd/0", "/fd/1", "/fd/2" };
    const char* rest = "";
    const bool parent = strstr(path, "/..") != 0;
    const bool kernel = oracle_path_under(path, "/proc", &rest) || oracle_path_under(path, "/sys", &rest) || oracle_path_under(path, "/dev/fd", &rest);
    bool package = false;
    for (uint32_t p = 0; p < sizeof(packaged) / sizeof(packaged[0]); p += 1)
    {
        package |= oracle_path_under(path, packaged[p], &rest);
    }
    package &= !oracle_path_under(path, "/usr/local", &rest);
    // A process's paths: /proc/PID's (and its task's), and /dev/fd's and the standard streams', which lead to the
    // caller's /proc/self/fd's.
    char link[BUSTER_ORACLE_PATH_BYTES + 8];
    bool own = false;
    const char* process = oracle_path_under(path, "/proc", &rest) ? oracle_process_part(rest, caller, &own) : 0;
    if (oracle_path_under(path, "/dev/fd", &rest))
    {
        snprintf(link, sizeof(link), "/fd%s", rest);
        process = link;
        own = true;
    }
    for (uint32_t s = 0; s < sizeof(streams) / sizeof(streams[0]); s += 1)
    {
        if (oracle_path_under(path, streams[s], &rest))
        {
            snprintf(link, sizeof(link), "%s%s", descriptors[s], rest);
            process = link;
            own = true;
        }
    }
    const OraclePath kernel_rule = process != 0 ? oracle_process_link(process, followed, use, own, caller) : use == ORACLE_USE_CHECK ? ORACLE_PATH_LEFT_OUT : ORACLE_PATH_REFUSED;
    return oracle_run_rooted(run, path) ? ORACLE_PATH_KEPT
           : parent && (kernel || process != 0) ? ORACLE_PATH_REFUSED
           : parent                            ? ORACLE_PATH_KEPT
           : kernel || process != 0            ? kernel_rule
           : package                           ? ORACLE_PATH_LEFT_OUT
                                               : ORACLE_PATH_KEPT;
}

// Whether an open of `path` reaches what the cache can't look at by its bytes: a device, whose data change with no file
// changing (/dev/urandom's on every read), a FIFO, whose come from another process, or a socket; but /dev/null,
// /dev/zero and /dev/full, whose data never change, and /dev/tty, which a run can't open, having no controlling
// terminal (oracle_child). Or a file of the kernel's, which it makes as it is read, wherever a path leads there (a
// symbolic link in a root to /proc/self/stat, a file system mounted elsewhere): procfs, sysfs, debugfs, tracefs,
// securityfs, cgroup's and cgroup2's, bpf's, configfs, efivarfs, pstore, the POSIX message queues', selinuxfs,
// binfmt_misc and fusectl, by their magic numbers (<linux/magic.h>), or one whose file system statfs can't give.
static bool oracle_opens_unseen(const char* path)
{
    static const uint64_t kernels[] = {
        0x9fa0,     0x62656572, 0x64626720, 0x74726163, 0x73636673, 0x27e0eb,   0x63677270, 0xcafe4a11,
        0x62656570, 0xde5e81e4, 0x6165676c, 0x19800202, 0xf97cff8c, 0x42494e4d, 0x65735543,
    };
    struct stat status;
    struct statfs system;
    const bool found = stat(path, &status) == 0;
    const bool measured = found && statfs(path, &system) == 0;
    bool kernel = found && !measured;
    for (uint32_t k = 0; measured && k < sizeof(kernels) / sizeof(kernels[0]); k += 1)
    {
        kernel |= (uint64_t)system.f_type == kernels[k];
    }
    const dev_t device = found ? status.st_rdev : 0;
    const bool constant = found && S_ISCHR(status.st_mode) &&
                          (device == makedev(1, 3) || device == makedev(1, 5) || device == makedev(1, 7) || device == makedev(5, 0));
    return found && (kernel || (!S_ISREG(status.st_mode) && !S_ISDIR(status.st_mode) && !constant));
}

// The kernel's sizes of a notification and its response (SECCOMP_GET_NOTIF_SIZES), which may be larger than the
// headers' structures: oracle_trace_answer's buffers hold up to BUSTER_ORACLE_NOTIFICATION bytes.
#define BUSTER_ORACLE_NOTIFICATION 1024
static struct seccomp_notif_sizes oracle_trace_sizes;

// openat2's resolution flags, the third of struct open_how's words (after its flags and mode) at `address`, which is
// `size` bytes long: UINT64_MAX if it can't be read.
static uint64_t oracle_trace_resolve(pid_t pid, uint64_t address, uint64_t size)
{
    uint64_t how[3] = { 0 };
    struct iovec local = {
        .iov_base = how,
        .iov_len = sizeof(how),
    };
    struct iovec remote = {
        .iov_base = (void*)(uintptr_t)address,
        .iov_len = sizeof(how),
    };
    const bool read = size >= sizeof(how) && (ssize_t)syscall(SYS_process_vm_readv, pid, &local, 1ul, &remote, 1ul, 0ul) == (ssize_t)sizeof(how);
    return read ? how[2] : UINT64_MAX;
}

// The `p`th path of a call that `traced` describes, as its process sees it, into `path` (`capacity` bytes): its name,
// read from the process, from its directory's descriptor if it is relative (or the working directory, without one),
// or the descriptor's own path where the call names none (a listing's, a null name's, "" with AT_EMPTY_PATH among
// `flags`). 1 if it is a path, 0 if the descriptor is something else (a pipe, a socket), and -1 if it can't be read or
// doesn't fit.
static int oracle_trace_path(pid_t pid, const struct seccomp_data* call, const OracleCall* traced, uint32_t p, uint64_t flags, char* path, size_t capacity)
{
    char named[BUSTER_ORACLE_PATH_BYTES] = { 0 };
    char base[BUSTER_ORACLE_PATH_BYTES] = { 0 };
    const int descriptor = traced->directories[p] >= 0 ? (int)(uint32_t)call->args[traced->directories[p]] : AT_FDCWD;
    const uint64_t address = traced->paths[p] >= 0 ? call->args[traced->paths[p]] : 0;
    const bool unnamed = address == 0 && traced->directories[p] >= 0;
    const bool named_read = unnamed || oracle_trace_string(pid, address, named, sizeof(named));
    const bool own = unnamed || ((flags & AT_EMPTY_PATH) != 0 && named[0] == 0);
    const int found = !named_read ? -1 : own || named[0] != '/' ? oracle_trace_descriptor(pid, descriptor, base, sizeof(base)) : 1;
    const int length = found <= 0 ? 0
                       : own ? snprintf(path, capacity, "%s", base)
                       : named[0] == '/' ? snprintf(path, capacity, "%s", named)
                                         : snprintf(path, capacity, "%s/%s", strcmp(base, "/") == 0 ? "" : base, named);
    return found <= 0 ? found : length > 0 && (size_t)length < capacity ? 1 : -1;
}

// The paths of a call the filter stopped (`request`) that it looks at, makes, removes or renames (or the directory whose
// names it lists, or must find empty), looked at, if the run hasn't before (oracle_looks_add), before the call goes on
// to the kernel. A call the tracing can't follow (oracle_unfollowed_calls, openat2's resolution flags), a path it can't
// read or must refuse (oracle_path_rule, and what oracle_opens_unseen refuses an open) and a look it can't keep make the
// run untraced.
static void oracle_trace_paths(OracleRun* run, const struct seccomp_notif* request)
{
    const struct seccomp_data* call = &request->data;
    const pid_t pid = (pid_t)request->pid;
    // The call's description, none for another architecture's, x32's and oracle_unfollowed_calls.
    const OracleCall* traced = 0;
    for (uint32_t c = 0; call->arch == AUDIT_ARCH_X86_64 && c < BUSTER_ORACLE_CALLS; c += 1)
    {
        traced = oracle_calls[c].number == (uint32_t)call->nr ? &oracle_calls[c] : traced;
    }
    const uint64_t flags = traced != 0 && traced->flags >= 0 ? call->args[traced->flags] : 0;
    // Whether it follows a symbolic link at its first path's end (oracle_path_rule): a listing's directory was opened,
    // which does.
    const bool followed = traced != 0 && (traced->follows ? (flags & AT_SYMLINK_NOFOLLOW) == 0 : (flags & AT_SYMLINK_FOLLOW) != 0);
    // openat2's resolution flags change which file a path reaches (RESOLVE_IN_ROOT's absolute names, from the
    // descriptor's directory), or whether it reaches one (RESOLVE_BENEATH's and the others' refusals), which the tracing
    // doesn't model: a call with any is one it can't follow.
    bool known = traced != 0 && (call->nr != SYS_openat2 || oracle_trace_resolve(pid, call->args[2], call->args[3]) == 0);
    const bool empties = traced != 0 && (traced->empties || (call->nr == SYS_unlinkat && (flags & AT_REMOVEDIR) != 0));
    char probes[2][BUSTER_ORACLE_PATH_BYTES + 1] = { { 0 } };
    OraclePath rules[2] = { ORACLE_PATH_LEFT_OUT, ORACLE_PATH_LEFT_OUT };
    for (uint32_t p = 0; known && p < 2; p += 1)
    {
        if (traced->paths[p] >= 0 || traced->directories[p] >= 0)
        {
            // The call's AT_ flags are its first path's.
            const int found = oracle_trace_path(pid, call, traced, p, p == 0 ? flags : 0, probes[p] + 1, sizeof(probes[p]) - 1);
            known = found >= 0;
            rules[p] = found > 0 ? oracle_path_rule(run, probes[p] + 1, p == 0 && followed, (OracleUse)traced->use, pid) : ORACLE_PATH_LEFT_OUT;
            rules[p] = rules[p] == ORACLE_PATH_KEPT && traced->use == ORACLE_USE_OPEN && oracle_opens_unseen(probes[p] + 1) ? ORACLE_PATH_REFUSED : rules[p];
        }
    }
    // The process may have ended, and its pid gone to another, while the paths were read.
    const bool valid = ioctl(run->listener, SECCOMP_IOCTL_NOTIF_ID_VALID, &request->id) == 0;
    for (uint32_t p = 0; valid && known && p < 2; p += 1)
    {
        if (rules[p] == ORACLE_PATH_KEPT)
        {
            probes[p][0] = traced->lists ? 'l' : 'p';
            oracle_looks_add(run, probes[p]);
            // The last path's directory's names too, where the call needs it empty.
            if (empties && (p == 1 || traced->paths[1] < 0))
            {
                probes[p][0] = 'l';
                oracle_looks_add(run, probes[p]);
            }
        }
    }
    run->untraced |= valid && (!known || rules[0] == ORACLE_PATH_REFUSED || rules[1] == ORACLE_PATH_REFUSED);
}

// A getrandom call the filter stopped (`request`), given its bytes here: the run's nth given call
// (OracleRun.randoms) gets SHA-256 of "buster oracle random 1", the run's key, n and each 32-byte block's number, so
// that its bytes are the same each time the run runs, and its result can't depend on bytes that differ from one run
// to the next (glibc's malloc and clang's temporary files' names take some; glibc's getrandom through the vDSO takes
// its key from such a call, then makes its bytes itself, until the kernel's generator reseeds). They are written
// through the process's memory file, opened before the notification is checked, so that a process that ended, whose
// pid went to another, isn't written to. True if they were: the call returns their count. A call the kernel refuses
// (unknown flags, or GRND_INSECURE with GRND_RANDOM) or that asks for no bytes isn't given any; one for more than
// BUSTER_ORACLE_RANDOM_BYTES, or whose bytes can't be written, goes on to the kernel, and its run isn't kept.
static bool oracle_trace_random(OracleRun* run, const struct seccomp_notif* request)
{
    const struct seccomp_data* call = &request->data;
    const uint64_t address = call->args[0];
    const uint64_t length = call->args[1];
    const uint64_t flags = call->args[2];
    const bool valid = (flags & ~(uint64_t)(GRND_NONBLOCK | GRND_RANDOM | GRND_INSECURE)) == 0 && (flags & (GRND_INSECURE | GRND_RANDOM)) != (GRND_INSECURE | GRND_RANDOM);
    const bool given = valid && length != 0 && length <= BUSTER_ORACLE_RANDOM_BYTES && address <= (uint64_t)INT64_MAX - length;
    uint8_t bytes[BUSTER_ORACLE_RANDOM_BYTES];
    for (uint32_t block = 0; given && (uint64_t)block * BUSTER_ORACLE_KEY_BYTES < length; block += 1)
    {
        OracleHash hash;
        oracle_sha256_begin(&hash);
        oracle_hash_text(&hash, "buster oracle random 1");
        oracle_hash_bytes(&hash, run->key.bytes, sizeof(run->key.bytes));
        oracle_hash_bytes(&hash, &run->randoms, sizeof(run->randoms));
        oracle_hash_bytes(&hash, &block, sizeof(block));
        const OracleKey key = oracle_hash_end(&hash);
        memcpy(bytes + (size_t)block * BUSTER_ORACLE_KEY_BYTES, key.bytes, sizeof(key.bytes));
    }
    char memory[64];
    snprintf(memory, sizeof(memory), "/proc/%ld/mem", (long)request->pid);
    const int descriptor = given ? open(memory, O_RDWR | O_CLOEXEC) : -1;
    const bool current = ioctl(run->listener, SECCOMP_IOCTL_NOTIF_ID_VALID, &request->id) == 0;
    const bool written = descriptor >= 0 && current && pwrite(descriptor, bytes, (size_t)length, (off_t)address) == (ssize_t)length;
    if (descriptor >= 0)
    {
        close(descriptor);
    }
    run->randoms += written ? 1 : 0;
    run->untraced |= current && !written && valid && length != 0;
    return written;
}

// Answers one call the filter stopped on the run's listener: getrandom's with its bytes (oracle_trace_random), any
// other's with its paths looked at (oracle_trace_paths), let through to the kernel. A listener that fails stops its
// answering (OracleRun.hung) and makes the run untraced.
static void oracle_trace_answer(OracleRun* run)
{
    const bool sized = oracle_trace_sizes.seccomp_notif != 0 || syscall(SYS_seccomp, SECCOMP_GET_NOTIF_SIZES, 0, &oracle_trace_sizes) == 0;
    _Alignas(16) uint8_t request_bytes[BUSTER_ORACLE_NOTIFICATION] = { 0 };
    _Alignas(16) uint8_t response_bytes[BUSTER_ORACLE_NOTIFICATION] = { 0 };
    const bool fits = sized && oracle_trace_sizes.seccomp_notif <= sizeof(request_bytes) && oracle_trace_sizes.seccomp_notif_resp <= sizeof(response_bytes) &&
                      oracle_trace_sizes.seccomp_notif >= sizeof(struct seccomp_notif) && oracle_trace_sizes.seccomp_notif_resp >= sizeof(struct seccomp_notif_resp);
    struct seccomp_notif* request = (struct seccomp_notif*)request_bytes;
    const int received = fits ? ioctl(run->listener, SECCOMP_IOCTL_NOTIF_RECV, request) : -1;
    // EINTR: answered on the next poll; ENOENT: the process that called it ended before it was read.
    const bool lost = received != 0 && (!fits || (errno != EINTR && errno != ENOENT));
    run->hung |= lost;
    run->untraced |= lost;
    if (received == 0)
    {
        const bool random = request->data.arch == AUDIT_ARCH_X86_64 && request->data.nr == SYS_getrandom;
        const bool given = random && oracle_trace_random(run, request);
        if (!random)
        {
            oracle_trace_paths(run, request);
        }
        struct seccomp_notif_resp* response = (struct seccomp_notif_resp*)response_bytes;
        response->id = request->id;
        response->val = given ? (int64_t)request->data.args[1] : 0;
        response->flags = given ? 0 : SECCOMP_USER_NOTIF_FLAG_CONTINUE;
        const bool answered = ioctl(run->listener, SECCOMP_IOCTL_NOTIF_SEND, response) == 0 || errno == ENOENT;
        run->hung |= !answered;
        run->untraced |= !answered;
    }
}

// --- Spawning ---

// The child's report to its parent: the listener of its filter, as an 'L' with the descriptor, or a step that failed, as
// an 'E' and its errno.
static bool oracle_send_listener(int report, int listener)
{
    char byte = 'L';
    struct iovec part = {
        .iov_base = &byte,
        .iov_len = 1,
    };
    _Alignas(struct cmsghdr) char control[CMSG_SPACE(sizeof(int))];
    memset(control, 0, sizeof(control));
    struct msghdr message = {
        .msg_iov = &part,
        .msg_iovlen = 1,
        .msg_control = listener >= 0 ? control : 0,
        .msg_controllen = listener >= 0 ? sizeof(control) : 0,
    };
    if (listener >= 0)
    {
        struct cmsghdr* header = CMSG_FIRSTHDR(&message);
        header->cmsg_level = SOL_SOCKET;
        header->cmsg_type = SCM_RIGHTS;
        header->cmsg_len = CMSG_LEN(sizeof(int));
        memcpy(CMSG_DATA(header), &listener, sizeof(int));
    }
    return sendmsg(report, &message, 0) == 1;
}

// The listener a traced run's child sends (oracle_send_listener), closed on exec here: -1 if its filter couldn't be
// installed, or if the child failed first (then OracleRun.unstarted).
static int oracle_receive_listener(OracleRun* run)
{
    char byte = 0;
    struct iovec part = {
        .iov_base = &byte,
        .iov_len = 1,
    };
    _Alignas(struct cmsghdr) char control[CMSG_SPACE(sizeof(int))];
    memset(control, 0, sizeof(control));
    struct msghdr message = {
        .msg_iov = &part,
        .msg_iovlen = 1,
        .msg_control = control,
        .msg_controllen = sizeof(control),
    };
    ssize_t got = -1;
    do
    {
        got = recvmsg(run->report, &message, 0);
    }
    while (got < 0 && errno == EINTR);
    int listener = -1;
    struct cmsghdr* header = got == 1 ? CMSG_FIRSTHDR(&message) : 0;
    if (header != 0 && header->cmsg_level == SOL_SOCKET && header->cmsg_type == SCM_RIGHTS && header->cmsg_len == CMSG_LEN(sizeof(int)))
    {
        memcpy(&listener, CMSG_DATA(header), sizeof(int));
        fcntl(listener, F_SETFD, FD_CLOEXEC);
    }
    run->unstarted |= got != 1 || byte != 'L';
    return byte == 'L' ? listener : -1;
}

// The child's side, between fork and exec, with async-signal-safe calls alone: a process group of its own, its standard
// input, output and errors, its working directory, with `filter` the filter and its listener sent to the parent on
// `report`, then the tool. A step that fails sends an 'E' and its errno on `report` and ends the child with status 127.
static void oracle_child(const OracleRun* run, int input, int report, const struct sock_fprog* filter, char* const* environment)
{
    setpgid(0, 0);
    bool ready = dup2(input, 0) == 0 && dup2(run->output_descriptor, 1) == 1 && dup2(run->errors_descriptor, 2) == 2 &&
                 (run->directory == 0 || chdir(run->directory) == 0);
    // What the tool reads comes through the paths the filter stops and from its key's input: it keeps no descriptor
    // but the standard three past exec (close_range's CLOSE_RANGE_CLOEXEC), and no controlling terminal, which
    // /dev/tty would read: given up (TIOCNOTTY), /dev/tty's open fails (ENXIO), as oracle_opens_unseen counts on. A
    // traced child that can't give both up installs no filter, so that its run isn't kept.
    const int terminal = ready ? open("/dev/tty", O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC) : -1;
    const bool alone = ready && syscall(SYS_close_range, 3u, ~0u, CLOSE_RANGE_CLOEXEC) == 0 && (terminal < 0 || ioctl(terminal, TIOCNOTTY) == 0);
    if (terminal >= 0)
    {
        close(terminal);
    }
    if (ready && filter != 0)
    {
        const int listener = alone && prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == 0 ? (int)syscall(SYS_seccomp, SECCOMP_SET_MODE_FILTER, SECCOMP_FILTER_FLAG_NEW_LISTENER, filter) : -1;
        ready = oracle_send_listener(report, listener);
        if (listener >= 0)
        {
            close(listener);
        }
    }
    if (ready)
    {
        execve(run->tool, run->arguments, environment);
    }
    const int error = errno;
    char failed[1 + sizeof(int)] = { 'E' };
    memcpy(failed + 1, &error, sizeof(error));
    const ssize_t sent = write(report, failed, sizeof(failed));
    _exit(sent == (ssize_t)sizeof(failed) ? 127 : 126);
}

// The self-test's switch: wait for every process as the cache does where it has no pidfd.
static bool oracle_without_pidfd;

// Starts the run's tool in a process group of its own, its standard input from its input, its output and errors into
// captures, and, if `traced`, under the filter, its listener answered by oracle_trace_answer: if the filter can't be
// installed, the tool runs all the same, untraced. False if it can't start: the tool isn't on PATH, its input can't be
// opened, or a capture, the socket or the fork can't be made.
static bool oracle_run_spawn(OracleRun* run, bool traced)
{
    bool result = run->tool[0] != 0 || oracle_find_tool(run->arguments[0], run->tool, sizeof(run->tool));
    char* const* environment = run->environment != 0 ? run->environment : environ;
    struct sock_filter filter[BUSTER_ORACLE_FILTER];
    struct sock_fprog program;
    oracle_trace_filter(filter, &program);
    int sockets[2] = { -1, -1 };
    const int input = result ? oracle_high(open(run->input != 0 ? run->input : "/dev/null", O_RDONLY | O_CLOEXEC)) : -1;
    run->output_descriptor = result ? oracle_capture() : -1;
    run->errors_descriptor = result ? oracle_capture() : -1;
    result = result && input >= 0 && run->output_descriptor >= 0 && run->errors_descriptor >= 0 && socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets) == 0;
    sockets[1] = oracle_high(sockets[1]);
    result = result && sockets[1] >= 0;
    const pid_t pid = result ? fork() : -1;
    if (pid == 0)
    {
        oracle_child(run, input, sockets[1], traced ? &program : 0, environment);
    }
    if (sockets[1] >= 0)
    {
        close(sockets[1]);
    }
    if (input >= 0)
    {
        close(input);
    }
    result = result && pid > 0;
    if (result)
    {
        // Its group made here too, so that it is there whichever side runs first.
        setpgid(pid, pid);
        run->pid = pid;
        run->started = oracle_now();
        run->process = oracle_without_pidfd ? -1 : (int)syscall(SYS_pidfd_open, pid, 0);
        run->report = sockets[0];
        sockets[0] = -1;
    }
    if (sockets[0] >= 0)
    {
        close(sockets[0]);
    }
    if (result && traced)
    {
        run->listener = oracle_receive_listener(run);
        run->untraced |= run->listener < 0;
        oracle_traced_own();
        if (run->listener >= 0)
        {
            oracle_traced[oracle_traced_count] = run;
            oracle_traced_count += 1;
        }
    }
    return result;
}

// Whether the process has ended, without reaping it (waitid with WNOWAIT), for a run without a pidfd: so that its
// group's id stays its own until the group is killed. True on an error but EINTR, which leaves nothing to wait for.
static bool oracle_ended(pid_t pid)
{
    siginfo_t information;
    memset(&information, 0, sizeof(information));
    const int waited = waitid(P_PID, (id_t)pid, &information, WEXITED | WNOHANG | WNOWAIT);
    return waited == 0 ? information.si_pid == pid : errno != EINTR;
}

// Waits for the run's process to end while it answers the traced runs' calls (its own and the others' this process
// has under way, so that they go on too): a poll on its pidfd and their listeners, a millisecond at a time without a
// pidfd, up to its time limit on the monotonic clock, past which its process group is killed; then what its group
// left running is killed before it is reaped. The wait status into `status`; false if it couldn't be waited for, or was
// killed for its time (OracleRun.killed).
static bool oracle_run_wait(OracleRun* run, int* status)
{
    const uint64_t deadline = run->started + (uint64_t)run->timeout * 1000000000;
    bool ended = run->pid <= 0;
    bool killed = false;
    while (!ended)
    {
        struct pollfd polled[1 + BUSTER_ORACLE_TRACED];
        OracleRun* owners[1 + BUSTER_ORACLE_TRACED];
        uint32_t count = 0;
        if (run->process >= 0)
        {
            polled[count] = (struct pollfd){
                .fd = run->process,
                .events = POLLIN,
            };
            owners[count] = 0;
            count += 1;
        }
        oracle_traced_own();
        for (uint32_t t = 0; t < oracle_traced_count; t += 1)
        {
            if (oracle_traced[t]->listener >= 0 && !oracle_traced[t]->hung)
            {
                polled[count] = (struct pollfd){
                    .fd = oracle_traced[t]->listener,
                    .events = POLLIN,
                };
                owners[count] = oracle_traced[t];
                count += 1;
            }
        }
        const uint64_t now = oracle_now();
        const uint64_t left = run->timeout == 0 || killed ? UINT64_MAX : deadline > now ? (deadline - now + 999999) / 1000000 : 0;
        const uint64_t limit = run->process < 0 && left > 1 ? 1 : left;
        const int ready = poll(polled, count, limit > INT32_MAX ? -1 : (int)limit);
        for (uint32_t p = 0; ready > 0 && p < count; p += 1)
        {
            if (owners[p] != 0 && (polled[p].revents & POLLIN) != 0)
            {
                oracle_trace_answer(owners[p]);
            }
            else if (owners[p] != 0 && (polled[p].revents & (POLLHUP | POLLERR | POLLNVAL)) != 0)
            {
                owners[p]->hung = true;
            }
        }
        ended = run->process >= 0 ? ready > 0 && (polled[0].revents & (POLLIN | POLLHUP | POLLERR | POLLNVAL)) != 0 : oracle_ended(run->pid);
        // A poll that fails but for a signal leaves the wait to oracle_ended, a millisecond at a time.
        if (ready < 0 && errno != EINTR && run->process >= 0)
        {
            close(run->process);
            run->process = -1;
        }
        if (!ended && run->timeout != 0 && !killed && oracle_now() >= deadline)
        {
            kill(-run->pid, SIGKILL);
            kill(run->pid, SIGKILL);
            killed = true;
        }
    }
    // Whatever the tool's processes left running in its group goes with it, while its ended leader keeps the group's id.
    if (run->pid > 0)
    {
        kill(-run->pid, SIGKILL);
    }
    pid_t waited = run->pid > 0 ? -1 : 0;
    do
    {
        waited = run->pid > 0 ? waitpid(run->pid, status, 0) : 0;
    }
    while (waited < 0 && errno == EINTR);
    run->killed = killed;
    return run->pid > 0 && waited == run->pid && !killed;
}

// Whether the run's child started its tool: true if the socket it reports on closed on exec without a word, false if
// it sent an 'E' (oracle_child), or was never there.
static bool oracle_run_started(OracleRun* run)
{
    char byte = 0;
    ssize_t got = -1;
    do
    {
        got = run->report >= 0 ? read(run->report, &byte, 1) : 0;
    }
    while (got < 0 && errno == EINTR);
    return run->report >= 0 && !run->unstarted && got == 0;
}

// The end of a run that ran: its tool waited for (oracle_run_wait), whether it started, its status, and its output and
// errors read. False if any of that failed.
static bool oracle_run_end(OracleRun* run)
{
    int status = 0;
    bool result = oracle_run_wait(run, &status);
    result = oracle_run_started(run) && result;
    run->status = status;
    result = oracle_read_capture(run->output_descriptor, &run->output, &run->output_length) && result;
    result = oracle_read_capture(run->errors_descriptor, &run->errors, &run->errors_length) && result;
    oracle_traced_remove(run);
    return result;
}

// Closes a finished run's descriptors and frees its looks.
static void oracle_run_close(OracleRun* run)
{
    const int descriptors[5] = { run->output_descriptor, run->errors_descriptor, run->process, run->report, run->listener };
    for (uint32_t d = 0; d < 5; d += 1)
    {
        if (descriptors[d] >= 0)
        {
            close(descriptors[d]);
        }
    }
    run->output_descriptor = -1;
    run->errors_descriptor = -1;
    run->process = -1;
    run->report = -1;
    run->listener = -1;
    run->pid = 0;
    oracle_looks_release(&run->looks);
}

// The run's own state before it starts, its result empty.
static void oracle_run_reset(OracleRun* run)
{
    run->cache = run->cache != 0 ? run->cache : oracle_cache();
    run->status = 0;
    run->output = 0;
    run->output_length = 0;
    run->errors = 0;
    run->errors_length = 0;
    run->taken = false;
    run->killed = false;
    run->keyed = false;
    run->tool[0] = 0;
    run->pid = 0;
    run->process = -1;
    run->started = 0;
    run->report = -1;
    run->unstarted = false;
    run->output_descriptor = -1;
    run->errors_descriptor = -1;
    run->listener = -1;
    run->hung = false;
    run->untraced = false;
    run->randoms = 0;
    run->looks = (OracleLooks){ 0 };
    run->checking = false;
    run->checked = (OracleKey){ 0 };
}

// --- Entries ---

static const char oracle_magic[8] = { 'B', 'U', 'S', 'T', 'O', 'R', 'C', '3' };

// An entry's flags: its files' modification times are part of what its run read (oracle_tool_epoch).
#define BUSTER_ORACLE_TIMES 1u

// An entry's header: the magic, the key, the wait status, its records' counts, its flags and the bytes after it. Then its
// roots (each a u32 length and its name), what its run looked at (each a u32 length, the probe, its kind and the path as
// the run named it, and SHA-256), its files (each a u64 length, UINT64_MAX for none, and its bytes), its standard output
// and its errors (each a u64 length and its bytes).
typedef struct OracleEntryHeader OracleEntryHeader;
struct OracleEntryHeader
{
    char magic[8];
    uint8_t key[BUSTER_ORACLE_KEY_BYTES];
    int32_t status;
    uint32_t roots;
    uint32_t dependencies;
    uint32_t flags;
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
    uint32_t flags;
    uint32_t root_count;
    OracleRoot roots[BUSTER_ORACLE_ROOTS];
    uint32_t dependency_count;
    uint64_t dependencies;
    uint32_t file_count;
    uint64_t files;
    uint8_t* output;
    uint64_t output_length;
    uint8_t* errors;
    uint64_t errors_length;
};

// A digest's name, its bytes in hexadecimal, into `name` (2 * BUSTER_ORACLE_KEY_BYTES + 1 bytes).
static void oracle_digest_name(OracleKey digest, char* name)
{
    for (uint32_t i = 0; i < BUSTER_ORACLE_KEY_BYTES; i += 1)
    {
        snprintf(name + 2 * i, 3, "%02x", digest.bytes[i]);
    }
}

// Whether `name` is a digest's (oracle_digest_name), as an entry's file is named, rather than a file that an entry's
// writing makes beside it.
static bool oracle_digest_named(const char* name)
{
    return strlen(name) == 2 * BUSTER_ORACLE_KEY_BYTES && strspn(name, "0123456789abcdef") == 2 * BUSTER_ORACLE_KEY_BYTES;
}

// The digest whose name `name` is (oracle_digest_named's).
static OracleKey oracle_digest_parse(const char* name)
{
    OracleKey result = { 0 };
    for (uint32_t i = 0; i < 2 * BUSTER_ORACLE_KEY_BYTES; i += 1)
    {
        const char c = name[i];
        const uint8_t nibble = (uint8_t)(c <= '9' ? c - '0' : c - 'a' + 10);
        result.bytes[i / 2] |= (uint8_t)(i % 2 == 0 ? nibble << 4 : nibble);
    }
    return result;
}

// The directory of a key's entries.
static void oracle_key_directory(const OracleCache* cache, OracleKey key, char* path, size_t capacity)
{
    char name[2 * BUSTER_ORACLE_KEY_BYTES + 1];
    oracle_digest_name(key, name);
    snprintf(path, capacity, "%s/%s", cache->directory, name);
}

// The file of a key's entry of `variant` (oracle_entry_variant).
static void oracle_entry_path(const OracleCache* cache, OracleKey key, OracleKey variant, char* path, size_t capacity)
{
    char key_name[2 * BUSTER_ORACLE_KEY_BYTES + 1];
    char variant_name[2 * BUSTER_ORACLE_KEY_BYTES + 1];
    oracle_digest_name(key, key_name);
    oracle_digest_name(variant, variant_name);
    snprintf(path, capacity, "%s/%s/%s", cache->directory, key_name, variant_name);
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

// Steps over `count` records of an entry, each a u32 length, its bytes and SHA-256: false if one runs past `end`.
static bool oracle_entry_skip(const uint8_t* bytes, uint64_t end, uint64_t* at, uint32_t count)
{
    bool result = true;
    for (uint32_t i = 0; result && i < count; i += 1)
    {
        uint32_t length = 0;
        result = oracle_take_u32(bytes, end, at, &length) && end - *at >= (uint64_t)length + BUSTER_ORACLE_KEY_BYTES;
        *at += result ? (uint64_t)length + BUSTER_ORACLE_KEY_BYTES : 0;
    }
    return result;
}

// The record at bytes[*at] of a section an entry's reading checked: its probe into `probe` (with the entry's roots
// replaced by `roots`, as many and as long) and its SHA-256, *at past it. False if the probe doesn't fit.
static bool oracle_entry_record(const OracleEntry* entry, uint64_t* at, const OracleRoot* roots, char* probe, size_t capacity, const uint8_t** digest)
{
    uint32_t length = 0;
    memcpy(&length, entry->bytes + *at, sizeof(length));
    const bool result = length < capacity;
    if (result)
    {
        memcpy(probe, entry->bytes + *at + sizeof(length), length);
        probe[length] = 0;
        oracle_roots_replace(entry->roots, roots, entry->root_count, (uint8_t*)probe, length);
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

// The entry of `key` in the file `path`, if it is there and whole: false if not.
static bool oracle_entry_read(const char* path, OracleKey key, OracleEntry* entry)
{
    *entry = (OracleEntry){ 0 };
    OracleBuffer buffer = { 0 };
    bool result = oracle_buffer_file(&buffer, path) && buffer.length >= sizeof(OracleEntryHeader);
    OracleEntryHeader header;
    if (result)
    {
        memcpy(&header, buffer.bytes, sizeof(header));
    }
    result = result && memcmp(header.magic, oracle_magic, sizeof(oracle_magic)) == 0 && memcmp(header.key, key.bytes, sizeof(key.bytes)) == 0 &&
             header.body == buffer.length - sizeof(header) && header.roots <= BUSTER_ORACLE_ROOTS && header.files <= BUSTER_ORACLE_FILES &&
             (header.flags & ~BUSTER_ORACLE_TIMES) == 0;
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
    result = result && oracle_entry_skip(buffer.bytes, end, &at, header.dependencies);
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
        entry->flags = header.flags;
        entry->root_count = header.roots;
        entry->dependency_count = header.dependencies;
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

// What a run looked at, `count` probes with their looks, into `digests` (BUSTER_ORACLE_KEY_BYTES each): each probe's
// digest (oracle_probe_digest) and the place of the first of them that is the same file (a hard link, which #pragma
// once takes once, whatever names reach it). False if one can't be read.
typedef struct OracleSame OracleSame;
struct OracleSame
{
    int64_t device;
    int64_t inode;
    uint32_t place;
};

static int oracle_compare_same(const void* a, const void* b)
{
    const OracleSame* x = a;
    const OracleSame* y = b;
    return x->device != y->device ? (x->device < y->device ? -1 : 1) : x->inode != y->inode ? (x->inode < y->inode ? -1 : 1) : x->place < y->place ? -1 : x->place > y->place;
}

static bool oracle_probe_digests(char* const* probes, const OracleLook* looks, uint32_t count, bool times, uint8_t* digests)
{
    OracleSame* files = calloc((size_t)count + 1, sizeof(OracleSame));
    uint32_t* firsts = calloc((size_t)count + 1, sizeof(uint32_t));
    bool result = files != 0 && firsts != 0;
    for (uint32_t i = 0; result && i < count; i += 1)
    {
        result = oracle_probe_digest(probes[i], &looks[i], times, digests + (size_t)i * BUSTER_ORACLE_KEY_BYTES);
        files[i] = (OracleSame){ .device = looks[i].identity[0], .inode = looks[i].identity[1], .place = i };
        firsts[i] = i;
    }
    if (result && count > 1)
    {
        qsort(files, count, sizeof(OracleSame), oracle_compare_same);
    }
    for (uint32_t i = 1; result && i < count; i += 1)
    {
        const bool same = files[i].device == files[i - 1].device && files[i].inode == files[i - 1].inode && (files[i].device | files[i].inode) != 0;
        firsts[files[i].place] = same ? firsts[files[i - 1].place] : files[i].place;
    }
    for (uint32_t i = 0; result && i < count; i += 1)
    {
        OracleHash hash;
        oracle_sha256_begin(&hash);
        oracle_hash_bytes(&hash, digests + (size_t)i * BUSTER_ORACLE_KEY_BYTES, BUSTER_ORACLE_KEY_BYTES);
        oracle_hash_bytes(&hash, &firsts[i], sizeof(firsts[i]));
        const OracleKey key = oracle_hash_end(&hash);
        memcpy(digests + (size_t)i * BUSTER_ORACLE_KEY_BYTES, key.bytes, sizeof(key.bytes));
    }
    free(files);
    free(firsts);
    return result;
}

// Whether what the entry's run looked at is as the run finds it now: each of its probes, the entry's roots replaced by
// the run's, looked at (oracle_probe_look) and its digest made (oracle_probe_digests) as when it was kept. The entry
// fits the run (oracle_entry_fits).
static bool oracle_entry_inputs(const OracleEntry* entry, const OracleRun* run)
{
    const uint32_t count = entry->dependency_count;
    char** names = calloc((size_t)count + 1, sizeof(char*));
    uint8_t* stored = malloc(((size_t)count + 1) * BUSTER_ORACLE_KEY_BYTES);
    uint8_t* now = malloc(((size_t)count + 1) * BUSTER_ORACLE_KEY_BYTES);
    OracleLook* looks = calloc((size_t)count + 1, sizeof(OracleLook));
    bool result = names != 0 && stored != 0 && now != 0 && looks != 0;
    uint64_t at = entry->dependencies;
    for (uint32_t d = 0; result && d < count; d += 1)
    {
        char named[BUSTER_ORACLE_PATH_BYTES + 1];
        const uint8_t* digest = 0;
        result = oracle_entry_record(entry, &at, run->roots.roots, named, sizeof(named), &digest) && (named[0] == 'p' || named[0] == 'l');
        names[d] = result ? strdup(named) : 0;
        result = result && names[d] != 0 && oracle_probe_look(run, names[d], &looks[d]);
        if (result)
        {
            memcpy(stored + (size_t)d * BUSTER_ORACLE_KEY_BYTES, digest, BUSTER_ORACLE_KEY_BYTES);
        }
    }
    result = result && oracle_probe_digests(names, looks, count, (entry->flags & BUSTER_ORACLE_TIMES) != 0, now) &&
             memcmp(now, stored, (size_t)count * BUSTER_ORACLE_KEY_BYTES) == 0;
    for (uint32_t d = 0; names != 0 && d < count; d += 1)
    {
        free(names[d]);
    }
    free(names);
    free(stored);
    free(now);
    free(looks);
    return result;
}

// An entry's variant, the name of its file among its key's entries: SHA-256 of its flags and of what its run looked at,
// each probe with the run's roots made alike (as in its key) and its digest. A run that finds what another found writes
// over that one's entry, and one that finds something else writes one of its own.
static OracleKey oracle_entry_variant(const OracleRun* run, const OracleRecords* probes, uint32_t flags)
{
    OracleHash hash;
    oracle_sha256_begin(&hash);
    oracle_hash_text(&hash, "buster oracle variant 1");
    oracle_hash_bytes(&hash, &flags, sizeof(flags));
    for (uint32_t i = 0; i < probes->count; i += 1)
    {
        oracle_hash_rooted(&hash, &run->roots, probes->records[i].path);
        oracle_hash_bytes(&hash, probes->records[i].digest, BUSTER_ORACLE_KEY_BYTES);
    }
    return oracle_hash_end(&hash);
}

// The entry the run would take: the first of its key's entries, in the order its directory lists them, that fits the
// run and whose run looked at what this one finds now (oracle_entry_inputs), into `entry`, with its variant in
// `*variant`. False if none does, with nothing in `entry` to release.
static bool oracle_entry_find(const OracleRun* run, OracleEntry* entry, OracleKey* variant)
{
    char directory[BUSTER_ORACLE_PATH_BYTES + 80];
    oracle_key_directory(run->cache, run->key, directory, sizeof(directory));
    DIR* listing = opendir(directory);
    bool result = false;
    *entry = (OracleEntry){ 0 };
    for (struct dirent* name = listing != 0 ? readdir(listing) : 0; name != 0 && !result; name = readdir(listing))
    {
        char path[BUSTER_ORACLE_PATH_BYTES + 160];
        const bool named = oracle_digest_named(name->d_name) && (size_t)snprintf(path, sizeof(path), "%s/%s", directory, name->d_name) < sizeof(path);
        result = named && oracle_entry_read(path, run->key, entry) && oracle_entry_fits(entry, run) && oracle_entry_inputs(entry, run);
        if (result)
        {
            *variant = oracle_digest_parse(name->d_name);
        }
        else
        {
            oracle_entry_release(entry);
        }
    }
    if (listing != 0)
    {
        closedir(listing);
    }
    return result;
}

// A key's entry and when it was written, for oracle_key_trim.
typedef struct OracleAge OracleAge;
struct OracleAge
{
    int64_t seconds;
    int64_t nanoseconds;
    char name[2 * BUSTER_ORACLE_KEY_BYTES + 1];
};

static int oracle_compare_ages(const void* a, const void* b)
{
    const OracleAge* x = a;
    const OracleAge* y = b;
    return x->seconds != y->seconds           ? (x->seconds < y->seconds ? -1 : 1)
           : x->nanoseconds != y->nanoseconds ? (x->nanoseconds < y->nanoseconds ? -1 : 1)
                                              : strcmp(x->name, y->name);
}

// Removes the oldest of the entries in a key's `directory` past BUSTER_ORACLE_VARIANTS, by modification time, but the
// one named `kept`, just written.
static void oracle_key_trim(const char* directory, const char* kept)
{
    OracleAge ages[2 * BUSTER_ORACLE_VARIANTS];
    uint32_t count = 0;
    uint32_t entries = 0;
    DIR* listing = opendir(directory);
    for (struct dirent* name = listing != 0 ? readdir(listing) : 0; name != 0; name = readdir(listing))
    {
        struct stat status;
        const bool entry = oracle_digest_named(name->d_name) && fstatat(dirfd(listing), name->d_name, &status, AT_SYMLINK_NOFOLLOW) == 0;
        entries += entry;
        if (entry && strcmp(name->d_name, kept) != 0 && count < 2 * BUSTER_ORACLE_VARIANTS)
        {
            ages[count] = (OracleAge){ .seconds = (int64_t)status.st_mtim.tv_sec, .nanoseconds = (int64_t)status.st_mtim.tv_nsec };
            memcpy(ages[count].name, name->d_name, sizeof(ages[count].name));
            count += 1;
        }
    }
    if (count > 1)
    {
        qsort(ages, count, sizeof(OracleAge), oracle_compare_ages);
    }
    const uint32_t past = entries > BUSTER_ORACLE_VARIANTS ? entries - BUSTER_ORACLE_VARIANTS : 0;
    for (uint32_t i = 0; listing != 0 && i < past && i < count; i += 1)
    {
        unlinkat(dirfd(listing), ages[i].name, 0);
    }
    if (listing != 0)
    {
        closedir(listing);
    }
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

// Writes the run's entry of `variant` (oracle_entry_variant) among its key's, into a file of its own renamed over its
// name, then trims the key's (oracle_key_trim): false if it couldn't.
static bool oracle_entry_write(const OracleRun* run, const OracleRecords* probes, uint32_t flags, const OracleFile* files, OracleKey variant)
{
    OracleBuffer body = { 0 };
    for (uint32_t r = 0; r < run->roots.count; r += 1)
    {
        oracle_buffer_u32(&body, run->roots.roots[r].length);
        oracle_buffer_put(&body, run->roots.roots[r].name, run->roots.roots[r].length);
    }
    for (uint32_t i = 0; i < probes->count; i += 1)
    {
        const uint32_t length = (uint32_t)strlen(probes->records[i].path);
        oracle_buffer_u32(&body, length);
        oracle_buffer_put(&body, probes->records[i].path, length);
        oracle_buffer_put(&body, probes->records[i].digest, BUSTER_ORACLE_KEY_BYTES);
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
        .dependencies = probes->count,
        .flags = flags,
        .files = run->file_count,
        .body = body.length,
    };
    memcpy(header.magic, oracle_magic, sizeof(oracle_magic));
    memcpy(header.key, run->key.bytes, sizeof(run->key.bytes));
    char directory[BUSTER_ORACLE_PATH_BYTES + 80];
    char path[BUSTER_ORACLE_PATH_BYTES + 160];
    char temporary[BUSTER_ORACLE_PATH_BYTES + 240];
    oracle_key_directory(run->cache, run->key, directory, sizeof(directory));
    oracle_entry_path(run->cache, run->key, variant, path, sizeof(path));
    static uint64_t written;
    written += 1;
    snprintf(temporary, sizeof(temporary), "%s.%ld.%llu.new", path, (long)getpid(), (unsigned long long)written);
    const bool made = mkdir(directory, 0700) == 0 || errno == EEXIST;
    const int fd = body.failed || !made ? -1 : open(temporary, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
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
    if (result)
    {
        char name[2 * BUSTER_ORACLE_KEY_BYTES + 1];
        oracle_digest_name(variant, name);
        oracle_key_trim(directory, name);
    }
    free(body.bytes);
    return result;
}

// --- Keeping and taking ---

// The tools whose __TIMESTAMP__ this process found to follow SOURCE_DATE_EPOCH or not, by SHA-256 of their identity
// (oracle_hash_tool's).
#define BUSTER_ORACLE_EPOCH_TOOLS 16
static OracleKey oracle_epoch_tools[BUSTER_ORACLE_EPOCH_TOOLS];
static bool oracle_epoch_follows[BUSTER_ORACLE_EPOCH_TOOLS];
static uint32_t oracle_epoch_count;

// Whether the run's tool's __TIMESTAMP__ follows SOURCE_DATE_EPOCH, as clang's does (with __DATE__ and __TIME__): GCC's
// is the source's modification time whatever the epoch, and so is it for the runs of a tool whose isn't the epoch's,
// whose entries then have their files' times (BUSTER_ORACLE_TIMES). Its run of `-E -P -x c -` on `__TIMESTAMP__` with the
// epoch 0, which must print the epoch's time, outside the cache, once per tool in a process: a process that keeps
// compilers' runs pays one more run.
static bool oracle_tool_epoch(const OracleRun* run)
{
    OracleHash hash;
    oracle_sha256_begin(&hash);
    char found[BUSTER_ORACLE_PATH_BYTES];
    const bool identified = oracle_hash_tool(&hash, run->arguments[0], found, sizeof(found));
    const OracleKey tool = oracle_hash_end(&hash);
    uint32_t place = oracle_epoch_count;
    for (uint32_t t = 0; t < oracle_epoch_count && place == oracle_epoch_count; t += 1)
    {
        place = memcmp(oracle_epoch_tools[t].bytes, tool.bytes, sizeof(tool.bytes)) == 0 ? t : place;
    }
    bool follows = place < oracle_epoch_count && oracle_epoch_follows[place];
    if (identified && place == oracle_epoch_count)
    {
        char input[BUSTER_ORACLE_PATH_BYTES];
        const int length = snprintf(input, sizeof(input), "%s/buster-oracle-epoch-XXXXXX", run->roots.base_length != 0 ? run->roots.base : "/tmp");
        const int descriptor = length > 0 && (size_t)length < sizeof(input) ? mkstemp(input) : -1;
        static const char source[] = "__TIMESTAMP__\n";
        const bool written = descriptor >= 0 && write(descriptor, source, sizeof(source) - 1) == (ssize_t)(sizeof(source) - 1);
        if (descriptor >= 0)
        {
            close(descriptor);
        }
        char* arguments[] = { found, "-E", "-P", "-x", "c", "-", 0 };
        char* environment[] = { "SOURCE_DATE_EPOCH=0", "TZ=UTC0", "LC_ALL=C", 0 };
        OracleRun probe = {
            .cache = run->cache,
            .arguments = arguments,
            .environment = environment,
            .input = input,
            .timeout = 60,
        };
        oracle_run_reset(&probe);
        const bool spawned = written && oracle_run_spawn(&probe, false);
        const bool ended = oracle_run_end(&probe);
        follows = spawned && ended && WIFEXITED(probe.status) && WEXITSTATUS(probe.status) == 0 && strstr(probe.output, "Thu Jan  1 00:00:00 1970") != 0;
        oracle_run_close(&probe);
        free(probe.output);
        free(probe.errors);
        if (descriptor >= 0)
        {
            unlink(input);
        }
        if (oracle_epoch_count < BUSTER_ORACLE_EPOCH_TOOLS)
        {
            oracle_epoch_tools[oracle_epoch_count] = tool;
            oracle_epoch_follows[oracle_epoch_count] = follows;
            oracle_epoch_count += 1;
        }
    }
    return follows;
}

// A probe's place among the run's looks, for their order by probe.
typedef struct OraclePlace OraclePlace;
struct OraclePlace
{
    const char* probe;
    uint32_t place;
};

static int oracle_compare_places(const void* a, const void* b)
{
    return strcmp(((const OraclePlace*)a)->probe, ((const OraclePlace*)b)->probe);
}

// What the run looked at, as its entry lists it, into `probes`: its first looks, by probe, the files it writes among
// them as they were before it (their bytes read at their looks), each with its digest (oracle_probe_digests). False if a
// file it read isn't the one it found (the run changed it), or a list can't be made.
static bool oracle_run_probes(OracleRun* run, bool times, OracleRecords* probes)
{
    const OracleLooks* looks = &run->looks;
    bool result = !looks->failed;
    OraclePlace* places = result ? calloc((size_t)looks->count + 1, sizeof(OraclePlace)) : 0;
    char** names = result ? calloc((size_t)looks->count + 1, sizeof(char*)) : 0;
    OracleLook* chosen = result ? calloc((size_t)looks->count + 1, sizeof(OracleLook)) : 0;
    uint8_t* digests = result ? malloc(((size_t)looks->count + 1) * BUSTER_ORACLE_KEY_BYTES) : 0;
    result = result && places != 0 && names != 0 && chosen != 0 && digests != 0;
    for (uint32_t i = 0; result && i < looks->count; i += 1)
    {
        places[i] = (OraclePlace){ .probe = looks->probes[i], .place = i };
    }
    if (result && looks->count > 1)
    {
        qsort(places, looks->count, sizeof(OraclePlace), oracle_compare_places);
    }
    const uint32_t count = result ? looks->count : 0;
    for (uint32_t i = 0; i < count; i += 1)
    {
        names[i] = (char*)places[i].probe;
        chosen[i] = looks->looks[places[i].place];
    }
    result = result && oracle_probe_digests(names, chosen, count, times, digests);
    for (uint32_t i = 0; result && i < count; i += 1)
    {
        oracle_records_add(probes, names[i], strlen(names[i]), digests + (size_t)i * BUSTER_ORACLE_KEY_BYTES);
        result = !probes->failed;
    }
    free(places);
    free(names);
    free(chosen);
    free(digests);
    return result;
}

// After a run that ran: what it looked at and wrote, and its entry written (its variant's, oracle_entry_variant); in
// check mode compared first with the entry it would have taken (OracleRun.checked). A traced run (a compiler's) is kept
// only if its tracing missed nothing. False if that entry is stale, with a line on the errors naming it.
static bool oracle_run_keep(OracleRun* run)
{
    OracleRecords probes = { 0 };
    bool keep = !run->dependencies || (run->listener >= 0 && !run->untraced);
    const uint32_t flags = keep && run->dependencies && !oracle_tool_epoch(run) ? BUSTER_ORACLE_TIMES : 0;
    keep = keep && (!run->dependencies || oracle_run_probes(run, (flags & BUSTER_ORACLE_TIMES) != 0, &probes));
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
    char checked[BUSTER_ORACLE_PATH_BYTES + 160];
    oracle_entry_path(run->cache, run->key, run->checked, checked, sizeof(checked));
    if (keep && run->checking && oracle_entry_read(checked, run->key, &entry))
    {
        if (oracle_entry_fits(&entry, run))
        {
            oracle_entry_root(&entry, run);
            differs = oracle_entry_differs(&entry, run, files);
        }
        oracle_entry_release(&entry);
    }
    if (differs != 0)
    {
        fprintf(stderr, "oracle cache: the entry %s (%s) is stale: %s differs from the run's:", checked, run->purpose != 0 ? run->purpose : "", differs);
        for (uint32_t a = 0; run->arguments[a] != 0; a += 1)
        {
            fprintf(stderr, " %s", run->arguments[a]);
        }
        fprintf(stderr, "%s%s\n", run->input != 0 ? " < " : "", run->input != 0 ? run->input : "");
        oracle_count(&run->cache->counts->stale);
        // Removed: the run's own entry, written next, may be another variant, which wouldn't replace it (a file it
        // names apart each time, by its pid, is among what it looked at).
        unlink(checked);
    }
    const bool written = keep && oracle_entry_write(run, &probes, flags, files, oracle_entry_variant(run, &probes, flags));
    oracle_count(written ? &run->cache->counts->run : &run->cache->counts->unkept);
    for (uint32_t f = 0; f < run->file_count; f += 1)
    {
        free(files[f].bytes.bytes);
    }
    oracle_records_release(&probes);
    return differs == 0;
}

// In use mode, before a run: the result of the entry it would take (oracle_entry_find), if one of its key's entries
// fits the run and what that entry's run looked at is as it is now, with the files it writes written (true: the tool
// doesn't run).
static bool oracle_run_take(OracleRun* run)
{
    OracleEntry entry;
    OracleKey variant;
    bool result = oracle_entry_find(run, &entry, &variant);
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

// Starts a run: in use mode takes its entry if it can (OracleRun.taken), in check mode finds the entry it would have
// taken (OracleRun.checked), and otherwise starts its tool, traced if it is a compiler's run the cache may keep and
// this process traces fewer than BUSTER_ORACLE_TRACED runs (else it isn't kept). False if the tool can't start;
// oracle_run_finish then finishes it all the same.
static bool oracle_run_start(OracleRun* run)
{
    oracle_run_reset(run);
    run->keyed = oracle_run_key(run);
    run->taken = run->keyed && run->cache->mode == ORACLE_MODE_USE && oracle_run_take(run);
    if (run->keyed && run->cache->mode == ORACLE_MODE_CHECK)
    {
        OracleEntry entry;
        run->checking = oracle_entry_find(run, &entry, &run->checked);
        oracle_entry_release(&entry);
    }
    oracle_traced_own();
    const bool room = oracle_traced_count < BUSTER_ORACLE_TRACED;
    if (run->taken)
    {
        oracle_count(&run->cache->counts->taken);
    }
    else if (oracle_cache_active(run->cache) && (!run->keyed || (run->dependencies && !room)))
    {
        oracle_count(&run->cache->counts->unkept);
        run->keyed = false;
    }
    return run->taken || oracle_run_spawn(run, run->keyed && run->dependencies);
}

// Finishes a run: waits for its tool (oracle_run_wait), reads what it wrote, and keeps its entry. False if the tool
// didn't start or can't be waited for, was killed for its time, its output can't be read, or (in check mode) its entry
// is stale; the result is the tool's.
static bool oracle_run_finish(OracleRun* run)
{
    bool result = run->taken;
    if (!run->taken)
    {
        result = oracle_run_end(run);
        if (run->keyed && !result)
        {
            oracle_count(&run->cache->counts->unkept);
        }
        result = result && (!run->keyed || oracle_run_keep(run));
    }
    oracle_run_close(run);
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

// A synchronous test tool's exit status, or -1 for a launcher, timeout, capture or cache-check failure. Optional
// capture files receive the complete bytes; diagnostics receives a bounded NUL-terminated prefix. Declared output
// artifacts and dependency tracing are the caller's OracleRun contract. Generated programs must still run directly.
static int oracle_test_run(OracleRun* run, const char* output_path, const char* error_path, char* diagnostics, size_t capacity)
{
    const bool ran = oracle_run(run);
    bool written = true;
    const char* paths[] = { output_path, error_path };
    const char* bytes[] = { run->output, run->errors };
    const uint64_t lengths[] = { run->output_length, run->errors_length };
    for (uint32_t capture = 0; capture < 2; capture += 1)
    {
        if (paths[capture] != 0)
        {
            FILE* file = fopen(paths[capture], "wb");
            if (file != 0)
            {
                written = fwrite(bytes[capture] != 0 ? bytes[capture] : "", 1, lengths[capture], file) == lengths[capture] && written;
                written = fclose(file) == 0 && written;
            }
            else
            {
                written = false;
            }
        }
    }
    if (diagnostics != 0 && capacity != 0)
    {
        const uint64_t kept = run->errors_length < capacity - 1 ? run->errors_length : capacity - 1;
        if (kept != 0)
        {
            memcpy(diagnostics, run->errors, kept);
        }
        diagnostics[kept] = 0;
    }
    const int result = ran && written && WIFEXITED(run->status) ? WEXITSTATUS(run->status) : -1;
    oracle_run_release(run);
    return result;
}

#endif
