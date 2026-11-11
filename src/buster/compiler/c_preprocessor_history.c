// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Shared cold-expansion history readers and diagnostic declarations. Included by c_preprocessor.c after its state
// and diagnostic entry point. c_preprocessor_history_impl.c supplies the private writers and rendering bodies in
// the preprocessor's implementation owner, retaining their original order.
// Origins in temporary token buffers are discarded with those buffers. The output keeps only eight-byte runs,
// built outside the token-copy kernels with adjacent comparisons and compression. Records and generated inputs
// live for the translation unit, including after a definition is replaced or an argument buffer is reclaimed.
// All links point to older records; diagnostics walk them iteratively, never through recursive calls.

static uint32_t pp_history_at(const Preprocessor* pp, uint32_t token)
{
    uint32_t low = 0;
    uint32_t high = pp->history_run_count;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        if (pp->history_runs[middle].token <= token)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    return token < pp->output.count && low != 0 ? pp->history_runs[low - 1].origin : 0;
}

// One logical diagnostic, including its include stack and notes, is rendered before its single fwrite. The usual
// diagnostic fits in the caller's stack buffer; exceptional long messages grow in temporary scratch. If scratch
// cannot grow, flush the complete prefix and keep printing directly, so memory exhaustion never drops or truncates
// a diagnostic. No FILE buffering mode changes, deferred output, or per-diagnostic heap allocation is needed.
#define BUSTER_PP_DIAGNOSTIC_BYTES 2048

typedef struct PPDiagnostic PPDiagnostic;
struct PPDiagnostic
{
    FILE* stream;
    Arena* arena;
    char* bytes;
    uint64_t count;
    uint64_t capacity;
    bool direct;
};

typedef struct PPDiagnosticSource PPDiagnosticSource;
struct PPDiagnosticSource
{
    uint32_t location;
    uint32_t origin;
    uint32_t column;
};

// Retain a completed construct's own ordering across producers that render its warnings and errors separately.
// Only the new records are visited; notes remain attached to their primary, and ordinary diagnostics pass zero.
static __attribute__((unused)) void pp_diagnostic_suborder(Preprocessor* pp, const PPDiagnosticRecord* previous, uint64_t suborder)
{
    for (PPDiagnosticRecord* record = suborder != 0 ? (previous != NULL ? previous->next : pp->diagnostic_first) : NULL;
         record != NULL; record = record->next)
    {
        record->suborder = suborder;
    }
}

BUSTER_PREPROCESSOR_ENTRY void pp_diagnostic_publish(Preprocessor* pp);

// The diagnostic location and its publication boundary can differ. A runtime warning, for example, completes
// after its function's closing brace and the preprocessor lookahead before the next source token. A boundary
// one past output.count is reserved for translation-unit final checks, after ordinary EOF completion groups.
// Lexical, deferred-body, return-analysis and ordinary Sema groups can share a lookahead boundary. Their
// explicit packed phase preserves that ordering while attached notes and publication budgets remain one group.
BUSTER_PREPROCESSOR_ENTRY __attribute__((unused, format(printf, 6, 7))) void pp_diagnose_output_phase(Preprocessor* pp, PPSeverity severity, uint32_t token, uint32_t position, PPDiagnosticPhase phase, const char* format, ...);

BUSTER_PREPROCESSOR_ENTRY __attribute__((unused, format(printf, 5, 6))) void pp_diagnose_output_at(Preprocessor* pp, PPSeverity severity, uint32_t token, uint32_t position, const char* format, ...);

// pp_diagnose_output_at at a character of the token's spelling, retaining its output origin and publication order.
BUSTER_PREPROCESSOR_ENTRY __attribute__((unused, format(printf, 6, 7))) void pp_diagnose_output_offset_at(Preprocessor* pp, PPSeverity severity, uint32_t token, uint32_t offset,
                                                                                                           uint32_t position, const char* format, ...);

// pp_diagnose_output_offset_at with notes at places of their own (#1447: a named universal character name's, at bytes of
// its name in the token's spelling, `offset` each), the token's history theirs.
BUSTER_PREPROCESSOR_ENTRY __attribute__((unused, format(printf, 8, 9))) void pp_diagnose_output_offset_notes(Preprocessor* pp, PPSeverity severity, uint32_t token, uint32_t offset,
                                                                                                              uint32_t position, const PPOffsetNote* notes,
                                                                                                              uint32_t note_count, const char* format, ...);

BUSTER_PREPROCESSOR_ENTRY __attribute__((unused, format(printf, 4, 5))) void pp_diagnose_output(Preprocessor* pp, PPSeverity severity, uint32_t token, const char* format, ...);

// `pp_diagnose_output_location` for callers holding a phase and a varargs-free message: a source location inside a
// token's spelling (not its start) publishes where `position` sorts it, keeping the token's own origin history.
BUSTER_PREPROCESSOR_ENTRY __attribute__((unused, format(printf, 7, 8))) void pp_diagnose_output_location_at(Preprocessor* pp, PPSeverity severity, uint32_t token, uint32_t location,
                                                                                                          uint32_t position, PPDiagnosticPhase phase, const char* format, ...);

// Notes share the primary's visibility, completion, error budget and fatal cutoff. Their output-token locations
// retain their own macro histories, while collection owns the complete note span and all message bytes. Every
// descriptor must name an existing output token and a non-NULL message; callers supply an immutable bounded span.
BUSTER_PREPROCESSOR_ENTRY __attribute__((unused, format(printf, 7, 8))) void pp_diagnose_output_notes_at(Preprocessor* pp, PPSeverity severity, uint32_t token, uint32_t position, const PPOutputDiagnosticNote* notes, uint32_t note_count, const char* format, ...);

// A diagnostic at no source location, as clang gives one at an invalid SourceLocation (no file:line:column, no macro
// history), with its notes, published where `position` sorts it among the semantic ones (#147: an interrupt attribute's
// errors where clang finds no place in the declaration, the warning of its implicit used after a definition).
BUSTER_PREPROCESSOR_ENTRY __attribute__((unused, format(printf, 6, 7))) void pp_diagnose_unlocated_notes(Preprocessor* pp, PPSeverity severity, uint32_t position,
                                                                                                     const PPOutputDiagnosticNote* notes, uint32_t note_count,
                                                                                                     const char* format, ...);

// Where clang gives a diagnostic of an attribute a macro wrote (#1447, Objective-C's pointer type attributes'): output token
// `token`'s location and macro history lifted out of the macro whose body spelled it, to that macro's expansion (the
// immediate expansion SourceManager gives), or, with `name`, to the outermost expansion where that macro is named `name`
// (Sema's findMacroSpelling: its expansion location spelled so), into *location and *origin. Whether it was lifted; where
// it isn't, they are the token's own.
BUSTER_PREPROCESSOR_ENTRY __attribute__((unused)) bool pp_output_macro_place(Preprocessor* pp, uint32_t token, const char* name, uint32_t* location, uint32_t* origin);

// pp_diagnose_output_notes_phase at an explicit place (pp_output_macro_place's).
BUSTER_PREPROCESSOR_ENTRY __attribute__((unused, format(printf, 9, 10))) void pp_diagnose_placed_notes_phase(Preprocessor* pp, PPSeverity severity, uint32_t location,
                                                                                                         uint32_t origin, uint32_t position, PPDiagnosticPhase phase,
                                                                                                         const PPOutputDiagnosticNote* notes, uint32_t note_count,
                                                                                                         const char* format, ...);

BUSTER_PREPROCESSOR_ENTRY __attribute__((unused, format(printf, 8, 9))) void pp_diagnose_output_notes_phase(Preprocessor* pp, PPSeverity severity, uint32_t token, uint32_t position, PPDiagnosticPhase phase,
                                                                                                        const PPOutputDiagnosticNote* notes, uint32_t note_count, const char* format, ...);
// A diagnostic at an explicit place (`location`, `origin`) with notes at explicit places, a place of location 0 a note
// without one (#1849: clang's note at an invalid location).
BUSTER_PREPROCESSOR_ENTRY __attribute__((unused, format(printf, 9, 10))) void pp_diagnose_placed_sources_phase(Preprocessor* pp, PPSeverity severity, uint32_t location, uint32_t origin,
                                                                                                          uint32_t position, PPDiagnosticPhase phase, const PPDiagnosticSourceNote* placed,
                                                                                                          uint32_t placed_count, const char* format, ...);
