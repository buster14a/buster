// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// The compile's count (#2850; docs/commands/ci.md, "The compile's count before the merge"): what a change does to the
// work of sqlite3.c's compile, counted where no hardware counter exists. The compiler built with clang's
// -fprofile-instr-generate writes a raw profile as it exits, a counter for each region of its own source; the count is
// the sum of those counters after one compile. tools/ci/compile-count.sh builds a change and its base that way, compiles
// the frozen sqlite3.c with each, and asks this for the verdict.
//
//   compile-count sum PROFILE...
//       Each profile's count and its name, a line each.
//   compile-count check --base-statement FILE --head-statement FILE --base PROFILE... --head PROFILE...
//       The verdict on a change (head) against its base. A side's count is the median of its profiles' counts. The
//       change moves the count by its percentage of the base's. A statement is the last line of
//       tools/ci/compile-count.txt that isn't empty or a comment, "PERCENT what moved it": the change states a move
//       when its line isn't its base's, and states none otherwise. Only the change's line is read as a statement: its
//       base's is compared as text, so that a base without the file, or with a line that is no statement, holds no
//       later change back (each then states a move, and is judged by it). The change passes when its move is within
//       half a percent of what it states, above or below.
//   compile-count statement FILE
//       A statement file's percentage and its line, where it has a well-formed one.
//   compile-count --self-test
//   compile-count --fixture FILE COUNT
//       Writes a raw profile whose count is COUNT, one function's one counter, for tests/ci/compile-count.sh.
//
// Exit status: 0 passed, 1 the change failed (a move it doesn't state, or one it misstates), 2 nothing could be judged
// (usage, a file, a profile's format, runs of one executable too far apart).
//
// The raw profile is the format clang 23.1.4 writes for a 64-bit little-endian target, version 11: 19 words of header
// (the magic, the version, the binary identifiers' bytes, the data records' count, the padding before the counters, the
// counters' count, then 13 words of sizes and addresses of what follows the counters, which this doesn't read), the
// binary identifiers, the data records of 72 bytes each, the padding, and the counters, a word each. The header's
// leading fields and the sections' order are as LLVM's "Instrumentation Profile Format" documents them (its diagrams
// leave the padding words out); version 11's header length, its record's size and the place of a record's count of
// counters (32 bits at byte 56) are read off the files clang 23.1.4 writes, of which the self-test holds one. The records' counts add up to the header's: profile_count
// checks that, so a file laid out otherwise is refused rather than misread. Another version is refused by its number;
// a toolchain's upgrade that changes it changes this reader with it.
//
// Plain C17 for any x86-64 runner (the CI glue's fixtures build it with the image's cc); outside the compiler's paths.

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// "\377lprofr\201" read as a little-endian word.
#define BUSTER_COUNT_MAGIC 0xff6c70726f667281ull
#define BUSTER_COUNT_VERSION 11u
#define BUSTER_COUNT_HEADER_WORDS 19u
#define BUSTER_COUNT_RECORD_BYTES 72u
// A data record's count of its function's counters, 32 bits.
#define BUSTER_COUNT_RECORD_COUNTERS 56u
// A change's move and its statement may be this far apart, in thousandths of a percent: half a percent (the owner's
// choice on 2026-11-23, #2850).
#define BUSTER_COUNT_BAND 500
// Runs of one executable on one input may be this far apart, in thousandths of a percent of their median: a tenth of
// the band. They are 0.0004% apart where measured.
#define BUSTER_COUNT_SPREAD 50
#define BUSTER_COUNT_RUNS 16u
// A statement's percentage is at most this many thousandths (a count may not move tenfold in one change), and a count
// at most this (so that no product below leaves 63 bits).
#define BUSTER_COUNT_STATED_MAXIMUM 1000000
#define BUSTER_COUNT_MAXIMUM (1ull << 40)
#define BUSTER_COUNT_LINE_BYTES 512u

typedef enum CountStatus CountStatus;
enum CountStatus
{
    COUNT_PASSED = 0,
    COUNT_FAILED = 1,
    COUNT_UNUSABLE = 2
};

typedef struct CountStatement CountStatement;
struct CountStatement
{
    // The line, without its end; empty where the file has none.
    char line[BUSTER_COUNT_LINE_BYTES];
    size_t length;
    // Its percentage in thousandths.
    int64_t stated;
};

typedef struct CountSide CountSide;
struct CountSide
{
    uint64_t counts[BUSTER_COUNT_RUNS];
    size_t runs;
    uint64_t median;
    uint64_t least;
    uint64_t most;
};

typedef struct CountVerdict CountVerdict;
struct CountVerdict
{
    CountStatus status;
    // The change's move in thousandths of a percent of the base's count, rounded toward zero.
    int64_t moved;
    // What it states, 0 where it states nothing.
    int64_t stated;
    bool states;
};

static uint64_t count_word(const uint8_t* bytes)
{
    uint64_t word = 0;
    for (size_t i = 0; i < 8; i += 1)
    {
        word |= (uint64_t)bytes[i] << (8 * i);
    }
    return word;
}

// The count of a raw profile's `length` bytes into *count, or why it has none (a static text) as the result; NULL when
// it has one.
static const char* profile_count(const uint8_t* bytes, size_t length, uint64_t* count)
{
    const char* refusal = NULL;
    uint64_t header[BUSTER_COUNT_HEADER_WORDS] = { 0 };
    if (length < 8 * BUSTER_COUNT_HEADER_WORDS)
    {
        refusal = "shorter than a raw profile's header";
    }
    else
    {
        for (size_t i = 0; i < BUSTER_COUNT_HEADER_WORDS; i += 1)
        {
            header[i] = count_word(bytes + 8 * i);
        }
        if (header[0] != BUSTER_COUNT_MAGIC)
        {
            refusal = "not a 64-bit little-endian raw profile (its magic)";
        }
        else if (header[1] != BUSTER_COUNT_VERSION)
        {
            // The version's high bits are the instrumentation's variant, none for -fprofile-instr-generate.
            refusal = "not raw profile version 11 of clang's -fprofile-instr-generate, the one this reads";
        }
    }
    if (refusal == NULL)
    {
        const uint64_t identifiers = header[2];
        const uint64_t records = header[3];
        const uint64_t padding = header[4];
        const uint64_t counters = header[5];
        const uint64_t room = length - 8 * BUSTER_COUNT_HEADER_WORDS;
        // Each term is checked against the file's length before the next is added, so no sum leaves 64 bits.
        bool fits = identifiers <= room;
        uint64_t offset = 8 * BUSTER_COUNT_HEADER_WORDS + (fits ? identifiers : 0);
        fits = fits && records <= (length - offset) / BUSTER_COUNT_RECORD_BYTES;
        const uint64_t first_record = offset;
        offset += fits ? records * BUSTER_COUNT_RECORD_BYTES : 0;
        fits = fits && padding <= length - offset;
        offset += fits ? padding : 0;
        fits = fits && counters <= (length - offset) / 8;
        if (!fits)
        {
            refusal = "shorter than its header's records and counters";
        }
        else
        {
            uint64_t declared = 0;
            for (uint64_t i = 0; i < records; i += 1)
            {
                const uint8_t* field = bytes + first_record + i * BUSTER_COUNT_RECORD_BYTES + BUSTER_COUNT_RECORD_COUNTERS;
                declared += (uint64_t)field[0] | (uint64_t)field[1] << 8 | (uint64_t)field[2] << 16 | (uint64_t)field[3] << 24;
            }
            if (declared != counters)
            {
                refusal = "laid out otherwise than version 11 as this reads it (its records' counters don't add up to its header's)";
            }
            else
            {
                uint64_t sum = 0;
                bool overflow = false;
                for (uint64_t i = 0; i < counters; i += 1)
                {
                    const uint64_t counter = count_word(bytes + offset + 8 * i);
                    overflow = overflow || counter > BUSTER_COUNT_MAXIMUM || sum > BUSTER_COUNT_MAXIMUM;
                    sum += overflow ? 0 : counter;
                }
                if (overflow || sum > BUSTER_COUNT_MAXIMUM)
                {
                    refusal = "a count past 2^40, which no compile of this input reaches";
                }
                else if (sum == 0)
                {
                    refusal = "a raw profile whose counters add up to nothing (none, or all zero)";
                }
                else
                {
                    *count = sum;
                }
            }
        }
    }
    return refusal;
}

// The statement of a statement file's `length` bytes: its last line that isn't empty or a comment (#). True when the
// text has no such line (an empty statement), or has one that starts with a percentage: an optional sign, digits, at
// most three more after a point and an optional percent sign, then a space or the line's end. With `text_only` (a
// base's file, whose line is only compared) the line is taken whatever it holds, as much of it as a statement's line
// may be long, and its percentage is 0 where it has none.
static bool statement_parse(const char* text, size_t length, bool text_only, CountStatement* statement)
{
    bool valid = true;
    size_t line_start = 0;
    size_t found_start = 0;
    size_t found_length = 0;
    bool found = false;
    for (size_t i = 0; i <= length; i += 1)
    {
        if (i == length || text[i] == '\n')
        {
            size_t start = line_start;
            size_t end = i;
            while (start < end && (text[start] == ' ' || text[start] == '\t'))
            {
                start += 1;
            }
            while (end > start && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r'))
            {
                end -= 1;
            }
            if (end > start && text[start] != '#')
            {
                found = true;
                found_start = start;
                found_length = end - start;
            }
            line_start = i + 1;
        }
    }
    memset(statement, 0, sizeof(*statement));
    if (found)
    {
        const char* line = text + found_start;
        size_t at = 0;
        bool negative = false;
        if (line[at] == '+' || line[at] == '-')
        {
            negative = line[at] == '-';
            at += 1;
        }
        int64_t whole = 0;
        size_t whole_digits = 0;
        while (at < found_length && line[at] >= '0' && line[at] <= '9' && whole_digits < 5)
        {
            whole = whole * 10 + (line[at] - '0');
            whole_digits += 1;
            at += 1;
        }
        int64_t thousandths = 0;
        size_t fraction_digits = 0;
        bool point = false;
        if (at < found_length && line[at] == '.')
        {
            point = true;
            at += 1;
            while (at < found_length && line[at] >= '0' && line[at] <= '9' && fraction_digits < 4)
            {
                thousandths = thousandths * 10 + (line[at] - '0');
                fraction_digits += 1;
                at += 1;
            }
        }
        for (size_t digit = fraction_digits; digit < 3; digit += 1)
        {
            thousandths *= 10;
        }
        if (at < found_length && line[at] == '%')
        {
            at += 1;
        }
        const bool ended = at == found_length || line[at] == ' ' || line[at] == '\t';
        const int64_t stated = whole * 1000 + thousandths;
        const bool well_formed = whole_digits > 0 && fraction_digits <= 3 && (!point || fraction_digits > 0) && ended && stated <= BUSTER_COUNT_STATED_MAXIMUM && found_length < BUSTER_COUNT_LINE_BYTES;
        valid = well_formed || text_only;
        if (valid)
        {
            // A base's line past the longest a statement holds is cut there: no statement's line equals it then, its
            // length being one no statement has.
            const size_t kept = found_length < BUSTER_COUNT_LINE_BYTES ? found_length : BUSTER_COUNT_LINE_BYTES - 1;
            memcpy(statement->line, line, kept);
            statement->length = well_formed ? found_length : found_length < BUSTER_COUNT_LINE_BYTES ? found_length : BUSTER_COUNT_LINE_BYTES;
            statement->stated = !well_formed ? 0 : negative ? -stated : stated;
        }
    }
    return valid;
}

// The side's median, least and most count. False for no runs or too many.
static bool side_reduce(CountSide* side)
{
    const size_t runs = side->runs;
    const bool valid = runs > 0 && runs <= BUSTER_COUNT_RUNS;
    if (valid)
    {
        uint64_t sorted[BUSTER_COUNT_RUNS];
        memcpy(sorted, side->counts, runs * sizeof(sorted[0]));
        for (size_t i = 1; i < runs; i += 1)
        {
            const uint64_t value = sorted[i];
            size_t at = i;
            while (at > 0 && sorted[at - 1] > value)
            {
                sorted[at] = sorted[at - 1];
                at -= 1;
            }
            sorted[at] = value;
        }
        side->least = sorted[0];
        side->most = sorted[runs - 1];
        // The lower of an even count's two middle runs.
        side->median = sorted[(runs - 1) / 2];
    }
    return valid;
}

// True when the side's runs are within BUSTER_COUNT_SPREAD of their median.
static bool side_steady(const CountSide* side)
{
    return (side->most - side->least) * 100000 <= (uint64_t)BUSTER_COUNT_SPREAD * side->median;
}

// The verdict on a head count against a base count, both positive and at most BUSTER_COUNT_MAXIMUM, where the change
// states `stated` thousandths of a percent (`states`) or nothing.
static CountVerdict count_judge(uint64_t base, uint64_t head, bool states, int64_t stated)
{
    CountVerdict verdict = { 0 };
    const int64_t difference = (int64_t)head - (int64_t)base;
    const int64_t claimed = states ? stated : 0;
    // |difference / base - claimed / 100000| <= band / 100000, in whole numbers: every product is below 2^63, a count
    // being at most 2^40 and a statement at most 10^6.
    int64_t apart = difference * 100000 - claimed * (int64_t)base;
    apart = apart < 0 ? -apart : apart;
    verdict.status = apart <= (int64_t)BUSTER_COUNT_BAND * (int64_t)base ? COUNT_PASSED : COUNT_FAILED;
    verdict.moved = difference * 100000 / (int64_t)base;
    verdict.stated = claimed;
    verdict.states = states;
    return verdict;
}

// A move's text, "+7.021%", into `text` (at least 24 bytes).
static void count_percent(int64_t thousandths, char* text, size_t capacity)
{
    const int64_t magnitude = thousandths < 0 ? -thousandths : thousandths;
    snprintf(text, capacity, "%c%" PRId64 ".%03" PRId64 "%%", thousandths < 0 ? '-' : '+', magnitude / 1000, magnitude % 1000);
}

// The line a change would state a move with, "+7.25 what moved it", into `text` (at least 48 bytes): the move to the
// nearest hundredth of a percent.
static void count_proposal(int64_t thousandths, char* text, size_t capacity)
{
    const int64_t magnitude = ((thousandths < 0 ? -thousandths : thousandths) + 5) / 10;
    snprintf(text, capacity, "%c%" PRId64 ".%02" PRId64 " what moved it", thousandths < 0 ? '-' : '+', magnitude / 100, magnitude % 100);
}

// A whole file's bytes, allocated, and their count; NULL with a message on stderr when it can't be read. *missing says
// that it doesn't exist, where the caller allows that (no message then).
static uint8_t* file_read(const char* path, size_t* length, bool* missing)
{
    uint8_t* bytes = NULL;
    FILE* file = fopen(path, "rb");
    if (file == NULL)
    {
        if (missing != NULL && errno == ENOENT)
        {
            *missing = true;
        }
        else
        {
            fprintf(stderr, "compile-count: can't open %s: %s\n", path, strerror(errno));
        }
    }
    else
    {
        size_t capacity = 1u << 16;
        size_t used = 0;
        bytes = malloc(capacity);
        bool failed = bytes == NULL;
        while (!failed)
        {
            const size_t taken = fread(bytes + used, 1, capacity - used, file);
            used += taken;
            if (taken == 0)
            {
                break;
            }
            if (used == capacity)
            {
                uint8_t* grown = capacity <= SIZE_MAX / 2 ? realloc(bytes, capacity * 2) : NULL;
                failed = grown == NULL;
                if (!failed)
                {
                    bytes = grown;
                    capacity *= 2;
                }
            }
        }
        failed = failed || ferror(file) != 0;
        fclose(file);
        if (failed)
        {
            fprintf(stderr, "compile-count: can't read %s\n", path);
            free(bytes);
            bytes = NULL;
        }
        else
        {
            *length = used;
        }
    }
    return bytes;
}

static bool file_count(const char* path, uint64_t* count)
{
    size_t length = 0;
    uint8_t* bytes = file_read(path, &length, NULL);
    bool counted = false;
    if (bytes != NULL)
    {
        const char* refusal = profile_count(bytes, length, count);
        counted = refusal == NULL;
        if (!counted)
        {
            fprintf(stderr, "compile-count: %s: %s\n", path, refusal);
        }
        free(bytes);
    }
    return counted;
}

// A statement file's statement. A base's (`base`) is its line as text, an empty one where the file doesn't exist; a
// change's must be there and be well formed.
static bool file_statement(const char* path, bool base, CountStatement* statement)
{
    size_t length = 0;
    bool missing = false;
    uint8_t* bytes = file_read(path, &length, base ? &missing : NULL);
    bool read = false;
    if (bytes != NULL)
    {
        read = statement_parse((const char*)bytes, length, base, statement);
        if (!read)
        {
            fprintf(stderr, "compile-count: %s: its last line that isn't a comment must start with a percentage, as in \"+7.02 what moved the count\"\n", path);
        }
        free(bytes);
    }
    else if (missing)
    {
        memset(statement, 0, sizeof(*statement));
        read = true;
    }
    return read;
}

static int command_sum(int argument_count, char** arguments)
{
    int status = argument_count > 0 ? COUNT_PASSED : COUNT_UNUSABLE;
    for (int i = 0; i < argument_count; i += 1)
    {
        uint64_t count = 0;
        if (file_count(arguments[i], &count))
        {
            printf("%" PRIu64 "\t%s\n", count, arguments[i]);
        }
        else
        {
            status = COUNT_UNUSABLE;
        }
    }
    if (argument_count == 0)
    {
        fprintf(stderr, "usage: compile-count sum PROFILE...\n");
    }
    return status;
}

static int command_statement(const char* path)
{
    CountStatement statement = { 0 };
    int status = file_statement(path, false, &statement) ? COUNT_PASSED : COUNT_UNUSABLE;
    if (status == COUNT_PASSED && statement.length == 0)
    {
        fprintf(stderr, "compile-count: %s has no statement line\n", path);
        status = COUNT_UNUSABLE;
    }
    if (status == COUNT_PASSED)
    {
        char stated[32];
        count_percent(statement.stated, stated, sizeof(stated));
        printf("%s\t%s\n", stated, statement.line);
    }
    return status;
}

static void side_print(const char* name, const CountSide* side)
{
    printf("compile-count: %s: %" PRIu64 " (%zu %s, %" PRIu64 " to %" PRIu64 ")\n", name, side->median, side->runs, side->runs == 1 ? "run" : "runs", side->least, side->most);
}

static int command_check(int argument_count, char** arguments)
{
    int status = COUNT_PASSED;
    const char* base_statement_path = NULL;
    const char* head_statement_path = NULL;
    CountSide base = { 0 };
    CountSide head = { 0 };
    CountSide* side = NULL;
    for (int i = 0; i < argument_count && status == COUNT_PASSED; i += 1)
    {
        const char* argument = arguments[i];
        const bool takes_value = strcmp(argument, "--base-statement") == 0 || strcmp(argument, "--head-statement") == 0;
        if (takes_value && i + 1 < argument_count)
        {
            if (strcmp(argument, "--base-statement") == 0)
            {
                base_statement_path = arguments[i + 1];
            }
            else
            {
                head_statement_path = arguments[i + 1];
            }
            side = NULL;
            i += 1;
        }
        else if (strcmp(argument, "--base") == 0)
        {
            side = &base;
        }
        else if (strcmp(argument, "--head") == 0)
        {
            side = &head;
        }
        else if (side != NULL && !takes_value && side->runs < BUSTER_COUNT_RUNS)
        {
            status = file_count(argument, &side->counts[side->runs]) ? COUNT_PASSED : COUNT_UNUSABLE;
            side->runs += 1;
        }
        else
        {
            fprintf(stderr, "compile-count: check: unexpected argument %s\n", argument);
            status = COUNT_UNUSABLE;
        }
    }
    if (status == COUNT_PASSED && (base_statement_path == NULL || head_statement_path == NULL || !side_reduce(&base) || !side_reduce(&head)))
    {
        fprintf(stderr, "usage: compile-count check --base-statement FILE --head-statement FILE --base PROFILE... --head PROFILE... (at most %u profiles a side)\n", BUSTER_COUNT_RUNS);
        status = COUNT_UNUSABLE;
    }
    CountStatement base_statement = { 0 };
    CountStatement head_statement = { 0 };
    if (status == COUNT_PASSED && (!file_statement(base_statement_path, true, &base_statement) || !file_statement(head_statement_path, false, &head_statement)))
    {
        status = COUNT_UNUSABLE;
    }
    if (status == COUNT_PASSED && head_statement.length == 0)
    {
        fprintf(stderr, "compile-count: %s has no statement line\n", head_statement_path);
        status = COUNT_UNUSABLE;
    }
    if (status == COUNT_PASSED)
    {
        side_print("the base's compile", &base);
        side_print("the change's compile", &head);
        if (!side_steady(&base) || !side_steady(&head))
        {
            printf("compile-count: UNUSABLE: runs of one executable are more than 0.05%% apart, so the count can't hold a band of half a percent\n");
            status = COUNT_UNUSABLE;
        }
    }
    if (status == COUNT_PASSED)
    {
        const bool states = base_statement.length != head_statement.length || memcmp(base_statement.line, head_statement.line, head_statement.length) != 0;
        const CountVerdict verdict = count_judge(base.median, head.median, states, head_statement.stated);
        char moved[32];
        char stated[32];
        char proposal[64];
        count_percent(verdict.moved, moved, sizeof(moved));
        count_percent(verdict.stated, stated, sizeof(stated));
        count_proposal(verdict.moved, proposal, sizeof(proposal));
        status = verdict.status;
        if (status == COUNT_PASSED && states)
        {
            printf("compile-count: passed: the change states %s (\"%s\") and moves the count by %s\n", stated, head_statement.line, moved);
        }
        else if (status == COUNT_PASSED)
        {
            printf("compile-count: passed: the change states nothing and moves the count by %s, within half a percent\n", moved);
        }
        else if (states)
        {
            printf("compile-count: FAILED: the change states %s (\"%s\") and moves the count by %s, more than half a percent from it. Its line in tools/ci/compile-count.txt states its own move against its base, \"%s\", or stays its base's where it moves the count less than half a percent\n", stated, head_statement.line, moved, proposal);
        }
        else
        {
            printf("compile-count: FAILED: the change moves the count by %s and doesn't state it. A change that means to move the compile's work, a feature's cost or a cut's gain, replaces the line of tools/ci/compile-count.txt with its own: \"%s\"\n", moved, proposal);
        }
    }
    return status;
}

// --- The self-test ---

#define BUSTER_COUNT_TEST_BYTES 4096u

typedef struct CountTestProfile CountTestProfile;
struct CountTestProfile
{
    uint8_t bytes[BUSTER_COUNT_TEST_BYTES];
    size_t length;
};

static void test_word(uint8_t* bytes, uint64_t word)
{
    for (size_t i = 0; i < 8; i += 1)
    {
        bytes[i] = (uint8_t)(word >> (8 * i));
    }
}

// A profile of `records` functions with `each` counters apiece, every counter `value`, after `identifiers` bytes of
// binary identifiers and before `padding` bytes.
static void test_profile(CountTestProfile* profile, uint64_t identifiers, uint64_t records, uint32_t each, uint64_t padding, uint64_t value)
{
    memset(profile, 0, sizeof(*profile));
    uint8_t* bytes = profile->bytes;
    test_word(bytes, BUSTER_COUNT_MAGIC);
    test_word(bytes + 8, BUSTER_COUNT_VERSION);
    test_word(bytes + 16, identifiers);
    test_word(bytes + 24, records);
    test_word(bytes + 32, padding);
    test_word(bytes + 40, records * each);
    size_t at = 8 * BUSTER_COUNT_HEADER_WORDS + identifiers;
    for (uint64_t i = 0; i < records; i += 1)
    {
        // Every other field of a record is filled, so that a reader of the wrong field doesn't find zeros.
        memset(bytes + at, 0xa5, BUSTER_COUNT_RECORD_BYTES);
        bytes[at + BUSTER_COUNT_RECORD_COUNTERS] = (uint8_t)each;
        bytes[at + BUSTER_COUNT_RECORD_COUNTERS + 1] = (uint8_t)(each >> 8);
        bytes[at + BUSTER_COUNT_RECORD_COUNTERS + 2] = (uint8_t)(each >> 16);
        bytes[at + BUSTER_COUNT_RECORD_COUNTERS + 3] = (uint8_t)(each >> 24);
        at += BUSTER_COUNT_RECORD_BYTES;
    }
    memset(bytes + at, 0x5a, padding);
    at += padding;
    for (uint64_t i = 0; i < records * each; i += 1)
    {
        test_word(bytes + at, value + i);
        at += 8;
    }
    // A names section follows the counters in a real file; the reader stops before it.
    memset(bytes + at, 0x7f, 24);
    profile->length = at + 24;
}

static uint64_t test_failures;

static void test_expect(bool condition, const char* what)
{
    if (!condition)
    {
        test_failures += 1;
        fprintf(stderr, "compile-count: self-test: %s\n", what);
    }
}

static void test_profiles(void)
{
    CountTestProfile profile;
    uint64_t count = 0;
    // 3 functions of 4 counters: 12 counters 100, 101, ..., 111.
    test_profile(&profile, 0, 3, 4, 0, 100);
    test_expect(profile_count(profile.bytes, profile.length, &count) == NULL && count == 12 * 100 + 66, "a profile's counters add up");
    test_profile(&profile, 16, 3, 4, 8, 100);
    test_expect(profile_count(profile.bytes, profile.length, &count) == NULL && count == 12 * 100 + 66, "binary identifiers and padding come before the counters");
    test_profile(&profile, 0, 1, 1, 0, 7);
    test_expect(profile_count(profile.bytes, profile.length, &count) == NULL && count == 7, "one counter");

    test_profile(&profile, 0, 3, 4, 0, 100);
    profile.bytes[0] ^= 1;
    test_expect(profile_count(profile.bytes, profile.length, &count) != NULL, "another magic is refused");
    test_profile(&profile, 0, 3, 4, 0, 100);
    test_word(profile.bytes + 8, BUSTER_COUNT_VERSION + 1);
    test_expect(profile_count(profile.bytes, profile.length, &count) != NULL, "another version is refused");
    test_profile(&profile, 0, 3, 4, 0, 100);
    test_word(profile.bytes + 8, BUSTER_COUNT_VERSION | 1ull << 56);
    test_expect(profile_count(profile.bytes, profile.length, &count) != NULL, "another instrumentation's variant is refused");
    test_profile(&profile, 0, 3, 4, 0, 100);
    test_expect(profile_count(profile.bytes, 8 * BUSTER_COUNT_HEADER_WORDS - 1, &count) != NULL, "a cut header is refused");
    // The counters end 24 bytes before the file does: one byte fewer than they need is refused, their exact end read.
    test_expect(profile_count(profile.bytes, profile.length - 25, &count) != NULL, "cut counters are refused");
    test_expect(profile_count(profile.bytes, profile.length - 24, &count) == NULL && count == 12 * 100 + 66, "counters that end the file are read");
    // Cut inside its third record, and inside its padding: what follows is in the buffer, past the length given.
    test_expect(profile_count(profile.bytes, 8 * BUSTER_COUNT_HEADER_WORDS + 2 * BUSTER_COUNT_RECORD_BYTES + 4, &count) != NULL, "cut records are refused");
    test_profile(&profile, 16, 3, 4, 8, 100);
    test_expect(profile_count(profile.bytes, 8 * BUSTER_COUNT_HEADER_WORDS + 16 + 3 * BUSTER_COUNT_RECORD_BYTES + 4, &count) != NULL, "cut padding is refused");
    test_profile(&profile, 0, 3, 4, 0, 100);
    test_word(profile.bytes + 16, UINT64_MAX - 100);
    test_expect(profile_count(profile.bytes, profile.length, &count) != NULL, "binary identifiers past the file are refused");
    // Binary identifiers whose size wraps the offset back into the header, where the bytes then read as one record of
    // two counters (words 9 and 10): a reader that added the size unchecked would count 7.
    test_profile(&profile, 0, 1, 2, 0, 100);
    test_word(profile.bytes + 16, UINT64_MAX - 8 * BUSTER_COUNT_HEADER_WORDS + 1);
    test_word(profile.bytes + 56, 2);
    test_word(profile.bytes + 72, 3);
    test_word(profile.bytes + 80, 4);
    test_expect(profile_count(profile.bytes, 8 * BUSTER_COUNT_HEADER_WORDS, &count) != NULL, "binary identifiers that wrap the offset are refused");
    test_profile(&profile, 0, 3, 4, 0, 100);
    test_word(profile.bytes + 24, UINT64_MAX / 8);
    test_expect(profile_count(profile.bytes, profile.length, &count) != NULL, "records past the file are refused");
    test_profile(&profile, 0, 3, 4, 0, 100);
    test_word(profile.bytes + 32, UINT64_MAX - 7);
    test_expect(profile_count(profile.bytes, profile.length, &count) != NULL, "padding past the file is refused");
    test_profile(&profile, 0, 3, 4, 0, 100);
    test_word(profile.bytes + 40, UINT64_MAX / 4);
    test_expect(profile_count(profile.bytes, profile.length, &count) != NULL, "counters past the file are refused");
    test_profile(&profile, 0, 3, 4, 0, 100);
    test_word(profile.bytes + 40, 11);
    test_expect(profile_count(profile.bytes, profile.length, &count) != NULL, "a header with fewer counters than its records is refused");
    test_profile(&profile, 0, 3, 4, 0, 100);
    profile.bytes[8 * BUSTER_COUNT_HEADER_WORDS + BUSTER_COUNT_RECORD_BYTES + BUSTER_COUNT_RECORD_COUNTERS] = 5;
    test_expect(profile_count(profile.bytes, profile.length, &count) != NULL, "a record with another count is refused");
    test_profile(&profile, 0, 0, 0, 0, 0);
    test_expect(profile_count(profile.bytes, profile.length, &count) != NULL, "a profile without counters is refused");
    test_profile(&profile, 0, 3, 4, 0, 0);
    memset(profile.bytes + 8 * BUSTER_COUNT_HEADER_WORDS + 3 * BUSTER_COUNT_RECORD_BYTES, 0, 12 * 8);
    test_expect(profile_count(profile.bytes, profile.length, &count) != NULL, "counters all zero are refused");
    test_profile(&profile, 0, 3, 4, 0, BUSTER_COUNT_MAXIMUM);
    test_expect(profile_count(profile.bytes, profile.length, &count) != NULL, "a counter past the most a count holds is refused");
    // A last counter that would wrap the sum back to a small one: 5 and 2^64 - 1 add up to 4.
    test_profile(&profile, 0, 1, 2, 0, 5);
    test_word(profile.bytes + 8 * BUSTER_COUNT_HEADER_WORDS + BUSTER_COUNT_RECORD_BYTES + 8, UINT64_MAX);
    test_expect(profile_count(profile.bytes, profile.length, &count) != NULL, "a counter that wraps the sum is refused");
    test_profile(&profile, 0, 3, 4, 0, BUSTER_COUNT_MAXIMUM / 12);
    test_expect(profile_count(profile.bytes, profile.length, &count) != NULL, "counters that add up past the most a count holds are refused");
}

// A real file, so that the layout this reads is clang's and not only this file's idea of it: the raw profile the
// official clang 23.1.4 release's build (the one tools/ci/hosted-tools.lock pins) of the program below wrote on
// x86-64 Linux, `clang -O0 -fprofile-instr-generate small.c -o small && LLVM_PROFILE_FILE=small.profraw ./small`.
// Its counters are main's entry (1), its loop's body (5), the `if` taken (4) and the `?:` true (1), and twice's
// entry (4): 15. Its header's addresses are that run's.
//
//     static int twice(int value)
//     {
//         return value * 2;
//     }
//
//     int main(void)
//     {
//         int sum = 0;
//         for (int i = 0; i < 5; i += 1)
//         {
//             if (i != 2)
//             {
//                 sum += twice(i);
//             }
//         }
//         return sum == 16 ? 0 : 1;
//     }
static void test_real_profile(void)
{
    static const uint8_t profile[368] =
    {
        0x81, 0x72, 0x66, 0x6f, 0x72, 0x70, 0x6c, 0xff, 0x0b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x1c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xd8, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0x38, 0x7e, 0xd4, 0xaa, 0xcb, 0xa9, 0xff, 0xff, 0x5f, 0x4d, 0x2b, 0x55, 0x34, 0x56, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfa, 0xd5, 0x8d, 0xe7, 0x36, 0x64, 0x95, 0xdb,
        0x5f, 0x83, 0x45, 0x91, 0x04, 0x2a, 0x1b, 0x01, 0xd8, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xe3, 0x5e, 0xdb, 0x6a, 0xde, 0x1c, 0x23, 0x61, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xb0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x12, 0x1a, 0x78, 0xda, 0xcb, 0x4d, 0xcc, 0xcc, 0x63, 0x2c, 0xce, 0x4d, 0xcc, 0xc9, 0xd1, 0x4b,
        0xb6, 0x2a, 0x29, 0xcf, 0x4c, 0x4e, 0x05, 0x00, 0x3e, 0x66, 0x06, 0xa7, 0x00, 0x00, 0x00, 0x00,
    };
    uint64_t count = 0;
    test_expect(profile_count(profile, sizeof(profile), &count) == NULL && count == 15, "the count of a profile clang 23.1.4 wrote");
    // Its last 32 bytes are the functions' names, which the reader doesn't need.
    count = 0;
    test_expect(profile_count(profile, sizeof(profile) - 32, &count) == NULL && count == 15, "the same without its names");
    test_expect(profile_count(profile, sizeof(profile) - 33, &count) != NULL, "and refused a byte into its counters");
}

typedef struct CountTestStatement CountTestStatement;
struct CountTestStatement
{
    const char* text;
    bool valid;
    int64_t stated;
    const char* line;
};

static void test_statements(void)
{
    static const CountTestStatement cases[] =
    {
        { "# a comment\n+7.02 the second preprocessing\n", true, 7020, "+7.02 the second preprocessing" },
        { "-1.3 the reader's cut", true, -1300, "-1.3 the reader's cut" },
        { "0\n", true, 0, "0" },
        { "+0.00 the check itself\n", true, 0, "+0.00 the check itself" },
        { "12.345 x\n\n   \n# trailing comment\n", true, 12345, "12.345 x" },
        { "+1.00 an earlier one\n-0.75 the last line states\r\n", true, -750, "-0.75 the last line states" },
        { "  \t+2.5\twith a tab  \n", true, 2500, "+2.5\twith a tab" },
        { "1000 the most\n", true, 1000000, "1000 the most" },
        { "", true, 0, "" },
        { "# only comments\n\n", true, 0, "" },
        { "the count rose\n", false, 0, "" },
        { "+ 7.02 a sign alone\n", false, 0, "" },
        { "+7.0234 a fourth digit\n", false, 0, "" },
        { "+7. a point alone\n", false, 0, "" },
        { ".5 no whole part\n", false, 0, "" },
        { "+7.02% with a percent sign\n", true, 7020, "+7.02% with a percent sign" },
        { "-3%\n", true, -3000, "-3%" },
        { "+7.02%% two percent signs\n", false, 0, "" },
        { "+7.02%x no space after it\n", false, 0, "" },
        { "% a percent sign alone\n", false, 0, "" },
        { "+7,02 a comma\n", false, 0, "" },
        { "1e3 an exponent\n", false, 0, "" },
        { "1000.001 past the most\n", false, 0, "" },
        { "100000 six digits\n", false, 0, "" },
        { "--1 two signs\n", false, 0, "" },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i += 1)
    {
        CountStatement statement;
        const bool valid = statement_parse(cases[i].text, strlen(cases[i].text), false, &statement);
        const bool same = valid == cases[i].valid && (!valid || (statement.stated == cases[i].stated && statement.length == strlen(cases[i].line) && memcmp(statement.line, cases[i].line, statement.length) == 0));
        test_expect(same, cases[i].text);
    }
    char longest[BUSTER_COUNT_LINE_BYTES + 8];
    memset(longest, 'x', sizeof(longest));
    memcpy(longest, "+1.0 ", 5);
    CountStatement statement;
    test_expect(statement_parse(longest, BUSTER_COUNT_LINE_BYTES - 1, false, &statement) && statement.length == BUSTER_COUNT_LINE_BYTES - 1, "the longest line a statement holds");
    test_expect(!statement_parse(longest, BUSTER_COUNT_LINE_BYTES, false, &statement), "a line past the longest is refused");
    // A base's line is text alone: one that is no statement is taken, with no percentage, and one past the longest is
    // cut, at a length no statement's line has.
    static const char* const loose[] = { "the count rose", "+7.0234 a fourth digit", "+7.25: a colon", "1e3" };
    for (size_t i = 0; i < sizeof(loose) / sizeof(loose[0]); i += 1)
    {
        const bool taken = statement_parse(loose[i], strlen(loose[i]), true, &statement);
        test_expect(taken && statement.stated == 0 && statement.length == strlen(loose[i]) && memcmp(statement.line, loose[i], statement.length) == 0, loose[i]);
    }
    test_expect(statement_parse("-3.00 a cut", 11, true, &statement) && statement.stated == -3000 && statement.length == 11, "a base's well-formed line keeps its percentage");
    test_expect(statement_parse(longest, sizeof(longest), true, &statement) && statement.length == BUSTER_COUNT_LINE_BYTES && statement.line[BUSTER_COUNT_LINE_BYTES - 1] == 0, "a base's line past the longest is cut");
    test_expect(statement_parse("# only a comment\n", 17, true, &statement) && statement.length == 0, "a base without a line states nothing");
}

typedef struct CountTestVerdict CountTestVerdict;
struct CountTestVerdict
{
    uint64_t base;
    uint64_t head;
    bool states;
    int64_t stated;
    CountStatus status;
    int64_t moved;
};

static void test_verdicts(void)
{
    static const CountTestVerdict cases[] =
    {
        // Nothing stated: half a percent either way passes, one count more fails.
        { 1000000, 1000000, false, 0, COUNT_PASSED, 0 },
        { 1000000, 1005000, false, 0, COUNT_PASSED, 500 },
        { 1000000, 1005001, false, 0, COUNT_FAILED, 500 },
        { 1000000, 995000, false, 0, COUNT_PASSED, -500 },
        { 1000000, 994999, false, 0, COUNT_FAILED, -500 },
        // A statement that nothing reads (the line is the base's) doesn't excuse a move.
        { 1000000, 1070000, false, 7000, COUNT_FAILED, 7000 },
        // Stated: half a percent around the statement, above and below.
        { 1000000, 1070000, true, 7000, COUNT_PASSED, 7000 },
        { 1000000, 1075000, true, 7000, COUNT_PASSED, 7500 },
        { 1000000, 1075001, true, 7000, COUNT_FAILED, 7500 },
        { 1000000, 1065000, true, 7000, COUNT_PASSED, 6500 },
        { 1000000, 1064999, true, 7000, COUNT_FAILED, 6499 },
        { 1000000, 970000, true, -3000, COUNT_PASSED, -3000 },
        { 1000000, 964999, true, -3000, COUNT_FAILED, -3500 },
        // A statement of a move the change doesn't make fails, and so does one of the wrong sign.
        { 1000000, 1000000, true, 7000, COUNT_FAILED, 0 },
        { 1000000, 1030000, true, -3000, COUNT_FAILED, 3000 },
        // A statement of nothing is as stating nothing.
        { 1000000, 1005000, true, 0, COUNT_PASSED, 500 },
        { 1000000, 1005001, true, 0, COUNT_FAILED, 500 },
        // The band is the base's half percent, not the head's: 315,115,702 allows 1,575,578 counts.
        { 315115702, 316691280, false, 0, COUNT_PASSED, 499 },
        { 315115702, 316691281, false, 0, COUNT_FAILED, 500 },
        { 315115702, 313540124, false, 0, COUNT_PASSED, -499 },
        { 315115702, 313540123, false, 0, COUNT_FAILED, -500 },
        // The largest counts and statement: no product leaves 63 bits.
        { BUSTER_COUNT_MAXIMUM, 1, true, -BUSTER_COUNT_STATED_MAXIMUM, COUNT_FAILED, -99999 },
        { 1, BUSTER_COUNT_MAXIMUM, true, BUSTER_COUNT_STATED_MAXIMUM, COUNT_FAILED, (int64_t)(BUSTER_COUNT_MAXIMUM - 1) * 100000 },
        { BUSTER_COUNT_MAXIMUM / 11, BUSTER_COUNT_MAXIMUM, true, BUSTER_COUNT_STATED_MAXIMUM, COUNT_PASSED, 1000000 },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i += 1)
    {
        const CountVerdict verdict = count_judge(cases[i].base, cases[i].head, cases[i].states, cases[i].stated);
        char what[128];
        snprintf(what, sizeof(what), "verdict %zu: base %" PRIu64 ", head %" PRIu64 ": status %d, moved %" PRId64, i, cases[i].base, cases[i].head, (int)verdict.status, verdict.moved);
        test_expect(verdict.status == cases[i].status && verdict.moved == cases[i].moved, what);
    }

    CountSide side = { .counts = { 315115702, 315114597, 315115704 }, .runs = 3 };
    test_expect(side_reduce(&side) && side.median == 315115702 && side.least == 315114597 && side.most == 315115704 && side_steady(&side), "three runs' median, and their spread within a twentieth of a percent");
    side = (CountSide){ .counts = { 9, 3 }, .runs = 2 };
    test_expect(side_reduce(&side) && side.median == 3 && side.least == 3 && side.most == 9, "the lower middle of two runs");
    side = (CountSide){ .counts = { 1000000, 1000500, 1000000 }, .runs = 3 };
    test_expect(side_reduce(&side) && side.median == 1000000 && side_steady(&side), "runs exactly a twentieth of a percent of their median apart are steady");
    side = (CountSide){ .counts = { 1000000, 1000501, 1000000 }, .runs = 3 };
    test_expect(side_reduce(&side) && !side_steady(&side), "runs one count further apart are not");
    // A low run counts as a high one does: the spread is the most's from the least, not from the median.
    side = (CountSide){ .counts = { 1000000, 999500, 1000000 }, .runs = 3 };
    test_expect(side_reduce(&side) && side.median == 1000000 && side_steady(&side), "a low run a twentieth of a percent below is steady");
    side = (CountSide){ .counts = { 1000000, 999499, 1000000 }, .runs = 3 };
    test_expect(side_reduce(&side) && !side_steady(&side), "a low run one count further below is not");
    side = (CountSide){ .runs = 0 };
    test_expect(!side_reduce(&side), "no runs are refused");
    side = (CountSide){ .runs = BUSTER_COUNT_RUNS + 1 };
    test_expect(!side_reduce(&side), "more runs than a side holds are refused");

    char text[32];
    count_percent(7021, text, sizeof(text));
    test_expect(strcmp(text, "+7.021%") == 0, "a rise's text");
    count_percent(-5, text, sizeof(text));
    test_expect(strcmp(text, "-0.005%") == 0, "a small cut's text");
    count_percent(0, text, sizeof(text));
    test_expect(strcmp(text, "+0.000%") == 0, "no move's text");
    char proposal[64];
    CountStatement proposed;
    count_proposal(7250, proposal, sizeof(proposal));
    test_expect(strcmp(proposal, "+7.25 what moved it") == 0 && statement_parse(proposal, strlen(proposal), false, &proposed) && proposed.stated == 7250, "a rise's proposed line, which states it");
    count_proposal(-1305, proposal, sizeof(proposal));
    test_expect(strcmp(proposal, "-1.31 what moved it") == 0 && statement_parse(proposal, strlen(proposal), false, &proposed) && proposed.stated == -1310, "a cut's proposed line, to the nearest hundredth");
    count_proposal(-1304, proposal, sizeof(proposal));
    test_expect(strcmp(proposal, "-1.30 what moved it") == 0, "a cut's proposed line, rounded down");
}

static int command_fixture(const char* path, const char* text)
{
    int status = COUNT_UNUSABLE;
    char* end = NULL;
    errno = 0;
    const unsigned long long count = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != 0 || count == 0 || count > BUSTER_COUNT_MAXIMUM)
    {
        fprintf(stderr, "compile-count: --fixture: %s is no count\n", text);
    }
    else
    {
        CountTestProfile profile;
        test_profile(&profile, 0, 1, 1, 0, count);
        FILE* file = fopen(path, "wb");
        const bool written = file != NULL && fwrite(profile.bytes, 1, profile.length, file) == profile.length;
        const bool closed = file != NULL && fclose(file) == 0;
        if (written && closed)
        {
            status = COUNT_PASSED;
        }
        else
        {
            fprintf(stderr, "compile-count: --fixture: can't write %s\n", path);
        }
    }
    return status;
}

static int command_self_test(void)
{
    test_profiles();
    test_real_profile();
    test_statements();
    test_verdicts();
    printf("compile-count: self-test: %" PRIu64 " failures\n", test_failures);
    return test_failures == 0 ? COUNT_PASSED : COUNT_FAILED;
}

int main(int argument_count, char** arguments)
{
    int status = COUNT_UNUSABLE;
    if (argument_count >= 2 && strcmp(arguments[1], "sum") == 0)
    {
        status = command_sum(argument_count - 2, arguments + 2);
    }
    else if (argument_count >= 2 && strcmp(arguments[1], "check") == 0)
    {
        status = command_check(argument_count - 2, arguments + 2);
    }
    else if (argument_count == 3 && strcmp(arguments[1], "statement") == 0)
    {
        status = command_statement(arguments[2]);
    }
    else if (argument_count == 2 && strcmp(arguments[1], "--self-test") == 0)
    {
        status = command_self_test();
    }
    else if (argument_count == 4 && strcmp(arguments[1], "--fixture") == 0)
    {
        status = command_fixture(arguments[2], arguments[3]);
    }
    else
    {
        fprintf(stderr, "usage: compile-count sum PROFILE... | check --base-statement FILE --head-statement FILE --base PROFILE... --head PROFILE... | statement FILE | --self-test | --fixture FILE COUNT\n");
    }
    return status;
}
