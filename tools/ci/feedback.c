// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Conservative, advisory selection for hosted reproductions (#1427). This never selects qualification coverage.
// Administrative text, outside compiler hot paths. Unknown paths expand to all groups; unreadable plans fail.
#include <fnmatch.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUSTER_FEEDBACK_TEXT (4 * 1024 * 1024)
#define BUSTER_FEEDBACK_LINE 4096
#define BUSTER_FEEDBACK_HEADINGS 4096
#define BUSTER_FEEDBACK_ALL 31u

static const char* const feedback_names[] = { "frontend", "checker", "object", "link", "diagnostics" };
static const char* const feedback_always[] = {
    "Test the early driver feedback corpus",
    "Test the C lexer",
    "Test the link representation [8 CPUs]",
    "Test deterministic diagnostic ordering",
};

typedef struct FeedbackRule FeedbackRule;
struct FeedbackRule
{
    const char* path;
    unsigned int groups;
    const char* reason;
};

// An auditable dependency map, intentionally broad. A frontend change affects every downstream consumer.
// Headers, build descriptions, selectors and CI use the unknown-path expansion unless explicitly named here.
// Multiple matching rules are unioned. Documentation exemptions cannot override a policy rule.
static const FeedbackRule feedback_rules[] = {
    { "src/buster/compiler/c_lexer*.c", 31, "lexer and all downstream consumers" },
    { "src/buster/compiler/c_preprocessor*.c", 31, "preprocessor and all downstream consumers" },
    { "src/buster/compiler/c_parser*.c", 31, "parser and all downstream consumers" },
    { "src/buster/compiler/c_check*.c", 30, "checking, emission, execution and diagnostics" },
    { "src/buster/compiler/diagnostic*.c", 31, "frontend and downstream diagnostic consumers" },
    { "src/buster/compiler/object_writer*.c", 12, "objects and their links" },
    { "src/buster/compiler/x86_64_encoder*.c", 12, "encoding and execution" },
    { "src/buster/compiler/link*.c", 12, "objects, linking and execution" },
    { "tests/checker/*", 30, "checker regressions and downstream consumers" },
    { "tests/parser/*", 31, "parser regressions and downstream consumers" },
    { "tests/preprocessor/*", 31, "preprocessor regressions and downstream consumers" },
    { "tests/lexer/*", 31, "lexer regressions and downstream consumers" },
    { "tests/ir/*", 12, "IR, code generation and execution" },
    { "tests/tier1*/*", 12, "code generation and execution" },
    { "docs/ci.md", 31, "CI policy" },
    { "docs/commands/ci.md", 31, "CI policy" },
    { "docs/hosted-toolchain.md", 31, "toolchain policy" },
    { "docs/coding-style.md", 31, "shared policy" },
    { "docs/performance/*", 31, "performance tooling inputs" },
    { "docs/*", 0, "documentation exemption" },
    { "README.md", 0, "documentation exemption" },
};

static char* feedback_read(const char* path)
{
    FILE* file = fopen(path, "rb");
    char* text = NULL;
    if (file != NULL)
    {
        char* buffer = malloc(BUSTER_FEEDBACK_TEXT + 1u);
        if (buffer != NULL)
        {
            const size_t size = fread(buffer, 1, BUSTER_FEEDBACK_TEXT + 1u, file);
            if (!ferror(file) && size <= BUSTER_FEEDBACK_TEXT && memchr(buffer, 0, size) == NULL)
            {
                buffer[size] = 0;
                text = buffer;
            }
            else
            {
                free(buffer);
            }
        }
        if (fclose(file) != 0)
        {
            free(text);
            text = NULL;
        }
    }
    return text;
}

static unsigned int feedback_path(const char* path, const char** reason)
{
    unsigned int groups = 0;
    bool matched = false;
    *reason = "unknown/shared/build/toolchain/policy path: expand all";
    for (size_t i = 0; i < sizeof(feedback_rules) / sizeof(feedback_rules[0]); i += 1)
    {
        if (fnmatch(feedback_rules[i].path, path, 0) == 0)
        {
            groups |= feedback_rules[i].groups;
            if (!matched || feedback_rules[i].groups != 0)
            {
                *reason = feedback_rules[i].reason;
            }
            matched = true;
        }
    }
    const size_t length = strlen(path);
    if (length >= 2 && strcmp(path + length - 2, ".h") == 0)
    {
        groups = BUSTER_FEEDBACK_ALL;
        matched = true;
        *reason = "shared header: expand all";
    }
    if (!matched)
    {
        groups = BUSTER_FEEDBACK_ALL;
    }
    return groups;
}

static bool feedback_paths(char* paths, unsigned int* groups, FILE* report)
{
    bool valid = true;
    size_t count = 0;
    *groups = 0;
    for (char* line = paths; *line != 0 && valid;)
    {
        char* end = strchr(line, '\n');
        if (end != NULL)
        {
            *end = 0;
        }
        const size_t length = strlen(line);
        valid = length != 0 && length < BUSTER_FEEDBACK_LINE && line[0] != '/' && strstr(line, "..") == NULL;
        for (size_t i = 0; i < length && valid; i += 1)
        {
            valid = (unsigned char)line[i] >= 32 && line[i] != 127 && line[i] != '|' && line[i] != '`';
        }
        if (valid)
        {
            const char* reason = NULL;
            *groups |= feedback_path(line, &reason);
            fprintf(report, "- `%s`: %s.\n", line, reason);
            count += 1;
        }
        line = end != NULL ? end + 1 : line + length;
    }
    if (valid && count == 0)
    {
        *groups = BUSTER_FEEDBACK_ALL;
        fputs("- Empty change list: expand all groups.\n", report);
    }
    return valid;
}

static unsigned int feedback_heading(const char* title)
{
    static const char* const terms[][12] = {
        { "lexer", "preprocessor", "parser", "per-file", "directive", "refinement", "streaming", NULL },
        { "checker", "checking", "semantic", "binding", "types", "builtins", "deferred", NULL },
        { "Tier", "SSA", "IR", "encoder", "branch", "backend", "assembly", "calling convention", "object", "emission", NULL },
        { "link", "execut", "foreign", "dynamic", "static", "snapshot", "import", "unwind", "DWARF", NULL },
        { "diagnostic", NULL },
    };
    unsigned int groups = 0;
    for (size_t i = 0; i < sizeof(terms) / sizeof(terms[0]); i += 1)
    {
        for (size_t j = 0; terms[i][j] != NULL; j += 1)
        {
            if (strstr(title, terms[i][j]) != NULL)
            {
                groups |= 1u << i;
            }
        }
    }
    return groups;
}

static bool feedback_inventory(char* text, const char* profile, unsigned int groups, FILE* output, FILE* report)
{
    size_t count = 0;
    size_t selected = 0;
    unsigned int found = 0;
    const char* headings[BUSTER_FEEDBACK_HEADINGS];
    size_t lengths[BUSTER_FEEDBACK_HEADINGS];
    bool keep = false;
    bool valid = true;
    for (char* line = text; *line != 0 && valid;)
    {
        char* end = strchr(line, '\n');
        const size_t size = end != NULL ? (size_t)(end - line) : strlen(line);
        if (size >= 3 && memcmp(line, "== ", 3) == 0)
        {
            char title[BUSTER_FEEDBACK_LINE];
            valid = size - 3 < sizeof(title) && count < BUSTER_FEEDBACK_HEADINGS;
            for (size_t i = 0; i < count && valid; i += 1)
            {
                valid = lengths[i] != size || memcmp(headings[i], line, size) != 0;
            }
            if (valid)
            {
                headings[count] = line;
                lengths[count] = size;
                memcpy(title, line + 3, size - 3);
                title[size - 3] = 0;
                unsigned int mandatory = 0;
                for (size_t i = 0; i < sizeof(feedback_always) / sizeof(feedback_always[0]); i += 1)
                {
                    if (strcmp(title, feedback_always[i]) == 0)
                    {
                        mandatory |= 1u << i;
                    }
                }
                valid = (found & mandatory) == 0;
                found |= mandatory;
                const unsigned int owner = feedback_heading(title);
                const bool smoke = strcmp(profile, "smoke") == 0;
                keep = mandatory != 0 || (!smoke && (owner == 0 || (owner & groups) != 0));
                fprintf(report, "| %s | %s | %s |\n", title, keep ? "run" : "skip",
                    mandatory != 0 ? "always-run corpus/regression" : owner == 0 ? "shared or unclassified heading: always run" :
                    keep ? "selected dependency group" : smoke ? "outside smoke; full qualification retained" : "no selected dependency group; full qualification retained");
                selected += keep;
                count += 1;
            }
        }
        else if (count == 0 && size != 0 && line[0] != '#')
        {
            valid = false;
        }
        if (valid && keep)
        {
            valid = fwrite(line, 1, size, output) == size && fputc('\n', output) != EOF;
        }
        line += size + (end != NULL);
    }
    valid = valid && count != 0 && selected != 0 && found == (1u << (sizeof(feedback_always) / sizeof(feedback_always[0]))) - 1;
    fprintf(report, "\nSelected %zu of %zu inventory headings. This advisory result cannot qualify a merge.\n", selected, count);
    return valid && !ferror(report) && !ferror(output);
}

int main(int argc, char** argv)
{
    const char* profile = NULL;
    const char* files = NULL;
    const char* inventory = NULL;
    const char* report_path = NULL;
    bool valid = argc >= 7 && argc % 2 == 1;
    for (int i = 1; i + 1 < argc && valid; i += 2)
    {
        const char** option = NULL;
        if (strcmp(argv[i], "--profile") == 0)
        {
            option = &profile;
        }
        else if (strcmp(argv[i], "--files") == 0)
        {
            option = &files;
        }
        else if (strcmp(argv[i], "--inventory") == 0)
        {
            option = &inventory;
        }
        else if (strcmp(argv[i], "--report") == 0)
        {
            option = &report_path;
        }
        valid = option != NULL && *option == NULL;
        if (valid)
        {
            *option = argv[i + 1];
        }
    }
    valid = valid && profile != NULL && inventory != NULL && report_path != NULL;
    unsigned int groups = 0;
    bool known = false;
    if (valid)
    {
        for (size_t i = 0; i < sizeof(feedback_names) / sizeof(feedback_names[0]); i += 1)
        {
            if (strcmp(profile, feedback_names[i]) == 0)
            {
                groups = 1u << i;
                known = true;
            }
        }
        if (strcmp(profile, "full") == 0 || strcmp(profile, "smoke") == 0 || strcmp(profile, "affected") == 0)
        {
            groups = BUSTER_FEEDBACK_ALL;
            known = true;
        }
    }
    valid = valid && known;
    char* text = valid ? feedback_read(inventory) : NULL;
    FILE* report = valid ? fopen(report_path, "w") : NULL;
    valid = valid && text != NULL && report != NULL;
    if (valid)
    {
        fprintf(report, "## Hosted feedback plan (%s; advisory)\n\n", profile);
        if (strcmp(profile, "affected") == 0)
        {
            char* paths = files != NULL ? feedback_read(files) : NULL;
            valid = paths != NULL && feedback_paths(paths, &groups, report);
            free(paths);
        }
        fputs("\n| Group | Decision |\n| --- | --- |\n", report);
        for (size_t i = 0; i < sizeof(feedback_names) / sizeof(feedback_names[0]); i += 1)
        {
            fprintf(report, "| %s | %s |\n", feedback_names[i], groups & (1u << i) ? "selected" : "no matching changed dependency; always-run checks remain");
        }
        fputs("\n| Inventory heading | Decision | Reason |\n| --- | --- | --- |\n", report);
        // Validate completely before publishing any executable output. A failed plan cannot become a green no-op.
        FILE* staged = valid ? tmpfile() : NULL;
        valid = staged != NULL && feedback_inventory(text, profile, groups, staged, report);
        if (valid)
        {
            rewind(staged);
            char buffer[8192];
            size_t size = 0;
            while ((size = fread(buffer, 1, sizeof(buffer), staged)) != 0 && valid)
            {
                valid = fwrite(buffer, 1, size, stdout) == size;
            }
            valid = valid && !ferror(staged) && fflush(stdout) == 0;
        }
        if (staged != NULL && fclose(staged) != 0)
        {
            valid = false;
        }
    }
    if (report != NULL && fclose(report) != 0)
    {
        valid = false;
    }
    free(text);
    if (!valid)
    {
        fputs("feedback: invalid profile, unreadable/invalid paths or incomplete inventory; no runnable plan published\n", stderr);
    }
    return valid ? 0 : 1;
}
