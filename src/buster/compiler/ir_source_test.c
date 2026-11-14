// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #131's acceptance on real C (test-ir-source): the IR lowered from every function the driver's -c compiles of a set of
// files, by both of ir_lower.c's paths, which must agree column for column and verify, each function then promoted by
// both of ssa_ir.c's paths, whose results must verify with dominance (#134's use of the verifier). Each lowered and
// both promoted forms must then round-trip through text by both parsers, preserving structure and canonical bytes
// (#175), and must agree on raw-IR dataflow, including phi edges (#143). Include after ir_lower_test.c, ir_parse_test.c,
// ssa_ir.c, dataflow.c and tier0_source_benchmark.c.
//
//   build/compiler test-ir-source [--simd] [--print NAME] [--dominators] [--dominators-cfg NEW_FILE] [--dataflow-benchmark] [preprocessor options] file...
//   build/compiler test-ir-source --dataflow-self-test
//
// Each file is checked and its definitions selected and emitted as benchmark-backend-source has them
// (ir_source_prepare, which benchmark-ir-source shares), their typed streams in an arena that outlives the file's other
// state. A function's semantic result type, which the typed stream's ABI record doesn't give, is its RETURN entries'
// class, or for a function with none its declared result's (ir_source_result). A file the checker rejects, or whose
// checked program or emission refuses a feature by name, is left out with the reason, as -c refuses it; any other
// failure of the file fails the command. So does a function either lowering refuses, that the paths lower apart, or
// that either promotion refuses, or whose text changes meaning: the report names each failure. --print NAME prints the IR of the
// function of that name, as lowered and as promoted. Before the files, the first is prepared again in a child whose
// address space can't grow by the preparation's arena, which must fail it as the memory's, touching nothing it couldn't
// allocate (ir_source_exhausted). --simd instead checks the bounded vector adapter and text forms, and requires a
// function with vectors to promote (#141) as a function whose only SIMD values are masks does (#179), counting the
// former: a file of vectors none of which promote fails.
// --dominators additionally checks each form against #174's scalar/vector/AUTO analysis and its independent
// definition oracle; unlike the ordinary command, no excluded file is a pass. --dominators-cfg NEW_FILE implies
// that check and exports the qualified CFGs to an exclusively created file. Only a complete successful corpus
// receives the completion record; failed writes fail the command. This is a test outside the compile path, with
// no vector path of its own.

#include <sys/resource.h>
#include <sys/wait.h>

#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
#include "ir_source_roundtrip.c"
#include "ir_source_dominators.c"
#endif
#include "ir_dataflow.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
#include "ir_dataflow_test.c"
#include "ir_dataflow_benchmark.c"
#endif

// ir_source_prepare's linkage (main.c's BUSTER_IR_SOURCE_ENTRY): main.c's units compile it in the checker's, its home,
// whose check and emission it runs. The backend's IR commands (#784) read its declaration and call that home; a unit
// of its own has it static.
#ifndef BUSTER_IR_SOURCE_ENTRY
#define BUSTER_IR_SOURCE_ENTRY static
#endif

// The functions -c compiles of one file, as ir_source_prepare leaves them: each one's typed stream (in the caller's
// `lowered` arena), its semantic result type, its symbol bound (Tier0Function.symbol_count, the emission's link's
// symbols), its name and symbol, and the file's tokens (the parser's: those that reach it after preprocessing). With
// the program kept (ir_source_prepare's `keep_program`), also the symbols of its labels whose addresses the program
// takes, in the order of its Tier0Function.label_entries (and so of the IR's IrFunction.labels), or null for none.
typedef struct IrSourceFunction IrSourceFunction;
struct IrSourceFunction
{
    const Tier0Function* function;
    const uint8_t* name;
    const SymbolId* labels;
    uint32_t name_length;
    uint32_t symbol_count;
    SymbolId symbol;
    IrType result_type;
    uint64_t target_features;
};

typedef enum IrSourceStatus
{
    IR_SOURCE_PREPARED,
    // The file is left out: the checker rejects it, or its checked program or emission refuses a feature by name.
    IR_SOURCE_LEFT_OUT,
    // The file failed: it doesn't preprocess, a resource runs out, an emission fails otherwise.
    IR_SOURCE_FAILED,
} IrSourceStatus;

typedef struct IrSourceFile IrSourceFile;
struct IrSourceFile
{
    IrSourceFunction* functions;
    // With the program kept, for a run of it (#134's test-ssa-source): the emission's link, its columns and names in
    // `lowered` as the emission left them, and its atoms' bytes copied there from the arenas the file's end releases;
    // and each of its symbols' sizes as the program declares them (0 for a function, an incomplete type, or a symbol
    // that is the emission's own), by which an object the program takes from a library is known.
    Link* link;
    uint64_t* sizes;
    uint32_t count;
    uint64_t tokens;
    IrSourceStatus status;
    // What left the file out or failed it, for the report (a static string, or the checker's message, copied).
    char reason[256];
};

#if !defined(BUSTER_UNIT) || BUSTER_UNIT == BUSTER_UNIT_CHECKER
// The semantic result type of body `b`'s function: its RETURN entries' class (each RETURN of a function returns its
// class: tier0_validate), which for a struct or a union is the address of its bytes (U64) or nothing (an empty one's);
// or for a function no RETURN leaves, its declared result's class, an aggregate's taken as its address's.
static IrType ir_source_result(const Tier0Function* function, const CheckProgram* program, uint32_t b, bool naked)
{
    const uint8_t class = check_program_result_class(function, program, b, naked);
    return class == UINT8_MAX ? IR_VOID : (IrType)ir_typed_types[class];
}

// Prepares, selects and emits `file` read with `options` as benchmark-backend-source does (backend_source_file), into
// `*result`: its functions' streams in `lowered`, their table and names in `arena`. Both arenas keep what they hold
// for the caller; everything else the file needed is released. With `keep_program`, the program is kept for a run of
// it too: the emission's link and its symbols' sizes (IrSourceFile.link and .sizes), and its functions' labels
// (IrSourceFunction.labels). With `lifetimes`, the program gives its blocks' objects' lifetimes, as the C driver's
// --tier1 asks for them (#177), and the IR carries them as lifetime markers, which promotion reads (#141).
BUSTER_IR_SOURCE_ENTRY void ir_source_prepare(IrSourceFile* result, const char* file, char** options, uint32_t option_count, Arena* lowered, Arena* arena, bool keep_program, bool lifetimes)
{
    memset(result, 0, sizeof(*result));
    // The file prepared as -c prepares it (c_check_source_prepare.c, #1057), as the source benchmarks prepare theirs:
    // its options read and refused as the driver reads and refuses them, checked as the driver checks it (the vector
    // path), its checked program made with the options' target and the predefines its functions' target attributes
    // resolve against (CheckProgram.body_targets, IrSourceFunction.target_features), its referenced definitions
    // selected and their target features checked. The source's state lives in an arena of its own that the file's end
    // releases: an entry point, which main.c's other units call through its declaration, holds no static object.
    CheckSource source;
    const bool chosen = check_source_prepare(&source, file, options, option_count, (CheckSourceRequest){ .select = true, .emits = true, .lifetimes = lifetimes, .asm_helpers = true });
    const bool allocated = source.pp != 0;
    const bool valid = source.stage != CHECK_SOURCE_OPTIONS;
    const bool preprocessed = valid && source.stage != CHECK_SOURCE_PREPROCESS;
    const bool accepted = preprocessed && source.stage != CHECK_SOURCE_CHECK;
    const bool exhausted = preprocessed && !accepted && source.path.resource;
    const bool prepared = accepted && source.stage != CHECK_SOURCE_PROGRAM;
    const bool unsupported = accepted && !prepared && source.program.status == CHECK_PROGRAM_UNSUPPORTED;
    // -c's target-feature errors leave the file out, as -c refuses it; the check's running out of memory fails it.
    const bool features = prepared && source.stage == CHECK_SOURCE_TARGET_FEATURES && source.target_feature_errors != 0;
    const Parser* parser = source.parser;
    const InternTable* identifiers = allocated ? source.pp->table : 0;
    CheckCorpusPath* path = &source.path;
    const CheckProgramSelection* selected = &source.selected;
    Arena bytes = arena_reserve(UINT64_C(1) << 34);
    CheckProgramCode code = { .status = CHECK_PROGRAM_RESOURCE, .token = BUSTER_CHECK_NONE };
    const bool emitted = chosen && bytes.base != 0 && check_program_emit(&code, &selected->program, parser, identifiers, selected->data.capacity,
                                                                         BUSTER_SCALAR_PATH(source.scalar), lowered, &bytes, &path->scratch, NULL, NULL, false,
                                                                         false);
    const bool refused = chosen && !emitted && code.status == CHECK_PROGRAM_UNSUPPORTED;
    uint8_t* defined = 0;
    const bool accessed = emitted && check_program_access(&code, &selected->program, lowered, &path->scratch, &defined);
    const uint32_t count = accessed ? selected->program.body_count : 0;
    result->functions = accessed ? BUSTER_ARENA_PUSH_ARRAY(arena, IrSourceFunction, (uint64_t)count + 1) : 0;
    bool named = result->functions != 0;
    for (uint32_t b = 0; named && b < count; b += 1)
    {
        const Tier0Function* function = &code.lowered.functions[b];
        const bool naked = code.lowered.asms.naked != 0 && code.lowered.asms.naked[b].planned;
        const SymbolId symbol = selected->program.object_symbols[selected->program.bodies[b].object];
        const NameId name = code.link.symbols.name[symbol];
        const uint32_t length = code.link.names.length[name];
        uint8_t* spelling = arena_push(arena, (uint64_t)length + 1, 1);
        named = spelling != 0;
        if (named)
        {
            memcpy(spelling, code.link.names.bytes[name], length);
            spelling[length] = 0;
            const uint32_t body_target = selected->program.body_targets != 0 ? selected->program.body_targets[b] : 0;
            const X86Target target = body_target == 0 || body_target == BUSTER_CHECK_PROGRAM_TARGET_VERSIONS ? selected->program.target : selected->program.targets[body_target - 1];
            result->functions[b] = (IrSourceFunction){
                .function = function,
                .name = spelling,
                .name_length = length,
                .symbol_count = function->symbol_count != 0 ? function->symbol_count : code.link.symbols.count,
                .symbol = symbol,
                .result_type = ir_source_result(function, &selected->program, b, naked),
                .target_features = target.features,
            };
        }
    }
    // The program, for a run of it: its static data published into the link as the driver publishes it after the
    // emission (check_data_publish), and its aliases' symbols defined at their definitions' (#147,
    // check_program_define_aliases), as every caller that publishes the data defines them; the link's atoms' bytes,
    // which the emission and the data left in arenas the file's end releases, copied beside the link's columns, which
    // the emission made in `lowered`; and each function's labels' symbols, the emission's
    // (CheckProgramLowered.label_symbols), each function's after the last's. An ifunc, whose resolver a run would have
    // to call where its symbol is called, which the IR's interpreter doesn't, leaves the file out.
    bool ifunc = false;
    for (uint32_t k = 0; keep_program && named && k < selected->program.alias_count; k += 1)
    {
        ifunc |= selected->program.aliases[k].kind == CHECK_PROGRAM_IFUNC;
    }
    named = named && !ifunc;
    bool kept = true;
    if (keep_program && named)
    {
        CheckDataResult published = { 0 };
        kept = check_data_publish(&selected->data, &path->types, selected->program.stream, selected->program.object_symbols, selected->program.object_count, &code.link, 0,
                                  &bytes, &path->scratch, &published) &&
               check_program_define_aliases(&code, &selected->program);
        Link* link = kept ? BUSTER_ARENA_PUSH_ARRAY(arena, Link, 1) : 0;
        const uint32_t label_count = code.lowered.label_symbol_count;
        SymbolId* labels = kept && label_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, SymbolId, label_count) : 0;
        uint64_t* sizes = kept ? BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, code.link.symbols.count) : 0;
        kept = link != 0 && (label_count == 0 || labels != 0) && sizes != 0;
        if (kept)
        {
            *link = code.link;
            if (label_count != 0)
            {
                memcpy(labels, code.lowered.label_symbols, (uint64_t)label_count * sizeof(SymbolId));
            }
            // The program's symbols come first in the link, symbol s + 1 its symbol s's (check_program_emit).
            memset(sizes, 0, (uint64_t)code.link.symbols.count * sizeof(uint64_t));
            const CheckProgram* checked = &selected->program;
            for (uint32_t s = 0; s < checked->symbol_count && s + 1 < code.link.symbols.count; s += 1)
            {
                const uint32_t declaration = checked->object_declarations[checked->symbol_objects[s]];
                if (declaration != BUSTER_CHECK_NONE && checked->input->declaration_kinds[declaration] != C_BINDING_FUNCTION)
                {
                    sizes[s + 1] = type_size(checked->types, checked->input->declared_types[declaration]);
                }
            }
            result->sizes = sizes;
        }
        for (AtomId atom = 1; kept && atom < link->atoms.count; atom += 1)
        {
            const uint64_t size = link->atoms.size[atom];
            if (link->atoms.bytes[atom] != 0 && size != 0)
            {
                uint8_t* copy = arena_push(lowered, size, (uint64_t)1 << link->atoms.alignment_log2[atom]);
                kept = copy != 0;
                if (kept)
                {
                    link->atoms.bytes[atom] = memcpy(copy, link->atoms.bytes[atom], size);
                }
            }
        }
        uint32_t first = 0;
        for (uint32_t b = 0; kept && b < count; b += 1)
        {
            const uint32_t taken = result->functions[b].function->label_count;
            kept = taken <= label_count - first;
            result->functions[b].labels = kept && taken != 0 ? labels + first : 0;
            first += kept ? taken : 0;
        }
        kept = kept && first == label_count;
        result->link = kept ? link : 0;
    }
    named = named && kept;
    result->count = named ? count : 0;
    result->tokens = accepted ? parser->tokens.count : 0;
    const bool failed = !valid || !preprocessed || exhausted || (accepted && !prepared && !unsupported) || (prepared && !chosen && !features) ||
                        (chosen && !emitted && !refused) || (emitted && !named && !ifunc);
    result->status = failed ? IR_SOURCE_FAILED : named ? IR_SOURCE_PREPARED : IR_SOURCE_LEFT_OUT;
    if (!allocated)
    {
        snprintf(result->reason, sizeof(result->reason), "memory ran out for its preprocessor and parser");
    }
    else if (!valid && source.refusal.error != CHECK_REQUEST_ACCEPTED)
    {
        check_request_describe_text(&source.refusal, result->reason, sizeof(result->reason));
    }
    else if (!valid)
    {
        snprintf(result->reason, sizeof(result->reason), "its options couldn't be read");
    }
    else if (!preprocessed)
    {
        snprintf(result->reason, sizeof(result->reason), "it couldn't be preprocessed");
    }
    else if (exhausted)
    {
        snprintf(result->reason, sizeof(result->reason), "the checker ran out of a resource");
    }
    else if (!accepted)
    {
        snprintf(result->reason, sizeof(result->reason), "rejected: %s:%u:%u: %s", path->file, path->line, path->column, path->message);
    }
    else if (!prepared)
    {
        snprintf(result->reason, sizeof(result->reason), "%s",
                 unsupported ? "its checked program has an unsupported C statement" : "its static data or checked program couldn't be prepared");
    }
    else if (features)
    {
        snprintf(result->reason, sizeof(result->reason), "rejected: code generation's target-feature check: %u errors, as -c reports them",
                 source.target_feature_errors);
    }
    else if (!chosen)
    {
        snprintf(result->reason, sizeof(result->reason), "%s", source.stage == CHECK_SOURCE_SELECT ? "its definitions couldn't be selected" :
                                                                                                        "its definitions' target features couldn't be checked");
    }
    else if (!emitted)
    {
        snprintf(result->reason, sizeof(result->reason), "%s: %s", refused ? "unsupported" : "its emission failed", code.reason != NULL ? code.reason : "this C type or operation");
    }
    else if (ifunc)
    {
        snprintf(result->reason, sizeof(result->reason), "unsupported: an ifunc in a program to run, whose resolver the IR's interpreter doesn't call");
    }
    else if (!named)
    {
        snprintf(result->reason, sizeof(result->reason), "%s", kept ? "memory ran out for its functions' names" : "its program couldn't be kept for a run");
    }
    arena_release(&bytes);
    check_source_release(&source);
}
#else
__attribute__((noinline)) void ir_source_prepare(IrSourceFile* result, const char* file, char** options, uint32_t option_count, Arena* lowered, Arena* arena, bool keep_program, bool lifetimes);
#endif

// Only the backend's commands need the fixtures; the preparation above also serves the checker's code certificate.
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)

// What the command found, over every file.
typedef struct IrSourceTotals IrSourceTotals;
struct IrSourceTotals
{
    uint64_t files;
    uint64_t left_out;
    uint64_t failed_files;
    uint64_t functions;
    uint64_t entries;
    uint64_t tokens;
    uint64_t instructions;
    uint64_t bytes;
    uint64_t promoted_instructions;
    uint64_t promoted_bytes;
    // Functions a lowering refused, that the two paths lowered apart, and that a promotion refused.
    uint64_t refused;
    uint64_t disagreed;
    uint64_t unpromoted;
    uint64_t simd_promoted;
    // Representations checked through both text parser paths, and representations whose round trip failed.
    uint64_t text_forms;
    uint64_t text_failed;
    uint64_t dominator_forms;
    uint64_t dominator_checks;
    uint64_t dominator_failed;
    // Every accepted source form, not a selected subset of operations or functions.
    uint64_t dataflow_forms;
    uint64_t dataflow_failed;
    uint64_t dataflow_checks;
    uint64_t dataflow_benchmark_failed;
    // Forms whose use lists both twins built (#1634), and forms where they disagreed.
    uint64_t uses_forms;
    uint64_t uses_failed;
};

static const char* const ir_source_errors[IR_ERROR_UNSUPPORTED + 1] = {
    "ok",
    "limit",
    "memory",
    "structure",
    "opcode",
    "type",
    "operand",
    "metadata",
    "terminator",
    "phi",
    "dominance",
    "io",
    "unsupported",
};

// #1634: both use-list twins on one of a source function's verified forms, which must agree use for use.
static bool ir_source_uses_twins(const IrFunction* function, Arena* arena, Arena* scratch)
{
    const uint64_t mark = arena->length;
    IrUses vector = { 0 };
    IrUses scalar = { 0 };
    const IrError vector_error = ir_uses_validated(function, arena, scratch, &vector);
    const IrError scalar_error = ir_uses_validated_scalar(function, arena, scratch, &scalar);
    const bool same = vector_error == IR_OK && scalar_error == IR_OK && vector.use_count == scalar.use_count && vector.value_count == scalar.value_count &&
                      memcmp(vector.offsets, scalar.offsets, ((uint64_t)vector.value_count + 1) * sizeof(uint32_t)) == 0 &&
                      (vector.use_count == 0 || memcmp(vector.uses, scalar.uses, (uint64_t)vector.use_count * sizeof(IrUse)) == 0);
    arena_pop_to(arena, mark);
    return same;
}

// Lowers, compares and promotes each of `file`'s functions, adding to `totals`, and prints the file's line and a line
// per function that fails. `print` names a function to print the IR of, or is null.
// Each lowering goes into its own arena and each promotion into another, which mustn't hold its input (ssa_ir.c).
static void ir_source_check(IrSourceTotals* totals, const IrSourceFile* file, const char* name, const char* print, bool allow_simd, bool dominators, FILE* graphs, Arena* output,
                            Arena* twin_output, Arena* promoted_output, Arena* text_output, Arena* scratch, bool benchmark_dataflow, Tier0BenchmarkCounters counters)
{
    uint64_t entries = 0;
    uint64_t instructions = 0;
    uint64_t bytes = 0;
    uint32_t failures = 0;
    for (uint32_t f = 0; f < file->count; f += 1)
    {
        const IrSourceFunction* source = &file->functions[f];
        const uint64_t output_mark = output->length;
        const uint64_t twin_mark = twin_output->length;
        const uint64_t promoted_mark = promoted_output->length;
        IrFunction vector = { 0 };
        IrFunction scalar = { 0 };
        const IrCheck vector_check = ir_lower_typed_target(source->function, source->result_type, source->symbol_count, source->target_features, output, scratch, &vector);
        const IrCheck scalar_check = ir_lower_typed_target_scalar(source->function, source->result_type, source->symbol_count, source->target_features, twin_output, scratch, &scalar);
        const bool lowered = vector_check.error == IR_OK && scalar_check.error == IR_OK;
        const bool agree = lowered && ir_lower_test_equal(&vector, &scalar);
        IrFunction promoted = { 0 };
        IrFunction promoted_twin = { 0 };
        const IrCheck promotion = agree ? ssa_ir_promote(&vector, promoted_output, scratch, &promoted) : (IrCheck){ .error = IR_ERROR_STRUCTURE };
        const IrCheck promotion_twin = agree ? ssa_ir_promote_scalar(&scalar, promoted_output, scratch, &promoted_twin) : (IrCheck){ .error = IR_ERROR_STRUCTURE };
        const bool promotes = agree && promotion.error == IR_OK && promotion_twin.error == IR_OK && promotion.dominance_checked && promotion_twin.dominance_checked;
        // --simd counts the functions with vectors that promote (#141), as every function must.
        totals->simd_promoted += allow_simd && promotes && ir_function_has_vectors(&vector);
        totals->refused += !lowered;
        totals->disagreed += lowered && !agree;
        totals->unpromoted += agree && !promotes;
        entries += source->function->count;
        if (agree)
        {
            instructions += vector.instruction_count;
            bytes += vector.arena_bytes;
        }
        if (promotes)
        {
            totals->promoted_instructions += promoted.instruction_count;
            totals->promoted_bytes += promoted.arena_bytes;
        }
        bool text_valid = true;
        bool dominators_valid = true;
        const IrFunction* const forms[] = {
            &vector,
            &promoted,
            &promoted_twin,
        };
        static const char* const form_names[] = {
            "lowered",
            "promoted-vector",
            "promoted-scalar",
        };
        for (uint32_t form = 0; agree && form < (promotes ? 3u : 1u); form += 1)
        {
            IrSourceTextCheck check = { 0 };
            const bool roundtrip = ir_source_text_roundtrip(forms[form], text_output, scratch, &check);
            totals->text_forms += 1;
            totals->text_failed += !roundtrip;
            text_valid = text_valid && roundtrip;
            if (!roundtrip)
            {
                printf("ir-source: file=%s function=%s form=%s text_failed=\"%s\" parse_error=%u line=%" PRIu64 " column=%" PRIu64 " ir_error=%u instruction=%u detail=%u\n",
                       name, source->name, form_names[form], check.reason, (unsigned)check.parsed.error,
                       (uint64_t)check.parsed.line, (uint64_t)check.parsed.column, (unsigned)check.parsed.check.error,
                       check.parsed.check.instruction, check.parsed.check.detail);
            }
            if (dominators)
            {
                IrSourceDominatorCheck dominance = { 0 };
                const bool qualified = ir_source_dominator_check(forms[form], text_output, scratch, graphs, name, source->name, source->name_length, form, &dominance);
                totals->dominator_forms += 1;
                totals->dominator_checks += dominance.checks;
                totals->dominator_failed += !qualified;
                dominators_valid = dominators_valid && qualified;
                if (!qualified)
                {
                    printf("ir-source: file=%s function=%s form=%s dominators_failed=\"%s\" cfg_error=%u scalar=%u vector=%u auto=%u\n",
                           name, source->name, form_names[form], dominance.reason, (unsigned)dominance.cfg_error,
                           (unsigned)dominance.status[0], (unsigned)dominance.status[1], (unsigned)dominance.status[2]);
                }
            }
        }
        bool dataflow_valid = true;
        for (uint32_t form = 0; agree && form < (promotes ? 3u : 1u); form += 1)
        {
            IrDataflowTests checks = { 0 };
            const bool checked = ir_dataflow_test_function(forms[form], text_output, scratch, false, &checks);
            totals->dataflow_forms += 1;
            totals->dataflow_failed += !checked;
            totals->dataflow_checks += checks.checks;
            dataflow_valid = dataflow_valid && checked;
            if (!checked)
            {
                printf("ir-source: file=%s function=%s form=%s dataflow_failed=%" PRIu64 "\n", name, source->name, form_names[form], checks.failures);
            }
            if (benchmark_dataflow && checked)
            {
                const bool measured = ir_dataflow_benchmark_function(forms[form], name, (const char*)source->name, form_names[form], text_output, scratch, counters);
                totals->dataflow_benchmark_failed += !measured;
                dataflow_valid = dataflow_valid && measured;
            }
        }
        bool uses_valid = true;
        for (uint32_t form = 0; agree && form < (promotes ? 3u : 1u); form += 1)
        {
            const bool same = ir_source_uses_twins(forms[form], text_output, scratch);
            totals->uses_forms += 1;
            totals->uses_failed += !same;
            uses_valid = uses_valid && same;
            if (!same)
            {
                printf("ir-source: file=%s function=%s form=%s uses_failed\n", name, source->name, form_names[form]);
            }
        }
        failures += !promotes || !text_valid || !dominators_valid || !dataflow_valid || !uses_valid;
        if (!promotes)
        {
            const IrCheck first = vector_check.error != IR_OK ? vector_check : scalar_check.error != IR_OK ? scalar_check : promotion.error != IR_OK ? promotion : promotion_twin;
            printf("ir-source: file=%s function=%s %s error=%s instruction=%u detail=%u\n", name, source->name,
                   !lowered ? "refused" : !agree ? "the paths disagree" : "unpromoted", ir_source_errors[first.error <= IR_ERROR_UNSUPPORTED ? first.error : IR_ERROR_IO],
                   first.instruction, first.detail);
        }
        if (print != 0 && strcmp(print, (const char*)source->name) == 0)
        {
            printf("ir-source: %s as lowered:\n", source->name);
            fflush(stdout);
            if (agree)
            {
                ir_print(&vector, scratch, stdout);
            }
            printf("ir-source: %s as promoted:\n", source->name);
            fflush(stdout);
            if (promotes)
            {
                ir_print(&promoted, scratch, stdout);
            }
        }
        arena_pop_to(output, output_mark);
        arena_pop_to(twin_output, twin_mark);
        arena_pop_to(promoted_output, promoted_mark);
    }
    printf("ir-source: file=%s functions=%u entries=%" PRIu64 " instructions=%" PRIu64 " bytes=%" PRIu64 " tokens=%" PRIu64 " failures=%u\n", name, file->count, entries,
           instructions, bytes, file->tokens, failures);
    totals->files += 1;
    totals->functions += file->count;
    totals->entries += entries;
    totals->tokens += file->tokens;
    totals->instructions += instructions;
    totals->bytes += bytes;
}

// The preparation with no room for its state (#845's review): in a child whose address space can't grow by its arena,
// `file` must fail as IR_SOURCE_FAILED with the memory's reason, touching none of the objects it couldn't allocate.
// Whether it did, as the child's status.
static bool ir_source_exhausted(const char* file, char** options, uint32_t option_count, Arena* lowered, Arena* arena)
{
    bool refused = false;
    const pid_t child = fork();
    if (child == 0)
    {
        long pages = 0;
        FILE* statm = fopen("/proc/self/statm", "r");
        const bool sized = statm != 0 && fscanf(statm, "%ld", &pages) == 1;
        if (statm != 0)
        {
            fclose(statm);
        }
        struct rlimit bound = { 0 };
        bool limited = sized && getrlimit(RLIMIT_AS, &bound) == 0;
        // The mappings it has and 4 MB more, short of the 16 MB the preparation reserves first.
        bound.rlim_cur = (rlim_t)pages * (rlim_t)sysconf(_SC_PAGESIZE) + (UINT64_C(4) << 20);
        limited = limited && bound.rlim_cur <= bound.rlim_max && setrlimit(RLIMIT_AS, &bound) == 0;
        IrSourceFile prepared = { 0 };
        if (limited)
        {
            ir_source_prepare(&prepared, file, options, option_count, lowered, arena, false, false);
        }
        _exit(limited && prepared.status == IR_SOURCE_FAILED && strstr(prepared.reason, "memory") != 0 ? 0 : 1);
    }
    else if (child > 0)
    {
        int status = 0;
        refused = waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    }
    return refused;
}

// Defined with the benchmark: its corpus admission and both measured lowering paths, without a timing report.
static int run_ir_source_benchmark_tests(int count, char** arguments);
// #139's passes on real C (ir_source_passes.c): test-ir-source --passes and benchmark-ir-source --passes.
static int run_ir_source_passes_tests(int count, char** arguments);
static int run_ir_source_passes_benchmark(int count, char** arguments);

static int ir_source_tests_command(int count, char** arguments)
{
    const char* print = 0;
    bool simd = false;
    const char* graph_name = 0;
    bool dominators = false;
    bool dataflow_self_test = false;
    bool benchmark_dataflow = false;
    Tier0BenchmarkCounters counters = { .cycles = -1, .instructions = -1, .throttle = -1 };
    char** options = calloc((size_t)count + 1, sizeof(char*));
    char** files = calloc((size_t)count + 1, sizeof(char*));
    uint32_t option_count = 0;
    uint32_t file_count = 0;
    bool valid = options != 0 && files != 0;
    for (int i = 0; valid && i < count; i += 1)
    {
        if (strcmp(arguments[i], "--print") == 0)
        {
            valid = i + 1 < count;
            print = valid ? arguments[++i] : 0;
        }
        else if (strcmp(arguments[i], "--simd") == 0)
        {
            simd = true;
        }
        else if (strcmp(arguments[i], "--dominators") == 0)
        {
            dominators = true;
        }
        else if (strcmp(arguments[i], "--dominators-cfg") == 0)
        {
            valid = graph_name == 0 && i + 1 < count;
            graph_name = valid ? arguments[++i] : 0;
            dominators = true;
        }
        else if (strcmp(arguments[i], "--dataflow-self-test") == 0)
        {
            dataflow_self_test = true;
        }
        else if (strcmp(arguments[i], "--dataflow-benchmark") == 0)
        {
            benchmark_dataflow = true;
        }
        else if (arguments[i][0] != '-')
        {
            files[file_count++] = arguments[i];
        }
        else if (strcmp(arguments[i], "--no-default-config") != 0)
        {
            // An option and its value, which pp_parse_options reads with each file; clang's --no-default-config, which the
            // 9700X job's options for the frozen corpora pass, is dropped, as benchmark-backend-source drops it.
            options[option_count++] = arguments[i];
            if (pp_option_takes_value(arguments[i]))
            {
                valid = i + 1 < count;
                options[option_count++] = valid ? arguments[++i] : 0;
            }
        }
    }
    valid = valid && (dataflow_self_test ? file_count == 0 && option_count == 0 && print == NULL && !benchmark_dataflow && !simd : file_count != 0);
    if (valid && benchmark_dataflow)
    {
        counters.cycles = tier0_benchmark_counter(PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES);
        counters.instructions = tier0_benchmark_counter(PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS);
        valid = counters.cycles >= 0 && counters.instructions >= 0;
        if (!valid)
        {
            fprintf(stderr, "ir-dataflow-benchmark: hardware cycles and instructions counters are required\n");
        }
    }
    Arena lowered = arena_reserve(UINT64_C(1) << 36);
    Arena arena = arena_reserve(UINT64_C(1) << 32);
    Arena output = arena_reserve(UINT64_C(1) << 34);
    Arena twin_output = arena_reserve(UINT64_C(1) << 34);
    Arena promoted = arena_reserve(UINT64_C(1) << 34);
    Arena text_output = arena_reserve(UINT64_C(1) << 34);
    Arena scratch = arena_reserve(UINT64_C(1) << 34);
    valid = valid && lowered.base != 0 && arena.base != 0 && output.base != 0 && twin_output.base != 0 && promoted.base != 0 && text_output.base != 0 && scratch.base != 0;
    // Exclusive creation never truncates an input or an earlier measurement. Check close as well as every write:
    // buffered output can fail only when the completion record is flushed.
    FILE* graphs = valid && graph_name != 0 ? fopen(graph_name, "wx") : 0;
    const bool graph_open = graph_name == 0 || graphs != 0;
    bool graph_valid = graph_open;
    if (graphs != 0)
    {
        graph_valid = dominators_export_begin(graphs);
    }
    if (valid && !graph_valid)
    {
        fprintf(stderr, "ir-source: cannot create CFG export %s: %s\n", graph_name, strerror(errno));
    }
    valid = valid && graph_valid;
    IrSourceTotals totals = { 0 };
    const bool dominator_oracle = !dominators || (valid && ir_source_dominator_tests(&arena, &text_output, &scratch));
    const bool dataflow_oracle = valid && (!dataflow_self_test || ir_dataflow_tests(&arena, &text_output, &scratch));
    const bool text_oracle = valid && (dataflow_self_test || ir_source_text_tests(&arena, &output, &twin_output, &text_output, &scratch));
    const bool exhausted = valid && (dataflow_self_test || ir_source_exhausted(files[0], options, option_count, &lowered, &arena));
    if (valid && !dataflow_self_test)
    {
        printf("ir-source: a preparation without memory for its state %s\n", exhausted ? "fails, as it should" : "doesn't fail as it should");
    }
    for (uint32_t i = 0; valid && i < file_count; i += 1)
    {
        IrSourceFile file;
        ir_source_prepare(&file, files[i], options, option_count, &lowered, &arena, false, false);
        if (file.status == IR_SOURCE_PREPARED)
        {
            ir_source_check(&totals, &file, files[i], print, simd, dominators, graphs, &output, &twin_output, &promoted, &text_output, &scratch, benchmark_dataflow, counters);
        }
        else
        {
            totals.left_out += file.status == IR_SOURCE_LEFT_OUT;
            totals.failed_files += file.status == IR_SOURCE_FAILED;
            printf("ir-source: file=%s %s=\"%s\"\n", files[i], file.status == IR_SOURCE_LEFT_OUT ? "left_out" : "failed", file.reason);
        }
        fflush(stdout);
        arena_pop_to(&lowered, 0);
        arena_pop_to(&arena, 0);
    }
    if (valid)
    {
        printf("ir-source: totals files=%" PRIu64 " left_out=%" PRIu64 " failed=%" PRIu64 " functions=%" PRIu64 " entries=%" PRIu64 " tokens=%" PRIu64
               " instructions=%" PRIu64 " bytes=%" PRIu64 " (%.2f per token) promoted_instructions=%" PRIu64 " promoted_bytes=%" PRIu64
               " refused=%" PRIu64 " disagreed=%" PRIu64 " unpromoted=%" PRIu64 " simd_promoted=%" PRIu64 " text_forms=%" PRIu64 " text_failed=%" PRIu64
               " dataflow_forms=%" PRIu64 " dataflow_failed=%" PRIu64 " dataflow_checks=%" PRIu64 " dataflow_benchmark_failed=%" PRIu64
               " uses_forms=%" PRIu64 " uses_failed=%" PRIu64 "\n",
               totals.files, totals.left_out, totals.failed_files, totals.functions, totals.entries, totals.tokens, totals.instructions, totals.bytes,
               totals.tokens != 0 ? (double)totals.bytes / (double)totals.tokens : 0.0, totals.promoted_instructions, totals.promoted_bytes, totals.refused,
               totals.disagreed, totals.unpromoted, totals.simd_promoted, totals.text_forms, totals.text_failed, totals.dataflow_forms,
               totals.dataflow_failed, totals.dataflow_checks, totals.dataflow_benchmark_failed, totals.uses_forms, totals.uses_failed);
    }
    else
    {
        fprintf(stderr, "usage: compiler test-ir-source [--simd] [--print NAME] [--dominators] [--dominators-cfg NEW_FILE] [--dataflow-benchmark] [preprocessor options] file... | --dataflow-self-test\n");
    }
    bool passed = valid && exhausted && text_oracle && dominator_oracle && dataflow_oracle && (!simd || (totals.simd_promoted != 0 && totals.left_out == 0)) &&
                  totals.text_failed == 0 && totals.failed_files == 0 && totals.refused == 0 && totals.disagreed == 0 && totals.unpromoted == 0 &&
                  totals.dataflow_failed == 0 && totals.dataflow_benchmark_failed == 0 && totals.uses_failed == 0 && (dataflow_self_test || totals.functions != 0) &&
                  (!benchmark_dataflow || totals.left_out == 0);
    if (dominators)
    {
        passed = passed && totals.left_out == 0 && totals.dominator_failed == 0 && totals.dominator_forms == 3 * totals.functions;
        printf("ir-source: dominators forms=%" PRIu64 " checks=%" PRIu64 " failed=%" PRIu64 " excluded=%" PRIu64 " host-vector=%u %s\n",
               totals.dominator_forms, totals.dominator_checks, totals.dominator_failed, totals.left_out, BUSTER_HOST_VECTOR, passed ? "passed" : "FAILED");
    }
    if (graphs != 0)
    {
        if (passed)
        {
            graph_valid = dominators_export_complete(graphs, totals.files, totals.functions, totals.dominator_forms);
        }
        graph_valid = ferror(graphs) == 0 && graph_valid;
        const int closed = fclose(graphs);
        graph_valid = closed == 0 && graph_valid;
        if (!graph_valid)
        {
            fprintf(stderr, "ir-source: CFG export write/close failed: %s\n", graph_name);
        }
        passed = passed && graph_valid;
        if (!passed && unlink(graph_name) != 0)
        {
            fprintf(stderr, "ir-source: cannot remove incomplete CFG export: %s\n", graph_name);
        }
    }
    if (benchmark_dataflow)
    {
        printf("ir-dataflow-benchmark: qualified=%u forms=%" PRIu64 " left_out=%" PRIu64 " vector_host=%u\n", passed, totals.dataflow_forms, totals.left_out, BUSTER_HOST_VECTOR);
    }
    if (counters.cycles >= 0)
    {
        close(counters.cycles);
    }
    if (counters.instructions >= 0)
    {
        close(counters.instructions);
    }
    arena_release(&scratch);
    arena_release(&text_output);
    arena_release(&promoted);
    arena_release(&twin_output);
    arena_release(&output);
    arena_release(&arena);
    arena_release(&lowered);
    free(files);
    free(options);
    return passed ? 0 : 1;
}

static int run_ir_source_tests(int count, char** arguments)
{
    const bool benchmark = count > 0 && strcmp(arguments[0], "--benchmark-self-test") == 0;
    const bool passes = count > 0 && strcmp(arguments[0], "--passes") == 0;
    return benchmark ? run_ir_source_benchmark_tests(count - 1, arguments + 1) :
           passes    ? run_ir_source_passes_tests(count - 1, arguments + 1) :
                       ir_source_tests_command(count, arguments);
}

#endif
