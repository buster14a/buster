// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The profiling tools: what the lexer's and the encoder's optimization rounds ran around perf and objdump on the 9700X,
// as commands of this executable. docs/profiling.md has the method they serve and the perf and objdump commands that
// make their inputs:
//
//   build/compiler ab [--rounds N] [--cpu N] [--lock PATH] TREE... -- ARGUMENT... [-- ARGUMENT...]...
//   build/compiler hot-loops FUNCTION DISASSEMBLY SAMPLES [--loops N]
//   build/compiler label-spills FUNCTION TAG INPUT.s OUTPUT.s
//   build/compiler spill-report FUNCTION LABELED.s DISASSEMBLY SAMPLES [--slots N]
//   build/compiler branch-misses BRSTACK [--bias ADDRESS] [--disassembly FILE] [--branches N]
//   build/compiler time-runs [--runs N] [--warmups N] [--after-scalar MS] [--copies N] [--directory PATH]
//                            [--executable PATH | --build NAME PATH]... --list ARGUMENT... [--list ARGUMENT...]...
//
// - ab runs `TREE/build/compiler ARGUMENT...` (or TREE itself, when it is a file) for every tree, every argument list
//   and every round, the trees in an order that rotates by round, each run pinned to --cpu and under --lock (the
//   9700X's exclusive.sh), which a CI job can then take between two runs. Comparing builds in separate processes has
//   about 2% of noise on the 9700X, a single round more; variants inside one process (benchmark-lexer's, build.sh
//   lexer-variant) take turns in each pass, and a copy of the reference among them measures what placement alone moves.
// - hot-loops reads `objdump -d --no-show-raw-insn` of the executable and `perf script -F ip,sym,symoff,dsoff` of its
//   samples, finds each sample of the function by its offset in the file (a static function has a copy in every
//   translation unit that includes it, and objdump 2.47's --disassemble=NAME prints only one), takes the copy with the
//   most, and ranks its loops (each backward jump's range) by their share of the samples, then prints the smallest loop
//   with at least half of them, instruction by instruction with its samples.
// - label-spills copies clang's assembly (`clang -S`) with a label before every spill and reload of one function
//   (clang's annotations on them, `# 64-byte Spill` and the others), `buster_spill.TAG.N`, and nothing else changed;
//   spill-report finds the labeled copy and its labels by address in `objdump -d` of the program built from it, and
//   reads its samples (IBS's), `perf script -F ip,sym,symoff,dsoff`, by their offsets in the file: each label's own
//   samples are its spill's or reload's, by stack slot, with the source location of each slot's hottest, against all
//   the copy's samples. Names aren't identities: static functions of the same name in other translation units, a
//   static local's `f.name` and other labeled variants (other tags) don't count.
// - branch-misses reads `perf script -F brstack` of last-branch records (`perf record -j any,u`) and ranks the branches
//   by their mispredictions, at addresses less the load bias (where a PIE was mapped, from --show-mmap-events), so
//   that `addr2line` and objdump of the file take them; with the file's disassembly, each with its instruction.
// - time-runs times whole processes of this executable (or of each --executable, and of each --build's placements),
//   one per argument list, executable and copy in each run, in an order that rotates by run, after warm-up runs in
//   that order: each one's wall time from its exec's release to wait4's return, its user and system time, peak
//   resident set and page faults, from wait4's rusage, and its user cycles and instructions (perf_event_open from its
//   exec, all its threads), every run's line and each one's medians. With --after-scalar MS, an untimed scalar loop
//   runs MS ms before each timed process, on the same CPU, the state a compiler starts in after lighter work. Each
//   process's output goes to /dev/null, and its exit status must be the one its first warm-up had. Each list starts at
//   a `--list` (not `--`, which separates ab's own lists), so that ab, which pins it and takes the lock per run, can
//   run it to compare whole drivers (#335). Comparing builds (#509): the same code runs up to 2% faster or slower from
//   another address (a build's placement, build.sh's `shifts`) and 0.3% from another copy of the same file (other
//   physical pages), so with several builds or --copies N it runs each executable from N copies made fresh in a
//   directory of its own, interleaved with the other builds', and compares the builds by wall time and user cycles,
//   with each one's spread over its placements and copies and each difference's range over them (profile_time_compare).
//
// Each tool's work is a function of its input texts that writes its report into a ProfileReport (profile_hot_loops,
// profile_label_spills, profile_spill_report, profile_branch_misses, and time-runs' profile_time_compare), which
// profile_test.c checks, and ab's order is profile_ab_tree. main.c includes this file after lane.c, for its arenas, and profile_test.c after it.
//
// Layout, in this order: the report (ProfileReport, profile_print), reading text (profile_line and the others), the
// disassembly (ProfileDisassembly, profile_disassembly_read, profile_jump_target), hot-loops, label-spills,
// spill-report, branch-misses, ab (profile_ab_tree, profile_ab_run), time-runs (profile_time_slot, profile_time_one,
// profile_time_compare, profile_time_make_copies, profile_time_runs), and the commands (run_profile_command).

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

// OS-dependent includes
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <signal.h>
#include <time.h>
#include <linux/perf_event.h>

// Every command's arena: the input files and what it builds from them. Its pages are committed only as they are touched.
#define BUSTER_PROFILE_ARENA_CAPACITY (64ull << 30)
// The report's own arena, so that what is printed into it stays one run of bytes.
#define BUSTER_PROFILE_REPORT_CAPACITY (16ull << 30)
// The defaults of hot-loops' --loops, spill-report's --slots and branch-misses' --branches.
#define BUSTER_PROFILE_DEFAULT_LOOPS 6
#define BUSTER_PROFILE_DEFAULT_SLOTS 24
#define BUSTER_PROFILE_DEFAULT_BRANCHES 30
// branch-misses' table of branch addresses starts this large (a power of two) and doubles past half full.
#define BUSTER_PROFILE_BRANCH_TABLE_START 4096
// label-spills' labels: BUSTER_PROFILE_SPILL_LABEL, the tag, '.', then the spill's number (`buster_spill.k1.0`). It
// renames a static function to its name, '.' and the tag (`tokenize_c.k1`), so that its samples are its own: every other
// translation unit that includes its file has a copy of the same name, and so may another labeled variant, of another
// tag.
#define BUSTER_PROFILE_SPILL_LABEL "buster_spill."
// ab's --cpu: sched_setaffinity's mask of 1,024 CPUs, as glibc's cpu_set_t.
#define BUSTER_PROFILE_CPUS 1024
// time-runs' limits on executables and copies, whose numbers name the copies with three digits each, and the buffer it
// copies them through.
#define BUSTER_PROFILE_EXECUTABLES 999
#define BUSTER_PROFILE_COPIES 999
#define BUSTER_PROFILE_COPY_BUFFER (1ull << 20)

// ---------------------------------------------------------------------------------------------------------------------
// The report

typedef struct ProfileReport ProfileReport;
struct ProfileReport
{
    // The text is arena.base, arena.length bytes: nothing else is pushed on it.
    Arena arena;
    // A push failed, so the text is incomplete.
    bool full;
};

static String8 profile_report_text(const ProfileReport* report)
{
    return (String8){ .pointer = (char*)report->arena.base, .length = report->arena.length };
}

static void profile_append(ProfileReport* report, String8 text)
{
    char* bytes = arena_push(&report->arena, text.length, 1);
    if (bytes == 0)
    {
        report->full = true;
    }
    else
    {
        memcpy(bytes, text.pointer, text.length);
    }
}

static __attribute__((format(printf, 2, 3))) void profile_print(ProfileReport* report, const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    va_list copy;
    va_copy(copy, arguments);
    const int length = vsnprintf(0, 0, format, copy);
    va_end(copy);
    // vsnprintf writes a NUL after the text, which the report then drops.
    char* bytes = length >= 0 ? arena_push(&report->arena, (uint64_t)length + 1, 1) : 0;
    if (bytes == 0)
    {
        report->full = true;
    }
    else
    {
        vsnprintf(bytes, (uint64_t)length + 1, format, arguments);
        arena_pop_to(&report->arena, report->arena.length - 1);
    }
    va_end(arguments);
}

// ---------------------------------------------------------------------------------------------------------------------
// Reading text

// The line at *position, without its '\n', and *position past it.
static String8 profile_line(String8 text, uint64_t* position)
{
    const uint64_t start = *position;
    uint64_t end = start;
    while (end < text.length && text.pointer[end] != '\n')
    {
        end += 1;
    }
    *position = end < text.length ? end + 1 : end;
    return (String8){ .pointer = text.pointer + start, .length = end - start };
}

static bool profile_is_blank(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

static String8 profile_trim(String8 text)
{
    uint64_t start = 0;
    uint64_t end = text.length;
    while (start < end && profile_is_blank(text.pointer[start]))
    {
        start += 1;
    }
    while (end > start && profile_is_blank(text.pointer[end - 1]))
    {
        end -= 1;
    }
    return (String8){ .pointer = text.pointer + start, .length = end - start };
}

static String8 profile_slice(String8 text, uint64_t start, uint64_t end)
{
    return (String8){ .pointer = text.pointer + start, .length = end - start };
}

static bool profile_equal(String8 a, String8 b)
{
    return a.length == b.length && memcmp(a.pointer, b.pointer, a.length) == 0;
}

static bool profile_starts_with(String8 text, String8 prefix)
{
    return text.length >= prefix.length && memcmp(text.pointer, prefix.pointer, prefix.length) == 0;
}

// The index of the first `needle` in `text`, or text.length if there is none.
static uint64_t profile_find(String8 text, String8 needle)
{
    uint64_t result = text.length;
    uint64_t i = 0;
    while (result == text.length && needle.length <= text.length && i <= text.length - needle.length)
    {
        if (memcmp(text.pointer + i, needle.pointer, needle.length) == 0)
        {
            result = i;
        }
        i += 1;
    }
    return result;
}

static int32_t profile_hex_digit(char c)
{
    int32_t result = -1;
    if (c >= '0' && c <= '9')
    {
        result = c - '0';
    }
    else if (c >= 'a' && c <= 'f')
    {
        result = c - 'a' + 10;
    }
    else if (c >= 'A' && c <= 'F')
    {
        result = c - 'A' + 10;
    }
    return result;
}

// Reads a hexadecimal number, with or without 0x, at text[*position], and moves *position past it. False, and *position
// unchanged, if there are no digits or there are more than 16.
static bool profile_hex(String8 text, uint64_t* position, uint64_t* value)
{
    uint64_t i = *position;
    if (i + 1 < text.length && text.pointer[i] == '0' && (text.pointer[i + 1] == 'x' || text.pointer[i + 1] == 'X'))
    {
        i += 2;
    }
    const uint64_t first = i;
    uint64_t number = 0;
    while (i < text.length && i - first <= 16 && profile_hex_digit(text.pointer[i]) >= 0)
    {
        number = (number << 4) | (uint64_t)profile_hex_digit(text.pointer[i]);
        i += 1;
    }
    const bool result = i > first && i - first <= 16;
    if (result)
    {
        *value = number;
        *position = i;
    }
    return result;
}

// The whole of `text` as a decimal number that fits 32 bits.
static bool profile_decimal(String8 text, uint64_t* value)
{
    uint64_t number = 0;
    bool result = text.length != 0 && text.length <= 9;
    for (uint64_t i = 0; result && i < text.length; i += 1)
    {
        result = text.pointer[i] >= '0' && text.pointer[i] <= '9';
        number = number * 10 + (uint64_t)(text.pointer[i] - '0');
    }
    if (result)
    {
        *value = number;
    }
    return result;
}

static String8 profile_string(const char* text)
{
    return (String8){ .pointer = (char*)text, .length = strlen(text) };
}

typedef struct ProfileSample ProfileSample;
struct ProfileSample
{
    // perf script's ip field, first on the line.
    uint64_t ip;
    // Its sym and symoff fields, `NAME+0xOFFSET`, after it: empty for another field, as `[unknown]`.
    String8 symbol;
    uint64_t offset;
    // Its dsoff field, `(PATH+0xOFFSET)` last on the line: the file and the offset in it, which the file's LOAD segments
    // map to the addresses objdump gives (equal in GNU ld's executables, not in lld's).
    String8 file;
    uint64_t file_offset;
    bool has_ip;
    bool has_file_offset;
};

// A line of `perf script -F ip,sym,symoff` or `-F ip,sym,symoff,dsoff`.
static ProfileSample profile_sample(String8 line)
{
    ProfileSample result = { 0 };
    uint64_t i = 0;
    while (i < line.length && profile_is_blank(line.pointer[i]))
    {
        i += 1;
    }
    uint64_t end = i;
    while (end < line.length && !profile_is_blank(line.pointer[end]))
    {
        end += 1;
    }
    uint64_t at = i;
    result.has_ip = profile_hex(line, &at, &result.ip) && at == end;
    i = end;
    while (i < line.length && profile_is_blank(line.pointer[i]))
    {
        i += 1;
    }
    end = i;
    while (end < line.length && !profile_is_blank(line.pointer[end]))
    {
        end += 1;
    }
    const String8 field = profile_slice(line, i, end);
    uint64_t plus = field.length;
    for (uint64_t k = 0; k < field.length; k += 1)
    {
        plus = field.pointer[k] == '+' ? k : plus;
    }
    at = plus + 1;
    if (result.has_ip && plus != 0 && plus < field.length && profile_hex(field, &at, &result.offset) && at == field.length)
    {
        result.symbol = profile_slice(field, 0, plus);
    }
    const String8 trimmed = profile_trim(line);
    plus = trimmed.length;
    for (uint64_t k = 0; k < trimmed.length; k += 1)
    {
        plus = trimmed.pointer[k] == '+' ? k : plus;
    }
    at = plus + 1;
    result.has_file_offset = trimmed.length != 0 && trimmed.pointer[trimmed.length - 1] == ')' && plus < trimmed.length &&
                             profile_hex(trimmed, &at, &result.file_offset) && at == trimmed.length - 1;
    uint64_t open = plus < trimmed.length ? plus : 0;
    while (open > 0 && trimmed.pointer[open] != '(')
    {
        open -= 1;
    }
    result.has_file_offset = result.has_file_offset && trimmed.pointer[open] == '(';
    result.file = result.has_file_offset ? profile_slice(trimmed, open + 1, plus) : (String8){ 0 };
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The disassembly

typedef struct ProfileInstruction ProfileInstruction;
struct ProfileInstruction
{
    uint64_t address;
    // Without the comment that objdump writes after '#'.
    String8 text;
    // The index of the symbol the instruction is under.
    uint32_t symbol;
};

typedef struct ProfileSymbol ProfileSymbol;
struct ProfileSymbol
{
    String8 name;
    uint64_t address;
    // Its instructions are first, first + 1, ... up to the next symbol's first.
    uint64_t first;
};

// A LOAD segment of objdump -p's program headers: `size` bytes of the file from `offset` at `address`.
typedef struct ProfileSegment ProfileSegment;
struct ProfileSegment
{
    uint64_t offset;
    uint64_t address;
    uint64_t size;
};

typedef struct ProfileDisassembly ProfileDisassembly;
struct ProfileDisassembly
{
    ProfileInstruction* instructions;
    uint64_t count;
    ProfileSymbol* symbols;
    uint64_t symbol_count;
    // The file objdump read (its first line, `FILE:     file format elf64-x86-64`), its path as perf names it (absolute,
    // with symbolic links resolved: realpath of the file, or the file as written when it is absolute and isn't here),
    // empty if the file is a relative path that doesn't resolve, and its LOAD segments (-p).
    String8 file;
    String8 image;
    ProfileSegment* segments;
    uint64_t segment_count;
    // The symbol table (-t): every symbol, those at the same address as another among them, which the disassembly's
    // headers show only one of. Its `first` is unused.
    ProfileSymbol* table;
    uint64_t table_count;
    // Every instruction is under a symbol, and their addresses increase.
    bool valid;
    // The arena was too small.
    bool full;
};

// Reads objdump -d's text: `ADDRESS <NAME>:` starts a symbol, and `  ADDRESS:\tTEXT` is an instruction. Other lines
// (the file's and the sections' headings, blank lines) are left out. The instruction's text is whatever follows the
// tab, so the disassembly must be --no-show-raw-insn.
static ProfileDisassembly profile_disassembly_read(Arena* arena, String8 text)
{
    ProfileDisassembly result = { .valid = true };
    uint64_t lines = 0;
    uint64_t position = 0;
    while (position < text.length)
    {
        profile_line(text, &position);
        lines += 1;
    }
    result.instructions = BUSTER_ARENA_PUSH_ARRAY(arena, ProfileInstruction, lines);
    result.symbols = BUSTER_ARENA_PUSH_ARRAY(arena, ProfileSymbol, lines);
    result.segments = BUSTER_ARENA_PUSH_ARRAY(arena, ProfileSegment, lines);
    result.table = BUSTER_ARENA_PUSH_ARRAY(arena, ProfileSymbol, lines);
    result.full = lines != 0 && (result.instructions == 0 || result.symbols == 0 || result.segments == 0 || result.table == 0);
    // A LOAD line's `filesz` is on the line after it. The symbol table runs from `SYMBOL TABLE:` to a blank line.
    bool segment_pending = false;
    bool in_table = false;
    position = 0;
    while (!result.full && position < text.length)
    {
        const String8 line = profile_line(text, &position);
        const String8 trimmed = profile_trim(line);
        uint64_t table_at = 0;
        uint64_t table_address = 0;
        if (in_table && profile_hex(line, &table_at, &table_address) && table_at == 16)
        {
            uint64_t name_start = trimmed.length;
            while (name_start > 0 && !profile_is_blank(trimmed.pointer[name_start - 1]))
            {
                name_start -= 1;
            }
            result.table[result.table_count] = (ProfileSymbol){
                .name = profile_slice(trimmed, name_start, trimmed.length),
                .address = table_address,
            };
            result.table_count += 1;
        }
        in_table = profile_equal(trimmed, profile_string("SYMBOL TABLE:")) || (in_table && trimmed.length != 0);
        const uint64_t format = profile_find(line, profile_string(":     file format "));
        if (result.file.length == 0 && format != line.length && format != 0)
        {
            result.file = profile_slice(line, 0, format);
        }
        uint64_t at = profile_find(trimmed, profile_string("filesz ")) + 7;
        if (segment_pending && at <= trimmed.length && profile_hex(trimmed, &at, &result.segments[result.segment_count].size))
        {
            result.segment_count += 1;
        }
        segment_pending = false;
        if (profile_starts_with(trimmed, profile_string("LOAD off ")))
        {
            const uint64_t address_at = profile_find(trimmed, profile_string("vaddr "));
            uint64_t offset_at = 9;
            while (offset_at < trimmed.length && profile_is_blank(trimmed.pointer[offset_at]))
            {
                offset_at += 1;
            }
            uint64_t vaddr_at = address_at + 6;
            segment_pending = address_at != trimmed.length && profile_hex(trimmed, &offset_at, &result.segments[result.segment_count].offset) &&
                              profile_hex(trimmed, &vaddr_at, &result.segments[result.segment_count].address);
        }
        uint64_t i = 0;
        while (i < line.length && profile_is_blank(line.pointer[i]))
        {
            i += 1;
        }
        const bool indented = i != 0;
        uint64_t address = 0;
        if (profile_hex(line, &i, &address))
        {
            if (!indented && i + 2 < line.length && line.pointer[i] == ' ' && line.pointer[i + 1] == '<' && line.pointer[line.length - 1] == ':' &&
                line.pointer[line.length - 2] == '>')
            {
                result.symbols[result.symbol_count] = (ProfileSymbol){
                    .name = profile_slice(line, i + 2, line.length - 2),
                    .address = address,
                    .first = result.count,
                };
                result.symbol_count += 1;
            }
            else if (indented && i + 1 < line.length && line.pointer[i] == ':' && line.pointer[i + 1] == '\t')
            {
                String8 instruction = profile_slice(line, i + 2, line.length);
                const uint64_t comment = profile_find(instruction, profile_string("#"));
                instruction = profile_trim(profile_slice(instruction, 0, comment));
                const bool ordered = result.count == 0 || result.instructions[result.count - 1].address < address;
                result.valid = result.valid && result.symbol_count != 0 && ordered;
                if (instruction.length != 0)
                {
                    result.instructions[result.count] = (ProfileInstruction){
                        .address = address,
                        .text = instruction,
                        .symbol = result.symbol_count != 0 ? (uint32_t)(result.symbol_count - 1) : 0,
                    };
                    result.count += 1;
                }
            }
        }
    }
    // The image's path, which perf's dsoff field gives: two trees' build/compiler share a name, not a path.
    char* name = result.file.length != 0 ? arena_push(arena, result.file.length + 1, 1) : 0;
    if (name != 0)
    {
        memcpy(name, result.file.pointer, result.file.length);
        name[result.file.length] = 0;
        char* resolved = realpath(name, 0);
        const uint64_t length = resolved != 0 ? strlen(resolved) : 0;
        char* copy = resolved != 0 ? arena_push(arena, length, 1) : 0;
        if (copy != 0)
        {
            memcpy(copy, resolved, length);
            result.image = (String8){ .pointer = copy, .length = length };
        }
        else if (name[0] == '/')
        {
            result.image = result.file;
        }
        free(resolved);
    }
    return result;
}

// Whether a sample is in the disassembly's file (by its path: another tree's build/compiler isn't this one), and its
// address there, through the LOAD segment that maps its offset in the file. A shared library's offsets are its own,
// whatever they coincide with.
static bool profile_same_file(const ProfileDisassembly* disassembly, const ProfileSample* sample)
{
    return sample->has_file_offset && disassembly->image.length != 0 && profile_equal(sample->file, disassembly->image);
}

static bool profile_sample_address(const ProfileDisassembly* disassembly, const ProfileSample* sample, uint64_t* address)
{
    bool result = false;
    const bool same_file = profile_same_file(disassembly, sample);
    for (uint64_t i = 0; same_file && i < disassembly->segment_count; i += 1)
    {
        const ProfileSegment* segment = &disassembly->segments[i];
        if (!result && sample->file_offset >= segment->offset && sample->file_offset - segment->offset < segment->size)
        {
            *address = sample->file_offset - segment->offset + segment->address;
            result = true;
        }
    }
    return result;
}

// Whether the disassembly's file has a path and LOAD segments, which profile_sample_address needs.
static bool profile_has_image(const ProfileDisassembly* disassembly)
{
    return disassembly->image.length != 0 && disassembly->segment_count != 0;
}

// The index of the last instruction at or before `address` among instructions[first, end), or `end` if there is none.
static uint64_t profile_instruction_at(const ProfileDisassembly* disassembly, uint64_t first, uint64_t end, uint64_t address)
{
    uint64_t low = first;
    uint64_t high = end;
    while (low < high)
    {
        const uint64_t middle = low + (high - low) / 2;
        if (disassembly->instructions[middle].address <= address)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    return low > first ? low - 1 : end;
}

// The last symbol of `symbol`'s function: label-spills' labels after it are inside it.
static uint64_t profile_symbol_last(const ProfileDisassembly* disassembly, uint64_t symbol)
{
    uint64_t last = symbol;
    while (last + 1 < disassembly->symbol_count && profile_starts_with(disassembly->symbols[last + 1].name, profile_string(BUSTER_PROFILE_SPILL_LABEL)))
    {
        last += 1;
    }
    return last;
}

// The end of symbol `symbol`'s instructions, with its labels'.
static uint64_t profile_symbol_end(const ProfileDisassembly* disassembly, uint64_t symbol)
{
    const uint64_t last = profile_symbol_last(disassembly, symbol);
    return last + 1 < disassembly->symbol_count ? disassembly->symbols[last + 1].first : disassembly->count;
}

// Whether `address` is in symbol `symbol`'s function: from its address to the next symbol's that isn't one of its
// labels, or for the last symbol to the end of its last instruction, which is at most 15 bytes long.
static bool profile_symbol_contains(const ProfileDisassembly* disassembly, uint64_t symbol, uint64_t address)
{
    const uint64_t last = profile_symbol_last(disassembly, symbol);
    const uint64_t end = profile_symbol_end(disassembly, symbol);
    const uint64_t first = disassembly->symbols[symbol].first;
    uint64_t limit = disassembly->symbols[symbol].address;
    if (last + 1 < disassembly->symbol_count)
    {
        limit = disassembly->symbols[last + 1].address;
    }
    else if (end != first)
    {
        limit = disassembly->instructions[end - 1].address + 15;
    }
    return address >= disassembly->symbols[symbol].address && address < limit;
}

// A direct jump's target: a mnemonic that starts with 'j', blanks, then a hexadecimal address (objdump writes
// `jne    30ff8f <x86_encode_64+0x6bf>`). Indirect jumps have none.
static bool profile_jump_target(String8 text, uint64_t* target)
{
    bool result = false;
    if (text.length != 0 && text.pointer[0] == 'j')
    {
        uint64_t i = 0;
        while (i < text.length && !profile_is_blank(text.pointer[i]))
        {
            i += 1;
        }
        while (i < text.length && profile_is_blank(text.pointer[i]))
        {
            i += 1;
        }
        result = profile_hex(text, &i, target) && (i == text.length || profile_is_blank(text.pointer[i]));
    }
    return result;
}

static bool profile_mentions_stack(String8 text)
{
    return profile_find(text, profile_string("rsp")) != text.length || profile_find(text, profile_string("rbp")) != text.length;
}

// ---------------------------------------------------------------------------------------------------------------------
// hot-loops

typedef struct ProfileLoop ProfileLoop;
struct ProfileLoop
{
    // Instructions [first, last] of the disassembly: from the jump's target to the jump.
    uint64_t first;
    uint64_t last;
    uint64_t samples;
    bool reported;
};

static void profile_print_loop(ProfileReport* report, const ProfileDisassembly* disassembly, const ProfileLoop* loop, uint64_t total)
{
    uint64_t stack = 0;
    for (uint64_t i = loop->first; i <= loop->last; i += 1)
    {
        stack += profile_mentions_stack(disassembly->instructions[i].text);
    }
    profile_print(report, "  %llx-%llx: %llu instructions, %llu stack references, %.1f%% of the samples\n",
                  (unsigned long long)disassembly->instructions[loop->first].address, (unsigned long long)disassembly->instructions[loop->last].address,
                  (unsigned long long)(loop->last - loop->first + 1), (unsigned long long)stack, total != 0 ? 100.0 * (double)loop->samples / (double)total : 0.0);
}

// Ranks the loops of `function` by their samples. Returns false, with the reason in the report, if the function isn't
// in the disassembly or the inputs can't be read.
static bool profile_hot_loops(Arena* arena, ProfileReport* report, String8 function, String8 disassembly_text, String8 samples_text, uint64_t loop_count)
{
    bool result = false;
    const ProfileDisassembly disassembly = profile_disassembly_read(arena, disassembly_text);
    // The function's copies (a static function in several translation units), and each one's samples.
    uint64_t* copies = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, disassembly.symbol_count + 1);
    uint64_t* copy_samples = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, disassembly.symbol_count + 1);
    uint64_t copy_count = 0;
    for (uint64_t i = 0; copies != 0 && i < disassembly.symbol_count; i += 1)
    {
        if (profile_equal(disassembly.symbols[i].name, function))
        {
            copies[copy_count] = i;
            copy_samples[copy_count] = 0;
            copy_count += 1;
        }
    }
    // Each sample's copy, by its address: its offset in the file, through the file's segments. Names aren't identities
    // (a copy the disassembly doesn't have, as objdump --disassemble=NAME may print one, or a shared library's function
    // of the name).
    uint64_t without_offset = 0;
    uint64_t outside = 0;
    uint64_t position = 0;
    while (copy_samples != 0 && copy_count != 0 && position < samples_text.length)
    {
        const ProfileSample sample = profile_sample(profile_line(samples_text, &position));
        uint64_t address = 0;
        const bool here = profile_sample_address(&disassembly, &sample, &address);
        uint64_t copy = copy_count;
        for (uint64_t c = 0; here && c < copy_count; c += 1)
        {
            copy = profile_symbol_contains(&disassembly, copies[c], address) ? c : copy;
        }
        if (copy != copy_count)
        {
            copy_samples[copy] += 1;
        }
        const bool named = profile_equal(sample.symbol, function);
        without_offset += named && !sample.has_file_offset;
        outside += named && profile_same_file(&disassembly, &sample) && copy == copy_count;
    }
    uint64_t busiest = 0;
    uint64_t all = 0;
    for (uint64_t c = 0; c < copy_count; c += 1)
    {
        busiest = copy_samples[c] > copy_samples[busiest] ? c : busiest;
        all += copy_samples[c];
    }
    if (disassembly.full || copies == 0 || copy_samples == 0)
    {
        profile_print(report, "hot-loops: out of memory\n");
    }
    else if (!disassembly.valid)
    {
        profile_print(report, "hot-loops: the disassembly has an instruction before its first symbol, or out of address order\n");
    }
    else if (copy_count == 0)
    {
        profile_print(report, "hot-loops: %.*s isn't in the disassembly\n", (int)function.length, function.pointer);
    }
    else if (!profile_has_image(&disassembly))
    {
        profile_print(report, "hot-loops: the disassembly needs its file's LOAD segments (objdump -d -p) and path (absolute, or found from here)\n");
    }
    else if (without_offset != 0)
    {
        profile_print(report, "hot-loops: %llu samples of %.*s have no offset in the file (perf script's dsoff field)\n", (unsigned long long)without_offset,
                      (int)function.length, function.pointer);
    }
    else
    {
        const uint64_t symbol = copies[busiest];
        const uint64_t first = disassembly.symbols[symbol].first;
        const uint64_t end = profile_symbol_end(&disassembly, symbol);
        const uint64_t count = end - first;
        // counts[i] is instruction first + i's samples, and prefix[i] those of the instructions before it.
        uint64_t* counts = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, count + 1);
        uint64_t* prefix = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, count + 1);
        ProfileLoop* loops = BUSTER_ARENA_PUSH_ARRAY(arena, ProfileLoop, count + 1);
        if (counts == 0 || prefix == 0 || loops == 0)
        {
            profile_print(report, "hot-loops: out of memory\n");
        }
        else
        {
            memset(counts, 0, (count + 1) * sizeof(uint64_t));
            uint64_t total = 0;
            position = 0;
            while (position < samples_text.length)
            {
                const ProfileSample sample = profile_sample(profile_line(samples_text, &position));
                uint64_t address = 0;
                const bool here = profile_sample_address(&disassembly, &sample, &address);
                const uint64_t index = here ? profile_instruction_at(&disassembly, first, end, address) : end;
                const bool counted = index != end && profile_symbol_contains(&disassembly, symbol, address);
                if (counted)
                {
                    counts[index - first] += 1;
                    total += 1;
                }
            }
            prefix[0] = 0;
            for (uint64_t i = 0; i < count; i += 1)
            {
                prefix[i + 1] = prefix[i] + counts[i];
            }

            uint64_t loop_total = 0;
            for (uint64_t i = first; i < end; i += 1)
            {
                uint64_t target = 0;
                if (profile_jump_target(disassembly.instructions[i].text, &target) && target <= disassembly.instructions[i].address &&
                    target >= disassembly.symbols[symbol].address)
                {
                    // A target inside an instruction (which objdump never prints for clang's code) starts the loop at the next.
                    const uint64_t at = profile_instruction_at(&disassembly, first, end, target);
                    const uint64_t loop_first = at == end || disassembly.instructions[at].address == target ? at : at + 1;
                    if (loop_first != end)
                    {
                        loops[loop_total] = (ProfileLoop){
                            .first = loop_first,
                            .last = i,
                            .samples = prefix[i + 1 - first] - prefix[loop_first - first],
                        };
                        loop_total += 1;
                    }
                }
            }

            profile_print(report, "hot-loops: %llu samples in %.*s (%llu instructions, %llu loops)", (unsigned long long)total, (int)function.length,
                          function.pointer, (unsigned long long)count, (unsigned long long)loop_total);
            if (copy_count > 1)
            {
                profile_print(report, ", the copy at %llx, the busiest of %llu (%llu samples in the others)", (unsigned long long)disassembly.symbols[symbol].address,
                              (unsigned long long)copy_count, (unsigned long long)(all - copy_samples[busiest]));
            }
            if (outside != 0)
            {
                profile_print(report, ", %llu samples named %.*s outside its copies", (unsigned long long)outside, (int)function.length, function.pointer);
            }
            profile_print(report, "\nloops by samples:\n");
            // The top loop_count, by selection: each round takes the most sampled loop not reported yet.
            for (uint64_t round = 0; round < loop_count && round < loop_total; round += 1)
            {
                uint64_t best = loop_total;
                for (uint64_t l = 0; l < loop_total; l += 1)
                {
                    const bool better = best == loop_total || loops[l].samples > loops[best].samples ||
                                        (loops[l].samples == loops[best].samples && loops[l].last - loops[l].first < loops[best].last - loops[best].first);
                    best = !loops[l].reported && better ? l : best;
                }
                loops[best].reported = true;
                profile_print_loop(report, &disassembly, &loops[best], total);
            }

            // Enclosing loops tie with the hot loop, so the one to read is the smallest with at least half the samples.
            uint64_t hot = loop_total;
            for (uint64_t l = 0; l < loop_total; l += 1)
            {
                const bool enough = total != 0 && 2 * loops[l].samples >= total;
                const bool smaller = hot == loop_total || loops[l].last - loops[l].first < loops[hot].last - loops[hot].first;
                hot = enough && smaller ? l : hot;
            }
            if (hot == loop_total)
            {
                profile_print(report, "no loop has half the samples\n");
            }
            else
            {
                profile_print(report, "the smallest loop with half the samples:\n");
                profile_print_loop(report, &disassembly, &loops[hot], total);
                for (uint64_t i = loops[hot].first; i <= loops[hot].last; i += 1)
                {
                    const ProfileInstruction* instruction = &disassembly.instructions[i];
                    profile_print(report, "  %8llu  %llx  %.*s\n", (unsigned long long)counts[i - first], (unsigned long long)instruction->address,
                                  (int)instruction->text.length, instruction->text.pointer);
                }
            }
            result = !report->full;
        }
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// label-spills

// Whether line is `name:`, a function's label in clang's assembly, alone or with a comment (clang's `# @name`). Its end
// is the next line that starts `.Lfunc_end`.
static bool profile_is_label(String8 line, String8 name)
{
    const bool labeled = line.length > name.length && profile_starts_with(line, name) && line.pointer[name.length] == ':';
    const String8 rest = labeled ? profile_trim(profile_slice(line, name.length + 1, line.length)) : line;
    return labeled && (rest.length == 0 || rest.pointer[0] == '#');
}

typedef enum ProfileSpillKind
{
    PROFILE_SPILL_NONE,
    PROFILE_SPILL_SPILL,
    PROFILE_SPILL_RELOAD,
} ProfileSpillKind;

// Whether a line of clang's assembly is a spill or a reload: an instruction (not a directive, a label or a comment)
// whose comment is clang's annotation, `# 64-byte Spill`, `# 8-byte Reload`, `# 4-byte Folded Spill` or
// `# 8-byte Folded Reload`, and nothing else. Other comments, as the source paths on .loc directives, may hold the words.
static ProfileSpillKind profile_spill_kind(String8 line)
{
    ProfileSpillKind result = PROFILE_SPILL_NONE;
    const uint64_t comment = profile_find(line, profile_string("#"));
    const String8 instruction = profile_trim(profile_slice(line, 0, comment));
    const bool is_instruction = comment != line.length && instruction.length != 0 && instruction.pointer[0] != '.' &&
                                instruction.pointer[instruction.length - 1] != ':';
    String8 text = profile_trim(profile_slice(line, comment == line.length ? comment : comment + 1, line.length));
    uint64_t digits = 0;
    while (digits < text.length && text.pointer[digits] >= '0' && text.pointer[digits] <= '9')
    {
        digits += 1;
    }
    text = profile_slice(text, digits, text.length);
    if (is_instruction && digits != 0 && profile_starts_with(text, profile_string("-byte ")))
    {
        text = profile_slice(text, 6, text.length);
        if (profile_starts_with(text, profile_string("Folded ")))
        {
            text = profile_slice(text, 7, text.length);
        }
        if (profile_equal(text, profile_string("Spill")))
        {
            result = PROFILE_SPILL_SPILL;
        }
        else if (profile_equal(text, profile_string("Reload")))
        {
            result = PROFILE_SPILL_RELOAD;
        }
    }
    return result;
}

static bool profile_is_symbol_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '$';
}

// Whether `tag` can name a labeled copy: letters, digits and '_', so that it ends at the '.' after it.
static bool profile_valid_tag(String8 tag)
{
    bool result = tag.length != 0;
    for (uint64_t i = 0; i < tag.length; i += 1)
    {
        result = result && profile_is_symbol_char(tag.pointer[i]) && tag.pointer[i] != '.' && tag.pointer[i] != '$';
    }
    return result;
}

// Copies clang's assembly with a label (BUSTER_PROFILE_SPILL_LABEL, the tag and a number) before each spill and reload
// of `function`, numbered from 0 in order, and nothing else changed. Returns false, with the reason in the report, if
// the tag isn't valid or the function isn't in the assembly; the output is then empty.
static bool profile_label_spills(ProfileReport* output, ProfileReport* report, String8 function, String8 tag, String8 assembly)
{
    const bool valid = profile_valid_tag(tag);
    uint64_t labels = 0;
    bool found = false;
    bool inside = false;
    uint64_t position = 0;
    while (valid && position < assembly.length)
    {
        const uint64_t start = position;
        const String8 line = profile_line(assembly, &position);
        if (profile_is_label(line, function))
        {
            found = true;
            inside = true;
        }
        else if (inside && profile_starts_with(line, profile_string(".Lfunc_end")))
        {
            inside = false;
        }
        else if (inside && profile_spill_kind(line) != PROFILE_SPILL_NONE)
        {
            profile_print(output, BUSTER_PROFILE_SPILL_LABEL "%.*s.%llu:\n", (int)tag.length, tag.pointer, (unsigned long long)labels);
            labels += 1;
        }
        profile_append(output, profile_slice(assembly, start, position));
    }
    const bool result = found && !output->full;
    if (!valid)
    {
        profile_print(report, "label-spills: the tag %.*s isn't letters, digits and '_'\n", (int)tag.length, tag.pointer);
    }
    else if (!found)
    {
        output->arena.length = 0;
        profile_print(report, "label-spills: %.*s isn't in the assembly\n", (int)function.length, function.pointer);
    }
    else
    {
        profile_print(report, "label-spills: %llu spills and reloads labeled in %.*s\n", (unsigned long long)labels, (int)function.length, function.pointer);
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// spill-report

typedef struct ProfileSpill ProfileSpill;
struct ProfileSpill
{
    // `zmm` for a 512-bit register, `ymm` and `xmm`, or `gpr`, then the stack operand, as `64(%rsp)`.
    String8 width;
    String8 slot;
    // The source location of the last .loc before it (clang's comment on it), or empty.
    String8 location;
    uint64_t samples;
    bool reload;
};

typedef struct ProfileSlot ProfileSlot;
struct ProfileSlot
{
    String8 width;
    String8 slot;
    uint64_t spill_samples;
    uint64_t reload_samples;
    // Its spill and its reload with the most samples, or the count of spills if none.
    uint64_t hottest_spill;
    uint64_t hottest_reload;
    bool reported;
};

static String8 profile_spill_width(String8 instruction)
{
    String8 result = profile_string("gpr");
    if (profile_find(instruction, profile_string("%zmm")) != instruction.length)
    {
        result = profile_string("zmm");
    }
    else if (profile_find(instruction, profile_string("%ymm")) != instruction.length)
    {
        result = profile_string("ymm");
    }
    else if (profile_find(instruction, profile_string("%xmm")) != instruction.length)
    {
        result = profile_string("xmm");
    }
    else if (profile_find(instruction, profile_string("%k")) != instruction.length)
    {
        result = profile_string("k");
    }
    return result;
}

// The stack operand, `-64(%rsp)`, of a spill's instruction, or `?` without one.
static String8 profile_spill_slot(String8 instruction)
{
    String8 result = profile_string("?");
    uint64_t end = profile_find(instruction, profile_string("(%rsp)"));
    if (end == instruction.length)
    {
        end = profile_find(instruction, profile_string("(%rbp)"));
    }
    if (end != instruction.length)
    {
        uint64_t start = end;
        while (start > 0 && ((instruction.pointer[start - 1] >= '0' && instruction.pointer[start - 1] <= '9') || instruction.pointer[start - 1] == '-'))
        {
            start -= 1;
        }
        result = profile_slice(instruction, start, end + 6);
    }
    return result;
}

// A source location as clang's .loc comment gives it, each path without its directories: `same.c:2701:5 @[ same.c:3336:12 ]`.
static void profile_print_location(ProfileReport* report, const char* what, const ProfileSpill* spills, uint64_t spill)
{
    const String8 location = spills[spill].location;
    profile_print(report, "  %s %llu at ", what, (unsigned long long)spills[spill].samples);
    uint64_t start = 0;
    for (uint64_t i = 0; i < location.length; i += 1)
    {
        if (location.pointer[i] == '/')
        {
            start = i + 1;
        }
        else if (profile_is_blank(location.pointer[i]))
        {
            profile_append(report, profile_slice(location, start, i + 1));
            start = i + 1;
        }
    }
    profile_append(report, profile_slice(location, start, location.length));
}

// Attributes samples to the spills and reloads of `function` in `labeled` (label-spills' output) and ranks their stack
// slots. The labeled copy and its labels are found by address, in `disassembly_text` (objdump -d of the executable
// built from it), and the samples (perf script -F ip,sym,symoff,dsoff) by their offsets in the file: a name isn't an
// identity, since another translation unit may have a static function of the same name, the function may have a
// static local `function.name`, and another labeled variant has its own labels. The copy runs from the function's
// symbol before its first label to the next symbol that isn't its label.
static bool profile_spill_report(Arena* arena, ProfileReport* report, String8 function, String8 labeled, String8 disassembly_text, String8 samples_text,
                                 uint64_t slot_count)
{
    bool result = false;
    uint64_t lines = 0;
    uint64_t position = 0;
    while (position < labeled.length)
    {
        profile_line(labeled, &position);
        lines += 1;
    }
    ProfileSpill* spills = BUSTER_ARENA_PUSH_ARRAY(arena, ProfileSpill, lines + 1);
    ProfileSlot* slots = BUSTER_ARENA_PUSH_ARRAY(arena, ProfileSlot, lines + 1);
    uint64_t* addresses = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, lines + 1);
    // The labels' prefix, BUSTER_PROFILE_SPILL_LABEL, the tag and '.', with room for a number after it.
    char* label_bytes = arena_push(arena, lines + sizeof(BUSTER_PROFILE_SPILL_LABEL) + 24, 1);
    const ProfileDisassembly disassembly = profile_disassembly_read(arena, disassembly_text);
    if (spills == 0 || slots == 0 || addresses == 0 || label_bytes == 0 || disassembly.full)
    {
        profile_print(report, "spill-report: out of memory\n");
    }
    else
    {
        // The labels, in order: label n is spill n, with the instruction on the line after it. The tag is the first's.
        uint64_t spill_count = 0;
        bool found = false;
        bool inside = false;
        bool numbered = true;
        bool pending = false;
        String8 label = { 0 };
        String8 location = { 0 };
        position = 0;
        while (position < labeled.length)
        {
            const String8 line = profile_line(labeled, &position);
            const String8 trimmed = profile_trim(line);
            if (profile_is_label(line, function))
            {
                found = true;
                inside = true;
            }
            else if (inside && profile_starts_with(line, profile_string(".Lfunc_end")))
            {
                inside = false;
            }
            else if (inside && profile_starts_with(line, profile_string(BUSTER_PROFILE_SPILL_LABEL)))
            {
                if (label.length == 0)
                {
                    const String8 after = profile_slice(line, sizeof(BUSTER_PROFILE_SPILL_LABEL) - 1, line.length);
                    const uint64_t length = sizeof(BUSTER_PROFILE_SPILL_LABEL) + profile_find(after, profile_string("."));
                    memcpy(label_bytes, line.pointer, length < line.length ? length : line.length);
                    label = (String8){ .pointer = label_bytes, .length = length < line.length ? length : 0 };
                }
                uint64_t number = 0;
                const bool ours = label.length != 0 && profile_starts_with(line, label) && line.pointer[line.length - 1] == ':';
                numbered = numbered && ours && profile_decimal(profile_slice(line, label.length, line.length - 1), &number) && number == spill_count;
                pending = true;
            }
            else if (inside && profile_starts_with(trimmed, profile_string(".loc")))
            {
                const uint64_t comment = profile_find(trimmed, profile_string("#"));
                location = comment != trimmed.length ? profile_trim(profile_slice(trimmed, comment + 1, trimmed.length)) : profile_trim(profile_slice(trimmed, 4, trimmed.length));
            }
            else if (inside && pending && trimmed.length != 0)
            {
                spills[spill_count] = (ProfileSpill){
                    .width = profile_spill_width(trimmed),
                    .slot = profile_spill_slot(trimmed),
                    .location = location.length != 0 ? location : profile_string("-"),
                    .reload = profile_spill_kind(line) == PROFILE_SPILL_RELOAD,
                };
                spill_count += 1;
                pending = false;
            }
        }

        // Each label's address, from the disassembly's symbols; a tag's labels must be there once each.
        uint64_t label_count = 0;
        uint64_t repeated = 0;
        for (uint64_t n = 0; n < spill_count; n += 1)
        {
            addresses[n] = UINT64_MAX;
        }
        // The headers, then the symbol table (-t), which also has a label at its function's address, as a spill that is
        // the first instruction's.
        for (uint64_t i = 0; label.length != 0 && i < disassembly.symbol_count + disassembly.table_count; i += 1)
        {
            const ProfileSymbol* entry = i < disassembly.symbol_count ? &disassembly.symbols[i] : &disassembly.table[i - disassembly.symbol_count];
            uint64_t number = 0;
            if (profile_starts_with(entry->name, label) && profile_decimal(profile_slice(entry->name, label.length, entry->name.length), &number) &&
                number < spill_count)
            {
                repeated += addresses[number] != UINT64_MAX && addresses[number] != entry->address;
                label_count += addresses[number] == UINT64_MAX;
                addresses[number] = entry->address;
            }
        }
        bool ordered = true;
        for (uint64_t n = 1; n < spill_count; n += 1)
        {
            ordered = ordered && addresses[n - 1] < addresses[n];
        }
        // The copy: the function's symbol at or before the first label, up to the next symbol that isn't a label of its.
        uint64_t start = UINT64_MAX;
        uint64_t end = UINT64_MAX;
        for (uint64_t i = 0; spill_count != 0 && i < disassembly.symbol_count; i += 1)
        {
            const ProfileSymbol* symbol = &disassembly.symbols[i];
            const bool candidate = profile_equal(symbol->name, function) && symbol->address <= addresses[0];
            start = candidate && (start == UINT64_MAX || symbol->address > start) ? symbol->address : start;
        }
        for (uint64_t i = 0; start != UINT64_MAX && i < disassembly.symbol_count; i += 1)
        {
            const ProfileSymbol* symbol = &disassembly.symbols[i];
            // Only its labels: a later function of the same name (another unit's static one) is another function.
            const bool own = profile_starts_with(symbol->name, label);
            end = !own && symbol->address > start && symbol->address < end ? symbol->address : end;
        }
        if (end == UINT64_MAX && start != UINT64_MAX && disassembly.count != 0)
        {
            end = disassembly.instructions[disassembly.count - 1].address + 15;
        }

        if (!found)
        {
            profile_print(report, "spill-report: %.*s isn't in the assembly\n", (int)function.length, function.pointer);
        }
        else if (!numbered || pending)
        {
            profile_print(report, "spill-report: the labels aren't label-spills' (numbered from 0 in order, each before an instruction)\n");
        }
        else if (spill_count == 0)
        {
            profile_print(report, "spill-report: %.*s has no spills or reloads\n", (int)function.length, function.pointer);
        }
        else if (!disassembly.valid)
        {
            profile_print(report, "spill-report: the disassembly has an instruction before its first symbol, or out of address order\n");
        }
        else if (repeated != 0 || label_count != spill_count)
        {
            profile_print(report, "spill-report: the disassembly (objdump -d -p -t) has %llu of the %llu labels %.*s0 to %.*s%llu, %llu at two addresses\n", (unsigned long long)label_count,
                          (unsigned long long)spill_count, (int)label.length, label.pointer, (int)label.length, label.pointer, (unsigned long long)(spill_count - 1),
                          (unsigned long long)repeated);
        }
        else if (!ordered)
        {
            profile_print(report, "spill-report: the labels are out of address order in the disassembly\n");
        }
        else if (start == UINT64_MAX)
        {
            profile_print(report, "spill-report: no %.*s in the disassembly before %.*s0\n", (int)function.length, function.pointer, (int)label.length, label.pointer);
        }
        else if (!profile_has_image(&disassembly))
        {
            profile_print(report, "spill-report: the disassembly needs its file's LOAD segments (objdump -d -p) and path (absolute, or found from here)\n");
        }
        else
        {
            // Samples in the copy, by their offsets in the file, and at each label's address: the instruction after it.
            uint64_t total = 0;
            uint64_t at_spills = 0;
            uint64_t at_reloads = 0;
            uint64_t without_offset = 0;
            position = 0;
            while (position < samples_text.length)
            {
                const ProfileSample sample = profile_sample(profile_line(samples_text, &position));
                const bool named = sample.symbol.length != 0;
                without_offset += named && !sample.has_file_offset;
                uint64_t address = 0;
                const bool here = profile_sample_address(&disassembly, &sample, &address);
                const bool in_copy = here && address >= start && address < end;
                total += in_copy;
                // The label at or before the sample, by binary search.
                uint64_t low = 0;
                uint64_t high = spill_count;
                while (in_copy && low < high)
                {
                    const uint64_t middle = low + (high - low) / 2;
                    low = addresses[middle] <= address ? middle + 1 : low;
                    high = addresses[middle] <= address ? high : middle;
                }
                if (in_copy && low != 0 && addresses[low - 1] == address)
                {
                    spills[low - 1].samples += 1;
                    at_reloads += spills[low - 1].reload;
                    at_spills += !spills[low - 1].reload;
                }
            }

            if (without_offset != 0)
            {
                profile_print(report, "spill-report: %llu samples have no offset in the file (perf script's dsoff field)\n", (unsigned long long)without_offset);
            }
            else
            {
                uint64_t used = 0;
                for (uint64_t s = 0; s < spill_count; s += 1)
                {
                    uint64_t slot = used;
                    for (uint64_t t = 0; t < used; t += 1)
                    {
                        slot = slot == used && profile_equal(slots[t].width, spills[s].width) && profile_equal(slots[t].slot, spills[s].slot) ? t : slot;
                    }
                    if (slot == used)
                    {
                        slots[used] = (ProfileSlot){
                            .width = spills[s].width,
                            .slot = spills[s].slot,
                            .hottest_spill = spill_count,
                            .hottest_reload = spill_count,
                        };
                        used += 1;
                    }
                    uint64_t* hottest = spills[s].reload ? &slots[slot].hottest_reload : &slots[slot].hottest_spill;
                    if (*hottest == spill_count || spills[s].samples > spills[*hottest].samples)
                    {
                        *hottest = s;
                    }
                    slots[slot].reload_samples += spills[s].reload ? spills[s].samples : 0;
                    slots[slot].spill_samples += spills[s].reload ? 0 : spills[s].samples;
                }

                profile_print(report, "spill-report: %llu samples in %.*s at %llx, %llu (%.2f%%) at its %llu spills and reloads: %llu spills, %llu reloads, in %llu slots\n",
                              (unsigned long long)total, (int)function.length, function.pointer, (unsigned long long)start, (unsigned long long)(at_spills + at_reloads),
                              total != 0 ? 100.0 * (double)(at_spills + at_reloads) / (double)total : 0.0, (unsigned long long)spill_count,
                              (unsigned long long)at_spills, (unsigned long long)at_reloads, (unsigned long long)used);
                for (uint64_t round = 0; round < slot_count && round < used; round += 1)
                {
                    uint64_t best = used;
                    for (uint64_t t = 0; t < used; t += 1)
                    {
                        const uint64_t samples = slots[t].spill_samples + slots[t].reload_samples;
                        const bool better = best == used || samples > slots[best].spill_samples + slots[best].reload_samples;
                        best = !slots[t].reported && better ? t : best;
                    }
                    const ProfileSlot* slot = &slots[best];
                    slots[best].reported = true;
                    profile_print(report, "%-4.*s %-12.*s spills %6llu reloads %6llu", (int)slot->width.length, slot->width.pointer, (int)slot->slot.length,
                                  slot->slot.pointer, (unsigned long long)slot->spill_samples, (unsigned long long)slot->reload_samples);
                    // The hottest of each kind, if it has samples.
                    if (slot->hottest_spill != spill_count && spills[slot->hottest_spill].samples != 0)
                    {
                        profile_print_location(report, "| hottest spill", spills, slot->hottest_spill);
                    }
                    if (slot->hottest_reload != spill_count && spills[slot->hottest_reload].samples != 0)
                    {
                        profile_print_location(report, "| hottest reload", spills, slot->hottest_reload);
                    }
                    profile_print(report, "\n");
                }
                result = !report->full;
            }
        }
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// branch-misses

typedef struct ProfileBranch ProfileBranch;
struct ProfileBranch
{
    // The branch's address as recorded, 0 in an empty entry.
    uint64_t address;
    uint64_t records;
    uint64_t mispredicted;
    bool reported;
};

typedef struct ProfileBranchTable ProfileBranchTable;
struct ProfileBranchTable
{
    ProfileBranch* entries;
    // A power of two.
    uint64_t capacity;
    uint64_t count;
};

static uint64_t profile_branch_slot(const ProfileBranchTable* table, uint64_t address)
{
    uint64_t slot = (address * 0x9e3779b97f4a7c15ull >> 32) & (table->capacity - 1);
    while (table->entries[slot].address != 0 && table->entries[slot].address != address)
    {
        slot = (slot + 1) & (table->capacity - 1);
    }
    return slot;
}

// An empty table of `capacity` entries, or one without entries if the arena is too small.
static ProfileBranchTable profile_branch_table(Arena* arena, uint64_t capacity)
{
    ProfileBranchTable result = { .entries = BUSTER_ARENA_PUSH_ARRAY(arena, ProfileBranch, capacity), .capacity = capacity };
    if (result.entries != 0)
    {
        memset(result.entries, 0, capacity * sizeof(ProfileBranch));
    }
    return result;
}

// Doubles the table into new memory. False, and the table as it was, if the arena is too small.
static bool profile_branch_grow(Arena* arena, ProfileBranchTable* table)
{
    ProfileBranchTable grown = profile_branch_table(arena, table->capacity * 2);
    const bool result = grown.entries != 0;
    if (result)
    {
        for (uint64_t i = 0; i < table->capacity; i += 1)
        {
            if (table->entries[i].address != 0)
            {
                grown.entries[profile_branch_slot(&grown, table->entries[i].address)] = table->entries[i];
            }
        }
        grown.count = table->count;
        *table = grown;
    }
    return result;
}

// One brstack record, `0xFROM/0xTO/FLAG/...`: the branch's address and whether it was mispredicted. False for a field
// that isn't one, or whose branch is at 0.
static bool profile_brstack_record(String8 field, uint64_t* from, bool* mispredicted)
{
    uint64_t at = 0;
    uint64_t to = 0;
    bool result = profile_hex(field, &at, from) && at < field.length && field.pointer[at] == '/';
    at += 1;
    result = result && profile_hex(field, &at, &to) && at + 1 < field.length && field.pointer[at] == '/' && *from != 0;
    *mispredicted = result && field.pointer[at + 1] == 'M';
    return result;
}

// Counts the records of `brstack` (perf script -F brstack: fields `0xFROM/0xTO/FLAG/...`, FLAG M when the branch was
// mispredicted and P when it wasn't) by the branch's address, and ranks the branches by mispredictions, each at its
// address less `bias`, with its instruction from `disassembly_text` (objdump -d --no-show-raw-insn of the file) when that
// isn't empty.
static bool profile_branch_misses(Arena* arena, ProfileReport* report, String8 brstack, uint64_t bias, String8 disassembly_text, uint64_t branch_count)
{
    bool result = false;
    ProfileBranchTable table = profile_branch_table(arena, BUSTER_PROFILE_BRANCH_TABLE_START);
    bool valid = table.entries != 0;
    const ProfileDisassembly disassembly = profile_disassembly_read(arena, disassembly_text);
    uint64_t records = 0;
    uint64_t mispredicted = 0;
    uint64_t malformed = 0;
    uint64_t position = 0;
    while (valid && position < brstack.length)
    {
        const String8 line = profile_line(brstack, &position);
        uint64_t i = 0;
        while (valid && i < line.length)
        {
            while (i < line.length && profile_is_blank(line.pointer[i]))
            {
                i += 1;
            }
            uint64_t end = i;
            while (end < line.length && !profile_is_blank(line.pointer[end]))
            {
                end += 1;
            }
            const String8 field = profile_slice(line, i, end);
            uint64_t from = 0;
            bool missed = false;
            if (profile_brstack_record(field, &from, &missed))
            {
                if (2 * (table.count + 1) > table.capacity)
                {
                    valid = profile_branch_grow(arena, &table);
                }
                if (valid)
                {
                    const uint64_t slot = profile_branch_slot(&table, from);
                    table.count += table.entries[slot].address == 0;
                    table.entries[slot].address = from;
                    table.entries[slot].records += 1;
                    table.entries[slot].mispredicted += missed;
                    records += 1;
                    mispredicted += missed;
                }
            }
            else
            {
                malformed += field.length != 0;
            }
            i = end;
        }
    }

    if (!valid || disassembly.full)
    {
        profile_print(report, "branch-misses: out of memory\n");
    }
    else if (!disassembly.valid)
    {
        profile_print(report, "branch-misses: the disassembly has an instruction before its first symbol, or out of address order\n");
    }
    else
    {
        profile_print(report, "branch-misses: %llu records of %llu branches, %llu mispredicted (%.2f%%)", (unsigned long long)records, (unsigned long long)table.count,
                      (unsigned long long)mispredicted, records != 0 ? 100.0 * (double)mispredicted / (double)records : 0.0);
        if (malformed != 0)
        {
            profile_print(report, ", %llu fields that aren't records", (unsigned long long)malformed);
        }
        profile_print(report, "\n     address  mispredicted   records  share\n");
        for (uint64_t round = 0; round < branch_count && round < table.count; round += 1)
        {
            uint64_t best = table.capacity;
            for (uint64_t slot = 0; slot < table.capacity; slot += 1)
            {
                const ProfileBranch* entry = &table.entries[slot];
                const bool better = best == table.capacity || entry->mispredicted > table.entries[best].mispredicted ||
                                    (entry->mispredicted == table.entries[best].mispredicted && entry->address < table.entries[best].address);
                best = entry->address != 0 && !entry->reported && better ? slot : best;
            }
            ProfileBranch* branch = &table.entries[best];
            branch->reported = true;
            const uint64_t address = branch->address - bias;
            profile_print(report, "  %10llx  %12llu  %8llu  %5.2f%%", (unsigned long long)address, (unsigned long long)branch->mispredicted,
                          (unsigned long long)branch->records, mispredicted != 0 ? 100.0 * (double)branch->mispredicted / (double)mispredicted : 0.0);
            const uint64_t index = profile_instruction_at(&disassembly, 0, disassembly.count, address);
            if (index != disassembly.count && disassembly.instructions[index].address == address)
            {
                const ProfileInstruction* instruction = &disassembly.instructions[index];
                const ProfileSymbol* symbol = &disassembly.symbols[instruction->symbol];
                profile_print(report, "  %.*s+0x%llx: %.*s", (int)symbol->name.length, symbol->name.pointer, (unsigned long long)(address - symbol->address),
                              (int)instruction->text.length, instruction->text.pointer);
            }
            else if (disassembly.count != 0)
            {
                profile_print(report, "  (not an instruction of the disassembly)");
            }
            profile_print(report, "\n");
        }
        result = !report->full;
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// ab

// The tree that run `position` of a round runs: the trees rotate by one each round, so that each runs at every place
// in turn.
static uint64_t profile_ab_tree(uint64_t round, uint64_t position, uint64_t tree_count)
{
    return (round + position) % tree_count;
}

typedef struct ProfileAb ProfileAb;
struct ProfileAb
{
    // The trees' executables.
    const char* const* executables;
    const char* const* names;
    uint64_t tree_count;
    // Argument lists: configuration c is arguments[starts[c]] to arguments[starts[c] + counts[c] - 1].
    char* const* arguments;
    const uint64_t* starts;
    const uint64_t* counts;
    uint64_t configuration_count;
    uint64_t rounds;
    // The CPU each run is pinned to, or -1.
    int64_t cpu;
    // The command each run goes through (exclusive.sh), or null.
    const char* lock;
    // Where the headings and the runs' output and errors go.
    int output;
};

// Runs every round, and returns how many runs failed: those that exited with another status than 0, or couldn't run.
static uint64_t profile_ab_run(Arena* arena, const ProfileAb* ab)
{
    uint64_t failures = 0;
    for (uint64_t round = 0; round < ab->rounds; round += 1)
    {
        for (uint64_t c = 0; c < ab->configuration_count; c += 1)
        {
            for (uint64_t position = 0; position < ab->tree_count; position += 1)
            {
                const uint64_t tree = profile_ab_tree(round, position, ab->tree_count);
                dprintf(ab->output, "== round %llu tree %s config [", (unsigned long long)(round + 1), ab->names[tree]);
                for (uint64_t a = 0; a < ab->counts[c]; a += 1)
                {
                    dprintf(ab->output, "%s%s", a == 0 ? "" : " ", ab->arguments[ab->starts[c] + a]);
                }
                dprintf(ab->output, "]\n");

                // The child's arguments: the lock, the executable, then the configuration's.
                const uint64_t mark = arena->length;
                char** argv = BUSTER_ARENA_PUSH_ARRAY(arena, char*, ab->counts[c] + 3);
                bool ran = false;
                int status = 0;
                if (argv != 0)
                {
                    uint64_t n = 0;
                    if (ab->lock != 0)
                    {
                        argv[n] = (char*)ab->lock;
                        n += 1;
                    }
                    argv[n] = (char*)ab->executables[tree];
                    n += 1;
                    for (uint64_t a = 0; a < ab->counts[c]; a += 1)
                    {
                        argv[n] = ab->arguments[ab->starts[c] + a];
                        n += 1;
                    }
                    argv[n] = 0;
                    // The child leaves no copy of buffered output to write again at its exit (a sanitizer's _exit flushes).
                    fflush(stdout);
                    fflush(stderr);
                    const pid_t child = fork();
                    if (child == 0)
                    {
                        // The lock reads EXCLUSIVE_OWNER to say whose run holds it.
                        setenv("EXCLUSIVE_OWNER", "build/compiler ab", 0);
                        bool ready = dup2(ab->output, STDOUT_FILENO) != -1 && dup2(ab->output, STDERR_FILENO) != -1;
                        if (ready && ab->cpu >= 0)
                        {
                            uint64_t mask[BUSTER_PROFILE_CPUS / 64] = { 0 };
                            mask[ab->cpu / 64] = 1ull << (ab->cpu % 64);
                            ready = syscall(SYS_sched_setaffinity, 0, sizeof(mask), mask) == 0;
                        }
                        if (ready)
                        {
                            execv(argv[0], argv);
                        }
                        dprintf(ab->output, "ab: can't run %s\n", argv[0]);
                        _exit(127);
                    }
                    ran = child != -1 && waitpid(child, &status, 0) == child;
                }
                arena_pop_to(arena, mark);
                const bool succeeded = ran && WIFEXITED(status) && WEXITSTATUS(status) == 0;
                if (!succeeded)
                {
                    dprintf(ab->output, "ab: round %llu tree %s failed\n", (unsigned long long)(round + 1), ab->names[tree]);
                }
                failures += !succeeded;
            }
        }
    }
    dprintf(ab->output, "ab: done, %llu failed runs\n", (unsigned long long)failures);
    return failures;
}

// ---------------------------------------------------------------------------------------------------------------------
// time-runs

static bool profile_count_option(const char* text, uint64_t* value);

// Where profile_spin's state goes, so that its loop isn't removed.
static volatile uint64_t profile_spin_sink;

// One timed process: wall time from its release to wait4's return, wait4's rusage and status, and its user cycles and
// instructions from its exec to its exit, all its threads', as perf stat counts them (counted: both counters opened
// and ran the whole time).
typedef struct ProfileTimed ProfileTimed;
struct ProfileTimed
{
    uint64_t wall_ns;
    uint64_t user_us;
    uint64_t system_us;
    uint64_t peak_kb;
    uint64_t minor_faults;
    uint64_t major_faults;
    uint64_t cycles;
    uint64_t instructions;
    bool counted;
    int status;
};

// What time-runs runs. Its builds are executables, each at one or more placements (one executable per placement: a
// build linked at several code shifts, build.sh's `shifts`), and the executables are in the order the slots take them:
// placement by placement, each build's in turn, so that neighbouring slots are other builds'. With copies, each
// executable runs from that many copies of itself, made fresh for the command.
typedef struct ProfileTimeRuns ProfileTimeRuns;
struct ProfileTimeRuns
{
    // Executable e is placement placements[e] of build builds[e], named build_names[builds[e]].
    const char* const* executables;
    const uint64_t* builds;
    const uint64_t* placements;
    uint64_t executable_count;
    const char* const* build_names;
    uint64_t build_count;
    // How many copies of each executable run in its place (0: the executables themselves), and the directory to make
    // them in (null for $TMPDIR, or /tmp).
    uint64_t copies;
    const char* directory;
    // Argument lists, as ab's: list l is arguments[starts[l]] to arguments[starts[l] + counts[l] - 1].
    char* const* arguments;
    const uint64_t* starts;
    const uint64_t* counts;
    uint64_t list_count;
    uint64_t runs;
    uint64_t warmups;
    uint64_t after_scalar_ms;
    int output;
};

// A slot of a run: an executable, a copy of it (0 without copies) and a list.
typedef struct ProfileTimeSlot ProfileTimeSlot;
struct ProfileTimeSlot
{
    uint64_t executable;
    uint64_t copy;
    uint64_t list;
};

// The copies each executable runs from: at least the one, itself.
static uint64_t profile_time_copies(const ProfileTimeRuns* t)
{
    return t->copies != 0 ? t->copies : 1;
}

static uint64_t profile_time_slot_count(const ProfileTimeRuns* t)
{
    return t->executable_count * profile_time_copies(t) * t->list_count;
}

// Slot `slot`'s executable, copy and list: the executable changes fastest, then the copy, then the list, so that a run
// takes every build's copies in turn, list by list.
static ProfileTimeSlot profile_time_slot(const ProfileTimeRuns* t, uint64_t slot)
{
    const uint64_t copies = profile_time_copies(t);
    return (ProfileTimeSlot){
        .executable = slot % t->executable_count,
        .copy = (slot / t->executable_count) % copies,
        .list = slot / (t->executable_count * copies),
    };
}

static uint64_t profile_time_slot_index(const ProfileTimeRuns* t, uint64_t executable, uint64_t copy, uint64_t list)
{
    return (list * profile_time_copies(t) + copy) * t->executable_count + executable;
}

// The executable that is placement `placement` of build `build`, or executable_count if it has none.
static uint64_t profile_time_executable(const ProfileTimeRuns* t, uint64_t build, uint64_t placement)
{
    uint64_t result = t->executable_count;
    for (uint64_t e = 0; e < t->executable_count; e += 1)
    {
        result = t->builds[e] == build && t->placements[e] == placement ? e : result;
    }
    return result;
}

static uint64_t profile_time_placement_count(const ProfileTimeRuns* t, uint64_t build)
{
    uint64_t result = 0;
    for (uint64_t e = 0; e < t->executable_count; e += 1)
    {
        result += t->builds[e] == build;
    }
    return result;
}

// Prints a slot's executable and copy (" main placement 2 copy 3"), where the command has more than one of either.
static void profile_time_print_unit(int output, const ProfileTimeRuns* t, ProfileTimeSlot slot)
{
    if (t->executable_count > 1 || t->copies != 0)
    {
        const uint64_t build = t->builds[slot.executable];
        dprintf(output, " %s", t->build_names[build]);
        if (profile_time_placement_count(t, build) > 1)
        {
            dprintf(output, " placement %llu", (unsigned long long)(t->placements[slot.executable] + 1));
        }
        if (t->copies != 0)
        {
            dprintf(output, " copy %llu", (unsigned long long)(slot.copy + 1));
        }
    }
}

static uint64_t profile_now_ns(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}

// Integer work for `ms` milliseconds: lighter code than a compiler's vector phases, which lets the core's clock settle
// as it does before them.
static void profile_spin(uint64_t ms)
{
    const uint64_t end = profile_now_ns() + ms * 1000000ull;
    uint64_t state = 0x9e3779b97f4a7c15ull;
    uint64_t now = ms != 0 ? profile_now_ns() : end;
    while (now < end)
    {
        for (uint32_t i = 0; i < 4096; i += 1)
        {
            state ^= state << 13;
            state ^= state >> 7;
            state ^= state << 17;
        }
        now = profile_now_ns();
    }
    profile_spin_sink = state;
}

// A counter of the child's user-mode events, from its exec to its exit, with every thread it starts (as perf stat's),
// or -1 where perf_event_open refuses it.
static int profile_open_counter(pid_t child, uint64_t config)
{
    struct perf_event_attr attribute = { 0 };
    attribute.size = sizeof(attribute);
    attribute.type = PERF_TYPE_HARDWARE;
    attribute.config = config;
    attribute.disabled = 1;
    attribute.enable_on_exec = 1;
    attribute.inherit = 1;
    attribute.exclude_kernel = 1;
    attribute.exclude_hv = 1;
    attribute.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
    return (int)syscall(SYS_perf_event_open, &attribute, child, -1, -1, PERF_FLAG_FD_CLOEXEC);
}

// A counter's value, closing it; false if it couldn't be read or didn't count the whole time (multiplexed).
static bool profile_read_counter(int counter, uint64_t* value)
{
    uint64_t values[3] = { 0 };
    const bool result = counter != -1 && read(counter, values, sizeof(values)) == (ssize_t)sizeof(values) && values[1] == values[2];
    *value = result ? values[0] : 0;
    if (counter != -1)
    {
        close(counter);
    }
    return result;
}

// Runs `executable` with `argv` (argv[0] the executable) once, its standard streams on /dev/null. The child waits on a
// pipe until its counters are open, then execs; a child that can't be counted still runs.
static ProfileTimed profile_time_one(const char* executable, char* const* argv)
{
    ProfileTimed result = { .status = -1 };
    int release[2] = { -1, -1 };
    const bool piped = pipe(release) == 0 && fcntl(release[0], F_SETFD, FD_CLOEXEC) == 0 && fcntl(release[1], F_SETFD, FD_CLOEXEC) == 0;
    const pid_t child = fork();
    if (child == 0)
    {
        char byte = 0;
        const int null = open("/dev/null", O_RDWR | O_CLOEXEC);
        if (piped)
        {
            close(release[1]);
        }
        // An end of file, the parent gone before it released the child, runs nothing.
        const bool released = !piped || read(release[0], &byte, 1) == 1;
        if (released && null != -1 && dup2(null, STDIN_FILENO) != -1 && dup2(null, STDOUT_FILENO) != -1 && dup2(null, STDERR_FILENO) != -1)
        {
            execv(executable, argv);
        }
        _exit(127);
    }
    const int cycles = child != -1 ? profile_open_counter(child, PERF_COUNT_HW_CPU_CYCLES) : -1;
    const int instructions = child != -1 ? profile_open_counter(child, PERF_COUNT_HW_INSTRUCTIONS) : -1;
    const uint64_t start = profile_now_ns();
    if (piped)
    {
        close(release[0]);
        if (child != -1 && write(release[1], "x", 1) != 1)
        {
            kill(child, SIGKILL);
        }
        close(release[1]);
    }
    struct rusage usage;
    int status = 0;
    if (child != -1 && wait4(child, &status, 0, &usage) == child)
    {
        const uint64_t wall_ns = profile_now_ns() - start;
        uint64_t cycle_count = 0;
        uint64_t instruction_count = 0;
        const bool counted_cycles = profile_read_counter(cycles, &cycle_count);
        const bool counted_instructions = profile_read_counter(instructions, &instruction_count);
        result = (ProfileTimed){
            .wall_ns = wall_ns,
            .user_us = (uint64_t)usage.ru_utime.tv_sec * 1000000ull + (uint64_t)usage.ru_utime.tv_usec,
            .system_us = (uint64_t)usage.ru_stime.tv_sec * 1000000ull + (uint64_t)usage.ru_stime.tv_usec,
            .peak_kb = (uint64_t)usage.ru_maxrss,
            .minor_faults = (uint64_t)usage.ru_minflt,
            .major_faults = (uint64_t)usage.ru_majflt,
            .cycles = cycle_count,
            .instructions = instruction_count,
            .counted = counted_cycles && counted_instructions,
            .status = status,
        };
    }
    else
    {
        uint64_t unused = 0;
        profile_read_counter(cycles, &unused);
        profile_read_counter(instructions, &unused);
    }
    return result;
}

// The median of `count` values, sorted in place (insertion sort: a few dozen runs).
static uint64_t profile_median(uint64_t* values, uint64_t count)
{
    for (uint64_t i = 1; i < count; i += 1)
    {
        const uint64_t value = values[i];
        uint64_t j = i;
        while (j > 0 && values[j - 1] > value)
        {
            values[j] = values[j - 1];
            j -= 1;
        }
        values[j] = value;
    }
    return count != 0 ? values[count / 2] : 0;
}

// `value` against `reference`, in percent; 0 against a reference of 0.
static double profile_change(double value, double reference)
{
    return reference != 0 ? (value / reference - 1) * 100 : 0;
}

// One metric's comparison of the builds on list `list`, into `report`: values[r * slot count + slot] is run r's
// process in each slot (profile_time_slot). For each build, in order: its runs and their median; each placement's
// median (over its copies' runs) and their spread, the largest over the smallest; the largest spread of one
// placement's copies' medians; and, for every build but the first, its median against the first's, with that
// difference's range over the placements, each of its placements' medians against each of the first build's, and
// over the placements and the copies, each of its copies' medians against each of the first build's. The ranges pair
// nothing: two trees' code starts at bases an arbitrary distance apart, so their placements of one number aren't the
// same placement. Medians are printed divided by `scale`.
static bool profile_time_compare(Arena* arena, ProfileReport* report, const ProfileTimeRuns* t, const uint64_t* values, uint64_t list, const char* metric, double scale)
{
    const uint64_t copies = profile_time_copies(t);
    const uint64_t slot_count = profile_time_slot_count(t);
    const uint64_t mark = arena->length;
    uint64_t* gathered = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, t->runs * t->executable_count * copies + 1);
    double* unit_medians = BUSTER_ARENA_PUSH_ARRAY(arena, double, t->executable_count * copies + 1);
    double* placement_medians = BUSTER_ARENA_PUSH_ARRAY(arena, double, t->executable_count + 1);
    const bool result = gathered != 0 && unit_medians != 0 && placement_medians != 0;
    for (uint64_t e = 0; result && e < t->executable_count; e += 1)
    {
        for (uint64_t c = 0; c < copies; c += 1)
        {
            for (uint64_t r = 0; r < t->runs; r += 1)
            {
                gathered[r] = values[r * slot_count + profile_time_slot_index(t, e, c, list)];
            }
            unit_medians[e * copies + c] = (double)profile_median(gathered, t->runs);
        }
        uint64_t count = 0;
        for (uint64_t c = 0; c < copies; c += 1)
        {
            for (uint64_t r = 0; r < t->runs; r += 1)
            {
                gathered[count] = values[r * slot_count + profile_time_slot_index(t, e, c, list)];
                count += 1;
            }
        }
        placement_medians[e] = (double)profile_median(gathered, count);
    }
    double reference = 0;
    const uint64_t reference_placements = result ? profile_time_placement_count(t, 0) : 0;
    if (result)
    {
        profile_print(report, "list %llu, %s:\n", (unsigned long long)(list + 1), metric);
    }
    for (uint64_t b = 0; result && b < t->build_count; b += 1)
    {
        const uint64_t placements = profile_time_placement_count(t, b);
        uint64_t count = 0;
        for (uint64_t p = 0; p < placements; p += 1)
        {
            const uint64_t e = profile_time_executable(t, b, p);
            for (uint64_t c = 0; c < copies; c += 1)
            {
                for (uint64_t r = 0; r < t->runs; r += 1)
                {
                    gathered[count] = values[r * slot_count + profile_time_slot_index(t, e, c, list)];
                    count += 1;
                }
            }
        }
        const double median = (double)profile_median(gathered, count);
        reference = b == 0 ? median : reference;
        profile_print(report, "  %s: %llu runs, median %.3f", t->build_names[b], (unsigned long long)count, median / scale);
        double smallest = 0;
        double largest = 0;
        double copy_spread = 0;
        for (uint64_t p = 0; p < placements; p += 1)
        {
            const uint64_t e = profile_time_executable(t, b, p);
            smallest = p == 0 || placement_medians[e] < smallest ? placement_medians[e] : smallest;
            largest = p == 0 || placement_medians[e] > largest ? placement_medians[e] : largest;
            double least = unit_medians[e * copies];
            double most = least;
            for (uint64_t c = 1; c < copies; c += 1)
            {
                least = unit_medians[e * copies + c] < least ? unit_medians[e * copies + c] : least;
                most = unit_medians[e * copies + c] > most ? unit_medians[e * copies + c] : most;
            }
            copy_spread = profile_change(most, least) > copy_spread ? profile_change(most, least) : copy_spread;
        }
        if (placements > 1)
        {
            profile_print(report, "; placements");
            for (uint64_t p = 0; p < placements; p += 1)
            {
                profile_print(report, "%s %.3f", p == 0 ? "" : ",", placement_medians[profile_time_executable(t, b, p)] / scale);
            }
            profile_print(report, " (spread %.2f%%)", profile_change(largest, smallest));
        }
        if (copies > 1)
        {
            profile_print(report, "; copies within %.2f%%", copy_spread);
        }
        if (b != 0)
        {
            profile_print(report, "; against %s %+.2f%%", t->build_names[0], profile_change(median, reference));
        }
        const bool placed = placements > 1 || reference_placements > 1;
        if (b != 0 && (placed || copies > 1))
        {
            double placements_low = 0;
            double placements_high = 0;
            double low = 0;
            double high = 0;
            for (uint64_t p = 0; p < placements; p += 1)
            {
                const uint64_t e = profile_time_executable(t, b, p);
                for (uint64_t q = 0; q < reference_placements; q += 1)
                {
                    const uint64_t f = profile_time_executable(t, 0, q);
                    const double change = profile_change(placement_medians[e], placement_medians[f]);
                    placements_low = (p == 0 && q == 0) || change < placements_low ? change : placements_low;
                    placements_high = (p == 0 && q == 0) || change > placements_high ? change : placements_high;
                    for (uint64_t c = 0; c < copies; c += 1)
                    {
                        for (uint64_t d = 0; d < copies; d += 1)
                        {
                            const double copy_change = profile_change(unit_medians[e * copies + c], unit_medians[f * copies + d]);
                            const bool first = p == 0 && q == 0 && c == 0 && d == 0;
                            low = first || copy_change < low ? copy_change : low;
                            high = first || copy_change > high ? copy_change : high;
                        }
                    }
                }
            }
            profile_print(report, " (");
            if (placed)
            {
                profile_print(report, "over placements %+.2f%% to %+.2f%%%s", placements_low, placements_high, copies > 1 ? "; " : "");
            }
            if (copies > 1)
            {
                profile_print(report, "over %s %+.2f%% to %+.2f%%", placed ? "placements and copies" : "copies", low, high);
            }
            profile_print(report, ")");
        }
        profile_print(report, "\n");
    }
    arena_pop_to(arena, mark);
    return result && !report->full;
}

// Makes `copies` copies of each executable in a new directory under t->directory, named DIRECTORY/EEE-CCC (the
// executable's and the copy's numbers, from 0), every one's path the same length, so that argv[0] puts every process's
// stack at the same offset from its top. Each is written whole, so that its pages are the copy's own. Returns the
// copies' paths, executable by executable, or null after removing what it made.
static char** profile_time_make_copies(Arena* arena, const ProfileTimeRuns* t, char* directory, uint64_t directory_bytes)
{
    const char* base = t->directory != 0 ? t->directory : getenv("TMPDIR");
    base = base != 0 && base[0] != 0 ? base : "/tmp";
    const int written = snprintf(directory, directory_bytes, "%s/buster-time-runs-XXXXXX", base);
    bool made = written > 0 && (uint64_t)written < directory_bytes && mkdtemp(directory) != 0;
    const uint64_t count = t->executable_count * t->copies;
    char** paths = made ? BUSTER_ARENA_PUSH_ARRAY(arena, char*, count + 1) : 0;
    char* buffer = paths != 0 ? arena_push(arena, BUSTER_PROFILE_COPY_BUFFER, 64) : 0;
    uint64_t done = 0;
    bool copying = buffer != 0;
    for (uint64_t e = 0; copying && e < t->executable_count; e += 1)
    {
        for (uint64_t c = 0; copying && c < t->copies; c += 1)
        {
            const uint64_t length = strlen(directory) + sizeof("/000-000");
            char* path = arena_push(arena, length, 1);
            copying = path != 0;
            if (copying)
            {
                snprintf(path, length, "%s/%03llu-%03llu", directory, (unsigned long long)e, (unsigned long long)c);
                const int from = open(t->executables[e], O_RDONLY | O_CLOEXEC);
                const int to = from != -1 ? open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0700) : -1;
                ssize_t bytes = from != -1 && to != -1 ? read(from, buffer, BUSTER_PROFILE_COPY_BUFFER) : -1;
                while (bytes > 0)
                {
                    bytes = write(to, buffer, (size_t)bytes) == bytes ? read(from, buffer, BUSTER_PROFILE_COPY_BUFFER) : -1;
                }
                // A file still open for writing can't be run (ETXTBSY): it is closed before any process starts.
                const bool closed = to != -1 && close(to) == 0;
                copying = bytes == 0 && closed;
                if (from != -1)
                {
                    close(from);
                }
                paths[done] = path;
                done += to != -1;
                if (!copying)
                {
                    dprintf(t->output, "time-runs: can't copy %s to %s\n", t->executables[e], path);
                }
            }
        }
    }
    if (made && !copying)
    {
        for (uint64_t i = 0; i < done; i += 1)
        {
            unlink(paths[i]);
        }
        rmdir(directory);
    }
    else if (!made)
    {
        dprintf(t->output, "time-runs: can't make a directory for the copies under %s\n", base);
    }
    return copying ? paths : 0;
}

// The warm-ups in slot order, then the runs, each with every slot in turn from an order that rotates by run
// (profile_ab_tree's), then each slot's medians and, with more than one executable or copies, each list's comparison
// of its builds (profile_time_compare): by wall time, and by user cycles where every process was counted. Returns how
// many processes failed: an exit status other than its slot's first warm-up's, a signal, or a process that didn't
// start (status 127, or no wait); and one more if the copies couldn't be made or the report couldn't be printed.
static uint64_t profile_time_runs(Arena* arena, const ProfileTimeRuns* t)
{
    uint64_t failures = 0;
    const uint64_t mark = arena->length;
    const uint64_t slot_count = profile_time_slot_count(t);
    ProfileTimed* timed = BUSTER_ARENA_PUSH_ARRAY(arena, ProfileTimed, t->runs * slot_count + 1);
    int* expected = BUSTER_ARENA_PUSH_ARRAY(arena, int, slot_count + 1);
    uint64_t* values = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, t->runs * slot_count + 1);
    uint64_t longest = 0;
    for (uint64_t l = 0; l < t->list_count; l += 1)
    {
        longest = t->counts[l] > longest ? t->counts[l] : longest;
    }
    char** argv = BUSTER_ARENA_PUSH_ARRAY(arena, char*, longest + 2);
    char directory[4096];
    char** copies = 0;
    if (timed != 0 && t->copies != 0)
    {
        copies = profile_time_make_copies(arena, t, directory, sizeof(directory));
    }
    const bool ready = timed != 0 && expected != 0 && values != 0 && argv != 0 && (t->copies == 0 || copies != 0);
    failures += !ready;
    for (uint64_t run = 0; ready && run < t->warmups + t->runs; run += 1)
    {
        const bool warmup = run < t->warmups;
        for (uint64_t position = 0; position < slot_count; position += 1)
        {
            const uint64_t s = warmup ? position : profile_ab_tree(run - t->warmups, position, slot_count);
            const ProfileTimeSlot slot = profile_time_slot(t, s);
            const char* executable = copies != 0 ? copies[slot.executable * t->copies + slot.copy] : t->executables[slot.executable];
            argv[0] = (char*)executable;
            for (uint64_t a = 0; a < t->counts[slot.list]; a += 1)
            {
                argv[1 + a] = t->arguments[t->starts[slot.list] + a];
            }
            argv[1 + t->counts[slot.list]] = 0;
            profile_spin(warmup ? 0 : t->after_scalar_ms);
            const ProfileTimed one = profile_time_one(executable, argv);
            const bool exited = one.status != -1 && WIFEXITED(one.status) && WEXITSTATUS(one.status) != 127;
            if (warmup && run == 0)
            {
                expected[s] = one.status;
            }
            const bool same = exited && one.status == expected[s];
            failures += !same;
            if (!warmup)
            {
                timed[(run - t->warmups) * slot_count + s] = one;
                dprintf(t->output, "run %llu list %llu", (unsigned long long)(run - t->warmups + 1), (unsigned long long)(slot.list + 1));
                profile_time_print_unit(t->output, t, slot);
                dprintf(t->output, ": %.3f ms wall, %.3f ms user, %.3f ms system, %llu KB peak, %llu minor faults, %llu major, %llu user cycles, %llu instructions, status %d%s\n",
                        (double)one.wall_ns / 1e6, (double)one.user_us / 1e3, (double)one.system_us / 1e3, (unsigned long long)one.peak_kb,
                        (unsigned long long)one.minor_faults, (unsigned long long)one.major_faults, (unsigned long long)one.cycles, (unsigned long long)one.instructions,
                        exited ? WEXITSTATUS(one.status) : -1, same ? "" : " (FAILED: another status than the first warm-up's)");
            }
            else if (!same)
            {
                dprintf(t->output, "warm-up %llu list %llu", (unsigned long long)(run + 1), (unsigned long long)(slot.list + 1));
                profile_time_print_unit(t->output, t, slot);
                dprintf(t->output, ": FAILED (status %d)\n", exited ? WEXITSTATUS(one.status) : -1);
            }
        }
    }
    bool counted = true;
    for (uint64_t s = 0; ready && s < slot_count; s += 1)
    {
        const ProfileTimeSlot slot = profile_time_slot(t, s);
        uint64_t medians[8];
        for (uint32_t field = 0; t->runs != 0 && field < 8; field += 1)
        {
            for (uint64_t r = 0; r < t->runs; r += 1)
            {
                const ProfileTimed* one = &timed[r * slot_count + s];
                const uint64_t fields[8] = { one->wall_ns, one->user_us, one->system_us, one->peak_kb, one->minor_faults, one->major_faults, one->cycles, one->instructions };
                values[r] = fields[field];
                counted = counted && one->counted;
            }
            medians[field] = profile_median(values, t->runs);
        }
        // values holds the sorted instructions now; the wall times' extremes again.
        for (uint64_t r = 0; r < t->runs; r += 1)
        {
            values[r] = timed[r * slot_count + s].wall_ns;
        }
        profile_median(values, t->runs);
        if (t->runs != 0)
        {
            dprintf(t->output, "list %llu", (unsigned long long)(slot.list + 1));
            profile_time_print_unit(t->output, t, slot);
            dprintf(t->output, " [");
            for (uint64_t a = 0; a < t->counts[slot.list]; a += 1)
            {
                dprintf(t->output, "%s%s", a == 0 ? "" : " ", t->arguments[t->starts[slot.list] + a]);
            }
            dprintf(t->output,
                    "]: %llu runs, median %.3f ms wall (%.3f to %.3f), %.3f ms user, %.3f ms system, %llu KB peak, %llu minor faults, %llu major, %llu user cycles, %llu "
                    "instructions\n",
                    (unsigned long long)t->runs, (double)medians[0] / 1e6, (double)values[0] / 1e6, (double)values[t->runs - 1] / 1e6, (double)medians[1] / 1e3,
                    (double)medians[2] / 1e3, (unsigned long long)medians[3], (unsigned long long)medians[4], (unsigned long long)medians[5],
                    (unsigned long long)medians[6], (unsigned long long)medians[7]);
        }
    }
    if (ready && t->runs != 0 && (t->executable_count > 1 || t->copies > 1))
    {
        ProfileReport report = { .arena = arena_reserve(BUSTER_PROFILE_REPORT_CAPACITY) };
        bool compared = report.arena.base != 0;
        for (uint64_t l = 0; compared && l < t->list_count; l += 1)
        {
            for (uint64_t i = 0; i < t->runs * slot_count; i += 1)
            {
                values[i] = timed[i].wall_ns;
            }
            compared = profile_time_compare(arena, &report, t, values, l, "wall time (ms)", 1e6);
            for (uint64_t i = 0; compared && counted && i < t->runs * slot_count; i += 1)
            {
                values[i] = timed[i].cycles;
            }
            compared = compared && (!counted || profile_time_compare(arena, &report, t, values, l, "user cycles (millions)", 1e6));
        }
        const String8 text = profile_report_text(&report);
        compared = compared && write(t->output, text.pointer, text.length) == (ssize_t)text.length;
        failures += !compared;
        if (!counted)
        {
            dprintf(t->output, "time-runs: user cycles weren't counted for every process, so they aren't compared\n");
        }
        arena_release(&report.arena);
    }
    if (copies != 0)
    {
        for (uint64_t i = 0; i < t->executable_count * t->copies; i += 1)
        {
            unlink(copies[i]);
        }
        rmdir(directory);
    }
    dprintf(t->output, "time-runs: done, %llu failed processes\n", (unsigned long long)failures);
    arena_pop_to(arena, mark);
    return failures;
}

static int run_time_runs_command(Arena* arena, int argc, char** argv)
{
    ProfileTimeRuns t = { .runs = 21, .warmups = 1, .output = STDOUT_FILENO };
    // At most one executable per pair of arguments, and one build each.
    const char** executables = BUSTER_ARENA_PUSH_ARRAY(arena, const char*, (uint64_t)argc + 1);
    uint64_t* builds = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, (uint64_t)argc + 1);
    uint64_t* placements = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, (uint64_t)argc + 1);
    const char** build_names = BUSTER_ARENA_PUSH_ARRAY(arena, const char*, (uint64_t)argc + 1);
    // The executables in the order they are given, with their builds, before they are put in the slots' order.
    const char** given = BUSTER_ARENA_PUSH_ARRAY(arena, const char*, (uint64_t)argc + 1);
    uint64_t* given_builds = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, (uint64_t)argc + 1);
    bool valid = executables != 0 && builds != 0 && placements != 0 && build_names != 0 && given != 0 && given_builds != 0;
    uint64_t given_count = 0;
    int i = 1;
    while (valid && i < argc && strcmp(argv[i], "--list") != 0)
    {
        uint64_t value = 0;
        if (strcmp(argv[i], "--runs") == 0 && i + 1 < argc && profile_count_option(argv[i + 1], &value) && value <= 1000)
        {
            t.runs = value;
        }
        else if (strcmp(argv[i], "--warmups") == 0 && i + 1 < argc && profile_decimal(profile_string(argv[i + 1]), &value) && value >= 1 && value <= 1000)
        {
            t.warmups = value;
        }
        else if (strcmp(argv[i], "--after-scalar") == 0 && i + 1 < argc && profile_decimal(profile_string(argv[i + 1]), &value) && value <= 10000)
        {
            t.after_scalar_ms = value;
        }
        else if (strcmp(argv[i], "--copies") == 0 && i + 1 < argc && profile_count_option(argv[i + 1], &value) && value <= BUSTER_PROFILE_COPIES)
        {
            t.copies = value;
        }
        else if (strcmp(argv[i], "--directory") == 0 && i + 1 < argc)
        {
            t.directory = argv[i + 1];
        }
        else if ((strcmp(argv[i], "--executable") == 0 && i + 1 < argc) || (strcmp(argv[i], "--build") == 0 && i + 2 < argc))
        {
            // --executable PATH is a build of its own, named PATH; --build NAME PATH adds a placement to build NAME.
            const bool named = argv[i][2] == 'b';
            const char* name = argv[i + 1];
            uint64_t build = t.build_count;
            for (uint64_t b = 0; named && b < t.build_count; b += 1)
            {
                build = strcmp(build_names[b], name) == 0 ? b : build;
            }
            build_names[build] = name;
            t.build_count += build == t.build_count;
            given[given_count] = argv[i + 1 + named];
            given_builds[given_count] = build;
            given_count += 1;
            i += named;
        }
        else
        {
            valid = false;
        }
        i += 2;
    }
    if (valid && given_count == 0)
    {
        given[0] = "/proc/self/exe";
        given_builds[0] = 0;
        build_names[0] = "/proc/self/exe";
        given_count = 1;
        t.build_count = 1;
    }
    // The slots' order: placement by placement, each build's in turn.
    for (uint64_t p = 0; valid && t.executable_count < given_count; p += 1)
    {
        for (uint64_t b = 0; b < t.build_count; b += 1)
        {
            uint64_t placement = 0;
            for (uint64_t g = 0; g < given_count; g += 1)
            {
                if (given_builds[g] == b && placement == p)
                {
                    executables[t.executable_count] = given[g];
                    builds[t.executable_count] = b;
                    placements[t.executable_count] = p;
                    t.executable_count += 1;
                }
                placement += given_builds[g] == b;
            }
        }
    }
    valid = valid && t.executable_count <= BUSTER_PROFILE_EXECUTABLES;
    uint64_t* starts = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, (uint64_t)argc);
    uint64_t* counts = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, (uint64_t)argc);
    valid = valid && i < argc && starts != 0 && counts != 0;
    while (valid && i < argc)
    {
        // At a `--list`: a list starts after it.
        starts[t.list_count] = (uint64_t)(i + 1);
        counts[t.list_count] = 0;
        i += 1;
        while (i < argc && strcmp(argv[i], "--list") != 0)
        {
            counts[t.list_count] += 1;
            i += 1;
        }
        t.list_count += 1;
    }
    t.executables = executables;
    t.builds = builds;
    t.placements = placements;
    t.build_names = build_names;
    t.arguments = argv;
    t.starts = starts;
    t.counts = counts;
    int result = 1;
    if (!valid || t.list_count == 0)
    {
        fprintf(stderr, "usage: time-runs [--runs N] [--warmups N] [--after-scalar MS] [--copies N] [--directory PATH] [--executable PATH | --build NAME PATH]... --list "
                        "ARGUMENT... [--list ARGUMENT...]...\n");
    }
    else
    {
        result = profile_time_runs(arena, &t) == 0 ? 0 : 1;
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The commands

// Reads a whole file into the arena. A null pointer if it can't be read.
static String8 profile_read_file(Arena* arena, const char* path)
{
    String8 result = { 0 };
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    struct stat stats;
    char* bytes = 0;
    uint64_t length = 0;
    if (fd != -1 && fstat(fd, &stats) == 0 && S_ISREG(stats.st_mode))
    {
        length = (uint64_t)stats.st_size;
        bytes = arena_push(arena, length + 1, 1);
    }
    uint64_t read_bytes = 0;
    bool reading = bytes != 0;
    while (reading && read_bytes < length)
    {
        const ssize_t count = read(fd, bytes + read_bytes, length - read_bytes);
        reading = count > 0;
        read_bytes += reading ? (uint64_t)count : 0;
    }
    if (fd != -1)
    {
        close(fd);
    }
    if (bytes != 0 && read_bytes == length)
    {
        result = (String8){ .pointer = bytes, .length = length };
    }
    else
    {
        fprintf(stderr, "can't read %s\n", path);
    }
    return result;
}

// A count option's value, at least 1.
static bool profile_count_option(const char* text, uint64_t* value)
{
    return profile_decimal(profile_string(text), value) && *value != 0;
}

static bool profile_write_file(const char* path, String8 text)
{
    const int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    uint64_t written = 0;
    bool writing = fd != -1;
    while (writing && written < text.length)
    {
        const ssize_t count = write(fd, text.pointer + written, text.length - written);
        writing = count > 0;
        written += writing ? (uint64_t)count : 0;
    }
    const bool result = fd != -1 && written == text.length && close(fd) == 0;
    if (!result)
    {
        fprintf(stderr, "can't write %s\n", path);
    }
    return result;
}

static int run_ab_command(Arena* arena, int argc, char** argv)
{
    int result = 1;
    ProfileAb ab = { .rounds = 1, .cpu = -1, .output = STDOUT_FILENO };
    bool valid = true;
    int i = 1;
    while (valid && i < argc && argv[i][0] == '-' && strcmp(argv[i], "--") != 0)
    {
        uint64_t value = 0;
        if (strcmp(argv[i], "--rounds") == 0 && i + 1 < argc && profile_count_option(argv[i + 1], &value))
        {
            ab.rounds = value;
            i += 2;
        }
        else if (strcmp(argv[i], "--cpu") == 0 && i + 1 < argc && profile_decimal(profile_string(argv[i + 1]), &value) && value < BUSTER_PROFILE_CPUS)
        {
            ab.cpu = (int64_t)value;
            i += 2;
        }
        else if (strcmp(argv[i], "--lock") == 0 && i + 1 < argc)
        {
            ab.lock = argv[i + 1];
            i += 2;
        }
        else
        {
            valid = false;
        }
    }
    const int first_tree = i;
    while (i < argc && strcmp(argv[i], "--") != 0)
    {
        i += 1;
    }
    const uint64_t tree_count = (uint64_t)(i - first_tree);
    const char** executables = BUSTER_ARENA_PUSH_ARRAY(arena, const char*, tree_count + 1);
    uint64_t* starts = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, (uint64_t)argc);
    uint64_t* counts = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, (uint64_t)argc);
    valid = valid && tree_count != 0 && i < argc && executables != 0 && starts != 0 && counts != 0;
    for (uint64_t t = 0; valid && t < tree_count; t += 1)
    {
        // A directory is a tree, whose build/compiler runs; a file runs itself.
        const char* tree = argv[first_tree + (int)t];
        struct stat stats;
        if (stat(tree, &stats) == 0 && S_ISDIR(stats.st_mode))
        {
            const uint64_t length = strlen(tree);
            char* path = arena_push(arena, length + sizeof("/build/compiler"), 1);
            valid = path != 0;
            if (valid)
            {
                memcpy(path, tree, length);
                memcpy(path + length, "/build/compiler", sizeof("/build/compiler"));
                executables[t] = path;
            }
        }
        else
        {
            executables[t] = tree;
        }
    }
    // Each `--` starts a configuration, which may be empty: the command's defaults.
    while (valid && i < argc)
    {
        i += 1;
        starts[ab.configuration_count] = (uint64_t)i;
        while (i < argc && strcmp(argv[i], "--") != 0)
        {
            i += 1;
        }
        counts[ab.configuration_count] = (uint64_t)i - starts[ab.configuration_count];
        ab.configuration_count += 1;
    }

    if (!valid)
    {
        fprintf(stderr, "usage: ab [--rounds N] [--cpu N] [--lock PATH] TREE... -- ARGUMENT... [-- ARGUMENT...]...\n");
    }
    else
    {
        ab.executables = executables;
        ab.names = (const char* const*)(argv + first_tree);
        ab.tree_count = tree_count;
        ab.arguments = argv;
        ab.starts = starts;
        ab.counts = counts;
        fflush(stdout);
        result = profile_ab_run(arena, &ab) == 0 ? 0 : 1;
    }
    return result;
}

// Parses `--NAME N` at argv[i], the only option of the text commands.
static bool profile_option(int argc, char** argv, int* i, const char* name, uint64_t* value)
{
    const bool result = strcmp(argv[*i], name) == 0 && *i + 1 < argc && profile_count_option(argv[*i + 1], value);
    *i += result ? 2 : 0;
    return result;
}

// The commands but ab, which reads no text: hot-loops, label-spills, spill-report and branch-misses.
static int run_profile_text_command(Arena* arena, ProfileReport* report, int argc, char** argv)
{
    bool valid = true;
    bool succeeded = false;
    String8 output = { 0 };
    const char* output_path = 0;
    uint64_t count = 0;
    if (strcmp(argv[0], "hot-loops") == 0)
    {
        count = BUSTER_PROFILE_DEFAULT_LOOPS;
        int i = 4;
        while (valid && i < argc)
        {
            valid = profile_option(argc, argv, &i, "--loops", &count);
        }
        valid = valid && argc >= 4;
        const String8 disassembly = valid ? profile_read_file(arena, argv[2]) : (String8){ 0 };
        const String8 samples = valid && disassembly.pointer != 0 ? profile_read_file(arena, argv[3]) : (String8){ 0 };
        if (disassembly.pointer != 0 && samples.pointer != 0)
        {
            succeeded = profile_hot_loops(arena, report, profile_string(argv[1]), disassembly, samples, count);
        }
        if (!valid)
        {
            fprintf(stderr, "usage: hot-loops FUNCTION DISASSEMBLY SAMPLES [--loops N]\n");
        }
    }
    else if (strcmp(argv[0], "label-spills") == 0)
    {
        valid = argc == 5;
        const String8 assembly = valid ? profile_read_file(arena, argv[3]) : (String8){ 0 };
        ProfileReport labeled = { .arena = arena_reserve(BUSTER_PROFILE_REPORT_CAPACITY) };
        if (assembly.pointer != 0)
        {
            succeeded = profile_label_spills(&labeled, report, profile_string(argv[1]), profile_string(argv[2]), assembly);
            output = profile_report_text(&labeled);
            output_path = argv[4];
        }
        if (succeeded)
        {
            succeeded = profile_write_file(output_path, output);
        }
        arena_release(&labeled.arena);
        if (!valid)
        {
            fprintf(stderr, "usage: label-spills FUNCTION TAG INPUT.s OUTPUT.s\n");
        }
    }
    else if (strcmp(argv[0], "spill-report") == 0)
    {
        count = BUSTER_PROFILE_DEFAULT_SLOTS;
        int i = 5;
        while (valid && i < argc)
        {
            valid = profile_option(argc, argv, &i, "--slots", &count);
        }
        valid = valid && argc >= 5;
        const String8 labeled = valid ? profile_read_file(arena, argv[2]) : (String8){ 0 };
        const String8 disassembly = valid && labeled.pointer != 0 ? profile_read_file(arena, argv[3]) : (String8){ 0 };
        const String8 samples = valid && disassembly.pointer != 0 ? profile_read_file(arena, argv[4]) : (String8){ 0 };
        if (labeled.pointer != 0 && disassembly.pointer != 0 && samples.pointer != 0)
        {
            succeeded = profile_spill_report(arena, report, profile_string(argv[1]), labeled, disassembly, samples, count);
        }
        if (!valid)
        {
            fprintf(stderr, "usage: spill-report FUNCTION LABELED.s DISASSEMBLY SAMPLES [--slots N]\n");
        }
    }
    else
    {
        count = BUSTER_PROFILE_DEFAULT_BRANCHES;
        uint64_t bias = 0;
        const char* disassembly_path = 0;
        int i = 2;
        while (valid && i < argc)
        {
            uint64_t position = 0;
            if (strcmp(argv[i], "--bias") == 0 && i + 1 < argc)
            {
                const String8 text = profile_string(argv[i + 1]);
                valid = profile_hex(text, &position, &bias) && position == text.length;
                i += 2;
            }
            else if (strcmp(argv[i], "--disassembly") == 0 && i + 1 < argc)
            {
                disassembly_path = argv[i + 1];
                i += 2;
            }
            else
            {
                valid = profile_option(argc, argv, &i, "--branches", &count);
            }
        }
        valid = valid && argc >= 2;
        const String8 brstack = valid ? profile_read_file(arena, argv[1]) : (String8){ 0 };
        String8 disassembly = { .pointer = "", .length = 0 };
        if (brstack.pointer != 0 && disassembly_path != 0)
        {
            disassembly = profile_read_file(arena, disassembly_path);
        }
        if (brstack.pointer != 0 && disassembly.pointer != 0)
        {
            succeeded = profile_branch_misses(arena, report, brstack, bias, disassembly, count);
        }
        if (!valid)
        {
            fprintf(stderr, "usage: branch-misses BRSTACK [--bias ADDRESS] [--disassembly FILE] [--branches N]\n");
        }
    }
    const String8 text = profile_report_text(report);
    fwrite(text.pointer, 1, text.length, succeeded ? stdout : stderr);
    if (report->full)
    {
        fprintf(stderr, "%s: out of memory for the report\n", argv[0]);
    }
    return succeeded ? 0 : 1;
}

// argv[0] is the command's name: ab, hot-loops, label-spills, spill-report or branch-misses.
static int run_profile_command(int argc, char** argv)
{
    int result = 1;
    Arena arena = arena_reserve(BUSTER_PROFILE_ARENA_CAPACITY);
    ProfileReport report = { .arena = arena_reserve(BUSTER_PROFILE_REPORT_CAPACITY) };
    if (strcmp(argv[0], "ab") == 0)
    {
        result = run_ab_command(&arena, argc, argv);
    }
    else if (strcmp(argv[0], "time-runs") == 0)
    {
        result = run_time_runs_command(&arena, argc, argv);
    }
    else
    {
        result = run_profile_text_command(&arena, &report, argc, argv);
    }
    arena_release(&report.arena);
    arena_release(&arena);
    return result;
}
