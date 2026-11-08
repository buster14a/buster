// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

//
// measure-tier1-rows (#1283): vector code by Tier 0 and Tier 1, where machine rows (#141, tier1_emit_cfg_row) run it, with
// the moves Tier 1 writes around its rows counted, as whole processes in a rotating order; with --base, against another
// compiler's --tier1 (the main branch's, for a pull request's numbers against it).
//
//   build/compiler measure-tier1-rows --clang PATH [--base COMPILER] [--src DIR] [--lexer FILE]... [--march ARCH]
//                                     [--rounds N] [--passes N] [--runs N] [--warmups N] [--directory DIR] [--keep]
//                                     [--uncounted]
//
// The programs, each a unit the build compiles (`-c`) and a main by clang -O2 that runs it and prints what it computed:
// - rows: test-checker-driver's machine rows' unit (c_check_tier1_driver_test.c's check_test_tier1_rows_source: x86-64-v4's
//   vector builtins and operators on bytes, words and quadwords), its main its other unit's rounds, --rounds of them
//   (10000), hashed.
// - scan: a lexer's byte classes over a 1 MiB C-like text, 64 bytes at a time (identifier starts, lines, quotes, and a
//   vector carried across the loop's back edge), by vector operators and byte compares into
//   masks, --passes times (200).
// - lexer: the compiler's own lexer (DIR/buster/compiler/c_lexer_benchmark.c, --src's DIR, src by default, as build.sh
//   compiles it but by the build's compiler), as `benchmark-lexer FILE...` runs it on --lexer's files: its corpus check
//   (the vector lexer against the scalar one) and both lexers' timed passes. Left out without --lexer.
// The builds: this compiler's Tier 0 and `--tier1 -fno-asynchronous-unwind-tables`, --base's --tier1 (left out without
// --base) and clang -O2, each at -march=ARCH (native by default; the rows' unit needs x86-64-v4, the lexer's every feature
// of host_isa.h's vector host). Tier 1's builds print their --tier1-report: each program's records, machine rows and the
// moves around them (machine_rows and row_moves, which a compiler before #1283 doesn't count: then `none`). Before any
// timing, each program's output must be the same by every build (the lexer's: its corpus line, its tokens' count and its
// exit status 0, which says the vector lexer agreed with the scalar one), and the lexer's c_tokenize median of that run is
// printed.
//
// Then --runs rounds (11), after --warmups (1) in which every build runs every program once, as measure-tier1-programs
// times its programs (profile_time_one, in an order that rotates by round, pinned where the caller pins this process): a
// `tier1-rows-run` line per run, then every pair's medians, then a table by program and metric of the builds' medians
// and --tier1 against Tier 0 and against --base's. DIR is a new directory under --directory ($TMPDIR, or /tmp), removed
// at the end unless --keep.

#define BUSTER_TIER1_ROWS_BUILDS 4
#define BUSTER_TIER1_ROWS_PROGRAMS 3
#define BUSTER_TIER1_ROWS_LEXER_FILES 8
#define BUSTER_TIER1_ROWS_ARGUMENTS 32
#define BUSTER_TIER1_ROWS_MAX_RUNS 1000

typedef enum Tier1RowsBuild
{
    TIER1_ROWS_TIER0,
    TIER1_ROWS_BASE,
    TIER1_ROWS_TIER1,
    TIER1_ROWS_CLANG,
} Tier1RowsBuild;

typedef enum Tier1RowsProgram
{
    TIER1_ROWS_ROWS,
    TIER1_ROWS_SCAN,
    TIER1_ROWS_LEXER,
} Tier1RowsProgram;

static const char* const tier1_rows_build_names[BUSTER_TIER1_ROWS_BUILDS] = { "Tier 0", "--tier1 (base)", "--tier1", "clang -O2" };
static const char* const tier1_rows_build_files[BUSTER_TIER1_ROWS_BUILDS] = { "tier0", "base", "tier1", "clang2" };
static const char* const tier1_rows_program_names[BUSTER_TIER1_ROWS_PROGRAMS] = { "rows", "scan", "lexer" };

// The scan's unit: a lexer's byte classes, 64 bytes at a time, by vector operators on bytes and byte compares into masks
// (machine rows, #141), with a vector carried across the loop's back edge. k holds its constants, each byte splatted:
// 'a', 25, '0', 9, '_', '\n', 0x20 and '"'.
static const char tier1_rows_scan_source[] =
    "typedef char v64i8 __attribute__((vector_size(64)));\n"
    "typedef unsigned long long u64;\n"
    "u64 scan(const v64i8* blocks, unsigned count, const v64i8* k, u64* counts)\n"
    "{\n"
    "    u64 idents = counts[0];\n"
    "    u64 lines = counts[1];\n"
    "    u64 quotes = counts[2];\n"
    "    u64 carry = 0;\n"
    "    v64i8 acc = k[0] ^ k[0];\n"
    "    for (unsigned i = 0; i < count; i += 1)\n"
    "    {\n"
    "        const v64i8 v = blocks[i];\n"
    "        const v64i8 folded = v | k[6];\n"
    "        const u64 letters = __builtin_ia32_ucmpb512_mask(folded - k[0], k[1], 2, (u64)-1);\n"
    "        const u64 digits = __builtin_ia32_ucmpb512_mask(v - k[2], k[3], 2, (u64)-1);\n"
    "        const u64 under = __builtin_ia32_cmpb512_mask(v, k[4], 0, (u64)-1);\n"
    "        const u64 ident = letters | digits | under;\n"
    "        const u64 starts = ident & ~((ident << 1) | carry);\n"
    "        carry = ident >> 63;\n"
    "        idents += (u64)__builtin_popcountll(starts);\n"
    "        lines += (u64)__builtin_popcountll(__builtin_ia32_cmpb512_mask(v, k[5], 0, (u64)-1));\n"
    "        quotes += (u64)__builtin_popcountll(__builtin_ia32_cmpb512_mask(v, k[7], 0, (u64)-1));\n"
    "        acc = (acc + (v & k[6])) ^ (folded - acc);\n"
    "    }\n"
    "    counts[0] = idents;\n"
    "    counts[1] = lines;\n"
    "    counts[2] = quotes;\n"
    "    return __builtin_ia32_cmpb512_mask(acc, k[0], 4, (u64)-1) ^ __builtin_ia32_cmpb512_mask(acc, k[6], 6, (u64)-1);\n"
    "}\n";

// The scan's main: the text (a C-like line, varied by its position), then the passes, then the counts.
static const char tier1_rows_scan_main[] =
    "#include <stdio.h>\n"
    "#include <stdlib.h>\n"
    "#include <string.h>\n"
    "typedef char v64i8 __attribute__((vector_size(64)));\n"
    "typedef unsigned long long u64;\n"
    "u64 scan(const v64i8* blocks, unsigned count, const v64i8* k, u64* counts);\n"
    "int main(int argc, char** argv)\n"
    "{\n"
    "    const unsigned passes = argc > 1 ? (unsigned)strtoul(argv[1], 0, 10) : 1;\n"
    "    const unsigned long bytes = 1ul << 20;\n"
    "    unsigned char* text = aligned_alloc(64, bytes);\n"
    "    static const char line[] = \"    total_count += lookup(table, \\\"key_%u\\\", 42) * factor_7;\\n\";\n"
    "    for (unsigned long i = 0; i < bytes; i += 1) text[i] = (unsigned char)(line[i % (sizeof(line) - 1)] + ((i / 977) % 3 == 0 && line[i % (sizeof(line) - 1)] == 'a' ? 1 : 0));\n"
    "    static const unsigned char splats[8] = { 'a', 25, '0', 9, '_', '\\n', 0x20, '\"' };\n"
    "    v64i8 k[8];\n"
    "    for (unsigned j = 0; j < 8; j += 1) memset(&k[j], splats[j], 64);\n"
    "    u64 counts[3] = { 0, 0, 0 };\n"
    "    u64 last = 0;\n"
    "    for (unsigned pass = 0; pass < passes; pass += 1) last = last * 31 + scan((const v64i8*)text, (unsigned)(bytes / 64), k, counts);\n"
    "    printf(\"%llu %llu %llu %016llx\\n\", counts[0], counts[1], counts[2], last);\n"
    "    free(text);\n"
    "    return 0;\n"
    "}\n";

// The rows' main: the machine rows' fixture's other unit's round, --rounds of them, hashed.
static const char tier1_rows_rows_main[] =
    "#include <stdio.h>\n"
    "#include <stdlib.h>\n"
    "#include <string.h>\n"
    "#include \"tier1-rows.h\"\n"
    "static uint64_t hash = 0xcbf29ce484222325ull;\n"
    "static void mix(const void* p, unsigned n) { const unsigned char* b = p; for (unsigned i = 0; i < n; i += 1) hash = (hash ^ b[i]) * 0x100000001b3ull; }\n"
    "int main(int argc, char** argv)\n"
    "{\n"
    "    const unsigned rounds = argc > 1 ? (unsigned)strtoul(argv[1], 0, 10) : 1;\n"
    "    unsigned char in[512];\n"
    "    unsigned char out[256];\n"
    "    for (unsigned round = 0; round < rounds; round += 1)\n"
    "    {\n"
    "        for (unsigned i = 0; i < sizeof(in); i += 1) in[i] = (unsigned char)(i * 37u + (i >> 3) * 11u + round * 101u + (i % 7 == 0 ? 'a' : 0));\n"
    "        memset(out, 0, sizeof(out));\n"
    "        __m512i a = _mm512_loadu_si512(in);\n"
    "        __m512i b = _mm512_loadu_si512(in + 64);\n"
    "        __m512i c = _mm512_loadu_si512(in + 128);\n"
    "        __m512i r = r_bytes(a, b);\n"
    "        mix(&r, 64);\n"
    "        uint64_t m = r_compares(a, b) ^ r_compares(b, c);\n"
    "        mix(&m, 8);\n"
    "        r = r_masked(a, b, m, (__mmask16)m);\n"
    "        mix(&r, 64);\n"
    "        r = r_permutes(a, b, c);\n"
    "        mix(&r, 64);\n"
    "        r = r_shifts(a, b, 5 + (int)(round & 3));\n"
    "        mix(&r, 64);\n"
    "        r_memory(out, in, m, (__mmask16)(m >> 9));\n"
    "        mix(out, sizeof(out));\n"
    "        r = r_lanes(a, _mm256_loadu_si256((const void*)(in + 200)), _mm_loadu_si128((const void*)(in + 300)), 0x123456789abcll * (round + 1), -77 + (int)(round & 3));\n"
    "        mix(&r, 64);\n"
    "        long long s = r_reduce(a, c);\n"
    "        mix(&s, 8);\n"
    "        __m256i n = r_narrow(_mm256_loadu_si256((const void*)(in + 17)), _mm256_loadu_si256((const void*)(in + 99)), _mm_loadu_si128((const void*)(in + 222)));\n"
    "        mix(&n, 32);\n"
    "        int v = r_vex(_mm256_loadu_si256((const void*)(in + 5)), _mm256_loadu_si256((const void*)(in + 41)), _mm256_loadu_si256((const void*)(in + 77)));\n"
    "        mix(&v, 4);\n"
    "        uint32_t k = r_loop(in, sizeof(in), out);\n"
    "        mix(&k, 4);\n"
    "        mix(out, 64);\n"
    "    }\n"
    "    printf(\"%016llx\\n\", (unsigned long long)hash);\n"
    "    return 0;\n"
    "}\n";

// The lexer's main: benchmark-lexer's entry point, as main.c calls it.
static const char tier1_rows_lexer_main[] = "int run_c_lexer_benchmark(int argc, char** argv);\n"
                                            "int main(int argc, char** argv) { return run_c_lexer_benchmark(argc, argv); }\n";

typedef struct Tier1Rows Tier1Rows;
struct Tier1Rows
{
    const char* clang;
    // Null without --base: its build is left out.
    const char* base;
    const char* src;
    const char* march;
    const char* lexer[BUSTER_TIER1_ROWS_LEXER_FILES];
    uint32_t lexer_count;
    uint32_t rounds;
    uint32_t passes;
    uint32_t runs;
    uint32_t warmups;
    // --uncounted: a run need not be counted (a host without the counters), its cycles and instructions then 0.
    bool uncounted;
    // The builds there are (--tier1 (base)'s only with --base) and the programs (the lexer only with --lexer).
    bool built[BUSTER_TIER1_ROWS_BUILDS];
    uint32_t program_count;
    char directory[PATH_MAX];
    char errors[PATH_MAX];
    char march_option[128];
    char include_option[PATH_MAX + 2];
    char counts[2][32];
    char executables[BUSTER_TIER1_ROWS_BUILDS][BUSTER_TIER1_ROWS_PROGRAMS][PATH_MAX];
    // Each Tier 1 build's report of each program's unit: its records, machine rows and the moves around them, UINT64_MAX
    // where the report has no such field.
    uint64_t records[BUSTER_TIER1_ROWS_BUILDS][BUSTER_TIER1_ROWS_PROGRAMS];
    uint64_t machine_rows[BUSTER_TIER1_ROWS_BUILDS][BUSTER_TIER1_ROWS_PROGRAMS];
    uint64_t row_moves[BUSTER_TIER1_ROWS_BUILDS][BUSTER_TIER1_ROWS_PROGRAMS];
};

// Formats into a buffer of PATH_MAX bytes; false if it doesn't fit.
static bool tier1_rows_path(char* path, const char* format, const char* a, const char* b)
{
    const int length = snprintf(path, PATH_MAX, format, a, b);
    return length > 0 && length < PATH_MAX;
}

// Writes `text` into the file at `path`. Returns whether it was written whole.
static bool tier1_rows_write(const char* path, const char* text)
{
    const size_t length = strlen(text);
    const int file = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    const bool written = file != -1 && write(file, text, length) == (ssize_t)length;
    if (file != -1)
    {
        close(file);
    }
    return written;
}

// The value of `name` in a --tier1-report's line of `text` (its "tier1\tfunctions=" line), UINT64_MAX where it has none.
static uint64_t tier1_rows_field(const char* text, const char* name)
{
    const char* line = text != NULL ? strstr(text, "tier1\tfunctions=") : NULL;
    char key[64];
    snprintf(key, sizeof(key), "\t%s=", name);
    const char* end = line != NULL ? strchr(line, '\n') : NULL;
    const char* field = line != NULL ? strstr(line, key) : NULL;
    const bool found = field != NULL && (end == NULL || field < end);
    return found ? strtoull(field + strlen(key), NULL, 10) : UINT64_MAX;
}

// Build `b`'s compiler and options before a unit's: this executable (with --tier1's for Tier 1's build), --base's
// with --tier1's, or clang -O2; each at -march.
static void tier1_rows_compiler(const Tier1Rows* p, uint32_t b, Tier1ProgramsCommand* c)
{
    c->count = 0;
    c->arguments[0] = NULL;
    tier1_programs_add(c, b == TIER1_ROWS_CLANG ? p->clang : b == TIER1_ROWS_BASE ? p->base : "/proc/self/exe");
    if (b == TIER1_ROWS_CLANG)
    {
        tier1_programs_add(c, "-O2");
    }
    if (b == TIER1_ROWS_BASE || b == TIER1_ROWS_TIER1)
    {
        tier1_programs_add(c, "--tier1");
        tier1_programs_add(c, "--tier1-report");
        tier1_programs_add(c, "-fno-asynchronous-unwind-tables");
    }
    tier1_programs_add(c, p->march_option);
}

// The programs' mains by clang -O2, each an object in the directory. Returns whether each compiled.
static bool tier1_rows_mains(Tier1Rows* p)
{
    bool ok = true;
    for (uint32_t g = 0; ok && g < p->program_count; g += 1)
    {
        char source[PATH_MAX];
        char object[PATH_MAX];
        ok = tier1_rows_path(source, "%s/%s-main.c", p->directory, tier1_rows_program_names[g]) &&
             tier1_rows_path(object, "%s/%s-main.o", p->directory, tier1_rows_program_names[g]);
        Tier1ProgramsCommand c = { 0 };
        tier1_rows_compiler(p, TIER1_ROWS_CLANG, &c);
        tier1_programs_add(&c, "-c");
        tier1_programs_add(&c, "-o");
        tier1_programs_add(&c, object);
        tier1_programs_add(&c, source);
        ok = ok && tier1_programs_spawn(c.arguments, NULL, p->errors);
    }
    return ok;
}

// Build `b`'s object of each program's unit, linked with its main by clang. Tier 1's builds' reports are read into the
// counts. Returns whether every command succeeded.
static bool tier1_rows_build(Arena* arena, Tier1Rows* p, uint32_t b)
{
    bool ok = true;
    for (uint32_t g = 0; ok && g < p->program_count; g += 1)
    {
        const char* name = tier1_rows_program_names[g];
        const char* file = tier1_rows_build_files[b];
        char unit[PATH_MAX];
        char object[PATH_MAX];
        char main_object[PATH_MAX];
        char report[PATH_MAX];
        const int length = snprintf(p->executables[b][g], PATH_MAX, "%s/%s-%s", p->directory, name, file);
        ok = (g == TIER1_ROWS_LEXER ? tier1_rows_path(unit, "%s/%s", p->src, "buster/compiler/c_lexer_benchmark.c") : tier1_rows_path(unit, "%s/%s.c", p->directory, name)) &&
             tier1_rows_path(main_object, "%s/%s-main.o", p->directory, name) && length > 0 && length < PATH_MAX;
        ok = ok && tier1_rows_path(object, "%s.o%s", p->executables[b][g], "") && tier1_rows_path(report, "%s.report%s", p->executables[b][g], "");
        Tier1ProgramsCommand c = { 0 };
        tier1_rows_compiler(p, b, &c);
        if (g == TIER1_ROWS_LEXER)
        {
            // Without BUSTER_EXECUTABLE_UNIT, the unit is linked alone: the lexer's \N names (c_unicode_names.c) are
            // static in it, as c_tokenize is.
            tier1_programs_add(&c, p->include_option);
            tier1_programs_add(&c, "-fwrapv");
            tier1_programs_add(&c, "-funsigned-char");
            tier1_programs_add(&c, "-fno-strict-aliasing");
        }
        tier1_programs_add(&c, "-c");
        tier1_programs_add(&c, "-o");
        tier1_programs_add(&c, object);
        tier1_programs_add(&c, unit);
        // A report of its own: the command's errors, Tier 1's --tier1-report among them, then appended to the errors.
        unlink(report);
        ok = ok && tier1_programs_spawn(c.arguments, NULL, report);
        const uint64_t mark = arena->length;
        const char* text = measure_compile_read(arena, report);
        p->records[b][g] = tier1_rows_field(text, "records");
        p->machine_rows[b][g] = tier1_rows_field(text, "machine_rows");
        p->row_moves[b][g] = tier1_rows_field(text, "row_moves");
        const int errors = open(p->errors, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
        if (errors != -1 && text != NULL)
        {
            const ssize_t written = write(errors, text, strlen(text));
            (void)written;
        }
        if (errors != -1)
        {
            close(errors);
        }
        arena_pop_to(arena, mark);
        // The link, by clang.
        c.count = 0;
        c.arguments[0] = NULL;
        tier1_programs_add(&c, p->clang);
        tier1_programs_add(&c, "-o");
        tier1_programs_add(&c, p->executables[b][g]);
        tier1_programs_add(&c, main_object);
        tier1_programs_add(&c, object);
        ok = ok && tier1_programs_spawn(c.arguments, NULL, p->errors);
    }
    printf("measure-tier1-rows: %s's builds %s\n", tier1_rows_build_names[b], ok ? "built" : "FAILED");
    return ok;
}

// The arguments of program `g` by build `b` into argv (room for BUSTER_TIER1_ROWS_LEXER_FILES + 2).
static void tier1_rows_list(const Tier1Rows* p, uint32_t b, uint32_t g, char** argv)
{
    uint32_t count = 0;
    argv[count++] = (char*)p->executables[b][g];
    if (g == TIER1_ROWS_LEXER)
    {
        for (uint32_t f = 0; f < p->lexer_count; f += 1)
        {
            argv[count++] = (char*)p->lexer[f];
        }
    }
    else
    {
        argv[count++] = (char*)p->counts[g];
    }
    argv[count] = NULL;
}

// Each program's output by every build against clang -O2's: the rows' hash and the scan's counts the same, the lexer's
// corpus line and tokens' count the same and its status 0 (its vector lexer agreed with its scalar one), with its
// c_tokenize median printed. Returns whether they all agree.
static bool tier1_rows_check(Arena* arena, Tier1Rows* p)
{
    char* argv[BUSTER_TIER1_ROWS_LEXER_FILES + 2];
    bool ok = true;
    for (uint32_t g = 0; ok && g < p->program_count; g += 1)
    {
        char outputs[BUSTER_TIER1_ROWS_BUILDS][PATH_MAX];
        for (uint32_t b = 0; ok && b < BUSTER_TIER1_ROWS_BUILDS; b += 1)
        {
            tier1_rows_list(p, b, g, argv);
            ok = !p->built[b] || (tier1_rows_path(outputs[b], "%s.out%s", p->executables[b][g], "") && tier1_programs_spawn(argv, outputs[b], p->errors));
        }
        for (uint32_t b = 0; ok && b < BUSTER_TIER1_ROWS_BUILDS; b += 1)
        {
            const uint64_t mark = arena->length;
            const char* reference = p->built[b] ? measure_compile_read(arena, outputs[TIER1_ROWS_CLANG]) : NULL;
            const char* text = p->built[b] ? measure_compile_read(arena, outputs[b]) : NULL;
            if (g == TIER1_ROWS_LEXER && text != NULL && reference != NULL)
            {
                // Its corpus check's lines, up to the timed passes' (which differ by build), and c_tokenize's median.
                const char* timed = strstr(text, " interleaved passes over ");
                const char* reference_timed = strstr(reference, " interleaved passes over ");
                const char* tokens = timed != NULL ? strstr(timed, " tokens") : NULL;
                const char* reference_tokens = reference_timed != NULL ? strstr(reference_timed, " tokens") : NULL;
                const char* tokenize = timed != NULL ? strstr(timed, "c_tokenize ") : NULL;
                const bool shaped = timed != NULL && reference_timed != NULL && tokens != NULL && reference_tokens != NULL;
                ok = shaped && tokens - text == reference_tokens - reference && memcmp(text, reference, (size_t)(tokens - text)) == 0;
                printf("measure-tier1-rows: lexer by %s: %s; c_tokenize %.*s\n", tier1_rows_build_names[b], ok ? "clang -O2's corpus and tokens" : "DIFFERS (FAILED)",
                       tokenize != NULL ? (int)strcspn(tokenize + 11, "\n") : 0, tokenize != NULL ? tokenize + 11 : "");
            }
            else if (text != NULL && reference != NULL)
            {
                ok = strcmp(text, reference) == 0;
                printf("measure-tier1-rows: %s by %s: %.*s%s\n", tier1_rows_program_names[g], tier1_rows_build_names[b], (int)strcspn(text, "\n"), text, ok ? "" : " (DIFFERS: FAILED)");
            }
            else
            {
                ok = !p->built[b];
            }
            arena_pop_to(arena, mark);
        }
    }
    return ok;
}

// The warm-ups and the runs, every (program, build) pair's medians into `medians` (by program, then build; pairs of builds
// there aren't left at 0). Returns the failed runs.
static uint64_t tier1_rows_time(Arena* arena, Tier1Rows* p, Tier1ProgramsMedians* medians)
{
    // The pairs that run: each program by each build there is.
    uint32_t pairs[BUSTER_TIER1_ROWS_BUILDS * BUSTER_TIER1_ROWS_PROGRAMS];
    uint32_t slots = 0;
    for (uint32_t g = 0; g < p->program_count; g += 1)
    {
        for (uint32_t b = 0; b < BUSTER_TIER1_ROWS_BUILDS; b += 1)
        {
            pairs[slots] = g * BUSTER_TIER1_ROWS_BUILDS + b;
            slots += p->built[b] ? 1 : 0;
        }
    }
    const uint64_t mark = arena->length;
    ProfileTimed* timed = BUSTER_ARENA_PUSH_ARRAY(arena, ProfileTimed, (uint64_t)p->runs * slots + 1);
    uint64_t* values = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, (uint64_t)p->runs + 1);
    char* argv[BUSTER_TIER1_ROWS_LEXER_FILES + 2];
    uint64_t failures = timed == NULL || values == NULL;
    printf("tier1-rows-run\tphase\tround\tposition\tprogram\tbuild\twall_ns\tuser_cycles\tinstructions\tstatus\tcounted\n");
    for (uint64_t run = 0; failures == 0 && run < (uint64_t)p->warmups + p->runs; run += 1)
    {
        const bool warmup = run < p->warmups;
        for (uint64_t position = 0; position < slots; position += 1)
        {
            const uint64_t s = warmup ? position : profile_ab_tree(run - p->warmups, position, slots);
            const uint32_t g = pairs[s] / BUSTER_TIER1_ROWS_BUILDS;
            const uint32_t b = pairs[s] % BUSTER_TIER1_ROWS_BUILDS;
            tier1_rows_list(p, b, g, argv);
            const ProfileTimed one = profile_time_one(argv[0], argv);
            const int status = one.status != -1 && WIFEXITED(one.status) ? WEXITSTATUS(one.status) : -1;
            const bool passed = status == 0 && (one.counted || p->uncounted);
            failures += !passed;
            printf("tier1-rows-run\t%s\t%llu\t%llu\t%s\t%s\t%llu\t%llu\t%llu\t%d\t%d\n", warmup ? "warm-up" : "measured", (unsigned long long)(warmup ? run : run - p->warmups) + 1,
                   (unsigned long long)position + 1, tier1_rows_program_names[g], tier1_rows_build_files[b], (unsigned long long)one.wall_ns, (unsigned long long)one.cycles,
                   (unsigned long long)one.instructions, status, one.counted);
            if (!passed)
            {
                printf("measure-tier1-rows: %s's %s: FAILED (status %d, counted %d)\n", tier1_rows_build_names[b], tier1_rows_program_names[g], status, one.counted);
            }
            if (!warmup)
            {
                timed[(run - p->warmups) * slots + s] = one;
            }
        }
    }
    for (uint64_t s = 0; failures == 0 && s < slots; s += 1)
    {
        Tier1ProgramsMedians* m = &medians[pairs[s]];
        for (uint32_t r = 0; r < p->runs; r += 1)
        {
            values[r] = timed[(uint64_t)r * slots + s].cycles;
        }
        m->cycles = profile_median(values, p->runs);
        for (uint32_t r = 0; r < p->runs; r += 1)
        {
            values[r] = timed[(uint64_t)r * slots + s].instructions;
        }
        m->instructions = profile_median(values, p->runs);
        for (uint32_t r = 0; r < p->runs; r += 1)
        {
            values[r] = timed[(uint64_t)r * slots + s].wall_ns;
        }
        m->wall_ns = profile_median(values, p->runs);
        m->wall_low = values[0];
        m->wall_high = values[p->runs - 1];
    }
    arena_pop_to(arena, mark);
    return failures;
}

// A count of a report, or `none`.
static void tier1_rows_print_count(uint64_t value)
{
    if (value == UINT64_MAX)
    {
        printf(" none |");
    }
    else
    {
        printf(" %llu |", (unsigned long long)value);
    }
}

// Every pair's medians, Tier 1's builds' counts, then the table by program and metric: each build's medians, and --tier1
// against Tier 0 and against --base's.
static void tier1_rows_report(const Tier1Rows* p, const Tier1ProgramsMedians* medians)
{
    for (uint32_t g = 0; g < p->program_count; g += 1)
    {
        for (uint32_t b = 0; b < BUSTER_TIER1_ROWS_BUILDS; b += 1)
        {
            const Tier1ProgramsMedians* m = &medians[g * BUSTER_TIER1_ROWS_BUILDS + b];
            if (p->built[b])
            {
                printf("%s by %s: %u runs, median %.3f ms wall (%.3f to %.3f), %llu user cycles, %llu instructions\n", tier1_rows_program_names[g], tier1_rows_build_names[b], p->runs,
                       (double)m->wall_ns / 1e6, (double)m->wall_low / 1e6, (double)m->wall_high / 1e6, (unsigned long long)m->cycles, (unsigned long long)m->instructions);
            }
        }
    }
    printf("\n| Unit | Build | records | machine_rows | row_moves |\n| --- | --- | ---: | ---: | ---: |\n");
    for (uint32_t g = 0; g < p->program_count; g += 1)
    {
        for (uint32_t b = TIER1_ROWS_BASE; b <= TIER1_ROWS_TIER1; b += 1)
        {
            if (p->built[b])
            {
                printf("| %s | %s |", tier1_rows_program_names[g], tier1_rows_build_names[b]);
                tier1_rows_print_count(p->records[b][g]);
                tier1_rows_print_count(p->machine_rows[b][g]);
                tier1_rows_print_count(p->row_moves[b][g]);
                printf("\n");
            }
        }
    }
    static const char* const metrics[3] = { "ms (median)", "user cycles, M", "instructions, M" };
    printf("\n| Program | ");
    for (uint32_t b = 0; b < BUSTER_TIER1_ROWS_BUILDS; b += 1)
    {
        if (p->built[b])
        {
            printf("%s | ", tier1_rows_build_names[b]);
        }
    }
    printf("`--tier1` / Tier 0 | `--tier1` / base |\n| --- |");
    for (uint32_t b = 0; b < BUSTER_TIER1_ROWS_BUILDS + 2; b += 1)
    {
        printf(b < BUSTER_TIER1_ROWS_BUILDS && !p->built[b] ? "" : " ---: |");
    }
    printf("\n");
    for (uint32_t g = 0; g < p->program_count; g += 1)
    {
        for (uint32_t metric = 0; metric < 3; metric += 1)
        {
            double values[BUSTER_TIER1_ROWS_BUILDS] = { 0 };
            printf("| %s, %s |", tier1_rows_program_names[g], metrics[metric]);
            for (uint32_t b = 0; b < BUSTER_TIER1_ROWS_BUILDS; b += 1)
            {
                const Tier1ProgramsMedians* m = &medians[g * BUSTER_TIER1_ROWS_BUILDS + b];
                values[b] = metric == 0 ? (double)m->wall_ns / 1e6 : metric == 1 ? (double)m->cycles / 1e6 : (double)m->instructions / 1e6;
                if (p->built[b])
                {
                    printf(" %.2f |", values[b]);
                }
            }
            // A metric --uncounted left at 0, or a build left out, has no ratio.
            for (uint32_t r = 0; r < 2; r += 1)
            {
                const uint32_t against = r == 0 ? TIER1_ROWS_TIER0 : TIER1_ROWS_BASE;
                if (p->built[against] && values[against] > 0)
                {
                    printf(" %.3f |", values[TIER1_ROWS_TIER1] / values[against]);
                }
                else
                {
                    printf(" none |");
                }
            }
            printf("\n");
        }
    }
}

static int run_tier1_rows_measure(int argc, char** argv)
{
    Arena arena = arena_reserve(UINT64_C(1) << 32);
    Tier1Rows p = { .src = "src", .march = "native", .rounds = 10000, .passes = 200, .runs = 11, .warmups = 1 };
    const char* parent = oracle_temporary_directory();
    bool keep = false;
    bool valid = arena.base != 0;
    int i = 1;
    while (valid && i < argc)
    {
        uint64_t value = 0;
        const bool pair = i + 1 < argc;
        const bool flag = strcmp(argv[i], "--keep") == 0 || strcmp(argv[i], "--uncounted") == 0;
        if (strcmp(argv[i], "--runs") == 0 && pair && profile_count_option(argv[i + 1], &value) && value <= BUSTER_TIER1_ROWS_MAX_RUNS)
        {
            p.runs = (uint32_t)value;
        }
        else if (strcmp(argv[i], "--warmups") == 0 && pair && profile_decimal(profile_string(argv[i + 1]), &value) && value <= 1000)
        {
            p.warmups = (uint32_t)value;
        }
        else if (strcmp(argv[i], "--rounds") == 0 && pair && profile_count_option(argv[i + 1], &value) && value <= UINT32_MAX)
        {
            p.rounds = (uint32_t)value;
        }
        else if (strcmp(argv[i], "--passes") == 0 && pair && profile_count_option(argv[i + 1], &value) && value <= UINT32_MAX)
        {
            p.passes = (uint32_t)value;
        }
        else if (strcmp(argv[i], "--clang") == 0 && pair)
        {
            p.clang = argv[i + 1];
        }
        else if (strcmp(argv[i], "--base") == 0 && pair)
        {
            p.base = argv[i + 1];
        }
        else if (strcmp(argv[i], "--src") == 0 && pair)
        {
            p.src = argv[i + 1];
        }
        else if (strcmp(argv[i], "--march") == 0 && pair)
        {
            p.march = argv[i + 1];
        }
        else if (strcmp(argv[i], "--lexer") == 0 && pair && p.lexer_count < BUSTER_TIER1_ROWS_LEXER_FILES)
        {
            p.lexer[p.lexer_count] = argv[i + 1];
            p.lexer_count += 1;
        }
        else if (strcmp(argv[i], "--directory") == 0 && pair)
        {
            parent = argv[i + 1];
        }
        else if (strcmp(argv[i], "--keep") == 0)
        {
            keep = true;
        }
        else if (strcmp(argv[i], "--uncounted") == 0)
        {
            p.uncounted = true;
        }
        else
        {
            valid = false;
        }
        i += flag ? 1 : 2;
    }
    valid = valid && p.clang != NULL && snprintf(p.march_option, sizeof(p.march_option), "-march=%s", p.march) < (int)sizeof(p.march_option) &&
            snprintf(p.include_option, sizeof(p.include_option), "-I%s", p.src) < (int)sizeof(p.include_option);
    p.built[TIER1_ROWS_TIER0] = true;
    p.built[TIER1_ROWS_BASE] = p.base != NULL;
    p.built[TIER1_ROWS_TIER1] = true;
    p.built[TIER1_ROWS_CLANG] = true;
    p.program_count = p.lexer_count != 0 ? BUSTER_TIER1_ROWS_PROGRAMS : BUSTER_TIER1_ROWS_PROGRAMS - 1;
    snprintf(p.counts[TIER1_ROWS_ROWS], sizeof(p.counts[TIER1_ROWS_ROWS]), "%u", p.rounds);
    snprintf(p.counts[TIER1_ROWS_SCAN], sizeof(p.counts[TIER1_ROWS_SCAN]), "%u", p.passes);
    const int length = valid ? snprintf(p.directory, sizeof(p.directory), "%s/measure-tier1-rows-XXXXXX", parent) : -1;
    valid = valid && length > 0 && (size_t)length + 64 < sizeof(p.directory);
    const bool made = valid && mkdtemp(p.directory) != NULL;
    int result = 1;
    if (!valid)
    {
        fprintf(stderr, "usage: measure-tier1-rows --clang PATH [--base COMPILER] [--src DIR] [--lexer FILE]... [--march ARCH] [--rounds N] [--passes N] [--runs N] [--warmups N] "
                        "[--directory DIR] [--keep] [--uncounted]\n");
    }
    else if (!made)
    {
        fprintf(stderr, "error: can't make a directory under %s: %s\n", parent, strerror(errno));
    }
    else
    {
        // The units and mains: the machine rows' fixture's unit and header (test-checker-driver's), the scan's, and the
        // lexer's main.
        char path[PATH_MAX];
        bool ok = tier1_rows_path(p.errors, "%s/%s", p.directory, "errors.txt");
        ok = ok && tier1_rows_path(path, "%s/%s", p.directory, "tier1-rows.h") && tier1_rows_write(path, check_test_tier1_rows_header);
        ok = ok && tier1_rows_path(path, "%s/%s", p.directory, "rows.c") && tier1_rows_write(path, check_test_tier1_rows_source);
        ok = ok && tier1_rows_path(path, "%s/%s", p.directory, "rows-main.c") && tier1_rows_write(path, tier1_rows_rows_main);
        ok = ok && tier1_rows_path(path, "%s/%s", p.directory, "scan.c") && tier1_rows_write(path, tier1_rows_scan_source);
        ok = ok && tier1_rows_path(path, "%s/%s", p.directory, "scan-main.c") && tier1_rows_write(path, tier1_rows_scan_main);
        ok = ok && tier1_rows_path(path, "%s/%s", p.directory, "lexer-main.c") && tier1_rows_write(path, tier1_rows_lexer_main);
        printf("measure-tier1-rows: %u warm-ups, %u runs, %u programs, %s, in %s\n", p.warmups, p.runs, p.program_count, p.march_option, p.directory);
        ok = ok && tier1_rows_mains(&p);
        for (uint32_t b = 0; ok && b < BUSTER_TIER1_ROWS_BUILDS; b += 1)
        {
            ok = !p.built[b] || tier1_rows_build(&arena, &p, b);
        }
        ok = ok && tier1_rows_check(&arena, &p);
        Tier1ProgramsMedians medians[BUSTER_TIER1_ROWS_BUILDS * BUSTER_TIER1_ROWS_PROGRAMS] = { 0 };
        ok = ok && tier1_rows_time(&arena, &p, medians) == 0;
        if (ok)
        {
            tier1_rows_report(&p, medians);
        }
        else
        {
            const char* errors = measure_compile_read(&arena, p.errors);
            fprintf(stderr, "measure-tier1-rows: FAILED; the commands' errors:\n%s\n", errors != NULL ? errors : "");
        }
        result = ok ? 0 : 1;
        if (!keep)
        {
            char* remove[] = { "rm", "-rf", p.directory, NULL };
            tier1_programs_spawn(remove, NULL, "/dev/null");
        }
    }
    arena_release(&arena);
    return result;
}
