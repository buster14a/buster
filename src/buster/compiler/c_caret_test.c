// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// test-carets (#2376): clang's caret display (pp_diagnostic_snippet), compared whole with clang's own. Each case of the
// catalog, tests/carets/catalog.tsv (runtime data, #2053), is a file and the options to compile it with; this
// executable and clang run that command line alike, carets on as both have them by default, and their exit statuses
// and standard errors must be the same, byte for byte, apart from scratch-space allocation line numbers (see below).
// A case flagged `prefix` is a command line both refuse for its
// options, where ours prints its usage text after the error clang prints alone: clang's text must start ours.
// Included by main.c in the driver's unit, which runs the command.
//
// The catalog's first line is `buster-fixtures v1 carets`. Each later line not starting with '#' is a case, five
// tab-separated fields: ID, unique, of letters, digits, '.', '_' and '-'; FILE, in the catalog's directory, of the same
// characters; FLAGS, `-` or `prefix`; OPTIONS, the arguments before the file, separated by single spaces; and NAME, what
// the case checks. Every .c file in the directory must be some case's (a header is an input of the cases that include
// it). A missing or malformed catalog, a missing file, a repeated ID or an empty catalog fails the run.

#include <dirent.h>

#define BUSTER_CARET_TEST_ARGUMENTS 32
#define BUSTER_CARET_TEST_SHOWN 8

typedef struct CaretTestRun CaretTestRun;
struct CaretTestRun
{
    char* errors;
    uint64_t length;
    int status;
};

// `path`'s bytes, NUL-terminated (malloc's; the caller frees them), or null.
static char* caret_test_read(const char* path, uint64_t* length)
{
    char* result = NULL;
    FILE* file = fopen(path, "rb");
    bool valid = file != NULL && fseek(file, 0, SEEK_END) == 0;
    const long size = valid ? ftell(file) : -1;
    valid = valid && size >= 0 && fseek(file, 0, SEEK_SET) == 0;
    result = valid ? malloc((uint64_t)size + 1) : NULL;
    valid = result != NULL && fread(result, 1, (uint64_t)size, file) == (uint64_t)size;
    if (valid)
    {
        result[size] = 0;
        *length = (uint64_t)size;
    }
    else
    {
        free(result);
        result = NULL;
    }
    if (file != NULL)
    {
        fclose(file);
    }
    return result;
}

// A field's characters: letters, digits, '.', '_' and '-', at least one.
static bool caret_test_word(const char* text)
{
    bool result = text[0] != 0;
    for (const char* at = text; result && *at != 0; at += 1)
    {
        const char c = *at;
        result = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
    }
    return result;
}

// `arguments` (argument 0 the program) with standard errors captured, as the tests' oracle runs go (oracle_serve_child).
static CaretTestRun caret_test_run(char* const* arguments)
{
    CaretTestRun result = { 0 };
    char* output = NULL;
    uint64_t output_length = 0;
    result.status = oracle_serve_child("test-carets", arguments, NULL, 2, &output, &output_length, &result.errors, &result.length);
    free(output);
    return result;
}

// As pp_test_normalize_scratch does for diagnostic headings, normalize generated-buffer line numbers: their value
// reflects allocation order, not source. Require the whole generated-note heading so an unnumbered excerpt that
// merely begins with this prefix stays source. A following source gutter changes only when it names that same line.
// Keep its width, every real source gutter, diagnostic column, excerpt byte, caret, range and fix-it unchanged.
static void caret_test_normalize_scratch(CaretTestRun* run)
{
    static const char prefix[] = "<scratch space>:";
    for (char* at = run->errors; at != NULL && (at = strstr(at, prefix)) != NULL;)
    {
        char* number = at + sizeof(prefix) - 1;
        char* end = number;
        while (*end >= '0' && *end <= '9')
        {
            end += 1;
        }
        char* column = *end == ':' ? end + 1 : end;
        char* column_end = column;
        while (*column_end >= '0' && *column_end <= '9')
        {
            column_end += 1;
        }
        static const char suffix[] = ": note: expanded from here\n";
        const bool heading = (at == run->errors || at[-1] == '\n') && end != number && *end == ':' && column_end != column &&
                             strncmp(column_end, suffix, sizeof(suffix) - 1) == 0;
        if (heading)
        {
            char* next = strchr(end, '\n');
            char* gutter = next != NULL ? next + 1 : NULL;
            while (gutter != NULL && *gutter == ' ')
            {
                gutter += 1;
            }
            char* last = gutter;
            while (last != NULL && *last >= '0' && *last <= '9')
            {
                last += 1;
            }
            const bool matching = last != NULL && last - gutter == end - number && strncmp(last, " | ", 3) == 0 &&
                                  memcmp(gutter, number, (uint64_t)(end - number)) == 0;
            if (matching)
            {
                memset(gutter, ' ', (uint64_t)(last - gutter));
                last[-1] = '1';
            }
            *number = '1';
            memmove(number + 1, end, strlen(end) + 1);
            run->length -= (uint64_t)(end - number - 1);
        }
        at = *number != 0 ? number + 1 : number;
    }
}

// Normalization may remove allocation-dependent line numbers only, including without gutters. Source-like prefixes,
// real-file gutters, columns, replacement lines and an incomplete trailing prefix retain their exact bytes.
static bool caret_test_normalization(void)
{
    char actual[] =
        "<scratch space>:123:abc\n"
        "<scratch space>:123:4: error: source text\n"
        "    7 | <scratch space>:123:4: note: expanded from here\n"
        "<scratch space>:123:4: note: expanded from here\n"
        "  123 | token\n"
        "      |    ^~~\n"
        "      |    replacement\n"
        "<scratch space>:45:2: note: expanded from here\n"
        "  token\n"
        "  ^\n"
        "<scratch space>:45:2: note: expanded from here\n"
        "   46 | unrelated\n"
        "<scratch space>:";
    static const char expected[] =
        "<scratch space>:123:abc\n"
        "<scratch space>:123:4: error: source text\n"
        "    7 | <scratch space>:123:4: note: expanded from here\n"
        "<scratch space>:1:4: note: expanded from here\n"
        "    1 | token\n"
        "      |    ^~~\n"
        "      |    replacement\n"
        "<scratch space>:1:2: note: expanded from here\n"
        "  token\n"
        "  ^\n"
        "<scratch space>:1:2: note: expanded from here\n"
        "   46 | unrelated\n"
        "<scratch space>:";
    CaretTestRun run = { .errors = actual, .length = sizeof(actual) - 1 };
    caret_test_normalize_scratch(&run);
    const bool exact = run.length == sizeof(expected) - 1 && memcmp(actual, expected, sizeof(expected)) == 0;
    if (!exact)
    {
        printf("test-carets: scratch line normalization changed source, columns or marks\n");
    }
    return exact;
}

// Prints a differing case's command line and both texts.
static void caret_test_report(const char* id, const char* name, char* const* arguments, const CaretTestRun* ours, const CaretTestRun* theirs)
{
    printf("test-carets: case %s (%s) differs:", id, name);
    for (uint32_t i = 1; arguments[i] != NULL; i += 1)
    {
        printf(" %s", arguments[i]);
    }
    printf("\nours, status %d:\n%sclang's, status %d:\n%s\n", ours->status, ours->errors != NULL ? ours->errors : "<none>\n", theirs->status,
           theirs->errors != NULL ? theirs->errors : "<none>\n");
}

// One case: whether ours and clang's runs agree. `options` is the OPTIONS field, which the arguments point into.
static bool caret_test_case(const char* clang, const char* directory, const char* id, const char* file, bool prefix, char* options, const char* name, uint32_t* shown)
{
    char path[PATH_MAX];
    const int written = snprintf(path, sizeof(path), "%s/%s", directory, file);
    char* arguments[BUSTER_CARET_TEST_ARGUMENTS + 3] = { 0 };
    uint32_t count = 1;
    bool valid = written > 0 && (uint64_t)written < sizeof(path) && access(path, R_OK) == 0;
    for (char* at = options; valid && strcmp(options, "-") != 0 && at != NULL;)
    {
        char* space = strchr(at, ' ');
        if (space != NULL)
        {
            *space = 0;
        }
        valid = count <= BUSTER_CARET_TEST_ARGUMENTS && at[0] != 0;
        arguments[count] = at;
        count += valid ? 1 : 0;
        at = space != NULL ? space + 1 : NULL;
    }
    arguments[count] = path;
    bool same = false;
    if (valid)
    {
        arguments[0] = (char*)clang;
        CaretTestRun theirs = caret_test_run(arguments);
        arguments[0] = "/proc/self/exe";
        CaretTestRun ours = caret_test_run(arguments);
        caret_test_normalize_scratch(&theirs);
        caret_test_normalize_scratch(&ours);
        const bool ran = theirs.status >= 0 && ours.status >= 0 && theirs.errors != NULL && ours.errors != NULL;
        const bool texts = ran && (prefix ? ours.length >= theirs.length && memcmp(ours.errors, theirs.errors, theirs.length) == 0 :
                                            ours.length == theirs.length && memcmp(ours.errors, theirs.errors, ours.length) == 0);
        same = texts && ours.status == theirs.status && (!prefix || ours.status != 0);
        if (!same && *shown < BUSTER_CARET_TEST_SHOWN)
        {
            caret_test_report(id, name, arguments, &ours, &theirs);
            *shown += 1;
        }
        free(theirs.errors);
        free(ours.errors);
    }
    else
    {
        printf("test-carets: case %s: its file %s is unreadable or its options are malformed\n", id, path);
    }
    return same;
}

// Every .c file in `directory` is a case's FILE, which `files` lists ('\n'-separated, each between newlines).
static bool caret_test_listed(const char* directory, const char* files)
{
    DIR* listing = opendir(directory);
    bool result = listing != NULL;
    for (struct dirent* entry = result ? readdir(listing) : NULL; entry != NULL; entry = readdir(listing))
    {
        const uint64_t length = strlen(entry->d_name);
        const bool source = length > 2 && strcmp(entry->d_name + length - 2, ".c") == 0;
        char line[NAME_MAX + 3];
        snprintf(line, sizeof(line), "\n%s\n", entry->d_name);
        const bool listed = !source || strstr(files, line) != NULL;
        if (!listed)
        {
            printf("test-carets: %s/%s is no case's file\n", directory, entry->d_name);
        }
        result &= listed;
    }
    if (listing != NULL)
    {
        closedir(listing);
    }
    return result;
}

static int run_caret_tests(int argc, char** argv)
{
    const char* clang = "clang";
    const char* directory = "tests/carets";
    bool valid = caret_test_normalization();
    for (int i = 0; valid && i < argc; i += 1)
    {
        const bool value = i + 1 < argc;
        if (strcmp(argv[i], "--clang") == 0 && value)
        {
            clang = argv[++i];
        }
        else if (strcmp(argv[i], "--fixtures") == 0 && value)
        {
            directory = argv[++i];
        }
        else
        {
            fprintf(stderr, "usage: compiler test-carets [--clang PATH] [--fixtures DIRECTORY]\n");
            valid = false;
        }
    }
    char path[PATH_MAX];
    const int written = valid ? snprintf(path, sizeof(path), "%s/catalog.tsv", directory) : -1;
    uint64_t length = 0;
    char* catalog = written > 0 && (uint64_t)written < sizeof(path) ? caret_test_read(path, &length) : NULL;
    static const char header[] = "buster-fixtures v1 carets\n";
    bool read = catalog != NULL && length >= sizeof(header) - 1 && memcmp(catalog, header, sizeof(header) - 1) == 0 && catalog[length - 1] == '\n';
    if (valid && !read)
    {
        printf("test-carets: %s is missing, or lacks its header or its last line's end\n", path);
    }
    // The IDs and files seen, each between newlines, for the repeats' and the directory's checks.
    char* ids = read ? malloc(length + 2) : NULL;
    char* files = read ? malloc(length + 2) : NULL;
    read = read && ids != NULL && files != NULL;
    uint64_t ids_length = 0;
    uint64_t files_length = 0;
    uint32_t cases = 0;
    uint32_t differ = 0;
    uint32_t shown = 0;
    if (read)
    {
        ids[ids_length++] = '\n';
        files[files_length++] = '\n';
        ids[ids_length] = 0;
        files[files_length] = 0;
    }
    for (char* line = read ? catalog + sizeof(header) - 1 : NULL; read && line < catalog + length;)
    {
        char* end = strchr(line, '\n');
        *end = 0;
        if (line[0] != '#')
        {
            char* fields[5] = { line };
            uint32_t count = 1;
            for (char* at = strchr(line, '\t'); at != NULL && count < 5; at = strchr(at + 1, '\t'))
            {
                *at = 0;
                fields[count++] = at + 1;
            }
            char needle[256];
            const int needed = count == 5 ? snprintf(needle, sizeof(needle), "\n%s\n", fields[0]) : -1;
            const bool flags = count == 5 && (strcmp(fields[2], "-") == 0 || strcmp(fields[2], "prefix") == 0);
            read = flags && strchr(fields[4], '\t') == NULL && fields[4][0] != 0 && fields[3][0] != 0 && caret_test_word(fields[0]) && caret_test_word(fields[1]) &&
                   needed > 0 && (uint64_t)needed < sizeof(needle) && strstr(ids, needle) == NULL;
            if (read)
            {
                ids_length += (uint64_t)snprintf(ids + ids_length, length + 2 - ids_length, "%s\n", fields[0]);
                files_length += (uint64_t)snprintf(files + files_length, length + 2 - files_length, "%s\n", fields[1]);
                cases += 1;
                differ += caret_test_case(clang, directory, fields[0], fields[1], strcmp(fields[2], "prefix") == 0, fields[3], fields[4], &shown) ? 0 : 1;
            }
            else
            {
                printf("test-carets: a malformed or repeated case in %s: %s\n", path, fields[0]);
            }
        }
        line = end + 1;
    }
    read = read && cases != 0 && caret_test_listed(directory, files);
    if (valid)
    {
        printf("test-carets: %u cases, %u differ from clang's%s\n", cases, differ, read ? "" : "; the catalog is incomplete or malformed");
    }
    free(ids);
    free(files);
    free(catalog);
    return valid && read && differ == 0 ? 0 : 1;
}
