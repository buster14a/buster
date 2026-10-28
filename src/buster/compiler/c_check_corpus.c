// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Independent source-corpus qualification. External files stay external: the harness records every input and
// compares acceptance and the first diagnostic line with Clang, and the complete scalar/vector parser, binding,
// graph and typed results with each other. Unsupported valid files are failures, never successful skips.
// --semantic-mutations checks the files' semantic mutants the same way (c_check_mutation.c). --symbols compares, for
// each file of the command line that the checker accepts, the functions and objects its -c object defines with
// clang -O0 -c's (#129, check_corpus_symbols_file), and --symbols-minimum N fails the run unless at least N files'
// objects were compared, so that a corpus whose files the backend comes to refuse can't pass by comparing none.
// Include after c_checker_test.c for the shared source-oracle and exact-value comparison helpers.
#ifndef BUSTER_C_CHECK_CORPUS_INCLUDED
#define BUSTER_C_CHECK_CORPUS_INCLUDED

#include "c_check_prefix.c"
#include "c_check_repair.c"

typedef struct CheckCorpusPath CheckCorpusPath;
struct CheckCorpusPath
{
    Arena types_arena;
    Arena index_arena;
    Arena input_arena;
    Arena output_arena;
    Arena scratch;
    Arena prefix_arena;
    CheckPrefix* prefix;
    CheckRepair* repair;
    CheckCorpusPath* repaired;
    CheckInput* original_pragmas;
    TypeTable types;
    CheckInput input;
    // #113's deferred reader (--deferred): with deferred_lanes, the input is check_input_deferred's on that many lanes,
    // whose state, in deferred_arena, lives as long as the input.
    uint32_t deferred_lanes;
    CheckInputBindingMode deferred_bindings;
    Arena deferred_arena;
    CheckDeferred* deferred;
    CheckStream stream;
    CheckInitializers initializers;
    CheckAsms asms;
    CheckInitDiagnostic initializer_diagnostic;
    CheckInitWarningSink initializer_warnings;
    CheckAsmDiagnostic asm_diagnostic;
    CheckProgramDiagnostic program_diagnostic;
    CheckProgram program;
    bool parsed;
    bool adapted;
    bool checked;
    bool initialized;
    bool programmed;
    bool resource;
    bool suppress_warnings;
    uint32_t warning_token;
    uint32_t warning_source;
    uint32_t token;
    uint32_t position;
    uint32_t pragma;
    uint32_t line;
    uint32_t column;
    const char* phase;
    char file[512];
    char message[256];
    char spelling[96];
};

#define BUSTER_CHECK_CORPUS_DEFERRED_REASONS 16

// --deferred's reason for a unit that doesn't parse, which the reader never reads: tallied, not reported per file.
static const char* const check_corpus_unparsed = "the unit doesn't parse";

typedef struct CheckCorpusTally CheckCorpusTally;
struct CheckCorpusTally
{
    uint64_t files;
    uint64_t excluded;
    uint64_t preprocessed;
    uint64_t preprocessing_excluded;
    uint64_t syntax_accepted_excluded;
    uint64_t oracle_accepted;
    uint64_t checker_accepted;
    uint64_t acceptance_differences;
    uint64_t line_differences;
    uint64_t diagnostic_path_differences;
    uint64_t path_differences;
    uint64_t resource_failures;
    uint64_t preprocessing_failures;
    uint64_t tokens;
    uint64_t nodes;
    uint64_t scalar_nodes;
    uint64_t vector_nodes;
    uint64_t binding_uses;
    uint64_t binding_fallback_uses;
    // --deferred: the units the deferred reader merged, those it left to source order, their bodies, and the files whose
    // deferred result wasn't the vector path's (counted in path_differences too).
    uint64_t deferred_units;
    uint64_t deferred_source_order;
    uint64_t deferred_bodies;
    uint64_t deferred_differences;
    // The units left to source order by why (the reader's reasons are fixed strings), in the order first met.
    const char* deferred_reasons[BUSTER_CHECK_CORPUS_DEFERRED_REASONS];
    uint64_t deferred_reason_units[BUSTER_CHECK_CORPUS_DEFERRED_REASONS];
    uint32_t deferred_reason_count;
    uint64_t reports;
};

// Adds `units` to the tally of `reason`, past the table's room to its last entry's.
static void check_corpus_tally_reason(CheckCorpusTally* tally, const char* reason, uint64_t units)
{
    uint32_t found = tally->deferred_reason_count;
    for (uint32_t i = 0; i < tally->deferred_reason_count; i += 1)
    {
        found = found == tally->deferred_reason_count && strcmp(tally->deferred_reasons[i], reason) == 0 ? i : found;
    }
    if (found == tally->deferred_reason_count && found < BUSTER_CHECK_CORPUS_DEFERRED_REASONS)
    {
        tally->deferred_reasons[found] = reason;
        tally->deferred_reason_count += 1;
    }
    found = found < BUSTER_CHECK_CORPUS_DEFERRED_REASONS ? found : BUSTER_CHECK_CORPUS_DEFERRED_REASONS - 1;
    tally->deferred_reason_units[found] += units;
}

typedef struct CheckCorpusOracle CheckCorpusOracle;
struct CheckCorpusOracle
{
    bool rejected;
    bool suppress_warnings;
    uint32_t line;
    uint32_t column;
    char file[512];
    char message[256];
};

// The parser oracle intentionally distinguishes syntax errors from later semantic errors. The checker requires
// every first error, including an included header's, and treats process/I/O failures separately from rejection.
// One invocation owns one translation unit. Included files may themselves be corpus inputs, #line may rename a
// location, and fatal include errors can survive -ferror-limit=1; none can misattribute acceptance to another file.
static bool check_corpus_clang_phase(const char* executable, const char* const* arguments, uint32_t argument_count,
                                     const char* path, bool preprocess, CheckCorpusOracle* result)
{
    *result = (CheckCorpusOracle){ 0 };
    char** argv = calloc((size_t)argument_count + 12, sizeof(char*));
    char** environment = pp_test_clang_environment();
    bool ready = argv != 0 && environment != 0;
    int status = 0;
    char* text = 0;
    if (ready)
    {
        uint32_t used = 0;
        argv[used++] = (char*)executable;
        for (uint32_t argument = 0; argument < argument_count; argument += 1)
        {
            argv[used++] = (char*)arguments[argument];
            result->suppress_warnings |= strcmp(arguments[argument], "-w") == 0;
        }
        argv[used++] = preprocess ? "-E" : "-fsyntax-only";
        argv[used++] = "-fno-caret-diagnostics";
        argv[used++] = "-fno-color-diagnostics";
        argv[used++] = "-ferror-limit=1";
        if (preprocess)
        {
            // Only its diagnostics are read: the text goes to /dev/null, as it did through standard output.
            argv[used++] = "-o";
            argv[used++] = "/dev/null";
        }
        argv[used++] = (char*)path;
        argv[used] = 0;
        // Through the oracle cache (oracle_cache.c, #542), without CPATH and C_INCLUDE_PATH.
        OracleRun run = {
            .purpose = "checker corpus: clang's first error",
            .arguments = argv,
            .environment = environment,
            .dependencies = true,
        };
        ready = oracle_run(&run) && WIFEXITED(run.status);
        status = run.status;
        text = run.errors;
        run.errors = 0;
        oracle_run_release(&run);
    }
    bool unaccounted = false;
    for (char* at = text; ready && at != 0 && *at != 0; )
    {
        char* end = strchr(at, '\n');
        if (end != 0)
        {
            *end = 0;
        }
        char* marker = strstr(at, ": error: ");
        char* fatal = strstr(at, ": fatal error: ");
        marker = marker != 0 ? marker : fatal;
        if (marker != 0 && strstr(at, "too many errors emitted") == 0)
        {
            char* column = marker;
            while (column > at && column[-1] >= '0' && column[-1] <= '9')
            {
                column -= 1;
            }
            char* line = column > at && column[-1] == ':' ? column - 1 : column;
            while (line > at && line[-1] >= '0' && line[-1] <= '9')
            {
                line -= 1;
            }
            const bool located = column > at && column < marker && line < column - 1 && line > at && line[-1] == ':';
            unaccounted |= !located;
            if (located && !result->rejected)
            {
                result->rejected = true;
                result->line = (uint32_t)strtoul(line, 0, 10);
                result->column = (uint32_t)strtoul(column, 0, 10);
                snprintf(result->file, sizeof(result->file), "%.*s", (int)(line - at - 1), at);
                snprintf(result->message, sizeof(result->message), "%s", marker + (marker == fatal ? 15 : 9));
            }
        }
        at = end != 0 ? end + 1 : 0;
    }
    ready = ready && !unaccounted && ((WEXITSTATUS(status) == 0 && !result->rejected) || (WEXITSTATUS(status) == 1 && result->rejected));
    if (!ready)
    {
        fprintf(stderr, "checker-corpus: Clang oracle process or diagnostic accounting failed for %s (status=%d)\n", path, status);
    }
    free(text);
    free(argv);
    free(environment);
    return ready;
}

static bool check_corpus_clang_file(const char* executable, const char* const* arguments, uint32_t argument_count,
                                    const char* path, CheckCorpusOracle* result)
{
    return check_corpus_clang_phase(executable, arguments, argument_count, path, false, result);
}

static bool check_corpus_clang(const char* executable, const char* const* arguments, uint32_t argument_count,
                               char** paths, uint32_t count, CheckCorpusOracle* results)
{
    bool ready = true;
    for (uint32_t file = 0; ready && file < count; file += 1)
    {
        ready = check_corpus_clang_file(executable, arguments, argument_count, paths[file], results + file);
    }
    return ready;
}

static void check_corpus_field(FILE* stream, const char* value)
{
    for (const char* at = value; *at != 0; at += 1)
    {
        const unsigned char byte = (unsigned char)*at;
        if (byte == '\t' || byte == '\n' || byte == '\r' || byte == '\\')
        {
            fprintf(stream, "\\%c", byte == '\t' ? 't' : byte == '\n' ? 'n' : byte == '\r' ? 'r' : '\\');
        }
        else
        {
            fputc(byte, stream);
        }
    }
}

static bool check_corpus_primary_location(const Preprocessor* pp, uint32_t token, uint32_t* location)
{
    uint32_t origin = pp_history_at(pp, token);
    const uint64_t maximum = (uint64_t)pp->history_count + pp->substitution_count + pp->generated_count;
    bool valid = token < pp->output.count;
    *location = valid ? pp->output.locations[token] : 0;
    for (uint64_t step = 0; valid && origin != 0 && step < maximum; step += 1)
    {
        const uint32_t index = (origin & BUSTER_PP_ORIGIN_INDEX) - 1;
        const uint32_t kind = origin & ~BUSTER_PP_ORIGIN_INDEX;
        if (kind == BUSTER_PP_ORIGIN_ARGUMENT)
        {
            valid = index < pp->substitution_count;
            origin = valid ? pp->substitutions[index].source : origin;
        }
        else if (kind == BUSTER_PP_ORIGIN_GENERATED)
        {
            valid = index < pp->generated_count;
            if (valid)
            {
                *location = pp->generated[index].anchor;
                origin = pp->generated[index].expansion;
            }
        }
        else
        {
            valid = kind == 0 && index < pp->history_count;
            if (valid)
            {
                *location = pp->history[index].begin;
                origin = pp->history[index].parent;
            }
        }
    }
    return valid && origin == 0;
}

static uint32_t check_corpus_source(const CheckCorpusPath* path, const Parser* parser)
{
    const CheckInputReport report =
    {
        .token = path->token,
        .pragma = path->pragma,
    };
    return path->pragma != 0 && strcmp(path->phase, "promoted-warning") == 0 ? path->warning_source :
           check_diagnostic_input_report_source(&path->input, parser, &report);
}

static void check_corpus_location(CheckCorpusPath* path, const Parser* parser, Preprocessor* pp)
{
    const uint32_t source = check_corpus_source(path, parser);
    if (source < pp->output.count)
    {
        const uint32_t spelling_location = pp->output.locations[source];
        uint32_t location = 0;
        path->resource |= !check_corpus_primary_location(pp, source, &location);
        const char* name = 0;
        uint32_t length = 0;
        path->line = pp_presumed(pp, location, &name, &length);
        snprintf(path->file, sizeof(path->file), "%.*s", (int)length, name != 0 ? name : "");
        const uint32_t range = pp_find_range(pp, location);
        if (range != BUSTER_PP_NONE)
        {
            pp_line_of(pp, range, location, &path->column);
        }
        const uint8_t* spelling = 0;
        const uint8_t kind = pp->output.kinds[source];
        const bool identifier = kind == C_TOKEN_RAW_IDENTIFIER || (kind >= BUSTER_PP_KIND_KEYWORD && kind < BUSTER_PP_KIND_KEYWORD + C_NAME_KEYWORD_COUNT);
        const uint64_t bytes = identifier || kind == BUSTER_PP_KIND_INTEGER || kind == BUSTER_PP_KIND_EMBED ? 24 :
                               (uint64_t)pp->output.values[source] - spelling_location + 1;
        Scratch scratch = scratch_begin(0);
        uint8_t* buffer = arena_push(scratch.arena, bytes, 1);
        if (buffer != 0)
        {
            const uint32_t spelling_length = pp_spelling(pp, kind, pp->output.values[source], spelling_location, buffer, &spelling);
            snprintf(path->spelling, sizeof(path->spelling), "%.*s", (int)spelling_length, spelling);
        }
        scratch_end(scratch);
    }
}

static void check_corpus_release(CheckCorpusPath* path)
{
    if (path->deferred != NULL)
    {
        check_deferred_release(path->deferred);
    }
    arena_release(&path->deferred_arena);
    arena_release(&path->scratch);
    arena_release(&path->output_arena);
    arena_release(&path->input_arena);
    arena_release(&path->index_arena);
    arena_release(&path->types_arena);
    arena_release(&path->prefix_arena);
}

static void check_corpus_close(CheckCorpusPath* path)
{
    if (path->repaired != NULL)
    {
        check_corpus_release(path->repaired);
    }
    check_corpus_release(path);
}

static uint32_t check_corpus_repair_warning(const CheckCorpusPath* path, const CheckRepair* repair, uint32_t token,
                                          uint32_t warnings, uint32_t ignored, uint32_t first)
{
    if (token < first && check_repair_warning_token(repair, token))
    {
        while (warnings != 0)
        {
            const uint32_t warning = warnings & (0u - warnings);
            if (check_diagnostic_severity_default(&path->input, token, warning, (ignored & warning) != 0) >= CHECK_WARNING_ERROR)
            {
                first = token;
            }
            warnings &= warnings - 1;
        }
    }
    return first;
}

static void check_corpus_warning_diagnostics(CheckCorpusPath* path, const Parser* parser, uint32_t before_token, const CheckRepair* repair)
{
    uint32_t first = BUSTER_CHECK_NONE;
    uint32_t pragma = 0;
    uint32_t pragma_source = BUSTER_CHECK_NONE;
    bool invalid_pragma = false;
    if (!path->suppress_warnings && !path->resource)
    {
        if (repair == NULL)
        {
            check_diagnostic_first_error(&path->input, path->checked ? &path->stream : NULL, &first);
        }
        else
        {
            for (uint32_t token = 0; path->input.attributes != NULL && token < path->input.token_count; token += 1)
            {
                first = check_corpus_repair_warning(path, repair, token, check_diagnostic_attribute_warnings(&path->input, token), 0, first);
            }
            for (uint32_t token = 0; path->input.constant_warnings != NULL && token < path->input.token_count; token += 1)
            {
                const uint32_t ignored = path->input.constant_warning_ignored != NULL ? path->input.constant_warning_ignored[token] : 0;
                first = check_corpus_repair_warning(path, repair, token, path->input.constant_warnings[token], ignored, first);
            }
            for (uint32_t node = 0; path->checked && node < path->stream.count; node += 1)
            {
                const uint32_t ignored = path->stream.warning_ignored != NULL ? path->stream.warning_ignored[node] : 0;
                first = check_corpus_repair_warning(path, repair, path->input.graph.tokens[node], path->stream.warnings[node], ignored, first);
            }
        }
        for (uint32_t i = 0; i < path->initializers.warning_count; i += 1)
        {
            const uint32_t token = path->initializers.warning_tokens[i];
            if (token < first && (repair == NULL || check_repair_warning_token(repair, token)) &&
                check_diagnostic_severity(&path->input, token, CHECK_WARNING_EXCESS_INITIALIZERS) >= CHECK_WARNING_ERROR)
            {
                first = token;
            }
        }
        for (uint32_t i = 0; i < path->initializer_warnings.count; i += 1)
        {
            const uint32_t token = path->initializer_warnings.tokens[i];
            if (token < first && (repair == NULL || check_repair_warning_token(repair, token)) &&
                check_diagnostic_severity(&path->input, token, CHECK_WARNING_EXCESS_INITIALIZERS) >= CHECK_WARNING_ERROR)
            {
                first = token;
            }
        }
    }
    if (!path->resource)
    {
        uint32_t pragma_token = BUSTER_CHECK_NONE;
        const uint32_t pragma_before = repair != NULL ? check_repair_pragma_end(repair) : before_token;
        if (check_diagnostic_first_pragma_error_mode(&path->input, pragma_before, &pragma_token, &pragma_source, path->suppress_warnings) && pragma_token <= first)
        {
            for (uint32_t i = 0; i < path->input.fp_pragma_count && pragma == 0; i += 1)
            {
                const CheckInputFpPragma* record = path->input.fp_pragmas + i;
                if (check_diagnostic_pragma_source(record) == pragma_source)
                {
                    pragma = record->pragma + 1;
                    invalid_pragma = (record->warnings & CHECK_FP_PRAGMA_INVALID_PLACEMENT) != 0;
                }
            }
            first = pragma_token;
        }
    }
    const uint32_t warning_before = pragma != 0 && repair != NULL ? check_repair_pragma_end(repair) : before_token;
    path->warning_token = first < warning_before ? first : BUSTER_CHECK_NONE;
    const bool earlier_pragma = pragma != 0 && path->warning_token == path->position && pragma_source < check_corpus_source(path, parser);
    if (path->warning_token < path->position || earlier_pragma)
    {
        path->token = path->warning_token;
        path->position = path->token;
        path->pragma = pragma;
        path->warning_source = pragma_source;
        path->phase = "promoted-warning";
        snprintf(path->message, sizeof(path->message), "%s", invalid_pragma ? "floating-point pragma is not permitted within an expression" :
                 pragma != 0 ? check_diagnostic_message(CHECK_WARNING_UNKNOWN_PRAGMA) : "semantic warning promoted to an error");
    }
}

static void check_corpus_late_diagnostics(CheckCorpusPath* path, const Parser* parser, uint32_t before_token)
{
    const bool assemblies = check_asms_diagnose(&path->asm_diagnostic, &path->input, &path->stream, &path->types, before_token, &path->scratch);
    path->resource |= path->asm_diagnostic.status == CHECK_ASM_RESOURCE;
    if (assemblies && path->asm_diagnostic.status != CHECK_ASM_OK && path->asm_diagnostic.token < path->position)
    {
        path->token = path->asm_diagnostic.token;
        path->position = path->token;
        path->pragma = 0;
        path->phase = "assembly";
        snprintf(path->message, sizeof(path->message), "%s", "invalid assembly operand");
    }
    path->initializer_warnings.capacity = path->input.token_count;
    path->initializer_warnings.count = 0;
    path->initializer_warnings.tokens = path->input.token_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(&path->output_arena, uint32_t, path->input.token_count) : NULL;
    const bool warning_storage = path->input.token_count == 0 || path->initializer_warnings.tokens != NULL;
    path->resource |= !warning_storage;
    const bool initializers = warning_storage && check_initializers_diagnose_warnings(&path->initializer_diagnostic, &path->input, &path->stream,
                                                                                     parser, &path->types, before_token, &path->output_arena,
                                                                                     &path->scratch, &path->initializer_warnings);
    path->resource |= path->initializer_diagnostic.status == CHECK_INIT_RESOURCE;
    if (initializers && path->initializer_diagnostic.status != CHECK_INIT_OK && path->initializer_diagnostic.token < path->position)
    {
        path->token = path->initializer_diagnostic.token;
        path->position = path->token;
        path->pragma = 0;
        path->phase = "initializer";
        snprintf(path->message, sizeof(path->message), "%s", check_driver_initializer_message(path->initializer_diagnostic.status));
    }
    const bool controls = check_program_diagnose(&path->program_diagnostic, &path->input, &path->stream, parser, &path->types,
                                                  before_token, &path->scratch);
    path->resource |= path->program_diagnostic.status == CHECK_PROGRAM_RESOURCE;
    if (controls && path->program_diagnostic.status != CHECK_PROGRAM_OK && path->program_diagnostic.position < path->position)
    {
        path->token = path->program_diagnostic.token;
        path->position = path->program_diagnostic.position;
        path->pragma = 0;
        path->phase = "control";
        snprintf(path->message, sizeof(path->message), "%s", "invalid C statement or control flow");
    }
}

static void check_corpus_repair(CheckCorpusPath* path, const Parser* original, Preprocessor* pp, bool scalar)
{
    path->repair = BUSTER_ARENA_PUSH_ARRAY(&path->prefix_arena, CheckRepair, 1);
    bool ready = path->repair != NULL;
    if (ready)
    {
        const CheckRepairStatus status = check_repair_make(path->repair, original, pp, &path->prefix_arena, &path->scratch);
        path->resource |= status == CHECK_REPAIR_RESOURCE;
        if (status == CHECK_REPAIR_READY)
        {
            path->repaired = BUSTER_ARENA_PUSH_ARRAY(&path->prefix_arena, CheckCorpusPath, 1);
            ready = path->repaired != NULL;
            if (ready)
            {
                *path->repaired = (CheckCorpusPath)
                {
                    .parsed = true,
                    .token = BUSTER_CHECK_NONE,
                    .position = BUSTER_CHECK_NONE,
                    .phase = "declaration",
                    .suppress_warnings = path->suppress_warnings,
                    .warning_token = BUSTER_CHECK_NONE,
                    .warning_source = BUSTER_CHECK_NONE,
                };
            }
        }
    }
    CheckCorpusPath* result = path->repaired;
    const CheckRepair* repair = path->repair;
    if (ready && result != NULL)
    {
        const uint32_t tokens = repair->parser.tokens.count;
        const TypeCapacity capacity =
        {
            .types = tokens < (1u << 27) ? tokens * 4 + 64 : 0,
            .parameters = tokens + 1,
            .tags = tokens + 1,
            .members = tokens + 1,
        };
        result->types_arena = arena_reserve(type_capacity_bytes(capacity));
        result->index_arena = arena_reserve(type_index_capacity_bytes(capacity));
        result->input_arena = arena_reserve(UINT64_C(1) << 36);
        result->output_arena = arena_reserve(UINT64_C(1) << 36);
        result->scratch = arena_reserve(UINT64_C(1) << 36);
        ready = capacity.types != 0 && result->input_arena.base != NULL && result->output_arena.base != NULL && result->scratch.base != NULL &&
                type_initialize(&result->types, &result->types_arena, &result->index_arena, capacity);
        if (ready)
        {
            result->adapted = check_input_fragment(&result->input, &repair->parser, repair->preprocessor, &result->types,
                                                   &result->input_arena, &result->scratch, repair->original_end);
            result->resource = result->input.resource;
            for (uint32_t report = 0; report < result->input.report_count; report += 1)
            {
                const CheckInputReport* diagnostic = result->input.reports + report;
                if ((check_repair_input_report(repair, diagnostic) || check_repair_loop_report(repair, &result->input, diagnostic)) && diagnostic->token < result->position)
                {
                    result->token = diagnostic->token;
                    result->position = result->token;
                    result->pragma = diagnostic->pragma;
                    snprintf(result->message, sizeof(result->message), "%s", check_driver_input_report_message(&result->input, diagnostic));
                }
            }
            if (result->adapted || result->input.diagnostic_graph)
            {
                const CheckOptions options =
                {
                    .scalar = scalar,
                };
                result->checked = check_graph_run(&result->types, &result->input.graph, &result->output_arena, &result->scratch, &options, &result->stream);
                result->checked = result->checked && check_input_packed_finalize(&result->input, &repair->parser, &result->types, &result->stream, &result->scratch);
                result->resource |= result->input.resource || result->stream.status == CHECK_STATUS_RESOURCE;
                for (uint32_t report = 0; report < result->stream.report_count; report += 1)
                {
                    const CheckReport* diagnostic = result->stream.reports + report;
                    if (check_repair_graph_report(repair, &result->input.graph, diagnostic) && diagnostic->token < result->position)
                    {
                        result->token = diagnostic->token;
                        result->position = result->token;
                        result->pragma = 0;
                        result->phase = "expression";
                        snprintf(result->message, sizeof(result->message), "%s", check_driver_report_message(diagnostic));
                    }
                }
                if (result->checked && !result->resource)
                {
                    check_corpus_late_diagnostics(result, &repair->parser, repair->record_begin);
                }
            }
            check_corpus_warning_diagnostics(result, &repair->parser, repair->record_begin, repair);
            check_corpus_location(result, &repair->parser, repair->preprocessor);
        }
        result->resource |= !ready;
        path->resource |= result->resource;
        if (result->position < path->position)
        {
            path->token = result->token;
            path->position = result->position;
            path->pragma = result->pragma;
            path->warning_token = result->warning_token;
            path->warning_source = result->warning_source;
            path->line = result->line;
            path->column = result->column;
            path->phase = result->phase;
            memcpy(path->file, result->file, sizeof(path->file));
            memcpy(path->message, result->message, sizeof(path->message));
            memcpy(path->spelling, result->spelling, sizeof(path->spelling));
        }
    }
    path->resource |= !ready;
}

// --deferred's lanes (0 for none) and binding resolution: the third path of check_corpus_measure, which reads on every
// lane the bodies can take, up to the lanes, however few tokens they have (CheckDeferred.every_lane). For
// test-checker-deferred: without check_corpus_deferred_every_lane, the reader's own rule of how many lanes; with
// check_corpus_deferred_fail_merge, the deferred reader's merge fails after its replay; with
// check_corpus_deferred_fail_state, the allocation of its state fails, as one that runs out of room does; with
// check_corpus_deferred_hold_lanes, its first lane takes every part (CheckDeferred.hold_lanes); and with
// check_corpus_deferred_timing, it times its phases and what its lanes cost (CheckDeferred.timing); the tests may also
// supply check_corpus_deferred_cpus to check one-lane context isolation from a pinned team.
static uint32_t check_corpus_deferred_lanes;
static CheckInputBindingMode check_corpus_deferred_bindings;
static bool check_corpus_deferred_every_lane = true;
static bool check_corpus_deferred_fail_merge;
static bool check_corpus_deferred_fail_state;
static bool check_corpus_deferred_hold_lanes;
static bool check_corpus_deferred_timing;
static const uint32_t* check_corpus_deferred_cpus;

static bool check_corpus_check_policy(CheckCorpusPath* path, Parser* parser, Preprocessor* pp, bool scalar, bool suppress_warnings)
{
    path->suppress_warnings = suppress_warnings;
    path->warning_token = BUSTER_CHECK_NONE;
    path->warning_source = BUSTER_CHECK_NONE;
    path->token = BUSTER_CHECK_NONE;
    path->position = BUSTER_CHECK_NONE;
    path->pragma = 0;
    path->phase = "parse";
    const ParseOptions parse_options = {
        .scalar = scalar,
    };
    bool ready = parse_initialize(parser, &parse_options, pp->options.language);
    if (ready)
    {
        const bool parsed = parse_translation_unit(parser, pp);
        path->parsed = parsed && parser->errors == 0 && parser->fatal == PARSE_MESSAGE_NONE;
        path->resource = parser->fatal != PARSE_MESSAGE_NONE;
    }
    const Parser* view = parser;
    bool semantic = path->parsed;
    if (ready && !path->parsed && !path->resource)
    {
        path->prefix_arena = arena_reserve(UINT64_C(1) << 36);
        path->scratch = arena_reserve(UINT64_C(1) << 36);
        path->prefix = BUSTER_ARENA_PUSH_ARRAY(&path->prefix_arena, CheckPrefix, 1);
        ready = path->prefix != 0 && path->scratch.base != 0;
        if (ready)
        {
            const CheckPrefixStatus status = check_prefix_make(path->prefix, parser, &path->prefix_arena, &path->scratch);
            path->resource |= status == CHECK_PREFIX_RESOURCE;
            semantic = status == CHECK_PREFIX_READY;
            view = semantic ? &path->prefix->parser : parser;
        }
    }
    if (ready && semantic)
    {
        const uint32_t tokens = view->tokens.count;
        const TypeCapacity capacity = {
            .types = tokens < (1u << 27) ? tokens * 4 + 64 : 0,
            .parameters = tokens + 1,
            .tags = tokens + 1,
            .members = tokens + 1,
        };
        path->phase = "declaration";
        path->types_arena = arena_reserve(type_capacity_bytes(capacity));
        path->index_arena = arena_reserve(type_index_capacity_bytes(capacity));
        path->input_arena = arena_reserve(UINT64_C(1) << 36);
        path->output_arena = arena_reserve(UINT64_C(1) << 36);
        if (path->scratch.base == 0)
        {
            path->scratch = arena_reserve(UINT64_C(1) << 36);
        }
        ready = capacity.types != 0 && path->input_arena.base != 0 && path->output_arena.base != 0 && path->scratch.base != 0 &&
                type_initialize(&path->types, &path->types_arena, &path->index_arena, capacity);
        if (ready)
        {
            if (path->parsed && path->deferred_lanes != 0)
            {
                path->deferred_arena = arena_reserve(UINT64_C(1) << 24);
                path->deferred = check_corpus_deferred_fail_state ? NULL : BUSTER_ARENA_PUSH_ARRAY(&path->deferred_arena, CheckDeferred, 1);
                if (path->deferred != NULL)
                {
                    path->deferred->every_lane = check_corpus_deferred_every_lane;
                    path->deferred->fail_after_replay = check_corpus_deferred_fail_merge;
                    path->deferred->hold_lanes = check_corpus_deferred_hold_lanes;
                    path->deferred->timing = check_corpus_deferred_timing;
                    path->deferred->cpus = check_corpus_deferred_cpus;
                }
                path->adapted = path->deferred != NULL && check_input_deferred(path->deferred, &path->input, view, pp, &path->types, &path->input_arena,
                                                                                &path->scratch, path->deferred_lanes, path->deferred_bindings);
                path->resource |= path->deferred == NULL;
            }
            else
            {
                path->adapted = path->parsed ? check_input(&path->input, view, pp, &path->types, &path->input_arena, &path->scratch) :
                                              check_input_prefix(&path->input, view, pp, &path->types, &path->input_arena, &path->scratch);
            }
            // Or'd: until the input, only the deferred reader's state, if it couldn't be allocated, sets it.
            path->resource |= path->input.resource;
            for (uint32_t report = 0; report < path->input.report_count; report += 1)
            {
                if (path->input.reports[report].token < path->token)
                {
                    path->token = path->input.reports[report].token;
                    path->position = path->token;
                    path->pragma = path->input.reports[report].pragma;
                    snprintf(path->message, sizeof(path->message), "%s", check_driver_input_report_message(&path->input, path->input.reports + report));
                }
            }
        }
    }
    if (ready && (path->adapted || path->input.diagnostic_graph))
    {
        path->phase = path->adapted ? "expression" : path->phase;
        const CheckOptions options = {
            .scalar = scalar,
        };
        path->checked = check_graph_run(&path->types, &path->input.graph, &path->output_arena, &path->scratch, &options, &path->stream);
        path->checked = path->checked && check_input_packed_finalize(&path->input, view, &path->types, &path->stream, &path->scratch);
        path->resource |= path->input.resource || path->stream.status == CHECK_STATUS_RESOURCE;
        if (path->stream.report_count != 0 && path->stream.reports[0].token < path->token)
        {
            path->phase = "expression";
            path->token = path->stream.reports[0].token;
            path->position = path->token;
            path->pragma = 0;
            snprintf(path->message, sizeof(path->message), "%s", check_driver_report_message(path->stream.reports));
        }
    }
    bool assemblies = false;
    if (path->adapted && path->checked && path->stream.report_count == 0)
    {
        path->phase = "assembly";
        assemblies = check_asms_run(&path->asms, &path->input, &path->stream, &path->types, &path->output_arena, &path->scratch);
        path->resource |= path->asms.status == CHECK_ASM_RESOURCE;
        if (!assemblies)
        {
            path->token = path->asms.token;
            path->position = path->token;
            path->pragma = 0;
            snprintf(path->message, sizeof(path->message), "%s", path->asms.status == CHECK_ASM_RESOURCE ? "assembly checking capacity exhausted" : "invalid assembly operand");
        }
    }
    if (assemblies)
    {
        path->phase = "initializer";
        path->initialized = check_initializers_run(&path->initializers, &path->input, &path->stream, view, &path->types,
                                                   &path->output_arena, &path->scratch);
        path->resource |= path->initializers.status == CHECK_INIT_RESOURCE;
        if (!path->initialized)
        {
            path->token = path->initializers.token;
            path->position = path->token;
            path->pragma = 0;
            snprintf(path->message, sizeof(path->message), "%s", check_driver_initializer_message(path->initializers.status));
        }
    }
    if (path->initialized)
    {
        path->phase = "control";
        const CheckProgramOptions options = {
            .initializers = &path->initializers,
            .asms = &path->asms,
            .identifiers = pp->table,
            .scalar = scalar,
        };
        path->programmed = check_program(&path->program, view, &path->input, &path->stream, &path->types, options,
                                         &path->output_arena, &path->scratch);
        path->resource |= path->program.status == CHECK_PROGRAM_RESOURCE;
        if (!path->programmed)
        {
            path->token = path->program.token;
            path->position = path->program.diagnostic_position;
            path->pragma = 0;
            snprintf(path->message, sizeof(path->message), "%s", path->program.status == CHECK_PROGRAM_RESOURCE ? "control checking capacity exhausted" :
                     path->program.status == CHECK_PROGRAM_UNSUPPORTED ? "control construct is not supported" : "invalid function control flow");
        }
        else
        {
            path->phase = "accepted";
        }
    }
    path->resource |= !ready;
    if (path->checked && !path->resource && (!path->programmed || !path->parsed))
    {
        check_corpus_late_diagnostics(path, view, path->position);
    }
    check_corpus_warning_diagnostics(path, view, BUSTER_CHECK_NONE, NULL);
    if (!path->parsed && path->token == BUSTER_CHECK_NONE)
    {
        // A clean semantic prefix never accepts the failed original parse or turns its cutoff into TU end.
        path->phase = "parse";
        const ParseTestOutcome outcome = parse_test_outcome(parser, pp);
        path->line = outcome.line;
        path->column = outcome.column;
        snprintf(path->message, sizeof(path->message), "%s", outcome.message);
        for (uint32_t report = 0; report < parser->diagnostic_count && path->token == BUSTER_CHECK_NONE; report += 1)
        {
            const ParseDiagnostic* diagnostic = parser->diagnostics + report;
            if (diagnostic->error)
            {
                path->token = diagnostic->token;
                path->position = path->token;
                path->pragma = 0;
                const uint32_t location = parse_diagnostic_location(parser, pp, diagnostic);
                const char* name = 0;
                uint32_t length = 0;
                pp_presumed(pp, location, &name, &length);
                snprintf(path->file, sizeof(path->file), "%.*s", (int)length, name != 0 ? name : "");
            }
        }
    }
    else
    {
        check_corpus_location(path, view, pp);
    }
    if (ready && !path->parsed && !path->resource)
    {
        // Diagnostic-only recovery retains the original parse rejection and uses only whitelisted reports.
        check_corpus_repair(path, parser, pp, scalar);
    }
    if (ready && !path->parsed && !path->resource && parser->tokens.pragma_count != 0)
    {
        // Unknown-state directives do not depend on the validity of a surrounding unfinished expression.
        // Their original insertion point can precede the parse lookahead while lying beyond every safe tree.
        path->original_pragmas = BUSTER_ARENA_PUSH_ARRAY(&path->prefix_arena, CheckInput, 1);
        const bool retained = path->original_pragmas != NULL &&
                              check_diagnostic_original_pragmas(path->original_pragmas, parser, pp, check_diagnostic_parse_end(parser),
                                                                &path->prefix_arena, &path->scratch);
        path->resource |= !retained;
        uint32_t token = BUSTER_CHECK_NONE;
        uint32_t source = BUSTER_CHECK_NONE;
        const bool promoted = retained && check_diagnostic_first_pragma_error_mode(path->original_pragmas, BUSTER_CHECK_NONE, &token, &source, suppress_warnings);
        if (promoted && (token < path->position || (token == path->position &&
            (strcmp(path->phase, "parse") == 0 || source < check_corpus_source(path, parser)))))
        {
            path->token = token;
            path->position = token;
            path->warning_token = token;
            path->warning_source = source;
            path->pragma = 0;
            bool invalid_pragma = false;
            for (uint32_t i = 0; i < path->original_pragmas->fp_pragma_count && path->pragma == 0; i += 1)
            {
                const CheckInputFpPragma* record = path->original_pragmas->fp_pragmas + i;
                if (check_diagnostic_pragma_source(record) == source)
                {
                    path->pragma = record->pragma + 1;
                    invalid_pragma = (record->warnings & CHECK_FP_PRAGMA_INVALID_PLACEMENT) != 0;
                }
            }
            path->phase = "promoted-warning";
            snprintf(path->message, sizeof(path->message), "%s", invalid_pragma ? "floating-point pragma is not permitted within an expression" :
                     check_diagnostic_message(CHECK_WARNING_UNKNOWN_PRAGMA));
            check_corpus_location(path, parser, pp);
        }
    }
    return path->parsed && path->adapted && path->programmed && path->warning_token == BUSTER_CHECK_NONE && !path->resource;
}

// --symbols (#129): for each file of the command line that the checker accepts, its object as -c writes it (the vector
// path's, selected, lowered and published as the driver does) against clang -O0 -c's of the file with the same options,
// which must define the same functions and objects (c_lowering_symbols.c). A file whose emission the backend refuses
// with a named feature, as `-c` would ("unsupported: ..."), is counted, not compared, as the c-testsuite harness counts
// it. Any other emission failure, a clang failure or an object that can't be read or written fails the run. The
// command sets check_corpus_symbols around its files alone: the semantic and random campaigns compare no objects.
//
// clang -O0 decides three things in its code that this compiler's selection decides otherwise, and a difference one
// gives is counted under its reason, each found from the checked program, never from the names alone; any other
// difference fails the run:
// - folded: an object only ours defines, a const and not volatile scalar with an initializer that a selected body
//   refers to. clang -O0 reads such an object's value as a constant (CodeGenFunction::tryEmitAsConstant) and so
//   doesn't need it; the checker's code reads it.
// - unselected: a definition only clang's object has, which the selection reaches when the operands of &&, || and ?:
//   that a constant condition never evaluates count too (CheckProgramOptions.unselected_operands). clang -O0 leaves a
//   constant condition's dead arm in a branch's condition (CodeGenFunction::EmitBranchOnBoolExpr); the selection
//   counts only what may run.
// - dropped: a definition only ours has, which the selection of clang's model doesn't reach: the program with
//   unselected operands counted and a constant if's dead arm left out (CheckProgramOptions.folded_ifs). clang -O0
//   doesn't emit the arm an integer constant condition never runs (CodeGenFunction::EmitIfStmt), and so leaves out
//   what only it calls, as of musl's FORCE_EVAL; Tier 0 lowers it, and the selection counts it.
// - inlined: a local function only ours has, whose definition has always_inline: clang -O0 inlines it at its calls
//   (its always-inliner) and emits none of a local one it inlined everywhere, as of SQLite's SQLITE_INLINE
//   allocateSpace; Tier 0 calls it.
typedef enum CheckCorpusSymbolReason
{
    CHECK_CORPUS_SYMBOL_FOLDED,
    CHECK_CORPUS_SYMBOL_UNSELECTED,
    CHECK_CORPUS_SYMBOL_DROPPED,
    CHECK_CORPUS_SYMBOL_INLINED,
    CHECK_CORPUS_SYMBOL_REASONS,
} CheckCorpusSymbolReason;

static const char* const check_corpus_symbol_reasons[CHECK_CORPUS_SYMBOL_REASONS] = { "folded", "unselected", "dropped", "inlined" };

typedef struct CheckCorpusSymbols CheckCorpusSymbols;
struct CheckCorpusSymbols
{
    const char* clang;
    const char* const* arguments;
    uint32_t argument_count;
    // The harness's directory, for the two objects of the file being compared.
    const char* directory;
    // Files compared, and those with an unexplained difference; differences explained by each reason, and not.
    uint64_t compared;
    uint64_t different;
    uint64_t documented[CHECK_CORPUS_SYMBOL_REASONS];
    uint64_t unexplained;
    uint64_t unsupported;
    uint64_t failures;
    uint64_t reports;
    uint64_t report_limit;
    // The features the backend named for the files it refused, each with how many: the emitter's reason strings, which
    // are static, by pointer, and "static data" for check_data_publish's refusals.
    const char* features[32];
    uint64_t feature_counts[32];
    uint32_t feature_count;
};

static CheckCorpusSymbols* check_corpus_symbols;

static void check_corpus_symbols_feature(CheckCorpusSymbols* symbols, const char* feature)
{
    uint32_t i = 0;
    while (i < symbols->feature_count && symbols->features[i] != feature && strcmp(symbols->features[i], feature) != 0)
    {
        i += 1;
    }
    if (i == symbols->feature_count && i < BUSTER_ARRAY_LENGTH(symbols->features))
    {
        symbols->features[symbols->feature_count++] = feature;
    }
    if (i < symbols->feature_count)
    {
        symbols->feature_counts[i] += 1;
    }
}

// clang -O0 -c of `file` with the oracle's arguments into `object`, its output and errors discarded: whether it succeeded.
static bool check_corpus_clang_object(const CheckCorpusSymbols* symbols, const char* file, const char* object)
{
    char** argv = calloc((size_t)symbols->argument_count + 9, sizeof(char*));
    char** environment = pp_test_clang_environment();
    bool succeeded = argv != 0 && environment != 0;
    if (succeeded)
    {
        uint32_t used = 0;
        argv[used++] = (char*)symbols->clang;
        for (uint32_t i = 0; i < symbols->argument_count; i += 1)
        {
            argv[used++] = (char*)symbols->arguments[i];
        }
        argv[used++] = "-O0";
        argv[used++] = "-c";
        argv[used++] = "-w";
        argv[used++] = "-o";
        argv[used++] = (char*)object;
        argv[used++] = (char*)file;
        argv[used] = 0;
        // Through the oracle cache (oracle_cache.c, #542), without CPATH and C_INCLUDE_PATH.
        const char* files[] = { object };
        OracleRun run = {
            .purpose = "checker corpus: clang -O0 -c",
            .arguments = argv,
            .environment = environment,
            .files = files,
            .file_count = 1,
            .dependencies = true,
        };
        succeeded = oracle_run(&run) && WIFEXITED(run.status) && WEXITSTATUS(run.status) == 0;
        oracle_run_release(&run);
    }
    free(argv);
    free(environment);
    return succeeded;
}

// Whether the emitted name of the definition at `declaration` (its asm label, or its identifier) is `name`, which
// can't be a made name's (LoweringNames).
static bool check_corpus_symbol_named(const CheckInput* input, const Parser* parser, const InternTable* identifiers, uint32_t declaration,
                                      const LoweringSymbol* name)
{
    String8 spelling = { .pointer = (char*)intern_bytes(identifiers, parser->tokens.names[declaration]),
                         .length = intern_length(identifiers, parser->tokens.names[declaration]) };
    const CheckInputAttributes* attributes = input->attributes[declaration];
    for (uint32_t i = 0; attributes != 0 && i < attributes->count; i += 1)
    {
        if (attributes->records[i].kind == CHECK_ATTRIBUTE_ASM_LABEL && !attributes->records[i].ignored)
        {
            spelling = attributes->records[i].text;
        }
    }
    return !name->uniqued && spelling.length == name->length && memcmp(spelling.pointer, name->name, name->length) == 0;
}

// The reason a difference of an accepted file's comparison has, or CHECK_CORPUS_SYMBOL_REASONS for none: `selected`
// and `code` are what -c emitted, `reached` what the selection reaches with unselected operands counted, `kept` what
// it reaches in clang's model, with them and folded ifs, and `names` the source's names, with which the comparison
// read both objects.
static CheckCorpusSymbolReason check_corpus_symbol_reason(const LoweringDifference* difference, const CheckCorpusPath* path, const Parser* parser,
                                                          const InternTable* identifiers, const CheckProgramSelection* selected,
                                                          const CheckProgramCode* code, const CheckProgramSelection* reached,
                                                          const CheckProgramSelection* kept, const LoweringNames* names)
{
    CheckCorpusSymbolReason reason = CHECK_CORPUS_SYMBOL_REASONS;
    const CheckInput* input = &path->input;
    const TypeTable* types = &path->types;
    const LoweringSymbol* name = difference->symbol;
    for (SymbolId symbol = 1; difference->first && reason == CHECK_CORPUS_SYMBOL_REASONS && symbol <= selected->program.symbol_count; symbol += 1)
    {
        const NameId emitted = code->link.symbols.name[symbol];
        // The emitted name as lowering_symbols_read compares it.
        bool uniqued = false;
        const uint8_t* spelling = code->link.names.bytes[emitted];
        const uint32_t length = lowering_symbols_compared(names, spelling, code->link.names.length[emitted], code->link.symbols.flags[symbol] & BUSTER_LINK_SYMBOL_BINDING,
                                                          &uniqued);
        const uint32_t object = selected->program.symbol_objects[symbol - 1];
        const uint32_t declaration = selected->program.object_declarations[object];
        if (length == name->length && uniqued == name->uniqued && memcmp(spelling, name->name, name->length) == 0 && declaration < input->token_count &&
            input->declaration_kinds[declaration] == C_BINDING_OBJECT)
        {
            const TypeId type = type_strip_aligned(types, input->declared_types[declaration]);
            const bool constant = (type_qualifiers(type) & (BUSTER_TYPE_CONST | BUSTER_TYPE_VOLATILE)) == BUSTER_TYPE_CONST &&
                                  (type_is_arithmetic(types, type) || type_kind(types, type) == BUSTER_TYPE_KIND_POINTER ||
                                   type_kind(types, type) == BUSTER_TYPE_KIND_ENUM);
            bool initialized = false;
            for (uint32_t i = 0; constant && i < selected->data.count; i += 1)
            {
                initialized |= selected->data.objects[i].object == object && selected->data.objects[i].initializer != BUSTER_CHECK_NONE;
            }
            bool read = false;
            for (uint32_t i = 0; initialized && i < selected->program.reference_count; i += 1)
            {
                read |= selected->program.references[i] == object;
            }
            reason = read ? CHECK_CORPUS_SYMBOL_FOLDED : reason;
        }
    }
    for (uint32_t body = 0; !difference->first && reason == CHECK_CORPUS_SYMBOL_REASONS && body < reached->program.body_count; body += 1)
    {
        const CheckProgramBody* definition = reached->program.bodies + body;
        bool ours = false;
        for (uint32_t i = 0; i < selected->program.body_count; i += 1)
        {
            ours |= selected->program.bodies[i].object == definition->object;
        }
        reason = !ours && check_corpus_symbol_named(input, parser, identifiers, definition->declaration, name) ? CHECK_CORPUS_SYMBOL_UNSELECTED : reason;
    }
    for (uint32_t i = 0; !difference->first && reason == CHECK_CORPUS_SYMBOL_REASONS && i < reached->data.count; i += 1)
    {
        const uint32_t object = reached->data.objects[i].object;
        const uint32_t declaration = reached->program.object_declarations[object];
        bool ours = false;
        for (uint32_t j = 0; j < selected->data.count; j += 1)
        {
            ours |= selected->data.objects[j].object == object;
        }
        reason = !ours && declaration < input->token_count && check_corpus_symbol_named(input, parser, identifiers, declaration, name) ?
                 CHECK_CORPUS_SYMBOL_UNSELECTED : reason;
    }
    for (uint32_t body = 0; difference->first && reason == CHECK_CORPUS_SYMBOL_REASONS && body < selected->program.body_count; body += 1)
    {
        const CheckProgramBody* definition = selected->program.bodies + body;
        bool theirs = false;
        for (uint32_t i = 0; i < kept->program.body_count; i += 1)
        {
            theirs |= kept->program.bodies[i].object == definition->object;
        }
        reason = !theirs && check_corpus_symbol_named(input, parser, identifiers, definition->declaration, name) ? CHECK_CORPUS_SYMBOL_DROPPED : reason;
    }
    for (uint32_t i = 0; difference->first && reason == CHECK_CORPUS_SYMBOL_REASONS && i < selected->data.count; i += 1)
    {
        const uint32_t object = selected->data.objects[i].object;
        const uint32_t declaration = selected->program.object_declarations[object];
        bool theirs = false;
        for (uint32_t j = 0; j < kept->data.count; j += 1)
        {
            theirs |= kept->data.objects[j].object == object;
        }
        reason = !theirs && declaration < input->token_count && check_corpus_symbol_named(input, parser, identifiers, declaration, name) ?
                 CHECK_CORPUS_SYMBOL_DROPPED : reason;
    }
    const bool local_function = name->binding == BUSTER_LINK_SYMBOL_LOCAL && name->type == BUSTER_LINK_SYMBOL_FUNCTION;
    for (uint32_t body = 0; difference->first && local_function && reason == CHECK_CORPUS_SYMBOL_REASONS && body < selected->program.body_count; body += 1)
    {
        const CheckProgramBody* definition = selected->program.bodies + body;
        const CheckInputAttributes* attributes = input->attributes[definition->declaration];
        bool always_inline = false;
        for (uint32_t i = 0; attributes != 0 && i < attributes->count; i += 1)
        {
            always_inline |= attributes->records[i].kind == CHECK_ATTRIBUTE_ALWAYS_INLINE && !attributes->records[i].ignored;
        }
        reason = always_inline && check_corpus_symbol_named(input, parser, identifiers, definition->declaration, name) ? CHECK_CORPUS_SYMBOL_INLINED : reason;
    }
    return reason;
}

// --symbols' comparison of an accepted file, whose vector path is `path` and parser `parser`, tallied in `symbols`.
static void check_corpus_symbols_file(CheckCorpusSymbols* symbols, const char* file, CheckCorpusPath* path, const Parser* parser, Preprocessor* pp)
{
    const uint64_t output_mark = path->output_arena.length;
    const uint64_t scratch_mark = path->scratch.length;
    Arena output = arena_reserve(UINT64_C(1) << 34);
    Arena bytes = arena_reserve(UINT64_C(1) << 34);
    CheckDataPrepared data = { 0 };
    CheckProgram program = { 0 };
    CheckProgram unselected = { 0 };
    CheckProgram model = { 0 };
    CheckProgramSelection selected = { 0 };
    CheckProgramSelection reached = { 0 };
    CheckProgramSelection kept = { 0 };
    CheckProgramCode code = { .status = CHECK_PROGRAM_RESOURCE, .token = BUSTER_CHECK_NONE };
    CheckDataResult emission = { .status = CHECK_DATA_RESOURCE };
    bool valid = output.base != 0 && bytes.base != 0 &&
                 check_data_prepare(&data, &path->input, &path->stream, &path->initializers, &path->types, parser->options.scalar, &path->output_arena, &path->scratch);
    CheckProgramOptions options = {
        .initializers = &path->initializers,
        .asms = &path->asms,
        .identifiers = pp->table,
        .initializer_references = data.references,
        .initializer_reference_count = data.reference_count,
    };
    valid = valid && check_program(&program, parser, &path->input, &path->stream, &path->types, options, &path->output_arena, &path->scratch) &&
            check_program_select(&selected, &program, &data, false, &path->output_arena, &path->scratch);
    options.unselected_operands = true;
    valid = valid && check_program(&unselected, parser, &path->input, &path->stream, &path->types, options, &path->output_arena, &path->scratch) &&
            check_program_select(&reached, &unselected, &data, false, &path->output_arena, &path->scratch);
    options.folded_ifs = true;
    valid = valid && check_program(&model, parser, &path->input, &path->stream, &path->types, options, &path->output_arena, &path->scratch) &&
            check_program_select(&kept, &model, &data, false, &path->output_arena, &path->scratch);
    const bool lowered = valid && check_program_emit(&code, &selected.program, parser, pp->table, selected.data.capacity, false, &output, &bytes,
                                                     &path->scratch, NULL, NULL, true, true);
    const bool published = lowered && check_data_publish(&selected.data, &path->types, selected.program.stream, selected.program.object_symbols,
                                                         selected.program.object_count, &code.link, 0, &bytes, &path->scratch, &emission);
    const bool unsupported = valid && ((!lowered && code.status == CHECK_PROGRAM_UNSUPPORTED) || (lowered && !published && emission.status == CHECK_DATA_UNSUPPORTED));
    char ours_path[PATH_MAX];
    char theirs_path[PATH_MAX];
    snprintf(ours_path, sizeof(ours_path), "%s/symbols-ours.o", symbols->directory);
    snprintf(theirs_path, sizeof(theirs_path), "%s/symbols-clang.o", symbols->directory);
    bool compared = false;
    uint32_t unexplained = 0;
    char report[1024] = { 0 };
    uint64_t written_report = 0;
    const char* failure = !valid ? "the checked program couldn't be selected" : !lowered && !unsupported ? "the backend failed" :
                          lowered && !published && !unsupported ? "its static data couldn't be published" : NULL;
    if (published)
    {
        ObjectUnits units = { 0 };
        ObjectSymbols object_symbols = { 0 };
        bool written = link_check(&code.link) == LINK_ERROR_NONE && object_prepare(&units, &code.link, 1, &path->scratch) &&
                       object_map_symbols(&object_symbols, &code.link, &path->scratch);
        if (written)
        {
            const Object object = object_build(&code.link, &units, &object_symbols, 0, (ObjectOptions){ 0 }, (String8){ .pointer = (char*)file, .length = strlen(file) },
                                               &path->scratch);
            written = object.error == OBJECT_ERROR_NONE && object_write_file(ours_path, object.bytes, object.size) == OBJECT_ERROR_NONE;
        }
        LoweringSymbols ours = { 0 };
        LoweringSymbols theirs = { 0 };
        LoweringNames names = { 0 };
        const bool oracle = written && check_corpus_clang_object(symbols, file, theirs_path);
        const bool named = oracle && lowering_names_of_input(&names, &path->input, parser, pp->table, &path->scratch);
        compared = named && lowering_symbols_read_file(&ours, ours_path, &names, &path->scratch) &&
                   lowering_symbols_read_file(&theirs, theirs_path, &names, &path->scratch);
        failure = !written ? "its object couldn't be written" : !oracle ? "clang -O0 -c failed" : !named ? "out of memory for the source's names" :
                  !compared ? "an object couldn't be read" : NULL;
        const uint32_t count = compared ? lowering_symbols_compare(&ours, "ours", &theirs, "clang -O0 -c", NULL, 0, NULL, 0) : 0;
        LoweringDifference* differences = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(&path->scratch, LoweringDifference, count) : NULL;
        failure = count != 0 && differences == NULL ? "out of memory for its differences" : failure;
        compared &= count == 0 || differences != NULL;
        if (compared && count != 0)
        {
            lowering_symbols_compare(&ours, "ours", &theirs, "clang -O0 -c", differences, count, NULL, 0);
        }
        for (uint32_t i = 0; compared && i < count; i += 1)
        {
            const CheckCorpusSymbolReason reason = check_corpus_symbol_reason(differences + i, path, parser, pp->table, &selected, &code, &reached, &kept, &names);
            if (reason == CHECK_CORPUS_SYMBOL_REASONS)
            {
                unexplained += 1;
                const LoweringSymbol* only = differences[i].symbol;
                const int length = written_report + 1 < sizeof(report) ?
                                   snprintf(report + written_report, sizeof(report) - written_report, "%s only: %s %s %.*s\n",
                                            differences[i].first ? "ours" : "clang -O0 -c", lowering_symbols_type_name(only->type),
                                            lowering_symbols_binding_name(only->binding), (int)only->spelled, (const char*)only->name) : 0;
                written_report += length > 0 ? (uint64_t)length : 0;
                written_report = written_report < sizeof(report) ? written_report : sizeof(report) - 1;
            }
            else
            {
                symbols->documented[reason] += 1;
            }
        }
        unlink(ours_path);
        unlink(theirs_path);
    }
    if (unsupported)
    {
        check_corpus_symbols_feature(symbols, !lowered ? (code.reason != NULL ? code.reason : "this C type or operation") : "static data");
    }
    symbols->compared += compared;
    symbols->different += compared && unexplained != 0;
    symbols->unexplained += unexplained;
    symbols->unsupported += unsupported;
    symbols->failures += failure != NULL;
    if ((failure != NULL || unexplained != 0) && symbols->reports < symbols->report_limit)
    {
        fprintf(stderr, "%s: symbols: %s%s%s", file, failure != NULL ? failure : "", failure != NULL ? "\n" : "", report);
        symbols->reports += 1;
    }
    arena_release(&bytes);
    arena_release(&output);
    arena_pop_to(&path->scratch, scratch_mark);
    arena_pop_to(&path->output_arena, output_mark);
}

static bool check_corpus_check(CheckCorpusPath* path, Parser* parser, Preprocessor* pp, bool scalar)
{
    return check_corpus_check_policy(path, parser, pp, scalar, false);
}

static bool check_corpus_types_equal(const TypeTable* x, const TypeTable* y)
{
    bool equal = x->types.count == y->types.count && x->parameters.count == y->parameters.count && x->tags.count == y->tags.count &&
                 x->tags.completions == y->tags.completions && x->members.count == y->members.count && x->last_error == y->last_error;
    for (uint32_t i = 1; equal && i < x->types.count; i += 1)
    {
        equal = x->types.kind[i] == y->types.kind[i] && x->types.code[i] == y->types.code[i] && x->types.flags[i] == y->types.flags[i] &&
                x->types.alignment_log2[i] == y->types.alignment_log2[i] && x->types.base[i] == y->types.base[i] && x->types.index[i] == y->types.index[i] &&
                x->types.extent[i] == y->types.extent[i] && x->types.size[i] == y->types.size[i];
    }
    for (uint32_t i = 0; equal && i < x->parameters.count; i += 1)
    {
        equal = x->parameters.type[i] == y->parameters.type[i];
    }
    for (uint32_t i = 1; equal && i < x->tags.count; i += 1)
    {
        equal = x->tags.type[i] == y->tags.type[i] && x->tags.name[i] == y->tags.name[i] && x->tags.member_begin[i] == y->tags.member_begin[i] &&
                x->tags.member_count[i] == y->tags.member_count[i] && x->tags.completion[i] == y->tags.completion[i] &&
                x->tags.packed[i] == y->tags.packed[i] && x->tags.pack_log2[i] == y->tags.pack_log2[i] && x->tags.alignment_log2[i] == y->tags.alignment_log2[i];
    }
    for (uint32_t i = 1; equal && i < x->members.count; i += 1)
    {
        equal = x->members.name[i] == y->members.name[i] && x->members.type[i] == y->members.type[i] &&
                x->members.bit_width[i] == y->members.bit_width[i] && x->members.alignment_log2[i] == y->members.alignment_log2[i] &&
                x->members.flags[i] == y->members.flags[i] && x->members.offset[i] == y->members.offset[i];
    }
    return equal;
}

static bool check_corpus_initializers_equal(const CheckInitializers* x, const CheckInitializers* y)
{
    bool equal = x->status == y->status && x->token == y->token && x->count == y->count && x->token_count == y->token_count &&
                 x->warning_count == y->warning_count;
    for (uint32_t warning = 0; equal && warning < x->warning_count; warning += 1)
    {
        equal = x->warning_tokens[warning] == y->warning_tokens[warning];
    }
    for (uint32_t token = 0; equal && token < x->token_count; token += 1)
    {
        equal = x->declaration_plans[token] == y->declaration_plans[token];
    }
    for (uint32_t plan = 0; equal && plan < x->count; plan += 1)
    {
        const CheckInitializer* a = x->plans + plan;
        const CheckInitializer* b = y->plans + plan;
        equal = x->declaration_tokens[plan] == y->declaration_tokens[plan] && a->type == b->type && a->count == b->count &&
                a->status == b->status && a->failed_token == b->failed_token && a->object_size == b->object_size &&
                a->warning_count == b->warning_count && a->warning_token == b->warning_token;
        for (uint32_t part = 0; equal && part < a->count; part += 1)
        {
            const CheckInitPart* p = a->parts + part;
            const CheckInitPart* q = b->parts + part;
            equal = p->kind == q->kind && p->type == q->type && p->node == q->node && p->token == q->token && p->bit_offset == q->bit_offset &&
                    p->bit_width == q->bit_width && p->byte_count == q->byte_count && p->overload_candidate == q->overload_candidate &&
                    p->conversion.type == q->conversion.type &&
                    p->conversion.flags == q->conversion.flags && check_test_value_equal(&p->value, &q->value) &&
                    (p->kind != CHECK_INIT_BYTES || p->byte_count == 0 || memcmp(p->bytes, q->bytes, p->byte_count) == 0);
        }
    }
    return equal;
}

static bool check_corpus_attributes_equal(const CheckInputAttributes* x, const CheckInputAttributes* y)
{
    bool equal = (x == 0) == (y == 0);
    if (equal && x != 0)
    {
        equal = x->count == y->count && x->error_first == y->error_first && x->error_end == y->error_end && x->error == y->error &&
                x->alignment_log2 == y->alignment_log2 && x->required_alignment_log2 == y->required_alignment_log2 &&
                x->calling_convention == y->calling_convention && x->packed == y->packed && x->has_alignas == y->has_alignas;
        for (uint32_t i = 0; equal && i < x->count; i += 1)
        {
            const CheckInputAttribute* a = x->records + i;
            const CheckInputAttribute* b = y->records + i;
            equal = a->first == b->first && a->end == b->end && a->name == b->name && a->name_token == b->name_token &&
                    a->argument_first == b->argument_first && a->argument_end == b->argument_end && a->argument_count == b->argument_count &&
                    a->kind == b->kind && a->schema == b->schema && a->value == b->value && a->resolved == b->resolved &&
                    a->standard == b->standard && a->ignored == b->ignored && a->text.length == b->text.length &&
                    (a->argument_values == 0) == (b->argument_values == 0) &&
                    (a->text.length == 0 || memcmp(a->text.pointer, b->text.pointer, a->text.length) == 0);
            for (uint32_t argument = 0; equal && a->argument_values != 0 && argument < a->argument_count; argument += 1)
            {
                equal = a->argument_values[argument] == b->argument_values[argument];
            }
        }
    }
    return equal;
}

static bool check_corpus_programs_equal(const CheckProgram* a, const CheckProgram* b)
{
    bool equal = a->status == b->status && a->token == b->token && a->diagnostic_position == b->diagnostic_position &&
                 a->control_count == b->control_count && a->body_count == b->body_count &&
                 a->case_count == b->case_count && a->region_count == b->region_count && a->reference_count == b->reference_count &&
                 a->object_count == b->object_count && a->symbol_count == b->symbol_count && a->builtin_count == b->builtin_count &&
                 a->asm_target_count == b->asm_target_count && a->output_bytes == b->output_bytes && (a->asm_offsets == NULL) == (b->asm_offsets == NULL);
    for (uint32_t i = 0; equal && i < a->control_count; i += 1)
    {
        const CheckControl* x = a->controls + i;
        const CheckControl* y = b->controls + i;
        equal = x->node == y->node && x->token == y->token && x->target == y->target && x->kind == y->kind;
    }
    for (uint32_t i = 0; equal && i < a->body_count; i += 1)
    {
        const CheckProgramBody* x = a->bodies + i;
        const CheckProgramBody* y = b->bodies + i;
        equal = x->declaration == y->declaration && x->opening == y->opening && x->object == y->object && x->first == y->first && x->count == y->count &&
                x->entry_count == y->entry_count && x->reference_first == y->reference_first && x->reference_count == y->reference_count && x->is_main == y->is_main;
    }
    for (uint32_t i = 0; equal && i < a->case_count; i += 1)
    {
        const CheckProgramCase* x = a->cases + i;
        const CheckProgramCase* y = b->cases + i;
        equal = x->lower == y->lower && x->upper == y->upper && x->type == y->type && x->token == y->token && x->owner == y->owner &&
                x->target == y->target && x->fallback == y->fallback && x->range == y->range;
    }
    for (uint32_t i = 0; equal && i < a->reference_count; i += 1)
    {
        equal = a->references[i] == b->references[i];
    }
    for (uint32_t i = 0; equal && i < a->region_count; i += 1)
    {
        const CheckProgramRegion* x = a->regions + i;
        const CheckProgramRegion* y = b->regions + i;
        equal = x->first == y->first && x->count == y->count && x->body == y->body && x->node == y->node && x->result == y->result;
    }
    for (uint32_t i = 0; equal && i < a->object_count; i += 1)
    {
        equal = a->object_symbols[i] == b->object_symbols[i] && a->object_declarations[i] == b->object_declarations[i];
    }
    for (uint32_t i = 0; equal && i < a->symbol_count; i += 1)
    {
        equal = a->symbol_objects[i] == b->symbol_objects[i];
    }
    for (uint32_t i = 0; equal && i < a->builtin_count; i += 1)
    {
        equal = a->builtin_nodes[i] == b->builtin_nodes[i];
    }
    for (uint32_t i = 0; equal && a->asm_offsets != NULL && i <= a->input->asm_count; i += 1)
    {
        equal = a->asm_offsets[i] == b->asm_offsets[i];
    }
    for (uint32_t i = 0; equal && i < a->asm_target_count; i += 1)
    {
        equal = a->asm_targets[i] == b->asm_targets[i];
    }
    return equal;
}

static bool check_corpus_pragmas_equal(const CheckInput* a, const CheckInput* b)
{
    const CheckInputDiagnosticPragmas* x = a->diagnostic_pragmas;
    const CheckInputDiagnosticPragmas* y = b->diagnostic_pragmas;
    const CheckInputNonnullPragmas* nonnull_a = a->nonnull_pragmas;
    const CheckInputNonnullPragmas* nonnull_b = b->nonnull_pragmas;
    const CheckInputPackPragmas* pack_a = a->pack_pragmas;
    const CheckInputPackPragmas* pack_b = b->pack_pragmas;
    const bool contents = !a->resource && !b->resource;
    bool equal = (x == NULL) == (y == NULL) && a->fp_pragma_count == b->fp_pragma_count &&
                 (a->fp_environments == NULL) == (b->fp_environments == NULL) &&
                 (a->cx_limited_ranges == NULL) == (b->cx_limited_ranges == NULL) && (nonnull_a == NULL) == (nonnull_b == NULL) &&
                 (a->pack_log2 == NULL) == (b->pack_log2 == NULL) && (pack_a == NULL) == (pack_b == NULL);
    if (equal && contents && x != NULL)
    {
        equal = x->count == y->count && x->final_state == y->final_state && x->maximum_depth == y->maximum_depth &&
                x->unmatched_pushes == y->unmatched_pushes && x->unmatched_pops == y->unmatched_pops;
        for (uint32_t i = 0; equal && i < x->count; i += 1)
        {
            const CheckInputDiagnosticPragma* p = x->records + i;
            const CheckInputDiagnosticPragma* q = y->records + i;
            equal = p->option.length == q->option.length && p->pragma == q->pragma && p->source_begin == q->source_begin && p->source_end == q->source_end &&
                    p->token == q->token && p->state_before == q->state_before && p->state_after == q->state_after && p->matching_push == q->matching_push &&
                    p->dialect == q->dialect && p->operation == q->operation &&
                    (p->option.length == 0 || memcmp(p->option.pointer, q->option.pointer, p->option.length) == 0);
        }
    }
    if (equal && contents && nonnull_a != NULL)
    {
        equal = nonnull_a->count == nonnull_b->count && nonnull_a->active == nonnull_b->active && nonnull_a->error_count == nonnull_b->error_count;
        for (uint32_t i = 0; equal && i < nonnull_a->count; i += 1)
        {
            const CheckInputNonnullPragma* p = nonnull_a->records + i;
            const CheckInputNonnullPragma* q = nonnull_b->records + i;
            equal = p->pragma == q->pragma && p->source_begin == q->source_begin && p->source_end == q->source_end && p->token == q->token &&
                    p->active_before == q->active_before && p->active_after == q->active_after && p->matching_begin == q->matching_begin &&
                    p->begin == q->begin && p->invalid == q->invalid;
        }
    }
    if (equal && contents && pack_a != NULL)
    {
        equal = pack_a->count == pack_b->count && pack_a->maximum_depth == pack_b->maximum_depth &&
                pack_a->unmatched_pushes == pack_b->unmatched_pushes && pack_a->final_log2 == pack_b->final_log2;
        for (uint32_t i = 0; equal && i < pack_a->count; i += 1)
        {
            const CheckInputPackPragma* p = pack_a->records + i;
            const CheckInputPackPragma* q = pack_b->records + i;
            equal = p->pragma == q->pragma && p->source_begin == q->source_begin && p->source_end == q->source_end && p->token == q->token &&
                    p->diagnostic_token == q->diagnostic_token && p->alignment_source == q->alignment_source &&
                    p->name == q->name && p->matching_push == q->matching_push &&
                    p->previous_same == q->previous_same && p->depth_before == q->depth_before && p->depth_after == q->depth_after &&
                    p->operation == q->operation && p->alignment_log2 == q->alignment_log2 && p->pack_before == q->pack_before &&
                    p->pack_after == q->pack_after && p->warnings == q->warnings && p->handled == q->handled && p->has_alignment == q->has_alignment;
        }
    }
    for (uint32_t i = 0; equal && contents && i < a->fp_pragma_count; i += 1)
    {
        const CheckInputFpPragma* p = a->fp_pragmas + i;
        const CheckInputFpPragma* q = b->fp_pragmas + i;
        equal = p->pragma == q->pragma && p->source_begin == q->source_begin && p->source_end == q->source_end && p->token == q->token &&
                p->scope == q->scope && p->scope_end == q->scope_end && p->kind == q->kind && p->environment == q->environment &&
                p->warnings == q->warnings && p->valid == q->valid;
    }
    for (uint32_t token = 0; equal && contents && a->fp_environments != NULL && token <= a->token_count; token += 1)
    {
        equal = a->fp_environments[token] == b->fp_environments[token];
    }
    for (uint32_t token = 0; equal && contents && a->cx_limited_ranges != NULL && token <= a->token_count; token += 1)
    {
        equal = a->cx_limited_ranges[token] == b->cx_limited_ranges[token];
    }
    for (uint32_t token = 0; equal && contents && a->pack_log2 != NULL && token <= a->token_count; token += 1)
    {
        equal = a->pack_log2[token] == b->pack_log2[token];
    }
    return equal;
}

static bool check_corpus_environments_equal(const CheckEnvironment* a, const CheckEnvironment* b)
{
    bool equal = a->valid == b->valid && a->body_count == b->body_count && a->tag_count == b->tag_count && a->token_count == b->token_count;
    if (equal && a->valid)
    {
        const CBindings* x = a->bindings;
        const CBindings* y = b->bindings;
        equal = x->count == y->count && x->name_capacity == y->name_capacity && x->valid == y->valid &&
                a->file.names == b->file.names && a->file.keys == b->file.keys && a->file.count == b->file.count;
        for (uint32_t event = 0; equal && event < x->count; event += 1)
        {
            equal = x->operation[event] == y->operation[event] && x->declaration[event] == y->declaration[event] && x->point[event] == y->point[event];
            // Restore events deliberately leave the other producer columns unspecified.
            if (equal && x->operation[event] != C_BINDING_RESTORE)
            {
                equal = x->key[event] == y->key[event] && x->end[event] == y->end[event] &&
                        x->depth[event] == y->depth[event] && x->kind[event] == y->kind[event];
            }
        }
        for (uint32_t key = 0; equal && key <= a->file.keys; key += 1)
        {
            equal = a->file.offsets[key] == b->file.offsets[key];
        }
        for (uint32_t event = 0; equal && event < a->file.count; event += 1)
        {
            equal = a->file.points[event] == b->file.points[event] && a->file.handles[event] == b->file.handles[event];
        }
        for (uint32_t tag = 0; equal && tag < a->tag_count; tag += 1)
        {
            equal = a->tag_completions[tag] == b->tag_completions[tag];
        }
        for (uint32_t body = 0; equal && body < a->body_count; body += 1)
        {
            equal = a->bodies[body].begin == b->bodies[body].begin && a->bodies[body].end == b->bodies[body].end;
        }
        for (uint32_t token = 0; equal && token < a->token_count; token += 1)
        {
            equal = a->declaration_types[token] == b->declaration_types[token];
        }
    }
    return equal;
}

static bool check_corpus_view_equal(const CheckCorpusPath* a, const CheckCorpusPath* b, const Parser* x, const Parser* y)
{
    bool equal = a->parsed == b->parsed && a->adapted == b->adapted && a->checked == b->checked && a->initialized == b->initialized &&
                 a->programmed == b->programmed && a->resource == b->resource &&
                 a->suppress_warnings == b->suppress_warnings && a->warning_token == b->warning_token && a->warning_source == b->warning_source &&
                 a->line == b->line && a->column == b->column && a->token == b->token && a->position == b->position && a->pragma == b->pragma &&
                 x->tokens.count == y->tokens.count &&
                 x->errors == y->errors && x->fatal == y->fatal && x->diagnostic_count == y->diagnostic_count &&
                 x->tokens.pragma_count == y->tokens.pragma_count && a->stream.status == b->stream.status &&
                 a->stream.failed_node == b->stream.failed_node && (a->prefix == 0) == (b->prefix == 0) &&
                 a->asm_diagnostic.status == b->asm_diagnostic.status && a->asm_diagnostic.token == b->asm_diagnostic.token &&
                 a->initializer_diagnostic.status == b->initializer_diagnostic.status && a->initializer_diagnostic.token == b->initializer_diagnostic.token &&
                 a->initializer_diagnostic.warning_count == b->initializer_diagnostic.warning_count &&
                 a->initializer_diagnostic.warning_token == b->initializer_diagnostic.warning_token &&
                 a->initializer_warnings.count == b->initializer_warnings.count &&
                 a->program_diagnostic.status == b->program_diagnostic.status && a->program_diagnostic.token == b->program_diagnostic.token &&
                 a->program_diagnostic.position == b->program_diagnostic.position;
    equal &= a->input.packed_member_count == b->input.packed_member_count;
    for (uint32_t i = 0; equal && i < a->input.packed_member_count; i += 1)
    {
        const CheckInputPackedMember* left = a->input.packed_members + i;
        const CheckInputPackedMember* right = b->input.packed_members + i;
        equal = left->address == right->address && left->token == right->token && left->member == right->member && left->record == right->record &&
                left->alignment_log2 == right->alignment_log2 && left->implicit_conversions == right->implicit_conversions && left->suppressed == right->suppressed &&
                left->member_name.length == right->member_name.length && left->record_name.length == right->record_name.length &&
                (left->member_name.length == 0 || memcmp(left->member_name.pointer, right->member_name.pointer, left->member_name.length) == 0) &&
                (left->record_name.length == 0 || memcmp(left->record_name.pointer, right->record_name.pointer, left->record_name.length) == 0);
    }
    for (uint32_t warning = 0; equal && warning < a->initializer_warnings.count; warning += 1)
    {
        equal = a->initializer_warnings.tokens[warning] == b->initializer_warnings.tokens[warning];
    }
    for (uint32_t report = 0; equal && report < x->diagnostic_count; report += 1)
    {
        const ParseDiagnostic* left = x->diagnostics + report;
        const ParseDiagnostic* right = y->diagnostics + report;
        equal = left->token == right->token && left->argument == right->argument && left->message == right->message &&
                left->place == right->place && left->error == right->error;
    }
    if (equal && a->prefix != 0)
    {
        equal = a->prefix->status == b->prefix->status && a->prefix->end == b->prefix->end && a->prefix->diagnostic == b->prefix->diagnostic;
    }
    const bool prefix = a->prefix != 0 && a->prefix->status == CHECK_PREFIX_READY;
    const bool trees = a->parsed || prefix;
    const Parser* left_parser = prefix ? &a->prefix->parser : x;
    const Parser* right_parser = prefix && b->prefix != 0 ? &b->prefix->parser : y;
    equal = equal && left_parser->tokens.count == right_parser->tokens.count && left_parser->tokens.pragma_count == right_parser->tokens.pragma_count;
    for (uint32_t token = 0; equal && trees && token < left_parser->tokens.count; token += 1)
    {
        equal = parse_test_canonical_role(left_parser->tree.roles[token]) == parse_test_canonical_role(right_parser->tree.roles[token]) &&
                left_parser->tree.parents[token] == right_parser->tree.parents[token] && left_parser->tree.depths[token] == right_parser->tree.depths[token] &&
                left_parser->postorder[token] == right_parser->postorder[token] && (left_parser->sizes != 0) == (right_parser->sizes != 0) &&
                (left_parser->sizes == 0 || left_parser->sizes[token] == right_parser->sizes[token]) &&
                (left_parser->positions != 0) == (right_parser->positions != 0) &&
                (left_parser->positions == 0 || (left_parser->positions[token] == right_parser->positions[token] && left_parser->ends[token] == right_parser->ends[token] &&
                                                 left_parser->first_children[token] == right_parser->first_children[token] &&
                                                 left_parser->next_siblings[token] == right_parser->next_siblings[token])) &&
                left_parser->tokens.kinds[token] == right_parser->tokens.kinds[token] && left_parser->tokens.names[token] == right_parser->tokens.names[token] &&
                left_parser->tokens.sources[token] == right_parser->tokens.sources[token] && left_parser->tokens.flags[token] == right_parser->tokens.flags[token];
    }
    for (uint32_t pragma = 0; equal && pragma < x->tokens.pragma_count; pragma += 1)
    {
        equal = x->tokens.pragma_positions[pragma] == y->tokens.pragma_positions[pragma] &&
                x->tokens.pragma_sources[pragma] == y->tokens.pragma_sources[pragma];
    }
    const CheckInput* left = &a->input;
    const CheckInput* right = &b->input;
    equal = equal && left->diagnostic_graph == right->diagnostic_graph && left->report_count == right->report_count && left->token_count == right->token_count &&
            left->string_count == right->string_count && left->region_count == right->region_count && left->vla_count == right->vla_count &&
            left->asm_count == right->asm_count && left->binding_event_count == right->binding_event_count &&
            left->parser_binding_event_count == right->parser_binding_event_count &&
            left->intrinsic_declaration_count == right->intrinsic_declaration_count && left->intrinsic_call_count == right->intrinsic_call_count &&
            (left->intrinsic_ids == NULL) == (right->intrinsic_ids == NULL) && (left->deferred_tokens == NULL) == (right->deferred_tokens == NULL) &&
            (left->overload_heads == NULL) == (right->overload_heads == NULL) && (left->explicit_abis == NULL) == (right->explicit_abis == NULL) &&
            (left->loop_diagnostic == NULL) == (right->loop_diagnostic == NULL) &&
            (left->naked_diagnostic == NULL) == (right->naked_diagnostic == NULL);
    if (equal && left->naked_diagnostic != NULL)
    {
        const CheckInputNakedDiagnostic* l = left->naked_diagnostic;
        const CheckInputNakedDiagnostic* r = right->naked_diagnostic;
        equal = l->position == r->position && l->token == r->token && l->attribute == r->attribute;
    }
    if (equal && left->loop_diagnostic != NULL)
    {
        const CheckInputLoopDiagnostic* l = left->loop_diagnostic;
        const CheckInputLoopDiagnostic* r = right->loop_diagnostic;
        equal = l->position == r->position && l->statement == r->statement && l->source == r->source && l->pragma == r->pragma &&
                l->message == r->message && strcmp(l->text, r->text) == 0;
    }
    for (uint32_t report = 0; equal && report < left->report_count; report += 1)
    {
        equal = left->reports[report].token == right->reports[report].token && left->reports[report].message == right->reports[report].message &&
                left->reports[report].pragma == right->reports[report].pragma;
    }
    for (uint32_t token = 0; equal && !a->resource && trees && left->declarations != 0 && right->declarations != 0 && token < left_parser->tokens.count; token += 1)
    {
        equal = left->declarations[token] == right->declarations[token] && left->declared_types[token] == right->declared_types[token] &&
                left->declaration_kinds[token] == right->declaration_kinds[token] && left->function_flags[token] == right->function_flags[token] &&
                left->objects[token] == right->objects[token] &&
                left->static_objects[token] == right->static_objects[token] && left->registers[token] == right->registers[token] &&
                left->thread_local[token] == right->thread_local[token] && left->compound_objects[token] == right->compound_objects[token] &&
                check_corpus_attributes_equal(left->attributes[token], right->attributes[token]) &&
                left->linkage[token] == right->linkage[token] && left->defined[token] == right->defined[token] &&
                left->tentative[token] == right->tentative[token] && left->initializer_nodes[token] == right->initializer_nodes[token] &&
                left->initializer_begins[token] == right->initializer_begins[token] && left->initializer_ends[token] == right->initializer_ends[token] &&
                left->token_nodes[token] == right->token_nodes[token] && left->body_declarations[token] == right->body_declarations[token] &&
                left->declaration_bodies[token] == right->declaration_bodies[token] && left->parameters[token] == right->parameters[token] &&
                left->parameter_next[token] == right->parameter_next[token] && left->control_owners[token] == right->control_owners[token] &&
                left->label_targets[token] == right->label_targets[token] && left->case_ranges[token] == right->case_ranges[token] &&
                left->expression_regions[token] == right->expression_regions[token] && left->asm_ids[token] == right->asm_ids[token] &&
                left->constant_warnings[token] == right->constant_warnings[token] &&
                left->constant_warning_ignored[token] == right->constant_warning_ignored[token];
    }
    for (uint32_t token = 0; equal && !a->resource && trees && token <= left->token_count; token += 1)
    {
        equal = (left->intrinsic_ids == NULL || left->intrinsic_ids[token] == right->intrinsic_ids[token]) &&
                (left->deferred_tokens == NULL || left->deferred_tokens[token] == right->deferred_tokens[token]) &&
                (left->overload_heads == NULL || left->overload_heads[token] == right->overload_heads[token]) &&
                (left->explicit_abis == NULL || left->explicit_abis[token] == right->explicit_abis[token]);
    }
    for (uint32_t declaration = 0; equal && !a->resource && declaration < left->intrinsic_declaration_count; declaration += 1)
    {
        const CheckInputIntrinsicDeclaration* x = left->intrinsic_declarations + declaration;
        const CheckInputIntrinsicDeclaration* y = right->intrinsic_declarations + declaration;
        const BuiltinIntrinsicTypeResult* p = &x->types;
        const BuiltinIntrinsicTypeResult* q = &y->types;
        equal = x->info == y->info && p->status == q->status && p->function_type == q->function_type && p->result_type == q->result_type &&
                p->parameter_count == q->parameter_count && p->ice_parameters == q->ice_parameters && p->unsupported == q->unsupported &&
                p->error_offset == q->error_offset && p->type_error == q->type_error && p->variadic == q->variadic &&
                p->prototype == q->prototype && p->signature_ignored == q->signature_ignored;
        for (uint32_t parameter = 0; equal && parameter < BUSTER_INTRINSIC_TYPE_MAX_PARAMETERS; parameter += 1)
        {
            equal = p->parameters[parameter] == q->parameters[parameter];
        }
    }
    for (uint32_t call = 0; equal && !a->resource && call < left->intrinsic_call_count; call += 1)
    {
        const CheckInputIntrinsicCall* x = left->intrinsic_calls + call;
        const CheckInputIntrinsicCall* y = right->intrinsic_calls + call;
        equal = x->declaration == y->declaration && x->keyword == y->keyword && x->opening == y->opening && x->closing == y->closing &&
                x->function_body == y->function_body && x->argument_count == y->argument_count;
        for (uint32_t argument = 0; equal && argument < x->argument_count; argument += 1)
        {
            equal = x->arguments[argument].first == y->arguments[argument].first && x->arguments[argument].end == y->arguments[argument].end;
        }
    }
    for (uint32_t region = 0; equal && region < left->region_count; region += 1)
    {
        const CheckInputRegion* x = left->regions + region;
        const CheckInputRegion* y = right->regions + region;
        equal = x->opening == y->opening && x->closing == y->closing && x->block == y->block && x->body == y->body &&
                x->result == y->result && x->final_statement == y->final_statement;
    }
    for (uint32_t string = 0; equal && string < left->string_count; string += 1)
    {
        const CheckInitString* x = left->strings + string;
        const CheckInitString* y = right->strings + string;
        equal = x->object == y->object && x->token == y->token && x->type == y->type && x->byte_count == y->byte_count &&
                (x->byte_count == 0 || memcmp(x->bytes, y->bytes, x->byte_count) == 0);
    }
    for (uint32_t vla = 0; equal && vla < left->vla_count; vla += 1)
    {
        const CheckInputVla* x = left->vlas + vla;
        const CheckInputVla* y = right->vlas + vla;
        equal = x->declaration == y->declaration && x->type == y->type && x->bound_begin == y->bound_begin && x->bound_end == y->bound_end &&
                x->bound_node == y->bound_node && x->body == y->body && x->scope_begin == y->scope_begin && x->scope_end == y->scope_end &&
                x->evaluated == y->evaluated;
    }
    for (uint32_t assembly = 0; equal && assembly < left->asm_count; assembly += 1)
    {
        const CheckInputAsm* x = left->asm_statements + assembly;
        const CheckInputAsm* y = right->asm_statements + assembly;
        equal = x->output_count == y->output_count && x->input_count == y->input_count && x->clobber_count == y->clobber_count && x->label_count == y->label_count &&
                x->token == y->token && x->closing == y->closing && x->statement == y->statement && x->body == y->body && x->basic == y->basic &&
                x->is_volatile == y->is_volatile && x->is_inline == y->is_inline && x->is_goto == y->is_goto &&
                x->memory_clobber == y->memory_clobber && x->cc_clobber == y->cc_clobber &&
                x->template_text.token == y->template_text.token && x->template_text.length == y->template_text.length &&
                (x->template_text.length == 0 || memcmp(x->template_text.bytes, y->template_text.bytes, x->template_text.length) == 0);
        for (uint32_t operand = 0; equal && operand < x->output_count + x->input_count; operand += 1)
        {
            const CheckAsmOperand* p = x->operands + operand;
            const CheckAsmOperand* q = y->operands + operand;
            equal = p->name == q->name && p->token == q->token && p->opening == q->opening && p->closing == q->closing &&
                    p->node == q->node && p->match == q->match && p->flags == q->flags &&
                    p->constraint.token == q->constraint.token && p->constraint.length == q->constraint.length &&
                    (p->constraint.length == 0 || memcmp(p->constraint.bytes, q->constraint.bytes, p->constraint.length) == 0);
        }
        for (uint32_t clobber = 0; equal && clobber < x->clobber_count; clobber += 1)
        {
            const CheckAsmText p = x->clobbers[clobber];
            const CheckAsmText q = y->clobbers[clobber];
            equal = p.token == q.token && p.length == q.length && (p.length == 0 || memcmp(p.bytes, q.bytes, p.length) == 0);
        }
        for (uint32_t label = 0; equal && label < x->label_count; label += 1)
        {
            equal = x->labels[label].name == y->labels[label].name && x->labels[label].token == y->labels[label].token && x->labels[label].target == y->labels[label].target;
        }
    }
    equal = equal && a->asms.status == b->asms.status && a->asms.token == b->asms.token && a->asms.count == b->asms.count;
    for (uint32_t operand = 0; equal && operand < a->asms.count; operand += 1)
    {
        const CheckAsmCheckedOperand* p = a->asms.operands + operand;
        const CheckAsmCheckedOperand* q = b->asms.operands + operand;
        equal = p->statement == q->statement && p->operand == q->operand && p->node == q->node && p->flags == q->flags &&
                p->conversion.type == q->conversion.type && p->conversion.flags == q->conversion.flags && check_test_value_equal(&p->value, &q->value);
    }
    const CheckGraph* graph = &left->graph;
    const CheckGraph* other = &right->graph;
    equal = equal && graph->count == other->count && graph->argument_count == other->argument_count && graph->varargs_count == other->varargs_count &&
            graph->extensions == other->extensions && graph->builtin_count == other->builtin_count && graph->vector_member_count == other->vector_member_count &&
            graph->x86_count == other->x86_count && graph->overload_count == other->overload_count &&
            graph->overload_candidate_count == other->overload_candidate_count &&
            graph->region_proof_count == other->region_proof_count && graph->region_step_count == other->region_step_count &&
            graph->tag_completion_count == other->tag_completion_count && (graph->tag_completions == 0) == (other->tag_completions == 0) &&
            (graph->transparent_unions == NULL) == (other->transparent_unions == NULL) &&
            (graph->source_positions == 0) == (other->source_positions == 0) && (graph->fp_environments == NULL) == (other->fp_environments == NULL) &&
            (graph->warning_suppressed == NULL) == (other->warning_suppressed == NULL);
    for (uint32_t tag = 0; equal && tag < graph->tag_completion_count; tag += 1)
    {
        equal = graph->tag_completions != 0 && graph->tag_completions[tag] == other->tag_completions[tag];
        equal = equal && (graph->transparent_unions == NULL || graph->transparent_unions[tag] == other->transparent_unions[tag]);
    }
    for (uint32_t node = 0; equal && !a->resource && node < graph->count; node += 1)
    {
        equal = graph->ops[node] == other->ops[node] && graph->tokens[node] == other->tokens[node] && graph->operand0[node] == other->operand0[node] &&
                graph->operand1[node] == other->operand1[node] && graph->operand2[node] == other->operand2[node] && graph->types[node] == other->types[node] &&
                graph->members[node] == other->members[node] && graph->contexts[node] == other->contexts[node] &&
                graph->argument_offsets[node] == other->argument_offsets[node] && graph->argument_counts[node] == other->argument_counts[node];
        equal = equal && (graph->source_positions == 0 || graph->source_positions[node] == other->source_positions[node]);
        equal = equal && (graph->fp_environments == NULL || graph->fp_environments[node] == other->fp_environments[node]);
        equal = equal && (graph->warning_suppressed == NULL || graph->warning_suppressed[node] == other->warning_suppressed[node]);
        if (equal && graph->ops[node] == CHECK_OP_VALUE)
        {
            equal = check_test_value_equal(check_graph_leaf_of(graph, node), check_graph_leaf_of(other, node));
        }
    }
    equal = equal && graph->system_token_count == other->system_token_count && (graph->system_tokens == NULL) == (other->system_tokens == NULL) &&
            (graph->system_tokens == NULL || memcmp(graph->system_tokens, other->system_tokens, graph->system_token_count) == 0);
    for (uint32_t argument = 0; equal && !a->resource && argument < graph->argument_count; argument += 1)
    {
        equal = graph->argument_nodes[argument] == other->argument_nodes[argument] &&
                (graph->argument_tokens == NULL) == (other->argument_tokens == NULL) &&
                (graph->argument_tokens == NULL || graph->argument_tokens[argument] == other->argument_tokens[argument]);
    }
    for (uint32_t call = 0; equal && !a->resource && call < graph->varargs_count; call += 1)
    {
        const CheckVarargsCall* x = graph->varargs_calls + call;
        const CheckVarargsCall* y = other->varargs_calls + call;
        equal = x->node == y->node && x->second_argument_declaration == y->second_argument_declaration &&
                x->context.va_list_type == y->context.va_list_type && x->context.function_type == y->context.function_type &&
                x->context.last_parameter == y->context.last_parameter && x->context.last_parameter_type == y->context.last_parameter_type &&
                x->context.last_parameter_flags == y->context.last_parameter_flags && x->context.standard == y->context.standard &&
                x->context.variadic == y->context.variadic;
    }
    for (uint32_t call = 0; equal && !a->resource && call < graph->builtin_count; call += 1)
    {
        equal = check_builtin_call_equal(graph->builtin_calls + call, other->builtin_calls + call);
    }
    for (uint32_t call = 0; equal && !a->resource && call < graph->vector_member_count; call += 1)
    {
        const CheckVectorCall* p = graph->vector_members + call;
        const CheckVectorCall* q = other->vector_members + call;
        equal = p->node == q->node && p->member.count == q->member.count && p->member.kind == q->member.kind &&
                (p->member.count == 0 || memcmp(p->member.lanes, q->member.lanes, p->member.count) == 0);
    }
    for (uint32_t call = 0; equal && !a->resource && call < graph->x86_count; call += 1)
    {
        equal = check_x86_call_equal(graph->x86_calls + call, other->x86_calls + call);
    }
    for (uint32_t candidate = 0; equal && !a->resource && candidate < graph->overload_candidate_count; candidate += 1)
    {
        const CheckOverloadCandidate* p = graph->overload_candidates + candidate;
        const CheckOverloadCandidate* q = other->overload_candidates + candidate;
        equal = p->type == q->type && p->declaration == q->declaration && p->canonical == q->canonical && p->object == q->object &&
                p->previous == q->previous && p->scope == q->scope && p->position == q->position && p->flags == q->flags;
    }
    for (uint32_t call = 0; equal && !a->resource && call < graph->overload_count; call += 1)
    {
        equal = graph->overload_calls[call].node == other->overload_calls[call].node && graph->overload_calls[call].head == other->overload_calls[call].head;
    }
    for (uint32_t region = 0; equal && !a->resource && region < graph->region_proof_count; region += 1)
    {
        equal = check_region_proof_equal(graph->region_proofs + region, other->region_proofs + region);
    }
    for (uint32_t step = 0; equal && !a->resource && step < graph->region_step_count; step += 1)
    {
        equal = check_region_step_equal(graph->region_steps + step, other->region_steps + step);
    }
    if (equal && a->checked)
    {
        equal = check_test_stream_equal(&a->stream, &b->stream, graph->argument_count);
    }
    equal = equal && check_corpus_pragmas_equal(left, right) && check_corpus_environments_equal(&left->environment, &right->environment) &&
            check_corpus_types_equal(&a->types, &b->types) &&
            check_corpus_initializers_equal(&a->initializers, &b->initializers) &&
            check_corpus_programs_equal(&a->program, &b->program);
    return equal;
}





static bool check_corpus_equal(const CheckCorpusPath* a, const CheckCorpusPath* b, const Parser* x, const Parser* y)
{
    bool equal = check_corpus_view_equal(a, b, x, y) && (a->repair == NULL) == (b->repair == NULL) &&
                 (a->repaired == NULL) == (b->repaired == NULL) && (a->original_pragmas == NULL) == (b->original_pragmas == NULL);
    if (equal && a->original_pragmas != NULL)
    {
        equal = a->original_pragmas->token_count == b->original_pragmas->token_count &&
                a->original_pragmas->resource == b->original_pragmas->resource &&
                check_corpus_pragmas_equal(a->original_pragmas, b->original_pragmas);
    }
    if (equal && a->repair != NULL)
    {
        equal = a->repair->status == b->repair->status && a->repair->diagnostic == b->repair->diagnostic &&
                a->repair->original_end == b->repair->original_end && a->repair->record_begin == b->repair->record_begin &&
                (a->repair->warning_complete == NULL) == (b->repair->warning_complete == NULL);
        for (uint32_t token = 0; equal && a->repair->warning_complete != NULL && token < a->repair->original_end; token += 1)
        {
            equal = a->repair->warning_complete[token] == b->repair->warning_complete[token];
        }
    }
    if (equal && a->repaired != NULL)
    {
        equal = check_corpus_view_equal(a->repaired, b->repaired, &a->repair->parser, &b->repair->parser);
    }
    return equal;
}

// The first difference between two inputs' other token columns and graph nodes, in `text`: whether there is one.
static bool check_corpus_deferred_columns(const CheckInput* x, const CheckInput* y, char* text, uint64_t capacity)
{
    const char* column = 0;
    uint32_t index = 0;
    for (uint32_t token = 0; column == 0 && token < x->token_count; token += 1)
    {
        column = x->function_flags[token] != y->function_flags[token] ? "function_flags" : x->static_objects[token] != y->static_objects[token] ? "static_objects" :
                 x->registers[token] != y->registers[token] ? "registers" : x->thread_local[token] != y->thread_local[token] ? "thread_local" :
                 x->compound_objects[token] != y->compound_objects[token] ? "compound_objects" :
                 !check_corpus_attributes_equal(x->attributes[token], y->attributes[token]) ? "attributes" : x->tentative[token] != y->tentative[token] ? "tentative" :
                 x->initializer_nodes[token] != y->initializer_nodes[token] ? "initializer_nodes" :
                 x->initializer_begins[token] != y->initializer_begins[token] ? "initializer_begins" : x->initializer_ends[token] != y->initializer_ends[token] ? "initializer_ends" :
                 x->body_declarations[token] != y->body_declarations[token] ? "body_declarations" : x->parameters[token] != y->parameters[token] ? "parameters" :
                 x->parameter_next[token] != y->parameter_next[token] ? "parameter_next" : x->control_owners[token] != y->control_owners[token] ? "control_owners" :
                 x->label_targets[token] != y->label_targets[token] ? "label_targets" : x->case_ranges[token] != y->case_ranges[token] ? "case_ranges" :
                 x->expression_regions[token] != y->expression_regions[token] ? "expression_regions" : x->asm_ids[token] != y->asm_ids[token] ? "asm_ids" :
                 x->constant_warnings[token] != y->constant_warnings[token] ? "constant_warnings" :
                 x->constant_warning_ignored[token] != y->constant_warning_ignored[token] ? "constant_warning_ignored" : 0;
        index = token;
    }
    const CheckGraph* g = &x->graph;
    const CheckGraph* h = &y->graph;
    for (uint32_t node = 0; column == 0 && node < g->count && node < h->count; node += 1)
    {
        column = g->ops[node] != h->ops[node] ? "graph ops" : g->tokens[node] != h->tokens[node] ? "graph tokens" : g->operand0[node] != h->operand0[node] ? "graph operand0" :
                 g->operand1[node] != h->operand1[node] ? "graph operand1" : g->operand2[node] != h->operand2[node] ? "graph operand2" :
                 g->types[node] != h->types[node] ? "graph types" : g->members[node] != h->members[node] ? "graph members" :
                 g->contexts[node] != h->contexts[node] ? "graph contexts" : g->argument_offsets[node] != h->argument_offsets[node] ? "graph argument_offsets" :
                 g->argument_counts[node] != h->argument_counts[node] ? "graph argument_counts" :
                 g->ops[node] == CHECK_OP_VALUE && !check_test_value_equal(check_graph_leaf_of(g, node), check_graph_leaf_of(h, node)) ? "graph leaves" : 0;
        index = node;
    }
    if (column == 0 && (g->argument_count != h->argument_count || g->varargs_count != h->varargs_count || g->builtin_count != h->builtin_count ||
                        g->vector_member_count != h->vector_member_count || g->x86_count != h->x86_count || g->region_proof_count != h->region_proof_count ||
                        g->region_step_count != h->region_step_count || g->tag_completion_count != h->tag_completion_count))
    {
        column = "graph side tables' counts";
    }
    for (uint32_t tag = 0; column == 0 && tag < g->tag_completion_count; tag += 1)
    {
        column = g->tag_completions[tag] != h->tag_completions[tag] ? "graph tag_completions" : 0;
        index = tag;
    }
    for (uint32_t i = 0; column == 0 && i < x->region_count; i += 1)
    {
        column = memcmp(x->regions + i, y->regions + i, sizeof(CheckInputRegion)) != 0 ? "regions" : 0;
        index = i;
    }
    for (uint32_t i = 0; column == 0 && i < x->vla_count; i += 1)
    {
        column = memcmp(x->vlas + i, y->vlas + i, sizeof(CheckInputVla)) != 0 ? "vlas" : 0;
        index = i;
    }
    for (uint32_t i = 0; column == 0 && i < x->string_count; i += 1)
    {
        column = x->strings[i].token != y->strings[i].token || x->strings[i].type != y->strings[i].type ? "strings" : 0;
        index = i;
    }
    if (column != 0)
    {
        snprintf(text, capacity, "%s at %u", column, index);
    }
    return column != 0;
}

// Where the deferred path (b) first differs from the vector path (a), for --deferred's report: the table, the bindings'
// stream, the environment, the appended tables, a token's column, the graph or the later stages. A path that made no
// input of the other's tokens (its state or tables not allocated) has no columns to compare: its stages say why.
static void check_corpus_deferred_difference(const CheckCorpusPath* a, const CheckCorpusPath* b, char* text, uint64_t capacity)
{
    const CheckInput* x = &a->input;
    const CheckInput* y = &b->input;
    const TypeTable* s = &a->types;
    const TypeTable* t = &b->types;
    const bool comparable = x->token_count == y->token_count && x->declarations != NULL && y->declarations != NULL;
    snprintf(text, capacity, "nothing found (the paths' outcomes or another column)");
    fprintf(stderr, "  stages: adapted %u %u, checked %u %u, initialized %u %u, programmed %u %u, resource %u %u, reports %u %u (first %u %u), token %u %u\n",
            a->adapted, b->adapted, a->checked, b->checked, a->initialized, b->initialized, a->programmed, b->programmed, a->resource, b->resource,
            x->report_count, y->report_count, x->report_count != 0 ? x->reports[0].token : 0, y->report_count != 0 ? y->reports[0].token : 0, a->token, b->token);
    uint32_t type = 0;
    while (type < s->types.count && type < t->types.count && s->types.kind[type] == t->types.kind[type] && s->types.base[type] == t->types.base[type] &&
           s->types.extent[type] == t->types.extent[type] && s->types.flags[type] == t->types.flags[type] && s->types.size[type] == t->types.size[type] &&
           s->types.index[type] == t->types.index[type])
    {
        type += 1;
    }
    uint32_t token = 0;
    while (comparable && token < x->token_count && x->declarations[token] == y->declarations[token] && x->declared_types[token] == y->declared_types[token] &&
           x->objects[token] == y->objects[token] && x->declaration_bodies[token] == y->declaration_bodies[token] && x->token_nodes[token] == y->token_nodes[token] &&
           x->declaration_kinds[token] == y->declaration_kinds[token] && x->linkage[token] == y->linkage[token] && x->defined[token] == y->defined[token])
    {
        token += 1;
    }
    if (!comparable)
    {
        snprintf(text, capacity, "the deferred path made no input of %u tokens to compare (%u tokens; its stages above)", x->token_count, y->token_count);
    }
    else if (type < s->types.count || s->types.count != t->types.count)
    {
        snprintf(text, capacity, "types: %u against %u, the first difference at index %u (kind %u against %u, base %#x against %#x)", s->types.count, t->types.count,
                 type, type < s->types.count ? s->types.kind[type] : 0, type < t->types.count ? t->types.kind[type] : 0, type < s->types.count ? s->types.base[type] : 0,
                 type < t->types.count ? t->types.base[type] : 0);
        for (uint32_t i = type > 8 ? type - 8 : 0; i < s->types.count || i < t->types.count; i += 1)
        {
            fprintf(stderr, "  type %u: %u/%#x/%llu against %u/%#x/%llu\n", i, i < s->types.count ? s->types.kind[i] : 0, i < s->types.count ? s->types.base[i] : 0,
                    (unsigned long long)(i < s->types.count ? s->types.extent[i] : 0), i < t->types.count ? t->types.kind[i] : 0,
                    i < t->types.count ? t->types.base[i] : 0, (unsigned long long)(i < t->types.count ? t->types.extent[i] : 0));
        }
    }
    else if (!check_corpus_types_equal(s, t))
    {
        snprintf(text, capacity, "types: tags, members, parameters or last_error (tags %u against %u, completions %u against %u, last_error %u against %u)",
                 s->tags.count, t->tags.count, s->tags.completions, t->tags.completions, s->last_error, t->last_error);
    }
    else if (x->binding_event_count != y->binding_event_count || x->string_count != y->string_count || x->vla_count != y->vla_count || x->asm_count != y->asm_count ||
             x->region_count != y->region_count || x->report_count != y->report_count || x->diagnostic_graph != y->diagnostic_graph ||
             x->parser_binding_event_count != y->parser_binding_event_count || x->intrinsic_declaration_count != y->intrinsic_declaration_count ||
             x->intrinsic_call_count != y->intrinsic_call_count || (x->intrinsic_ids == NULL) != (y->intrinsic_ids == NULL) ||
             (x->deferred_tokens == NULL) != (y->deferred_tokens == NULL) || (x->overload_heads == NULL) != (y->overload_heads == NULL) ||
             (x->explicit_abis == NULL) != (y->explicit_abis == NULL))
    {
        snprintf(text, capacity, "counts: events %u against %u, strings %u against %u, regions %u against %u, VLAs %u against %u, asm %u against %u, reports %u "
                 "against %u, intrinsics %u/%u against %u/%u, tables %u%u%u%u against %u%u%u%u", x->binding_event_count, y->binding_event_count, x->string_count,
                 y->string_count, x->region_count, y->region_count, x->vla_count, y->vla_count, x->asm_count, y->asm_count, x->report_count, y->report_count,
                 x->intrinsic_declaration_count, x->intrinsic_call_count, y->intrinsic_declaration_count, y->intrinsic_call_count, x->intrinsic_ids != NULL,
                 x->deferred_tokens != NULL, x->overload_heads != NULL, x->explicit_abis != NULL, y->intrinsic_ids != NULL, y->deferred_tokens != NULL,
                 y->overload_heads != NULL, y->explicit_abis != NULL);
    }
    else if (token < x->token_count)
    {
        snprintf(text, capacity, "token %u: declaration %u against %u, type %#x against %#x, object %u against %u, body %u against %u, node %u against %u",
                 token, x->declarations[token], y->declarations[token], x->declared_types[token], y->declared_types[token], x->objects[token], y->objects[token],
                 x->declaration_bodies[token], y->declaration_bodies[token], x->token_nodes[token], y->token_nodes[token]);
    }
    else if (!check_corpus_environments_equal(&x->environment, &y->environment))
    {
        const CBindings* e = x->environment.bindings;
        const CBindings* f = y->environment.bindings;
        uint32_t event = 0;
        while (e != 0 && f != 0 && event < e->count && event < f->count && e->operation[event] == f->operation[event] &&
               e->declaration[event] == f->declaration[event] && e->point[event] == f->point[event] &&
               (e->operation[event] == C_BINDING_RESTORE || (e->key[event] == f->key[event] && e->end[event] == f->end[event] &&
                                                             e->depth[event] == f->depth[event] && e->kind[event] == f->kind[event])))
        {
            event += 1;
        }
        const bool listed = e != 0 && f != 0 && event < e->count && event < f->count;
        uint32_t key = 0;
        while (key <= x->environment.file.keys && key <= y->environment.file.keys && x->environment.file.offsets[key] == y->environment.file.offsets[key])
        {
            key += 1;
        }
        uint32_t entry = 0;
        while (entry < x->environment.file.count && entry < y->environment.file.count && x->environment.file.points[entry] == y->environment.file.points[entry] &&
               x->environment.file.handles[entry] == y->environment.file.handles[entry])
        {
            entry += 1;
        }
        uint32_t tag = 0;
        while (tag < x->environment.tag_count && tag < y->environment.tag_count && x->environment.tag_completions[tag] == y->environment.tag_completions[tag])
        {
            tag += 1;
        }
        uint32_t declaration = 0;
        while (declaration < x->environment.token_count && declaration < y->environment.token_count &&
               x->environment.declaration_types[declaration] == y->environment.declaration_types[declaration])
        {
            declaration += 1;
        }
        fprintf(stderr, "environment: valid %u %u, bodies %u %u, file keys %u %u (first offset difference %u), file count %u %u (first entry difference %u: "
                "point %u %u handle %u %u), first tag difference %u (%u against %u), first declaration type difference %u of %u\n",
                x->environment.valid, y->environment.valid, x->environment.body_count, y->environment.body_count, x->environment.file.keys, y->environment.file.keys,
                key, x->environment.file.count, y->environment.file.count, entry,
                entry < x->environment.file.count ? x->environment.file.points[entry] : 0, entry < y->environment.file.count ? y->environment.file.points[entry] : 0,
                entry < x->environment.file.count ? x->environment.file.handles[entry] : 0, entry < y->environment.file.count ? y->environment.file.handles[entry] : 0,
                tag, tag < x->environment.tag_count ? x->environment.tag_completions[tag] : 0, tag < y->environment.tag_count ? y->environment.tag_completions[tag] : 0,
                declaration, x->environment.token_count);
        snprintf(text, capacity, "the environment: %u events against %u, event %u: operation %u against %u, declaration %u against %u, point %u against %u, "
                 "key %u against %u, end %u against %u, depth %u against %u, tags %u against %u", e != 0 ? e->count : 0, f != 0 ? f->count : 0, event,
                 listed ? e->operation[event] : 0, listed ? f->operation[event] : 0, listed ? e->declaration[event] : 0, listed ? f->declaration[event] : 0,
                 listed ? e->point[event] : 0, listed ? f->point[event] : 0, listed ? e->key[event] : 0, listed ? f->key[event] : 0,
                 listed ? e->end[event] : 0, listed ? f->end[event] : 0, listed ? e->depth[event] : 0, listed ? f->depth[event] : 0,
                 x->environment.tag_count, y->environment.tag_count);
    }
    else if (x->graph.count != y->graph.count)
    {
        snprintf(text, capacity, "the graph: %u nodes against %u", x->graph.count, y->graph.count);
    }
    else if (check_corpus_deferred_columns(x, y, text, capacity))
    {
    }
    else if (a->stream.count != b->stream.count || a->stream.status != b->stream.status)
    {
        snprintf(text, capacity, "the stream: %u entries against %u", a->stream.count, b->stream.count);
    }
    else if (!check_corpus_initializers_equal(&a->initializers, &b->initializers))
    {
        snprintf(text, capacity, "the initializers");
    }
    else if (!check_corpus_programs_equal(&a->program, &b->program))
    {
        snprintf(text, capacity, "the program");
    }
}

// What one unit's check gave, kept after its arenas are released: path 0 is the scalar path, path 1 the vector one, and
// the first error's place and message are the scalar path's.
typedef struct CheckCorpusOutcome CheckCorpusOutcome;
struct CheckCorpusOutcome
{
    bool prepared;
    // The scalar parser found no syntax error.
    bool parsed;
    bool accepted[2];
    bool equal;
    bool resource;
    uint32_t lines[2];
    uint32_t token;
    uint32_t column;
    uint32_t tokens;
    uint64_t nodes;
    uint64_t scalar_nodes;
    uint64_t vector_nodes;
    uint64_t binding_uses;
    uint64_t binding_fallback_uses;
    // With --deferred: whether the deferred reader merged the unit (not source order's), its bodies, their tokens and the
    // largest one's, the lanes they ran on and those that copied its reader, and whether it gave the vector path's result
    // exactly; and with check_corpus_deferred_timing, the lanes' span, lane_run's dispatch and the other lanes' wake-up
    // (CheckDeferred's), in nanoseconds.
    bool deferred;
    bool deferred_equal;
    uint64_t deferred_bodies;
    uint64_t deferred_body_tokens;
    uint32_t deferred_largest_body;
    uint32_t deferred_active;
    uint64_t deferred_copies;
    uint64_t deferred_lanes_ns;
    uint64_t deferred_dispatch_ns;
    uint64_t deferred_wake_ns;
    // Why a parsed unit was left to source order, for the report.
    const char* deferred_reason;
    const char* phase;
    char file[512];
    char message[256];
    char spelling[96];
};

// Preprocesses `file` and checks it on the scalar path and, when `both`, on the vector path too, which must give the
// same results exactly (`equal`; true when the vector path doesn't run). With --deferred, a third path, the vector
// path's with every body read apart (#113, check_input_deferred), must give the vector path's results exactly too.
static void check_corpus_measure(const char* file, const PPOptions* options, bool suppress_warnings, bool both, CheckCorpusOutcome* outcome)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parsers[3];
    CheckCorpusPath paths[3] = { 0 };
    *outcome = (CheckCorpusOutcome){ 0 };
    const uint32_t count = both ? (check_corpus_deferred_lanes != 0 ? 3 : 2) : 1;
    paths[2].deferred_lanes = check_corpus_deferred_lanes;
    paths[2].deferred_bindings = check_corpus_deferred_bindings;
    bool prepared = intern_initialize(&identifiers) && pp_initialize(&pp, options, &identifiers);
    prepared = prepared && pp_run(&pp, file);
    bool accepted[3] = { false, false, false };
    for (uint32_t path = 0; prepared && path < count; path += 1)
    {
        accepted[path] = check_corpus_check_policy(paths + path, parsers + path, &pp, path == 0, suppress_warnings);
    }
    outcome->accepted[0] = accepted[0];
    outcome->accepted[1] = accepted[1];
    outcome->prepared = prepared;
    outcome->parsed = paths[0].parsed;
    outcome->equal = prepared && (!both || check_corpus_equal(paths, paths + 1, parsers, parsers + 1));
    if (check_corpus_symbols != NULL && both && outcome->equal && outcome->accepted[1])
    {
        check_corpus_symbols_file(check_corpus_symbols, file, paths + 1, parsers + 1, &pp);
    }
    // The deferred path running out of room (its state's allocation among it) is a resource failure, not a result that
    // differs from the vector path's.
    const bool deferred_resource = count == 3 && paths[2].resource;
    outcome->deferred_equal = count < 3 || deferred_resource || (accepted[2] == accepted[1] && check_corpus_equal(paths + 1, paths + 2, parsers + 1, parsers + 2));
    if (!outcome->deferred_equal)
    {
        char difference[512];
        check_corpus_deferred_difference(paths + 1, paths + 2, difference, sizeof(difference));
        fprintf(stderr, "%s: the deferred reader's result differs: %s\n", file, difference);
    }
    outcome->deferred = count == 3 && paths[2].deferred != NULL && !paths[2].deferred->unsupported;
    // A unit that doesn't parse has nothing to read apart; it counts as source order's too.
    outcome->deferred_reason = count < 3 || !prepared || outcome->deferred ? NULL : !paths[2].parsed ? check_corpus_unparsed :
                               paths[2].deferred == NULL ? "the deferred path's state or tables couldn't be allocated" :
                               paths[2].deferred->reason != NULL ? paths[2].deferred->reason : "unknown";
    outcome->deferred_bodies = outcome->deferred ? paths[2].deferred->bodies : 0;
    outcome->deferred_body_tokens = outcome->deferred ? paths[2].deferred->body_tokens : 0;
    outcome->deferred_largest_body = outcome->deferred ? paths[2].deferred->largest_body : 0;
    outcome->deferred_active = outcome->deferred ? paths[2].deferred->active : 0;
    outcome->deferred_copies = outcome->deferred ? paths[2].deferred->copies : 0;
    outcome->deferred_lanes_ns = outcome->deferred ? paths[2].deferred->lanes_ns : 0;
    outcome->deferred_dispatch_ns = outcome->deferred ? paths[2].deferred->dispatch_ns : 0;
    outcome->deferred_wake_ns = outcome->deferred ? paths[2].deferred->wake_ns : 0;
    outcome->equal &= outcome->deferred_equal;
    outcome->resource = paths[0].resource || paths[1].resource || deferred_resource;
    outcome->accepted[1] = both ? outcome->accepted[1] : outcome->accepted[0];
    outcome->lines[0] = paths[0].line;
    outcome->lines[1] = both ? paths[1].line : paths[0].line;
    outcome->token = paths[0].token;
    outcome->column = paths[0].column;
    outcome->tokens = parsers[0].tokens.count;
    outcome->nodes = paths[0].stream.count;
    outcome->scalar_nodes = paths[1].stream.statistics.scalar_nodes;
    outcome->vector_nodes = paths[1].stream.statistics.vector_nodes;
    outcome->binding_uses = paths[1].input.binding_statistics.walked_uses;
    outcome->binding_fallback_uses = paths[1].input.binding_statistics.fallback_uses;
    outcome->phase = prepared ? paths[0].phase : "preprocess";
    memcpy(outcome->file, paths[0].file, sizeof(outcome->file));
    memcpy(outcome->message, paths[0].message, sizeof(outcome->message));
    memcpy(outcome->spelling, paths[0].spelling, sizeof(outcome->spelling));
    for (uint32_t path = 0; path < 3; path += 1)
    {
        check_corpus_close(paths + path);
        parse_release(parsers + path);
    }
    pp_release(&pp);
    intern_release(&identifiers);
}

// The tallies of `from` added to `to`, its differences only when `differences` (an intended semantic mutation's aren't).
static void check_corpus_tally_add(CheckCorpusTally* to, const CheckCorpusTally* from, bool differences)
{
    to->files += from->files;
    to->excluded += from->excluded;
    to->preprocessed += from->preprocessed;
    to->preprocessing_excluded += from->preprocessing_excluded;
    to->syntax_accepted_excluded += from->syntax_accepted_excluded;
    to->oracle_accepted += from->oracle_accepted;
    to->checker_accepted += from->checker_accepted;
    to->acceptance_differences += differences ? from->acceptance_differences : 0;
    to->line_differences += differences ? from->line_differences : 0;
    to->diagnostic_path_differences += differences ? from->diagnostic_path_differences : 0;
    to->path_differences += from->path_differences;
    to->resource_failures += from->resource_failures;
    to->preprocessing_failures += from->preprocessing_failures;
    to->tokens += from->tokens;
    to->nodes += from->nodes;
    to->scalar_nodes += from->scalar_nodes;
    to->vector_nodes += from->vector_nodes;
    to->binding_uses += from->binding_uses;
    to->binding_fallback_uses += from->binding_fallback_uses;
    to->deferred_units += from->deferred_units;
    to->deferred_source_order += from->deferred_source_order;
    to->deferred_bodies += from->deferred_bodies;
    to->deferred_differences += from->deferred_differences;
    for (uint32_t i = 0; i < from->deferred_reason_count; i += 1)
    {
        check_corpus_tally_reason(to, from->deferred_reasons[i], from->deferred_reason_units[i]);
    }
    to->reports += from->reports;
}

// Checks `file` against clang's result for it (`oracle`), tallies and reports the comparison, and returns whether it
// found a difference, with what the check gave in `outcome`.
static bool check_corpus_file_outcome(CheckCorpusTally* tally, const char* file, const char* label, const char* kind, const PPOptions* options,
                                      const CheckCorpusOracle* oracle, FILE* report, uint64_t report_limit, CheckCorpusOutcome* result)
{
    CheckCorpusOutcome outcome;
    check_corpus_measure(file, options, oracle->suppress_warnings, true, &outcome);
    const bool prepared = outcome.prepared;
    const bool equal = outcome.equal;
    const bool acceptance = prepared && outcome.accepted[0] != oracle->rejected;
    const bool line = !oracle->rejected || outcome.accepted[0] || outcome.lines[0] == oracle->line;
    const bool diagnostic_path = !oracle->rejected || outcome.accepted[0] || strcmp(outcome.file, oracle->file) == 0;
    const bool resource = outcome.resource;
    tally->files += 1;
    tally->oracle_accepted += !oracle->rejected;
    tally->checker_accepted += outcome.accepted[0];
    tally->acceptance_differences += !acceptance;
    tally->line_differences += !line;
    tally->diagnostic_path_differences += !diagnostic_path;
    tally->path_differences += prepared && !equal;
    tally->resource_failures += resource;
    tally->preprocessing_failures += !prepared;
    tally->tokens += outcome.tokens;
    tally->nodes += outcome.nodes;
    tally->scalar_nodes += outcome.scalar_nodes;
    tally->vector_nodes += outcome.vector_nodes;
    tally->binding_uses += outcome.binding_uses;
    tally->binding_fallback_uses += outcome.binding_fallback_uses;
    tally->deferred_units += outcome.deferred;
    tally->deferred_source_order += prepared && check_corpus_deferred_lanes != 0 && !outcome.deferred;
    tally->deferred_bodies += outcome.deferred_bodies;
    tally->deferred_differences += prepared && !outcome.deferred_equal;
    if (outcome.deferred_reason != NULL)
    {
        check_corpus_tally_reason(tally, outcome.deferred_reason, 1);
    }
    const bool different = !acceptance || !line || !diagnostic_path || !equal || resource;
    if (report != 0)
    {
        fprintf(report, "%s\t", kind);
        check_corpus_field(report, label);
        fprintf(report, "\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%s\t", !oracle->rejected, outcome.accepted[0], outcome.accepted[1], oracle->line,
                outcome.lines[0], outcome.lines[1], equal, resource, outcome.tokens, outcome.phase);
        check_corpus_field(report, outcome.file);
        fprintf(report, "\t%u\t%u\t", outcome.token, outcome.column);
        check_corpus_field(report, outcome.spelling);
        fputc('\t', report);
        check_corpus_field(report, outcome.message);
        fputc('\t', report);
        check_corpus_field(report, oracle->message);
        fputc('\t', report);
        check_corpus_field(report, oracle->file);
        fprintf(report, "\t%u", diagnostic_path);
        fputc('\n', report);
    }
    if (outcome.deferred_reason != NULL && outcome.deferred_reason != check_corpus_unparsed && tally->reports < report_limit)
    {
        fprintf(stderr, "%s: deferred reading left to source order: %s\n", label, outcome.deferred_reason);
        tally->reports += 1;
    }
    if (different && tally->reports < report_limit)
    {
        fprintf(stderr, "%s: clang=%s:%u checker=%s:%u paths=%s%s phase=%s token=%u spelling=\"%s\" at %s:%u:%u: %s\n", label,
                oracle->rejected ? "reject" : "accept", oracle->line, outcome.accepted[0] ? "accept" : "reject", outcome.lines[0],
                equal ? "equal" : "DIFFERENT", outcome.deferred_equal ? "" : " (deferred)", outcome.phase, outcome.token, outcome.spelling,
                outcome.file, outcome.lines[0], outcome.column, outcome.message);
        tally->reports += 1;
    }
    *result = outcome;
    return different;
}

static bool check_corpus_file(CheckCorpusTally* tally, const char* file, const char* label, const char* kind, const PPOptions* options,
                              const CheckCorpusOracle* oracle, FILE* report, uint64_t report_limit)
{
    CheckCorpusOutcome outcome;
    return check_corpus_file_outcome(tally, file, label, kind, options, oracle, report, report_limit, &outcome);
}

// `bases`, if not null, gets each file's fitness as a semantic mutation's base: compared, accepted by clang, and without
// a difference.
static bool check_corpus_files(CheckCorpusTally* tally, char** files, uint32_t count, const PPOptions* options, const char* clang,
                               const char* const* arguments, uint32_t argument_count, FILE* report, FILE* selection,
                               bool rejections, uint64_t report_limit, bool* bases)
{
    bool valid = true;
    for (uint32_t first = 0; valid && first < count; first += BUSTER_PARSE_TEST_BATCH)
    {
        const uint32_t batch = count - first < BUSTER_PARSE_TEST_BATCH ? count - first : BUSTER_PARSE_TEST_BATCH;
        CheckCorpusOracle outcomes[BUSTER_PARSE_TEST_BATCH];
        valid = check_corpus_clang(clang, arguments, argument_count, files + first, batch, outcomes);
        for (uint32_t i = 0; valid && i < batch; i += 1)
        {
            const char* file = files[first + i];
            CheckCorpusOracle preprocessing = { 0 };
            if (rejections && outcomes[i].rejected)
            {
                valid = check_corpus_clang_phase(clang, arguments, argument_count, file, true, &preprocessing);
            }
            const bool excluded = rejections ? !outcomes[i].rejected || preprocessing.rejected : selection != 0 && outcomes[i].rejected;
            if (valid && rejections)
            {
                tally->preprocessed += !preprocessing.rejected;
                tally->preprocessing_excluded += preprocessing.rejected;
                tally->syntax_accepted_excluded += !outcomes[i].rejected;
            }
            if (valid && excluded)
            {
                tally->excluded += 1;
                const CheckCorpusOracle* outcome = rejections && preprocessing.rejected ? &preprocessing : outcomes + i;
                const char* phase = rejections ? preprocessing.rejected ? "preprocess-excluded" : "syntax-accepted-not-compared" : "selection";
                if (report != 0)
                {
                    fputs("excluded\t", report);
                    check_corpus_field(report, file);
                    fprintf(report, "\t%u\t0\t0\t%u\t0\t0\t0\t0\t0\t%s\t", !outcomes[i].rejected, outcome->line, phase);
                    check_corpus_field(report, outcome->file);
                    fputs("\t0\t0\t\t\t", report);
                    check_corpus_field(report, outcome->message);
                    fputc('\t', report);
                    check_corpus_field(report, outcome->file);
                    fputs("\t0\n", report);
                }
            }
            else if (valid)
            {
                valid = selection == 0 || fprintf(selection, "%s\n", file) > 0;
                if (valid)
                {
                    const bool different = check_corpus_file(tally, file, file, rejections ? "semantic-rejection" : "file", options, outcomes + i,
                                                             report, report_limit);
                    if (bases != NULL)
                    {
                        bases[first + i] = !different && !outcomes[i].rejected;
                    }
                }
            }
        }
    }
    return valid;
}

// Original generated programs combine valid source with one intentional constraint error in every fourth case.
// Random literals, types, statement counts and operators vary the graph independently of the semantic fixtures.
static bool check_corpus_random_text(char* text, size_t capacity, uint64_t* state, uint64_t index, size_t* length)
{
    static const char* const integer_types[] =
    {
        "unsigned",
        "unsigned long",
        "unsigned long long",
    };
    static const char* const operations[] =
    {
        "+",
        "-",
        "*",
        "^",
        "|",
        "&",
    };
    const uint32_t a = (uint32_t)(parse_test_random(state) % 97 + 1);
    const uint32_t b = (uint32_t)(parse_test_random(state) % 13 + 1);
    const char* type = integer_types[parse_test_random(state) % BUSTER_ARRAY_LENGTH(integer_types)];
    int wrote = 0;
    const uint32_t shape = (uint32_t)(parse_test_random(state) % 16);
    switch (shape)
    {
        case 0:
            wrote = snprintf(text, capacity, "typedef %s Word;\nWord calc(Word x)\n{\n    Word v = %u;\n", type, a);
            break;
        case 1:
            wrote = snprintf(text, capacity, "struct Pair { long a; unsigned b; };\nstatic struct Pair pair = { .b = %u, .a = %u };\nlong calc(long x)\n{\n    long v = pair.a + pair.b + x;\n", b, a);
            break;
        case 2:
            wrote = snprintf(text, capacity, "static int add(int x) { return x + %u; }\nint calc(int (*fn)(int))\n{\n    int (*local)(int) = fn;\n    int v = local(%u);\n", b, a);
            break;
        case 3:
            wrote = snprintf(text, capacity, "enum { Count = (%u + %u) / %u + 1 };\nstatic int items[Count];\nint calc(int x)\n{\n    int v = items[x %% Count];\n", a, b, b);
            break;
        case 4:
            wrote = snprintf(text, capacity, "_Static_assert((1 ? %u : 1 / 0) > 0, \"selected\");\nunsigned calc(unsigned x)\n{\n    unsigned v = x ? %u : %u;\n", a, a, b);
            break;
        case 5:
            wrote = snprintf(text, capacity, "int calc(int x)\n{\n    int v = %u;\n    { int x = %u; v += x; }\n    int *p = &v;\n    *p += x;\n", a, b);
            break;
        case 6:
            wrote = snprintf(text, capacity, "static const char text[] = \"ab\\x63\" \"d\";\nunsigned calc(unsigned x)\n{\n    unsigned v = sizeof(text) + text[x %% (sizeof(text) - 1)];\n");
            break;
        case 7:
            wrote = snprintf(text, capacity, "unsigned calc(unsigned x)\n{\n    unsigned v = %u;\n    for (unsigned i = 0; i < %u; ++i) { v += x ^ i; }\n", a, b);
            break;
        case 8:
            wrote = snprintf(text, capacity, "unsigned calc(unsigned x)\n{\n    unsigned v;\n    switch (x & 3u) { case 0: v = %u; break; case 1: v = %u; break; default: v = x; }\n    if (x & 1u) goto done;\n    v++;\ndone: v += %u;\n", a, b, b);
            break;
        case 9:
            wrote = snprintf(text, capacity, "union Choice { unsigned word; unsigned char bytes[4]; };\nstatic union Choice choice = { .word = %u };\nunsigned calc(unsigned x)\n{\n    unsigned v = choice.word + x;\n", a);
            break;
        case 10:
            wrote = snprintf(text, capacity, "unsigned calc(unsigned x)\n{\n    unsigned v = _Generic((x), unsigned: x + %u, default: x - %u);\n", a, b);
            break;
        case 11:
            wrote = snprintf(text, capacity, "unsigned calc(unsigned x)\n{\n    unsigned v = __builtin_choose_expr(sizeof(int) == 4, x + %u, x - %u);\n", a, b);
            break;
        case 12:
            wrote = snprintf(text, capacity, "unsigned calc(unsigned x)\n{\n    unsigned stored;\n    unsigned v = __builtin_add_overflow(x, %u, &stored);\n    v += __builtin_popcount(x) + stored;\n", a);
            break;
        case 13:
            wrote = snprintf(text, capacity, "struct Pair { unsigned a; unsigned b; };\nunsigned calc(unsigned x)\n{\n    struct Pair *p = &(struct Pair){ .b = %u, .a = x };\n    unsigned v = p->a + p->b;\n", b);
            break;
        case 14:
            wrote = snprintf(text, capacity, "static unsigned items[8] = { [6] = %u, [2] = %u };\nunsigned calc(unsigned x)\n{\n    unsigned v = items[x %% 8] + sizeof(items);\n", a, b);
            break;
        case 15:
            wrote = snprintf(text, capacity, "unsigned calc(unsigned x)\n{\n    unsigned items[4] = { %u, %u, 0, 1 };\n    const unsigned *p = items + (x & 3u);\n    unsigned v = *p + (p - items);\n", a, b);
            break;
    }
    bool valid = wrote > 0 && (size_t)wrote < capacity;
    size_t used = valid ? (size_t)wrote : 0;
    const uint32_t statements = (uint32_t)(parse_test_random(state) % 12 + 1);
    for (uint32_t i = 0; valid && i < statements; i += 1)
    {
        const char* op = operations[parse_test_random(state) % BUSTER_ARRAY_LENGTH(operations)];
        const uint32_t value = (uint32_t)(parse_test_random(state) % 31 + 1);
        wrote = snprintf(text + used, capacity - used, "    v = (v %s %uu) + %uu;\n", op, value, b);
        valid = wrote > 0 && (size_t)wrote < capacity - used;
        used += valid ? (size_t)wrote : 0;
    }
    if (valid)
    {
        const char* error = "";
        if (index % 4 == 3)
        {
            static const char* const errors[] =
            {
                "    v = absent_identifier;\n",
                "    v = *v;\n",
                "    int broken[-1];\n",
                "    _Static_assert(0, \"intentional\");\n",
            };
            error = errors[parse_test_random(state) % BUSTER_ARRAY_LENGTH(errors)];
        }
        wrote = snprintf(text + used, capacity - used, "%s    return v;\n}\n", error);
        valid = wrote > 0 && (size_t)wrote < capacity - used;
        used += valid ? (size_t)wrote : 0;
    }
    *length = used;
    return valid;
}

static bool check_corpus_write(const char* path, const char* text, size_t length)
{
    FILE* stream = fopen(path, "wx");
    bool valid = stream != 0 && fwrite(text, 1, length, stream) == length;
    if (stream != 0)
    {
        valid = fclose(stream) == 0 && valid;
    }
    if (!valid && stream != 0)
    {
        unlink(path);
    }
    return valid;
}

static bool check_corpus_oracle_controls(const char* directory, const char* clang)
{
    static const char* const sources[] =
    {
        "\n\nint header(void) {\nreturn oracle_missing;\n}\n",
        "#include \"oracle_4.c\"\n",
        "int direct(void) {\nreturn oracle_other;\n}\n",
        "int accepted(void) { return 0; }\n",
        "#include \"oracle_0.c\"\n#include \"missing_oracle_header.h\"\n",
        "#line 37 \"oracle-presumed.c\"\nint renamed(void) { return oracle_renamed; }\n",
    };
    const char* arguments[] =
    {
        "--no-default-config",
        "--target=x86_64-linux-gnu",
        "-std=gnu17",
        "-x",
        "c",
    };
    char names[BUSTER_ARRAY_LENGTH(sources)][4200];
    char* paths[BUSTER_ARRAY_LENGTH(sources)];
    CheckCorpusOracle results[BUSTER_ARRAY_LENGTH(sources)];
    uint32_t created = 0;
    bool valid = true;
    for (uint32_t i = 0; valid && i < BUSTER_ARRAY_LENGTH(sources); i += 1)
    {
        snprintf(names[i], sizeof(names[i]), "%s/oracle_%u.c", directory, i);
        paths[i] = names[i];
        valid = check_corpus_write(paths[i], sources[i], strlen(sources[i]));
        created += valid;
    }
    valid = valid && check_corpus_clang(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), paths, BUSTER_ARRAY_LENGTH(sources), results) &&
            results[0].rejected && results[0].line == 4 && results[1].rejected && results[1].line == 4 &&
            strcmp(results[0].file, results[1].file) == 0 && results[2].rejected && results[2].line == 2 && !results[3].rejected &&
            results[4].rejected && results[4].line == 4 && strcmp(results[0].file, results[4].file) == 0 &&
            results[5].rejected && results[5].line == 37 && strcmp(results[5].file, "oracle-presumed.c") == 0;
    CheckCorpusOracle preprocessing[BUSTER_ARRAY_LENGTH(sources)];
    for (uint32_t i = 0; valid && i < BUSTER_ARRAY_LENGTH(sources); i += 1)
    {
        valid = check_corpus_clang_phase(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), paths[i], true, preprocessing + i);
    }
    valid = valid && !preprocessing[0].rejected && !preprocessing[2].rejected && !preprocessing[3].rejected && !preprocessing[5].rejected &&
            preprocessing[1].rejected && preprocessing[4].rejected && preprocessing[1].line == 2 && preprocessing[4].line == 2 &&
            strcmp(preprocessing[1].file, paths[4]) == 0 && strcmp(preprocessing[4].file, paths[4]) == 0;
    for (uint32_t i = 0; i < created; i += 1)
    {
        valid = unlink(paths[i]) == 0 && valid;
    }
    if (!valid)
    {
        fprintf(stderr, "checker-corpus: independent oracle attribution controls failed\n");
    }
    return valid;
}

static char* check_corpus_mutate(const char* text, size_t length, const uint32_t* starts, const uint32_t* ends,
                                uint32_t token, uint32_t operation, size_t* result_length)
{
    const uint32_t start = starts[token];
    const uint32_t end = ends[token];
    const uint32_t next_start = starts[token + 1];
    const uint32_t next_end = ends[token + 1];
    const size_t capacity = length + (size_t)(end - start) * 2 + 8;
    char* result = malloc(capacity);
    size_t used = 0;
    if (result != 0)
    {
        memcpy(result, text, start);
        used = start;
        result[used++] = ' ';
        if (operation == 0)
        {
            memcpy(result + used, text + end, length - end);
            used += length - end;
        }
        else if (operation == 1)
        {
            memcpy(result + used, text + start, end - start);
            used += end - start;
            result[used++] = ' ';
            memcpy(result + used, text + start, length - start);
            used += length - start;
        }
        else
        {
            memcpy(result + used, text + next_start, next_end - next_start);
            used += next_end - next_start;
            memcpy(result + used, text + end, next_start - end);
            used += next_start - end;
            memcpy(result + used, text + start, end - start);
            used += end - start;
            result[used++] = ' ';
            memcpy(result + used, text + next_end, length - next_end);
            used += length - next_end;
        }
    }
    *result_length = used;
    return result;
}

static bool check_corpus_generated(CheckCorpusTally* tally, char** files, uint32_t file_count, uint64_t count, uint64_t seed,
                                   uint32_t mode, const char* directory, bool keep, const PPOptions* options, const char* clang,
                                   const char* const* arguments, uint32_t argument_count, FILE* report, uint64_t report_limit)
{
    const bool mutations = mode != 0;
    const char* kind = mutations ? "mutation" : "random";
    PPOptions plain = *options;
    plain.no_standard_includes = true;
    plain.include_count = 0;
    plain.macro_count = 0;
    plain.predefines_path = 0;
    plain.undefine = true;
    const char** clang_arguments = malloc(((size_t)argument_count + 2) * sizeof(char*));
    if (clang_arguments != 0)
    {
        memcpy(clang_arguments, arguments, (size_t)argument_count * sizeof(char*));
        clang_arguments[argument_count] = "-x";
        clang_arguments[argument_count + 1] = "cpp-output";
    }
    char names[BUSTER_PARSE_TEST_BATCH][4200];
    char labels[BUSTER_PARSE_TEST_BATCH][4480];
    char* paths[BUSTER_PARSE_TEST_BATCH];
    CheckCorpusOracle oracle[BUSTER_PARSE_TEST_BATCH];
    uint64_t state = seed;
    uint64_t done = 0;
    uint64_t source = 0;
    uint32_t empty = 0;
    bool valid = clang_arguments != 0 && (count == 0 || !mutations || file_count != 0);
    while (valid && done < count)
    {
        size_t source_length = 0;
        const char* file = mutations ? files[source % file_count] : 0;
        char* original = mutations ? parse_test_preprocessed(file, options, &source_length) : 0;
        const uint64_t capacity = mutations ? BUSTER_C_TOKENIZER_CAPACITY(source_length) : 0;
        uint8_t* kinds = mutations && original != 0 ? malloc(capacity) : 0;
        uint32_t* starts = mutations && original != 0 ? malloc(capacity * sizeof(uint32_t)) : 0;
        uint32_t* ends = mutations && original != 0 ? malloc(capacity * sizeof(uint32_t)) : 0;
        valid = !mutations || (original != 0 && kinds != 0 && starts != 0 && ends != 0 && source_length <= UINT32_MAX);
        const int64_t tokens = valid && mutations ? c_tokenize((const uint8_t*)original, source_length, kinds, starts, ends, 0) : 0;
        valid = valid && tokens >= 0;
        uint64_t remaining = mutations ? (count + file_count - 1) / file_count : count;
        if (mutations && tokens < 2)
        {
            remaining = 0;
            empty += 1;
            valid = empty < file_count;
        }
        else
        {
            empty = 0;
        }
        while (valid && remaining != 0 && done < count)
        {
            const uint64_t available = remaining < count - done ? remaining : count - done;
            const uint32_t batch = available < BUSTER_PARSE_TEST_BATCH ? (uint32_t)available : BUSTER_PARSE_TEST_BATCH;
            uint32_t created = 0;
            bool retained[BUSTER_PARSE_TEST_BATCH] = { 0 };
            for (uint32_t b = 0; valid && b < batch; b += 1)
            {
                paths[b] = names[b];
                snprintf(names[b], sizeof(names[b]), "%s/%s_%" PRIu64 ".c", directory, kind, done + b);
                if (mutations)
                {
                    const uint32_t operation = (uint32_t)(parse_test_random(&state) % 3);
                    const uint32_t token = (uint32_t)(parse_test_random(&state) % (uint64_t)(tokens - 1));
                    size_t length = 0;
                    char* text = check_corpus_mutate(original, source_length, starts, ends, token, operation, &length);
                    valid = text != 0 && check_corpus_write(names[b], text, length);
                    const char* operation_name = operation == 0 ? "delete" : operation == 1 ? "duplicate" : "swap";
                    snprintf(labels[b], sizeof(labels[b]), "%s (seed=%" PRIu64 " %s=%" PRIu64 " token=%u %s)", file, seed, kind, done + b, token, operation_name);
                    free(text);
                }
                else
                {
                    char text[8192];
                    size_t length = 0;
                    valid = check_corpus_random_text(text, sizeof(text), &state, done + b, &length) && check_corpus_write(names[b], text, length);
                    snprintf(labels[b], sizeof(labels[b]), "random (seed=%" PRIu64 " program=%" PRIu64 ")", seed, done + b);
                }
                created += valid;
            }
            valid = valid && check_corpus_clang(clang, clang_arguments, argument_count + 2, paths, batch, oracle);
            for (uint32_t b = 0; valid && b < batch; b += 1)
            {
                const bool different = check_corpus_file(tally, paths[b], labels[b], kind, &plain, oracle + b, report, report_limit);
                retained[b] = keep && different;
            }
            for (uint32_t b = 0; b < created; b += 1)
            {
                if (!retained[b])
                {
                    valid = unlink(paths[b]) == 0 && valid;
                }
            }
            done += valid ? batch : 0;
            remaining -= valid ? batch : 0;
        }
        free(ends);
        free(starts);
        free(kinds);
        free(original);
        source += 1;
    }
    free(clang_arguments);
    return valid && done == count;
}

static bool check_corpus_number(const char* text, uint64_t* result)
{
    char* end = 0;
    errno = 0;
    *result = strtoull(text, &end, 10);
    return errno == 0 && text[0] != '-' && end != text && *end == 0;
}

static int check_corpus_path_compare(const void* a, const void* b)
{
    return strcmp(*(char* const*)a, *(char* const*)b);
}

static bool check_corpus_manifest(const char* path, const char* root, char** files, uint32_t* count, uint32_t capacity)
{
    FILE* stream = fopen(path, "r");
    char* line = 0;
    size_t capacity_line = 0;
    bool valid = stream != 0;
    ssize_t length = 0;
    while (valid && (length = getline(&line, &capacity_line, stream)) >= 0)
    {
        while (length != 0 && (line[length - 1] == '\n' || line[length - 1] == '\r'))
        {
            line[--length] = 0;
        }
        if (length != 0)
        {
            const size_t bytes = strlen(root) + (size_t)length + 2;
            char* name = *count < capacity ? malloc(bytes) : 0;
            valid = name != 0;
            if (valid)
            {
                snprintf(name, bytes, line[0] == '/' ? "%s%s" : "%s/%s", line[0] == '/' ? "" : root, line);
                struct stat status;
                valid = stat(name, &status) == 0 && S_ISREG(status.st_mode);
                if (valid)
                {
                    files[(*count)++] = name;
                }
                else
                {
                    fprintf(stderr, "corpus manifest input is not a readable regular file: %s\n", name);
                    free(name);
                }
            }
        }
    }
    if (stream != 0)
    {
        valid = valid && !ferror(stream);
        valid &= fclose(stream) == 0;
    }
    free(line);
    return valid;
}

// The semantic mutation mode (--semantic-mutations) checks its mutants with check_corpus_file_outcome above.
#include "c_check_mutation.c"

static int run_checker_corpus(int count, char** arguments)
{
    const char* clang = "clang";
    const char* report_path = 0;
    const char* selection_path = 0;
    const char* manifest = 0;
    const char* root = ".";
    uint64_t report_limit = 12;
    uint64_t limit = UINT32_MAX;
    uint64_t random_count = 0;
    uint64_t mutation_count = 0;
    uint64_t semantic_count = 0;
    uint64_t minimize = 4;
    uint64_t seed = 133;
    bool keep = false;
    uint64_t deferred_lanes = 0;
    CheckInputBindingMode deferred_bindings = CHECK_INPUT_BINDINGS_DEFAULT;
    bool minimize_mutants = false;
    bool rejections = false;
    bool symbols_requested = false;
    uint64_t symbols_minimum = 0;
    const uint32_t file_capacity = 1u << 20;
    char** files = calloc(file_capacity, sizeof(char*));
    char** inputs = calloc((size_t)count + 1, sizeof(char*));
    char** mutants = calloc((size_t)count + 1, sizeof(char*));
    uint32_t mutant_count = 0;
    const int argument_capacity = count < (INT_MAX - BUSTER_PP_ORACLE_EXTRA_ARGUMENTS) / 2 ? count * 2 + BUSTER_PP_ORACLE_EXTRA_ARGUMENTS : 0;
    char** pp_arguments = calloc((size_t)argument_capacity, sizeof(char*));
    const char** clang_arguments = calloc((size_t)argument_capacity, sizeof(char*));
    uint32_t input_count = 0;
    uint32_t pp_count = 0;
    uint32_t clang_count = 0;
    uint32_t file_count = 0;
    bool valid = argument_capacity != 0 && files != 0 && inputs != 0 && mutants != 0 && pp_arguments != 0 && clang_arguments != 0;
    if (valid)
    {
        clang_arguments[clang_count++] = "--no-default-config";
        clang_arguments[clang_count++] = "--target=x86_64-linux-gnu";
        clang_arguments[clang_count++] = "-x";
        clang_arguments[clang_count++] = "c";
        pp_arguments[pp_count++] = "--target=x86_64-linux-gnu";
    }
    for (int i = 0; valid && i < count; i += 1)
    {
        const char* argument = arguments[i];
        const bool value = strcmp(argument, "--clang") == 0 || strcmp(argument, "--report") == 0 || strcmp(argument, "--files-from") == 0 ||
                           strcmp(argument, "--select-accepted") == 0 || strcmp(argument, "--semantic-rejections") == 0 ||
                           strcmp(argument, "--root") == 0 || strcmp(argument, "--reports") == 0 || strcmp(argument, "--limit") == 0 ||
                           strcmp(argument, "--random") == 0 || strcmp(argument, "--mutations") == 0 || strcmp(argument, "--semantic-mutations") == 0 ||
                           strcmp(argument, "--seed") == 0 || strcmp(argument, "--minimize") == 0 || strcmp(argument, "--deferred") == 0 ||
                           strcmp(argument, "--deferred-bindings") == 0 || strcmp(argument, "--symbols-minimum") == 0;
        if (value)
        {
            valid = i + 1 < count;
            if (valid)
            {
                const char* text = arguments[++i];
                if (strcmp(argument, "--clang") == 0)
                {
                    clang = text;
                }
                else if (strcmp(argument, "--report") == 0)
                {
                    report_path = text;
                }
                else if (strcmp(argument, "--select-accepted") == 0 || strcmp(argument, "--semantic-rejections") == 0)
                {
                    valid = selection_path == NULL;
                    selection_path = text;
                    rejections = strcmp(argument, "--semantic-rejections") == 0;
                }
                else if (strcmp(argument, "--files-from") == 0)
                {
                    manifest = text;
                }
                else if (strcmp(argument, "--root") == 0)
                {
                    root = text;
                }
                else if (strcmp(argument, "--deferred-bindings") == 0)
                {
                    deferred_bindings = strcmp(text, "stacks") == 0 ? CHECK_INPUT_BINDINGS_STACKS : strcmp(text, "batch") == 0 ? CHECK_INPUT_BINDINGS_BATCH :
                                        strcmp(text, "direct") == 0 ? CHECK_INPUT_BINDINGS_DIRECT : CHECK_INPUT_BINDINGS_DEFAULT;
                    valid = deferred_bindings != CHECK_INPUT_BINDINGS_DEFAULT;
                }
                else
                {
                    uint64_t* destination = strcmp(argument, "--limit") == 0 ? &limit : strcmp(argument, "--random") == 0 ? &random_count :
                                            strcmp(argument, "--mutations") == 0 ? &mutation_count : strcmp(argument, "--semantic-mutations") == 0 ? &semantic_count :
                                            strcmp(argument, "--seed") == 0 ? &seed : strcmp(argument, "--minimize") == 0 ? &minimize :
                                            strcmp(argument, "--deferred") == 0 ? &deferred_lanes :
                                            strcmp(argument, "--symbols-minimum") == 0 ? &symbols_minimum : &report_limit;
                    valid = check_corpus_number(text, destination);
                    // A floor on the files compared asks for the comparison.
                    symbols_requested |= destination == &symbols_minimum;
                }
            }
        }
        else if (strcmp(argument, "--keep") == 0)
        {
            keep = true;
        }
        else if (strcmp(argument, "--symbols") == 0)
        {
            symbols_requested = true;
        }
        else if (strcmp(argument, "--minimize-mutant") == 0)
        {
            // Every file of the command line is then a mutant to minimize, as `--minimize-mutant FILE...` says.
            minimize_mutants = true;
        }
        else if (argument[0] != '-')
        {
            inputs[input_count++] = arguments[i];
        }
        else if (strcmp(argument, "-E") == 0 || strcmp(argument, "-H") == 0 || strcmp(argument, "--scalar") == 0 ||
                 strcmp(argument, "--buster-headers") == 0 || strcmp(argument, "--predefines") == 0 || strcmp(argument, "-o") == 0)
        {
            valid = false;
        }
        else
        {
            if (strcmp(argument, "--no-default-config") != 0 && strcmp(argument, "-pipe") != 0)
            {
                pp_arguments[pp_count++] = arguments[i];
            }
            if (pp_own_option_with_value(argument))
            {
                valid = i + 1 < count;
                if (valid)
                {
                    pp_arguments[pp_count++] = arguments[++i];
                }
            }
            else if (strcmp(argument, "--clang-features") != 0)
            {
                clang_arguments[clang_count++] = argument;
                const bool takes = strcmp(argument, "-I") == 0 || strcmp(argument, "-D") == 0 || strcmp(argument, "-U") == 0 ||
                                   strcmp(argument, "-isystem") == 0 || strcmp(argument, "-include") == 0 || strcmp(argument, "-iquote") == 0 ||
                                   strcmp(argument, "-idirafter") == 0 || strcmp(argument, "-x") == 0 || strcmp(argument, "-target") == 0;
                valid = !takes || i + 1 < count;
                if (valid && takes)
                {
                    clang_arguments[clang_count++] = arguments[++i];
                    pp_arguments[pp_count++] = arguments[i];
                }
            }
        }
    }
    Arena options_arena = arena_reserve(UINT64_C(1) << 26);
    PPOraclePaths include_paths = {
        .arena = arena_reserve(UINT64_C(1) << 20),
    };
    const bool fixed_epoch = getenv("SOURCE_DATE_EPOCH") == 0;
    valid = valid && options_arena.base != 0 && include_paths.arena.base != 0 &&
            (!fixed_epoch || setenv("SOURCE_DATE_EPOCH", BUSTER_PP_ORACLE_EPOCH, 1) == 0);
    bool no_standard_includes = false;
    for (uint32_t i = 0; i < clang_count; i += 1)
    {
        no_standard_includes |= strcmp(clang_arguments[i], "-nostdinc") == 0;
    }
    if (valid && !no_standard_includes)
    {
        int probed_count = (int)clang_count;
        valid = pp_oracle_includes(clang, (char**)clang_arguments, &probed_count, argument_capacity, &include_paths);
        for (uint32_t i = clang_count; valid && i < (uint32_t)probed_count; i += 1)
        {
            pp_arguments[pp_count++] = (char*)clang_arguments[i];
        }
        clang_count = (uint32_t)probed_count;
    }
    PPOptions options = { 0 };
    const char* placeholder = 0;
    const char* output = 0;
    bool markers = false;
    if (valid)
    {
        pp_arguments[pp_count++] = "placeholder.c";
        valid = pp_parse_options((int)pp_count, pp_arguments, &options, &options_arena, &placeholder, &output, &markers) && output == 0;
    }
    char standard[32];
    snprintf(standard, sizeof(standard), "-std=%s%u", options.language.gnu ? "gnu" : "c", options.language.standard % 100);
    if (valid)
    {
        clang_arguments[clang_count++] = standard;
        options.quiet = true;
        options.clang_features = true;
        options.semantic_pragmas = true;
        for (uint32_t i = 0; minimize_mutants && i < input_count; i += 1)
        {
            mutants[mutant_count++] = inputs[i];
        }
        for (uint32_t i = 0; valid && !minimize_mutants && i < input_count; i += 1)
        {
            const uint32_t before = file_count;
            struct stat information;
            if (stat(inputs[i], &information) == 0 && S_ISREG(information.st_mode) && file_count < file_capacity)
            {
                files[file_count] = strdup(inputs[i]);
                file_count += files[file_count] != NULL;
            }
            else
            {
                file_count = parse_test_collect(inputs[i], files, file_count, file_capacity);
            }
            valid = file_count > before && file_count < file_capacity;
        }
        valid = valid && (manifest == 0 || check_corpus_manifest(manifest, root, files, &file_count, file_capacity));
        valid = valid && (file_count != 0 || random_count != 0 || mutant_count != 0) && (!minimize_mutants || (mutant_count != 0 && file_count == 0 && random_count == 0)) &&
                limit != 0 && random_count <= UINT32_MAX && mutation_count <= UINT32_MAX &&
                semantic_count <= UINT32_MAX && ((mutation_count == 0 && semantic_count == 0) || file_count != 0) &&
                (selection_path == 0 || (random_count == 0 && mutation_count == 0 && (semantic_count == 0 || !rejections)));
    }
    uint32_t selected = 0;
    if (valid)
    {
        qsort(files, file_count, sizeof(char*), check_corpus_path_compare);
        for (uint32_t i = 0; i < file_count; i += 1)
        {
            if (selected != 0 && strcmp(files[selected - 1], files[i]) == 0)
            {
                free(files[i]);
            }
            else
            {
                files[selected++] = files[i];
            }
        }
        file_count = selected;
        selected = selected < limit ? selected : (uint32_t)limit;
    }
    char directory[4096] = { 0 };
    const char* temporary = getenv("TMPDIR");
    const int written = snprintf(directory, sizeof(directory), "%s/buster-check-corpus-XXXXXX", temporary != 0 ? temporary : "/tmp");
    const bool made_directory = valid && written > 0 && written < (int)sizeof(directory) && mkdtemp(directory) != 0;
    valid = valid && made_directory;
    valid = valid && check_corpus_oracle_controls(directory, clang);
    char predefines[4160];
    snprintf(predefines, sizeof(predefines), "%s/predefines.h", directory);
    if (valid)
    {
        PPClangOutput macros = pp_test_clang_process(clang, (char**)clang_arguments, (int)clang_count, "/dev/null", true);
        valid = macros.status == PP_CLANG_ACCEPTED;
        FILE* stream = valid ? fopen(predefines, "wx") : 0;
        valid = stream != 0 && fwrite(macros.text, 1, macros.length, stream) == macros.length;
        if (stream != 0)
        {
            valid = fclose(stream) == 0 && valid;
        }
        if (!valid && macros.diagnostics != 0)
        {
            fputs(macros.diagnostics, stderr);
        }
        free(macros.text);
        free(macros.diagnostics);
        options.predefines_path = predefines;
    }
    FILE* report = valid && report_path != 0 ? fopen(report_path, "wx") : 0;
    valid = valid && (report_path == 0 || report != 0);
    FILE* selection = valid && selection_path != 0 ? fopen(selection_path, "wx") : 0;
    valid = valid && (selection_path == 0 || selection != 0);
    if (report != 0)
    {
        fprintf(report, "kind\tinput\tclang_accepted\tscalar_accepted\tvector_accepted\tclang_line\tscalar_line\tvector_line\tpaths_equal\tresource\ttokens\tphase\tfirst_file\tfirst_token\tfirst_column\tspelling\tchecker_message\tclang_message\tclang_file\tdiagnostic_path_equal\n");
    }
    CheckCorpusTally tally = { 0 };
    CheckMutationTally mutations = { 0 };
    CheckCorpusSymbols symbols = {
        .clang = clang,
        .arguments = clang_arguments,
        .argument_count = clang_count,
        .directory = directory,
        .report_limit = report_limit,
    };
    const bool minimized = valid && mutant_count != 0 &&
                           check_mutation_minimize_files(mutants, mutant_count, &options, clang, clang_arguments, clang_count, directory);
    bool* bases = valid ? calloc((size_t)selected + 1, sizeof(bool)) : NULL;
    valid = valid && bases != NULL;
    if (valid && mutant_count == 0)
    {
        printf("checker-corpus: inventory=%u selected=%u random=%" PRIu64 " mutations=%" PRIu64 " semantic_mutations=%" PRIu64
               " seed=%" PRIu64 " standard=%s oracle=%s selection=%s\n", file_count, selected, random_count, mutation_count, semantic_count, seed, standard + 5, clang,
               rejections ? "preprocessed-semantic-rejections" : selection != NULL ? "syntax-accepted" : "all");
        fflush(stdout);
        check_corpus_deferred_lanes = deferred_lanes <= BUSTER_CHECK_DEFERRED_MAX_LANES ? (uint32_t)deferred_lanes : 0;
        check_corpus_deferred_bindings = deferred_bindings;
        check_corpus_symbols = symbols_requested ? &symbols : NULL;
        valid = deferred_lanes <= BUSTER_CHECK_DEFERRED_MAX_LANES &&
                check_corpus_files(&tally, files, selected, &options, clang, clang_arguments, clang_count, report, selection, rejections, report_limit,
                                   bases);
        check_corpus_symbols = NULL;
        valid = valid && check_corpus_generated(&tally, files, selected, random_count, seed, 0, directory, keep, &options, clang,
                                               clang_arguments, clang_count, report, report_limit);
        valid = valid && check_corpus_generated(&tally, files, selected, mutation_count, seed, 1, directory, keep, &options, clang,
                                               clang_arguments, clang_count, report, report_limit);
        valid = valid && (semantic_count == 0 || check_mutation_campaign(&tally, &mutations, files, bases, selected, semantic_count, seed, minimize,
                                                                          directory, keep, &options, clang, clang_arguments, clang_count, report,
                                                                          report_limit));
    }
    else if (!valid)
    {
        fprintf(stderr, "usage: test-checker-corpus [--clang PATH] [--report NEW.tsv] [--select-accepted NEW.list | --semantic-rejections NEW.list] [--files-from LIST --root DIR] [--reports N] [--limit N] [--random N] [--mutations N] [--semantic-mutations N [--minimize N]] [--deferred LANES [--deferred-bindings stacks|batch|direct]] [--symbols] [--symbols-minimum N] [--seed N] [--keep] [preprocessor options] file-or-directory...\n"
                "       test-checker-corpus [--clang PATH] --minimize-mutant FILE... [preprocessor options]\n");
    }
    if (report != 0)
    {
        valid = valid && !ferror(report);
        valid &= fclose(report) == 0;
    }
    if (selection != 0)
    {
        valid = !ferror(selection) && valid;
        valid = fclose(selection) == 0 && valid;
    }
    if (made_directory)
    {
        const int removed = unlink(predefines);
        valid = (removed == 0 || errno == ENOENT) && valid;
        if (rmdir(directory) != 0)
        {
            if (keep && errno == ENOTEMPTY)
            {
                printf("checker-corpus: retained mismatches in %s\n", directory);
            }
            else
            {
                valid = false;
            }
        }
    }
    const bool passed = mutant_count != 0 ? valid && minimized : valid && (tally.files != 0 || rejections) &&
                        tally.files + tally.excluded == selected + random_count + mutation_count + semantic_count + mutations.units &&
                        tally.acceptance_differences == 0 && tally.line_differences == 0 && tally.diagnostic_path_differences == 0 &&
                        tally.path_differences == 0 && tally.resource_failures == 0 && tally.preprocessing_failures == 0 &&
                        symbols.different == 0 && symbols.failures == 0 && symbols.compared >= symbols_minimum;
    if (semantic_count != 0)
    {
        for (uint32_t kind = 0; kind < CHECK_MUTATION_KIND_COUNT; kind += 1)
        {
            printf("checker-corpus: semantic kind=%s sites=%" PRIu64 " mutants=%" PRIu64 " clang_rejected=%" PRIu64 " differences=%" PRIu64 "\n",
                   check_mutation_names[kind], mutations.sites[kind], mutations.kinds[kind], mutations.kind_rejected[kind], mutations.kind_differences[kind]);
        }
        printf("checker-corpus: semantic units=%" PRIu64 " base_differences=%" PRIu64 " unusable=%" PRIu64 " mutants=%" PRIu64 " differences=%" PRIu64
               " intended=%" PRIu64 " minimized=%" PRIu64 " saved=%" PRIu64 "\n", mutations.units, mutations.base_differences, mutations.unusable,
               mutations.mutants, mutations.differences, mutations.intended, mutations.minimized, mutations.saved);
    }
    if (deferred_lanes != 0)
    {
        printf("checker-corpus: deferred lanes=%" PRIu64 " units=%" PRIu64 " source_order=%" PRIu64 " bodies=%" PRIu64 " differences=%" PRIu64 "\n", deferred_lanes,
               tally.deferred_units, tally.deferred_source_order, tally.deferred_bodies, tally.deferred_differences);
        for (uint32_t i = 0; i < tally.deferred_reason_count; i += 1)
        {
            printf("checker-corpus: deferred source_order=%" PRIu64 " reason=\"%s\"\n", tally.deferred_reason_units[i], tally.deferred_reasons[i]);
        }
    }
    if (symbols_requested)
    {
        printf("checker-corpus: symbols compared=%" PRIu64 " minimum=%" PRIu64 " different=%" PRIu64 " unexplained=%" PRIu64 " documented_%s=%" PRIu64
               " documented_%s=%" PRIu64 " documented_%s=%" PRIu64 " documented_%s=%" PRIu64 " unsupported=%" PRIu64 " failures=%" PRIu64 "\n", symbols.compared, symbols_minimum,
               symbols.different, symbols.unexplained, check_corpus_symbol_reasons[CHECK_CORPUS_SYMBOL_FOLDED], symbols.documented[CHECK_CORPUS_SYMBOL_FOLDED],
               check_corpus_symbol_reasons[CHECK_CORPUS_SYMBOL_UNSELECTED], symbols.documented[CHECK_CORPUS_SYMBOL_UNSELECTED],
               check_corpus_symbol_reasons[CHECK_CORPUS_SYMBOL_DROPPED], symbols.documented[CHECK_CORPUS_SYMBOL_DROPPED],
               check_corpus_symbol_reasons[CHECK_CORPUS_SYMBOL_INLINED], symbols.documented[CHECK_CORPUS_SYMBOL_INLINED], symbols.unsupported, symbols.failures);
        for (uint32_t i = 0; i < symbols.feature_count; i += 1)
        {
            printf("checker-corpus: symbols unsupported=%" PRIu64 " feature=%s\n", symbols.feature_counts[i], symbols.features[i]);
        }
    }
    printf("checker-corpus: files=%" PRIu64 " excluded_by_clang_selection=%" PRIu64 " clang_accepted=%" PRIu64 " checker_accepted=%" PRIu64 " acceptance_differences=%" PRIu64
           " first_line_differences=%" PRIu64 " diagnostic_path_differences=%" PRIu64 " path_differences=%" PRIu64 " resources=%" PRIu64 " preprocessing_failures=%" PRIu64
           " tokens=%" PRIu64 " nodes=%" PRIu64 " vector_path_scalar_nodes=%" PRIu64 " vector_nodes=%" PRIu64
           " binding_walked_uses=%" PRIu64 " binding_fallback_uses=%" PRIu64 " preprocessing_classified=%u clang_pp_accepted=%" PRIu64
           " clang_pp_excluded=%" PRIu64 " syntax_accepted_not_compared=%" PRIu64 " result=%s\n",
           tally.files, tally.excluded, tally.oracle_accepted, tally.checker_accepted, tally.acceptance_differences, tally.line_differences, tally.diagnostic_path_differences, tally.path_differences,
           tally.resource_failures, tally.preprocessing_failures, tally.tokens, tally.nodes, tally.scalar_nodes, tally.vector_nodes,
           tally.binding_uses, tally.binding_fallback_uses, rejections, tally.preprocessed, tally.preprocessing_excluded, tally.syntax_accepted_excluded,
           passed ? "passed" : "FAILED");
    for (uint32_t i = 0; i < file_count; i += 1)
    {
        free(files[i]);
    }
    free(bases);
    free(mutants);
    free(clang_arguments);
    free(pp_arguments);
    free(inputs);
    free(files);
    if (fixed_epoch)
    {
        unsetenv("SOURCE_DATE_EPOCH");
    }
    arena_release(&include_paths.arena);
    arena_release(&options_arena);
    oracle_cache_report(oracle_cache(), "test-checker-corpus");
    return passed ? 0 : 1;
}

#endif
