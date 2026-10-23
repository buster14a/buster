// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Independent source-corpus qualification. External files stay external: the harness records every input and
// compares acceptance and the first diagnostic line with Clang, and the complete scalar/vector parser, binding,
// graph and typed results with each other. Unsupported valid files are failures, never successful skips.
// Include after c_checker_test.c for the shared source-oracle and exact-value comparison helpers.
#ifndef BUSTER_C_CHECK_CORPUS_INCLUDED
#define BUSTER_C_CHECK_CORPUS_INCLUDED

#include "c_check_prefix.c"

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
    TypeTable types;
    CheckInput input;
    CheckStream stream;
    CheckInitializers initializers;
    CheckProgram program;
    bool parsed;
    bool adapted;
    bool checked;
    bool initialized;
    bool programmed;
    bool resource;
    uint32_t token;
    uint32_t line;
    uint32_t column;
    const char* phase;
    char file[512];
    char message[256];
    char spelling[96];
};

typedef struct CheckCorpusTally CheckCorpusTally;
struct CheckCorpusTally
{
    uint64_t files;
    uint64_t excluded;
    uint64_t oracle_accepted;
    uint64_t checker_accepted;
    uint64_t acceptance_differences;
    uint64_t line_differences;
    uint64_t path_differences;
    uint64_t resource_failures;
    uint64_t preprocessing_failures;
    uint64_t tokens;
    uint64_t nodes;
    uint64_t scalar_nodes;
    uint64_t vector_nodes;
    uint64_t binding_uses;
    uint64_t binding_fallback_uses;
    uint64_t reports;
};

typedef struct CheckCorpusOracle CheckCorpusOracle;
struct CheckCorpusOracle
{
    bool rejected;
    uint32_t line;
    uint32_t column;
    char file[512];
    char message[256];
};

// The parser oracle intentionally distinguishes syntax errors from later semantic errors. The checker requires
// every first error, including an included header's, and treats process/I/O failures separately from rejection.
// One invocation owns one translation unit. Included files may themselves be corpus inputs, #line may rename a
// location, and fatal include errors can survive -ferror-limit=1; none can misattribute acceptance to another file.
static bool check_corpus_clang_file(const char* executable, const char* const* arguments, uint32_t argument_count,
                                    const char* path, CheckCorpusOracle* result)
{
    *result = (CheckCorpusOracle){ 0 };
    FILE* diagnostics = tmpfile();
    char** argv = calloc((size_t)argument_count + 10, sizeof(char*));
    bool ready = diagnostics != 0 && argv != 0;
    pid_t child = -1;
    int status = 0;
    char* text = 0;
    size_t length = 0;
    if (ready)
    {
        uint32_t used = 0;
        argv[used++] = (char*)executable;
        for (uint32_t argument = 0; argument < argument_count; argument += 1)
        {
            argv[used++] = (char*)arguments[argument];
        }
        argv[used++] = "-fsyntax-only";
        argv[used++] = "-w";
        argv[used++] = "-fno-caret-diagnostics";
        argv[used++] = "-fno-color-diagnostics";
        argv[used++] = "-ferror-limit=1";
        argv[used++] = (char*)path;
        child = fork();
        if (child == 0)
        {
            unsetenv("CPATH");
            unsetenv("C_INCLUDE_PATH");
            const int output = open("/dev/null", O_WRONLY | O_CLOEXEC);
            if (output < 0 || dup2(output, STDOUT_FILENO) < 0 || dup2(fileno(diagnostics), STDERR_FILENO) < 0)
            {
                _exit(126);
            }
            close(output);
            execvp(executable, argv);
            _exit(127);
        }
        pid_t waited = -1;
        do
        {
            waited = child > 0 ? waitpid(child, &status, 0) : -1;
        }
        while (waited < 0 && child > 0 && errno == EINTR);
        ready = child > 0 && waited == child && WIFEXITED(status) && pp_test_read_output(diagnostics, &text, &length);
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
    if (diagnostics != 0)
    {
        ready = fclose(diagnostics) == 0 && ready;
    }
    free(text);
    free(argv);
    return ready;
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

static void check_corpus_location(CheckCorpusPath* path, const Parser* parser, Preprocessor* pp)
{
    if (path->token < parser->tokens.count)
    {
        const uint32_t source = parser->tokens.sources[path->token];
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

static void check_corpus_close(CheckCorpusPath* path)
{
    arena_release(&path->scratch);
    arena_release(&path->output_arena);
    arena_release(&path->input_arena);
    arena_release(&path->index_arena);
    arena_release(&path->types_arena);
    arena_release(&path->prefix_arena);
}

static bool check_corpus_check(CheckCorpusPath* path, Parser* parser, Preprocessor* pp, bool scalar)
{
    path->token = BUSTER_CHECK_NONE;
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
            path->adapted = path->parsed ? check_input(&path->input, view, pp, &path->types, &path->input_arena, &path->scratch) :
                                          check_input_prefix(&path->input, view, pp, &path->types, &path->input_arena, &path->scratch);
            path->resource = path->input.resource;
            for (uint32_t report = 0; report < path->input.report_count; report += 1)
            {
                if (path->input.reports[report].token < path->token)
                {
                    path->token = path->input.reports[report].token;
                    snprintf(path->message, sizeof(path->message), "%s", check_driver_input_message(path->input.reports[report].message));
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
        path->resource |= path->stream.status == CHECK_STATUS_RESOURCE;
        if (path->stream.report_count != 0 && path->stream.reports[0].token < path->token)
        {
            path->phase = "expression";
            path->token = path->stream.reports[0].token;
            snprintf(path->message, sizeof(path->message), "%s", check_driver_report_message(path->stream.reports));
        }
    }
    if (path->adapted && path->checked && path->stream.report_count == 0)
    {
        path->phase = "initializer";
        path->initialized = check_initializers_run(&path->initializers, &path->input, &path->stream, view, &path->types,
                                                   &path->output_arena, &path->scratch);
        path->resource |= path->initializers.status == CHECK_INIT_RESOURCE;
        if (!path->initialized)
        {
            path->token = path->initializers.token;
            snprintf(path->message, sizeof(path->message), "%s", check_driver_initializer_message(path->initializers.status));
        }
    }
    if (path->initialized)
    {
        path->phase = "control";
        const CheckProgramOptions options = {
            .initializers = &path->initializers,
            .identifiers = pp->table,
            .scalar = scalar,
        };
        path->programmed = check_program(&path->program, view, &path->input, &path->stream, &path->types, options,
                                         &path->output_arena, &path->scratch);
        path->resource |= path->program.status == CHECK_PROGRAM_RESOURCE;
        if (!path->programmed)
        {
            path->token = path->program.token;
            snprintf(path->message, sizeof(path->message), "%s", path->program.status == CHECK_PROGRAM_RESOURCE ? "control checking capacity exhausted" :
                     path->program.status == CHECK_PROGRAM_UNSUPPORTED ? "control construct is not supported" : "invalid function control flow");
        }
        else
        {
            path->phase = "accepted";
        }
    }
    path->resource |= !ready;
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
    return path->parsed && path->adapted && path->programmed && !path->resource;
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
    bool equal = x->status == y->status && x->token == y->token && x->count == y->count && x->token_count == y->token_count;
    for (uint32_t token = 0; equal && token < x->token_count; token += 1)
    {
        equal = x->declaration_plans[token] == y->declaration_plans[token];
    }
    for (uint32_t plan = 0; equal && plan < x->count; plan += 1)
    {
        const CheckInitializer* a = x->plans + plan;
        const CheckInitializer* b = y->plans + plan;
        equal = x->declaration_tokens[plan] == y->declaration_tokens[plan] && a->type == b->type && a->count == b->count &&
                a->status == b->status && a->failed_token == b->failed_token;
        for (uint32_t part = 0; equal && part < a->count; part += 1)
        {
            const CheckInitPart* p = a->parts + part;
            const CheckInitPart* q = b->parts + part;
            equal = p->kind == q->kind && p->type == q->type && p->node == q->node && p->token == q->token && p->bit_offset == q->bit_offset &&
                    p->bit_width == q->bit_width && p->byte_count == q->byte_count && p->conversion.type == q->conversion.type &&
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
                x->calling_convention == y->calling_convention && x->packed == y->packed;
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
    bool equal = a->status == b->status && a->token == b->token && a->control_count == b->control_count && a->body_count == b->body_count &&
                 a->case_count == b->case_count && a->region_count == b->region_count && a->reference_count == b->reference_count &&
                 a->object_count == b->object_count && a->symbol_count == b->symbol_count;
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
                x->target == y->target && x->fallback == y->fallback;
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

static bool check_corpus_equal(const CheckCorpusPath* a, const CheckCorpusPath* b, const Parser* x, const Parser* y)
{
    bool equal = a->parsed == b->parsed && a->adapted == b->adapted && a->checked == b->checked && a->initialized == b->initialized &&
                 a->programmed == b->programmed && a->resource == b->resource &&
                 a->line == b->line && a->column == b->column && a->token == b->token && x->tokens.count == y->tokens.count &&
                 x->errors == y->errors && x->fatal == y->fatal && x->diagnostic_count == y->diagnostic_count &&
                 x->tokens.pragma_count == y->tokens.pragma_count && a->stream.status == b->stream.status &&
                 a->stream.failed_node == b->stream.failed_node && (a->prefix == 0) == (b->prefix == 0);
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
                left_parser->postorder[token] == right_parser->postorder[token] && left_parser->sizes[token] == right_parser->sizes[token] &&
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
            left->binding_event_count == right->binding_event_count;
    for (uint32_t report = 0; equal && report < left->report_count; report += 1)
    {
        equal = left->reports[report].token == right->reports[report].token && left->reports[report].message == right->reports[report].message;
    }
    for (uint32_t token = 0; equal && !a->resource && trees && left->declarations != 0 && right->declarations != 0 && token < left_parser->tokens.count; token += 1)
    {
        equal = left->declarations[token] == right->declarations[token] && left->declared_types[token] == right->declared_types[token] &&
                left->declaration_kinds[token] == right->declaration_kinds[token] && left->objects[token] == right->objects[token] &&
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
                left->expression_regions[token] == right->expression_regions[token];
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
    const CheckGraph* graph = &left->graph;
    const CheckGraph* other = &right->graph;
    equal = equal && graph->count == other->count && graph->argument_count == other->argument_count && graph->varargs_count == other->varargs_count &&
            graph->extensions == other->extensions && graph->builtin_count == other->builtin_count &&
            graph->tag_completion_count == other->tag_completion_count && (graph->tag_completions == 0) == (other->tag_completions == 0) &&
            (graph->source_positions == 0) == (other->source_positions == 0);
    for (uint32_t tag = 0; equal && tag < graph->tag_completion_count; tag += 1)
    {
        equal = graph->tag_completions != 0 && graph->tag_completions[tag] == other->tag_completions[tag];
    }
    for (uint32_t node = 0; equal && !a->resource && node < graph->count; node += 1)
    {
        equal = graph->ops[node] == other->ops[node] && graph->tokens[node] == other->tokens[node] && graph->operand0[node] == other->operand0[node] &&
                graph->operand1[node] == other->operand1[node] && graph->operand2[node] == other->operand2[node] && graph->types[node] == other->types[node] &&
                graph->members[node] == other->members[node] && graph->contexts[node] == other->contexts[node] &&
                graph->argument_offsets[node] == other->argument_offsets[node] && graph->argument_counts[node] == other->argument_counts[node];
        equal = equal && (graph->source_positions == 0 || graph->source_positions[node] == other->source_positions[node]);
        if (equal && graph->ops[node] == CHECK_OP_VALUE)
        {
            equal = check_test_value_equal(graph->leaves + node, other->leaves + node);
        }
    }
    for (uint32_t argument = 0; equal && !a->resource && argument < graph->argument_count; argument += 1)
    {
        equal = graph->argument_nodes[argument] == other->argument_nodes[argument];
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
    if (equal && a->checked)
    {
        equal = check_test_stream_equal(&a->stream, &b->stream, graph->argument_count);
    }
    equal = equal && check_corpus_environments_equal(&left->environment, &right->environment) && check_corpus_types_equal(&a->types, &b->types) &&
            check_corpus_initializers_equal(&a->initializers, &b->initializers) &&
            check_corpus_programs_equal(&a->program, &b->program);
    return equal;
}

static bool check_corpus_file(CheckCorpusTally* tally, const char* file, const char* label, const char* kind, const PPOptions* options,
                              const CheckCorpusOracle* oracle, FILE* report, uint64_t report_limit)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parsers[2];
    CheckCorpusPath paths[2] = { 0 };
    bool accepted[2] = { 0 };
    bool prepared = intern_initialize(&identifiers) && pp_initialize(&pp, options, &identifiers);
    prepared = prepared && pp_run(&pp, file);
    for (uint32_t path = 0; prepared && path < 2; path += 1)
    {
        accepted[path] = check_corpus_check(paths + path, parsers + path, &pp, path == 0);
    }
    const bool equal = prepared && check_corpus_equal(paths, paths + 1, parsers, parsers + 1);
    const bool acceptance = prepared && accepted[0] != oracle->rejected;
    const bool line = !oracle->rejected || accepted[0] || paths[0].line == oracle->line;
    const bool resource = paths[0].resource || paths[1].resource;
    tally->files += 1;
    tally->oracle_accepted += !oracle->rejected;
    tally->checker_accepted += accepted[0];
    tally->acceptance_differences += !acceptance;
    tally->line_differences += !line;
    tally->path_differences += prepared && !equal;
    tally->resource_failures += resource;
    tally->preprocessing_failures += !prepared;
    tally->tokens += parsers[0].tokens.count;
    tally->nodes += paths[0].stream.count;
    tally->scalar_nodes += paths[1].stream.statistics.scalar_nodes;
    tally->vector_nodes += paths[1].stream.statistics.vector_nodes;
    tally->binding_uses += paths[1].input.binding_statistics.walked_uses;
    tally->binding_fallback_uses += paths[1].input.binding_statistics.fallback_uses;
    const bool different = !acceptance || !line || !equal || resource;
    if (report != 0)
    {
        fprintf(report, "%s\t", kind);
        check_corpus_field(report, label);
        fprintf(report, "\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%s\t", !oracle->rejected, accepted[0], accepted[1], oracle->line,
                paths[0].line, paths[1].line, equal, resource, parsers[0].tokens.count, prepared ? paths[0].phase : "preprocess");
        check_corpus_field(report, paths[0].file);
        fprintf(report, "\t%u\t%u\t", paths[0].token, paths[0].column);
        check_corpus_field(report, paths[0].spelling);
        fputc('\t', report);
        check_corpus_field(report, paths[0].message);
        fputc('\t', report);
        check_corpus_field(report, oracle->message);
        fputc('\n', report);
    }
    if (different && tally->reports < report_limit)
    {
        fprintf(stderr, "%s: clang=%s:%u checker=%s:%u paths=%s phase=%s token=%u spelling=\"%s\" at %s:%u:%u: %s\n", label,
                oracle->rejected ? "reject" : "accept", oracle->line, accepted[0] ? "accept" : "reject", paths[0].line,
                equal ? "equal" : "DIFFERENT", prepared ? paths[0].phase : "preprocess", paths[0].token, paths[0].spelling,
                paths[0].file, paths[0].line, paths[0].column, paths[0].message);
        tally->reports += 1;
    }
    for (uint32_t path = 0; path < 2; path += 1)
    {
        check_corpus_close(paths + path);
        parse_release(parsers + path);
    }
    pp_release(&pp);
    intern_release(&identifiers);
    return different;
}

static bool check_corpus_files(CheckCorpusTally* tally, char** files, uint32_t count, const PPOptions* options, const char* clang,
                               const char* const* arguments, uint32_t argument_count, FILE* report, FILE* selection, uint64_t report_limit)
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
            if (selection != 0 && outcomes[i].rejected)
            {
                tally->excluded += 1;
                if (report != 0)
                {
                    fputs("excluded\t", report);
                    check_corpus_field(report, file);
                    fprintf(report, "\t0\t0\t0\t%u\t0\t0\t0\t0\t0\tselection\t", outcomes[i].line);
                    check_corpus_field(report, outcomes[i].file);
                    fputs("\t0\t0\t\t\t", report);
                    check_corpus_field(report, outcomes[i].message);
                    fputc('\n', report);
                }
            }
            else
            {
                valid = selection == 0 || fprintf(selection, "%s\n", file) > 0;
                if (valid)
                {
                    check_corpus_file(tally, file, file, "file", options, outcomes + i, report, report_limit);
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

static bool check_corpus_word(const char* text, uint32_t start, uint32_t end, const char* word)
{
    const size_t length = strlen(word);
    return end - start == length && memcmp(text + start, word, length) == 0;
}

static bool check_corpus_leaf(const char* text, uint32_t start, uint32_t end, uint8_t kind)
{
    const bool number = start < end && text[start] >= '0' && text[start] <= '9';
    bool result = number || (kind & 0x7f) == C_TOKEN_RAW_IDENTIFIER;
    for (uint32_t keyword = 1; result && !number && keyword <= C_NAME_KEYWORD_COUNT; keyword += 1)
    {
        result = !check_corpus_word(text, start, end, c_name_spellings[keyword]);
    }
    return result;
}

// Each candidate changes a type, removes one whole simple declaration, or exchanges two scalar operands. The
// surrounding punctuation remains intact; unlike token fuzzing these probe semantic acceptance directly.
static void check_corpus_semantic_candidates(const char* text, const uint8_t* kinds, const uint32_t* starts, const uint32_t* ends,
                                             uint32_t tokens, uint32_t** candidates, uint32_t* counts)
{
    uint32_t depth = 0;
    for (uint32_t i = 0; i < tokens; i += 1)
    {
        const bool integer = check_corpus_word(text, starts[i], ends[i], "int");
        if (integer)
        {
            candidates[0][counts[0]++] = i;
        }
        const bool boundary = i != 0 && (check_corpus_word(text, starts[i - 1], ends[i - 1], ";") ||
                                        check_corpus_word(text, starts[i - 1], ends[i - 1], "{"));
        const bool identifier = i + 1 < tokens && (kinds[i + 1] & 0x7f) == C_TOKEN_RAW_IDENTIFIER &&
                                check_corpus_leaf(text, starts[i + 1], ends[i + 1], kinds[i + 1]);
        if (integer && depth != 0 && boundary && identifier && i + 2 < tokens && check_corpus_word(text, starts[i + 2], ends[i + 2], ";"))
        {
            candidates[1][counts[1]++] = i;
        }
        if (i + 2 < tokens && check_corpus_leaf(text, starts[i], ends[i], kinds[i]) &&
            check_corpus_leaf(text, starts[i + 2], ends[i + 2], kinds[i + 2]))
        {
            const uint32_t bytes = ends[i + 1] - starts[i + 1];
            const bool op = (bytes == 1 && strchr("+-*/%&|^<>", text[starts[i + 1]]) != 0) ||
                            check_corpus_word(text, starts[i + 1], ends[i + 1], "==") ||
                            check_corpus_word(text, starts[i + 1], ends[i + 1], "!=");
            const bool left_number = text[starts[i]] >= '0' && text[starts[i]] <= '9';
            const bool right_number = text[starts[i + 2]] >= '0' && text[starts[i + 2]] <= '9';
            const bool member = i != 0 && (check_corpus_word(text, starts[i - 1], ends[i - 1], ".") ||
                                          check_corpus_word(text, starts[i - 1], ends[i - 1], "->"));
            const bool suffix = i + 3 < tokens && (check_corpus_word(text, starts[i + 3], ends[i + 3], "(") ||
                                                   check_corpus_word(text, starts[i + 3], ends[i + 3], "[") ||
                                                   check_corpus_word(text, starts[i + 3], ends[i + 3], ".") ||
                                                   check_corpus_word(text, starts[i + 3], ends[i + 3], "->"));
            if (op && (left_number || right_number) && !member && !suffix)
            {
                candidates[2][counts[2]++] = i;
            }
        }
        depth += check_corpus_word(text, starts[i], ends[i], "{");
        if (depth != 0 && check_corpus_word(text, starts[i], ends[i], "}"))
        {
            depth -= 1;
        }
    }
}

static char* check_corpus_semantic_mutate(const char* text, size_t length, const uint32_t* starts, const uint32_t* ends,
                                         uint32_t token, uint32_t operation, size_t* result_length)
{
    char* result = malloc(length + 8);
    size_t used = length;
    if (result != 0)
    {
        const uint32_t start = starts[token];
        const uint32_t end = ends[token];
        memcpy(result, text, length);
        if (operation == 0)
        {
            memcpy(result + start, "double", 6);
            memcpy(result + start + 6, text + end, length - end);
            used = start + 6 + length - end;
        }
        else if (operation == 1)
        {
            for (uint32_t i = start; i < ends[token + 2]; i += 1)
            {
                result[i] = text[i] == '\n' ? '\n' : ' ';
            }
        }
        else
        {
            const uint32_t right = starts[token + 2];
            const uint32_t right_end = ends[token + 2];
            memcpy(result + start, text + right, right_end - right);
            memcpy(result + start + right_end - right, text + end, right - end);
            memcpy(result + right_end - end + start, text + start, end - start);
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
    const bool semantic = mode == 2;
    const char* kind = semantic ? "semantic" : mutations ? "mutation" : "random";
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
        uint32_t* candidates[3] = { 0 };
        uint32_t candidate_counts[3] = { 0 };
        for (uint32_t i = 0; valid && semantic && i < 3; i += 1)
        {
            candidates[i] = malloc((size_t)tokens * sizeof(uint32_t));
            valid = candidates[i] != 0;
        }
        if (valid && semantic)
        {
            check_corpus_semantic_candidates(original, kinds, starts, ends, (uint32_t)tokens, candidates, candidate_counts);
        }
        uint64_t remaining = mutations ? (count + file_count - 1) / file_count : count;
        if (mutations && (tokens < 2 || (semantic && (uint64_t)candidate_counts[0] + candidate_counts[1] + candidate_counts[2] == 0)))
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
                    uint32_t operation = semantic ? (uint32_t)((done + b) % 3) : (uint32_t)(parse_test_random(&state) % 3);
                    while (semantic && candidate_counts[operation] == 0)
                    {
                        operation = (operation + 1) % 3;
                    }
                    const uint32_t token = semantic ? candidates[operation][parse_test_random(&state) % candidate_counts[operation]] :
                                                     (uint32_t)(parse_test_random(&state) % (uint64_t)(tokens - 1));
                    size_t length = 0;
                    char* text = semantic ? check_corpus_semantic_mutate(original, source_length, starts, ends, token, operation, &length) :
                                           check_corpus_mutate(original, source_length, starts, ends, token, operation, &length);
                    valid = text != 0 && check_corpus_write(names[b], text, length);
                    const char* operation_name = semantic ? (operation == 0 ? "type-change" : operation == 1 ? "declaration-remove" : "operand-swap") :
                                                           (operation == 0 ? "delete" : operation == 1 ? "duplicate" : "swap");
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
        for (uint32_t i = 0; i < 3; i += 1)
        {
            free(candidates[i]);
        }
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
    uint64_t seed = 133;
    bool keep = false;
    const uint32_t file_capacity = 1u << 20;
    char** files = calloc(file_capacity, sizeof(char*));
    char** inputs = calloc((size_t)count + 1, sizeof(char*));
    const int argument_capacity = count < (INT_MAX - BUSTER_PP_ORACLE_EXTRA_ARGUMENTS) / 2 ? count * 2 + BUSTER_PP_ORACLE_EXTRA_ARGUMENTS : 0;
    char** pp_arguments = calloc((size_t)argument_capacity, sizeof(char*));
    const char** clang_arguments = calloc((size_t)argument_capacity, sizeof(char*));
    uint32_t input_count = 0;
    uint32_t pp_count = 0;
    uint32_t clang_count = 0;
    uint32_t file_count = 0;
    bool valid = argument_capacity != 0 && files != 0 && inputs != 0 && pp_arguments != 0 && clang_arguments != 0;
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
                           strcmp(argument, "--select-accepted") == 0 ||
                           strcmp(argument, "--root") == 0 || strcmp(argument, "--reports") == 0 || strcmp(argument, "--limit") == 0 ||
                           strcmp(argument, "--random") == 0 || strcmp(argument, "--mutations") == 0 || strcmp(argument, "--semantic-mutations") == 0 ||
                           strcmp(argument, "--seed") == 0;
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
                else if (strcmp(argument, "--select-accepted") == 0)
                {
                    selection_path = text;
                }
                else if (strcmp(argument, "--files-from") == 0)
                {
                    manifest = text;
                }
                else if (strcmp(argument, "--root") == 0)
                {
                    root = text;
                }
                else
                {
                    uint64_t* destination = strcmp(argument, "--limit") == 0 ? &limit : strcmp(argument, "--random") == 0 ? &random_count :
                                            strcmp(argument, "--mutations") == 0 ? &mutation_count : strcmp(argument, "--semantic-mutations") == 0 ? &semantic_count :
                                            strcmp(argument, "--seed") == 0 ? &seed : &report_limit;
                    valid = check_corpus_number(text, destination);
                }
            }
        }
        else if (strcmp(argument, "--keep") == 0)
        {
            keep = true;
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
        for (uint32_t i = 0; valid && i < input_count; i += 1)
        {
            const uint32_t before = file_count;
            file_count = parse_test_collect(inputs[i], files, file_count, file_capacity);
            valid = file_count > before && file_count < file_capacity;
        }
        valid = valid && (manifest == 0 || check_corpus_manifest(manifest, root, files, &file_count, file_capacity));
        valid = valid && (file_count != 0 || random_count != 0) && limit != 0 && random_count <= UINT32_MAX && mutation_count <= UINT32_MAX &&
                semantic_count <= UINT32_MAX && ((mutation_count == 0 && semantic_count == 0) || file_count != 0) &&
                (selection_path == 0 || (random_count == 0 && mutation_count == 0 && semantic_count == 0));
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
        fprintf(report, "kind\tinput\tclang_accepted\tscalar_accepted\tvector_accepted\tclang_line\tscalar_line\tvector_line\tpaths_equal\tresource\ttokens\tphase\tfirst_file\tfirst_token\tfirst_column\tspelling\tchecker_message\tclang_message\n");
    }
    CheckCorpusTally tally = { 0 };
    if (valid)
    {
        printf("checker-corpus: inventory=%u selected=%u random=%" PRIu64 " mutations=%" PRIu64 " semantic_mutations=%" PRIu64
               " seed=%" PRIu64 " standard=%s oracle=%s\n", file_count, selected, random_count, mutation_count, semantic_count, seed, standard + 5, clang);
        fflush(stdout);
        valid = check_corpus_files(&tally, files, selected, &options, clang, clang_arguments, clang_count, report, selection, report_limit);
        valid = valid && check_corpus_generated(&tally, files, selected, random_count, seed, 0, directory, keep, &options, clang,
                                               clang_arguments, clang_count, report, report_limit);
        valid = valid && check_corpus_generated(&tally, files, selected, mutation_count, seed, 1, directory, keep, &options, clang,
                                               clang_arguments, clang_count, report, report_limit);
        valid = valid && check_corpus_generated(&tally, files, selected, semantic_count, seed, 2, directory, keep, &options, clang,
                                               clang_arguments, clang_count, report, report_limit);
    }
    else
    {
        fprintf(stderr, "usage: test-checker-corpus [--clang PATH] [--report NEW.tsv] [--select-accepted NEW.list] [--files-from LIST --root DIR] [--reports N] [--limit N] [--random N] [--mutations N] [--semantic-mutations N] [--seed N] [--keep] [preprocessor options] file-or-directory...\n");
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
    const bool passed = valid && tally.files != 0 && tally.files + tally.excluded == selected + random_count + mutation_count + semantic_count &&
                        tally.acceptance_differences == 0 && tally.line_differences == 0 &&
                        tally.path_differences == 0 && tally.resource_failures == 0 && tally.preprocessing_failures == 0;
    printf("checker-corpus: files=%" PRIu64 " excluded_by_clang_selection=%" PRIu64 " clang_accepted=%" PRIu64 " checker_accepted=%" PRIu64 " acceptance_differences=%" PRIu64
           " first_line_differences=%" PRIu64 " path_differences=%" PRIu64 " resources=%" PRIu64 " preprocessing_failures=%" PRIu64
           " tokens=%" PRIu64 " nodes=%" PRIu64 " vector_path_scalar_nodes=%" PRIu64 " vector_nodes=%" PRIu64
           " binding_walked_uses=%" PRIu64 " binding_fallback_uses=%" PRIu64 " result=%s\n",
           tally.files, tally.excluded, tally.oracle_accepted, tally.checker_accepted, tally.acceptance_differences, tally.line_differences, tally.path_differences,
           tally.resource_failures, tally.preprocessing_failures, tally.tokens, tally.nodes, tally.scalar_nodes, tally.vector_nodes,
           tally.binding_uses, tally.binding_fallback_uses, passed ? "passed" : "FAILED");
    for (uint32_t i = 0; i < file_count; i += 1)
    {
        free(files[i]);
    }
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
    return passed ? 0 : 1;
}

#endif
