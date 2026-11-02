#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct Buffer
{
    char *data;
    size_t length;
    size_t capacity;
} Buffer;

typedef struct Topic
{
    const char *name;
    const char *commands;
    const char *source;
    Buffer body;
} Topic;

#define TOPIC(n, c, s) {n, c, s, {0}}
static Topic topics[] =
{
    TOPIC("assembler", "test-assembler, benchmark-assembler", "assembler.c"),
    TOPIC("bind", "test-bind, benchmark-bind", "bind.c"),
    TOPIC("bindings", "test-bindings, test-parser-bindings", "c_bindings.c"),
    TOPIC("branch-sizing", "test-branch-sizing, benchmark-branch-sizing", "branch_sizing.c"),
    TOPIC("build", "./build.sh, bake-tables, test-baked-tables", "baked.c"),
    TOPIC("builtins", "test-builtins, test-check-builtins, test-builtin-lowering", "c_builtin.c"),
    TOPIC("call-graph", "check-call-graph, test-call-graph", "call_graph.c"),
    TOPIC("calling-convention", "test-calling-convention, benchmark-calling-convention", "calling_convention.c"),
    TOPIC("checker", "check, test-checker, test-checker-driver", "c_checker.c"),
    TOPIC("checker-asm", "test-checker-asm", "c_check_program_asm.c"),
    TOPIC("checker-corpus", "test-checker-corpus", "c_check_corpus.c"),
    TOPIC("check-debug", "test-check-debug, benchmark-check-debug", "c_check_debug.c"),
    TOPIC("ci", "correctness.tests / CI selection", "tools/ci/correctness.tests"),
    TOPIC("compile-time", "measure-compile, --time-stages", "c_check_clock.c"),
    TOPIC("c-testsuite", "test-c-testsuite", "c_testsuite.c"),
    TOPIC("dataflow", "test-dataflow, benchmark-dataflow", "dataflow.c"),
    TOPIC("debug", "test-debug, benchmark-debug", "debug.c"),
    TOPIC("deferred", "test-checker-deferred, benchmark-checker-deferred", "c_check_deferred_test.c"),
    TOPIC("determinism", "test-diagnostics, benchmark-diagnostics", "diagnostic.c"),
    TOPIC("differential", "test-differential, test-differential-harness", "differential.c"),
    TOPIC("dominators", "test-dominators, benchmark-dominators", "dominators.c"),
    TOPIC("driver", "-E, parse, -fsyntax-only, -c", "main.c"),
    TOPIC("executable", "test-executable, test-dynamic, test-static", "executable.c"),
    TOPIC("files", "test-files, benchmark-files", "c_files.c"),
    TOPIC("floors", "benchmark-floors", "link_benchmark.c"),
    TOPIC("foreign", "test-foreign, benchmark-foreign", "foreign.c"),
    TOPIC("foreign-corpus", "test-foreign-corpus", "foreign_corpus_test.c"),
    TOPIC("foreign-debug", "link-foreign (DWARF)", "foreign_debug.c"),
    TOPIC("foreign-driver", "link-foreign", "foreign_driver.c"),
    TOPIC("foreign-gc", "benchmark-foreign-gc", "foreign_gc.c"),
    TOPIC("foreign-strings", "benchmark-foreign-strings", "foreign_strings.c"),
    TOPIC("foreign-unwind", "benchmark-foreign-unwind", "foreign_unwind.c"),
    TOPIC("foreign-unwind-dedup", "benchmark-foreign-unwind-dedup", "foreign_unwind_dedup.c"),
    TOPIC("function-lanes", "test-check-lanes, benchmark-check-lanes", "c_check_program_emit.c"),
    TOPIC("gnu", "test-gnu, benchmark-gnu", "c_gnu.c"),
    TOPIC("import", "test-import, benchmark-import", "import.c"),
    TOPIC("ir", "test-ir, test-ir-lowering, benchmark-ir", "ir.c"),
    TOPIC("ir-optimization", "test-ir-optimize, test-ir-gvn, test-ir-licm", "ir_optimize.c"),
    TOPIC("ir-tools", "test-ir-parse, reduce-ir, interpret-ir", "ir_parse.c"),
    TOPIC("lanes", "test-lanes, benchmark-lanes", "lane.c"),
    TOPIC("lexer", "test-lexer, benchmark-lexer", "c_lexer.c"),
    TOPIC("link", "test-link", "link.c"),
    TOPIC("link-benchmark", "benchmark-link", "link_benchmark.c"),
    TOPIC("lowering", "test-lowering, benchmark-lowering-source", "c_lowering.c"),
    TOPIC("object-writer", "test-object-writer, benchmark-object-writer", "object_writer.c"),
    TOPIC("oracle-cache", "test-oracle-cache, BUSTER_ORACLE_CACHE", "oracle_cache.c"),
    TOPIC("parallel", "parallel, test-parallel", "parallel.c"),
    TOPIC("parser", "test-parser, benchmark-parser", "c_parser.c"),
    TOPIC("preprocessor", "test-preprocessor, benchmark-preprocessor", "c_preprocessor.c"),
    TOPIC("preprocessor-fuzz", "test-preprocessor-fuzz, benchmark-preprocessor-fuzz", "preprocessor_fuzz.c"),
    TOPIC("profiling", "ab, time-runs, test-profile", "profile.c"),
    TOPIC("refinement", "test-refinement, benchmark-refinement", "c_intern.c"),
    TOPIC("snapshot", "test-snapshot, benchmark-snapshot", "snapshot.c"),
    TOPIC("ssa", "test-ssa, test-ssa-ir, test-ssa-source", "ssa.c"),
    TOPIC("streaming", "test-streaming, benchmark-streaming", "streaming.c"),
    TOPIC("tier0", "test-tier0, benchmark-backend, benchmark-backend-source", "tier0.c"),
    TOPIC("tier1", "test-tier1-select, test-tier1-pipeline, test-tier1-masks", "tier1_select.c"),
    TOPIC("tier1-allocation", "test-tier1-allocation, benchmark-tier1-allocation", "tier1_allocate.c"),
    TOPIC("tier1-certificate", "allocation certificate checks", "tier1_certificate.c"),
    TOPIC("tier1-cfg", "test-tier1-cfg, test-tier1-cfg-source", "tier1_cfg.c"),
    TOPIC("tier1-frame", "test-tier1-frame, test-tier1-frame-execution", "tier1_frame.c"),
    TOPIC("tier1-schedule", "test-tier1-schedule, benchmark-tier1-schedule", "tier1_schedule.c"),
    TOPIC("translation-units", "test-check-units", "c_checker_driver.c"),
    TOPIC("types", "test-types, benchmark-types", "c_types.c"),
    TOPIC("unwind", "test-unwind, benchmark-unwind", "unwind_tier0.c"),
    TOPIC("x86-disassembler", "disassemble, test-x86-disassembler", "x86_64_disassembler.c"),
    TOPIC("x86-encoder", "test-x86-encoder, benchmark-x86-encoder", "x86_64_encoder.c"),
    TOPIC("x86-intrinsics", "test-x86-intrinsics", "x86_intrinsics_test.c"),
    TOPIC("x86-semantics", "test-x86-semantics, benchmark-x86-semantics", "x86_semantics.c"),
    TOPIC("x86-target", "test-x86-target, test-x86-target-selection", "x86_target.c"),
    TOPIC("x86-zen5", "test-x86-zen5, measure-x86-zen5, measure-mask-spills", "x86_zen5.c"),
};

static const char *module_topics[] =
{
    "tier1", "ir-optimization", "ir-tools", "executable", "gnu", "differential", "dominators",
    "tier1-allocation", "tier1-certificate", "tier1-cfg", "tier1-frame", "tier1-schedule",
    "builtins", "builtins", "x86-zen5", "x86-target", "assembler", "checker-asm", "c-testsuite",
    "driver", "compile-time", "build", "lexer", "lexer", "lexer", "preprocessor-fuzz",
    "x86-encoder", "link", "lanes", "dataflow", "ssa", "link", "object-writer", "import", "bind",
    "executable", "debug", "check-debug", "foreign-debug", "unwind", "streaming", "snapshot",
    "link-benchmark", "foreign", "foreign", "foreign-strings", "foreign-unwind", "foreign-gc",
    "foreign-unwind-dedup", "foreign-driver", "branch-sizing", "x86-semantics", "x86-intrinsics",
    "types", "checker", "calling-convention", "tier0", "ir", "call-graph", "refinement",
    "preprocessor", "preprocessor", "files", "parser", "profiling", "parallel", "oracle-cache", "ROOT"
};

typedef struct SectionMap
{
    const char *title;
    const char *topic;
} SectionMap;

static SectionMap section_map[] =
{
    {"Call graph check", "call-graph"},
    {"Lexer tests", "lexer"},
    {"Lexer benchmarks", "lexer"},
    {"Profiling tools", "profiling"},
    {"The parallel runner", "parallel"},
    {"The oracle cache", "oracle-cache"},
    {"x86-64 disassembler", "x86-disassembler"},
    {"x86-64 encoder tests", "x86-encoder"},
    {"x86-64 encoder benchmark", "x86-encoder"},
    {"Branch sizing tests", "branch-sizing"},
    {"Branch sizing benchmark", "branch-sizing"},
    {"x86 semantics tests", "x86-semantics"},
    {"x86 semantics benchmark", "x86-semantics"},
    {"Lane tests", "lanes"},
    {"Deterministic output tests", "determinism"},
    {"Lane benchmark", "lanes"},
    {"Link representation tests", "link"},
    {"Object writer tests", "object-writer"},
    {"Object writer benchmark", "object-writer"},
    {"Import tests", "import"},
    {"Import benchmark", "import"},
    {"Bind tests", "bind"},
    {"Bind benchmark", "bind"},
    {"Direct executable tests", "executable"},
    {"Dynamic PIE and shared library tests", "executable"},
    {"Static executable tests", "executable"},
    {"Direct executable benchmark", "executable"},
    {"Snapshot tests", "snapshot"},
    {"Snapshot benchmark", "snapshot"},
    {"Link benchmark", "link-benchmark"},
    {"Floors benchmark", "floors"},
    {"Foreign tests", "foreign"},
    {"Foreign benchmark", "foreign"},
    {"Foreign corpus test", "foreign-corpus"},
    {"Dataflow tests", "dataflow"},
    {"Dataflow benchmark", "dataflow"},
    {"Preprocessor harness tests", "preprocessor-fuzz"},
    {"Preprocessor harness benchmark", "preprocessor-fuzz"},
    {"Refinement tests", "refinement"},
    {"Refinement benchmark", "refinement"},
    {"Preprocessor tests", "preprocessor"},
    {"Preprocessor benchmark", "preprocessor"},
    {"Per-file jobs tests", "files"},
    {"Per-file jobs benchmark", "files"},
    {"Parser tests", "parser"},
    {"Parser benchmark", "parser"},
    {"Scoped binding kernels", "bindings"},
    {"Deferred function bodies", "deferred"},
    {"Function lanes", "function-lanes"},
    {"Translation units", "translation-units"},
    {"Selective lowering", "lowering"},
    {"Type tests", "types"},
    {"Type benchmark", "types"},
    {"Checker corpus and semantic mutations", "checker-corpus"},
    {"Calling convention tests", "calling-convention"},
    {"Calling convention benchmark", "calling-convention"},
    {"Tier 0 tests", "tier0"},
    {"Backend benchmark", "tier0"},
    {"Streaming tests", "streaming"},
    {"Streaming benchmark", "streaming"},
    {"IR tests and benchmarks", "ir"},
};

typedef struct Section
{
    size_t start;
    size_t end;
    char *title;
    int topic;
    char anchor[256];
} Section;

static Section sections[128];
static size_t section_count;
static size_t covered;
static size_t source_size;

static void require(bool condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "ERROR: %s\n", message);
        exit(1);
    }
}

static void append(Buffer *buffer, const char *text, size_t length)
{
    if (buffer->length + length + 1 > buffer->capacity)
    {
        buffer->capacity = (buffer->length + length + 1) * 2;
        buffer->data = realloc(buffer->data, buffer->capacity);
        require(buffer->data != NULL, "allocation failed");
    }
    memcpy(buffer->data + buffer->length, text, length);
    buffer->length += length;
    buffer->data[buffer->length] = 0;
}

static void text(Buffer *buffer, const char *value)
{
    append(buffer, value, strlen(value));
}

static void format(Buffer *buffer, const char *pattern, ...)
{
    char line[4096];
    va_list args;
    va_start(args, pattern);
    int length = vsnprintf(line, sizeof(line), pattern, args);
    va_end(args);
    require(length >= 0 && (size_t)length < sizeof(line), "formatted line too long");
    append(buffer, line, (size_t)length);
}

static char *read_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    require(file != NULL, path);
    require(fseek(file, 0, SEEK_END) == 0, "seek failed");
    long size = ftell(file);
    require(size >= 0 && fseek(file, 0, SEEK_SET) == 0, "tell failed");
    char *data = calloc((size_t)size + 1, 1);
    require(data != NULL, "allocation failed");
    require(fread(data, 1, (size_t)size, file) == (size_t)size, "read failed");
    require(fclose(file) == 0, "close failed");
    return data;
}

static void write_file(const char *path, const Buffer *buffer)
{
    FILE *file = fopen(path, "wb");
    require(file != NULL, path);
    require(fwrite(buffer->data, 1, buffer->length, file) == buffer->length, "write failed");
    require(fclose(file) == 0, "close failed");
}

static int topic_id(const char *name)
{
    int result = -1;
    for (size_t i = 0; i < sizeof(topics) / sizeof(topics[0]); ++i)
    {
        if (strcmp(topics[i].name, name) == 0)
        {
            result = (int)i;
        }
    }
    require(result >= 0, name);
    return result;
}

static void slug(char *out, const char *title)
{
    size_t length = 0;
    for (const unsigned char *p = (const unsigned char *)title; *p; ++p)
    {
        if (isalnum(*p) || *p == '-' || *p == '_')
        {
            out[length++] = (char)tolower(*p);
        }
        else if (*p == ' ')
        {
            out[length++] = '-';
        }
        require(length < 250, "heading too long");
    }
    out[length] = 0;
}

static void original(Buffer *buffer, const char *source, size_t start, size_t end)
{
    require(start <= end && end <= source_size, "bad original range");
    append(buffer, source + start, end - start);
    covered += end - start;
}

static void replace_all(Buffer *buffer, const char *needle, const char *replacement)
{
    Buffer result = {0};
    const char *cursor = buffer->data;
    const char *match;
    while ((match = strstr(cursor, needle)) != NULL)
    {
        append(&result, cursor, (size_t)(match - cursor));
        text(&result, replacement);
        cursor = match + strlen(needle);
    }
    text(&result, cursor);
    free(buffer->data);
    *buffer = result;
}

static void destination(Buffer *buffer, int current, size_t section)
{
    if (sections[section].topic < 0)
    {
        text(buffer, "../../AGENTS.md");
    }
    else if (sections[section].topic != current)
    {
        format(buffer, "%s.md", topics[sections[section].topic].name);
    }
    format(buffer, "#%s", sections[section].anchor);
}

static void rewrite_links(Buffer *buffer, int current)
{
    Buffer result = {0};
    const char *p = buffer->data;
    while (*p)
    {
        if (p[0] == ']' && p[1] == '(')
        {
            const char *target = p + 2;
            const char *end = strchr(target, ')');
            require(end != NULL, "unterminated Markdown link");
            text(&result, "](");
            if (strncmp(target, "http:", 5) == 0 || strncmp(target, "https:", 6) == 0 || strncmp(target, "mailto:", 7) == 0 || *target == '/')
            {
                append(&result, target, (size_t)(end - target));
            }
            else
            {
                const char *anchor = *target == '#' ? target + 1 : NULL;
                if (strncmp(target, "AGENTS.md#", 10) == 0)
                {
                    anchor = target + 10;
                }
                bool found = false;
                if (anchor != NULL)
                {
                    for (size_t i = 0; i < section_count; ++i)
                    {
                        if (strlen(sections[i].anchor) == (size_t)(end - anchor) && strncmp(sections[i].anchor, anchor, (size_t)(end - anchor)) == 0)
                        {
                            destination(&result, current, i);
                            found = true;
                            break;
                        }
                    }
                    if (!found)
                    {
                        fprintf(stderr, "Unmapped fragment in %s: %.*s\n", topics[current].name, (int)(end - target), target);
                        require(false, "map the fragment before moving its link");
                    }
                }
                else
                {
                    text(&result, "../../");
                    append(&result, target, (size_t)(end - target));
                }
            }
            text(&result, ")");
            p = end + 1;
        }
        else
        {
            append(&result, p++, 1);
        }
    }
    free(buffer->data);
    *buffer = result;
    for (size_t i = 0; i < section_count; ++i)
    {
        Buffer replacement = {0};
        format(&replacement, "[%s](", sections[i].title);
        destination(&replacement, current, i);
        text(&replacement, ")");
        char needle[512];
        snprintf(needle, sizeof(needle), "\"%s\" below", sections[i].title);
        replace_all(buffer, needle, replacement.data);
        snprintf(needle, sizeof(needle), "\"%s\" above", sections[i].title);
        replace_all(buffer, needle, replacement.data);
        snprintf(needle, sizeof(needle), "\"%s\"", sections[i].title);
        replace_all(buffer, needle, replacement.data);
        free(replacement.data);
    }
}

int main(void)
{
    char *source = read_file("AGENTS.md");
    source_size = strlen(source);
    require(source_size == 435296, "the source must be the reviewed AGENTS.md blob");
    for (char *p = source; *p; ++p)
    {
        if ((p == source || p[-1] == '\n') && strncmp(p, "## ", 3) == 0)
        {
            require(section_count < 128, "too many sections");
            Section *section = &sections[section_count++];
            section->start = (size_t)(p - source);
            char *end = strchr(p, '\n');
            require(end != NULL, "heading has no newline");
            section->title = strndup(p + 3, (size_t)(end - p - 3));
            require(section->title != NULL, "allocation failed");
            slug(section->anchor, section->title);
            section->topic = -1;
        }
    }
    require(section_count == 74, "unexpected section count");
    for (size_t i = 0; i < section_count; ++i)
    {
        sections[i].end = i + 1 < section_count ? sections[i + 1].start : source_size;
        if (i >= 13)
        {
            require(i - 13 < sizeof(section_map) / sizeof(section_map[0]), "missing section map");
            require(strcmp(sections[i].title, section_map[i - 13].title) == 0, sections[i].title);
            sections[i].topic = topic_id(section_map[i - 13].topic);
        }
    }
    sections[10].topic = topic_id("ci");
    require(strcmp(sections[9].title, "Current repository and exact smoke commands") == 0, "missing inventory");
    require(strcmp(sections[10].title, "Sharing the 9700X") == 0, "missing lock rules");
    require(strcmp(sections[11].title, "Sharing the owner's machines' cores") == 0, "missing cores rules");
    require(strcmp(sections[12].title, "Agent memory") == 0, "missing memory rules");

    Buffer root = {0};
    original(&root, source, 0, sections[9].start);
    replace_all(&root, "it; keep project instructions here rather than maintaining parallel copies.", "it. Keep always-needed rules here and command/module details in the linked\nguides under `docs/commands/`; do not maintain parallel copies.");
    text(&root, "## Reading and maintaining this guide\n\nRead the command/module guide relevant to your change before editing or testing\nthat area; do not load the whole guide directory into every session. The\n[command index](docs/commands/README.md) links the detailed sections and their\nexisting design guides. Commands and source paths in those guides are relative\nto the repository root. Keep detailed flags, fixtures, acceptance boundaries and\nbenchmark recipes in the owning guide, not in this file. A new command adds or\nupdates its guide and one compact index line here. `CLAUDE.md` imports only this\nfile. CI enforces a **30,000-byte** limit here and a 1,024-byte limit on that\nimport wrapper; do not raise the limits to accommodate module detail.\n\n");
    text(&root, "## Sharing the 9700X\n\nAgent sessions and CI share the Ryzen 7 9700X. Run **every build, test, benchmark\nand perf run there** through `EXCLUSIVE_OWNER=<name> ~/buster-fast-bench/exclusive.sh COMMAND...`.\nSet `EXCLUSIVE_TITLE` to the session title and `EXCLUSIVE_LINK` to its private\nsession, PR or issue link. The runner hooks take the same lock; sessions and CI\nwait alike, with neither taking priority. Hold it **per run, never around a\nseries of runs**: CI times out waiting after 10 minutes. Keep PR jobs short.\nBenchmark timings come from this host under that lock, not from cloud runners\nor the owner's desktop. The `cores` queue below does not replace this lock.\n\nRead [CI and benchmark selection](docs/commands/ci.md) before changing gates,\nsuite inventory, benchmark steps or harness pins; it retains the full hosted\ncorrectness, sanitizer, native qualification and measurement rules. For tuning,\nalso read [profiling](docs/commands/profiling.md) and [the CI guide](docs/ci.md).\n\n");
    original(&root, source, sections[11].start, sections[12].end);
    text(&root, "## Build\n\nUse `./build.sh`, with Clang on Linux/x86-64; it produces `build/compiler`,\nincluding its tests and benchmarks. `BUSTER_HOST_ARCH=native` is the default;\nuse `x86-64` for the baseline scalar host and `znver5` for approved benchmark\nbuilds. Respect the lock and `cores` rules above. `BUSTER_DEBUG_INFO=0` omits\ndebug information. Read [build details](docs/commands/build.md) before changing\ntranslation units, build order, baked tables or code-placement experiments.\n`build/` is disposable; never commit generated binaries or captures.\n\n");

    Section *inventory = &sections[9];
    char *first = strstr(source + inventory->start, "\n- ");
    char *smoke = strstr(source + inventory->start, "From the repository root, on the approved Linux Ryzen 7 9700X host:");
    require(first != NULL && smoke != NULL && (size_t)(smoke - source) < inventory->end, "missing inventory boundaries");
    ++first;
    original(&root, source, inventory->start, (size_t)(first - source));
    size_t module = 0;
    char *cursor = first;
    while (cursor < smoke)
    {
        char *next = strstr(cursor + 2, "\n- ");
        if (next != NULL)
        {
            ++next;
        }
        if (next == NULL || next > smoke)
        {
            next = smoke;
        }
        require(module < sizeof(module_topics) / sizeof(module_topics[0]), "extra module bullet");
        if (strcmp(module_topics[module], "ROOT") == 0)
        {
            original(&root, source, (size_t)(cursor - source), (size_t)(next - source));
        }
        else
        {
            Topic *topic = &topics[topic_id(module_topics[module])];
            if (topic->body.length == 0)
            {
                text(&topic->body, "## Module overview\n\n");
            }
            original(&topic->body, source, (size_t)(cursor - source), (size_t)(next - source));
        }
        ++module;
        cursor = next;
    }
    require(module == 68, "missing module bullets");
    original(&root, source, (size_t)(smoke - source), inventory->end);
    for (size_t i = 10; i < section_count; ++i)
    {
        if (sections[i].topic >= 0)
        {
            original(&topics[sections[i].topic].body, source, sections[i].start, sections[i].end);
        }
    }
    require(covered == source_size, "source coverage is not exact");

    require(mkdir("docs/commands", 0755) == 0, "docs/commands must not already exist");
    Buffer index = {0};
    text(&index, "# Command and module guides\n\nRead only the guide for the area you are working on. [AGENTS.md](../../AGENTS.md)\ncontains the shared rules, build entry point, host-use requirements and smoke\ncommands. These guides hold the detailed command contracts and module status\npreviously embedded there; existing design documents remain authoritative for\ndesign. Edit the owning guide when adding flags, tests or benchmarks. Do not\nappend another repository-wide inventory to the root agent instructions.\n\nCommands run from the repository root. Source entry points below are relative\nto `src/buster/compiler/`, except paths beginning with `tools/`. A guide also\nnames its command-specific test and benchmark files. The commands in this\ntable are entry points, not an exhaustive list of every subtest or flag.\n\n## By module\n\n| Commands / area | Source entry point | Guide |\n| --- | --- | --- |\n");
    text(&root, "## Command and module index\n\nSource entry points below are relative to `src/buster/compiler/`, except\n`tools/` paths. Each guide contains the full test/benchmark commands, owning\nfiles, flags and acceptance boundaries; the entries here are compact starting\npoints. [Detailed section index](docs/commands/README.md#detailed-sections).\n\n");
    for (size_t i = 0; i < sizeof(topics) / sizeof(topics[0]); ++i)
    {
        Topic *topic = &topics[i];
        require(topic->body.length != 0, topic->name);
        char source_path[512];
        snprintf(source_path, sizeof(source_path), "%s%s", strncmp(topic->source, "tools/", 6) == 0 ? "" : "src/buster/compiler/", topic->source);
        struct stat metadata;
        if (stat(source_path, &metadata) != 0)
        {
            fprintf(stderr, "Missing source entry point: %s\n", source_path);
            require(false, "correct the source index");
        }
        rewrite_links(&topic->body, (int)i);
        Buffer output = {0};
        format(&output, "# %s\n\nRun commands and resolve source paths from the repository root. Follow the\n[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`\nrequirements. [Command index](README.md).\n\n", topic->name);
        append(&output, topic->body.data, topic->body.length);
        char path[512];
        snprintf(path, sizeof(path), "docs/commands/%s.md", topic->name);
        write_file(path, &output);
        format(&root, "- `%s` — `%s` — [guide](docs/commands/%s.md).\n", topic->commands, topic->source, topic->name);
        format(&index, "| `%s` | `%s` | [%s](%s.md) |\n", topic->commands, topic->source, topic->name, topic->name);
        printf("%6zu %s\n", output.length, path);
        free(output.data);
    }
    text(&index, "\n## Detailed sections\n\nThe original section titles are retained in their owning guides.\n\n");
    for (size_t i = 0; i < section_count; ++i)
    {
        if (sections[i].topic >= 0)
        {
            format(&index, "- [%s](%s.md#%s).\n", sections[i].title, topics[sections[i].topic].name, sections[i].anchor);
        }
    }
    require(root.length <= 30000, "root exceeds its 30,000-byte budget");
    write_file("docs/commands/README.md", &index);
    write_file("AGENTS.md", &root);
    Buffer claude = {0};
    text(&claude, "@AGENTS.md\n\nRead the shared rules in AGENTS.md, then only the linked command/module guide\nneeded for the task. Maintain detailed instructions in that guide, not here.\n");
    write_file("CLAUDE.md", &claude);
    printf("PASS: %zu original bytes accounted for exactly once; %zu sections, %zu module bullets; root %zu / 30000 bytes.\n", covered, section_count, module, root.length);
    return 0;
}
