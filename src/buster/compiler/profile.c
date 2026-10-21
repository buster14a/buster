// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The profiling tools: what the lexer's and the encoder's optimization rounds ran around perf and objdump on the 9700X,
// as commands of this executable. docs/profiling.md has the method they serve and the perf and objdump commands that
// make their inputs:
//
//   build/compiler ab [--rounds N] [--cpu N] [--lock PATH] TREE... -- ARGUMENT... [-- ARGUMENT...]...
//   build/compiler hot-loops FUNCTION DISASSEMBLY SAMPLES [--loops N]
//   build/compiler label-spills FUNCTION INPUT.s OUTPUT.s
//   build/compiler spill-report FUNCTION LABELED.s SAMPLES [--slots N]
//   build/compiler branch-misses BRSTACK [--bias ADDRESS] [--disassembly FILE] [--branches N]
//
// - ab runs `TREE/build/compiler ARGUMENT...` (or TREE itself, when it is a file) for every tree, every argument list
//   and every round, the trees in an order that rotates by round, each run pinned to --cpu and under --lock (the
//   9700X's exclusive.sh), which a CI job can then take between two runs. Comparing builds in separate processes has
//   about 2% of noise on the 9700X; comparing variants inside one process (benchmark-lexer's, build.sh lexer-variant)
//   has about 0.6%.
// - hot-loops reads `objdump -d --no-show-raw-insn` of the executable and `perf script -F ip,sym,symoff,dsoff` of its
//   samples, finds each sample of the function by its offset in the file (a static function has a copy in every
//   translation unit that includes it, and objdump 2.47's --disassemble=NAME prints only one), takes the copy with the
//   most, and ranks its loops (each backward jump's range) by their share of the samples, then prints the smallest loop
//   with at least half of them, instruction by instruction with its samples.
// - label-spills copies clang's assembly (`clang -S`) with a label before every spill and reload of one function
//   (clang's "Spill" and "Reload" comments), and spill-report reads samples (IBS's) of the program built from it, by
//   `perf script -F ip,sym,symoff`: each label's own samples are its spill's or reload's, by stack slot, with the
//   source location of each slot's hottest, against all the labeled copy's samples.
// - branch-misses reads `perf script -F brstack` of last-branch records (`perf record -j any,u`) and ranks the branches
//   by their mispredictions, at addresses less the load bias (where a PIE was mapped, from --show-mmap-events), so
//   that `addr2line` and objdump of the file take them; with the file's disassembly, each with its instruction.
//
// Each tool's work is a function of its input texts that writes its report into a ProfileReport (profile_hot_loops,
// profile_label_spills, profile_spill_report, profile_branch_misses), which profile_test.c checks, and ab's order is
// profile_ab_tree. main.c includes this file after lane.c, for its arenas, and profile_test.c after it.
//
// Layout, in this order: the report (ProfileReport, profile_print), reading text (profile_line and the others), the
// disassembly (ProfileDisassembly, profile_disassembly_read, profile_jump_target), hot-loops, label-spills,
// spill-report, branch-misses, ab (profile_ab_tree, profile_ab_run), and the commands (run_profile_command).

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
// label-spills' labels: BUSTER_PROFILE_SPILL_LABEL then the spill's number.
#define BUSTER_PROFILE_SPILL_LABEL "buster_spill_"
// ab's --cpu: sched_setaffinity's mask of 1,024 CPUs, as glibc's cpu_set_t.
#define BUSTER_PROFILE_CPUS 1024

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
    // Its dsoff field, `(PATH+0xOFFSET)` last on the line: the offset in the file, which is the address objdump gives
    // when the executable segment's address is its offset (as in GNU ld's and lld's executables).
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

typedef struct ProfileDisassembly ProfileDisassembly;
struct ProfileDisassembly
{
    ProfileInstruction* instructions;
    uint64_t count;
    ProfileSymbol* symbols;
    uint64_t symbol_count;
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
    result.full = lines != 0 && (result.instructions == 0 || result.symbols == 0);
    position = 0;
    while (!result.full && position < text.length)
    {
        const String8 line = profile_line(text, &position);
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
    return result;
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

// The end of symbol `symbol`'s instructions.
static uint64_t profile_symbol_end(const ProfileDisassembly* disassembly, uint64_t symbol)
{
    return symbol + 1 < disassembly->symbol_count ? disassembly->symbols[symbol + 1].first : disassembly->count;
}

// Whether `address` is in symbol `symbol`: from its address to the next symbol's, or for the last symbol to the end of
// its last instruction, which is at most 15 bytes long.
static bool profile_symbol_contains(const ProfileDisassembly* disassembly, uint64_t symbol, uint64_t address)
{
    const uint64_t end = profile_symbol_end(disassembly, symbol);
    const uint64_t first = disassembly->symbols[symbol].first;
    uint64_t limit = disassembly->symbols[symbol].address;
    if (symbol + 1 < disassembly->symbol_count)
    {
        limit = disassembly->symbols[symbol + 1].address;
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
    // Each sample's copy, by its offset in the file. Without it, a sample of a copy the disassembly doesn't have
    // (objdump --disassemble=NAME prints one) would count at the same offset in another.
    uint64_t without_offset = 0;
    uint64_t outside = 0;
    uint64_t position = 0;
    while (copy_samples != 0 && copy_count != 0 && position < samples_text.length)
    {
        const ProfileSample sample = profile_sample(profile_line(samples_text, &position));
        uint64_t copy = copy_count;
        for (uint64_t c = 0; sample.has_file_offset && c < copy_count; c += 1)
        {
            copy = profile_symbol_contains(&disassembly, copies[c], sample.file_offset) ? c : copy;
        }
        if (profile_equal(sample.symbol, function))
        {
            without_offset += !sample.has_file_offset;
            outside += sample.has_file_offset && copy == copy_count;
            if (copy != copy_count)
            {
                copy_samples[copy] += 1;
            }
        }
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
                const uint64_t index = profile_instruction_at(&disassembly, first, end, sample.file_offset);
                const bool counted = profile_equal(sample.symbol, function) && index != end && profile_symbol_contains(&disassembly, symbol, sample.file_offset);
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
                profile_print(report, ", %llu samples at file offsets outside its copies", (unsigned long long)outside);
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

static bool profile_is_spill(String8 line)
{
    const uint64_t comment = profile_find(line, profile_string("#"));
    const String8 text = profile_slice(line, comment, line.length);
    return comment != line.length && (profile_find(text, profile_string("Spill")) != text.length || profile_find(text, profile_string("Reload")) != text.length);
}

// Copies clang's assembly with BUSTER_PROFILE_SPILL_LABEL and a number before each spill and reload of `function`,
// numbered from 0 in order. Returns false, with the reason in the report, if the function isn't in the assembly; the
// output is then empty.
static bool profile_label_spills(ProfileReport* output, ProfileReport* report, String8 function, String8 assembly)
{
    uint64_t labels = 0;
    bool found = false;
    bool inside = false;
    uint64_t position = 0;
    while (position < assembly.length)
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
        else if (inside && profile_is_spill(line))
        {
            profile_print(output, BUSTER_PROFILE_SPILL_LABEL "%llu:\n", (unsigned long long)labels);
            labels += 1;
        }
        profile_append(output, profile_slice(assembly, start, position));
    }
    const bool result = found && !output->full;
    if (!found)
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

// Attributes the samples in `samples_text` (perf script -F ip,sym,symoff of a program built from label-spills' output)
// to the spills and reloads of `function` in `labeled` (that output), and ranks their stack slots.
static bool profile_spill_report(Arena* arena, ProfileReport* report, String8 function, String8 labeled, String8 samples_text, uint64_t slot_count)
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
    // The function's starts among the samples (ip less symoff), one per copy that has samples, and their samples.
    uint64_t sample_lines = 0;
    position = 0;
    while (position < samples_text.length)
    {
        profile_line(samples_text, &position);
        sample_lines += 1;
    }
    uint64_t* starts = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, sample_lines + 1);
    uint64_t* start_samples = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, sample_lines + 1);
    if (spills == 0 || slots == 0 || starts == 0 || start_samples == 0)
    {
        profile_print(report, "spill-report: out of memory\n");
    }
    else
    {
        // The labels, in order: label n is spill n, with the instruction on the line after it.
        uint64_t spill_count = 0;
        bool found = false;
        bool inside = false;
        bool numbered = true;
        bool pending = false;
        String8 location = { 0 };
        const String8 label = profile_string(BUSTER_PROFILE_SPILL_LABEL);
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
            else if (inside && profile_starts_with(line, label) && line.pointer[line.length - 1] == ':')
            {
                uint64_t number = 0;
                numbered = numbered && profile_decimal(profile_slice(line, label.length, line.length - 1), &number) && number == spill_count;
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
                    .reload = profile_find(trimmed, profile_string("Reload")) != trimmed.length,
                };
                spill_count += 1;
                pending = false;
            }
        }

        if (!found)
        {
            profile_print(report, "spill-report: %.*s isn't in the assembly\n", (int)function.length, function.pointer);
        }
        else if (!numbered || pending)
        {
            profile_print(report, "spill-report: the labels aren't label-spills' (numbered from 0 in order, each before an instruction)\n");
        }
        else
        {
            // Samples at each label's first byte, the instruction after it, and after it up to the next label, which
            // perf names by the label before them. The function's own name only covers the labeled copy up to its first
            // label, and names the copies of other translation units (each copy of c_lexer.c has a tokenize_c): the
            // labeled copy's start is the greatest one at or below its labels.
            uint64_t at_labels = 0;
            uint64_t at_spills = 0;
            uint64_t at_reloads = 0;
            uint64_t lowest_label = UINT64_MAX;
            uint64_t start_count = 0;
            position = 0;
            while (position < samples_text.length)
            {
                const ProfileSample sample = profile_sample(profile_line(samples_text, &position));
                uint64_t number = 0;
                const bool labeled = profile_starts_with(sample.symbol, label) && profile_decimal(profile_slice(sample.symbol, label.length, sample.symbol.length), &number) &&
                                     number < spill_count;
                if (labeled)
                {
                    at_labels += 1;
                    lowest_label = sample.ip - sample.offset < lowest_label ? sample.ip - sample.offset : lowest_label;
                    if (sample.offset == 0)
                    {
                        spills[number].samples += 1;
                        at_reloads += spills[number].reload;
                        at_spills += !spills[number].reload;
                    }
                }
                else if (profile_equal(sample.symbol, function))
                {
                    const uint64_t start = sample.ip - sample.offset;
                    uint64_t found_start = start_count;
                    for (uint64_t k = 0; k < start_count; k += 1)
                    {
                        found_start = starts[k] == start ? k : found_start;
                    }
                    if (found_start == start_count)
                    {
                        starts[start_count] = start;
                        start_samples[start_count] = 0;
                        start_count += 1;
                    }
                    start_samples[found_start] += 1;
                }
            }
            uint64_t labeled_start = start_count;
            for (uint64_t k = 0; k < start_count; k += 1)
            {
                const bool below = starts[k] <= lowest_label;
                labeled_start = below && (labeled_start == start_count || starts[k] > starts[labeled_start]) ? k : labeled_start;
            }
            const uint64_t total = at_labels + (labeled_start != start_count ? start_samples[labeled_start] : 0);

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

            profile_print(report, "spill-report: %llu samples in %.*s, %llu (%.2f%%) at its %llu spills and reloads: %llu spills, %llu reloads, in %llu slots\n",
                          (unsigned long long)total, (int)function.length, function.pointer, (unsigned long long)(at_spills + at_reloads),
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
        valid = argc == 4;
        const String8 assembly = valid ? profile_read_file(arena, argv[2]) : (String8){ 0 };
        ProfileReport labeled = { .arena = arena_reserve(BUSTER_PROFILE_REPORT_CAPACITY) };
        if (assembly.pointer != 0)
        {
            succeeded = profile_label_spills(&labeled, report, profile_string(argv[1]), assembly);
            output = profile_report_text(&labeled);
            output_path = argv[3];
        }
        if (succeeded)
        {
            succeeded = profile_write_file(output_path, output);
        }
        arena_release(&labeled.arena);
        if (!valid)
        {
            fprintf(stderr, "usage: label-spills FUNCTION INPUT.s OUTPUT.s\n");
        }
    }
    else if (strcmp(argv[0], "spill-report") == 0)
    {
        count = BUSTER_PROFILE_DEFAULT_SLOTS;
        int i = 4;
        while (valid && i < argc)
        {
            valid = profile_option(argc, argv, &i, "--slots", &count);
        }
        valid = valid && argc >= 4;
        const String8 labeled = valid ? profile_read_file(arena, argv[2]) : (String8){ 0 };
        const String8 samples = valid && labeled.pointer != 0 ? profile_read_file(arena, argv[3]) : (String8){ 0 };
        if (labeled.pointer != 0 && samples.pointer != 0)
        {
            succeeded = profile_spill_report(arena, report, profile_string(argv[1]), labeled, samples, count);
        }
        if (!valid)
        {
            fprintf(stderr, "usage: spill-report FUNCTION LABELED.s SAMPLES [--slots N]\n");
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
    else
    {
        result = run_profile_text_command(&arena, &report, argc, argv);
    }
    arena_release(&report.arena);
    arena_release(&arena);
    return result;
}
