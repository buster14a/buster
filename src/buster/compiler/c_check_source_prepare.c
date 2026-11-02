// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #1057's shared preparation of a real C file for the clients that measure or test what -c compiles of it:
// benchmark-backend-source, benchmark-check-lanes, benchmark-lowering-source and ir_source_prepare (which
// test-ir-source, benchmark-ir-source, test-ssa-source and the Tier 1 source commands share). Include after
// c_check_corpus.c.
//
// check_source_prepare takes a file through the stages -c takes it through before its code, each at a boundary the
// clients share:
//
// - the request: the options read as the driver reads them and refused as it refuses them (check_request_parse);
// - preprocessing and checking, as the driver checks (the vector path, check_corpus_check_policy), with -w's warnings
//   suppressed as -w suppresses them;
// - the static data (check_data_prepare) and the checked program, whose target, predefines and identifiers are the
//   unit's preprocessor's (check_request_program_options), so that no client compiles another target than -c;
// - with CheckSourceRequest.select, the referenced definitions (C_LOWERING_REACHABLE, check_program_select_mode), and
//   code generation's check of their target features (check_program_target_features), which refuses what -c refuses.
//
// Nothing here is timed: every client times what follows (its emission, its lanes, its lowering), and keeps its own
// report of the stage that stopped a file. The source's state lives in an arena of its own, so the function holds no
// static object and can be an entry point's callee in any unit (ir_source_prepare's), and check_source_release frees
// it whatever stage the file reached. A failed stage leaves the stages after it zero, never a partly made
// configuration, program or selection; CheckSourceRequest's explicit overrides (the scalar path, the tests' injected
// failures) are its only departures from -c's preparation.
#ifndef BUSTER_C_CHECK_SOURCE_PREPARE_INCLUDED
#define BUSTER_C_CHECK_SOURCE_PREPARE_INCLUDED

// The stage that stopped a file (CheckSource.stage), or READY when every stage asked for succeeded. Zero is READY, so
// that a request's zero `fault` injects nothing.
typedef enum CheckSourceStage
{
    CHECK_SOURCE_READY,
    // The options couldn't be read (CheckSource.refusal CHECK_REQUEST_ACCEPTED), or the request was refused as the
    // driver refuses it (its refusal).
    CHECK_SOURCE_OPTIONS,
    // The source's state couldn't be allocated, or the file couldn't be preprocessed.
    CHECK_SOURCE_PREPROCESS,
    // The checker rejected the file, with its diagnostic in CheckSource.path (file, line, column, message), or ran out
    // of a resource (path.resource).
    CHECK_SOURCE_CHECK,
    // The static data or the checked program couldn't be prepared: the program's status CHECK_PROGRAM_UNSUPPORTED is a
    // refusal by name, as -c refuses it; any other is a failure.
    CHECK_SOURCE_PROGRAM,
    // The referenced definitions couldn't be selected.
    CHECK_SOURCE_SELECT,
    // Code generation's check of the selected definitions' target features refused them, as -c does
    // (CheckSource.target_feature_errors of them), or ran out of memory (none).
    CHECK_SOURCE_TARGET_FEATURES,
} CheckSourceStage;

// What a client asks of the preparation, beside its options.
typedef struct CheckSourceRequest CheckSourceRequest;
struct CheckSourceRequest
{
    // Select the referenced definitions and check their target features, as -c does before its code.
    bool select;
    // The client emits code: the request's code generation refusals apply (-fPIC), as they do to -c and a link.
    bool emits;
    // --scalar's override: the checker's, the data's and the selection's scalar twins, and the program's scalar path.
    bool scalar;
    // The tests' injected failure: the stage it stops the file at (CHECK or PROGRAM; READY for none), as a real failure
    // there would, the checker's running out of a resource, or the program's with `fault_unsupported` a refusal by name
    // and otherwise a resource's.
    CheckSourceStage fault;
    bool fault_unsupported;
};

// A file prepared as -c prepares it (check_source_prepare): its request, its state and what each stage made, until
// check_source_release.
typedef struct CheckSource CheckSource;
struct CheckSource
{
    // The file's own memory: the intern table, the preprocessor and the parser, and the options' strings and arrays,
    // which the preprocessor's predefines (and so the program's) borrow.
    Arena arena;
    InternTable* identifiers;
    Preprocessor* pp;
    Parser* parser;
    // The request as read (zero unless it was read and accepted), and why it was refused, if it was.
    PPOptions options;
    CheckRequestRefusal refusal;
    const char* input;
    CheckCorpusPath path;
    CheckDataPrepared data;
    CheckProgram program;
    CheckProgramSelection selected;
    CheckSourceStage stage;
    uint32_t target_feature_errors;
};

// Prepares `file` read with `options` (-E's, `option_count` of them) as `request` asks into `*source`, which
// check_source_release must free whatever this returns: whether every stage asked for succeeded (CheckSource.stage
// READY); else the stage that stopped it.
static bool check_source_prepare(CheckSource* source, const char* file, char** options, uint32_t option_count, CheckSourceRequest request)
{
    memset(source, 0, sizeof(*source));
    source->arena = arena_reserve(UINT64_C(1) << 24);
    source->identifiers = BUSTER_ARENA_PUSH_ARRAY(&source->arena, InternTable, 1);
    source->pp = BUSTER_ARENA_PUSH_ARRAY(&source->arena, Preprocessor, 1);
    source->parser = BUSTER_ARENA_PUSH_ARRAY(&source->arena, Parser, 1);
    const bool allocated = source->identifiers != 0 && source->pp != 0 && source->parser != 0;
    const bool requested = allocated && check_request_parse(&source->options, &source->refusal, &source->input, file, options, option_count, request.emits, &source->arena);
    Preprocessor* pp = source->pp;
    Parser* parser = source->parser;
    CheckCorpusPath* path = &source->path;
    const bool preprocessed = requested && intern_initialize(source->identifiers) && pp_initialize(pp, &source->options, source->identifiers) && pp_run(pp, source->input);
    bool accepted = preprocessed && check_corpus_check_policy(path, parser, pp, request.scalar, source->options.quiet);
    if (accepted && request.fault == CHECK_SOURCE_CHECK)
    {
        accepted = false;
        path->resource = true;
    }
    bool prepared = accepted && check_data_prepare(&source->data, &path->input, &path->stream, &path->initializers, &path->types, parser->options.scalar,
                                                   &path->output_arena, &path->scratch);
    // A client that emits code passes the prepared data's declarations, as -c and a link pass theirs.
    const CheckProgramOptions program_options = prepared ? check_request_program_options(pp, (CheckProgramOptions){
        .initializers = &path->initializers,
        .asms = &path->asms,
        .prepared_data = request.emits ? &source->data : NULL,
        .initializer_references = source->data.references,
        .initializer_reference_count = source->data.reference_count,
        .scalar = request.scalar,
    }) : (CheckProgramOptions){ 0 };
    prepared = prepared && check_program(&source->program, parser, &path->input, &path->stream, &path->types, program_options, &path->output_arena, &path->scratch);
    if (prepared && request.fault == CHECK_SOURCE_PROGRAM)
    {
        prepared = false;
        source->program.status = request.fault_unsupported ? CHECK_PROGRAM_UNSUPPORTED : CHECK_PROGRAM_RESOURCE;
    }
    const bool selected = prepared && (!request.select || check_program_select_mode(&source->selected, &source->program, &source->data, C_LOWERING_REACHABLE,
                                                                                   request.scalar, &path->output_arena, &path->scratch));
    // -c's check of the selected definitions' target features (the driver's check_driver_target_features), its errors
    // counted rather than printed; its output popped, as the driver pops it.
    const uint64_t mark = path->output_arena.length;
    CheckTargetFeatureError* errors = 0;
    const bool checked = selected && (!request.select || check_program_target_features(&source->selected.program, &pp->options, &pp->options.predefines, request.scalar,
                                                                                       &path->output_arena, &path->scratch, &errors, &source->target_feature_errors));
    arena_pop_to(&path->output_arena, mark);
    const bool ready = checked && source->target_feature_errors == 0;
    source->stage = ready ? CHECK_SOURCE_READY : !requested && allocated ? CHECK_SOURCE_OPTIONS : !preprocessed ? CHECK_SOURCE_PREPROCESS :
                    !accepted ? CHECK_SOURCE_CHECK : !prepared ? CHECK_SOURCE_PROGRAM : !selected ? CHECK_SOURCE_SELECT : CHECK_SOURCE_TARGET_FEATURES;
    // A stage that failed leaves the ones after it as they were made: zero.
    if (!prepared)
    {
        source->selected = (CheckProgramSelection){ 0 };
    }
    return ready;
}

// Whether the file's stopping stage is the client's failure rather than a result: a file -c would refuse (its
// checker's rejection, a refusal by name, its target features') is left out of a measurement, but one that doesn't
// preprocess, or where a resource ran out, fails it. A refused request is the command's error, as the driver's is.
static bool check_source_failed(const CheckSource* source)
{
    const CheckSourceStage stage = source->stage;
    return stage == CHECK_SOURCE_OPTIONS || stage == CHECK_SOURCE_PREPROCESS || (stage == CHECK_SOURCE_CHECK && source->path.resource) ||
           (stage == CHECK_SOURCE_PROGRAM && source->program.status != CHECK_PROGRAM_UNSUPPORTED) || stage == CHECK_SOURCE_SELECT ||
           (stage == CHECK_SOURCE_TARGET_FEATURES && source->target_feature_errors == 0);
}

// What stopped the file, for a client's report, as `prefix: file=FILE failed="..."` or `... rejected=...`,
// `unsupported=...` and `refused=...`: the stage's own words, the checker's diagnostic or the request's refusal, in the
// form the clients printed before they shared the preparation.
static void check_source_report(FILE* stream, const char* prefix, const char* file, const CheckSource* source)
{
    const CheckCorpusPath* path = &source->path;
    switch (source->stage)
    {
        case CHECK_SOURCE_READY:
            break;
        case CHECK_SOURCE_OPTIONS:
            if (source->refusal.error != CHECK_REQUEST_ACCEPTED)
            {
                fprintf(stream, "%s: file=%s refused=\"", prefix, file);
                check_request_describe(stream, source->refusal);
                fprintf(stream, "\"\n");
            }
            else
            {
                fprintf(stream, "%s: file=%s failed=\"its options couldn't be read\"\n", prefix, file);
            }
            break;
        case CHECK_SOURCE_PREPROCESS:
            fprintf(stream, "%s: file=%s failed=\"it couldn't be preprocessed\"\n", prefix, file);
            break;
        case CHECK_SOURCE_CHECK:
            if (path->resource)
            {
                fprintf(stream, "%s: file=%s failed=\"the checker ran out of a resource\"\n", prefix, file);
            }
            else
            {
                fprintf(stream, "%s: file=%s rejected=\"%s:%u:%u: %s\"\n", prefix, file, path->file, path->line, path->column, path->message);
            }
            break;
        case CHECK_SOURCE_PROGRAM:
            fprintf(stream, "%s: file=%s %s\n", prefix, file,
                    source->program.status == CHECK_PROGRAM_UNSUPPORTED ? "unsupported=\"its checked program: an unsupported C statement\"" :
                                                                          "failed=\"its static data or checked program couldn't be prepared\"");
            break;
        case CHECK_SOURCE_SELECT:
            fprintf(stream, "%s: file=%s failed=\"its definitions couldn't be selected\"\n", prefix, file);
            break;
        case CHECK_SOURCE_TARGET_FEATURES:
            if (source->target_feature_errors != 0)
            {
                fprintf(stream, "%s: file=%s rejected=\"code generation's target-feature check: %u errors, as -c reports them\"\n", prefix, file,
                        source->target_feature_errors);
            }
            else
            {
                fprintf(stream, "%s: file=%s failed=\"its target features couldn't be checked\"\n", prefix, file);
            }
            break;
    }
}

// Frees everything `source` holds, whatever stage check_source_prepare reached; the arenas a client gave its stages
// after it are the client's.
static void check_source_release(CheckSource* source)
{
    check_corpus_close(&source->path);
    if (source->parser != 0)
    {
        parse_release(source->parser);
    }
    if (source->pp != 0)
    {
        pp_release(source->pp);
    }
    if (source->identifiers != 0)
    {
        intern_release(source->identifiers);
    }
    arena_release(&source->arena);
    memset(source, 0, sizeof(*source));
}

#endif
