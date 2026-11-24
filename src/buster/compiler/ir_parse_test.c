// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original text fixtures and parser-contract tests, included after ir_parse.c. Canonical fixture bytes are written
// independently of the parser and printer. Both parser paths must preserve those bytes through a complete round trip,
// agree on every column and diagnostic, and roll back both arenas and the unpublished result on failure. Source slices
// have no sentinel or readable padding. ir_parse_test_guards puts every prefix against a protected page.

#include <dirent.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define BUSTER_IR_PARSE_TEST_BYTES (16ull << 20)
#define BUSTER_IR_PARSE_TEST_REPORTS 20

typedef struct IrParseTest IrParseTest;
struct IrParseTest
{
    uint64_t checks;
    uint64_t failures;
    uint64_t functions;
    // Every parse's diagnostic and published columns, in order: frozen in #2924 while the line scan's scalar twin agreed.
    uint64_t digest;
    bool operations[IR_OP_COUNT];
    bool types[IR_TYPE_COUNT];
};

static void ir_parse_test_expect(IrParseTest* test, bool condition, const char* message)
{
    test->checks += 1;
    if (!condition)
    {
        if (test->failures < BUSTER_IR_PARSE_TEST_REPORTS)
        {
            fprintf(stderr, "IR parser: %s\n", message);
        }
        test->failures += 1;
    }
}

static bool ir_parse_test_print(const IrFunction* function, String8 expected, Arena* scratch)
{
    FILE* stream = tmpfile();
    bool equal = stream != 0;
    if (equal)
    {
        equal = ir_print(function, scratch, stream) == IR_OK && fseek(stream, 0, SEEK_SET) == 0;
        for (uint64_t byte = 0; equal && byte < expected.length; byte += 1)
        {
            equal = fgetc(stream) == (unsigned char)expected.pointer[byte];
        }
        equal = equal && fgetc(stream) == EOF && ferror(stream) == 0;
        fclose(stream);
    }
    return equal;
}

#define BUSTER_IR_PARSE_TEST_DIGEST UINT64_C(0x15368917bf264be2)

static void ir_parse_test_fold_bytes(uint64_t* digest, const void* bytes, uint64_t count)
{
    const uint8_t* source = bytes;
    for (uint64_t k = 0; k < count; k += 1)
    {
        *digest = (*digest ^ source[k]) * UINT64_C(0x100000001b3);
    }
}

static void ir_parse_test_fold_result(IrParseTest* test, IrParseResult result, const IrFunction* function)
{
    const uint64_t words[] = { result.error, result.consumed, result.offset, result.line, result.column };
    for (uint32_t k = 0; k < sizeof(words) / sizeof(words[0]); k += 1)
    {
        ir_test_fold(&test->digest, words[k]);
    }
    if (result.error == IR_PARSE_OK)
    {
        const uint64_t n = function->instruction_count;
        const uint64_t counts[] = { n, function->block_count, function->constant_count, function->extra_count, function->slot_count, function->address_count, function->call_count };
        for (uint32_t k = 0; k < sizeof(counts) / sizeof(counts[0]); k += 1)
        {
            ir_test_fold(&test->digest, counts[k]);
        }
        ir_parse_test_fold_bytes(&test->digest, function->op, n);
        ir_parse_test_fold_bytes(&test->digest, function->type, n);
        ir_parse_test_fold_bytes(&test->digest, function->flags, n);
        ir_parse_test_fold_bytes(&test->digest, function->a, n * 4);
        ir_parse_test_fold_bytes(&test->digest, function->b, n * 4);
        ir_parse_test_fold_bytes(&test->digest, function->c, n * 4);
        ir_parse_test_fold_bytes(&test->digest, function->block_offsets, ((uint64_t)function->block_count + 1) * 4);
        ir_parse_test_fold_bytes(&test->digest, function->extra, (uint64_t)function->extra_count * 4);
    }
}

static IrParseResult ir_parse_test_one(IrParseTest* test, String8 source, Arena* output, Arena* scratch, IrFunction* function)
{
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    memset(function, 0x5a, sizeof(*function));
    uint8_t saved[sizeof(*function)];
    memcpy(saved, function, sizeof(saved));
    const IrParseResult result = ir_parse(source, output, scratch, function);
    ir_parse_test_expect(test, scratch->length == scratch_mark, "parse retained scratch");
    ir_parse_test_fold_result(test, result, function);
    ir_parse_test_expect(test, result.consumed <= source.length && result.offset <= source.length, "parser location extends beyond its source slice");
    if (result.error == IR_PARSE_OK)
    {
        ir_parse_test_expect(test, result.check.dominance_checked, "published function lacks complete dominance verification");
        ir_parse_test_expect(test, result.consumed != 0, "successful parse did not consume a function");
        ir_parse_test_expect(test, function->arena_bytes == output->length - output_mark, "parser arena byte count differs from the published allocation");
        const IrCheck checked = ir_verify(function, scratch, 0);
        ir_parse_test_expect(test, checked.error == IR_OK && scratch->length == scratch_mark, "published function fails an independent verifier");
    }
    else
    {
        ir_parse_test_expect(test, output->length == output_mark, "failed parse retained output allocation");
        ir_parse_test_expect(test, memcmp(saved, function, sizeof(saved)) == 0, "failed parse published a partial function");
    }
    return result;
}

static bool ir_parse_test_read(const char* path, Arena* source, String8* text)
{
    FILE* stream = fopen(path, "rb");
    bool ok = stream != 0;
    if (ok)
    {
        ok = fseek(stream, 0, SEEK_END) == 0;
        const long length = ok ? ftell(stream) : -1;
        ok = length >= 0 && fseek(stream, 0, SEEK_SET) == 0;
        char* bytes = ok ? arena_push(source, (uint64_t)length, 1) : 0;
        ok = bytes != 0 && ok;
        if (ok)
        {
            ok = fread(bytes, 1, (size_t)length, stream) == (size_t)length && ferror(stream) == 0;
            if (ok)
            {
                *text = (String8){
                    .pointer = bytes,
                    .length = (uint64_t)length,
                };
            }
        }
        fclose(stream);
    }
    return ok;
}

static void ir_parse_test_fixture(IrParseTest* test, const char* path, Arena* source_arena, Arena* output, Arena* scratch)
{
    const uint64_t source_mark = source_arena->length;
    String8 source = { 0 };
    const bool read = ir_parse_test_read(path, source_arena, &source);
    ir_parse_test_expect(test, read, path);
    uint64_t position = 0;
    bool done = !read;
    while (!done)
    {
        const uint64_t output_mark = output->length;
        const String8 remaining = {
            .pointer = source.pointer + position,
            .length = source.length - position,
        };
        IrFunction function;
        const IrParseResult parsed = ir_parse_test_one(test, remaining, output, scratch, &function);
        done = parsed.error != IR_PARSE_OK || parsed.consumed == 0;
        if (parsed.error == IR_PARSE_OK && parsed.consumed != 0)
        {
            const String8 expected = {
                .pointer = remaining.pointer,
                .length = parsed.consumed,
            };
            ir_parse_test_expect(test, ir_parse_test_print(&function, expected, scratch), "canonical fixture changed bytes after parse and print");
            for (uint32_t instruction = 0; instruction < function.instruction_count; instruction += 1)
            {
                test->operations[function.op[instruction]] = true;
                test->types[function.type[instruction]] = true;
            }
            test->types[function.result_type] = true;
            test->functions += 1;
            position += parsed.consumed;
        }
        else
        {
            ir_parse_test_expect(test, parsed.error == IR_PARSE_END && position == source.length, path);
        }
        arena_pop_to(output, output_mark);
    }
    arena_pop_to(source_arena, source_mark);
}

// The canonical fixtures are runtime data (#2053): tests/ir/catalog.tsv under the fixture root (tests/ under the working
// directory, or test-ir-parse's --fixtures DIR) names them, so that editing a fixture or adding one compiles, links and
// bakes nothing. Its first line is `buster-fixtures v1 ir-parse`; each later line not starting with '#' is a case, three
// tab-separated fields: its ID, unique, of letters, digits, '.', '_' and '-'; its FILE in the catalog's directory, unique,
// of the same characters, ending in .ir; and its NAME. Every .ir file in the directory must be a case's (its C sources
// and subdirectories are other commands' inputs). A missing or malformed catalog, a missing file, a repeated ID or file,
// an .ir file no line names, or no case at all fails the run, and no case runs.
static const char* ir_parse_test_fixture_root = "tests";
static const char ir_parse_test_fixture_header[] = "buster-fixtures v1 ir-parse";
#define BUSTER_IR_PARSE_TEST_CASES 1024

// Whether `text` is 1 to 128 letters, digits, '.', '_' and '-'.
static bool ir_parse_test_word(const char* text)
{
    const size_t length = strlen(text);
    bool word = length != 0 && length <= 128;
    for (size_t i = 0; word && i < length; i += 1)
    {
        const char c = text[i];
        word = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.' || c == '_' || c == '-';
    }
    return word;
}

// Whether every .ir file in `directory` is one of the catalog's `count` files; the first that isn't in `problem`.
static bool ir_parse_test_orphans(const char* directory, char* const* files, uint32_t count, char* problem, size_t problem_size)
{
    DIR* listing = opendir(directory);
    bool clean = listing != 0;
    if (!clean)
    {
        snprintf(problem, problem_size, "can't list %s", directory);
    }
    for (struct dirent* entry = clean ? readdir(listing) : 0; clean && entry != 0; entry = readdir(listing))
    {
        const char* name = entry->d_name;
        const size_t length = strlen(name);
        bool named = length < 3 || strcmp(name + length - 3, ".ir") != 0;
        for (uint32_t i = 0; !named && i < count; i += 1)
        {
            named = strcmp(files[i], name) == 0;
        }
        if (!named)
        {
            snprintf(problem, problem_size, "%s/%s is in no catalog line", directory, name);
            clean = false;
        }
    }
    if (listing != 0)
    {
        closedir(listing);
    }
    return clean;
}

// The catalog's cases, each through ir_parse_test_fixture, once the whole catalog and its files are found sound; else a
// failure naming what isn't, and no case.
static void ir_parse_test_catalog(IrParseTest* test, Arena* source_arena, Arena* output, Arena* scratch)
{
    const uint64_t source_mark = source_arena->length;
    char directory[PATH_MAX];
    char path[PATH_MAX + 256];
    char problem[PATH_MAX + 512];
    problem[0] = 0;
    snprintf(directory, sizeof(directory), "%s/ir", ir_parse_test_fixture_root);
    snprintf(path, sizeof(path), "%s/catalog.tsv", directory);
    String8 catalog = { 0 };
    bool valid = ir_parse_test_read(path, source_arena, &catalog);
    if (!valid)
    {
        snprintf(problem, sizeof(problem), "can't read %s", path);
    }
    else if (catalog.length == 0 || catalog.pointer[catalog.length - 1] != '\n')
    {
        snprintf(problem, sizeof(problem), "%s doesn't end with a newline", path);
        valid = false;
    }
    char* identifiers[BUSTER_IR_PARSE_TEST_CASES];
    char* files[BUSTER_IR_PARSE_TEST_CASES];
    uint32_t count = 0;
    uint32_t line = 0;
    char* cursor = catalog.pointer;
    while (valid && cursor < catalog.pointer + catalog.length)
    {
        char* end = memchr(cursor, '\n', (size_t)(catalog.pointer + catalog.length - cursor));
        *end = 0;
        line += 1;
        const bool nul = memchr(cursor, 0, (size_t)(end - cursor)) != 0;
        if (line == 1)
        {
            valid = !nul && strcmp(cursor, ir_parse_test_fixture_header) == 0;
            if (!valid)
            {
                snprintf(problem, sizeof(problem), "%s's first line isn't '%s'", path, ir_parse_test_fixture_header);
            }
        }
        else if (nul || cursor[0] != '#')
        {
            char* fields[3] = { cursor, 0, 0 };
            uint32_t field_count = 1;
            for (char* tab = nul ? 0 : strchr(cursor, '\t'); tab != 0 && field_count < 4; tab = strchr(tab + 1, '\t'))
            {
                *tab = 0;
                if (field_count < 3)
                {
                    fields[field_count] = tab + 1;
                }
                field_count += 1;
            }
            const size_t file_length = field_count == 3 ? strlen(fields[1]) : 0;
            valid = !nul && field_count == 3 && ir_parse_test_word(fields[0]) && ir_parse_test_word(fields[1]) && file_length > 3 &&
                    strcmp(fields[1] + file_length - 3, ".ir") == 0 && fields[2][0] != 0 && count < BUSTER_IR_PARSE_TEST_CASES;
            if (!valid)
            {
                snprintf(problem, sizeof(problem), "%s line %u isn't ID, FILE.ir and NAME, tab-separated", path, line);
            }
            for (uint32_t i = 0; valid && i < count; i += 1)
            {
                valid = strcmp(identifiers[i], fields[0]) != 0 && strcmp(files[i], fields[1]) != 0;
                if (!valid)
                {
                    snprintf(problem, sizeof(problem), "%s line %u repeats an ID or a file", path, line);
                }
            }
            struct stat status;
            char case_path[PATH_MAX + 256];
            snprintf(case_path, sizeof(case_path), "%s/%s", directory, valid ? fields[1] : "");
            if (valid && (stat(case_path, &status) != 0 || !S_ISREG(status.st_mode)))
            {
                snprintf(problem, sizeof(problem), "can't read %s", case_path);
                valid = false;
            }
            if (valid)
            {
                identifiers[count] = fields[0];
                files[count] = fields[1];
                count += 1;
            }
        }
        cursor = end + 1;
    }
    if (valid && count == 0)
    {
        snprintf(problem, sizeof(problem), "%s names no case", path);
        valid = false;
    }
    valid = valid && ir_parse_test_orphans(directory, files, count, problem, sizeof(problem));
    char description[sizeof(problem) + 32];
    snprintf(description, sizeof(description), "fixtures (#2053): %s", problem);
    ir_parse_test_expect(test, valid, description);
    for (uint32_t i = 0; valid && i < count; i += 1)
    {
        snprintf(path, sizeof(path), "%s/%s", directory, files[i]);
        ir_parse_test_fixture(test, path, source_arena, output, scratch);
    }
    fprintf(stderr, "IR parser: %u canonical fixtures from %s (#2053)\n", valid ? count : 0, directory);
    arena_pop_to(source_arena, source_mark);
}

typedef struct IrParseTestCase IrParseTestCase;
struct IrParseTestCase
{
    const char* text;
    IrParseError error;
};

static void ir_parse_test_invalid(IrParseTest* test, Arena* output, Arena* scratch)
{
    static const IrParseTestCase cases[] = {
        { "", IR_PARSE_END },
        { " \t\r\n; only a comment", IR_PARSE_END },
        { "function", IR_PARSE_SYNTAX },
        { "function () -> void", IR_PARSE_SYNTAX },
        { "function () -> nope {\n}\n", IR_PARSE_SYNTAX },
        { "function () -> void {\nb1:\n%0 = ret.void\n}\n", IR_PARSE_SYNTAX },
        { "function () -> void {\nb0:\n%1 = ret.void\n}\n", IR_PARSE_SYNTAX },
        { "function () -> void {\nb0:\n%0 = mystery.void\n}\n", IR_PARSE_SYNTAX },
        { "function () -> void {\nb0:\n%0 ret.void\n}\n", IR_PARSE_SYNTAX },
        { "function () -> void {\nb0:\n%0 = ret.void extra\n}\n", IR_PARSE_SYNTAX },
        { "function () -> void {\nb0:\n%0 = const.i64 0x\n%1 = ret.void\n}\n", IR_PARSE_SYNTAX },
        { "function () -> void {\nb0:\n%0 = const.i64 0x1g\n%1 = ret.void\n}\n", IR_PARSE_SYNTAX },
        { "function () -> void {\nb0:\n%0 = vconst.v4i32 0x\n%1 = ret.void\n}\n", IR_PARSE_SYNTAX },
        { "function () -> void {\nb0:\n%0 = vconst.v4i32 0x100000000000000000000000000000000\n%1 = ret.void\n}\n", IR_PARSE_LIMIT },
        { "function () -> void {\nb0:\n%0 = vconst.v4i32 0x1g\n%1 = ret.void\n}\n", IR_PARSE_SYNTAX },
        { "function () -> void {\nb0:\n%0 = vconst.v4i32 0x1\n%1 = vcmp.m16 %0, %0 eq\n%2 = ret.void\n}\n", IR_PARSE_VERIFY },
        { "function () -> void {\nb0:\n%0 = const.m8 0x100\n%1 = ret.void\n}\n", IR_PARSE_VERIFY },
        { "function () -> void {\nb0:\n%0 = const.v8i32 0x1\n%1 = ret.void\n}\n", IR_PARSE_VERIFY },
        { "function () -> void {\nb0:\n%0 = vconst.m64 0x1\n%1 = ret.void\n}\n", IR_PARSE_VERIFY },
        { "function () -> void {\nb0:\n%0 = const.m64 0x1\n%1 = bitcast.f64 %0\n%2 = ret.void\n}\n", IR_PARSE_VERIFY },
        { "function () -> void {\nb0:\n%0 = undef.i64\n%1 = add.i64 %0 %0\n%2 = ret.void\n}\n", IR_PARSE_SYNTAX },
        { "function () -> void {\nb0:\n%0 = ret.void\n", IR_PARSE_SYNTAX },
        { "function () -> void {\nb0:\n%0 = call.void @1() fixed=() variadic=2\n%1 = ret.void\n}\n", IR_PARSE_LIMIT },
        { "function () -> void {\nb0:\n%0 = undef.i64\n%1 = icmp.i1 %0, %0 strange\n%2 = ret.void\n}\n", IR_PARSE_SYNTAX },
        // An intrinsic's operation (#141): a name of its own, of its type (mulhu an I64's alone), of operands of its type.
        { "function () -> void {\nb0:\n%0 = undef.i64\n%1 = intrinsic.i64 %0, %0 strange\n%2 = ret.void\n}\n", IR_PARSE_SYNTAX },
        { "function () -> void {\nb0:\n%0 = undef.i64\n%1 = intrinsic.i64 %0, %0\n%2 = ret.void\n}\n", IR_PARSE_SYNTAX },
        { "function () -> void {\nb0:\n%0 = undef.i32\n%1 = intrinsic.i32 %0, %0 mulhu\n%2 = ret.void\n}\n", IR_PARSE_VERIFY },
        { "function () -> void {\nb0:\n%0 = undef.i16\n%1 = intrinsic.i16 %0, %0 pdep\n%2 = ret.void\n}\n", IR_PARSE_VERIFY },
        { "function () -> void {\nb0:\n%0 = undef.i64\n%1 = undef.i32\n%2 = intrinsic.i64 %0, %1 bzhi\n%3 = ret.void\n}\n", IR_PARSE_VERIFY },
        { "function () -> void {\nslot $1 size=8 align=8 type=i64 flags=0x00\nb0:\n%0 = ret.void\n}\n", IR_PARSE_SYNTAX },
        { "function () -> void {\nb0:\n%0 = undef.i64\n}\n", IR_PARSE_VERIFY },
        { "function () -> i64 {\nb0:\n%0 = ret.void\n}\n", IR_PARSE_VERIFY },
        { "function () -> void {\nb0:\n%0 = copy.i64 %0\n%1 = ret.void\n}\n", IR_PARSE_VERIFY },
        { "function () -> void {\nb0:\n%0 = copy.i64 %1\n%1 = undef.i64\n%2 = ret.void\n}\n", IR_PARSE_VERIFY },
        { "function () -> void {\nb0:\n%0 = copy.i64 %99\n%1 = ret.void\n}\n", IR_PARSE_VERIFY },
        { "function () -> void {\nb0:\n%0 = const.i8 0x100\n%1 = ret.void\n}\n", IR_PARSE_VERIFY },
        { "function () -> void {\nb0:\n%0 = br.void b9\n}\n", IR_PARSE_VERIFY },
        { "function () -> void {\nb0:\n%0 = ret.void\n%1 = ret.void\n}\n", IR_PARSE_VERIFY },
        { "function (void) -> void {\nb0:\n%0 = ret.void\n}\n", IR_PARSE_VERIFY },
        { "function () -> void {\nslot $0 size=1 align=3 type=i64 flags=0x00\nb0:\n%0 = ret.void\n}\n", IR_PARSE_VERIFY },
        { "function () -> void {\nb0:\n%0 = symbol_addr.ptr @0\n%1 = ret.void\n}\n", IR_PARSE_VERIFY },
        { "function () -> void {\nb0:\n%0 = call.void @1() fixed=(i64) variadic=0\n%1 = ret.void\n}\n", IR_PARSE_VERIFY },
        { "function () -> void {\nb0:\n%0 = undef.i64\n%1 = cbr.void %0, b1, b1\nb1:\n%2 = ret.void\n}\n", IR_PARSE_VERIFY },
        { "function () -> void {\nb0:\n%0 = undef.i64\n%1 = switch.void %0, default b1, [0x1, b1], [0x1, b1]\nb1:\n%2 = ret.void\n}\n", IR_PARSE_VERIFY },
        { "function () -> void {\nb0:\n%0 = undef.i64\n%1 = br.void b1\nb1:\n%2 = phi.i64 [%0, b0], [%0, b0]\n%3 = ret.void\n}\n", IR_PARSE_VERIFY },
        { "function (i1) -> i64 {\nb0:\n%0 = param.i1 0\n%1 = cbr.void %0, b1, b2\nb1:\n%2 = const.i64 0x7\n%3 = br.void b3\nb2:\n%4 = br.void b3\nb3:\n%5 = ret.void %2\n}\n", IR_PARSE_VERIFY },
        // An atomic access aligned below its size (#1059): x86 makes only a naturally aligned one single-copy atomic.
        { "function (ptr) -> i64 {\nb0:\n%0 = param.ptr 0\n%1 = load.i64 %0 align=1 atomic seq_cst\n%2 = ret.void %1\n}\n", IR_PARSE_VERIFY },
        { "function (ptr, i32) -> void {\nb0:\n%0 = param.ptr 0\n%1 = param.i32 1\n%2 = store.void %0, %1 align=2 atomic release\n%3 = ret.void\n}\n", IR_PARSE_VERIFY },
        // A segment override (#1447) is a load's or a store's alone.
        { "function (ptr) -> void {\nb0:\n%0 = param.ptr 0\n%1 = load.i32 %0 align=4 fs gs\n%2 = ret.void\n}\n", IR_PARSE_SYNTAX },
        { "function (ptr, i16) -> i16 {\nb0:\n%0 = param.ptr 0\n%1 = param.i16 1\n%2 = rmw.i16 %0, %1 align=1 add seq_cst\n%3 = ret.void %2\n}\n", IR_PARSE_VERIFY },
        // A compare-exchange without its desired value, an exchange with one, and a desired value of another type.
        { "function (ptr, i16) -> i16 {\nb0:\n%0 = param.ptr 0\n%1 = param.i16 1\n%2 = rmw.i16 %0, %1 align=2 cas seq_cst\n%3 = ret.void %2\n}\n", IR_PARSE_VERIFY },
        { "function (ptr, i16) -> i16 {\nb0:\n%0 = param.ptr 0\n%1 = param.i16 1\n%2 = rmw.i16 %0, %1, %1 align=2 xchg seq_cst\n%3 = ret.void %2\n}\n", IR_PARSE_VERIFY },
        { "function (ptr, i16) -> i16 {\nb0:\n%0 = param.ptr 0\n%1 = param.i16 1\n%2 = rmw.i16 %0, %1, %0 align=2 cas seq_cst\n%3 = ret.void %2\n}\n", IR_PARSE_VERIFY },
    };
    for (uint32_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i += 1)
    {
        const String8 source = {
            .pointer = (char*)cases[i].text,
            .length = strlen(cases[i].text),
        };
        IrFunction function;
        const IrParseResult parsed = ir_parse_test_one(test, source, output, scratch, &function);
        if (parsed.error != cases[i].error && test->failures < BUSTER_IR_PARSE_TEST_REPORTS)
        {
            fprintf(stderr, "IR parser: case %u expected %u, got %u at %" PRIu64 "\n", i, (unsigned)cases[i].error, (unsigned)parsed.error, (uint64_t)parsed.offset);
        }
        ir_parse_test_expect(test, parsed.error == cases[i].error, "invalid fixture returned the wrong error category");
        if (parsed.error == IR_PARSE_VERIFY)
        {
            ir_parse_test_expect(test, parsed.check.error != IR_OK, "verifier rejection lost its IR diagnostic");
        }
    }
    static const char* const overflows[] = {
        "function () -> void {\nb0:\n%4294967296 = ret.void\n}\n",
        "function () -> void {\nb0:\n%0 = const.i128 0x100000000000000000000000000000000\n%1 = ret.void\n}\n",
        "function () -> void {\nslot $0 size=18446744073709551616 align=1 type=void flags=0x00\nb0:\n%0 = ret.void\n}\n",
        "function () -> void {\nb0:\n%0 = undef.ptr\n%1 = address.ptr %0 offset=-9223372036854775809\n%2 = ret.void\n}\n",
        "function () -> void {\nb0:\n%0 = undef.ptr\n%1 = address.ptr %0 offset=9223372036854775808\n%2 = ret.void\n}\n",
    };
    for (uint32_t i = 0; i < sizeof(overflows) / sizeof(overflows[0]); i += 1)
    {
        const String8 source = {
            .pointer = (char*)overflows[i],
            .length = strlen(overflows[i]),
        };
        IrFunction function;
        const IrParseResult parsed = ir_parse_test_one(test, source, output, scratch, &function);
        ir_parse_test_expect(test, parsed.error == IR_PARSE_LIMIT || parsed.error == IR_PARSE_SYNTAX, "numeric overflow was accepted or escaped lexical validation");
    }
}

static void ir_parse_test_reachable(IrParseTest* test, Arena* output, Arena* scratch)
{
    static const IrParseTestCase cases[] = {
        {
            "function () -> i64 {\nslot $0 size=8 align=8 type=i64 flags=0x00\nb0:\n%0 = stack_addr.ptr $0\n%1 = const.i64 0x1\n%2 = ret.void %1\nb1:\n%3 = load.i64 %0 align=8\n%4 = ret.void %3\n}\n",
            IR_PARSE_OK,
        },
        {
            "function (i1) -> i64 {\nb0:\n%0 = param.i1 0\n%1 = cbr.void %0, b1, b2\nb1:\n%2 = const.i64 0x7\n%3 = br.void b3\nb2:\n%4 = br.void b3\nb3:\n%5 = ret.void %2\n}\n",
            IR_PARSE_VERIFY,
        },
        {
            "function () -> i64 {\nb0:\n%0 = br.void b2\nb1:\n%1 = const.i64 0x7\n%2 = br.void b2\nb2:\n%3 = ret.void %1\n}\n",
            IR_PARSE_VERIFY,
        },
        {
            "function () -> i64 {\nb0:\n%0 = const.i64 0x1\n%1 = br.void b2\nb1:\n%2 = const.i64 0x2\n%3 = br.void b2\nb2:\n%4 = phi.i64 [%0, b0], [%2, b1]\n%5 = ret.void %4\n}\n",
            IR_PARSE_OK,
        },
    };
    for (uint32_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i += 1)
    {
        const uint64_t output_mark = output->length;
        const String8 source = {
            .pointer = (char*)cases[i].text,
            .length = strlen(cases[i].text),
        };
        IrFunction function;
        const IrParseResult parsed = ir_parse_test_one(test, source, output, scratch, &function);
        ir_parse_test_expect(test, parsed.error == cases[i].error, "reachable-use dominance policy returned the wrong outcome");
        if (cases[i].error == IR_PARSE_VERIFY)
        {
            ir_parse_test_expect(test, parsed.check.error == IR_ERROR_DOMINANCE, "reachable non-dominating use lost its dominance diagnostic");
        }
        arena_pop_to(output, output_mark);
    }
}

static void ir_parse_test_guards(IrParseTest* test, Arena* output, Arena* scratch)
{
    static const char text[] = "function () -> i64 {\nb0:\n  %0 = const.i64 0x00000000000000000000000000000011\n  %1 = ret.void %0\n}\n";
    const long page_result = sysconf(_SC_PAGESIZE);
    const uint64_t page = page_result > 0 ? (uint64_t)page_result : 0;
    uint8_t* mapping = page != 0 ? mmap(0, page * 3, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) : MAP_FAILED;
    const bool ready = mapping != MAP_FAILED && mprotect(mapping + page, page, PROT_READ | PROT_WRITE) == 0;
    ir_parse_test_expect(test, ready, "guarded source mapping could not be created");
    for (uint64_t length = 0; ready && length <= sizeof(text) - 1; length += 1)
    {
        char* bytes = (char*)mapping + 2 * page - length;
        memcpy(bytes, text, (size_t)length);
        const String8 source = {
            .pointer = bytes,
            .length = length,
        };
        const uint64_t output_mark = output->length;
        IrFunction function;
        const IrParseResult parsed = ir_parse_test_one(test, source, output, scratch, &function);
        const bool complete = length >= sizeof(text) - 2;
        ir_parse_test_expect(test, complete ? parsed.error == IR_PARSE_OK : length == 0 ? parsed.error == IR_PARSE_END : parsed.error != IR_PARSE_OK && parsed.error != IR_PARSE_END,
                             "guarded truncated function was accepted or complete function rejected");
        arena_pop_to(output, output_mark);
    }
    for (uint64_t padding = 0; ready && padding <= 129; padding += 1)
    {
        const uint64_t length = padding + sizeof(text) - 1;
        char* bytes = (char*)mapping + 2 * page - length;
        memset(bytes, ' ', (size_t)padding);
        memcpy(bytes + padding, text, sizeof(text) - 1);
        const String8 source = {
            .pointer = bytes,
            .length = length,
        };
        const uint64_t output_mark = output->length;
        IrFunction function;
        const IrParseResult parsed = ir_parse_test_one(test, source, output, scratch, &function);
        ir_parse_test_expect(test, parsed.error == IR_PARSE_OK && parsed.consumed == length, "guarded whitespace boundary changed consumed bytes");
        arena_pop_to(output, output_mark);
    }
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, page * 3);
    }
}

static void ir_parse_test_capacity(IrParseTest* test, Arena* output, Arena* scratch)
{
    static const char text[] = "function () -> i64 {\nb0:\n%0 = const.i64 0x11\n%1 = ret.void %0\n}\n";
    const String8 source = {
        .pointer = (char*)text,
        .length = sizeof(text) - 1,
    };
    _Alignas(64) uint8_t bytes[16384];
    uint32_t failures[2] = { 0 };
    uint32_t successes[2] = { 0 };
    for (uint32_t limited = 0; limited < 2; limited += 1)
    {
        for (uint64_t capacity = 13; capacity <= sizeof(bytes); capacity += 31)
        {
            memset(bytes, 0xa5, 13);
            Arena small = {
                .base = bytes,
                .length = 13,
                .capacity = capacity,
            };
            Arena* destination = limited == 0 ? &small : output;
            Arena* temporary = limited == 0 ? scratch : &small;
            const uint64_t output_mark = destination->length;
            const uint64_t scratch_mark = temporary->length;
            IrFunction function;
            memset(&function, 0x5a, sizeof(function));
            uint8_t saved[sizeof(function)];
            memcpy(saved, &function, sizeof(saved));
            const IrParseResult parsed = ir_parse(source, destination, temporary, &function);
            ir_parse_test_expect(test, temporary->length == scratch_mark && destination->length <= destination->capacity, "capacity limit changed scratch or exceeded output capacity");
            if (parsed.error == IR_PARSE_OK)
            {
                successes[limited] += 1;
                ir_parse_test_expect(test, function.arena_bytes == destination->length - output_mark, "limited parse reports the wrong output bytes");
            }
            else
            {
                failures[limited] += 1;
                ir_parse_test_expect(test, parsed.error == IR_PARSE_MEMORY && destination->length == output_mark && memcmp(saved, &function, sizeof(saved)) == 0,
                                     "allocation failure did not roll back output/result or return MEMORY");
            }
            bool preserved = true;
            for (uint32_t byte = 0; byte < 13; byte += 1)
            {
                preserved = preserved && bytes[byte] == 0xa5;
            }
            ir_parse_test_expect(test, preserved, "limited parser overwrote the arena prefix");
            arena_pop_to(destination, output_mark);
        }
    }
    ir_parse_test_expect(test, failures[0] != 0 && failures[1] != 0 && successes[0] != 0 && successes[1] != 0, "capacity sweep did not exercise failures and successes in both arenas");
}

static void ir_parse_test_comments(IrParseTest* test, Arena* output, Arena* scratch)
{
    static const char text[] = "; original fixture\n\tfunction ( ) -> i64 { ; header\n\n b0: ; entry\n%0 = const.i64 0x11 ; value\n%1 = ret.void %0\n} ; end\n";
    static const char canonical[] = "function () -> i64 {\nb0:\n  %0 = const.i64 0x00000000000000000000000000000011\n  %1 = ret.void %0\n}\n";
    const String8 source = {
        .pointer = (char*)text,
        .length = sizeof(text) - 1,
    };
    const String8 expected = {
        .pointer = (char*)canonical,
        .length = sizeof(canonical) - 1,
    };
    const uint64_t output_mark = output->length;
    IrFunction function;
    const IrParseResult parsed = ir_parse_test_one(test, source, output, scratch, &function);
    ir_parse_test_expect(test, parsed.error == IR_PARSE_OK, "comments, blank lines and bounded hexadecimal literal rejected");
    if (parsed.error == IR_PARSE_OK)
    {
        ir_parse_test_expect(test, ir_parse_test_print(&function, expected, scratch), "noncanonical input did not print canonical bytes");
        ir_parse_test_expect(test, function.instruction_count == 2 && function.constant_count == 1 && function.constants[0].low == 17 && function.constants[0].high == 0 && function.a[1] == 0,
                             "independent constant/return column expectations differ");
    }
    arena_pop_to(output, output_mark);
}

// Check every enum, including the invalid opcode and intrinsics absent from an individual fixture.
static void ir_parse_test_spelling_table(IrParseTest* test, const char* const* names, const uint8_t* lengths, uint32_t count)
{
    for (uint32_t index = 0; index < count; index += 1)
    {
        const uint64_t length = strlen(names[index]);
        IrParseState state = {
            .source = { (char*)names[index], length },
            .end = length,
        };
        const uint8_t value = ir_parse_named(&state, names, lengths, count);
        ir_parse_test_expect(test, value == index && state.result.error == IR_PARSE_OK && state.cursor == length, names[index]);
    }
}

static void ir_parse_test_locations(IrParseTest* test, Arena* output, Arena* scratch)
{
    static const char syntax[] = "function () -> void {\nb0:\n  %0 ret.void\n}\n";
    static const char verify[] = "function () -> void {\nb0:\n  %0 = copy.i64 %0\n  %1 = ret.void\n}\n";
    const String8 malformed = {
        .pointer = (char*)syntax,
        .length = sizeof(syntax) - 1,
    };
    const String8 invalid = {
        .pointer = (char*)verify,
        .length = sizeof(verify) - 1,
    };
    IrFunction function;
    const IrParseResult syntax_result = ir_parse_test_one(test, malformed, output, scratch, &function);
    ir_parse_test_expect(test, syntax_result.error == IR_PARSE_SYNTAX && syntax_result.line == 3 && syntax_result.column == 6 &&
                         syntax_result.offset == (uint64_t)(strstr(syntax, "ret.void") - syntax), "syntax diagnostic lost its exact source location");
    const IrParseResult verify_result = ir_parse_test_one(test, invalid, output, scratch, &function);
    ir_parse_test_expect(test, verify_result.error == IR_PARSE_VERIFY && verify_result.line == 3 && verify_result.column == 3 &&
                         verify_result.offset == (uint64_t)(strstr(verify, "%0") - verify) && verify_result.check.instruction == 0,
                         "verification diagnostic lost the offending instruction location");
    // The error points to the beginning of the complete name, including an empty name.
    static const struct
    {
        const char* text;
        const char* rejected;
        uint32_t line;
        uint32_t column;
    } names[] = {
        { "function (i12) -> void {\n", "i12", 1, 11 },
        { "function () ->   {\n", "{", 1, 18 },
        { "function () -> void {\nb0:\n  %0 = .i32\n", ".", 3, 8 },
        { "function () -> void {\nb0:\n  %0 = call_indirec.void\n", "call_indirec", 3, 8 },
        { "function () -> void {\nb0:\n  %0 = call_indirectx.void\n", "call_indirectx", 3, 8 },
        { "function () -> void {\nb0:\n  %0 = fence.void acq_relaxed\n", "acq_relaxed", 3, 19 },
    };
    for (uint32_t i = 0; i < sizeof(names) / sizeof(names[0]); i += 1)
    {
        const String8 source = { (char*)names[i].text, strlen(names[i].text) };
        const IrParseResult result = ir_parse_test_one(test, source, output, scratch, &function);
        const uint64_t offset = (uint64_t)(strstr(names[i].text, names[i].rejected) - names[i].text);
        ir_parse_test_expect(test, result.error == IR_PARSE_SYNTAX && result.consumed == 0 && result.offset == offset &&
                             result.line == names[i].line && result.column == names[i].column, "named token lost its syntax diagnostic");
    }
    const uint64_t output_mark = output->length;
    memset(&function, 0x5a, sizeof(function));
    uint8_t saved[sizeof(function)];
    memcpy(saved, &function, sizeof(saved));
    const IrParseResult aliased = ir_parse(malformed, output, output, &function);
    ir_parse_test_expect(test, aliased.error == IR_PARSE_SYNTAX && output->length == output_mark &&
                         memcmp(saved, &function, sizeof(saved)) == 0, "aliased output/scratch arenas were accepted or changed");
    char embedded[] = "function () -> void {\nb0:\n%0 = ret.void\n}\n";
    embedded[12] = 0;
    const String8 zero_byte = {
        .pointer = embedded,
        .length = sizeof(embedded) - 1,
    };
    const IrParseResult zero_result = ir_parse_test_one(test, zero_byte, output, scratch, &function);
    ir_parse_test_expect(test, zero_result.error == IR_PARSE_SYNTAX, "embedded NUL terminated the bounded source early");
}

static void ir_parse_test_normalization(IrParseTest* test, Arena* output, Arena* scratch)
{
    static const char canonical[] =
        "function () -> i64 {\n"
        "  slot $0 size=8 align=8 type=i64 flags=0x00\n"
        "b0:\n"
        "  %0 = const.i64 0x00000000000000000000000000000011\n"
        "  %1 = const.i64 0x00000000000000000000000000000011\n"
        "  %2 = stack_addr.ptr $0\n"
        "  %3 = address.ptr %2 offset=-7\n"
        "  %4 = address.ptr %2 offset=-7\n"
        "  %5 = call.i64 @1(%0) fixed=(i64) variadic=0\n"
        "  %6 = call.i64 @1(%0) fixed=(i64) variadic=0\n"
        "  %7 = add.i64 %5, %6\n"
        "  %8 = ret.void %7\n"
        "}\n";
    const String8 source = {
        .pointer = (char*)canonical,
        .length = sizeof(canonical) - 1,
    };
    const uint64_t output_mark = output->length;
    const IrSizes sizes = {
        .instruction_count = 9,
        .block_count = 1,
        .extra_count = 4,
        .constant_count = 4,
        .slot_count = 1,
        .address_count = 3,
        .call_count = 2,
        .call_type_count = 3,
    };
    IrFunction original = { 0 };
    const bool created = ir_create(output, &sizes, &original) == IR_OK;
    ir_parse_test_expect(test, created, "shared and unused metadata fixture allocation failed");
    if (created)
    {
        original.result_type = IR_I64;
        original.symbol_count = 2;
        original.block_offsets[1] = 9;
        original.constants[2].low = 17;
        original.slots[0] = (IrSlot){
            .size = 8,
            .alignment = 8,
            .type = IR_I64,
        };
        for (uint32_t address = 0; address < original.address_count; address += 1)
        {
            original.addresses[address].scale = 1;
        }
        original.addresses[1].offset = -7;
        original.call_types[0] = IR_I64;
        original.call_types[1] = IR_I8;
        original.call_types[2] = IR_I16;
        for (uint32_t call = 0; call < original.call_count; call += 1)
        {
            original.calls[call].argument_count = 1;
            original.calls[call].parameter_count = 1;
            original.calls[call].result_type = IR_I64;
        }
        const uint8_t ops[] = {
            IR_CONST,
            IR_CONST,
            IR_STACK_ADDR,
            IR_ADDRESS,
            IR_ADDRESS,
            IR_CALL,
            IR_CALL,
            IR_ADD,
            IR_RET,
        };
        const uint8_t types[] = {
            IR_I64,
            IR_I64,
            IR_PTR,
            IR_PTR,
            IR_PTR,
            IR_I64,
            IR_I64,
            IR_I64,
            IR_VOID,
        };
        const uint32_t a[] = {
            2,
            2,
            0,
            2,
            2,
            1,
            1,
            5,
            7,
        };
        for (uint32_t id = 0; id < original.instruction_count; id += 1)
        {
            original.op[id] = ops[id];
            original.type[id] = types[id];
            original.a[id] = a[id];
            original.b[id] = BUSTER_IR_NONE;
            original.c[id] = BUSTER_IR_NONE;
        }
        original.c[3] = 1;
        original.c[4] = 1;
        original.b[5] = 1;
        original.b[6] = 1;
        original.b[7] = 6;
        ir_parse_test_expect(test, ir_parse_test_print(&original, source, scratch), "independent shared metadata fixture did not print expected bytes");
        IrFunction parsed;
        const IrParseResult result = ir_parse_test_one(test, source, output, scratch, &parsed);
        ir_parse_test_expect(test, result.error == IR_PARSE_OK, "normalized metadata fixture was rejected");
        if (result.error == IR_PARSE_OK)
        {
            ir_parse_test_expect(test, parsed.constant_count == 2 && parsed.address_count == 2 && parsed.call_count == 2 && parsed.extra_count == 2 &&
                                 parsed.call_type_count == 2 && parsed.slot_count == 1, "parser retained unavailable metadata identities or unused entries");
            ir_parse_test_expect(test, ir_parse_test_print(&parsed, source, scratch), "metadata normalization changed canonical printer bytes");
        }
    }
    arena_pop_to(output, output_mark);
}

static int run_ir_parse_tests(int argc, char** argv)
{
    int result = 1;
    if (argc == 2 && strcmp(argv[0], "--fixtures") == 0 && argv[1][0] != 0)
    {
        ir_parse_test_fixture_root = argv[1];
        argc = 0;
    }
    if (argc != 0)
    {
        fprintf(stderr, "usage: test-ir-parse [--fixtures DIRECTORY]\n");
    }
    else
    {
        Arena source = arena_reserve(BUSTER_IR_PARSE_TEST_BYTES);
        Arena output = arena_reserve(BUSTER_IR_PARSE_TEST_BYTES);
        Arena scratch = arena_reserve(BUSTER_IR_PARSE_TEST_BYTES);
        IrParseTest test = { .digest = UINT64_C(0xcbf29ce484222325) };
        const bool ready = source.base != 0 && output.base != 0 && scratch.base != 0;
        ir_parse_test_expect(&test, ready, "test arenas could not be reserved");
        if (ready)
        {
            uint8_t* output_prefix = arena_push(&output, 19, 1);
            uint8_t* scratch_prefix = arena_push(&scratch, 19, 1);
            memset(output_prefix, 0xa5, 19);
            memset(scratch_prefix, 0xa5, 19);
            ir_parse_test_catalog(&test, &source, &output, &scratch);
            for (uint32_t op = IR_CONST; op < IR_OP_COUNT; op += 1)
            {
                ir_parse_test_expect(&test, test.operations[op], "canonical fixture coverage omitted an opcode");
            }
            for (uint32_t type = IR_VOID; type < IR_TYPE_COUNT; type += 1)
            {
                ir_parse_test_expect(&test, test.types[type], "canonical fixture coverage omitted a type");
            }
            ir_parse_test_spelling_table(&test, ir_op_names, ir_op_name_lengths, IR_OP_COUNT);
            ir_parse_test_spelling_table(&test, ir_type_names, ir_type_name_lengths, IR_TYPE_COUNT);
            ir_parse_test_spelling_table(&test, ir_intrinsic_names, ir_intrinsic_name_lengths, BUSTER_IR_INTRINSIC_COUNT);
            ir_parse_test_invalid(&test, &output, &scratch);
            ir_parse_test_reachable(&test, &output, &scratch);
            ir_parse_test_comments(&test, &output, &scratch);
            ir_parse_test_locations(&test, &output, &scratch);
            ir_parse_test_normalization(&test, &output, &scratch);
            ir_parse_test_guards(&test, &output, &scratch);
            ir_parse_test_capacity(&test, &output, &scratch);
            bool preserved = output.length == 19 && scratch.length == 19;
            for (uint32_t byte = 0; byte < 19; byte += 1)
            {
                preserved = preserved && output_prefix[byte] == 0xa5 && scratch_prefix[byte] == 0xa5;
            }
            ir_parse_test_expect(&test, preserved, "parser tests changed the caller's arena prefixes");
            ir_parse_test_expect(&test, test.digest == BUSTER_IR_PARSE_TEST_DIGEST, "the parses' results have their frozen digest");
            if (test.digest != BUSTER_IR_PARSE_TEST_DIGEST)
            {
                fprintf(stderr, "IR parser: digest %#018" PRIx64 "\n", test.digest);
            }
        }
        fprintf(stderr, "IR parser: %" PRIu64 " checks, %" PRIu64 " functions, %" PRIu64 " failures\n", test.checks, test.functions, test.failures);
        result = test.failures != 0;
        arena_release(&scratch);
        arena_release(&output);
        arena_release(&source);
    }
    return result;
}
