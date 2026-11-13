// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// The predefined macros and the feature tables are clang 23.1.3's answers, generated as docs/c-preprocessor.md
// describes (THIRD_PARTY_NOTICES.md).
//
// The C preprocessor: translation phase 4 for one translation unit, from the refined tokens of each file it reads
// (c_lexer.c, then c_refine.c) to one token stream, and `-E`'s text from that stream. docs/c-preprocessor.md has the
// design and its 9700X measurements.
//
// Files. Every file is read or mapped, lexed, refined and given a directive table once (pp_prepare_into), and kept for
// the translation unit, since macro definitions point into the tokens of the file that defines them. The directive
// table lists each '#' that starts a line, with its kind and the token where its line ends; conditional directives are
// paired with an explicit stack, each pointing to the next directive of its group, so that skipping a group that isn't
// taken is a jump. A file whose tokens are all inside `#ifndef X` ... `#endif` (or `#if !defined X`) has X as its
// include guard: an inclusion while X is defined reads nothing, as an inclusion after `#pragma once` doesn't. What is
// prepared from a file is kept in a FileCache, by the file's identity, with every path looked up and what it found
// there: the translation units that preprocess with one cache share it, and c_files.c fills it from several lanes
// ahead of them (#150). A translation unit keeps its own state about each file (PPFile, PPFileData), copied from the
// cache's record when it first needs it.
//
// Macros. The macro table is indexed by IdentId (`macro_of`, with a bitset of the names that are macros for the main
// loop's scan). A definition records where its replacement list is in the defining file's tokens; a function-like one
// is compiled at its first expansion (pp_compile_macro) into body items that resolve each parameter.
//
// The main loop (pp_run_contexts) reads from a stack of contexts, each either a file (its tokens up to its next
// directive, or a directive) or a buffer of tokens (a macro's expansion, an argument being expanded, a directive's
// line). It copies runs of tokens that name no macro straight to the output, 64 at a time (pp_copy_file_run_vector,
// pp_copy_buffer_run_vector), and expands a macro name in place (pp_expand_current): an object-like macro whose body
// names no macro is copied out directly, any other expansion is built in a buffer and pushed as a context, with the
// macro disabled until that context is popped. A name of a disabled macro is painted (BUSTER_PP_NO_EXPAND) and never
// expands, as in clang. Arguments are collected from whatever contexts follow the macro's name: at once when they are
// all in the top one (pp_collect_arguments), else by the main loop a token at a time (pp_collect_step), popping the
// contexts they exhaust and running any directive among them. An argument that names a macro is expanded before
// substitution by pushing it as a barrier context with a capture, which the main loop fills until the barrier ends;
// so are a #if, #elif, computed #include or #line line and a builtin's operand, each capture with the continuation that
// takes it (pp_finish_capture). Nothing nests through calls: there is no recursion (the call graph check, #112),
// only explicit stacks (contexts, invocations, captures). An expansion that gives no tokens passes its name's leading
// space and start of line on to the next token read from its context (PPContext.pending_flags), as clang does.
//
// Tokens. A token in a buffer or the output is a kind (C_TOKEN_*, or a keyword's kind in the final output), flags
// (BUSTER_PP_SPACE, BUSTER_PP_START_OF_LINE, BUSTER_PP_NO_EXPAND, BUSTER_PP_FROM_MACRO), a value (an identifier's
// IdentId, else the location of its end) and a location. Locations are offsets in the translation unit's location
// space: each inclusion of a file gets a range of its own, as do the scratch spellings that pasting and stringizing
// make (PPRange), so that any token's spelling is the bytes from its location to its end.
//
// The vectorized kernels, each with a scalar twin that PPOptions.scalar selects (test-preprocessor compares them):
// the directive scan (pp_scan_directives_vector) and the conditionals' pairing (pp_pair_conditionals_vector), the file
// and buffer runs (pp_copy_file_run_vector, pp_copy_buffer_run_vector), the argument scan (pp_collect_arguments) and
// the copies of whole token ranges (pp_append_file_tokens, pp_append_stack_tokens: a function-like macro's arguments,
// a body's runs of tokens, an object-like macro's body), identifier pastes interned without lexing (pp_paste), line
// counts (pp_count_lines), the keyword pass over the output (pp_resolve_keywords_range) and -E's printer
// (pp_print_vector). docs/c-preprocessor.md ("Data-parallel design, piece by piece") has each one's 9700X comparison.
//
// main.c includes this file after c_lexer.c, lane.c, c_intern.c and c_refine.c; every function is static, but for the
// entry points that take BUSTER_PREPROCESSOR_ENTRY, which main.c's units compile in one unit alone. Shared state,
// declarations and readers remain here. The private bodies and tables are in c_preprocessor_impl.c, included only
// by FRONTEND, standalone consumers and main.c's complete driver (BUSTER_COMPLETE_DRIVER: a fuzz build's, or a
// self-test input's), which keeps the original nonowner linkage. The two files preserve the following logical layout:
//   - API: PPOptions, PPTokens, the flags and internal kinds, PPStatistics, Preprocessor and the entry points.
//   - Diagnostics, token stacks, location ranges and scratch spellings.
//   - Files: PPFileData, PPFile, the file cache's records and paths (FileCache), the directive scan
//     (pp_scan_directives_vector, pp_finish_directives, pp_find_guard), pp_prepare_into (a file whole, pp_prepare_whole, or
//     a large one a window at a time, pp_prepare_windows), pp_count_lines and pp_line_of,
//     the cache's functions (file_cache_lookup_path, file_cache_prepare and the ones they're made of), and a translation
//     unit's paths (pp_lookup_path) and preparation (pp_prepare_data).
//   - Include search: pp_include_candidate and pp_find_include (pp_dedupe_directories drops missing and repeated
//     directories), and the literal header names (pp_scan_header_name, pp_literal_header_name).
//   - Macros: PPMacro, pp_define, pp_same_definition, pp_compile_macro.
//   - The main loop: contexts, the token copies, pp_read_token, substitution (pp_paste, pp_stringize, pp_paste_in_place,
//     pp_substitute),
//     invocations (pp_collect_arguments, pp_invoke, PPInvocation), pp_expand_object, the built-in macros
//     (pp_expand_builtin), pp_expand_current and pp_run_contexts.
//   - Directives: pp_directive and the handlers (pp_condition, pp_include, pp_line, pp_pragma).
//   - #if expressions (pp_evaluate), and the feature tables the __has_* builtins read (pp_feature).
//   - The predefines, pp_initialize, pp_release and pp_run.
//   - Output: pp_resolve_keywords and pp_print, and the -E command (pp_parse_options, with pp_take_family and
//     pp_settle_families for the -f options that change clang's keywords, lexing or -E output).
//   - clang's arguments for ours (pp_clang_arguments), and a clang run (pp_clang_preprocess), for the commands that run
//     clang with them.

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/statfs.h>
#include <sys/syscall.h>
#if __has_feature(memory_sanitizer)
#include <sanitizer/msan_interface.h>
#endif
#include "host_isa.h"
#include "c_dependency_driver.c"
#include "c_builtin_catalog.c"
#include "c_builtin_fortify.c"
#include "c_builtin_library.c"
#include "c_builtin_source_intrinsics.c"
#include "c_builtin_intrinsic_features.c"
#include "buster/compiler/x86_target.c"

// The linkage of the entry points that main.c's units compile in one of them, their home, and call there (main.c): a
// unit of its own, such as a benchmark's, has them static, as every other function here.
#ifndef BUSTER_PREPROCESSOR_ENTRY
#define BUSTER_PREPROCESSOR_ENTRY static
#endif

// ---------------------------------------------------------------------------------------------------------------------
// API

#define BUSTER_PP_DEFAULT_ERROR_LIMIT 19

// Token flags, in a buffer or the output.
#define BUSTER_PP_SPACE 0x01 // Whitespace or a comment before it.
#define BUSTER_PP_START_OF_LINE 0x02 // It starts a line of the output (for -E).
#define BUSTER_PP_NO_EXPAND 0x04 // It names a macro that was disabled when it was read: it never expands.
#define BUSTER_PP_FROM_MACRO 0x08 // A macro's expansion made it (for __LINE__ and __FILE__).
// Its BUSTER_PP_SPACE stands for the line it started among a macro's arguments, where clang's token keeps its own
// flags: clang's printer gives it no space for that, and starts a line with it when its expansion's line is another one
// (-E with line markers does the same, and test-preprocessor's emulation of the -P printer reads this).
#define BUSTER_PP_LINE_SPACE 0x10
// Its BUSTER_PP_SPACE is an expansion's name's, which the expansion's first token takes (and an empty expansion passes
// on to the next token): clang's token then has a leading space only as the name does, for whitespace or a comment
// before it on its line, where ours counts the line's start too. Clang's -P printer shows that in a line's first token's
// indentation (test-preprocessor's emulation of it reads this).
#define BUSTER_PP_NAME_SPACE 0x20
// Its BUSTER_PP_START_OF_LINE (or BUSTER_PP_LINE_SPACE) is a name's that had only its line's start before it: clang's
// token has no leading space, where our BUSTER_PP_SPACE counts the line's start. Set only in runs that keep expansion
// starts (pp_name_line), for test-preprocessor's emulation of clang's -P printer.
#define BUSTER_PP_NAME_LINE 0x40
// Only in a context's pending flags (PPContext.pending_flags): the next token read from the context starts no line,
// though it starts one in its file. After an #embed's tokens clang's lexer starts no line until a newline outside
// comments (pp_embed_joins), so its printer prints that token on the line of the embed's output. pp_with_pending takes it.
#define BUSTER_PP_JOIN_LINE 0x80

// Kinds that only buffers hold, and the end of a pragma's tokens and an #embed's data in the output.
#define BUSTER_PP_KIND_PLACEMARKER 0xFE // An empty argument next to ##, while substituting.
#define BUSTER_PP_KIND_INTEGER 0xFD // An integer whose value is the token's value (spelled in decimal).
#define BUSTER_PP_KIND_PRAGMA_END 0xFC // After the tokens of a #pragma (or _Pragma, or #ident) in the output (its '#' starts a line).
// An #embed's data (clang's annot_embed): bytes of a file, which -E prints as their values separated by ", ". Its value is
// its index in Preprocessor.embeds (PPEmbed), its location the directive's '#'.
#define BUSTER_PP_KIND_EMBED 0xFF
// In the output, keyword kinds follow the token kinds: id `k` (1 to C_NAME_KEYWORD_COUNT) is BUSTER_PP_KIND_KEYWORD + k - 1.
#define BUSTER_PP_KIND_KEYWORD C_TOKEN_KIND_COUNT
_Static_assert(BUSTER_PP_KIND_KEYWORD + C_NAME_KEYWORD_COUNT <= BUSTER_PP_KIND_PRAGMA_END, "keyword kinds must fit below the internal kinds");

// The ids pp_resolve_keywords looks up (the keywords' are below it): two vpermi2b tables, by an id's low byte.
#define BUSTER_PP_KEYWORD_TABLE 256
_Static_assert(C_NAME_KEYWORD_COUNT < BUSTER_PP_KEYWORD_TABLE, "the keywords must have ids below the keyword table's size");
_Static_assert(BUSTER_PP_KEYWORD_TABLE == 256, "pp_resolve_keywords_range looks up an id's low byte in four 64-byte quarters");
// Limits: tokens in the output and in each stack, contexts, nested inclusions, a directive's line.
#define BUSTER_PP_MAX_OUTPUT_TOKENS (1u << 28)
#define BUSTER_PP_MAX_STACK_TOKENS (1u << 26)
#define BUSTER_PP_MAX_CONTEXTS (1u << 20)
#define BUSTER_PP_MAX_INCLUDE_DEPTH 200
#define BUSTER_PP_MAX_PARAMETERS 4096
// Token arrays get whole-vector stores past their last token.
#define BUSTER_PP_TOKEN_SLACK 64
// The arenas of arrays of records: the unit's tables, all in one (pp_initialize_once), and the arrays reserved later, an
// arena each (pp_array_small).
#define BUSTER_PP_ARRAYS 40
// The base pages before the huge pages of the tables that a translation unit usually fills to a few hundred kilobytes:
// the history's substitutions and generated tokens, and the arguments' links. On sqlite3.c (with glibc's headers) each
// of these, and the macros, their names' index and the compiled bodies before they were tables of the unit's in base
// pages throughout (pp_table), would otherwise take a huge page the kernel clears whole (2 MB, about 86 us on the
// 9700X) for 86 to 530 KB, which base pages cover for 0.46 us per 4 KB; on this compiler's main.c, whose tables are
// larger, the first megabyte in base pages costs about as much as the huge pages it replaces.
#define BUSTER_PP_TABLE_SMALL_BYTES (1ull << 20)
// Records: location ranges, files, macros, compiled body items and parameters.
#define BUSTER_PP_MAX_RANGES (1u << 22)
#define BUSTER_PP_MAX_FILES (1u << 20)
#define BUSTER_PP_MAX_MACROS (1u << 24)
#define BUSTER_PP_MAX_ITEMS (1u << 26)
#define BUSTER_PP_MAX_LINE_DIRECTIVES (1u << 20)
// The _Pragma operators that printed nothing which a run notes (Preprocessor.silent_pragmas).
#define BUSTER_PP_MAX_SILENT_PRAGMAS (1u << 16)
// The #embed directives that put data in the output (Preprocessor.embeds).
#define BUSTER_PP_MAX_EMBEDS (1u << 20)
#define BUSTER_PP_MAX_PUSHED (1u << 16)

typedef uint32_t PPMacroId;

// What a run did, counted when PPOptions.timing is set (benchmark-preprocessor prints it). Include and embed lookup
// ticks are inclusive intervals, separate from PPPhase: include lookup overlaps reading and the main loop, and embed
// lookup overlaps the main loop. Neither lookup enters/prepares a file, executes it, or invokes the other lookup.
typedef struct PPStatistics PPStatistics;
struct PPStatistics
{
    uint64_t file_run_tokens; // Tokens files' runs copied to the current target (including temporary captures),
    uint64_t file_runs; // in this many runs.
    uint64_t buffer_run_tokens; // Tokens buffers' runs copied,
    uint64_t buffer_runs; // in this many runs.
    // Disjoint parts of the final output, counted only when the copy's target is Preprocessor.output. Pragma records
    // and an #embed's single output token count as in PPTokens.count; pp_emit and lifted pragma lines are the remainder.
    uint64_t final_file_run_tokens;
    uint64_t final_buffer_run_tokens;
    uint64_t final_direct_object_tokens;
    uint64_t direct_expansions; // Object-like macros copied straight out,
    uint64_t buffered_expansions; // and the ones pushed as a buffer.
    uint64_t invocations; // Function-like macros invoked,
    uint64_t arguments; // their arguments,
    uint64_t expanded_arguments; // and the arguments expanded before substitution.
    uint64_t pastes;
    uint64_t builtins;
    uint64_t conditions; // #if and #elif lines evaluated,
    uint64_t condition_tokens; // their tokens after expansion,
    uint64_t long_conditions; // the lines of more than 32 of them,
    uint64_t longest_condition; // and the most tokens a line had.
    uint64_t directives;
    uint64_t skipped_guards; // #include of a file whose guard is defined (clang's "multi-include optimization"),
    uint64_t skipped_once; // and of one #pragma once or #import read.
    uint64_t include_searches; // pp_find_include calls, including __has_include and __has_include_next,
    uint64_t include_found; // and those that found a file.
    uint64_t include_opens; // File-probe open and fstat attempts inside those calls (a subset of Preprocessor.probes).
    uint64_t include_fstats;
    uint64_t include_ticks;
    uint64_t embed_searches; // pp_find_embed calls, including __has_embed,
    uint64_t embed_found; // and those that found a file (a device too).
    uint64_t embed_stats; // Actual stat attempts, including failed ones; embed lookup has no path cache.
    uint64_t embed_ticks;
    uint64_t directory_stats; // stat attempts discovering/deduplicating the search directories during initialization.
};

// The disjoint phases PPOptions.timing measures (in TSC ticks): path-cache lookup/byte acquisition, lexing, refining,
// making directive tables; the rest of a run includes initialization and the main loop with its directives.
typedef enum PPPhase
{
    PP_PHASE_READ,
    PP_PHASE_LEX,
    PP_PHASE_REFINE,
    PP_PHASE_DIRECTIVES,
    PP_PHASE_COUNT,
} PPPhase;

// Every file a preprocessor reads, and what is prepared from it, shared by the translation units that use it ("Files").
typedef struct FileCache FileCache;

// Whether '$' is an identifier character, clang's DollarIdents: as the mode has it (C's identifiers and pp-numbers take
// it, and in assembly it is punctuation), unless -fdollars-in-identifiers or -fno-dollars-in-identifiers set it.
typedef enum PPDollars
{
    PP_DOLLARS_MODE,
    PP_DOLLARS_IDENTIFIER,
    PP_DOLLARS_PUNCTUATION,
} PPDollars;

// Whether the lexer takes raw string literals, clang's RawStringLiterals: as the mode has them (GNU modes from C99 on,
// assembler-with-cpp among them), unless -fraw-string-literals or -fno-raw-string-literals set it in any mode.
typedef enum PPRawStrings
{
    PP_RAW_STRINGS_MODE,
    PP_RAW_STRINGS_ON,
    PP_RAW_STRINGS_OFF,
} PPRawStrings;

// One non-regular primary input, read to EOF before any unit or replay opens it. `path` borrows the command's argv.
typedef struct PPStreamInput PPStreamInput;
struct PPStreamInput
{
    const char* path;
    String8 bytes;
};

#include "c_driver_inputs.c"

typedef struct PPDriverInput PPDriverInput;
struct PPDriverInput
{
    char* path;
    CheckDriverInputKind kind;
    bool explicit_language;
};

typedef struct PPOptions PPOptions;
struct PPOptions
{
    DependencyOptions dependencies;
    // Frozen command-owned stdin bytes, shared read-only by a unit and its diagnostic replay.
    String8 stdin_source;
    bool stdin_ready;
    // Named non-regular inputs, read once the same way. A replay and a second pass see these bytes, not the stream.
    PPStreamInput* streams;
    uint32_t stream_count;
    bool explicit_language;
    // A .i input is already preprocessed. Tokens copied to the output are not expanded again; directives still run.
    bool preprocessed;
    bool verbose;
    // The checker's implicit-function diagnostic: 0 follows the language mode, 1 ignores, 2 warns, 3 errors.
    uint8_t implicit_function_diagnostic;
    uint8_t implicit_library_diagnostic;
    // An unknown builtin's (a `__builtin_` name's) implicit declaration, clang's DefaultError warn_builtin_unknown in every
    // language mode before C23: the same values, 0 an error (#2121).
    uint8_t implicit_builtin_diagnostic;
    bool implicit_function_system;
    // The file cache to read files through, made with file_cache_initialize for the same language mode and intern
    // table; or null, for a cache of the translation unit's own.
    FileCache* files;
    CLanguage language;
    // The generated code's instruction set (x86_target.c), from -march and the -m flags (predefines_target): for the
    // assembler's forms and __has_builtin's target builtins.
    X86Target target;
    // Retained independently for the future scheduler; it cannot enable an ISA feature.
    X86Architecture tune;
    bool freestanding;
    // Plain library names only: explicit __builtin_ spellings remain available. Names borrow command-line storage.
    bool no_builtin;
    const char* const* no_builtin_functions;
    uint32_t no_builtin_function_count;
    // The target's and the other options' predefined macros (c_predefines.c): -march, the -m feature flags, -O, -f...,
    // -g. All zero is clang's defaults with no option.
    PredefinesOptions predefines;
    // The scalar twins of the vectorized kernels, for test-preprocessor's differential checks.
    bool scalar;
    bool no_standard_includes;
    // In-memory consumers can refuse every external include/embed lookup,
    // including absolute paths and __has_include/__has_embed queries.
    bool no_external_files;
    // -nobuiltininc: no compiler headers (clang's resource directory, or buster's) among the standard directories.
    bool no_builtin_includes;
    // --buster-headers: buster's own headers (src/buster/include, found from the executable) in place of clang's
    // resource directory.
    bool buster_headers;
    // Opt-in assembler-with-cpp input: '$' is punctuation (unless `dollars` says otherwise), and unknown # lines are
    // assembly comments.
    bool assembly;
    // A PPDollars, zero for the mode's (pp_dollar_punctuation).
    uint8_t dollars;
    // A PPRawStrings, zero for the mode's (pp_raw_strings).
    uint8_t raw_strings;
    // -iquote, -I and -isystem directories, and -idirafter ones after the standard directories.
    const char* const* quote_directories;
    uint32_t quote_directory_count;
    const char* const* include_directories;
    uint32_t include_directory_count;
    const char* const* system_directories;
    uint32_t system_directory_count;
    const char* const* after_directories;
    uint32_t after_directory_count;
    // -D and -U in command-line order: "Dname", "Dname=value" or "Uname".
    const char* const* macros;
    uint32_t macro_count;
    // -include files, read before the main file.
    const char* const* includes;
    uint32_t include_count;
    // --embed-dir directories: where #embed looks for a quoted name after the including file's directory and the working
    // directory, and for an angled name alone (pp_find_embed).
    const char* const* embed_directories;
    uint32_t embed_directory_count;
    // A file of #define lines that replaces the built-in predefines (test-preprocessor gives clang's own).
    const char* predefines_path;
    // Measure the phases (PPPhase) in Preprocessor.ticks and lookup intervals/counters in Preprocessor.statistics.
    bool timing;
    // Time the checker's expressions' walk by class (CheckInput.expression_class_ticks, #1038): three TSC reads a token
    // the coded walk's classes reach. Off in every compile; benchmark-checker-bindings sets it for runs of its own,
    // apart from its samples.
    bool expression_timing;
    // After the coded walk, number its tokens again as the graph by passes would (CheckInput.expression_numbering_*,
    // #1038's stages 2 and 4), timed and checked against the walk's graph. benchmark-checker-bindings sets it with
    // expression_timing.
    bool expression_numbering;
    // Keep where the expansions that can start a line start in the output (PPExpansionStart), which -E's line markers
    // need to place them: the printer can't print markers for a run without them.
    bool expansion_starts;
    // Semantic consumers expand supported pragma operands at their original source point. Ordinary -E keeps them raw.
    bool semantic_pragmas;
    // Replace trigraphs (translation phase 1): clang's default in the ISO modes before C23, and -trigraphs,
    // -ftrigraphs or -fno-trigraphs.
    bool trigraphs;
    // -undef: none of the predefined macros but the ones clang keeps with it (__STDC__, __STDC_VERSION__,
    // __STDC_HOSTED__, __STDC_UTF_16__, __STDC_UTF_32__ and #embed's results), and no __BUSTER__.
    bool undefine;
    // Count the diagnostics without printing them: test-preprocessor's fuzz runs compare acceptance, and print a
    // difference's diagnostics when they run its case again.
    bool quiet;
    // -Wsystem-headers overrides location-based suppression of ordinary preprocessing warnings.
    bool system_header_warnings;
    // Ordered semantic warning options; a valued option's operand is never captured as diagnostic policy. The
    // arena owns the pointer slice; argument strings have the same borrowed lifetime as -D/-I option strings.
    const char* const* semantic_warning_options;
    uint32_t semantic_warning_option_count;
    // Diagnostic publication policy. Zero leaves errors unlimited; the command-line default permits nineteen
    // ordinary errors, followed by Clang's twentieth, fatal cutoff. Suppressed warnings are not counted.
    uint32_t error_limit;
    // -Wfatal-errors: the first command-line or default error, including a warning mapped to one, is published as a
    // fatal error and stops publication; -Wno-fatal-errors restores the ordinary error budget.
    bool fatal_errors;
    bool suppress_warnings;
    // The default-on negative-to-unsigned #if warning has no individual Clang warning group.
    bool negative_conversion_error;
    bool no_diagnostic_summary;
    // -dM: -E prints the macros defined at the end (pp_print_macros) instead of the tokens.
    bool dump_macros;
    // -H (#1380): each header the unit enters, as clang prints it (pp_show_include). It records the history as the
    // unit runs (no defer_history), so that the headers' lines and the diagnostics come out in the order clang's do.
    bool show_includes;
    // --clang-features: the feature queries (__has_builtin, __has_attribute, __has_feature, __has_extension,
    // __has_c_attribute, __has_constexpr_builtin) answer as clang 23 does, from pp_features_*: the oracle's mode
    // (#125). By default they answer what buster implements (pp_implemented_*; #123, #148, #154).
    bool clang_features;
    // Recording is on by default; the benchmark's control disables it without changing preprocessing.
    bool no_history;
    // Record the expansion history only once something needs it (pp_history_ensure): pp_run and pp_run_source then
    // record none, and preprocess the unit again, with history, when a diagnostic or a consumer of the history first
    // asks for it (into a preprocessor of its own, with these options, file cache and intern table). Their own
    // diagnostics are counted, not printed, and a unit that had any runs again at its end to print them with their
    // notes, as it would have. A clean unit never runs again. -E, `parse` and the checker's driver defer it; the checker
    // judges system headers' tokens from the main loop's runs (pp_system_levels), and records the history only for
    // --scalar's twin and a unit whose runs can't say. The unit preprocesses nothing more after its run, and its options'
    // no_history stays set.
    bool defer_history;
    // Enter a file whose include guard is defined rather than skip it (#120's control: the output is the same).
    bool no_guard_skip;
    // Give every chunk's spellings by the vector printer's general loop, token by token, as before pp_print_spellings
    // (#649's control, benchmark-preprocessor --spelling-cost: the printed bytes are the same, and the copies are the
    // printer's own, its batches with line markers too; docs/c-preprocessor.md).
    bool print_per_token;
    // The location space's size, for tests that run out of it (--location-limit N; 0 is the whole 4 GB).
    uint32_t location_limit;
    // The bytes of the windows a large file is lexed and refined in (pp_prepare_windows): 0 for BUSTER_PP_WINDOW_BYTES.
    // Tests make them small, so that small files take the windows too.
    uint32_t window_bytes;
    // Null means stderr. Tests use a memory stream for the same renderer as the command line.
    FILE* diagnostics;
    // Source checking collects complete groups before applying the publication budget in expansion order. Other
    // consumers keep immediate publication. Storage is taken only when a visible diagnostic is collected.
    bool collect_diagnostics;
};

// Is '$' punctuation with these options (PPDollars)? The lexer takes it into identifiers and pp-numbers, and
// pp_split_dollars splits it out of them where it isn't.
static bool pp_dollar_punctuation(const PPOptions* options)
{
    return options->dollars == PP_DOLLARS_PUNCTUATION || (options->dollars == PP_DOLLARS_MODE && options->assembly);
}

// The lexer's flags for raw string literals with these options (PPRawStrings): C_TOKENIZER_RAW_STRINGS for their R and
// LR forms, and from C11, whose u8, u and U prefixes they need, C_TOKENIZER_UTF_RAW_STRINGS for those forms too.
static uint32_t pp_raw_strings(const PPOptions* options)
{
    const CLanguage language = options->language;
    const bool on = options->raw_strings == PP_RAW_STRINGS_ON || (options->raw_strings == PP_RAW_STRINGS_MODE && language.gnu && language.standard >= 1999);
    return on ? C_TOKENIZER_RAW_STRINGS | (language.standard >= 2011 ? C_TOKENIZER_UTF_RAW_STRINGS : 0) : 0;
}

// A column array of tokens: an output stream, or a stack of buffers.
typedef struct PPTokens PPTokens;
struct PPTokens
{
    uint8_t* kinds;
    uint8_t* flags;
    uint32_t* values;
    uint32_t* locations;
    uint32_t count;
    uint32_t capacity;
    // The output's columns, tables of one arena (arena_reserve_tables, #1128) whose first pages are each base pages as an
    // arena of its own's would be (lane.c): an output of a few tokens touches a few kilobytes, where columns side by side
    // would each start in a 2 MB huge page the kernel clears. A stack's columns are tables of the unit's (pp_tokens_plan,
    // #652), and this is empty.
    Arena columns;
    // Temporary buffers only. The final output has no origin column, just PPHistoryRun records.
    uint32_t* origins;
    uint32_t origin_capacity;
    Arena origin_arena;
};

typedef struct Preprocessor Preprocessor;

BUSTER_PREPROCESSOR_ENTRY __attribute__((unused)) bool pp_initialize(Preprocessor* pp, const PPOptions* options, InternTable* table);
BUSTER_PREPROCESSOR_ENTRY __attribute__((unused)) bool pp_run(Preprocessor* pp, const char* main_path);
BUSTER_PREPROCESSOR_ENTRY __attribute__((unused)) bool pp_run_source(Preprocessor* pp, const char* name, const uint8_t* bytes, uint32_t length);
static __attribute__((unused)) void pp_release(Preprocessor* pp);

// ---------------------------------------------------------------------------------------------------------------------
// The state

#define BUSTER_PP_NONE UINT32_MAX
// The bytes of the windows pp_prepare_windows lexes and refines a file of at least two of them in: a window's source and
// columns, about 280 KB, fit the 9700X's 1 MB L2 cache beside the intern table's lines that refinement reads.
#define BUSTER_PP_WINDOW_BYTES (128u << 10)
// The longest part of a line that a window's end may cut before the window is cut before the line instead
// (pp_window_cut): longer than any but a few lines, so that a window of ordinary lines ends where it would.
#define BUSTER_PP_WINDOW_TAIL_BYTES (16u << 10)
// The pages of a file prepared a window at a time whose line ends it counts while each window is in the caches
// (PPFileData.line_pages): __LINE__ and diagnostics count lines from the page's start at most, whose bytes left the
// caches long before, rather than from the last place asked about.
#define BUSTER_PP_LINE_PAGE_BYTES 4096u
// The bytes of scratch spellings per location range, and the most the scratch arena holds.
#define BUSTER_PP_SCRATCH_CHUNK_BYTES (64u << 10)
#define BUSTER_PP_SCRATCH_BYTES (1ull << 32)

// A directive, by its name.
typedef enum PPDirectiveKind
{
    PP_DIRECTIVE_NULL, // '#' alone on its line.
    PP_DIRECTIVE_UNKNOWN, // An unknown name, or a token that isn't an identifier.
    PP_DIRECTIVE_LINE_MARKER, // `# 33 "file.h" 2`, as in preprocessed source.
    PP_DIRECTIVE_DEFINE,
    PP_DIRECTIVE_UNDEF,
    PP_DIRECTIVE_INCLUDE,
    PP_DIRECTIVE_INCLUDE_NEXT,
    PP_DIRECTIVE_IMPORT,
    PP_DIRECTIVE_IF,
    PP_DIRECTIVE_IFDEF,
    PP_DIRECTIVE_IFNDEF,
    PP_DIRECTIVE_ELIF,
    PP_DIRECTIVE_ELIFDEF,
    PP_DIRECTIVE_ELIFNDEF,
    PP_DIRECTIVE_ELSE,
    PP_DIRECTIVE_ENDIF,
    PP_DIRECTIVE_LINE,
    PP_DIRECTIVE_ERROR,
    PP_DIRECTIVE_WARNING,
    PP_DIRECTIVE_PRAGMA,
    PP_DIRECTIVE_IDENT,
    PP_DIRECTIVE_ASSERT,
    PP_DIRECTIVE_EMBED,
} PPDirectiveKind;

// What a directive that a skip passes over must still report, as clang reports it while it skips (the file's notes,
// PPFileData.note_*): an #elif, #elifdef, #elifndef or #else after its group's #else (an error); an #if, #ifdef or
// #ifndef whose group has no #endif, when the skip goes to the end of the file (an error, innermost first); and an
// #elifdef or #elifndef, a C23 extension before C23 (a warning, before the error).
#define BUSTER_PP_NOTE_AFTER_ELSE 1
#define BUSTER_PP_NOTE_UNTERMINATED 2
#define BUSTER_PP_NOTE_ELIFDEF 4

// A file's contents and everything prepared from them, shared by the paths that name it (by device and inode). The file
// cache's record of a file is one, and so is a translation unit's copy of it, which also has the unit's own state about
// the file: `once`, the line cursor, and `prepared`, which says the unit has copied the record's prepared fields (a
// copy's other fields are the record's).
typedef struct PPFileData PPFileData;
// PPRawError's kind for a named universal character name (\N{...}) that starts an identifier or an unknown token and
// isn't a character such a token may have: a name that isn't Unicode's, or a character below U+00A0
// (pp_diagnose_named_ucn words clang's errors, #1447). One within a token is read without them, as clang reads it.
#define BUSTER_PP_NAMED_UCN_ERROR 0x80
// A named UCN that starts a token as clang reads it whole, its name exactly Unicode's (#1447): -pedantic's warning that it
// is clang's extension (pp_diagnose_named_ucn), before its character's error, if any (then it is
// BUSTER_PP_NAMED_UCN_ERROR's).
#define BUSTER_PP_NAMED_UCN_EXTENSION 0x81
// A `\N` where a token starts or an identifier goes on that clang reads as no UCN (#1447): -Wunicode's warnings, which
// treat its backslash as itself (pp_diagnose_named_ucn_shape): no `{` after it, an empty name (`\N{}`), or a name with
// no `}` before the line's end.
#define BUSTER_PP_NAMED_UCN_INCOMPLETE 0x82
#define BUSTER_PP_NAMED_UCN_EMPTY 0x83
#define BUSTER_PP_NAMED_UCN_UNTERMINATED 0x84
// The same of a `\u` or `\U` clang reads as no UCN (#1447): no hex digit after it (its letter's "\u used with no following
// hex digits"), too few (BUSTER_PP_NAMED_UCN_INCOMPLETE), or a delimited one's empty braces or no `}` before a byte that
// isn't a hex digit (BUSTER_PP_NAMED_UCN_EMPTY, BUSTER_PP_NAMED_UCN_UNTERMINATED). The words name the source's letter.
#define BUSTER_PP_UCN_NO_DIGITS 0x85
// A `\u` or `\U` that starts a token and that clang reads whole, of a character below U+00A0 or a surrogate (#1447):
// clang's error at its backslash (pp_diagnose_ucn), "character 'c' cannot be specified by a universal character name",
// "universal character name refers to a control character" or "invalid universal character".
#define BUSTER_PP_UCN_ERROR 0x86
// The bytes of a bad name pp_diagnose_named_ucn's error prints, at most.
#define BUSTER_PP_NAMED_UCN_NAME_BYTES 1024

typedef struct PPRawError PPRawError;
struct PPRawError
{
    uint32_t token;
    uint32_t offset;
    // A CRawStringError, or a named UCN's (BUSTER_PP_NAMED_UCN_ERROR and those after it).
    uint8_t kind;
};

struct PPFileData
{
    uint64_t device;
    uint64_t inode;
    // The file's modification time (for __TIMESTAMP__), and whether it has one (a file the preprocessor made has none).
    int64_t modified;
    bool has_modified;
    const uint8_t* bytes;
    // Only a private contextual view: this inclusion's masking buffer, shared by its successive snapshots. Only the
    // current file context probes it; macro snapshots read original bytes and immutable token/directive columns.
    const uint8_t* lexical_bytes;
    uint32_t length;
    // The bytes are a mapping of the file, which the record's cache unmaps (file_cache_release), not arena memory.
    bool mapped;
    // A translation unit's copy: the file cache's record it copies, or BUSTER_PP_NONE for a file the unit made (the
    // predefines, the command line), which isn't in the cache.
    uint32_t record;
    // First observed dependency + 1 in this unit, never in the shared cache; zero means unobserved.
    uint32_t dependency;
    uint32_t token_count;
    // The exceptional tokens the lexer reads with an error (an invalid raw string literal's UNKNOWN token, or a token with
    // a named UCN), sorted by token; emission waits until their source context is known.
    PPRawError* raw_errors;
    uint32_t raw_error_count;
    // The lexer's kinds and starts, refinement's values (in place of the ends) and leading-space bits.
    uint8_t* kinds;
    uint32_t* starts;
    uint32_t* values;
    uint64_t* space;
    // The directive table: per directive, its '#' token, the token past its line, its kind, and for a conditional the
    // index of the next directive of its group (#elif, #else or #endif), or BUSTER_PP_NONE if the group is unterminated.
    uint32_t directive_count;
    uint32_t* directive_hash;
    uint32_t* directive_end;
    uint8_t* directive_kind;
    uint32_t* directive_next;
    // The directives a skip must still report (BUSTER_PP_NOTE_*), in order: each one's index and notes. Most files have
    // none (pp_note_directives).
    uint32_t note_count;
    uint32_t* note_directives;
    uint8_t* note_kinds;
    // The include guard's macro (0 if the file has none), whether an inclusion that took the guard's group has ended,
    // from when on the guard skips the file (clang knows a file's guard from the end of such an inclusion, so it enters
    // the file again from inside it, and every time while the macro was defined before the first), and whether #pragma
    // once (or #import) read it.
    IdentId guard;
    uint32_t guard_opener; // The guard's #ifndef or #if: the directive after the null ones that start the file.
    bool guarded;
    bool once;
    bool prepared;
    // Whether an inclusion has taken it, as clang's markIncluded records it: entered, or refused at the depth limit
    // (pp_enter_file), which doesn't prepare it.
    bool included;
    // Where an unterminated block comment starts, plus 1, or 0: the lexer makes it the last token, which preparing the
    // file drops. Reading it in an active group reports it; a skipped group suppresses it, as clang's lexer does.
    uint32_t unterminated_comment;
    bool unterminated_comment_starts_line;
    // A place whose line is known, from which pp_line_of counts lines forward: the preprocessor asks about later places
    // as it goes (__LINE__, diagnostics), so each file's lines are counted about once.
    uint32_t line_cursor;
    uint32_t line_cursor_line;
    // For a file prepared a window at a time (pp_prepare_windows), the line ends before each page of its bytes
    // (BUSTER_PP_LINE_PAGE_BYTES): entry p counts those in bytes [0, p * BUSTER_PP_LINE_PAGE_BYTES), a "\r\n" at its '\n'.
    // pp_line_of counts from a page's start with it, from the cursor only on the same page. Null for the others.
    uint32_t* line_pages;
    // Built on the first diagnostic in this file, then shared by all of its inclusions.
    uint32_t* diagnostic_lines;
    uint32_t diagnostic_line_count;
};

// A path to a file, as found: what __FILE__, line markers and the quoted includes it holds go by.
typedef struct PPFile PPFile;
struct PPFile
{
    char* path; // NUL-terminated.
    uint32_t path_length;
    // The length of the directory part (before the last '/'), or BUSTER_PP_NONE if there is none.
    uint32_t directory_length;
    uint32_t data;
    // Why it has no data: an errno value, or -1 for a file that is neither regular nor a directory (0 when it has data).
    int error;
    // BUSTER_PP_USER_FILE, BUSTER_PP_SYSTEM_FILE or BUSTER_PP_EXTERN_C_FILE: how its inclusions start.
    uint8_t system;
};

// What a file found through a search directory is, as clang's line markers say (SrcMgr::CharacteristicKind): a user
// file, a system header (flag 3: -isystem, -idirafter and clang's resource directory), or a system header clang would
// wrap in extern "C" for C++ (flags 3 4: the standard directories, -internal-externc-isystem in clang's driver).
#define BUSTER_PP_USER_FILE 0
#define BUSTER_PP_SYSTEM_FILE 1
#define BUSTER_PP_EXTERN_C_FILE 2

// The file cache. A record per file, by identity (device and inode), holds its bytes (a read-only mapping) and, once it
// is prepared (file_cache_prepare), its tokens, leading-space bits, directive table and guard: a record never changes
// after that, so that any number of translation units and lanes read it at once. A path entry per path looked up names
// the path's record, or no record (no file, a directory, a file too big or unreadable), so that a translation unit's
// search makes no system call for a path the cache already looked up. One lane at a time adds paths and records, but
// lanes prepare different records at once, each into arenas of its own (FileCacheArenas) and with an intern table only
// it writes (c_files.c).

// The system calls lookups made, counted for whoever made them: open attempts (one per uncached path, whether or not a
// file is there), fstat attempts (one per successful open, even for a non-regular file or a failed fstat), and files
// mapped/read (the latter counts file-read operations, not individual pread attempts). Each unit/lane owns its counts.
// `listings` counts directories whose names were read (file_cache_list_directory: an open, an fstatfs, getdents64 to the
// end and a close, or fewer system calls if one of them fails), and `listed` the paths a listing answered without a
// system call.
typedef struct FileCacheCounts FileCacheCounts;
struct FileCacheCounts
{
    uint64_t opens;
    uint64_t fstats;
    uint64_t maps;
    uint64_t reads;
    uint64_t listings;
    uint64_t listed;
};

// What a probe of a path found (file_cache_probe): whether a regular file can be read there, its identity and
// modification time, and either the record that already had that identity when it looked (BUSTER_PP_NONE if none) or
// its bytes, which the probe mapped (`mapped`) or read into an arena; or why no file can be read there, an errno value
// or -1 for one that is neither a regular file nor a directory.
typedef struct FileProbe FileProbe;
struct FileProbe
{
    uint64_t device;
    uint64_t inode;
    int64_t modified;
    const uint8_t* bytes;
    uint32_t length;
    uint32_t record;
    int error;
    bool found;
    bool mapped;
};

// How a probe gets a file's bytes: mapped with MAP_POPULATE (a translation unit's, and one lane's), mapped without it
// (the mapping holds the process's memory map lock for less time, and its pages are faulted in as they are first read),
// or read into an arena. A translation unit and a lane read small files whatever this says (file_cache_probe's
// `read_limit`).
typedef enum FileCacheBytes
{
    FILE_CACHE_MAP_POPULATED,
    FILE_CACHE_MAP,
    FILE_CACHE_READ,
} FileCacheBytes;

// A path entry's record from its probe until file_cache_identify gives it one.
#define BUSTER_FILE_CACHE_PENDING (UINT32_MAX - 1)

// A path the cache looked up: its record, BUSTER_PP_NONE if no file can be read there, or BUSTER_FILE_CACHE_PENDING.
typedef struct FileCachePath FileCachePath;
struct FileCachePath
{
    uint64_t hash;
    const char* path; // NUL-terminated.
    uint32_t length;
    uint32_t record;
    FileProbe probe;
};

// A directory the include search looked up paths in (a search directory, or an includer's), which the cache counts the
// failed probes of: the paths in it that had no file (ENOENT). When the count reaches BUSTER_FILE_CACHE_LISTING_MISSES,
// the cache reads the directory's names once (file_cache_list_directory). The listing then answers a path in the
// directory whose first component it doesn't have, with no system call (file_cache_listed_absent). It is about as dear
// as ten to twenty failed probes, so a short search path or a small unit never pays for one (#653).
typedef struct FileCacheDirectory FileCacheDirectory;
struct FileCacheDirectory
{
    uint64_t hash;
    const char* path; // NUL-terminated.
    uint32_t length;
    uint32_t misses;
    // The listed names' keys (file_cache_name_key, never 0), in name_mask + 1 slots, or null: not listed yet, put off
    // (`deferred`: more names than a first look reads), or (`unlisted`) a directory a listing can't answer for exactly.
    const uint32_t* names;
    uint32_t name_mask;
    bool deferred;
    bool unlisted;
    FileCacheDirectory* next;
};

// The chains of directories the cache looked up paths in, by hash: a search directory and every includer's directory
// (for quoted names) get one. A lookup comes here only for a path the cache never looked up, which costs a probe or
// skips one, so a few directories a chain are cheap.
#define BUSTER_FILE_CACHE_DIRECTORY_SLOTS 256u

// The arenas a lane prepares records into: the token columns (kinds, starts, values), each in an arena of its own as a
// translation unit's are, the leading-space words and directive tables, and the bytes of the files it reads.
typedef struct FileCacheArenas FileCacheArenas;
struct FileCacheArenas
{
    Arena columns[3];
    Arena arena;
    Arena bytes;
};

// How files are prepared: the lexer's and refinement's flags (trigraphs, and raw strings: pp_raw_strings), the literal
// prefixes the language mode splits (pp_split_kinds), whether '$' is punctuation (pp_dollar_punctuation), whether by the
// vectorized kernels or by their scalar twins, which prepare the same records, and the bytes of the windows a large file
// is lexed and refined in (pp_prepare_windows; 0 for BUSTER_PP_WINDOW_BYTES).
typedef struct PPPrepare PPPrepare;
struct PPPrepare
{
    uint32_t refine_flags;
    uint64_t split_kinds;
    bool dollar_punctuation;
    bool vectorized;
    uint32_t window_bytes;
};

struct FileCache
{
    uint32_t capacity_shift;
    PPPrepare prepare;
    // The intern table file_cache_prepare refines records with.
    InternTable* table;
    // The records, and their index by identity (a record's index + 1, or 0).
    PPFileData* records;
    uint32_t record_count;
    uint32_t* identity_slots;
    uint32_t identity_slot_mask;
    // The paths, their index by the path's hash (a path's index + 1, or 0), and their bytes.
    FileCachePath* paths;
    uint32_t path_count;
    uint32_t* path_slots;
    uint32_t path_slot_mask;
    // The paths' bytes, and the directories (FileCacheDirectory) and their listings, which a directory gets at its
    // `listing_misses`-th failed probe (BUSTER_FILE_CACHE_LISTING_MISSES; 0 for never, the tests' oracle).
    Arena path_arena;
    FileCacheDirectory* directory_slots[BUSTER_FILE_CACHE_DIRECTORY_SLOTS];
    uint32_t listing_misses;
    // The directories with a listing: until there is one, a lookup that finds a file looks no directory up.
    uint32_t listed_directories;
    // The four arrays' arena of tables, file_cache_initialize's (#1521), or empty where they are tables of a unit's
    // (pp_initialize_once).
    Arena arrays;
    // The records prepared, and their tokens (for the tests and the benchmark).
    uint64_t prepared_records;
    uint64_t prepared_tokens;
    // The largest file a lookup (file_cache_lookup_path: a translation unit's) reads into its lane's bytes arena rather
    // than maps: BUSTER_FILE_CACHE_READ_LIMIT, unless benchmark-preprocessor's --read-limit sets another.
    uint32_t read_limit;
    // The base pages at the start of the token columns' arenas of the lanes past lane 0, which c_files.c sets for a group
    // of many lanes before it reserves them (file_cache_arenas), or 0 for arena_reserve's.
    uint64_t lane_small_bytes;
    // The lanes whose arenas file_cache_arenas has reserved, or tried to, a bit each: a lane whose bit is clear has its
    // arenas all zero. What walks the lanes' arenas (file_cache_release, pp_release_file_columns) walks these alone
    // (#1444): `lanes` is 130 KB, 32 pages, of which a compile on one lane uses the first, and a walk of every lane read
    // and wrote every page, 63 of an empty file's 98 faults in its preprocessor's tables, where the cache is.
    uint64_t lanes_reserved[BUSTER_LANE_MAX_COUNT / 64];
    // Each lane's arenas, reserved when the lane first prepares a record. Last, so that the fields before it are
    // cleared without its pages (file_cache_release).
    FileCacheArenas lanes[BUSTER_LANE_MAX_COUNT];
};

// A range of the location space: one inclusion of a file, or a chunk of scratch spellings (`file` BUSTER_PP_NONE).
typedef struct PPRange PPRange;
struct PPRange
{
    uint32_t base;
    uint32_t length;
    uint32_t file;
    const uint8_t* bytes;
    // The inclusion that included it, and where (for line markers), or BUSTER_PP_NONE.
    uint32_t parent;
    uint32_t include_location;
    // Its line notes (indices in Preprocessor.line_directives, linked by next_in_range), or BUSTER_PP_NONE.
    uint32_t first_line_directive;
    uint32_t last_line_directive;
    // What the file was when this inclusion started (PPFile.system then: #pragma GCC system_header changes it later).
    uint8_t system;
    // The line markers with flag 1 in it that no marker with flag 2 has left yet (pp_apply_line).
    uint32_t marker_entries;
    // One past the output boundary where argument collection left this inclusion, or zero. Only -E records it.
    uint32_t collected_exit;
};

// FileChanged runs before the collected invocation emits anything. Keep these rare exits in execution order;
// ordinary inclusions still need no event allocation. Locations and expansion history retain their source meaning.
typedef struct PPIncludeExit PPIncludeExit;
struct PPIncludeExit
{
    PPIncludeExit* next;
    uint32_t token;
    uint32_t range;
    uint32_t location;
};

// A line note, from a #line or a #pragma GCC system_header: from `location` on in its range, lines are `delta` more
// than the physical ones, the file is `name` if it isn't null, and it is a `system` one (BUSTER_PP_USER_FILE, ...).
typedef struct PPLineDirective PPLineDirective;
struct PPLineDirective
{
    uint32_t range;
    uint32_t location;
    int64_t delta;
    const char* name;
    uint32_t name_length;
    // The range's next note, or BUSTER_PP_NONE.
    uint32_t next_in_range;
    // Where a line marker's flag 1 entered the file its lines are presumed in, as clang's LineTableInfo keeps it (#1954):
    // the location right before the marker's number, or for flag 2 or none the include location the marker returns to or
    // keeps; BUSTER_PP_NONE for the range's own inclusion's.
    uint32_t include_location;
    uint8_t system;
};

// Where the output comes from, for the checker's system-header judgment (pp_system_levels): from output token `token`
// on, the main loop reads range `range` (or BUSTER_PP_NONE) at system level `level` (as PPLineDirective.system), its
// tokens there at or after `from` (a line note's location, or the range's base). A run starts at an inclusion's start
// and end and at a line note.
typedef struct PPSystemRun PPSystemRun;
struct PPSystemRun
{
    uint32_t token;
    uint32_t range;
    uint32_t from;
    uint8_t level;
};

// The output token at `token` and the ones after it come from the expansion of a macro whose name, at `location`,
// was read from a file into the output (or none, until the next start): -E's line markers place such a token that
// starts a line on the line of that name, as clang places a token by its expansion location. A start is recorded where
// such a token can start a line: for a name that starts a line (whose first token does), and for a _Pragma (its '#',
// and the token after its line).
typedef struct PPExpansionStart PPExpansionStart;
struct PPExpansionStart
{
    uint32_t token;
    uint32_t location;
};

// An #embed's data in the output (BUSTER_PP_KIND_EMBED): `length` bytes of a file's data (PPFileData), from `offset`.
typedef struct PPEmbed PPEmbed;
struct PPEmbed
{
    uint32_t data;
    uint32_t offset;
    uint32_t length;
};

// Macro flags.
#define BUSTER_PP_MACRO_FUNCTION 0x01
#define BUSTER_PP_MACRO_VARIADIC 0x02 // Its last parameter is `...` or `name...`.
#define BUSTER_PP_MACRO_COMPILED 0x04 // pp_compile_macro made its body items.
#define BUSTER_PP_MACRO_BUILTIN 0x08
#define BUSTER_PP_MACRO_PASTES 0x10 // Its body has ## (an object-like macro then needs a buffer).
// Its body has no __VA_OPT__: its arguments are expanded in an order known from its definition, its parameters' first
// expanded uses, which pp_compile_macro lists after its items (pp_continue_invocation).
#define BUSTER_PP_MACRO_IN_ORDER 0x20
// #pragma push_macro pushed it: a redefinition is no warning (clang's isAllowRedefinitionsWithoutWarning).
#define BUSTER_PP_MACRO_REDEFINABLE 0x40
// A compiled BUSTER_PP_MACRO_IN_ORDER macro whose body has only tokens and parameters (no #, ## or __VA_OPT__): its
// invocations can be finished without the invocation stack (pp_finish_simple).
#define BUSTER_PP_MACRO_SIMPLE 0x80

// The built-in macros.
typedef enum PPBuiltin
{
    PP_BUILTIN_NONE,
    PP_BUILTIN_FILE,
    PP_BUILTIN_LINE,
    PP_BUILTIN_COUNTER,
    PP_BUILTIN_DATE,
    PP_BUILTIN_TIME,
    PP_BUILTIN_TIMESTAMP,
    PP_BUILTIN_BASE_FILE,
    PP_BUILTIN_FILE_NAME,
    PP_BUILTIN_INCLUDE_LEVEL,
    PP_BUILTIN_FLT_EVAL_METHOD,
    PP_BUILTIN_PRAGMA,
    PP_BUILTIN_DEFINED,
    PP_BUILTIN_HAS_INCLUDE,
    PP_BUILTIN_HAS_INCLUDE_NEXT,
    PP_BUILTIN_HAS_ATTRIBUTE,
    PP_BUILTIN_HAS_BUILTIN,
    PP_BUILTIN_HAS_CONSTEXPR_BUILTIN,
    PP_BUILTIN_HAS_FEATURE,
    PP_BUILTIN_HAS_EXTENSION,
    PP_BUILTIN_HAS_C_ATTRIBUTE,
    PP_BUILTIN_HAS_CPP_ATTRIBUTE,
    PP_BUILTIN_HAS_DECLSPEC_ATTRIBUTE,
    PP_BUILTIN_HAS_WARNING,
    PP_BUILTIN_HAS_EMBED,
    PP_BUILTIN_IS_IDENTIFIER,
    PP_BUILTIN_IS_TARGET_ARCH,
    PP_BUILTIN_IS_TARGET_VENDOR,
    PP_BUILTIN_IS_TARGET_OS,
    PP_BUILTIN_IS_TARGET_ENVIRONMENT,
    PP_BUILTIN_IS_TARGET_VARIANT_OS,
    PP_BUILTIN_IS_TARGET_VARIANT_ENVIRONMENT,
    PP_BUILTIN_BUILDING_MODULE,
    PP_BUILTIN_POISONED, // A name #pragma GCC poison poisoned: using it is an error.
} PPBuiltin;

typedef struct PPMacro PPMacro;
struct PPMacro
{
    IdentId name;
    uint8_t flags;
    uint8_t builtin;
    // Nonzero while its expansion is being rescanned.
    uint8_t disabled;
    uint16_t parameter_count; // With the variadic one.
    // A BUSTER_PP_MACRO_IN_ORDER macro's: how many parameters its arguments are expanded for, listed after its items.
    uint16_t order_count;
    // A BUSTER_PP_MACRO_SIMPLE macro's: how many of its body's tokens are identifiers, listed (as PP_ITEM_TOKEN items)
    // after the order (pp_emit_simple tests whether they name macros).
    uint16_t name_count;
    // Where its definition is: the file's data, the base of the range it was read in, the replacement list's tokens.
    uint32_t data;
    uint32_t base;
    uint32_t body_begin;
    uint32_t body_end;
    // The token of its first parameter (function-like), and its name's location.
    uint32_t parameters_begin;
    uint32_t location;
    // Its compiled body (pp->items) and parameters (pp->parameters), from pp_compile_macro.
    uint32_t items_begin;
    uint32_t item_count;
    uint32_t parameters;
    // A BUSTER_PP_MACRO_SIMPLE macro's template (pp->templates) plus 1, or 0 without one (pp_compile_macro).
    uint32_t template_index;
    // Bit i: parameter i appears as an operand of neither # nor ##, so it is substituted expanded. Bit 63 stands for
    // every parameter from 63 on.
    uint64_t expanded_parameters;
};

// A body item of a compiled macro.
typedef enum PPItemKind
{
    PP_ITEM_TOKEN, // A body token, copied; `parameter` counts consecutive body tokens after it, capped at UINT16_MAX.
    PP_ITEM_PARAMETER, // A parameter, substituted (expanded, or raw next to ##).
    PP_ITEM_STRINGIZE, // # and a parameter: its raw argument as a string literal.
    PP_ITEM_PASTE, // ## between the items before and after it.
    PP_ITEM_VA_OPT, // __VA_OPT__( ... ): `parameter` is the item count of its content, which follows.
    PP_ITEM_STRINGIZE_VA_OPT, // # __VA_OPT__( ... ), the same way.
} PPItemKind;

// An item's flags, for clang's NextTokGetsSpace (pp_substitute): a leading space on its token, but on the replacement
// list's first token and on one after ##, and the same of the other tokens a __VA_OPT__ has (pp_compile_macro).
#define BUSTER_PP_ITEM_SPACE 0x01 // The item's (first) token's, as BUSTER_PP_SPACE.
#define BUSTER_PP_ITEM_VA_OPT_SPACE 0x10 // # __VA_OPT__'s __VA_OPT__'s.
#define BUSTER_PP_ITEM_CLOSE_SPACE 0x20 // A __VA_OPT__'s closing ')''s.
#define BUSTER_PP_ITEM_SKIP_SPACE 0x40 // Any of a __VA_OPT__'s content's tokens' and its ')''s, which a skipped one reads.
_Static_assert(BUSTER_PP_ITEM_SPACE == BUSTER_PP_SPACE, "an item's space is its token's");

typedef struct PPItem PPItem;
struct PPItem
{
    uint8_t kind;
    uint8_t flags; // BUSTER_PP_ITEM_SPACE and the others.
    uint16_t parameter;
    uint32_t token; // The body token, in the defining file.
};

// The most tokens of a template's replacement list (PPTemplate), and of an expansion pp_emit_template writes and of each
// argument it uses; and the most templates a translation unit has.
#define BUSTER_PP_TEMPLATE_TOKENS 16
#define BUSTER_PP_MAX_TEMPLATES (1u << 20)

// A BUSTER_PP_MACRO_SIMPLE macro's replacement list of at most BUSTER_PP_TEMPLATE_TOKENS items as its expansions
// write it (pp_template_write), made with its items (pp_compile_macro): each body token as pp_store_file_tokens stores
// it from the macro (its kind, BUSTER_PP_FROM_MACRO and its leading space, and its value and location in the location
// space), each parameter's index, and the items' BUSTER_PP_ITEM_SPACE. In a simple macro an item is a body token, so
// position j is token body_begin + j; a template of a macro with ## and # (pp_template_operators) has an item per ##
// and per # with its parameter, whose place is the #'s. Its names of macros as pp_emit_simple_names last found them are kept
// (pp_template_names), with the macros' table they were found in (Preprocessor.macro_epoch).
typedef struct PPTemplate PPTemplate;
struct PPTemplate
{
    uint32_t values[BUSTER_PP_TEMPLATE_TOKENS];
    uint32_t locations[BUSTER_PP_TEMPLATE_TOKENS];
    // The object-like macro whose body goes straight in for a position whose bit is set in `expanding`.
    PPMacroId nested[BUSTER_PP_TEMPLATE_TOKENS];
    uint8_t kinds[BUSTER_PP_TEMPLATE_TOKENS];
    uint8_t flags[BUSTER_PP_TEMPLATE_TOKENS];
    // A parameter's index (below the macro's BUSTER_PP_TEMPLATE_TOKENS parameters), for a position whose bit is set in
    // `parameter_mask`.
    uint8_t parameters[BUSTER_PP_TEMPLATE_TOKENS];
    // The tokens the bodies of the names in `expanding` add to the expansion, past the names themselves.
    uint32_t extra;
    // The macros' table the names were found in plus 1 (0 when they weren't, or were found with a name painted because
    // its macro was disabled, which is a state of the expansions under way).
    uint32_t epoch;
    uint16_t parameter_mask;
    uint16_t spaces; // Bit j: item j's BUSTER_PP_ITEM_SPACE.
    uint16_t names; // The body's identifiers (the positions pp_emit_simple_names looks at), but the operands of ##.
    uint16_t painted; // Of those, the names of macros that don't expand.
    uint16_t expanding; // And those whose bodies go straight in (pp_direct_object).
    // A macro with ## and # (pp_compile_macro): its ## items, the first operand of each chain of pastes (an operand,
    // then ## and an operand as many times as it goes on), and its # items, each chain and # making one token or none in
    // an invocation (pp_template_operators). A # item's parameter is in `parameters`.
    uint16_t pastes;
    uint16_t chains;
    uint16_t ends; // The last operand of each chain.
    uint16_t strings;
    uint8_t count;
    // The items whose places pp_substitute leaves as the history's anchor and spelling (pp->history_anchor and
    // history_spelling), which pp_template_write leaves too: the last item its loop reads (a run's first token, a
    // parameter, a # or a ##), and for a ## the item before it.
    uint8_t anchor;
    uint8_t spelling;
    // Whether every name of a macro is painted or goes straight in (pp_emit_simple_names' result, without its conditions
    // on the run).
    bool direct;
};

// The most bytes the spellings of a template's chains of pastes take in one invocation (PPOperators.joined).
#define BUSTER_PP_TEMPLATE_JOINED_BYTES 256

// What one invocation of a template with ## and # makes (pp_template_operators, then pp_template_make): a token in the
// lane of each chain's first item that has one and of each # (`made`), with its kind, its flags but the leading space
// (which pp_template_write gives it), its value and its location, those of a paste once pp_template_make has made
// it. A chain whose operands give it two tokens or more pastes them (`pasted`): their spellings are joined in `joined`
// from `begins` at its lane, each operand's ending at `ends` at its item, so that each paste's spelling is a prefix,
// as pp_substitute's pastes go from left to right.
typedef struct PPOperators PPOperators;
struct PPOperators
{
    uint32_t values[BUSTER_PP_TEMPLATE_TOKENS];
    uint32_t locations[BUSTER_PP_TEMPLATE_TOKENS];
    uint8_t kinds[BUSTER_PP_TEMPLATE_TOKENS];
    uint8_t flags[BUSTER_PP_TEMPLATE_TOKENS];
    uint16_t begins[BUSTER_PP_TEMPLATE_TOKENS];
    uint16_t ends[BUSTER_PP_TEMPLATE_TOKENS];
    uint16_t made;
    uint16_t pasted;
    uint16_t operands; // The chains' operands that give a token.
    uint16_t painted; // The made names of macros that don't expand, painted when the expansion goes straight in.
    bool rescan; // Whether a made name is of a macro that expands: then the main loop reads the expansion again.
    uint32_t size; // The expansion's tokens, without what names that go straight in add.
    uint8_t joined[BUSTER_PP_TEMPLATE_JOINED_BYTES];
};

// The kinds of context.
#define BUSTER_PP_CONTEXT_FILE 0
#define BUSTER_PP_CONTEXT_BUFFER 1

typedef struct PPContext PPContext;
typedef struct PPOpaqueSpan PPOpaqueSpan;
struct PPOpaqueSpan
{
    PPOpaqueSpan* next;
    uint32_t begin;
    uint32_t end;
};

struct PPContext
{
    uint8_t kind;
    // A buffer that ends a capture: an argument being expanded, or a directive's line. Nothing reads past it.
    bool barrier;
    // The leading space and start of line (BUSTER_PP_SPACE, BUSTER_PP_START_OF_LINE) of a macro name read from this context
    // whose expansion was empty: the next token read from it takes them, as clang passes them on (C 6.10.4.4 leaves
    // spacing to the implementation; `a EMPTY+b` stringizes as "a +b").
    uint8_t pending_flags;
    // A buffer: the leading space its expansion left after its last token (an empty argument there), which the next
    // token read after it takes.
    uint8_t trailing_flags;
    // A file: whether a line lexed again from a header name hid some of its directives in a comment (pp_resume_after), so
    // that the file's conditional pairing, which counted them, doesn't hold for it (pp_skip_group).
    bool hidden_directives;
    // A file: this inclusion took the group of the file's include guard (its #ifndef was true). Only such an inclusion
    // tells clang the guard (its multiple-include optimization), and from its end on the guard skips the file.
    bool guard_taken;
    // A source piece of a condition lexed again after a raw header: continue at this byte when it ends. A piece ends
    // after each '(' so a header query can read its operand raw before the ordinary lexer sees comments in its name.
    bool relexed;
    uint32_t relex_position;
    uint32_t relex_file;
    // This inclusion has read the file's trailing unterminated comment, actively or while skipping a group.
    bool comment_read;
    // Confirmed header names containing an invalid raw-looking token that didn't need a full lexical repair.
    PPOpaqueSpan* opaque_spans;
    // A file's comment-gap search advances with source header names; a new private view restarts that token cursor.
    uint32_t header_gap_data;
    uint32_t header_gap_token;
    // The macro to enable again when the context is popped.
    PPMacroId macro;
    uint32_t position;
    uint32_t end;
    // For __LINE__ and __FILE__ in a token a macro made: the end of the outermost expansion it is in.
    uint32_t expansion_end;
    // A file: the file, its location range, the next directive and the conditional stack's depth when it was entered, and
    // the search directory this inclusion of it was found in (for #include_next), or BUSTER_PP_NONE. The directory is the
    // inclusion's, not the file's: one file can be found through two entries of the search list (-iquote d and -I d).
    uint32_t search_index;
    uint32_t file;
    // This inclusion's prepared view; canonical file data retains shared cache and once/guard state.
    uint32_t data;
    uint32_t range;
    uint32_t next_directive;
    uint32_t conditional_base;
    // A buffer: the stack its tokens are in, and the stack's count when it was pushed (the context's tokens start
    // there; popping the context gives them back if they are still on top).
    PPTokens* tokens;
    uint32_t begin;
};

// A conditional group that is open: whether a branch was taken, whether #else was seen, and its #if.
typedef struct PPConditional PPConditional;
struct PPConditional
{
    // A branch of the group was taken; the group's #else was read; the group is being skipped to its next directive
    // (pp_skip_group) rather than read.
    bool taken;
    bool seen_else;
    bool skipping;
    // The opening directive's name, as lexed: clang reports the group unterminated at the first of any line splices
    // before it (pp_lexed_start).
    uint32_t location;
};

// One token, as buffers hold them.
typedef struct PPToken PPToken;
struct PPToken
{
    uint8_t kind;
    uint8_t flags;
    uint32_t value;
    uint32_t location;
    uint32_t origin;
};

// An argument of an invocation: its raw tokens in pp->arguments, and its expanded ones (in pp->arguments too when
// expanding it changes nothing, else in pp->captures).
typedef struct PPArgument PPArgument;
struct PPArgument
{
    uint32_t raw_begin;
    uint32_t raw_end;
    uint32_t expanded_begin;
    uint32_t expanded_end;
    bool expanded_captured;
    // Whether its expanded tokens are known (pp_continue_invocation).
    bool expanded;
};

// A function-like macro's invocation, while its arguments are collected (when they aren't all in one context, a token
// at a time by the main loop: pp_collect_step) and then expanded; or a builtin macro's parenthesized operand while it
// is collected (`builtin`).
typedef struct PPInvocation PPInvocation;
struct PPInvocation
{
    PPMacroId macro;
    uint32_t history;
    uint32_t arguments; // Index of its first PPArgument.
    uint32_t argument_count;
    // Where pp_continue_invocation's walk is: the next argument, or the next item of the body; and the end of the
    // __VA_OPT__ content the walk is in, or BUSTER_PP_NONE.
    uint32_t next;
    uint32_t content_end;
    uint32_t captures_begin; // pp->captures' count before its first expanded argument.
    uint32_t pragma_mark; // pp->pragma_lines when it started (pp_keep_pragma_lines).
    uint32_t arguments_begin; // pp->arguments' count before its first raw argument.
    uint32_t expansion_end;
    uint32_t name_location;
    uint8_t name_flags;
    bool variadic_omitted; // The variadic argument wasn't given at all (no comma): GNU's `, ## __VA_ARGS__` drops the comma.
    // While its tokens are collected: whether its '(' was read (a builtin's is collected too), the parentheses' depth,
    // and pp->capture_count when the collection started (a directive among the arguments can start a capture, during
    // which the collection waits).
    bool collecting;
    bool builtin;
    bool opened;
    uint32_t depth;
    uint32_t capture_mark;
};

// What a capture is for: which continuation takes its tokens when its barrier ends (pp_finish_capture).
#define BUSTER_PP_CAPTURE_ARGUMENT 0 // An invocation's argument, expanded before substitution (pp_finish_argument).
#define BUSTER_PP_CAPTURE_CONDITION 1 // A #if or #elif line (pp_finish_condition): `defined` and __has_include work.
#define BUSTER_PP_CAPTURE_INCLUDE 2 // A computed #include's line (pp_finish_include).
#define BUSTER_PP_CAPTURE_LINE 3 // A #line's line (pp_finish_line).
#define BUSTER_PP_CAPTURE_BUILTIN 4 // A builtin's operand (pp_finish_builtin).
#define BUSTER_PP_CAPTURE_PRAGMA 5 // A pragma's or an #ident's operands that clang reads expanded (pp_finish_pragma).
#define BUSTER_PP_CAPTURE_STREAM 6 // An attribute query's operand, read expanded a token at a time (pp_stream_check).
#define BUSTER_PP_CAPTURE_EMBED 7 // An #embed's line, whose parameters clang reads expanded (pp_finish_embed).

// A stream's stages: before its operand's first token, after a first identifier whose next token clang reads
// unexpanded (__has_c_attribute's and __has_cpp_attribute's, for a `::`), and after that. A _Pragma's stream reads a
// '(', a string literal and a ')', and after another token where the string literal goes, drops the tokens up to a ')'
// or a line's first token, as clang's Handle_Pragma does.
#define BUSTER_PP_STREAM_FIRST 0
#define BUSTER_PP_STREAM_UNEXPANDED 1
#define BUSTER_PP_STREAM_REST 2
#define BUSTER_PP_STREAM_PRAGMA_OPEN 3
#define BUSTER_PP_STREAM_PRAGMA_STRING 4
#define BUSTER_PP_STREAM_PRAGMA_CLOSE 5
#define BUSTER_PP_STREAM_PRAGMA_SKIP 6

typedef struct PPCapture PPCapture;
struct PPCapture
{
    uint8_t kind;
    uint32_t history;
    // The capture's tokens start here in pp->captures.
    uint32_t begin;
    // An argument's: its invocation, and which of its arguments.
    uint32_t invocation;
    uint32_t argument;
    // The other kinds' continuation: pp->arguments' count to give back (the copied line, or the builtin's operand);
    // a directive's: the directive, its kind and its file context; a builtin's: which one, and its name's flags and
    // location.
    uint32_t arguments_begin;
    uint32_t directive;
    uint32_t context;
    uint8_t directive_kind;
    uint8_t builtin;
    uint8_t flags;
    uint32_t location;
    // pp->pragma_lines when the capture started: the pragma lines a _Pragma or a #pragma among its tokens emitted into it
    // since then go to the target below (pp_lift_pragma_lines).
    uint32_t pragma_mark;
    // A stream's: its parentheses' depth, the tokens it has read (an index of pp->captures), whether one of them is
    // inside a pragma line, and its stage (BUSTER_PP_STREAM_FIRST and the others).
    uint32_t depth;
    uint32_t scanned;
    bool in_pragma;
    uint8_t stage;
    // A #line's: the place of the last name read from its line while the capture had no token outside pragma lines,
    // whose expansion gives the line's first token when a macro made it (pp_note_line_anchor), or BUSTER_PP_NONE.
    uint32_t anchor;
    // One over a barrier: the place of the latest name the main loop read from the barrier while the capture was on
    // top, or 0. A _Pragma's line in a directive's goes there (pp_emit_pragma_start), as clang places it at its
    // expansion in the line.
    uint32_t name_location;
    // A directive's line that clang reads expanded only in part: how many tokens (outside pragma lines) it reads
    // expanded before it reads the rest as it is and drops it (pp_check_limit), BUSTER_PP_NONE while a computed
    // #include's '<' waits for its '>', or 0 for the whole line; and how far pp_check_limit has counted them: up to
    // index `counted_to` of pp->captures, `counted` of them, the last in pragma state `counted_state`.
    uint32_t limit;
    uint32_t counted_to;
    uint32_t counted;
    uint8_t counted_state;
};

// The regions #pragma clang assume_nonnull, arc_cf_code_audited and unsafe_buffer_usage open (pp_pragma_region), as
// Preprocessor.region_locations has them.
#define BUSTER_PP_REGION_ASSUME_NONNULL 0
#define BUSTER_PP_REGION_ARC_CF 1
#define BUSTER_PP_REGION_UNSAFE_BUFFER 2

// A feature query's table: the names a __has_* builtin answers nonzero for, sorted, with their values.
typedef struct PPFeature PPFeature;
struct PPFeature
{
    const char* name;
    uint32_t value;
    // The C standard's year from which clang gives the answer (a C11 feature's from 2011), or 0 for every mode.
    uint32_t standard;
};

// Origins are tagged indices: 0 is a file token, otherwise an expansion, a substitution or a generated token.
#define BUSTER_PP_ORIGIN_ARGUMENT (1u << 30)
#define BUSTER_PP_ORIGIN_GENERATED (2u << 30)
#define BUSTER_PP_ORIGIN_INDEX ((1u << 30) - 1)
#define BUSTER_PP_MAX_HISTORY (1u << 26)
#define BUSTER_PP_INITIAL_HISTORY_BYTES 4096u

typedef struct PPExpansion PPExpansion;
struct PPExpansion
{
    IdentId name;
    uint32_t definition;
    uint32_t begin;
    uint32_t end;
    uint32_t parent;
    uint32_t end_origin;
    uint32_t builtin;
};

typedef struct PPSubstitution PPSubstitution;
struct PPSubstitution
{
    uint32_t expansion;
    uint32_t source;
    uint32_t parameter;
};

typedef struct PPSource PPSource;
struct PPSource
{
    uint32_t location;
    uint32_t origin;
};

typedef enum PPGeneratedKind
{
    PP_GENERATED_PASTE,
    PP_GENERATED_STRING,
    PP_GENERATED_PRAGMA,
} PPGeneratedKind;

typedef struct PPGenerated PPGenerated;
struct PPGenerated
{
    uint32_t expansion;
    uint32_t location;
    uint32_t anchor;
    uint32_t first;
    uint32_t count;
    PPGeneratedKind kind;
};

typedef struct PPHistoryRun PPHistoryRun;
struct PPHistoryRun
{
    uint32_t token;
    uint32_t origin;
};
_Static_assert(sizeof(PPHistoryRun) == 8, "an output history run is eight bytes");

// A semantic use of __FLT_EVAL_METHOD__, including expansions consumed by #if or another directive. The output
// boundary and preceding pragma count retain the expansion's place even when a capture has not lifted its pragmas
// into the output yet. No record is made for ordinary -E, or for tokens that were never expanded.
typedef struct PPEvalMethodUse PPEvalMethodUse;
struct PPEvalMethodUse
{
    uint32_t position;
    uint32_t pragmas;
    uint32_t location;
    uint32_t origin;
};

// The most invocations in one invocation's arguments that pp_finish_simple expands in place (Preprocessor.nested).
#define BUSTER_PP_NESTED_PLANS 8

// An invocation in an argument that pp_argument_in_place takes (pp_nested_invocation): its macro and template, its ')'
// in pp->arguments, its arguments' records (into the argument's tokens), how many arguments it was given and whether its
// variadic one was left out, and what its ## and # make.
typedef struct PPNested PPNested;
struct PPNested
{
    PPMacroId macro;
    PPTemplate* template;
    uint32_t close;
    uint32_t count;
    bool omitted;
    PPArgument records[BUSTER_PP_TEMPLATE_TOKENS];
    PPOperators made;
};

typedef struct PPDiagnosticRecord PPDiagnosticRecord;
typedef struct PPDiagnosticSourceNote PPDiagnosticSourceNote;
struct PPDiagnosticSourceNote
{
    uint32_t location;
    uint32_t origin;
    const char* message;
};

struct PPDiagnosticRecord
{
    PPDiagnosticRecord* next;
    Preprocessor* owner;
    uint64_t position;
    // Nonzero orders a completed construct's diagnostics before unsequenced ones at the same publication position.
    // Zero retains ordinary discovery order. Notes belong to the same record and follow its primary.
    uint64_t suborder;
    uint32_t location;
    uint32_t origin;
    uint32_t note_location;
    IdentId note_macro;
    const char* message;
    const char* note_message;
    PPDiagnosticSourceNote* source_notes;
    uint32_t source_note_count;
    uint8_t severity;
    // The diagnostic's highlighted range spans more than one token (Preprocessor.diagnostic_spans).
    bool spans;
    // Whether its macro notes show the argument it came from (Preprocessor.diagnostic_arguments).
    bool arguments;
};

struct Preprocessor
{
    // Frozen at admission; every producer checks the same bounds that its tables were allocated with.
    uint32_t capacity_shift;
    PPOptions options;
    InternTable* table;
    bool vectorized;
    uint32_t refine_flags;
    // The prefixed literals the language mode doesn't have, by kind (pp_split_kinds).
    uint64_t split_kinds;
    // Memory for files' leading-space words and directive tables, macro bodies' spellings and paths. Files' token
    // columns (kinds, starts and values) have an arena each, `columns`. The output (PPTokens) keeps its columns in an
    // arena of tables, and the unit's arrays of records, the stacks' columns and its own file cache are the tables of
    // `arrays`' first (#1128, #652), all of it base pages; an array reserved later has an arena of its own.
    Arena arena;
    Arena columns[3];
    Arena arrays[BUSTER_PP_ARRAYS];
    uint32_t array_count;
    // The output, and the three stacks of buffers. Once released (pp_release_output), the output keeps its count but not
    // its columns until a replay gives them back (pp_output_ensure).
    PPTokens output;
    bool output_released;
    PPTokens arguments;
    // Lazy delimiter links for immutable raw argument slices, indexed through argument_indexed. A write after a
    // stack rewind invalidates its suffix (pp_room); live contexts retain their bounded, unchanged prefix.
    uint32_t* argument_links;
    uint32_t argument_indexed;
    Arena argument_link_arena;
    PPTokens captures;
    PPTokens expansions;
    // Files: the cache they're read through (PPOptions.files, or one of the unit's own, in `arena`), this unit's data of
    // each (its copies of the cache's records, and the files it makes) and of each record (the data's index + 1, or 0),
    // and its paths by name, with their table.
    FileCache* cache;
    bool owns_cache;
    PPFileData* datas;
    uint32_t data_count;
    DependencyFile* dependencies;
    uint32_t dependency_count;
    uint32_t* record_data;
    PPFile* files;
    uint32_t file_count;
    uint32_t* file_slots; // A PPFile index + 1, or 0, by the path's hash.
    uint32_t file_slot_mask;
    // The system calls this unit's path lookups made (none for a path the cache had looked up already).
    FileCacheCounts probes;
    // The search directories: quoted ones first, then -I, then the system ones.
    char** directories;
    uint32_t* directory_lengths;
    uint8_t* directory_system;
    // Each one's directory in the file cache (FileCacheDirectory), once a lookup has needed it.
    FileCacheDirectory** directory_entries;
    uint32_t directory_count;
    uint32_t angled_first; // The first directory <...> searches.
    // With --buster-headers: clang's resource directory, right after buster's (`buster_directory`, its path, as
    // pp_add_directory copied it), which supplies only the names buster's doesn't have (pp_find_include); otherwise
    // BUSTER_PP_NONE.
    uint32_t fallback_directory;
    const char* buster_directory;
    uint32_t buster_directory_length;
    // The location space.
    PPRange* ranges;
    uint32_t range_count;
    uint32_t location_top;
    PPLineDirective* line_directives;
    uint32_t line_directive_count;
    // The runs of the output by where it comes from (PPSystemRun), in the arena; and whether a run started while an
    // invocation or an expansion read before it was open, at another level, or the runs' room ran out: then only the
    // history says where those tokens come from (pp_system_levels_history).
    PPSystemRun* system_runs;
    uint32_t system_run_count;
    uint32_t system_run_capacity;
    bool system_runs_inexact;
    // The starts of the expansions read from files into the output, by token (PPExpansionStart), and the location of the
    // name of the latest one.
    PPExpansionStart* expansion_starts;
    uint32_t expansion_start_count;
    uint32_t expansion_location;
    // With expansion_starts: the start of an expansion that a _Pragma's line in a capture took the place of before the
    // expansion had a token (a #if's _Pragma among its arguments), which starts again after that line when it is lifted
    // into the output (pp_lift_pragma_lines); or 0.
    uint32_t displaced_location;
    // With expansion_starts: the _Pragma operators that printed nothing, by the output token after them, and the place
    // of the expansion that token comes from if a macro made it (the one the _Pragma is in, or one read after it): clang's
    // lexer starts a line at the token after a _Pragma (Lexer::PropagateLineStartLeadingSpaceInfo, as its pragma lexer
    // ends), which its printer moves to when that is another line (MoveToLine). test-preprocessor's fuzz runs print the
    // line break that gives, as clang's text has it.
    PPExpansionStart* silent_pragmas;
    uint32_t silent_pragma_count;
    // The data of the #embed directives in the output, by BUSTER_PP_KIND_EMBED tokens' values.
    PPEmbed* embeds;
    uint32_t embed_count;
    // The latest change of file or of presumed position for which clang's printer starts a line (FileChanged: a file
    // entered or left, a #line or a line marker): the output's count then, and where the printer is after it, at which an
    // #embed's tokens that come before any other output start a line (pp_embed_line).
    uint32_t file_change_token;
    uint32_t file_change_location;
    PPIncludeExit* include_exits;
    PPIncludeExit* last_include_exit;
    PPExpansion* history;
    uint32_t history_count;
    uint32_t history_capacity;
    PPSubstitution* substitutions;
    uint32_t substitution_count;
    uint32_t substitution_capacity;
    PPGenerated* generated;
    uint32_t generated_count;
    uint32_t generated_capacity;
    PPSource* history_sources;
    uint32_t history_source_count;
    uint32_t history_source_capacity;
    PPHistoryRun* history_runs;
    uint32_t history_run_count;
    uint32_t history_run_capacity;
    Arena history_initial;
    // PPOptions.defer_history's state: whether the history is still to record, the quiet flag the options asked for (a
    // deferred run counts its diagnostics without printing them), how pp_run or pp_run_source ran the unit (its path, or
    // its name and bytes), for the replay, and the replay, whose history the fields above point into once recorded.
    bool history_deferred;
    bool history_quiet;
    const char* history_path;
    const char* history_name;
    const uint8_t* history_bytes;
    uint32_t history_length;
    // The run's outcome at its end, which the replay must have too: its result and its diagnostics' counts.
    bool history_result;
    bool history_fatal;
    uint32_t history_errors;
    uint32_t history_warnings;
    // A warning while system-level runs were inexact needs the history replay's canonical visibility/count.
    bool history_warning_filter_deferred;
    Preprocessor* history_replay;
    Arena history_replay_arena;
    // Only set around synchronous expansion/substitution work; never inferred from a spelling location.
    uint32_t history_current;
    uint32_t history_anchor;
    uint32_t history_spelling;
    uint32_t diagnostic_origin;
    // Set by a caller around a diagnostic whose highlighted range spans more than one token, which clang's macro backtrace
    // shows a macro's argument's own "expanded from macro" note for (DiagnosticRenderer::emitMacroExpansions, whose
    // rangesInsideSameMacroArgExpansion leaves it out where every range is one token of an argument). render_spans is
    // that flag while the diagnostic's primary line renders, its collected record's or its own.
    bool diagnostic_spans;
    bool render_spans;
    // The include location of the last diagnostic's file (pp_presumed_include), whose include stack a diagnostic prints
    // only when its own differs.
    uint32_t diagnostic_include_range;
    uint32_t origin_peak[3];
    // The invocations in pp_finish_simple's invocation's arguments that it expands in place, as pp_argument_in_place
    // planned them, in the order pp_expand_argument_in_place takes them (nested_taken).
    PPNested nested[BUSTER_PP_NESTED_PLANS];
    uint32_t nested_count;
    uint32_t nested_taken;
    // A test's fault (pp_test_direct_argument_failures): the next growth of a stack's origins fails, as when its arena
    // can't be reserved.
    bool fail_origin_growth;
    // Scratch spellings, per kind (raw, as source to be read again, or clean, which are final: pp_scratch_clean): the
    // arena, the range of the chunk being filled (BUSTER_PP_NONE before the first) and how much of it is used. A clean
    // spelling is known by its arena (pp_without_splices).
    Arena scratch[2];
    uint32_t scratch_range[2];
    uint32_t scratch_used[2];
    // Macros: by IdentId (macro_of, and is_macro's bits), and the records.
    PPMacroId* macro_of;
    uint64_t* is_macro;
    PPMacro* macros;
    uint32_t macro_count;
    PPItem* items;
    uint32_t item_count;
    IdentId* parameters;
    uint32_t parameter_count;
    // The simple macros' templates (pp_compile_macro), in the runs that write expansions from them: the vectorized
    // kernels' that record no history. Null in the others.
    PPTemplate* templates;
    uint32_t template_count;
    // Changed by every change of a name's macro (pp_set_macro): the macros' table a template's names were found in.
    uint32_t macro_epoch;
    // #pragma push_macro: a name and the macro it had, in order.
    IdentId* pushed_names;
    PPMacroId* pushed_macros;
    uint32_t pushed_count;
    // The #pragma clang (or GCC) diagnostic pushes not yet popped: a pop without one isn't printed (pp_pragma_diagnostic).
    uint32_t diagnostic_depth;
    // The mappings those pragmas give the preprocessor's warning groups (#1934, pp_warning_severity): a PPPragmaMapping
    // each, by PPWarning, and the ones each open push saved, which its pop restores, BUSTER_PP_WARNINGS bytes a
    // depth (room for `warning_capacity` depths, grown in the arena as the pushes nest: any depth).
    uint8_t warning_mappings[32];
    uint8_t* warning_saved;
    uint32_t warning_capacity;
    // The severity the command line gives each warning, by PPWarning (pp_warning_command_read, #2547): what
    // pp_warning_severity reads before a pragma's mapping and the location count, from PPOptions at the warning's first
    // use in the unit, BUSTER_PP_COMMAND_UNREAD before. And a bit by PPWarning, clear for each that no location can give
    // from here on (pp_warning_possible), set again for all by each #pragma clang diagnostic push, pop or mapping. A
    // path that reaches a warning off by default at every directive (-Wreserved-macro-identifier's) tests its bit first.
    uint8_t warning_commands[32];
    uint32_t warnings_possible;
    // Where the open #pragma clang assume_nonnull, arc_cf_code_audited and unsafe_buffer_usage regions begin, or 0.
    uint32_t region_locations[3];
    // #pragma GCC poison's names, by IdentId, and how many there are (pp_check_poison).
    uint64_t* poisoned;
    uint32_t poison_count;
    // The stacks of the main loop.
    PPContext* contexts;
    uint32_t context_count;
    PPConditional* conditionals;
    uint32_t conditional_count;
    PPInvocation* invocations;
    uint32_t invocation_count;
    // Clang's InMacroArgs: set when a macro's arguments start to be collected a token at a time, and cleared when any
    // macro's collection ends, a nested one's in a directive among the arguments too, so that the directives clang
    // rejects among a macro's arguments (pp_embedded_directive_name) are taken after such a nested invocation, as clang
    // takes them (Preprocessor::HandleMacroExpandedIdentifier sets and clears one flag, not a depth).
    bool in_macro_arguments;
    PPArgument* argument_records;
    uint32_t argument_record_count;
    PPCapture* capture_frames;
    uint32_t capture_count;
    // The open streams (BUSTER_PP_CAPTURE_STREAM), which the main loop checks after each step (of a token at a time
    // while one is open: pp_step_limit), and how many pragma lines were emitted (PPCapture.pragma_mark).
    uint32_t stream_count;
    // The open captures with a limit (PPCapture.limit), which the main loop checks after each step (pp_check_limit).
    uint32_t limit_count;
    uint32_t pragma_lines;
    // Built-in state: __COUNTER__, the inclusion depth, the main file, and the spellings "0", "1" and "#".
    uint32_t counter;
    uint32_t zero_location;
    uint32_t one_location;
    uint32_t hash_location;
    uint32_t include_depth;
    // An #include reached the depth limit: a file included before (PPFileData.included) isn't entered again
    // (pp_enter_file).
    bool include_limit_reached;
    uint32_t main_file;
    // -H's lines, from the main file's run on (pp_show_include): not the prelude's -include files, nor a quiet run's.
    bool showing_includes;
    // The predefines' file data (clang's <built-in> buffer), whose __STDC macros clang reports as builtin when they are
    // redefined or undefined (pp_language_defined); BUSTER_PP_NONE before it is read.
    uint32_t predefines_data;
    // __DATE__'s and __TIME__'s text, empty until the first expansion of either without SOURCE_DATE_EPOCH (pp_unit_time);
    // and the unit's time they give, in seconds.
    char date[16];
    char time[16];
    char timestamp[32]; // __TIMESTAMP__'s with SOURCE_DATE_EPOCH (`has_epoch`); else each file's modification time.
    bool has_epoch;
    int64_t unit_seconds;
    // The builtin __FLT_EVAL_METHOD__'s value (predefines_text).
    uint32_t flt_eval_method;
    // Cold semantic expansion uses, in preprocessing order. History replay supplies their retained origins too.
    PPEvalMethodUse* eval_method_uses;
    uint32_t eval_method_use_count;
    uint32_t eval_method_use_capacity;
    // The phases' TSC ticks, the tokens the prepared files have, and what the run did, when PPOptions.timing is set.
    uint64_t ticks[PP_PHASE_COUNT];
    uint64_t input_tokens;
    PPStatistics statistics;
    // Diagnostics.
    uint32_t errors;
    uint32_t warnings;
    bool fatal;
    PPDiagnosticRecord* diagnostic_first;
    PPDiagnosticRecord* diagnostic_last;
    // A consumed file run may have been copied before its raw errors are read. Their insertion points are inside
    // that run, not after it. NONE uses the current output boundary; standalone consumers ignore both fields.
    uint32_t diagnostic_position;
    uint32_t diagnostic_run_first;
    bool diagnostic_collection_failed;
    bool diagnostic_source;
    // Set by a caller around a literal's diagnostic at a character of its spelling (#1447): clang's StringLiteralParser and
    // CharLiteralParser diagnostics, whose macro notes show the macro argument the literal came from ("expanded from
    // macro 'M'" at the parameter), where other diagnostics' forwarding is silent (pp_render_diagnostic).
    bool diagnostic_arguments;
    bool history_failed;
    // What a run leaves open, or closes without its opening: invocations and builtin operands left open where their
    // context ended, conditionals and block comments left open at a file's end, pragma regions (assume_nonnull,
    // arc_cf_code_audited, unsafe_buffer_usage) left open at the main file's end, and #elif, #else or #endif without #if.
    // test-preprocessor's fuzz runs read such a case by clang alone, since in a batch it would reach into the next case.
    uint32_t unterminated;
    // The output's tokens whose keywords have their kinds (pp_resolve_keywords_range): the vectorized kernels resolve a
    // chunk at a time as the output grows, while its tokens are in the caches, and pp_resolve_keywords the rest.
    uint32_t keywords_resolved;
    // The output kind of each identifier id below BUSTER_PP_KEYWORD_TABLE: its keyword's in this language mode, or an
    // identifier's.
    uint8_t keyword_kinds[BUSTER_PP_KEYWORD_TABLE] __attribute__((aligned(64)));
};

// ---------------------------------------------------------------------------------------------------------------------
// Token stacks

static void pp_tokens_release(PPTokens* tokens)
{
    arena_release(&tokens->origin_arena);
    arena_release(&tokens->columns);
    *tokens = (PPTokens){ 0 };
}

// ---------------------------------------------------------------------------------------------------------------------
// Diagnostics

typedef enum PPSeverity
{
    PP_SEVERITY_WARNING,
    PP_SEVERITY_ERROR,
    PP_SEVERITY_FATAL,
    // Rendered only inside an accepted diagnostic group; never passed through the counting gate.
    PP_SEVERITY_NOTE,
    // #warning's diagnostic is explicitly visible in system headers, unlike ordinary preprocessing warnings.
    PP_SEVERITY_WARNING_ALWAYS,
    // Warning policy can promote a group without making it a genuine error for analysis-based warnings.
    PP_SEVERITY_MAPPED_ERROR,
    PP_SEVERITY_MAPPED_FATAL,
    // A genuine delayed error inhibits later analysis after this body's deferred diagnostics are flushed.
    PP_SEVERITY_DEFERRED_ERROR,
} PPSeverity;

typedef enum PPDiagnosticPhase
{
    PP_DIAGNOSTIC_LEXICAL,
    PP_DIAGNOSTIC_DEFERRED,
    PP_DIAGNOSTIC_RETURN,
    // A declarator's completed type precedes declaration-attribute checks at the same parser boundary.
    PP_DIAGNOSTIC_TYPE,
    PP_DIAGNOSTIC_SEMANTIC,
    // A file-scope declarator's queued runtime warnings (clang's DiagRuntimeBehavior outside a function): its shift
    // counts and constant conversions, after its literal conversions at the same declarator end.
    PP_DIAGNOSTIC_RUNTIME,
    PP_DIAGNOSTIC_PHASE_COUNT,
} PPDiagnosticPhase;

typedef struct PPOutputDiagnosticNote PPOutputDiagnosticNote;
struct PPOutputDiagnosticNote
{
    uint32_t token;
    const char* message;
};

// A note at a byte of a token's spelling (pp_diagnose_output_offset_notes).
typedef struct PPOffsetNote PPOffsetNote;
struct PPOffsetNote
{
    uint32_t offset;
    const char* message;
};

typedef struct PPDiagnosticNote PPDiagnosticNote;
struct PPDiagnosticNote
{
    uint32_t location;
    const char* message;
    IdentId macro;
};

static uint32_t pp_find_range(const Preprocessor* pp, uint32_t location);
static uint32_t pp_line_of(Preprocessor* pp, uint32_t range, uint32_t location, uint32_t* column);
static uint32_t pp_presumed(Preprocessor* pp, uint32_t location, const char** name, uint32_t* name_length);

static void pp_diagnostic_lines(Preprocessor* pp, uint32_t range);

// The most notes with places of their own one diagnostic has (pp_diagnose_notes, pp_diagnose_output_offset_notes).
#define BUSTER_PP_PLACED_NOTES 8
BUSTER_PREPROCESSOR_ENTRY __attribute__((unused)) void pp_diagnostic_publish(Preprocessor* pp);

BUSTER_PREPROCESSOR_ENTRY __attribute__((unused)) void pp_diagnostic_summary(const Preprocessor* pp);

// Prints "file:line:column: error: message" to stderr for `location` (0 for none): the presumed file and line, as #line
// gives them, and the column. PPOptions.quiet only counts it.
BUSTER_PREPROCESSOR_ENTRY __attribute__((format(printf, 4, 5))) void pp_diagnose(Preprocessor* pp, PPSeverity severity, uint32_t location, const char* format, ...);

BUSTER_PREPROCESSOR_ENTRY __attribute__((unused)) bool pp_history_ensure(Preprocessor* pp);
BUSTER_PREPROCESSOR_ENTRY __attribute__((unused)) void pp_release_file_columns(Preprocessor* pp, bool poison);
BUSTER_PREPROCESSOR_ENTRY __attribute__((unused)) void pp_release_output(Preprocessor* pp, bool poison);
BUSTER_PREPROCESSOR_ENTRY __attribute__((unused)) bool pp_output_ensure(Preprocessor* pp);
BUSTER_PREPROCESSOR_ENTRY __attribute__((unused)) bool pp_location_in_main_file(const Preprocessor* pp, uint32_t location);
BUSTER_PREPROCESSOR_ENTRY __attribute__((unused)) void pp_diagnose_empty_unit(Preprocessor* pp);
BUSTER_PREPROCESSOR_ENTRY __attribute__((unused)) void pp_diagnose_eval_method_use(Preprocessor* pp, uint32_t use, uint32_t position, uint32_t pragma_source);

#include "c_preprocessor_history.c"


// ---------------------------------------------------------------------------------------------------------------------
// The location space

// The range that holds `location`: ranges are in the order of their bases.
static uint32_t pp_find_range(const Preprocessor* pp, uint32_t location)
{
    uint32_t low = 0;
    uint32_t high = pp->range_count;
    while (high - low > 1)
    {
        const uint32_t middle = low + (high - low) / 2;
        if (pp->ranges[middle].base <= location)
        {
            low = middle;
        }
        else
        {
            high = middle;
        }
    }
    return pp->range_count != 0 && location >= pp->ranges[low].base && location - pp->ranges[low].base < pp->ranges[low].length ? low : BUSTER_PP_NONE;
}

// Whether a source location is written in the main file, where Clang gives a file-scope variable's unused warning: it
// leaves a header's file-scope variables alone, as every unit that includes the header shares them.
BUSTER_PREPROCESSOR_ENTRY __attribute__((unused)) bool pp_location_in_main_file(const Preprocessor* pp, uint32_t location);
BUSTER_PREPROCESSOR_ENTRY __attribute__((unused)) void pp_diagnose_empty_unit(Preprocessor* pp);

// The bytes of [location, end) in the location space (in one range).
static const uint8_t* pp_location_bytes(const Preprocessor* pp, uint32_t location)
{
    const uint32_t range = pp_find_range(pp, location);
    return range != BUSTER_PP_NONE ? pp->ranges[range].bytes + (location - pp->ranges[range].base) : 0;
}

// Is a token of this kind a string literal?
BUSTER_INLINE bool pp_is_string_literal(uint8_t kind)
{
    return kind >= C_TOKEN_STRING_LITERAL && kind <= C_TOKEN_UTF32_STRING_LITERAL;
}

// Do `length` bytes start with a raw string literal's prefix and opening quote, as written (R", LR", uR", UR" or u8R",
// with no line splice or trigraph in them)? A token that does is one the lexer read as a raw string literal: one, or an
// unknown token if its delimiter is invalid or it is unterminated.
static bool pp_raw_string_start(const uint8_t* bytes, uint32_t length)
{
    const bool u8 = length >= 2 && bytes[0] == 'u' && bytes[1] == '8';
    const uint32_t prefix = u8 ? 2 : length >= 1 && (bytes[0] == 'L' || bytes[0] == 'u' || bytes[0] == 'U') ? 1 : 0;
    return length >= prefix + 2 && bytes[prefix] == 'R' && bytes[prefix + 1] == '"';
}

// `length` bytes of source cleaned of their line splices (and trigraphs, with `trigraphs`) into `buffer`, which needs
// room for `length` bytes, through the scalar lexer's characters; with `string`, a raw string literal's characters
// after its opening quote as written. Returns the length.
static uint32_t pp_clean_splices(const uint8_t* bytes, uint32_t length, bool trigraphs, bool string, uint8_t* buffer)
{
    const ScalarLexer lexer = {
        .source = bytes,
        .length = length,
        .trigraphs = trigraphs,
        .cplusplus = false,
    };
    uint64_t p = 0;
    uint32_t result = 0;
    bool opened = false;
    while (p < length)
    {
        uint64_t size;
        uint64_t char_start;
        const int32_t c = get_char(&lexer, p, &size, &char_start);
        if (c != BUSTER_LEX_END_OF_FILE)
        {
            buffer[result] = (uint8_t)c;
            result += 1;
        }
        p += size != 0 ? size : 1;
        // A string literal's first quote is its opening one, right after its R if it is a raw string literal.
        if (string && !opened && c == '"')
        {
            opened = true;
            const bool raw = result >= 2 && buffer[result - 2] == 'R';
            const uint64_t rest = raw ? length - p : 0;
            memcpy(buffer + result, bytes + p, rest);
            result += (uint32_t)rest;
            p += rest;
        }
    }
    return result;
}

// Whether `length` bytes may hold a line splice or, with `trigraphs`, a trigraph, so that pp_remove_splices_by cleans
// them. A splice is a backslash, whitespace and a line break (get_char), so a backslash may start one only when a byte
// up to ' ' follows it or it ends the bytes; a backslash before anything else, as an escape sequence's, may not, and
// cleaning such bytes gives back the same bytes. The scalar twin of pp_splice_possible_vector, and its oracle.
static bool pp_splice_possible_scalar(const uint8_t* bytes, uint32_t length, bool trigraphs)
{
    bool result = false;
    for (uint32_t i = 0; !result && i < length; i += 1)
    {
        result = (bytes[i] == '\\' && (i + 1 == length || bytes[i + 1] <= ' ')) || (trigraphs && bytes[i] == '?');
    }
    return result;
}

// pp_splice_possible_scalar 64 bytes at a time, in masked loads that read no byte past `length`: a backslash's lane
// shifted up one against the blanks (every byte up to ' '; the zeros past the bytes count, so a final backslash does),
// a block's backslash in its last lane carried to the next block's first, or past the last block to its end.
static bool pp_splice_possible_vector(const uint8_t* bytes, uint32_t length, bool trigraphs)
{
#if !BUSTER_HOST_VECTOR
    return pp_splice_possible_scalar(bytes, length, trigraphs);
#else
    const __mmask64 questions = trigraphs ? ~0ull : 0;
    uint64_t carry = 0;
    uint64_t possible = 0;
    for (uint32_t b = 0; possible == 0 && b < length; b += 64)
    {
        const __mmask64 lanes = _bzhi_u64(~0ull, length - b < 64 ? length - b : 64);
        const __m512i block = _mm512_maskz_loadu_epi8(lanes, bytes + b);
        const uint64_t backslashes = _mm512_mask_cmpeq_epi8_mask(lanes, block, _mm512_set1_epi8('\\'));
        const uint64_t blanks = _mm512_cmple_epu8_mask(block, _mm512_set1_epi8(' '));
        possible = (((backslashes << 1) | carry) & blanks) | _mm512_mask_cmpeq_epi8_mask(questions & lanes, block, _mm512_set1_epi8('?'));
        carry = backslashes >> 63;
    }
    return possible != 0 || carry != 0;
#endif
}

// `length` bytes of source of a token of kind `kind`, without the line splices in them (and trigraphs, with
// C_TOKENIZER_TRIGRAPHS in `refine_flags`): the bytes themselves if they have none, else a copy in `buffer`, which
// needs room for `length` bytes. Bytes have none to remove when no backslash in them may start a splice nor, with
// trigraphs, a '?' a trigraph (pp_splice_possible_vector, or its scalar twin with `scalar`). A raw string literal's
// characters undo translation phases 1 and 2, as clang's getSpelling keeps them: a raw string literal keeps what
// follows its opening quote as written, and so does an unknown token the lexer read as one (its delimiter invalid, or
// unterminated) whose prefix and quote are as written, which clang reads byte by byte (a token that needs cleaning
// anywhere, as one with a line splice in its prefix, clang cleans whole). Returns the length.
static uint32_t pp_remove_splices_by(const uint8_t* bytes, uint32_t length, uint32_t refine_flags, uint8_t kind, bool scalar, uint8_t* buffer, const uint8_t** spelling)
{
    const bool trigraphs = (refine_flags & C_TOKENIZER_TRIGRAPHS) != 0;
    const bool string = pp_is_string_literal(kind);
    bool cleaned = BUSTER_SCALAR_PATH(scalar) ? pp_splice_possible_scalar(bytes, length, trigraphs) : pp_splice_possible_vector(bytes, length, trigraphs);
    cleaned = cleaned && !(kind == C_TOKEN_UNKNOWN && pp_raw_string_start(bytes, length));
    uint32_t result = length;
    *spelling = bytes;
    if (cleaned)
    {
        result = pp_clean_splices(bytes, length, trigraphs, string, buffer);
        *spelling = buffer;
    }
    return result;
}

// pp_remove_splices_by with the vectorized check.
static uint32_t pp_remove_splices(const uint8_t* bytes, uint32_t length, uint32_t refine_flags, uint8_t kind, uint8_t* buffer, const uint8_t** spelling)
{
    return pp_remove_splices_by(bytes, length, refine_flags, kind, false, buffer, spelling);
}

// `length` bytes of a token of kind `kind` without the line splices in them (pp_remove_splices), unless they are a
// clean scratch spelling (in its arena: pp_scratch_clean), which is final and is the bytes themselves.
static uint32_t pp_without_splices(const Preprocessor* pp, uint8_t kind, const uint8_t* bytes, uint32_t length, uint8_t* buffer, const uint8_t** spelling)
{
    const Arena* clean = &pp->scratch[1];
    const bool final = bytes >= clean->base && bytes < clean->base + clean->capacity;
    uint32_t result = length;
    *spelling = bytes;
    if (!final)
    {
        result = pp_remove_splices_by(bytes, length, pp->refine_flags, kind, pp->options.scalar, buffer, spelling);
    }
    return result;
}

// The spelling of an unknown token at `location` of `length` bytes that ends in a line splice, an unterminated literal's
// (no other token can): clang's cleaning reads the character after the splice, past the token, which is the newline
// that ended the literal (Lexer::getSpellingSlow), so its spelling ends with that newline; or, for a literal the file's
// end ended, the NUL that ends clang's buffer (#109's seed 3110). `spelled` bytes of it are in `buffer`, without the
// splices (pp_without_splices), which has room for `length`. Returns the length.
static uint32_t pp_trailing_splice(const Preprocessor* pp, uint32_t location, uint32_t length, uint8_t* buffer, uint32_t spelled)
{
    uint32_t result = spelled;
    const uint32_t range = pp_find_range(pp, location);
    const PPRange* record = range != BUSTER_PP_NONE ? &pp->ranges[range] : 0;
    if (record != 0 && record->file != BUSTER_PP_NONE && length >= 2 && location - record->base + length < record->length)
    {
        // A range has a location past its file's bytes (pp_add_range), which holds no byte to read.
        const bool end = location - record->base + length == record->length - 1;
        const uint8_t* bytes = record->bytes + (location - record->base);
        uint32_t p = length;
        const bool newline = bytes[p - 1] == '\n' || bytes[p - 1] == '\r';
        p -= newline ? 1 : 0;
        p -= newline && bytes[p] == '\n' && p >= 1 && bytes[p - 1] == '\r' ? 1 : 0;
        while (newline && p >= 1 && (bytes[p - 1] == ' ' || bytes[p - 1] == '\t'))
        {
            p -= 1;
        }
        const bool spliced = newline && p >= 1 && (bytes[p - 1] == '\\' || (pp->options.trigraphs && p >= 3 && bytes[p - 1] == '/' && bytes[p - 2] == '?' && bytes[p - 3] == '?'));
        const uint8_t next = end ? 0 : bytes[length];
        if (spliced && (end || next == '\n' || next == '\r') && spelled < length)
        {
            buffer[spelled] = next;
            result = spelled + 1;
        }
    }
    return result;
}

// A token's spelling: an identifier's (or a keyword's) interned bytes, an integer's digits, or else its bytes in the
// location space without the line splices in them (or trigraphs, with trigraphs on), an unterminated literal's as clang
// spells it (pp_trailing_splice). `buffer` holds a spelling that needs writing; it needs room for the token's bytes, or
// 24 for an integer. Returns the length.
static uint32_t pp_spelling(Preprocessor* pp, uint8_t kind, uint32_t value, uint32_t location, uint8_t* buffer, const uint8_t** spelling)
{
    uint32_t result;
    if (kind == C_TOKEN_RAW_IDENTIFIER || (kind >= BUSTER_PP_KIND_KEYWORD && kind < BUSTER_PP_KIND_KEYWORD + C_NAME_KEYWORD_COUNT))
    {
        *spelling = intern_bytes(pp->table, value);
        result = intern_length(pp->table, value);
    }
    else if (kind == BUSTER_PP_KIND_INTEGER)
    {
        result = (uint32_t)snprintf((char*)buffer, 24, "%u", value);
        *spelling = buffer;
    }
    else if (kind == BUSTER_PP_KIND_EMBED)
    {
        // An #embed's data is no one token's spelling (the printers write its text: pp_print_embed). Only a builtin's
        // operand can have one, which then isn't what the builtin takes.
        result = 0;
        *spelling = buffer;
    }
    else
    {
        result = pp_without_splices(pp, kind, pp_location_bytes(pp, location), value - location, buffer, spelling);
        result = kind == C_TOKEN_UNKNOWN && *spelling == buffer ? pp_trailing_splice(pp, location, value - location, buffer, result) : result;
    }
    return result;
}

// Recover a refined identifier's end, respecting the '$' boundary of pp_split_dollars where '$' is punctuation.
static uint32_t pp_identifier_end(const Preprocessor* pp, const uint8_t* bytes, uint32_t length, uint32_t start)
{
    uint32_t result = refine_identifier_end(bytes, length, start, pp->refine_flags);
    if (pp_dollar_punctuation(&pp->options))
    {
        const uint8_t* dollar = memchr(bytes + start, '$', result - start);
        result = dollar != 0 ? (uint32_t)(dollar - bytes) : result;
    }
    return result;
}

// The spelling an identifier at `location` was written with, without its line splices but with its UCNs as written:
// what stringizing it gives, as in clang ("caf\u00e9" for caf\u00e9, whose interned spelling is café). `buffer` needs
// room for the identifier's bytes. Returns the length, or 0 if the location is in no range.
static uint32_t pp_identifier_as_written(Preprocessor* pp, uint32_t location, uint8_t* buffer, const uint8_t** spelling)
{
    uint32_t result = 0;
    const uint32_t range = pp_find_range(pp, location);
    if (range != BUSTER_PP_NONE)
    {
        const PPRange* bytes_range = &pp->ranges[range];
        const uint32_t start = location - bytes_range->base;
        const uint32_t end = pp_identifier_end(pp, bytes_range->bytes, bytes_range->length, start);
        result = pp_without_splices(pp, C_TOKEN_RAW_IDENTIFIER, bytes_range->bytes + start, end - start, buffer, spelling);
    }
    return result;
}

// The printers' place in a pragma line, which pp_pragma_state keeps from token to token: outside one, after its '#', or
// after its name (`pragma` or `ident`). -E spells an identifier after a pragma line's name as it was written
// (pp_written_spelling: its UCNs as written, without its splices), as clang's printer spells a pragma's tokens
// (Preprocessor::getSpelling), where it spells any other identifier as interned (a UCN as UTF-8).
#define BUSTER_PP_PRAGMA_OUTSIDE 0
#define BUSTER_PP_PRAGMA_TOKENS 1
#define BUSTER_PP_PRAGMA_NAME 2


// ---------------------------------------------------------------------------------------------------------------------
// Files

// The directive kind of a name.
static uint8_t pp_directive_kind_of(IdentId name)
{
    uint8_t result = PP_DIRECTIVE_UNKNOWN;
    switch (name)
    {
        case C_NAME_DEFINE: result = PP_DIRECTIVE_DEFINE; break;
        case C_NAME_UNDEF: result = PP_DIRECTIVE_UNDEF; break;
        case C_NAME_INCLUDE: result = PP_DIRECTIVE_INCLUDE; break;
        case C_NAME_INCLUDE_NEXT: result = PP_DIRECTIVE_INCLUDE_NEXT; break;
        case C_NAME_IMPORT: result = PP_DIRECTIVE_IMPORT; break;
        case C_NAME_IF: result = PP_DIRECTIVE_IF; break;
        case C_NAME_IFDEF: result = PP_DIRECTIVE_IFDEF; break;
        case C_NAME_IFNDEF: result = PP_DIRECTIVE_IFNDEF; break;
        case C_NAME_ELIF: result = PP_DIRECTIVE_ELIF; break;
        case C_NAME_ELIFDEF: result = PP_DIRECTIVE_ELIFDEF; break;
        case C_NAME_ELIFNDEF: result = PP_DIRECTIVE_ELIFNDEF; break;
        case C_NAME_ELSE: result = PP_DIRECTIVE_ELSE; break;
        case C_NAME_ENDIF: result = PP_DIRECTIVE_ENDIF; break;
        case C_NAME_LINE: result = PP_DIRECTIVE_LINE; break;
        case C_NAME_ERROR: result = PP_DIRECTIVE_ERROR; break;
        case C_NAME_WARNING: result = PP_DIRECTIVE_WARNING; break;
        case C_NAME_PRAGMA: result = PP_DIRECTIVE_PRAGMA; break;
        case C_NAME_IDENT: result = PP_DIRECTIVE_IDENT; break;
        case C_NAME_SCCS: result = PP_DIRECTIVE_IDENT; break;
        case C_NAME_ASSERT: result = PP_DIRECTIVE_ASSERT; break;
        case C_NAME_UNASSERT: result = PP_DIRECTIVE_ASSERT; break;
        case C_NAME_EMBED: result = PP_DIRECTIVE_EMBED; break;
    }
    return result;
}

// The directives of a file, 64 kinds at a time: each '#' that starts a line (one compare of the kinds with the hash
// kind and the start-of-line bit), and the start-of-line bits, from which pp_finish_directives finds where each
// directive's line ends. Writes the '#' tokens to `hashes` (if it isn't null) and returns how many there are.
static uint32_t pp_scan_directives_scalar(const uint8_t* kinds, uint32_t count, uint32_t* hashes, uint64_t* lines);

static uint32_t pp_scan_directives_vector(const uint8_t* kinds, uint32_t count, uint32_t* hashes, uint64_t* lines)
{
#if !BUSTER_HOST_VECTOR
    return pp_scan_directives_scalar(kinds, count, hashes, lines);
#else
    uint32_t result = 0;
    for (uint32_t i = 0; i < count; i += 64)
    {
        const __mmask64 valid = count - i >= 64 ? ~0ull : _bzhi_u64(~0ull, count - i);
        const __m512i block = _mm512_maskz_loadu_epi8(valid, kinds + i);
        lines[i / 64] = _mm512_movepi8_mask(block) & valid;
        uint64_t directives = _mm512_mask_cmpeq_epi8_mask(valid, block, _mm512_set1_epi8((char)(C_TOKEN_HASH | BUSTER_C_TOKEN_START_OF_LINE)));
        if (hashes != 0)
        {
            for (; directives != 0; directives &= directives - 1)
            {
                hashes[result] = i + (uint32_t)__builtin_ctzll(directives);
                result += 1;
            }
        }
        else
        {
            result += (uint32_t)__builtin_popcountll(directives);
        }
    }
    return result;
#endif
}

// The same, a token at a time.
static uint32_t pp_scan_directives_scalar(const uint8_t* kinds, uint32_t count, uint32_t* hashes, uint64_t* lines)
{
    uint32_t result = 0;
    for (uint32_t i = 0; i < count; i += 1)
    {
        if (i % 64 == 0)
        {
            lines[i / 64] = 0;
        }
        lines[i / 64] |= (uint64_t)((kinds[i] & BUSTER_C_TOKEN_START_OF_LINE) != 0) << (i % 64);
        if (kinds[i] == (C_TOKEN_HASH | BUSTER_C_TOKEN_START_OF_LINE))
        {
            if (hashes != 0)
            {
                hashes[result] = i;
            }
            result += 1;
        }
    }
    return result;
}

// The first start of a line after token `after`, or `count`.
static uint32_t pp_next_line(const uint64_t* lines, uint32_t count, uint32_t after)
{
    uint32_t result = count;
    uint32_t i = after + 1;
    bool found = false;
    while (!found && i < count)
    {
        const uint64_t bits = lines[i / 64] & ~_bzhi_u64(~0ull, i % 64);
        if (bits != 0)
        {
            result = (i & ~63u) + (uint32_t)__builtin_ctzll(bits);
            found = true;
        }
        i = (i & ~63u) + 64;
    }
    return result < count ? result : count;
}

// Pairs a file's conditional directives with an explicit stack of the open groups' last directives: each points to the
// next directive of its group (#elif, #else or #endif). An #elif, #else or #endif without an open group is left
// unpaired (the directive reports it). The scalar kernels' pairing, and the oracle of pp_pair_conditionals_vector.
static void pp_pair_conditionals_scalar(PPFileData* data, uint32_t* open)
{
    uint32_t open_count = 0;
    for (uint32_t d = 0; d < data->directive_count; d += 1)
    {
        switch (data->directive_kind[d])
        {
            case PP_DIRECTIVE_IF:
            case PP_DIRECTIVE_IFDEF:
            case PP_DIRECTIVE_IFNDEF:
                open[open_count] = d;
                open_count += 1;
                break;
            case PP_DIRECTIVE_ELIF:
            case PP_DIRECTIVE_ELIFDEF:
            case PP_DIRECTIVE_ELIFNDEF:
            case PP_DIRECTIVE_ELSE:
                if (open_count != 0)
                {
                    data->directive_next[open[open_count - 1]] = d;
                    open[open_count - 1] = d;
                }
                break;
            case PP_DIRECTIVE_ENDIF:
                if (open_count != 0)
                {
                    data->directive_next[open[open_count - 1]] = d;
                    open_count -= 1;
                }
                break;
        }
    }
}

// The word indices 0 to 31, for pp_prefix_sum_words' permutes and pp_pair_conditionals_vector's lanes.
static const uint16_t pp_word_indices[32] __attribute__((aligned(64))) = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31 };

// The inclusive prefix sums of 32 16-bit words, wrapping: five steps of a permute that moves the words up by 1, 2, 4, 8
// and 16 places, zeroing the places it shifts in (vpermw: one uop on Zen 5, latency 4), and an add.
BUSTER_INLINE __m512i pp_prefix_sum_words(__m512i x)
{
    const __m512i indices = _mm512_load_si512(pp_word_indices);
    for (uint32_t shift = 1; shift < 32; shift *= 2)
    {
        const __m512i from = _mm512_sub_epi16(indices, _mm512_set1_epi16((short)shift));
        x = _mm512_add_epi16(x, _mm512_maskz_permutexvar_epi16(~0u << shift, from, x));
    }
    return x;
}

// The most groups pp_pair_conditionals_vector's 16-bit levels hold open.
#define BUSTER_PP_PAIR_MAX_DEPTH 30000

// Pairs a file's conditional directives without a stack walk, as pp_pair_conditionals_scalar does. A conditional's
// level is the number of groups open before it, less one for a continuation (#elif, #elifdef, #elifndef, #else,
// #endif), so that a group's directives share their level, and the directive after d in d's group is the nearest
// continuation after d at d's level or lower (nearest smaller-or-equal values). The conditionals are compressed out of
// the directives 16 at a time (vpcompressd); then, 32 at a time in 16-bit lanes, their levels are an exclusive prefix
// sum of +1 at an opener and -1 at an #endif (pp_prefix_sum_words), and each lane finds its answer by pointer jumping
// (vpermw: one uop on Zen 5) from the next lane: a lane whose candidate is an opener or is deeper takes over the
// candidate's own pointer, which in a balanced file never passes its answer, so that about five rounds settle 32 lanes.
// Lanes still unanswered at a block's end are its open groups (as many as the nesting depth); each later block answers
// them with one compare of its levels against each one's. Returns false, having paired nothing, for a file with a
// continuation that has no open group (an #endif or #else too many), which the stack then pairs. Sets *notable if a
// group has a directive after its #else or no #endif, for pp_note_directives.
static bool pp_pair_conditionals_vector(PPFileData* data, uint32_t* scratch_indices, bool* notable)
{
    const uint32_t count = data->directive_count;
    Scratch scratch = scratch_begin(0);
    // The conditionals: their directives, kinds and levels, with room for whole-vector stores past the last.
    uint32_t* conditionals = scratch_indices;
    uint8_t* kinds = arena_push(scratch.arena, (uint64_t)count + 64, 64);
    int16_t* levels = arena_push_array(scratch.arena, (uint64_t)count + 32, sizeof(int16_t), 64);
    uint32_t* answers = arena_push_array(scratch.arena, (uint64_t)count + 32, sizeof(uint32_t), 64);
    uint32_t* carried = arena_push_array(scratch.arena, (uint64_t)count + 32, sizeof(uint32_t), 64);
    bool result = kinds != 0 && levels != 0 && answers != 0 && carried != 0;
    const __m512i lanes = _mm512_cvtepu16_epi32(_mm256_load_si256((const __m256i*)pp_word_indices));
    uint32_t n = 0;
    for (uint32_t d = 0; result && d < count; d += 16)
    {
        const __mmask16 valid = (__mmask16)_bzhi_u32(~0u, count - d < 16 ? count - d : 16);
        const __m512i kind = _mm512_cvtepu8_epi32(_mm_maskz_loadu_epi8(valid, data->directive_kind + d));
        const __mmask16 conditional = _mm512_mask_cmple_epu32_mask(valid, _mm512_sub_epi32(kind, _mm512_set1_epi32(PP_DIRECTIVE_IF)),
                                                                    _mm512_set1_epi32(PP_DIRECTIVE_ENDIF - PP_DIRECTIVE_IF));
        _mm512_storeu_si512(conditionals + n, _mm512_maskz_compress_epi32(conditional, _mm512_add_epi32(lanes, _mm512_set1_epi32((int)d))));
        _mm_storeu_si128((__m128i*)(kinds + n), _mm512_cvtepi32_epi8(_mm512_maskz_compress_epi32(conditional, kind)));
        n += (uint32_t)__builtin_popcount(conditional);
    }
    // Levels, 32 conditionals at a time; a continuation without an open group fails the file.
    int32_t depth = 0;
    for (uint32_t b = 0; result && b < n; b += 32)
    {
        const __mmask32 valid = _bzhi_u32(~0u, n - b < 32 ? n - b : 32);
        const __m512i kind = _mm512_cvtepu8_epi16(_mm256_maskz_loadu_epi8(valid, kinds + b));
        const __mmask32 opens = _mm512_mask_cmple_epu16_mask(valid, _mm512_sub_epi16(kind, _mm512_set1_epi16(PP_DIRECTIVE_IF)),
                                                              _mm512_set1_epi16(PP_DIRECTIVE_IFNDEF - PP_DIRECTIVE_IF));
        const __mmask32 endifs = _mm512_mask_cmpeq_epi16_mask(valid, kind, _mm512_set1_epi16(PP_DIRECTIVE_ENDIF));
        const __mmask32 continues = valid & ~opens;
        const __m512i steps = _mm512_mask_mov_epi16(_mm512_maskz_mov_epi16(opens, _mm512_set1_epi16(1)), endifs, _mm512_set1_epi16(-1));
        const __m512i before = _mm512_add_epi16(_mm512_sub_epi16(pp_prefix_sum_words(steps), steps), _mm512_set1_epi16((short)depth));
        result = _mm512_mask_cmple_epi16_mask(continues, before, _mm512_setzero_si512()) == 0;
        _mm512_storeu_si512(levels + b, _mm512_mask_sub_epi16(before, continues, before, _mm512_set1_epi16(1)));
        depth += __builtin_popcount(opens) - __builtin_popcount(endifs);
        result = result && depth <= BUSTER_PP_PAIR_MAX_DEPTH;
    }
    // Answers (indices of conditionals), 32 conditionals at a time.
    uint32_t carried_count = 0;
    const __m512i words = _mm512_load_si512(pp_word_indices);
    for (uint32_t b = 0; result && b < n; b += 32)
    {
        const uint32_t available = n - b < 32 ? n - b : 32;
        const __mmask32 valid = _bzhi_u32(~0u, available);
        const __m512i kind = _mm512_cvtepu8_epi16(_mm256_maskz_loadu_epi8(valid, kinds + b));
        const __mmask32 opens = _mm512_mask_cmple_epu16_mask(valid, _mm512_sub_epi16(kind, _mm512_set1_epi16(PP_DIRECTIVE_IF)),
                                                              _mm512_set1_epi16(PP_DIRECTIVE_IFNDEF - PP_DIRECTIVE_IF));
        const __mmask32 continues = valid & ~opens;
        const __mmask32 endifs = _mm512_mask_cmpeq_epi16_mask(valid, kind, _mm512_set1_epi16(PP_DIRECTIVE_ENDIF));
        const __m512i level = _mm512_maskz_loadu_epi16(valid, levels + b);
        // The open groups of earlier blocks: each one's answer is this block's first continuation at its level or lower.
        uint32_t kept = 0;
        for (uint32_t c = 0; c < carried_count; c += 1)
        {
            const uint32_t entry = carried[c];
            const __mmask32 found = _mm512_mask_cmple_epi16_mask(continues, level, _mm512_set1_epi16(levels[entry]));
            answers[entry] = found != 0 ? b + (uint32_t)__builtin_ctz(found) : BUSTER_PP_NONE;
            carried[kept] = entry;
            kept += found == 0;
        }
        carried_count = kept;
        // Pointer jumping from each lane's next lane; 32 is past the block.
        const __m512i candidate = _mm512_maskz_mov_epi16(continues, _mm512_set1_epi16(1));
        __m512i next = _mm512_add_epi16(words, _mm512_set1_epi16(1));
        __mmask32 jump = 1;
        for (uint32_t round = 0; jump != 0 && round < 32; round += 1)
        {
            const __mmask32 inside = _mm512_cmplt_epu16_mask(next, _mm512_set1_epi16((short)available));
            const __m512i next_level = _mm512_permutexvar_epi16(next, level);
            const __m512i next_candidate = _mm512_permutexvar_epi16(next, candidate);
            const __mmask32 good = inside & _mm512_test_epi16_mask(next_candidate, next_candidate) & _mm512_cmple_epi16_mask(next_level, level);
            jump = valid & inside & ~good;
            next = _mm512_mask_permutexvar_epi16(next, jump, next, next);
        }
        // Answered lanes; the others (not an #endif's, whose answer nothing reads) are open groups for the next blocks.
        const __mmask32 answered = valid & _mm512_cmplt_epu16_mask(next, _mm512_set1_epi16((short)available));
        const __m512i low = _mm512_add_epi32(_mm512_cvtepu16_epi32(_mm512_castsi512_si256(next)), _mm512_set1_epi32((int)b));
        const __m512i high = _mm512_add_epi32(_mm512_cvtepu16_epi32(_mm512_extracti64x4_epi64(next, 1)), _mm512_set1_epi32((int)b));
        const __m512i none = _mm512_set1_epi32((int)BUSTER_PP_NONE);
        _mm512_storeu_si512(answers + b, _mm512_mask_mov_epi32(none, (__mmask16)answered, low));
        _mm512_storeu_si512(answers + b + 16, _mm512_mask_mov_epi32(none, (__mmask16)(answered >> 16), high));
        const __mmask32 open = valid & ~answered & ~endifs;
        const __m512i entries = _mm512_add_epi32(lanes, _mm512_set1_epi32((int)b));
        _mm512_storeu_si512(carried + carried_count, _mm512_maskz_compress_epi32((__mmask16)open, entries));
        carried_count += (uint32_t)__builtin_popcount(open & 0xFFFF);
        _mm512_storeu_si512(carried + carried_count, _mm512_maskz_compress_epi32((__mmask16)(open >> 16), _mm512_add_epi32(entries, _mm512_set1_epi32(16))));
        carried_count += (uint32_t)__builtin_popcount(open >> 16);
    }
    // Each opener's, #elif's and #else's answer, as directives; an unanswered one is in a group without #endif.
    bool found = false;
    for (uint32_t i = 0; result && i < n; i += 1)
    {
        if (kinds[i] != PP_DIRECTIVE_ENDIF)
        {
            const uint32_t answer = answers[i];
            data->directive_next[conditionals[i]] = answer != BUSTER_PP_NONE ? conditionals[answer] : BUSTER_PP_NONE;
            found |= answer == BUSTER_PP_NONE || (kinds[i] == PP_DIRECTIVE_ELSE && kinds[answer] != PP_DIRECTIVE_ENDIF);
        }
    }
    *notable = found;
    scratch_end(scratch);
    return result;
}

// The file's notes (BUSTER_PP_NOTE_*), with an explicit stack of the open groups and whether each has had its #else,
// which pairs the directives as pp_pair_conditionals_scalar does: the scalar kernels' notes, and the vectorized
// kernels' for a file whose pairing found a group with a directive after its #else or without its #endif, or that has
// an #elifdef or #elifndef. Returns false if memory runs out.
static bool pp_note_directives(Arena* arena, PPFileData* data)
{
    const uint32_t count = data->directive_count;
    Scratch scratch = scratch_begin(0);
    uint8_t* notes = arena_push(scratch.arena, (uint64_t)count + 1, 1);
    uint32_t* open = arena_push_array(scratch.arena, (uint64_t)count + 1, sizeof(uint32_t), 4);
    bool* seen_else = arena_push(scratch.arena, (uint64_t)count + 1, 1);
    bool result = notes != 0 && open != 0 && seen_else != 0;
    uint32_t open_count = 0;
    for (uint32_t d = 0; result && d < count; d += 1)
    {
        const uint8_t kind = data->directive_kind[d];
        uint8_t note = kind == PP_DIRECTIVE_ELIFDEF || kind == PP_DIRECTIVE_ELIFNDEF ? BUSTER_PP_NOTE_ELIFDEF : 0;
        switch (kind)
        {
            case PP_DIRECTIVE_IF:
            case PP_DIRECTIVE_IFDEF:
            case PP_DIRECTIVE_IFNDEF:
                open[open_count] = d;
                seen_else[open_count] = false;
                open_count += 1;
                break;
            case PP_DIRECTIVE_ELIF:
            case PP_DIRECTIVE_ELIFDEF:
            case PP_DIRECTIVE_ELIFNDEF:
            case PP_DIRECTIVE_ELSE:
                if (open_count != 0)
                {
                    note |= seen_else[open_count - 1] ? BUSTER_PP_NOTE_AFTER_ELSE : 0;
                    seen_else[open_count - 1] |= kind == PP_DIRECTIVE_ELSE;
                }
                break;
            case PP_DIRECTIVE_ENDIF:
                open_count -= open_count != 0;
                break;
        }
        notes[d] = note;
    }
    for (uint32_t i = 0; result && i < open_count; i += 1)
    {
        notes[open[i]] |= BUSTER_PP_NOTE_UNTERMINATED;
    }
    uint32_t note_count = 0;
    for (uint32_t d = 0; result && d < count; d += 1)
    {
        note_count += notes[d] != 0;
    }
    data->note_directives = result ? arena_push_array(arena, (uint64_t)note_count + 1, sizeof(uint32_t), 4) : 0;
    data->note_kinds = result ? arena_push(arena, (uint64_t)note_count + 1, 1) : 0;
    result = data->note_directives != 0 && data->note_kinds != 0;
    data->note_count = 0;
    for (uint32_t d = 0; result && d < count; d += 1)
    {
        if (notes[d] != 0)
        {
            data->note_directives[data->note_count] = d;
            data->note_kinds[data->note_count] = notes[d];
            data->note_count += 1;
        }
    }
    scratch_end(scratch);
    return result;
}

// Each directive's kind and line end, then the conditional groups' pairing (pp_pair_conditionals_vector, or
// pp_pair_conditionals_scalar for the scalar kernels and for files the vectorized pairing leaves to it), then the
// notes (pp_note_directives) where there can be any, in `arena` with the rest of the table.
static bool pp_finish_directives(bool vectorized, Arena* arena, PPFileData* data, const uint64_t* lines)
{
    (void)vectorized;

    bool elifdef = false;
    for (uint32_t d = 0; d < data->directive_count; d += 1)
    {
        const uint32_t hash = data->directive_hash[d];
        const uint32_t end = pp_next_line(lines, data->token_count, hash);
        uint8_t kind = PP_DIRECTIVE_NULL;
        if (hash + 1 < end)
        {
            const uint8_t name_kind = data->kinds[hash + 1] & 0x7F;
            kind = name_kind == C_TOKEN_RAW_IDENTIFIER ? pp_directive_kind_of(data->values[hash + 1]) : name_kind == C_TOKEN_NUMERIC_CONSTANT ? PP_DIRECTIVE_LINE_MARKER : PP_DIRECTIVE_UNKNOWN;
        }
        data->directive_end[d] = end;
        data->directive_kind[d] = kind;
        data->directive_next[d] = BUSTER_PP_NONE;
        elifdef |= kind == PP_DIRECTIVE_ELIFDEF || kind == PP_DIRECTIVE_ELIFNDEF;
    }
    Scratch scratch = scratch_begin(0);
    uint32_t* indices = arena_push_array(scratch.arena, (uint64_t)data->directive_count + 32, sizeof(uint32_t), 64);
    bool result = indices != 0;
    bool notable = true;
    if (result && !(BUSTER_VECTOR_PATH(vectorized) && pp_pair_conditionals_vector(data, indices, &notable)))
    {
        pp_pair_conditionals_scalar(data, indices);
        notable = true;
    }
    scratch_end(scratch);
    data->note_count = 0;
    result = result && (!(notable || elifdef) || pp_note_directives(arena, data));
    return result;
}

// The include guard of a prepared file, or 0, as clang's multiple-include optimization finds one: the file is null
// directives ('#' alone), then `#ifndef X` (with extra tokens or not) or a `#if` whose line is `defined X` or
// `defined(X)` under an odd number of '!' and any parentheses, whose group has no other branch, and whose #endif's line
// only null directives follow. *opener gets the #ifndef's or #if's index.
static IdentId pp_find_guard(const PPFileData* data, uint32_t* opener)
{
    IdentId result = 0;
    const uint8_t* kinds = data->kinds;
    uint32_t first = 0;
    while (first < data->directive_count && data->directive_kind[first] == PP_DIRECTIVE_NULL && data->directive_hash[first] == first)
    {
        first += 1;
    }
    const uint32_t endif = first < data->directive_count && data->directive_hash[first] == first ? data->directive_next[first] : BUSTER_PP_NONE;
    bool closes = endif != BUSTER_PP_NONE && data->directive_kind[endif] == PP_DIRECTIVE_ENDIF;
    // Each null directive is one token, so only they follow the #endif's line when as many tokens as directives do.
    for (uint32_t d = endif + 1; closes && d < data->directive_count; d += 1)
    {
        closes = data->directive_kind[d] == PP_DIRECTIVE_NULL;
    }
    closes = closes && data->directive_end[endif] + (data->directive_count - 1 - endif) == data->token_count;
    const uint32_t hash = closes ? data->directive_hash[first] : 0;
    const uint32_t end = closes ? data->directive_end[first] : 0;
    if (closes && data->directive_kind[first] == PP_DIRECTIVE_IFNDEF && hash + 2 < end && (kinds[hash + 2] & 0x7F) == C_TOKEN_RAW_IDENTIFIER)
    {
        result = data->values[hash + 2];
    }
    else if (closes && data->directive_kind[first] == PP_DIRECTIVE_IF)
    {
        // Parentheses and '!' before `defined`, then as many ')' after its operand as there were '(' before.
        uint32_t t = hash + 2;
        uint32_t negations = 0;
        uint32_t parentheses = 0;
        while (t < end && ((kinds[t] & 0x7F) == C_TOKEN_EXCLAIM || (kinds[t] & 0x7F) == C_TOKEN_L_PAREN))
        {
            negations += (kinds[t] & 0x7F) == C_TOKEN_EXCLAIM;
            parentheses += (kinds[t] & 0x7F) == C_TOKEN_L_PAREN;
            t += 1;
        }
        const bool defined = t + 1 < end && (kinds[t] & 0x7F) == C_TOKEN_RAW_IDENTIFIER && data->values[t] == C_NAME_DEFINED;
        const bool plain = defined && (kinds[t + 1] & 0x7F) == C_TOKEN_RAW_IDENTIFIER;
        const bool enclosed = defined && !plain && t + 3 < end && (kinds[t + 1] & 0x7F) == C_TOKEN_L_PAREN && (kinds[t + 2] & 0x7F) == C_TOKEN_RAW_IDENTIFIER &&
                              (kinds[t + 3] & 0x7F) == C_TOKEN_R_PAREN;
        const IdentId name = plain ? data->values[t + 1] : enclosed ? data->values[t + 2] : 0;
        t += plain ? 2 : enclosed ? 4 : 0;
        uint32_t closing = 0;
        while (name != 0 && t + closing < end && (kinds[t + closing] & 0x7F) == C_TOKEN_R_PAREN)
        {
            closing += 1;
        }
        result = name != 0 && t + closing == end && closing == parentheses && negations % 2 == 1 ? name : 0;
    }
    *opener = first;
    return result;
}

// The prefixed literals a language mode doesn't have, as a mask of their kinds: clang lexes one as its prefix, an
// identifier, and the literal without it (u'a' before C11 is `u` and 'a'). The u, U and u8 prefixes are C11's, but u8 on
// a character constant is C23's; L is every mode's.
static uint64_t pp_split_kinds(CLanguage language)
{
    const uint64_t c11 = (1ull << C_TOKEN_UTF8_CHAR_CONSTANT) | (1ull << C_TOKEN_UTF16_CHAR_CONSTANT) | (1ull << C_TOKEN_UTF32_CHAR_CONSTANT) |
                         (1ull << C_TOKEN_UTF8_STRING_LITERAL) | (1ull << C_TOKEN_UTF16_STRING_LITERAL) | (1ull << C_TOKEN_UTF32_STRING_LITERAL);
    return language.standard < 2011 ? c11 : language.standard < 2023 ? 1ull << C_TOKEN_UTF8_CHAR_CONSTANT : 0;
}

// Is a token of this kind (its start-of-line bit aside) one the mode splits?
BUSTER_INLINE bool pp_splits_kind(uint64_t split, uint8_t kind)
{
    const uint8_t base = kind & 0x7F;
    return base < 64 && ((split >> base) & 1) != 0;
}

// Does the mode split an unknown token [start, end): an empty character constant or an unterminated literal that a
// prefix the mode doesn't have starts (`u8''` before C23), which clang lexes as the prefix and an unknown token from the
// quote? Such a token starts with u or U, and its prefix's letters are the u, U and 8 before its first quote (any other
// byte there is a line splice's); no other unknown token starts so.
static bool pp_splits_unknown(const uint8_t* bytes, uint32_t start, uint32_t end, uint64_t split)
{
    uint32_t at = start < end && (bytes[start] == 'u' || bytes[start] == 'U') ? start : end;
    uint32_t letters = 0;
    bool u = false;
    bool upper = false;
    bool eight = false;
    while (at < end && bytes[at] != '\'' && bytes[at] != '"')
    {
        letters += bytes[at] == 'u' || bytes[at] == 'U' || bytes[at] == '8';
        u |= letters == 1 && bytes[at] == 'u';
        upper |= letters == 1 && bytes[at] == 'U';
        eight |= letters == 2 && bytes[at] == '8';
        at += 1;
    }
    const bool character = at < end && bytes[at] == '\'';
    const uint8_t kind = !(at < end) ? 0
                         : u && eight && letters == 2 ? (character ? C_TOKEN_UTF8_CHAR_CONSTANT : C_TOKEN_UTF8_STRING_LITERAL)
                         : u && letters == 1 ? (character ? C_TOKEN_UTF16_CHAR_CONSTANT : C_TOKEN_UTF16_STRING_LITERAL)
                         : upper && letters == 1 ? (character ? C_TOKEN_UTF32_CHAR_CONSTANT : C_TOKEN_UTF32_STRING_LITERAL)
                         : 0;
    return kind != 0 && pp_splits_kind(split, kind);
}

// How many of `count` tokens a mode splits (`split`, pp_split_kinds), 64 kinds at a time: the mask spread to a byte per
// kind is a vpermi2b table, which reads only a kind's low 7 bits, so the start-of-line bit doesn't matter.
static uint32_t pp_count_splits_scalar(const uint8_t* bytes, const uint8_t* kinds, const uint32_t* starts, const uint32_t* ends, uint32_t count, uint64_t split);

static uint32_t pp_count_splits_vector(const uint8_t* bytes, const uint8_t* kinds, const uint32_t* starts, const uint32_t* ends, uint32_t count, uint64_t split)
{
#if !BUSTER_HOST_VECTOR
    return pp_count_splits_scalar(bytes, kinds, starts, ends, count, split);
#else
    const __m512i ones = _mm512_set1_epi8(1);
    const __m512i low = _mm512_maskz_mov_epi8((__mmask64)split, ones);
    const __m512i high = _mm512_setzero_si512();
    uint32_t result = 0;
    for (uint32_t i = 0; i < count; i += 64)
    {
        const __mmask64 valid = _bzhi_u64(~0ull, count - i < 64 ? count - i : 64);
        const __m512i kind = _mm512_maskz_loadu_epi8(valid, kinds + i);
        result += (uint32_t)__builtin_popcountll(_mm512_mask_test_epi8_mask(valid, _mm512_permutex2var_epi8(low, kind, high), ones));
        // The rare unknown tokens, one at a time (pp_splits_unknown).
        for (uint64_t unknown = _mm512_mask_cmpeq_epi8_mask(valid, _mm512_and_si512(kind, _mm512_set1_epi8(0x7F)), _mm512_set1_epi8(C_TOKEN_UNKNOWN)); unknown != 0; unknown &= unknown - 1)
        {
            const uint32_t t = i + (uint32_t)__builtin_ctzll(unknown);
            result += pp_splits_unknown(bytes, starts[t], ends[t], split);
        }
    }
    return result;
#endif
}

// pp_count_splits_vector's scalar twin.
static uint32_t pp_count_splits_scalar(const uint8_t* bytes, const uint8_t* kinds, const uint32_t* starts, const uint32_t* ends, uint32_t count, uint64_t split)
{
    uint32_t result = 0;
    for (uint32_t i = 0; i < count; i += 1)
    {
        result += pp_splits_kind(split, kinds[i]) || ((kinds[i] & 0x7F) == C_TOKEN_UNKNOWN && pp_splits_unknown(bytes, starts[i], ends[i], split));
    }
    return result;
}

// Splits the `splits` tokens whose kinds the mode splits into their prefix, an identifier with the token's start of line,
// and the literal from its first quote (a line splice can't hold one), in place from the end. The identifier ends after
// its last prefix character (u, U or 8, which a splice can't hold either): a splice before the quote stays between the
// two, where refinement reads it as no whitespace, and doesn't take the quote into the identifier. The arrays have room,
// since a lexer's arrays hold a token per byte. Returns the new count.
static uint32_t pp_split_prefixes(const uint8_t* bytes, uint8_t* kinds, uint32_t* starts, uint32_t* ends, uint32_t count, uint32_t splits, uint64_t split)
{
    uint32_t remaining = splits;
    for (uint32_t i = count; remaining != 0 && i > 0; i -= 1)
    {
        const uint32_t t = i - 1;
        const uint8_t kind = kinds[t];
        const uint32_t start = starts[t];
        const uint32_t end = ends[t];
        const bool unknown = (kind & 0x7F) == C_TOKEN_UNKNOWN && pp_splits_unknown(bytes, start, end, split);
        if (pp_splits_kind(split, kind) || unknown)
        {
            uint32_t quote = start;
            uint32_t prefix_end = start;
            while (bytes[quote] != '\'' && bytes[quote] != '"')
            {
                prefix_end = bytes[quote] == 'u' || bytes[quote] == 'U' || bytes[quote] == '8' ? quote + 1 : prefix_end;
                quote += 1;
            }
            kinds[t + remaining] = unknown ? C_TOKEN_UNKNOWN : (kind & 0x7F) <= C_TOKEN_UTF32_CHAR_CONSTANT ? C_TOKEN_CHAR_CONSTANT : C_TOKEN_STRING_LITERAL;
            starts[t + remaining] = quote;
            ends[t + remaining] = end;
            kinds[t + remaining - 1] = (uint8_t)((kind & 0x80) | C_TOKEN_RAW_IDENTIFIER);
            starts[t + remaining - 1] = start;
            ends[t + remaining - 1] = prefix_end;
            remaining -= 1;
        }
        else
        {
            kinds[t + remaining] = kind;
            starts[t + remaining] = start;
            ends[t + remaining] = end;
        }
    }
    return count + splits;
}

// Does token `i` hold a '$' that the vectorized lexer took into it (an identifier or a pp-number)?
static bool pp_holds_dollar(const uint8_t* bytes, const uint8_t* kinds, const uint32_t* starts, const uint32_t* ends, uint32_t i)
{
    const uint8_t kind = kinds[i] & 0x7F;
    return (kind == C_TOKEN_RAW_IDENTIFIER || kind == C_TOKEN_NUMERIC_CONSTANT) && memchr(bytes + starts[i], '$', ends[i] - starts[i]) != 0;
}

// The last token of the run token `i` starts: the tokens after it with nothing between them but line splices.
static uint32_t pp_run_end(uint32_t refine_flags, const uint8_t* bytes, uint32_t length, const uint32_t* starts, const uint32_t* ends, uint32_t count, uint32_t i)
{
    uint32_t result = i;
    while (result + 1 < count && (ends[result] == starts[result + 1] || refine_gap_is_splices(bytes, length, ends[result], starts[result + 1], refine_flags)))
    {
        result += 1;
    }
    return result;
}

// C's GNU identifier grammar includes '$'. Assembly instead needs $MACRO to expand MACRO, and so does C with
// -fno-dollars-in-identifiers (pp_dollar_punctuation): there '$' is punctuation, as C_TOKENIZER_NO_DOLLARS lexes it.
// The vectorized lexer takes '$' into identifiers and pp-numbers, so each run of tokens with nothing but line splices
// between them that holds such a token is lexed again that way: what follows a '$' can then join the tokens after it,
// as in clang ($L"x" is $ and a wide string literal, x$1.5 is x, $ and 1.5). Whitespace bounds the runs, and both
// lexings start and end tokens there alike, but for a literal a run's end cuts: a quote that C23's digit separators
// free ($1'2' is $, 1'2 and an unterminated character constant that takes the rest of the line). A run that ends in
// an unknown token of more than a '$' sends the whole file to C_TOKENIZER_NO_DOLLARS instead. Literals stay opaque.
// Scratch columns leave the original columns intact on allocation failure. A run can lex into more tokens, at most one
// per byte: the runs' bytes bound the columns. `bytes` are the whole file's, of `length` bytes.
static int64_t pp_split_dollars(uint32_t refine_flags, const uint8_t* bytes, uint32_t length, uint8_t* kinds, uint32_t* starts, uint32_t* ends, uint32_t count)
{
    // Most sources have no '$' at all, which one scan of their extent finds.
    const bool dollars = count != 0 && memchr(bytes + starts[0], '$', ends[count - 1] - starts[0]) != 0;
    uint64_t run_bytes = 0;
    for (uint32_t i = 0; dollars && i < count; i += 1)
    {
        if (pp_holds_dollar(bytes, kinds, starts, ends, i))
        {
            const uint32_t last = pp_run_end(refine_flags, bytes, length, starts, ends, count, i);
            run_bytes += ends[last] - starts[i];
            i = last;
        }
    }
    int64_t result = count;
    if (run_bytes != 0)
    {
        Scratch scratch = scratch_begin(0);
        const uint64_t capacity = (uint64_t)count + run_bytes + BUSTER_C_TOKENIZER_OUTPUT_SLACK;
        uint8_t* split_kinds = arena_push(scratch.arena, capacity, 64);
        uint32_t* split_starts = arena_push_array(scratch.arena, capacity, sizeof(uint32_t), 64);
        uint32_t* split_ends = arena_push_array(scratch.arena, capacity, sizeof(uint32_t), 64);
        result = split_kinds != 0 && split_starts != 0 && split_ends != 0 ? 0 : -1;
        bool whole = false;
        for (uint32_t i = 0; result >= 0 && !whole && i < count; i += 1)
        {
            if (pp_holds_dollar(bytes, kinds, starts, ends, i))
            {
                const uint32_t last = pp_run_end(refine_flags, bytes, length, starts, ends, count, i);
                const uint32_t begin = starts[i];
                const int64_t added = c_tokenize_scalar(bytes + begin, ends[last] - begin, split_kinds + result, split_starts + result, split_ends + result,
                                                        refine_flags | C_TOKENIZER_NO_DOLLARS);
                for (int64_t n = 0; n < added; n += 1)
                {
                    split_kinds[result + n] &= 0x7F;
                    split_starts[result + n] += begin;
                    split_ends[result + n] += begin;
                }
                if (added > 0)
                {
                    const int64_t final = result + added - 1;
                    split_kinds[result] |= kinds[i] & BUSTER_C_TOKEN_START_OF_LINE;
                    whole = split_kinds[final] == C_TOKEN_UNKNOWN && (split_ends[final] - split_starts[final] > 1 || bytes[split_starts[final]] != '$');
                }
                result = added >= 0 ? result + added : -1;
                i = last;
            }
            else
            {
                split_kinds[result] = kinds[i];
                split_starts[result] = starts[i];
                split_ends[result] = ends[i];
                result += 1;
            }
        }
        if (result >= 0 && whole)
        {
            result = c_tokenize_scalar(bytes, length, kinds, starts, ends, refine_flags | C_TOKENIZER_NO_DOLLARS);
        }
        else if (result >= 0)
        {
            memcpy(kinds, split_kinds, result);
            memcpy(starts, split_starts, (uint64_t)result * sizeof(uint32_t));
            memcpy(ends, split_ends, (uint64_t)result * sizeof(uint32_t));
        }
        scratch_end(scratch);
    }
    return result;
}

// The lexer's flags for a mode: refinement's (trigraphs, digraphs and "//" comments), and C_TOKENIZER_NO_DOLLARS where '$'
// is punctuation.
static uint32_t pp_lex_flags(const PPPrepare* mode)
{
    return mode->refine_flags | (mode->dollar_punctuation ? C_TOKENIZER_NO_DOLLARS : 0);
}

// Splits, among `count` freshly lexed tokens of `length` bytes, '$' out of the identifiers and pp-numbers that the
// vectorized lexer gave it to, where it is punctuation (pp_split_dollars: the scalar lexer, pp_lex_flags, lexes it so
// itself, so `vectorized` says which made them), then the prefixed literals that `mode`'s language doesn't have (a
// scan of the kinds, and the rare split: pp_split_prefixes), the literals pp_split_dollars makes among them. Returns
// the new count, or -1 on allocation failure.
static int64_t pp_lex_for_mode(const PPPrepare* mode, const uint8_t* bytes, uint32_t length, uint8_t* kinds, uint32_t* starts, uint32_t* ends, uint32_t count, bool vectorized)
{
    (void)vectorized;

    int64_t result = mode->dollar_punctuation && BUSTER_VECTOR_PATH(vectorized) ? pp_split_dollars(mode->refine_flags, bytes, length, kinds, starts, ends, count) : count;
    if (result > 0 && mode->split_kinds != 0)
    {
        const uint32_t splits = BUSTER_VECTOR_PATH(vectorized) ? pp_count_splits_vector(bytes, kinds, starts, ends, (uint32_t)result, mode->split_kinds)
                                          : pp_count_splits_scalar(bytes, kinds, starts, ends, (uint32_t)result, mode->split_kinds);
        result = splits != 0 ? pp_split_prefixes(bytes, kinds, starts, ends, (uint32_t)result, splits, mode->split_kinds) : result;
    }
    return result;
}

// Whether a lexed token is an unterminated block comment, which runs to the end of its text, so that only the last token
// can be one: an unknown token that starts with '/' (a '/' alone is a slash). Clang's lexer makes no token of it.
BUSTER_INLINE bool pp_is_unterminated_comment(const uint8_t* bytes, uint8_t kind, uint32_t start)
{
    return (kind & 0x7F) == C_TOKEN_UNKNOWN && bytes[start] == '/';
}

// The directive table's arrays for `count` directives, in `arena`. Returns false if memory runs out.
static bool pp_directive_arrays(Arena* arena, PPFileData* data, uint32_t count)
{
    data->directive_count = count;
    data->directive_hash = arena_push_array(arena, count + 1, sizeof(uint32_t), 4);
    data->directive_end = arena_push_array(arena, count + 1, sizeof(uint32_t), 4);
    data->directive_kind = arena_push(arena, count + 1, 1);
    data->directive_next = arena_push_array(arena, count + 1, sizeof(uint32_t), 4);
    return data->directive_hash != 0 && data->directive_end != 0 && data->directive_kind != 0 && data->directive_next != 0;
}

// A file's directive table (#118) from its refined tokens, in `arena`, by the kernels `vectorized` picks: the
// directives (pp_scan_directives_*), their kinds, line ends, pairing and notes (pp_finish_directives), and the include
// guard. Returns false if memory runs out. benchmark-preprocessor --tables times it alone.
static bool pp_directive_table(bool vectorized, Arena* arena, PPFileData* data)
{
    (void)vectorized;

    Scratch scratch = scratch_begin(0);
    uint64_t* lines = arena_push_array(scratch.arena, data->token_count / 64 + 1, sizeof(uint64_t), 64);
    bool result = lines != 0;
    if (result)
    {
        const uint32_t directive_count = BUSTER_VECTOR_PATH(vectorized) ? pp_scan_directives_vector(data->kinds, data->token_count, 0, lines)
                                                    : pp_scan_directives_scalar(data->kinds, data->token_count, 0, lines);
        result = pp_directive_arrays(arena, data, directive_count);
    }
    if (result)
    {
        if (BUSTER_VECTOR_PATH(vectorized))
        {
            pp_scan_directives_vector(data->kinds, data->token_count, data->directive_hash, lines);
        }
        else
        {
            pp_scan_directives_scalar(data->kinds, data->token_count, data->directive_hash, lines);
        }
        result = pp_finish_directives(BUSTER_VECTOR_PATH(vectorized), arena, data, lines);
    }
    scratch_end(scratch);
    data->guard = result ? pp_find_guard(data, &data->guard_opener) : 0;
    return result;
}

// pp_directive_table's scan for a file prepared a window at a time (pp_prepare_windows), for tokens [begin, end) as each
// window's blocks are refined, while their kinds are in the caches: per 64 tokens, the word of the tokens that start a
// line into `lines` (pp_scan_directives_vector's) and the word of the directives' '#' tokens into `directives`.
// Returns the directives found. Appending each directive's place to an array as the scan finds it, rather than taking
// them out of the words at the end, made the directive tables of sqlite3.c and its headers take 0.01 ms more on the
// 9700X (0.316-0.318 ms against 0.306-0.307).
static uint32_t pp_scan_directives_window(const uint8_t* kinds, uint32_t begin, uint32_t end, uint64_t* lines, uint64_t* directives)
{
    uint32_t result = 0;
    for (uint32_t i = begin; i < end; i += 64)
    {
        const __mmask64 valid = end - i >= 64 ? ~0ull : _bzhi_u64(~0ull, end - i);
        const __m512i block = _mm512_maskz_loadu_epi8(valid, kinds + i);
        const uint64_t word = _mm512_mask_cmpeq_epi8_mask(valid, block, _mm512_set1_epi8((char)(C_TOKEN_HASH | BUSTER_C_TOKEN_START_OF_LINE)));
        lines[i / 64] = _mm512_movepi8_mask(block) & valid;
        directives[i / 64] = word;
        result += (uint32_t)__builtin_popcountll(word);
    }
    return result;
}
// The windows' scans start at refinement's block boundaries, a word of 64 tokens apart.
_Static_assert(BUSTER_REFINE_BLOCK_TOKENS % 64 == 0, "pp_scan_directives_window takes whole words of 64 tokens");

// The places of the tokens whose bits are set in `words`, a word per 64 tokens, `word_count` of them (the directives'
// '#' tokens that pp_scan_directives_window found), into `places` in order, a bit at a time: pp_directive_places' exact
// twin. Returns how many.
static uint32_t pp_directive_places_scalar(const uint64_t* words, uint32_t word_count, uint32_t* places)
{
    uint32_t count = 0;
    for (uint32_t w = 0; w < word_count; w += 1)
    {
        for (uint64_t bits = words[w]; bits != 0; bits &= bits - 1)
        {
            places[count] = 64 * w + (uint32_t)__builtin_ctzll(bits);
            count += 1;
        }
    }
    return count;
}

// pp_directive_places_scalar eight words at a time: a group of no directive is skipped, and each word of the others has
// the places of its set bits compressed out of a vector of 0 to 63 (vpcompressb), widened to dwords, offset by the
// word's first token and stored under a mask of their count, without a branch on the word's bits, which a bit at a time
// mispredicted: sqlite3.c's places took 45 us so on the 9700X (#451). The groups' branch keeps a file of few directives
// in long lines from paying a word's compress for each 64 tokens. A word of more than 16 (16 directives in 64 tokens,
// null ones) takes the bit-at-a-time loop for all of its places. Writes `places` only below the count it returns.
static uint32_t pp_directive_places(const uint64_t* words, uint32_t word_count, uint32_t* places)
{
#if !BUSTER_HOST_VECTOR
    return pp_directive_places_scalar(words, word_count, places);
#else
    const __m512i positions = _mm512_set_epi8(63, 62, 61, 60, 59, 58, 57, 56, 55, 54, 53, 52, 51, 50, 49, 48, 47, 46, 45, 44, 43, 42, 41, 40, 39, 38, 37,
                                              36, 35, 34, 33, 32, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9,
                                              8, 7, 6, 5, 4, 3, 2, 1, 0);
    uint32_t count = 0;
    for (uint32_t group = 0; group < word_count; group += 8)
    {
        const uint32_t size = word_count - group < 8 ? word_count - group : 8;
        const __m512i eight = _mm512_maskz_loadu_epi64((__mmask8)_bzhi_u32(0xFF, size), words + group);
        for (uint32_t w = group; _mm512_test_epi64_mask(eight, eight) != 0 && w < group + size; w += 1)
        {
            const uint64_t bits = words[w];
            const uint32_t here = (uint32_t)__builtin_popcountll(bits);
            const __m128i set = _mm512_castsi512_si128(_mm512_maskz_compress_epi8(bits, positions));
            _mm512_mask_storeu_epi32(places + count, (__mmask16)_bzhi_u32(0xFFFF, here), _mm512_add_epi32(_mm512_cvtepu8_epi32(set), _mm512_set1_epi32((int)(64 * w))));
            if (BUSTER_UNLIKELY(here > 16))
            {
                pp_directive_places_scalar(words + w, 1, places + count);
                for (uint32_t i = 0; i < here; i += 1)
                {
                    places[count + i] += 64 * w;
                }
            }
            count += here;
        }
    }
    return count;
#endif
}

// pp_directive_table for a file prepared a window at a time, whose `count` directives pp_scan_directives_window found
// in the words of `directives`, and its lines in those of `lines`: the directives' '#' tokens out of their words into
// the table (pp_directive_places).
static bool pp_directive_table_windowed(Arena* arena, PPFileData* data, const uint64_t* lines, const uint64_t* directives, uint32_t count)
{
    bool result = pp_directive_arrays(arena, data, count);
    if (result)
    {
        pp_directive_places(directives, (data->token_count + 63) / 64, data->directive_hash);
    }
    result = result && pp_finish_directives(true, arena, data, lines);
    data->guard = result ? pp_find_guard(data, &data->guard_opener) : 0;
    return result;
}

// What preparing a file came to (pp_prepare_into).
typedef enum PPPrepareStatus
{
    PP_PREPARE_DONE,
    // The lexer can't take the file: its positions must fit 32 bits.
    PP_PREPARE_TOO_BIG,
    // No room for the lexer's columns.
    PP_PREPARE_NO_COLUMNS,
    // No room for the rest, or the intern table is full.
    PP_PREPARE_NO_MEMORY,
} PPPrepareStatus;

// The UNKNOWN tokens among `count` kinds, where a raw string literal's error can be (raw strings on, or C++), by the
// kernels `prepare` picks: 0 otherwise.
static uint32_t pp_count_raw_candidates(const PPPrepare* prepare, const uint8_t* kinds, uint64_t count)
{
    uint32_t unknowns = 0;
    const bool raw = (prepare->refine_flags & (C_TOKENIZER_RAW_STRINGS | C_TOKENIZER_CPLUSPLUS)) != 0;
    for (uint64_t t = 0; raw && t < count; t += 64)
    {
        const uint32_t chunk = count - t < 64 ? (uint32_t)(count - t) : 64;
        if (BUSTER_VECTOR_PATH(prepare->vectorized))
        {
            const __mmask64 valid = _bzhi_u64(~0ull, chunk);
            const __m512i chunk_kinds = _mm512_and_si512(_mm512_maskz_loadu_epi8(valid, kinds + t), _mm512_set1_epi8(0x7F));
            unknowns += (uint32_t)__builtin_popcountll(_mm512_mask_cmpeq_epi8_mask(valid, chunk_kinds, _mm512_set1_epi8(C_TOKEN_UNKNOWN)));
        }
        else
        {
            for (uint32_t i = 0; i < chunk; i += 1)
            {
                unknowns += (kinds[t + i] & 0x7F) == C_TOKEN_UNKNOWN;
            }
        }
    }
    return unknowns;
}

// Keep only exceptional raw errors, never a per-token diagnostic column. Ordinary files allocate nothing. Both
// preparation kernels use the same independent scalar delimiter check on the UNKNOWN candidates they produced, of which
// the file has `unknowns` (pp_count_raw_candidates).
static bool pp_record_raw_errors(const PPPrepare* prepare, Arena* arena, PPFileData* data, uint32_t unknowns)
{
    data->raw_error_count = 0;
    data->raw_errors = 0;
    const uint64_t mark = arena->length;
    data->raw_errors = unknowns != 0 ? arena_push_array(arena, unknowns, sizeof(PPRawError), _Alignof(PPRawError)) : 0;
    const bool result = unknowns == 0 || data->raw_errors != 0;
    for (uint32_t t = 0; result && unknowns != 0 && t < data->token_count; t += 1)
    {
        if ((data->kinds[t] & 0x7F) == C_TOKEN_UNKNOWN)
        {
            uint64_t offset;
            const CRawStringError error = c_raw_string_error(data->bytes + data->starts[t], data->length - data->starts[t], prepare->refine_flags, &offset);
            if (error != C_RAW_STRING_OK)
            {
                data->raw_errors[data->raw_error_count] = (PPRawError){ .token = t, .offset = data->starts[t] + (uint32_t)offset, .kind = error };
                data->raw_error_count += 1;
            }
        }
    }
    if (result && unknowns != 0)
    {
        // Unknown punctuation (notably assembly '$') isn't a raw error. Keep only the records actually written,
        // before any other allocation in this arena; unused capacity hasn't been touched.
        const uint64_t keep = data->raw_error_count != 0 ? (uint64_t)((uint8_t*)data->raw_errors - arena->base) + (uint64_t)data->raw_error_count * sizeof(PPRawError) : mark;
        arena_pop_to(arena, keep);
        data->raw_errors = data->raw_error_count != 0 ? data->raw_errors : 0;
    }
    return result;
}

// The candidates a file's scan keeps the places of (PPNamedUcnScan.kept), so that pp_record_named_ucns reads them alone.
#define BUSTER_PP_NAMED_UCN_KEPT 256

// Where a file's named universal character names (\N{...}) and the `\u`s clang reads as none can be: the candidates its
// scan counts (pp_named_ucn_candidates; pp_scan_named_ucns or pp_scan_named_ucn_chunks), the first's and the last's byte,
// and the first BUSTER_PP_NAMED_UCN_KEPT ones' bytes (#1447: a `\u` in a string literal is a candidate too, and they may
// be far apart).
typedef struct PPNamedUcnScan PPNamedUcnScan;
struct PPNamedUcnScan
{
    uint32_t count;
    uint64_t first;
    uint64_t last;
    uint64_t kept[BUSTER_PP_NAMED_UCN_KEPT];
};

// Adds the candidates `bits` of the block at byte `b` to `scan`.
BUSTER_INLINE void pp_named_ucn_keep(PPNamedUcnScan* scan, uint64_t b, uint64_t bits)
{
    scan->first = scan->count == 0 ? b + (uint64_t)__builtin_ctzll(bits) : scan->first;
    scan->last = b + 63 - (uint64_t)__builtin_clzll(bits);
    for (uint64_t rest = bits; rest != 0; rest &= rest - 1)
    {
        const uint32_t at = scan->count;
        scan->count += 1;
        if (at < BUSTER_PP_NAMED_UCN_KEPT)
        {
            scan->kept[at] = b + (uint64_t)__builtin_ctzll(rest);
        }
    }
}

// The candidates for a named UCN's backslash among bytes [b, end) of a source of `length` bytes, at most 64 of them, as
// bits: a backslash (or with trigraphs a '/' after a '?', the end of a ??/) followed by 'N', 'u' or 'U' (#1447: a `\u`
// clang reads as no UCN is -Wunicode's too), by a line splice's backslash and a
// blank or a line break, or with trigraphs by a '?' (a ??/ that splices the line). A named UCN's backslash is always one,
// since its N follows it after any line splices; pp_named_ucn_at decides which of them are. No byte past `length` is
// read: the loads are masked. The vector path and its scalar twin give the same bits.
BUSTER_INLINE uint64_t pp_named_ucn_candidates(const uint8_t* bytes, uint64_t length, uint64_t b, uint64_t end, bool trigraphs, bool vectorized)
{
    (void)vectorized;

    const uint32_t count = end - b < 64 ? (uint32_t)(end - b) : 64;
    uint64_t result = 0;
    if (BUSTER_VECTOR_PATH(vectorized))
    {
        // A whole block before the source's last two bytes loads plainly; the others, masked.
        const bool whole = count == 64 && length - b >= 66;
        const __m512i bytes_0 = whole ? _mm512_loadu_si512(bytes + b) : _mm512_maskz_loadu_epi8(_bzhi_u64(~0ull, count), bytes + b);
        // With trigraphs, the '?' before each byte: the block's own, a lane up, and the byte before the block.
        const __mmask64 question_before = trigraphs ? (_mm512_cmpeq_epi8_mask(bytes_0, _mm512_set1_epi8('?')) << 1) | (uint64_t)(b != 0 && bytes[b - 1] == '?') : 0;
        const __mmask64 slash = _mm512_cmpeq_epi8_mask(bytes_0, _mm512_set1_epi8('\\')) | (_mm512_cmpeq_epi8_mask(bytes_0, _mm512_set1_epi8('/')) & question_before);
        if (slash != 0)
        {
            const __mmask64 next_valid = length > b + 1 ? _bzhi_u64(~0ull, length - (b + 1) < 64 ? (uint32_t)(length - (b + 1)) : 64) : 0;
            const __mmask64 after_valid = length > b + 2 ? _bzhi_u64(~0ull, length - (b + 2) < 64 ? (uint32_t)(length - (b + 2)) : 64) : 0;
            const __m512i bytes_1 = whole ? _mm512_loadu_si512(bytes + b + 1) : _mm512_maskz_loadu_epi8(next_valid, bytes + b + 1);
            const __m512i bytes_2 = whole ? _mm512_loadu_si512(bytes + b + 2) : _mm512_maskz_loadu_epi8(after_valid, bytes + b + 2);
            // A blank or a line break: ' ', or '\t' to '\r'.
            const __mmask64 blank = _mm512_cmpeq_epi8_mask(bytes_2, _mm512_set1_epi8(' ')) |
                                    _mm512_cmple_epu8_mask(_mm512_sub_epi8(bytes_2, _mm512_set1_epi8('\t')), _mm512_set1_epi8('\r' - '\t'));
            // ('u' and 'U' are one compare of the byte with bit 5 set.)
            const __mmask64 letter = _mm512_cmpeq_epi8_mask(bytes_1, _mm512_set1_epi8('N')) |
                                     _mm512_cmpeq_epi8_mask(_mm512_or_si512(bytes_1, _mm512_set1_epi8(0x20)), _mm512_set1_epi8('u'));
            const __mmask64 next = letter | (_mm512_cmpeq_epi8_mask(bytes_1, _mm512_set1_epi8('\\')) & blank) |
                                   (trigraphs ? _mm512_cmpeq_epi8_mask(bytes_1, _mm512_set1_epi8('?')) : 0);
            result = slash & next;
        }
    }
    else
    {
        for (uint32_t k = 0; k < count; k += 1)
        {
            const uint64_t i = b + k;
            const uint8_t next = i + 1 < length ? bytes[i + 1] : 0;
            const uint8_t after = i + 2 < length ? bytes[i + 2] : 0;
            const bool slash = bytes[i] == '\\' || (trigraphs && bytes[i] == '/' && i != 0 && bytes[i - 1] == '?');
            const bool blank = after == ' ' || (after >= '\t' && after <= '\r');
            const bool candidate = slash && (next == 'N' || next == 'u' || next == 'U' || (next == '\\' && blank) || (trigraphs && next == '?'));
            result |= (uint64_t)candidate << k;
        }
    }
    return result;
}

// Adds the candidates among bytes [begin, end) of a source of `length` bytes (pp_named_ucn_candidates) to `scan`, a byte at
// a time: the scalar kernels' search, which the vector kernels' (pp_scan_named_ucn_chunks) must find the same named UCNs
// as.
static void pp_scan_named_ucns(const uint8_t* bytes, uint64_t length, uint64_t begin, uint64_t end, bool trigraphs, PPNamedUcnScan* scan)
{
    for (uint64_t b = begin; b < end; b += 64)
    {
        const uint64_t bits = pp_named_ucn_candidates(bytes, length, b, end, trigraphs, false);
        if (BUSTER_UNLIKELY(bits != 0))
        {
            pp_named_ucn_keep(scan, b, bits);
        }
    }
}

// Adds the candidates (pp_named_ucn_candidates) of a source's 64-byte chunks that refinement marked as holding a backslash
// or with trigraphs a '?' (refine_scan_chunks, refine_tokens_chunks), in words [first, last) of its bitmap `chunks`, to
// `scan`: the vector kernels' search, which reads no byte again but in those chunks, and with trigraphs in the chunk after
// each (a ??/ whose '/' starts it). The chunks cover the source up to its last token's end, past which no named UCN is
// in a token. Most files have no candidate, and most chunks no backslash.
static void pp_scan_named_ucn_chunks(const uint8_t* bytes, uint64_t length, const uint64_t* chunks, uint64_t first, uint64_t last, bool trigraphs,
                                     PPNamedUcnScan* scan)
{
    uint64_t carry = trigraphs && first != 0 ? chunks[first - 1] >> 63 : 0;
    for (uint64_t word = first; word < last; word += 1)
    {
        const uint64_t marked = chunks[word];
        const uint64_t read = trigraphs ? marked | (marked << 1) | carry : marked;
        carry = trigraphs ? marked >> 63 : 0;
        for (uint64_t chunk_bits = read; chunk_bits != 0; chunk_bits &= chunk_bits - 1)
        {
            const uint64_t b = (word * 64 + (uint64_t)__builtin_ctzll(chunk_bits)) * 64;
            const uint64_t bits = b < length ? pp_named_ucn_candidates(bytes, length, b, length - b < 64 ? length : b + 64, trigraphs, true) : 0;
            if (BUSTER_UNLIKELY(bits != 0))
            {
                pp_named_ucn_keep(scan, b, bits);
            }
        }
    }
}

// Is candidate byte `i` of a file (pp_named_ucn_candidates) the backslash (or the end of ??/) of a named UCN that starts
// an identifier or an unknown token and that clang diagnoses there? Then `token` is that token, `position` the UCN's
// first byte and `kind` the diagnostic's. clang reads one within a token (an identifier's or a pp-number's later
// character) without a diagnostic (tryConsumeIdentifierUCN): its name exactly Unicode's and its character one the token
// may have, or else the token ends before it and it starts the next. Where it starts a token, its name not exactly
// Unicode's or a character below U+00A0 (whatever its name loosely stands for) is an error (BUSTER_PP_NAMED_UCN_ERROR,
// pp_diagnose_named_ucn); an exact name's, -pedantic's warning that it is clang's extension
// (BUSTER_PP_NAMED_UCN_EXTENSION, #1447); and a `\N` clang reads as no UCN (no `{`, an empty name or no `}`), a -Wunicode
// warning (#1447, BUSTER_PP_NAMED_UCN_INCOMPLETE and after). A UCN in a literal or a comment is in neither: #if words one
// in a character constant when it reads it (pp_parse_ucn), and the checker one in a literal.
static bool pp_named_ucn_at(uint32_t refine_flags, const PPFileData* data, uint64_t i, uint32_t* token, uint64_t* position, uint8_t* kind)
{
    // As pp_file_lexer reads the file.
    const ScalarLexer lexer = {
        .source = data->bytes,
        .length = data->length,
        .trigraphs = (refine_flags & C_TOKENIZER_TRIGRAPHS) != 0,
        .cplusplus = false,
    };
    const bool trigraph = data->bytes[i] == '/';
    const uint64_t start = trigraph && i >= 2 ? i - 2 : i;
    bool result = !trigraph || (lexer.trigraphs && i >= 2 && data->bytes[i - 1] == '?' && data->bytes[i - 2] == '?');
    uint64_t size = 0;
    uint64_t at = 0;
    result = result && get_char(&lexer, start, &size, &at) == '\\' && at == start;
    uint64_t kind_size = 0;
    const int32_t letter = result ? peek_char(&lexer, start + size, &kind_size) : 0;
    const bool named_letter = letter == 'N';
    result = result && (named_letter || letter == 'u' || letter == 'U');
    uint64_t end = 0;
    uint32_t code_point = 0;
    const bool read = result && read_ucn(&lexer, start, &end, &code_point);
    // What clang reads where no UCN is (read_ucn_name's shapes): no `{`, `{}`, or a name the line ends in.
    uint64_t brace_size = 0;
    const bool braced = result && !read && peek_char(&lexer, start + size + kind_size, &brace_size) == '{';
    uint64_t close_size = 0;
    const bool empty = braced && peek_char(&lexer, start + size + kind_size + brace_size, &close_size) == '}';
    // (A name exactly Unicode's of a character at U+00A0 or past it is no error, but -pedantic's warning.)
    uint8_t name[BUSTER_PP_NAMED_UCN_NAME_BYTES];
    uint64_t name_length = 0;
    uint64_t name_start = 0;
    uint64_t name_end = 0;
    uint32_t exact = 0;
    const bool named = named_letter && read && code_point != 0 && read_ucn_name(&lexer, start, name, sizeof(name), &name_length, &name_start, &name_end) &&
                       name_length < sizeof(name) && unicode_name_strict(name, (uint32_t)name_length, &exact);
    // A `\u` or `\U` clang reads as no UCN (#1447): no hex digit, too few, or a delimited `\u`'s braces (a `\U` takes none).
    uint64_t digit_size = 0;
    const int32_t digit = result && !named_letter && !read ? peek_char(&lexer, start + size + kind_size, &digit_size) : 0;
    const bool hex = (digit >= '0' && digit <= '9') || ((digit | 0x20) >= 'a' && (digit | 0x20) <= 'f');
    // (A `\U{` is clang's error, "\U used with no following hex digits", not this warning.)
    const uint8_t numeric = letter == 'U' && braced ? 0 : !hex && !braced ? BUSTER_PP_UCN_NO_DIGITS : !braced ? BUSTER_PP_NAMED_UCN_INCOMPLETE :
                            empty ? BUSTER_PP_NAMED_UCN_EMPTY : BUSTER_PP_NAMED_UCN_UNTERMINATED;
    *kind = !result ? 0 : !named_letter ? (read ? (code_point == 0 ? BUSTER_PP_UCN_ERROR : 0) : numeric) : read && code_point == 0 ? BUSTER_PP_NAMED_UCN_ERROR : named ? BUSTER_PP_NAMED_UCN_EXTENSION :
            read ? 0 : !braced ? BUSTER_PP_NAMED_UCN_INCOMPLETE : empty ? BUSTER_PP_NAMED_UCN_EMPTY : BUSTER_PP_NAMED_UCN_UNTERMINATED;
    result = *kind != 0;
    // The token it may start: the last that starts at it or before it.
    uint32_t low = 0;
    uint32_t high = result ? data->token_count : 0;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        const bool before = data->starts[middle] <= start;
        low = before ? middle + 1 : low;
        high = before ? high : middle;
    }
    result = result && low != 0;
    const uint32_t t = low != 0 ? low - 1 : 0;
    const uint8_t token_kind = result ? data->kinds[t] & 0x7F : C_TOKEN_KIND_COUNT;
    result = result && (token_kind == C_TOKEN_UNKNOWN || token_kind == C_TOKEN_RAW_IDENTIFIER) && data->starts[t] == start;
    *token = t;
    *position = start;
    return result;
}

// Adds an error of kind BUSTER_PP_NAMED_UCN_ERROR to the file's raw errors for each named UCN that starts an identifier
// or an unknown token and that clang diagnoses there (pp_named_ucn_at), at its backslash, merged with
// pp_record_raw_errors's, in the arena they were just pushed to, in the token order the readers' search takes. Only a
// file with a candidate (`scan`) reads its bytes again, from the first candidate to the last. Returns false if the arena
// has no room.
static bool pp_record_named_ucns(const PPPrepare* prepare, Arena* arena, PPFileData* data, const PPNamedUcnScan* scan)
{
    bool result = true;
    if (BUSTER_UNLIKELY(scan->count != 0))
    {
        const uint64_t mark = arena->length;
        const uint32_t raw_count = data->raw_error_count;
        PPRawError* errors = arena_push_array(arena, (uint64_t)raw_count + scan->count, sizeof(PPRawError), _Alignof(PPRawError));
        result = errors != 0;
        const bool trigraphs = (prepare->refine_flags & C_TOKENIZER_TRIGRAPHS) != 0;
        uint32_t named = 0;
        // The kept candidates' places, or past those, every block from the first candidate to the last again.
        const bool kept = scan->count <= BUSTER_PP_NAMED_UCN_KEPT;
        for (uint32_t k = 0; result && kept && k < scan->count; k += 1)
        {
            uint32_t token = 0;
            uint64_t position = 0;
            uint8_t kind = 0;
            if (pp_named_ucn_at(prepare->refine_flags, data, scan->kept[k], &token, &position, &kind))
            {
                errors[raw_count + named] = (PPRawError){ .token = token, .offset = (uint32_t)position, .kind = kind };
                named += 1;
            }
        }
        for (uint64_t b = scan->first; result && !kept && b <= scan->last; b += 64)
        {
            for (uint64_t bits = pp_named_ucn_candidates(data->bytes, data->length, b, scan->last + 1, trigraphs, BUSTER_VECTOR_PATH(prepare->vectorized)); bits != 0; bits &= bits - 1)
            {
                uint32_t token = 0;
                uint64_t position = 0;
                uint8_t kind = 0;
                if (named < scan->count && pp_named_ucn_at(prepare->refine_flags, data, b + (uint64_t)__builtin_ctzll(bits), &token, &position, &kind))
                {
                    errors[raw_count + named] = (PPRawError){ .token = token, .offset = (uint32_t)position, .kind = kind };
                    named += 1;
                }
            }
        }
        if (result && named != 0)
        {
            // The raw string errors and these merged by token, in place: the next one written is never past the next of
            // these to read.
            uint32_t raw = 0;
            uint32_t ucn = 0;
            for (uint32_t k = 0; k < raw_count + named; k += 1)
            {
                const bool take_raw = raw < raw_count && (ucn == named || data->raw_errors[raw].token <= errors[raw_count + ucn].token);
                errors[k] = take_raw ? data->raw_errors[raw] : errors[raw_count + ucn];
                raw += take_raw;
                ucn += !take_raw;
            }
            data->raw_errors = errors;
            data->raw_error_count = raw_count + named;
            arena_pop_to(arena, (uint64_t)((uint8_t*)errors - arena->base) + (uint64_t)data->raw_error_count * sizeof(PPRawError));
        }
        else if (result)
        {
            arena_pop_to(arena, mark);
        }
    }
    return result;
}

// pp_prepare_into for the whole file at once: lexed, then refined, then indexed.
static PPPrepareStatus pp_prepare_whole(const PPPrepare* prepare, InternTable* table, Arena* columns, Arena* arena, PPFileData* data, uint64_t* ticks)
{
    const uint64_t capacity = BUSTER_C_TOKENIZER_CAPACITY((uint64_t)data->length);
    data->kinds = arena_push(&columns[0], capacity, 64);
    data->starts = arena_push_array(&columns[1], capacity, sizeof(uint32_t), 64);
    data->values = arena_push_array(&columns[2], capacity, sizeof(uint32_t), 64);
    const bool pushed = data->kinds != 0 && data->starts != 0 && data->values != 0;
    PPPrepareStatus result = PP_PREPARE_DONE;
    uint64_t tick = ticks != 0 ? __rdtsc() : 0;
    int64_t count = -1;
    if (pushed)
    {
        // The scalar kernels lex '$' as punctuation themselves, the twin of the vectorized lexer and pp_split_dollars.
        count = prepare->dollar_punctuation && !BUSTER_VECTOR_PATH(prepare->vectorized) ? c_tokenize_scalar(data->bytes, data->length, data->kinds, data->starts, data->values, pp_lex_flags(prepare))
                                                                    : c_tokenize(data->bytes, data->length, data->kinds, data->starts, data->values, prepare->refine_flags);
    }
    const bool lexed = count >= 0;
    count = count > 0 ? pp_lex_for_mode(prepare, data->bytes, data->length, data->kinds, data->starts, data->values, (uint32_t)count, BUSTER_VECTOR_PATH(prepare->vectorized)) : count;
    if (ticks != 0)
    {
        const uint64_t now = __rdtsc();
        ticks[PP_PHASE_LEX] += now - tick;
        tick = now;
    }
    if (count < 0)
    {
        result = !pushed ? PP_PREPARE_NO_COLUMNS : !lexed ? PP_PREPARE_TOO_BIG : PP_PREPARE_NO_MEMORY;
    }
    else
    {
        // An unterminated block comment (pp_is_unterminated_comment), reported when this inclusion reads it.
        if (count > 0 && pp_is_unterminated_comment(data->bytes, data->kinds[count - 1], data->starts[count - 1]))
        {
            data->unterminated_comment = data->starts[count - 1] + 1;
            data->unterminated_comment_starts_line = (data->kinds[count - 1] & BUSTER_C_TOKEN_START_OF_LINE) != 0;
            count -= 1;
        }
        data->token_count = (uint32_t)count;
        // Each column keeps what the file has, with the lexer's slack, and the next file's starts after it. The lexer
        // needs room for a token per byte, but a file has about one per six: with the columns of every file in one
        // arena, the room between them would be cleared too wherever a huge page holds a used byte, which for sqlite3.c
        // and its headers was 9 bytes per input byte. The leading-space words, whose count is now known, need no room
        // of their own: a word past the last token's, for pp_store_file_tokens. They go after the kinds, where the
        // lexer's writes have most likely touched their page already (refinement then needn't fault a fresh one in).
        const uint64_t kept = (uint64_t)count + BUSTER_C_TOKENIZER_OUTPUT_SLACK;
        arena_pop_to(&columns[0], (uint64_t)(data->kinds - columns[0].base) + kept);
        arena_pop_to(&columns[1], (uint64_t)((uint8_t*)data->starts - columns[1].base) + kept * sizeof(uint32_t));
        arena_pop_to(&columns[2], (uint64_t)((uint8_t*)data->values - columns[2].base) + kept * sizeof(uint32_t));
        data->space = arena_push_array(&columns[0], (uint64_t)count / 64 + 2, sizeof(uint64_t), 64);
        bool refined = data->space != 0 && pp_record_raw_errors(prepare, arena, data, pp_count_raw_candidates(prepare, data->kinds, data->token_count));
        // The named UCNs' candidates: from the chunks with a backslash that refinement marks (pp_scan_named_ucn_chunks), or
        // a byte at a time by the scalar kernels (pp_scan_named_ucns), while the bytes just lexed are in the caches.
        const bool trigraphs = (prepare->refine_flags & C_TOKENIZER_TRIGRAPHS) != 0;
        PPNamedUcnScan named = { 0 };
        if (refined && prepare->vectorized)
        {
            Scratch scratch = scratch_begin(0);
            uint64_t* chunks = arena_push_array(scratch.arena, refine_progress_words(data->length), sizeof(uint64_t), 64);
            RefineProgress progress;
            refine_progress_begin(&progress, chunks);
            refined = chunks != 0 && refine_tokens_chunks(table, data->bytes, data->length, data->kinds, data->starts, data->values, data->token_count, data->values,
                                                          data->space, prepare->refine_flags, &progress);
            if (refined)
            {
                pp_scan_named_ucn_chunks(data->bytes, data->length, chunks, 0, progress.scanned, trigraphs, &named);
            }
            scratch_end(scratch);
        }
        else if (refined)
        {
            refined = refine_tokens_scalar(table, data->bytes, data->length, data->kinds, data->starts, data->values, data->token_count, data->values,
                                           data->space, prepare->refine_flags);
            pp_scan_named_ucns(data->bytes, data->length, 0, data->length, trigraphs, &named);
        }
        refined = refined && pp_record_named_ucns(prepare, arena, data, &named);
        if (ticks != 0)
        {
            const uint64_t now = __rdtsc();
            ticks[PP_PHASE_REFINE] += now - tick;
            tick = now;
        }
        const bool indexed = refined && pp_directive_table(BUSTER_VECTOR_PATH(prepare->vectorized), arena, data);
        if (indexed)
        {
            data->prepared = true;
        }
        result = indexed ? PP_PREPARE_DONE : PP_PREPARE_NO_MEMORY;
        if (ticks != 0)
        {
            ticks[PP_PHASE_DIRECTIVES] += __rdtsc() - tick;
        }
    }
    return result;
}

// Where a window [at, end) of a file, not its last, is lexed to (pp_prepare_windows), from its last two line ends that
// aren't line splices': a '\n' that doesn't follow a backslash, nor a '\r' after one, and a '\r' that doesn't follow a
// backslash and isn't the start of a "\r\n" in the window, 64 bytes at a time from its end. The lexer drops the tokens
// from the last line it lexes on, which the next window lexes again, so:
// - `end` when the part of a line after the last line end, which the next window lexes again, is no longer than
//   `longest_tail` (BUSTER_PP_WINDOW_TAIL_BYTES): an ordinary window, which needs no line end before it, so the scan
//   stops there;
// - past the last line end when that part, which the window's end cuts, is longer, and the line that ends there,
//   which is lexed again instead, is shorter: the start of a line longer than most, which x86_64_encoder.c's
//   BUSTER_X86_VECTOR_FORMS (157 KB on 1,695 lines) had 110 KB of lexed twice (#451);
// - `at`, so that the window is doubled unlexed, when it has no line end (its line goes on through splices alone, as a
//   macro defined over many lines does, and would keep no token) or only the one before that part (lexed, the window
//   would keep its first line and lex the long one's start again);
// - `end` otherwise, a line before the last line end longer than the part after it.
// A splice the lexer takes otherwise (with blanks after the backslash, or a trigraph's) counts as a line end here; the
// cut only spares work, and the tokens are the same wherever it falls.
static uint64_t pp_window_cut(const uint8_t* bytes, uint64_t at, uint64_t end, uint64_t longest_tail)
{
    // The last line end and the one before it, from the end: UINT64_MAX until found.
    uint64_t last = UINT64_MAX;
    uint64_t before = UINT64_MAX;
    uint64_t chunk_end = end;
    while (before == UINT64_MAX && chunk_end > at && (last == UINT64_MAX || end - 1 - last > longest_tail))
    {
        const uint64_t base = chunk_end - at >= 64 ? chunk_end - 64 : at;
        const __mmask64 valid = _bzhi_u64(~0ull, (uint32_t)(chunk_end - base));
        const __m512i chunk = _mm512_maskz_loadu_epi8(valid, bytes + base);
        const uint64_t newlines = _mm512_mask_cmpeq_epi8_mask(valid, chunk, _mm512_set1_epi8('\n'));
        const uint64_t returns = _mm512_mask_cmpeq_epi8_mask(valid, chunk, _mm512_set1_epi8('\r'));
        const uint64_t backslashes = _mm512_mask_cmpeq_epi8_mask(valid, chunk, _mm512_set1_epi8('\\'));
        // The window's bytes around the chunk: a backslash, or a '\r' after one, right before it, and a '\n' right after.
        const uint64_t backslash_before = base > at && bytes[base - 1] == '\\';
        const uint64_t spliced_return_before = base > at + 1 && bytes[base - 1] == '\r' && bytes[base - 2] == '\\';
        const uint64_t newline_after = chunk_end < end && bytes[chunk_end] == '\n';
        const uint64_t after_backslash = (backslashes << 1) | backslash_before;
        const uint64_t spliced_returns = returns & after_backslash;
        const uint64_t spliced_newlines = newlines & (after_backslash | (spliced_returns << 1) | spliced_return_before);
        const uint64_t crlf_returns = returns & ((newlines >> 1) | (newline_after << (chunk_end - base - 1)));
        uint64_t ends = (newlines & ~spliced_newlines) | (returns & ~spliced_returns & ~crlf_returns);
        while (ends != 0 && before == UINT64_MAX)
        {
            const uint32_t bit = 63 - (uint32_t)__builtin_clzll(ends);
            before = last != UINT64_MAX ? base + bit : before;
            last = last == UINT64_MAX ? base + bit : last;
            ends &= ~(1ull << bit);
        }
        chunk_end = base;
    }
    const uint64_t tail = last != UINT64_MAX ? end - 1 - last : 0;
    return last == UINT64_MAX || (tail > longest_tail && before == UINT64_MAX) ? at : tail > longest_tail && last - before < tail ? last + 1 : end;
}

// The line ends in the 64-byte block at `base` of a file that lie in [begin, end), as pp_count_lines counts them but for a
// '\r' at the block's end, which a '\n' first in the next block makes no line end of its own.
BUSTER_INLINE uint32_t pp_count_block_lines(const PPFileData* data, uint64_t base, uint64_t begin, uint64_t end)
{
    const uint64_t available = data->length - base;
    const __mmask64 file = _bzhi_u64(~0ull, available < 64 ? (uint32_t)available : 64);
    const __m512i bytes = _mm512_maskz_loadu_epi8(file, data->bytes + base);
    const uint64_t newlines = _mm512_mask_cmpeq_epi8_mask(file, bytes, _mm512_set1_epi8('\n'));
    const uint64_t returns = _mm512_mask_cmpeq_epi8_mask(file, bytes, _mm512_set1_epi8('\r'));
    // The block's bytes in [begin, end).
    const uint32_t low = base < begin ? (uint32_t)(begin - base) : 0;
    const uint32_t high = end - base < 64 ? (uint32_t)(end - base) : 64;
    const uint64_t taken = _bzhi_u64(~0ull, high) & ~_bzhi_u64(~0ull, low);
    uint64_t ends = newlines;
    if (BUSTER_UNLIKELY(returns != 0))
    {
        // A '\r' ends a line unless a '\n' follows it, in the block or first in the next one.
        const uint64_t next_newline = available > 64 && data->bytes[base + 64] == '\n';
        ends |= returns & ~((newlines >> 1) | (next_newline << 63));
    }
    return (uint32_t)__builtin_popcountll(ends & taken);
}

// Adds the line ends in bytes [begin, end) of a file to `*lines`, as pp_count_block_lines counts them, and stores the
// count before each page boundary it reaches (BUSTER_PP_LINE_PAGE_BYTES) in `pages` (PPFileData.line_pages). Each
// window's kept bytes in turn, from the file's start, while they are in the caches (pp_prepare_windows). The whole
// 64-byte blocks up to each page's end go by their '\n's alone, a few instructions each, and again a block at a time
// (pp_count_block_lines) when one of them has a '\r'; so does a block two windows share, in two parts.
static void pp_count_page_lines(const PPFileData* data, uint64_t begin, uint64_t end, uint32_t* pages, uint32_t* lines)
{
    uint32_t count = *lines;
    const uint64_t whole_end = end & ~63ull;
    uint64_t base = begin & ~63ull;
    while (base < end)
    {
        const uint64_t page_end = (base / BUSTER_PP_LINE_PAGE_BYTES + 1) * BUSTER_PP_LINE_PAGE_BYTES;
        const uint64_t stop = base >= begin && base < whole_end ? (page_end < whole_end ? page_end : whole_end) : base;
        if (stop > base)
        {
            uint32_t newlines = 0;
            uint64_t returns = 0;
            for (uint64_t b = base; b < stop; b += 64)
            {
                const __m512i bytes = _mm512_loadu_si512(data->bytes + b);
                newlines += (uint32_t)__builtin_popcountll(_mm512_cmpeq_epi8_mask(bytes, _mm512_set1_epi8('\n')));
                returns |= _mm512_cmpeq_epi8_mask(bytes, _mm512_set1_epi8('\r'));
            }
            for (uint64_t b = base; BUSTER_UNLIKELY(returns != 0) && b < stop; b += 64)
            {
                count += pp_count_block_lines(data, b, b, b + 64);
            }
            count += returns == 0 ? newlines : 0;
            base = stop;
        }
        else
        {
            count += pp_count_block_lines(data, base, begin, end);
            base += 64;
        }
        if (base % BUSTER_PP_LINE_PAGE_BYTES == 0 && base <= end)
        {
            pages[base / BUSTER_PP_LINE_PAGE_BYTES] = count;
        }
    }
    *lines = count;
}

// pp_prepare_into for a file of at least two windows (`window` bytes) that the vectorized kernels lex: lexed a window at
// a time (c_tokenize_lines), each window's tokens kept up to the last one that starts a logical line (that line is lexed
// again with the next window, and a line that takes a whole window doubles it, unlexed when only splices continue it;
// a window that ends inside a long line is cut before it: pp_window_cut), and each completed block of
// BUSTER_REFINE_BLOCK_TOKENS refined at once (refine_tokens_blocks), while the lexer's columns and the source are still
// in the caches: a whole file lexed before its refinement had refinement read them from memory again
// (docs/c-refinement.md). The records are pp_prepare_whole's, byte for byte. The leading-space words, whose place after
// the kinds waits for the count, are refined into scratch memory and copied there. Each window's kept bytes have their
// line ends counted by page (pp_count_page_lines, PPFileData.line_pages) with the directive table's scan, also while they
// are in the caches; a file whose pages get no memory has none, and pp_line_of counts its lines from the cursor.
static PPPrepareStatus pp_prepare_windows(const PPPrepare* prepare, InternTable* table, Arena* columns, Arena* arena, PPFileData* data, uint64_t window,
                                          uint64_t* ticks)
{
    const uint64_t length = data->length;
    const uint64_t capacity = BUSTER_C_TOKENIZER_CAPACITY(length);
    data->kinds = arena_push(&columns[0], capacity, 64);
    data->starts = arena_push_array(&columns[1], capacity, sizeof(uint32_t), 64);
    data->values = arena_push_array(&columns[2], capacity, sizeof(uint32_t), 64);
    uint64_t tick = ticks != 0 ? __rdtsc() : 0;
    Scratch scratch = scratch_begin(0);
    uint64_t* backslash_chunks = arena_push_array(scratch.arena, refine_progress_words(length), sizeof(uint64_t), 64);
    // A word for every 64 of the most tokens the file can have; refinement writes those of the tokens it has. The
    // directive table's scan (pp_scan_directives_window) writes two, the lines' and the directives' '#' tokens, into the
    // two halves of `directive_words`: the directives' places need no room of their own, bounded only by the tokens, so
    // that for a file of up to about 12 MB the scratch memory touched stays in the huge page that pp_prepare_whole's
    // touches.
    const uint64_t word_room = capacity / 64 + 2;
    uint64_t* space = arena_push_array(scratch.arena, word_room, sizeof(uint64_t), 64);
    uint64_t* directive_words = arena_push_array(scratch.arena, 2 * word_room, sizeof(uint64_t), 64);
    RefineProgress progress;
    refine_progress_begin(&progress, backslash_chunks);
    // The line ends before each page; those before `lined` are counted (`lines`).
    uint32_t* pages = arena_push_array(arena, length / BUSTER_PP_LINE_PAGE_BYTES + 1, sizeof(uint32_t), 64);
    uint64_t lined = 0;
    uint32_t lines = 0;
    if (pages != 0)
    {
        pages[0] = 0;
    }
    const bool pushed = data->kinds != 0 && data->starts != 0 && data->values != 0;
    PPPrepareStatus result = !pushed                                                           ? PP_PREPARE_NO_COLUMNS
                             : backslash_chunks == 0 || space == 0 || directive_words == 0 ? PP_PREPARE_NO_MEMORY
                                                                                               : PP_PREPARE_DONE;
    // The next window starts at `at`, the start of the file or of a token that starts a logical line, and takes `size`
    // bytes; the tokens before it are kept (`count`), those before `refined` are refined, and those before `counted`
    // have had their UNKNOWN tokens counted (`unknowns`).
    uint64_t at = 0;
    uint64_t size = window;
    uint64_t count = 0;
    uint64_t refined = 0;
    uint64_t counted = 0;
    uint32_t unknowns = 0;
    // The named UCNs' candidates in the chunks of the backslash bitmap's words before `named_words`
    // (pp_scan_named_ucn_chunks).
    PPNamedUcnScan named = { 0 };
    uint64_t named_words = 0;
    uint32_t directives = 0;
    bool last = false;
    while (result == PP_PREPARE_DONE && !last)
    {
        const uint64_t end = length - at <= size ? length : at + size;
        last = end == length;
        const uint64_t cut = last ? end : pp_window_cut(data->bytes, at, end, BUSTER_PP_WINDOW_TAIL_BYTES);
        const int64_t lexed = cut > at ? c_tokenize_lines(data->bytes, cut, at, data->kinds + count, data->starts + count, data->values + count, prepare->refine_flags)
                                       : 0;
        int64_t kept = last ? lexed : 0;
        for (int64_t i = lexed - 1; !last && kept == 0 && i > 0; i -= 1)
        {
            kept = (data->kinds[count + i] & BUSTER_C_TOKEN_START_OF_LINE) != 0 ? i : 0;
        }
        const uint64_t next = kept > 0 && !last ? data->starts[count + kept] : end;
        if (lexed < 0)
        {
            result = PP_PREPARE_TOO_BIG;
        }
        else if (kept == 0 && !last)
        {
            size *= 2;
        }
        else
        {
            kept = pp_lex_for_mode(prepare, data->bytes, data->length, data->kinds + count, data->starts + count, data->values + count, (uint32_t)kept, true);
            result = kept < 0 ? PP_PREPARE_NO_MEMORY : PP_PREPARE_DONE;
            // An unterminated block comment (pp_is_unterminated_comment), reported when this inclusion reads it.
            if (last && kept > 0 && pp_is_unterminated_comment(data->bytes, data->kinds[count + kept - 1], data->starts[count + kept - 1]))
            {
                data->unterminated_comment = data->starts[count + kept - 1] + 1;
                data->unterminated_comment_starts_line = (data->kinds[count + kept - 1] & BUSTER_C_TOKEN_START_OF_LINE) != 0;
                kept -= 1;
            }
            count += kept > 0 ? (uint64_t)kept : 0;
            at = next;
            size = window;
        }
        if (ticks != 0)
        {
            const uint64_t now = __rdtsc();
            ticks[PP_PHASE_LEX] += now - tick;
            tick = now;
        }
        // The raw string errors' candidates (pp_record_raw_errors), counted while the window's kinds are in the caches.
        unknowns += result == PP_PREPARE_DONE ? pp_count_raw_candidates(prepare, data->kinds + counted, count - counted) : 0;
        counted = count;
        const uint64_t complete = last ? count : count - count % BUSTER_REFINE_BLOCK_TOKENS;
        const uint64_t begin = refined;
        if (result == PP_PREPARE_DONE && complete > refined)
        {
            result = refine_tokens_blocks(table, data->bytes, length, data->kinds, data->starts, data->values, space, prepare->refine_flags, refined, complete, &progress)
                         ? PP_PREPARE_DONE
                         : PP_PREPARE_NO_MEMORY;
            refined = complete;
            // The named UCNs' candidates in the chunks refinement has just marked, while their bytes are in the caches.
            pp_scan_named_ucn_chunks(data->bytes, length, backslash_chunks, named_words, progress.scanned, (prepare->refine_flags & C_TOKENIZER_TRIGRAPHS) != 0, &named);
            named_words = progress.scanned;
        }
        if (ticks != 0)
        {
            const uint64_t now = __rdtsc();
            ticks[PP_PHASE_REFINE] += now - tick;
            tick = now;
        }
        // The directive table's scan of the blocks just refined, while their kinds are in the caches, and the line ends of
        // the window's kept bytes, while they are.
        directives += result == PP_PREPARE_DONE ? pp_scan_directives_window(data->kinds, (uint32_t)begin, (uint32_t)refined, directive_words, directive_words + word_room) : 0;
        if (result == PP_PREPARE_DONE && pages != 0 && at > lined)
        {
            pp_count_page_lines(data, lined, at, pages, &lines);
            lined = at;
        }
        if (ticks != 0)
        {
            const uint64_t now = __rdtsc();
            ticks[PP_PHASE_DIRECTIVES] += now - tick;
            tick = now;
        }
    }
    if (result == PP_PREPARE_DONE)
    {
        // Each column keeps what the file has, with the lexer's slack, and the leading-space words go after the kinds, as
        // pp_prepare_whole places them.
        data->token_count = (uint32_t)count;
        const uint64_t kept = count + BUSTER_C_TOKENIZER_OUTPUT_SLACK;
        arena_pop_to(&columns[0], (uint64_t)(data->kinds - columns[0].base) + kept);
        arena_pop_to(&columns[1], (uint64_t)((uint8_t*)data->starts - columns[1].base) + kept * sizeof(uint32_t));
        arena_pop_to(&columns[2], (uint64_t)((uint8_t*)data->values - columns[2].base) + kept * sizeof(uint32_t));
        data->space = arena_push_array(&columns[0], count / 64 + 2, sizeof(uint64_t), 64);
        if (data->space != 0)
        {
            // The words past the last token's are zero: an earlier window's tokens may have been written there.
            const uint64_t words = (count + 63) / 64;
            memcpy(data->space, space, words * sizeof(uint64_t));
            memset(data->space + words, 0, (count / 64 + 2 - words) * sizeof(uint64_t));
        }
        result = data->space != 0 && pp_record_raw_errors(prepare, arena, data, unknowns) && pp_record_named_ucns(prepare, arena, data, &named) ? PP_PREPARE_DONE
                                                                                                                                         : PP_PREPARE_NO_MEMORY;
    }
    if (ticks != 0)
    {
        const uint64_t now = __rdtsc();
        ticks[PP_PHASE_REFINE] += now - tick;
        tick = now;
    }
    if (result == PP_PREPARE_DONE)
    {
        data->prepared = pp_directive_table_windowed(arena, data, directive_words, directive_words + word_room, directives);
        result = data->prepared ? PP_PREPARE_DONE : PP_PREPARE_NO_MEMORY;
        data->line_pages = pages;
    }
    scratch_end(scratch);
    if (ticks != 0)
    {
        ticks[PP_PHASE_DIRECTIVES] += __rdtsc() - tick;
    }
    return result;
}

// Lexes, refines and indexes a file's bytes, `data`: its tokens, values (its identifiers interned in `table`),
// leading-space bits, directive table and guard, in `columns` (kinds, starts and values, an arena each) and `arena` (the
// directive table). Adds each phase's TSC ticks to `ticks` (by PPPhase) unless it is null. A file of at least two windows
// (BUSTER_PP_WINDOW_BYTES, or the options' size) that the vectorized kernels lex is lexed and refined a window at a time
// (pp_prepare_windows), into the same records.
static PPPrepareStatus pp_prepare_into(const PPPrepare* prepare, InternTable* table, Arena* columns, Arena* arena, PPFileData* data, uint64_t* ticks)
{
    const uint64_t window = prepare->window_bytes != 0 ? prepare->window_bytes : BUSTER_PP_WINDOW_BYTES;
    const bool windows = prepare->vectorized && !prepare->dollar_punctuation && data->length >= 2 * window &&
                         data->length <= UINT32_MAX - BUSTER_C_TOKENIZER_OUTPUT_SLACK && !c_tokenize_scalar_only(data->bytes, data->length, prepare->refine_flags);
    return windows ? pp_prepare_windows(prepare, table, columns, arena, data, window, ticks) : pp_prepare_whole(prepare, table, columns, arena, data, ticks);
}

// The line ends in bytes [begin, end) of a file: "\n", "\r\n" (at its '\n', so a '\r' at end - 1 that a '\n' follows
// in the file isn't one) and a '\r' that no '\n' follows, 64 bytes at a time with popcounts.
static uint32_t pp_count_lines(const PPFileData* data, uint32_t begin, uint32_t end)
{
#if !BUSTER_HOST_VECTOR
    uint32_t result = 0;
    for (uint32_t i = begin; i < end; i += 1)
    {
        result += data->bytes[i] == '\n' || (data->bytes[i] == '\r' && (i + 1 == data->length || data->bytes[i + 1] != '\n'));
    }
    return result;
#else
    uint32_t result = 0;
    for (uint32_t i = begin; i < end; i += 64)
    {
        const uint32_t remaining = end - i;
        const uint32_t loaded = remaining < 64 ? remaining : 64;
        const __mmask64 valid = _bzhi_u64(~0ull, loaded);
        const __m512i bytes = _mm512_maskz_loadu_epi8(valid, data->bytes + i);
        const uint64_t newlines = _mm512_cmpeq_epi8_mask(bytes, _mm512_set1_epi8('\n'));
        const uint64_t returns = _mm512_cmpeq_epi8_mask(bytes, _mm512_set1_epi8('\r'));
        if (BUSTER_UNLIKELY(returns != 0))
        {
            // The file's byte after the loaded ones, in the next block or past the counted bytes' end: a '\r' before a
            // '\n' ends a line at the '\n', which only a range that holds the '\n' counts.
            const uint64_t next_newline = i + loaded < data->length && data->bytes[i + loaded] == '\n';
            const uint64_t followed = (newlines >> 1) | (next_newline << (loaded - 1));
            result += (uint32_t)__builtin_popcountll(returns & ~followed);
        }
        // The masked load zeroed the bytes past the end.
        result += (uint32_t)__builtin_popcountll(newlines);
    }
    return result;
#endif
}

static void pp_diagnostic_lines(Preprocessor* pp, uint32_t range)
{
    PPFileData* data = &pp->datas[pp->files[pp->ranges[range].file].data];
    if (data->diagnostic_lines == 0)
    {
        const uint32_t count = pp_count_lines(data, 0, data->length) + 1;
        uint32_t* lines = arena_push_array(&pp->arena, count, sizeof(uint32_t), 4);
        if (lines != 0)
        {
            uint32_t used = 1;
            lines[0] = 0;
            for (uint32_t i = 0; i < data->length; i += BUSTER_VECTOR_PATH(pp->vectorized) ? 64 : 1)
            {
                if (BUSTER_VECTOR_PATH(pp->vectorized))
                {
                    const uint32_t remaining = data->length - i;
                    const __mmask64 valid = _bzhi_u64(~0ull, remaining < 64 ? remaining : 64);
                    const __m512i bytes = _mm512_maskz_loadu_epi8(valid, data->bytes + i);
                    const uint64_t nl = _mm512_cmpeq_epi8_mask(bytes, _mm512_set1_epi8('\n'));
                    const uint64_t cr = _mm512_cmpeq_epi8_mask(bytes, _mm512_set1_epi8('\r'));
                    const uint64_t next = remaining > 64 && data->bytes[i + 64] == '\n';
                    for (uint64_t bits = (nl | (cr & ~((nl >> 1) | (next << 63)))) & valid; bits != 0; bits &= bits - 1)
                    {
                        lines[used++] = i + (uint32_t)__builtin_ctzll(bits) + 1;
                    }
                }
                else if (data->bytes[i] == '\n' || (data->bytes[i] == '\r' && (i + 1 == data->length || data->bytes[i + 1] != '\n')))
                {
                    lines[used++] = i + 1;
                }
            }
            data->diagnostic_lines = lines;
            data->diagnostic_line_count = used;
        }
        // Allocation failure keeps the existing cursor-based location lookup available.
    }
}

// The line of byte `offset` of a file with line pages (PPFileData.line_pages): its page's count, then the line ends from
// the page's start, which are in the caches no more than the cursor's place is.
static uint32_t pp_page_line(const PPFileData* data, uint32_t offset)
{
    const uint32_t page = offset / BUSTER_PP_LINE_PAGE_BYTES;
    return data->line_pages[page] + 1 + pp_count_lines(data, page * BUSTER_PP_LINE_PAGE_BYTES, offset);
}

// The physical line and column of `location` in its range's file: lines counted from the file's cursor (or from its
// start, for a place before the cursor; or for a file with line pages, from the place's page's start unless the cursor
// is on that page before it), and the column from the line's start before it, unless `column` is null.
static uint32_t pp_line_of(Preprocessor* pp, uint32_t range, uint32_t location, uint32_t* column)
{
    uint32_t result = 1;
    uint32_t found = 1;
    const PPRange* record = &pp->ranges[range];
    if (record->file != BUSTER_PP_NONE)
    {
        PPFileData* data = &pp->datas[pp->files[record->file].data];
        const uint32_t offset = location - record->base < data->length ? location - record->base : data->length;
        if (data->diagnostic_lines != 0)
        {
            uint32_t low = 0;
            uint32_t high = data->diagnostic_line_count;
            while (high - low > 1)
            {
                const uint32_t middle = low + (high - low) / 2;
                if (data->diagnostic_lines[middle] <= offset)
                {
                    low = middle;
                }
                else
                {
                    high = middle;
                }
            }
            result = low + 1;
            found = offset - data->diagnostic_lines[low] + 1;
        }
        else
        {
            const uint32_t page_start = offset / BUSTER_PP_LINE_PAGE_BYTES * BUSTER_PP_LINE_PAGE_BYTES;
            const bool cursor = data->line_cursor_line != 0 && offset >= data->line_cursor && (data->line_pages == 0 || data->line_cursor >= page_start);
            if (!cursor && data->line_pages == 0)
            {
                data->line_cursor = 0;
                data->line_cursor_line = 1;
            }
            result = cursor || data->line_pages == 0 ? data->line_cursor_line + pp_count_lines(data, data->line_cursor, offset) : pp_page_line(data, offset);
            data->line_cursor = offset;
            data->line_cursor_line = result;
            // Back to the line's start: past a line end, as pp_count_lines counts them (a "\r\n" at its '\n').
            uint32_t line_start = offset;
            while (column != 0 && line_start > 0 && data->bytes[line_start - 1] != '\n' &&
                   (data->bytes[line_start - 1] != '\r' || (line_start < data->length && data->bytes[line_start] == '\n')))
            {
                line_start -= 1;
            }
            found = offset - line_start + 1;
        }
    }
    if (column != 0)
    {
        *column = found;
    }
    return result;
}

// The file cache's limits: paths (and their bytes) and records, and a lane's arenas. Its slot tables start small and
// double when they are half full, as a translation unit's path table does (pp_grow_file_slots), so that the pages they
// touch are about the ones they need.
#define BUSTER_FILE_CACHE_MAX_PATHS (1u << 22)
#define BUSTER_FILE_CACHE_PATH_BYTES (1ull << 32)
#define BUSTER_FILE_CACHE_INITIAL_SLOTS 1024u
#define BUSTER_FILE_CACHE_ARENA_BYTES (64ull << 30)
// The base pages that a lane's records arena and its bytes arena start with (file_cache_arenas): a unit fills a few
// hundred kilobytes of each (sqlite3.c's 268 and 376 KB), where each one's first huge page took 2 MB (#672).
#define BUSTER_FILE_CACHE_SMALL_BYTES (2ull << 20)
// The largest file a translation unit's lookup reads into its lane's bytes arena rather than maps
// (file_cache_lookup_path), as c_files.c's lanes read theirs (BUSTER_FILES_READ_LIMIT): a read saves the file's mmap
// and, when the cache is released, its munmap. On the 9700X, reading the files of up to 16 KB took 1.9% to 2.3% off the
// run and release of sqlite3.c, main.c and musl's units. 4 KB took 0.2% more off musl's and 0.7% to 0.8% less off the
// others', larger limits less, and reading every file (sqlite3.c's 9.5 MB too) made sqlite3.c's slower than mapping
// them all (docs/c-preprocessor.md, "Files").
#define BUSTER_FILE_CACHE_READ_LIMIT (16u << 10)
// The failed probes of paths in a directory after which the cache first looks at its names (FileCacheDirectory), the
// buffer of that look (about 25 names: a directory with more is put off), the factor of failed probes after which a
// larger directory is read whole, the largest getdents64 record (its 19 bytes and a name of NAME_MAX bytes and its NUL,
// aligned to 8), and the buffer a whole read reads with (about 200 names a call). A unit with only <stdio.h> fails 26
// probes in each of clang's resource directory and /usr/local/include. On the 9700X, its include searches took 60 us
// with first looks at 32 misses or none, and 64 us at 16; deflate.c's 159, 154 and 158 us, and sqlite3.c's 582, 576
// and 601 us. Listing every directory whole at 16 made the first 77 us and sqlite3.c's 540 (file_cache_list_directory).
#define BUSTER_FILE_CACHE_LISTING_MISSES 32u
#define BUSTER_FILE_CACHE_SMALL_LISTING 1024u
#define BUSTER_FILE_CACHE_LARGE_LISTING 8u
#define BUSTER_FILE_CACHE_RECORD_MAX 280u
#define BUSTER_FILE_CACHE_LISTING_BUFFER (8u << 10)
// The longest name a directory entry has (NAME_MAX on every file system file_cache_exact_names takes).
#define BUSTER_FILE_CACHE_NAME_MAX 255u

// The capacity shift of a cache's arrays (arena_capacity_shift), for the larger of its records and paths.
static uint32_t file_cache_shift(void)
{
    const uint64_t records = (uint64_t)BUSTER_PP_MAX_FILES * sizeof(PPFileData);
    const uint64_t paths = (uint64_t)BUSTER_FILE_CACHE_MAX_PATHS * sizeof(FileCachePath);
    return arena_capacity_shift(records > paths ? records : paths);
}

// The entries and entry bytes of a cache's four arrays at capacity shift `shift`, in file_cache_start's order: the
// records, their identity slots, the paths and their slots.
static void file_cache_array_shape(uint32_t shift, uint64_t counts[4], uint64_t sizes[4])
{
    counts[0] = BUSTER_PP_MAX_FILES >> shift;
    counts[1] = 2ull * (BUSTER_PP_MAX_FILES >> shift);
    counts[2] = BUSTER_FILE_CACHE_MAX_PATHS >> shift;
    counts[3] = 2ull * (BUSTER_FILE_CACHE_MAX_PATHS >> shift);
    sizes[0] = sizeof(PPFileData);
    sizes[1] = sizeof(uint32_t);
    sizes[2] = sizeof(FileCachePath);
    sizes[3] = sizeof(uint32_t);
}

// Starts a cache whose bytes are all zero, with its four arrays at `arrays` (file_cache_array_shape's, at `shift`, zero
// too, or a null among them if they couldn't be reserved), and reserves its paths' bytes. The arrays are the caller's to
// release: file_cache_initialize's arena of tables, or a unit's tables (pp_initialize_once, #652), which hold the cache
// itself too and so need neither its 120 KB written zero nor an arena of their own.
static bool file_cache_start(FileCache* cache, InternTable* table, PPPrepare prepare, uint32_t shift, void* const arrays[4])
{
    cache->prepare = prepare;
    cache->table = table;
    cache->read_limit = BUSTER_FILE_CACHE_READ_LIMIT;
    cache->listing_misses = BUSTER_FILE_CACHE_LISTING_MISSES;
    cache->capacity_shift = shift;
    const bool reserved = arrays[0] != 0 && arrays[1] != 0 && arrays[2] != 0 && arrays[3] != 0;
    cache->path_arena = reserved ? arena_reserve(BUSTER_FILE_CACHE_PATH_BYTES) : (Arena){ 0 };
    cache->records = arrays[0];
    cache->identity_slots = arrays[1];
    cache->paths = arrays[2];
    cache->path_slots = arrays[3];
    cache->identity_slot_mask = BUSTER_FILE_CACHE_INITIAL_SLOTS - 1;
    cache->path_slot_mask = BUSTER_FILE_CACHE_INITIAL_SLOTS - 1;
    return reserved && cache->path_arena.base != 0;
}

// Reserves a cache's memory, for files prepared as `prepare` says and refined with `table`. Returns false if it can't be
// reserved (file_cache_release then releases what was).
static __attribute__((unused)) bool file_cache_initialize(FileCache* cache, InternTable* table, PPPrepare prepare)
{
    *cache = (FileCache){ 0 };
    const uint32_t shift = file_cache_shift();
    uint64_t counts[4];
    uint64_t sizes[4];
    file_cache_array_shape(shift, counts, sizes);
    // One arena of tables (#1521), each in the pages an arena of its own would have: it commits them whole without
    // charging their 568 MiB, of which a unit's files touch a few pages, and takes one mapping and one commit for four.
    ArenaTable tables[4];
    void* arrays[4];
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(tables); i += 1)
    {
        tables[i] = (ArenaTable){
            .bytes = counts[i] * sizes[i] + 64,
            .small = BUSTER_ARENA_SMALL_BYTES,
        };
    }
    cache->arrays = arena_reserve_tables(tables, BUSTER_ARRAY_LENGTH(tables));
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(tables); i += 1)
    {
        arrays[i] = tables[i].base;
    }
    return file_cache_start(cache, table, prepare, shift, arrays);
}

// Unmaps the records' files, and the files of paths whose probe mapped one that no record took, and releases the
// cache's memory.
static __attribute__((unused)) void file_cache_release(FileCache* cache)
{
    for (uint32_t r = 0; r < cache->record_count; r += 1)
    {
        if (cache->records[r].mapped)
        {
            munmap((void*)cache->records[r].bytes, cache->records[r].length);
        }
    }
    for (uint32_t p = 0; p < cache->path_count; p += 1)
    {
        if (cache->paths[p].probe.mapped)
        {
            munmap((void*)cache->paths[p].probe.bytes, cache->paths[p].probe.length);
        }
    }
    for (uint32_t word = 0; word < BUSTER_ARRAY_LENGTH(cache->lanes_reserved); word += 1)
    {
        for (uint64_t lanes = cache->lanes_reserved[word]; lanes != 0; lanes &= lanes - 1)
        {
            FileCacheArenas* arenas = &cache->lanes[word * 64 + (uint32_t)__builtin_ctzll(lanes)];
            for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(arenas->columns); i += 1)
            {
                arena_release(&arenas->columns[i]);
            }
            arena_release(&arenas->arena);
            arena_release(&arenas->bytes);
        }
    }
    arena_release(&cache->arrays);
    arena_release(&cache->path_arena);
    // Every lane's arenas are zero now, released or never reserved: the rest of the cache is cleared without touching
    // their pages, of which a unit's cache, in its tables, has written only the reserved lanes'.
    memset(cache, 0, offsetof(FileCache, lanes));
}

// The arenas lane `lane` prepares records into, reserved the first time. Reserving takes the process's memory map lock,
// which the lanes of a group are better off taking one after another: c_files.c reserves them before its lanes start.
static FileCacheArenas* file_cache_arenas(FileCache* cache, uint32_t lane)
{
    FileCacheArenas* result = &cache->lanes[lane];
    if (result->arena.base == 0)
    {
        // Atomic, as two lanes whose first reservations failed may try again at once (c_files.c reserves them before its
        // lanes start, one lane after another), each on its own lane's bit of a shared word.
        __atomic_fetch_or(&cache->lanes_reserved[lane / 64], 1ull << (lane % 64), __ATOMIC_RELAXED);
        // Reserved in turn until one can't be, and then none of them, so that a lane short of address space fails to
        // prepare, as a reservation that fails stops a run, rather than going on without the arena it lacked.
        Arena* arenas[] = { &result->columns[0], &result->columns[1], &result->columns[2], &result->arena, &result->bytes };
        bool reserved = true;
        for (uint32_t i = 0; reserved && i < BUSTER_ARRAY_LENGTH(arenas); i += 1)
        {
            // The token columns are the first three. The records' arena and the bytes arena, the last two, start with
            // BUSTER_FILE_CACHE_SMALL_BYTES of base pages.
            const bool small = lane != 0 && cache->lane_small_bytes != 0 && i < 3;
            const uint64_t base_pages = i >= 3 ? BUSTER_FILE_CACHE_SMALL_BYTES : small ? cache->lane_small_bytes : BUSTER_ARENA_SMALL_BYTES;
            *arenas[i] = arena_reserve_small(BUSTER_FILE_CACHE_ARENA_BYTES, base_pages);
            reserved = arenas[i]->base != 0;
        }
        for (uint32_t i = 0; !reserved && i < BUSTER_ARRAY_LENGTH(arenas); i += 1)
        {
            arena_release(arenas[i]);
        }
    }
    return result;
}

// The hash of a file's identity, for the identity table: wyhash's mixing of the device and the inode.
static uint64_t file_cache_identity_hash(uint64_t device, uint64_t inode)
{
    uint64_t x = (device * BUSTER_INTERN_SECRET0) ^ inode;
    x = (x ^ (x >> 32)) * BUSTER_INTERN_SECRET1;
    return x ^ (x >> 29);
}

// The record of the file with this identity, or BUSTER_PP_NONE.
static uint32_t file_cache_find_identity(const FileCache* cache, uint64_t device, uint64_t inode)
{
    uint32_t slot = (uint32_t)file_cache_identity_hash(device, inode) & cache->identity_slot_mask;
    uint32_t result = BUSTER_PP_NONE;
    bool done = false;
    while (!done)
    {
        const uint32_t entry = cache->identity_slots[slot];
        done = entry == 0 || (cache->records[entry - 1].device == device && cache->records[entry - 1].inode == inode);
        result = entry != 0 && done ? entry - 1 : result;
        slot = (slot + 1) & cache->identity_slot_mask;
    }
    return result;
}

// Places record `record` in the identity table, which has room for it.
static void file_cache_place_identity(FileCache* cache, uint32_t record)
{
    uint32_t slot = (uint32_t)file_cache_identity_hash(cache->records[record].device, cache->records[record].inode) & cache->identity_slot_mask;
    while (cache->identity_slots[slot] != 0)
    {
        slot = (slot + 1) & cache->identity_slot_mask;
    }
    cache->identity_slots[slot] = record + 1;
}

// A new record for the file a probe mapped (whose identity no record has): the cache now owns its bytes. Returns the
// record's index, or BUSTER_PP_NONE (having unmapped the bytes) if the cache has as many records as it can hold.
static uint32_t file_cache_add_record(FileCache* cache, const FileProbe* probe)
{
    uint32_t result = BUSTER_PP_NONE;
    if (cache->record_count < (BUSTER_PP_MAX_FILES >> cache->capacity_shift))
    {
        result = cache->record_count;
        cache->records[result] = (PPFileData){
            .device = probe->device,
            .inode = probe->inode,
            .modified = probe->modified,
            .has_modified = true,
            .bytes = probe->bytes,
            .length = probe->length,
            .mapped = probe->mapped,
            .record = result,
        };
        cache->record_count += 1;
        // The table is never more than half full, and can grow to twice as large as the most records there can be.
        if (2 * cache->record_count > cache->identity_slot_mask + 1)
        {
            const uint32_t slots = 2 * (cache->identity_slot_mask + 1);
            memset(cache->identity_slots, 0, slots * sizeof(uint32_t));
            cache->identity_slot_mask = slots - 1;
            for (uint32_t r = 0; r < cache->record_count; r += 1)
            {
                file_cache_place_identity(cache, r);
            }
        }
        else
        {
            file_cache_place_identity(cache, result);
        }
    }
    else if (probe->mapped)
    {
        munmap((void*)probe->bytes, probe->length);
    }
    return result;
}

// The entry of a path (`length` bytes) with `hash` (intern_hash_bytes's), or 0 if the cache never looked it up. Lanes may
// find paths at once while no lane adds one.
static FileCachePath* file_cache_find_path(FileCache* cache, const char* path, uint32_t length, uint64_t hash)
{
    uint32_t slot = (uint32_t)hash & cache->path_slot_mask;
    FileCachePath* result = 0;
    bool done = false;
    while (!done)
    {
        const uint32_t entry = cache->path_slots[slot];
        FileCachePath* candidate = entry != 0 ? &cache->paths[entry - 1] : 0;
        done = entry == 0 || (candidate->hash == hash && candidate->length == length && memcmp(candidate->path, path, length) == 0);
        result = entry != 0 && done ? candidate : result;
        slot = (slot + 1) & cache->path_slot_mask;
    }
    return result;
}

// Probes the file system for a regular file at `path` (NUL-terminated): opens it and reads its identity, and unless a
// record already has that identity, gets its bytes as `how` says. A mapping is read-only: the page cache's pages, which
// costs neither a copy nor clearing new pages, but each mapping holds the process's memory map lock, on which lanes that
// map at once queue. A file of up to `read_limit` bytes, or any with FILE_CACHE_READ, is read into `bytes_arena`
// instead (unless it is null), followed by 64 zero bytes. A file too big for 32-bit positions, or that can't be mapped
// or read whole, is none. An open that fails for another reason than there being no file there (no such file, a part
// of the path that isn't a directory, or a directory, which clang's header search takes as none there) gives the probe
// its error, which the include search reports (pp_find_include). The probe reads the identity table only, so lanes
// probe at once while no lane adds records; the system calls go to `counts`.
static void file_cache_probe(const FileCache* cache, const char* path, FileCacheBytes how, uint32_t read_limit, Arena* bytes_arena, FileProbe* probe,
                             FileCacheCounts* counts)
{
    static const uint8_t empty[1] = { 0 };
    *probe = (FileProbe){ .record = BUSTER_PP_NONE };
    // Without blocking: opening a FIFO waits for a writer otherwise, also for the walk's speculative probes of includes
    // no unit reaches. A FIFO, a device or a directory then isn't a regular file, and is none. O_NONBLOCK changes
    // nothing for a regular file, and O_NOCTTY keeps a terminal from becoming the process's controlling one.
    // A primary source is not one of those probes: pp_run_input reads a non-regular input to EOF, as `-` does.
    const int fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK | O_NOCTTY);
    int error = fd >= 0 ? 0 : errno;
    counts->opens += 1;
    struct stat stats;
    const bool stated = fd >= 0 && fstat(fd, &stats) == 0;
    error = fd >= 0 && !stated ? errno : error;
    counts->fstats += fd >= 0;
    error = stated && !S_ISREG(stats.st_mode) ? (S_ISDIR(stats.st_mode) ? EISDIR : -1) : error;
    error = stated && S_ISREG(stats.st_mode) && (uint64_t)stats.st_size > UINT32_MAX - BUSTER_C_TOKENIZER_OUTPUT_SLACK ? EFBIG : error;
    if (stated && S_ISREG(stats.st_mode) && (uint64_t)stats.st_size <= UINT32_MAX - BUSTER_C_TOKENIZER_OUTPUT_SLACK)
    {
        probe->device = (uint64_t)stats.st_dev;
        probe->inode = (uint64_t)stats.st_ino;
        probe->modified = (int64_t)stats.st_mtime;
        probe->length = (uint32_t)stats.st_size;
        probe->record = file_cache_find_identity(cache, probe->device, probe->inode);
        const uint8_t* bytes = empty;
        const bool read = bytes_arena != 0 && (how == FILE_CACHE_READ || probe->length <= read_limit);
        if (probe->record == BUSTER_PP_NONE && probe->length != 0 && !read)
        {
            void* mapped = mmap(0, probe->length, PROT_READ, MAP_PRIVATE | (how == FILE_CACHE_MAP_POPULATED ? MAP_POPULATE : 0), fd, 0);
            error = mapped != MAP_FAILED ? 0 : errno;
            bytes = mapped != MAP_FAILED ? mapped : 0;
            probe->mapped = bytes != 0;
            counts->maps += 1;
        }
        else if (probe->record == BUSTER_PP_NONE && probe->length != 0)
        {
            uint8_t* buffer = arena_push(bytes_arena, (uint64_t)probe->length + 64, 64);
            uint64_t done = 0;
            int64_t got = 1;
            while (buffer != 0 && done < probe->length && got > 0)
            {
                got = pread(fd, buffer + done, probe->length - done, (off_t)done);
                done += got > 0 ? (uint64_t)got : 0;
            }
            error = buffer == 0 ? ENOMEM : done == probe->length ? 0 : got < 0 ? errno : EIO;
            if (buffer != 0 && done == probe->length)
            {
                memset(buffer + done, 0, 64);
            }
            bytes = buffer != 0 && done == probe->length ? buffer : 0;
            counts->reads += 1;
        }
        probe->bytes = bytes;
        probe->found = bytes != 0;
    }
    probe->error = probe->found ? 0 : error;
    if (fd >= 0)
    {
        close(fd);
    }
}

// Adds a path the cache never looked up, with `hash` (intern_hash_bytes's) and what its probe found: its record is
// BUSTER_FILE_CACHE_PENDING until file_cache_identify gives it one, or BUSTER_PP_NONE if no file can be read there.
// Returns 0, leaving the probe's mapping to the caller, if the cache has as many paths as it can hold.
static FileCachePath* file_cache_add_path(FileCache* cache, const char* path, uint32_t length, uint64_t hash, const FileProbe* probe)
{
    FileCachePath* result = 0;
    char* copy = cache->path_count < (BUSTER_FILE_CACHE_MAX_PATHS >> cache->capacity_shift) ? arena_push(&cache->path_arena, (uint64_t)length + 1, 1) : 0;
    if (copy != 0)
    {
        memcpy(copy, path, length);
        copy[length] = 0;
        const uint32_t index = cache->path_count;
        cache->paths[index] = (FileCachePath){
            .hash = hash,
            .path = copy,
            .length = length,
            .record = probe->found ? BUSTER_FILE_CACHE_PENDING : BUSTER_PP_NONE,
            .probe = *probe,
        };
        cache->path_count += 1;
        // The table is never more than half full, and can grow to twice as large as the most paths there can be. When it
        // grows, every path is placed again.
        const bool grows = 2 * cache->path_count > cache->path_slot_mask + 1;
        if (grows)
        {
            const uint32_t slots = 2 * (cache->path_slot_mask + 1);
            memset(cache->path_slots, 0, slots * sizeof(uint32_t));
            cache->path_slot_mask = slots - 1;
        }
        for (uint32_t p = grows ? 0 : index; p <= index; p += 1)
        {
            uint32_t slot = (uint32_t)cache->paths[p].hash & cache->path_slot_mask;
            while (cache->path_slots[slot] != 0)
            {
                slot = (slot + 1) & cache->path_slot_mask;
            }
            cache->path_slots[slot] = p + 1;
        }
        result = &cache->paths[index];
    }
    return result;
}

// Gives a path whose probe found a file its record: the record that has the file's identity (unmapping the probe's
// bytes, if it mapped them, since another path's probe mapped the same file first), or a new one.
static void file_cache_identify(FileCache* cache, FileCachePath* entry)
{
    if (entry->record == BUSTER_FILE_CACHE_PENDING)
    {
        const FileProbe* probe = &entry->probe;
        uint32_t record = probe->record != BUSTER_PP_NONE ? probe->record : file_cache_find_identity(cache, probe->device, probe->inode);
        if (record != BUSTER_PP_NONE && probe->mapped)
        {
            munmap((void*)probe->bytes, probe->length);
        }
        else if (record == BUSTER_PP_NONE)
        {
            record = file_cache_add_record(cache, probe);
        }
        entry->record = record;
        entry->probe.mapped = false;
    }
}

// Whether a lookup of a name on a file system of type `type` (fstatfs's f_type) finds what a listing of its directory
// finds: the name byte for byte, or with ASCII letters' case folded, as file_cache_name_key folds them. Case folding
// covers casefold directories (ext4, F2FS, tmpfs and bcachefs) and XFS's ascii-ci. Names with a byte past ASCII are
// neither listed nor looked up (file_cache_list_directory, file_cache_listed_absent), so Unicode's foldings and
// normalizations never apply to them. Some file systems alias names that their listings don't show (vfat's and NTFS's
// short names and trailing dots), and network and FUSE file systems answer as their servers do. None of those is here:
// their directories are probed as before.
static bool file_cache_exact_names(uint64_t type)
{
    static const uint64_t types[] = {
        0xEF53u,     // ext2, ext3 and ext4
        0x58465342u, // XFS
        0x9123683Eu, // Btrfs
        0x01021994u, // tmpfs
        0x794C7630u, // overlayfs
        0xF2F52010u, // F2FS
        0x73717368u, // SquashFS
        0xE0F5E1E2u, // EROFS
        0xCA451A4Eu, // bcachefs
    };
    bool result = false;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(types); i += 1)
    {
        result = result || type == types[i];
    }
    return result;
}

// The key of a name of `length` bytes (1 to BUSTER_FILE_CACHE_NAME_MAX, all ASCII) in a listing: 32 bits of the hash of
// the name with its ASCII letters in lower case, never 0, which marks an empty slot. Two names with one key only cost
// a probe: a listing rules out a name whose key it doesn't have.
static uint32_t file_cache_name_key(const uint8_t* name, uint32_t length)
{
    uint8_t folded[BUSTER_FILE_CACHE_NAME_MAX];
    for (uint32_t i = 0; i < length; i += 1)
    {
        const uint8_t c = name[i];
        folded[i] = c >= 'A' && c <= 'Z' ? (uint8_t)(c + ('a' - 'A')) : c;
    }
    const uint64_t hash = intern_hash_bytes(folded, length);
    const uint32_t key = (uint32_t)(hash ^ (hash >> 32));
    return key != 0 ? key : 1;
}

// The directory of the first `length` bytes of `path`, with `hash` (intern_hash_bytes's), or null if the cache never
// looked a path up in it. Lanes may find directories at once while no lane adds one.
static FileCacheDirectory* file_cache_find_directory(const FileCache* cache, const char* path, uint32_t length, uint64_t hash)
{
    FileCacheDirectory* result = cache->directory_slots[hash & (BUSTER_FILE_CACHE_DIRECTORY_SLOTS - 1)];
    while (result != 0 && !(result->hash == hash && result->length == length && memcmp(result->path, path, length) == 0))
    {
        result = result->next;
    }
    return result;
}

// The directory of the first `length` bytes of `path`, added (with no misses) if the cache never looked a path up in
// it; null if the cache can't hold it. One lane at a time.
static FileCacheDirectory* file_cache_directory(FileCache* cache, const char* path, uint32_t length)
{
    const uint64_t hash = intern_hash_bytes((const uint8_t*)path, length);
    FileCacheDirectory* result = file_cache_find_directory(cache, path, length, hash);
    if (result == 0)
    {
        FileCacheDirectory** slot = &cache->directory_slots[hash & (BUSTER_FILE_CACHE_DIRECTORY_SLOTS - 1)];
        result = arena_push(&cache->path_arena, sizeof(FileCacheDirectory), _Alignof(FileCacheDirectory));
        char* copy = result != 0 ? arena_push(&cache->path_arena, (uint64_t)length + 1, 1) : 0;
        if (copy != 0)
        {
            memcpy(copy, path, length);
            copy[length] = 0;
            *result = (FileCacheDirectory){
                .hash = hash,
                .path = copy,
                .length = length,
                .next = *slot,
            };
            *slot = result;
        }
        result = copy != 0 ? result : 0;
    }
    return result;
}

// Whether the first `directory_length` bytes of `path` (`length` bytes) are a directory a path is looked up in: a
// '/' follows them, and the path has more after it. BUSTER_PP_NONE is none.
static bool file_cache_in_directory(const char* path, uint32_t length, uint32_t directory_length)
{
    return directory_length < length && path[directory_length] == '/';
}

// The table of a listing's names, grown to `*slots` (twice as many as `count` + 1 names at least, and at least 64) in the
// cache's path arena, with the names of `names` (`*slots` before) placed again; null if the arena can't hold it.
static uint32_t* file_cache_grow_names(FileCache* cache, uint32_t* names, uint32_t count, uint32_t* slots)
{
    const uint32_t old = *slots;
    uint32_t grown = old != 0 ? old : 64;
    while ((uint64_t)grown < 2 * ((uint64_t)count + 1))
    {
        grown *= 2;
    }
    uint32_t* result = grown != old ? arena_push_array(&cache->path_arena, grown, sizeof(uint32_t), 64) : names;
    if (result != 0 && grown != old)
    {
        memset(result, 0, (uint64_t)grown * sizeof(uint32_t));
        for (uint32_t i = 0; i < old; i += 1)
        {
            uint32_t slot = names[i] & (grown - 1);
            while (names[i] != 0 && result[slot] != 0)
            {
                slot = (slot + 1) & (grown - 1);
            }
            result[slot] = names[i] != 0 ? names[i] : result[slot];
        }
        *slots = grown;
    }
    return result;
}

// The generic libc syscall entry does not run MSan's getdents64 post-write hook. Only the returned record span is
// initialized by this boundary: do not clear or unpoison the requested capacity, EOF/error output, or an unread tail.
static long file_cache_read_directory(int fd, uint8_t* buffer, uint32_t capacity)
{
    const long got = syscall(SYS_getdents64, fd, buffer, capacity);
#if __has_feature(memory_sanitizer)
    if (got > 0 && (uint64_t)got <= capacity)
    {
        __msan_unpoison(buffer, (size_t)got);
    }
#endif
    return got;
}

// Adds the names of `got` bytes of getdents64's records to a listing's table (file_cache_grow_names), `*count` of them
// so far in `*slots`. False for an incomplete/unterminated record, a name past ASCII, or a table that can't grow.
static bool file_cache_list_names(FileCache* cache, const uint8_t* buffer, long got, uint32_t** names, uint32_t* slots, uint32_t* count)
{
    bool result = got >= 0;
    // Each record (struct linux_dirent64): an 8-byte inode, an 8-byte offset, its 2-byte length, a type byte, and the
    // NUL-terminated name.
    for (long offset = 0; result && offset < got;)
    {
        const long remaining = got - offset;
        uint16_t record = 0;
        if (remaining >= 20)
        {
            memcpy(&record, buffer + offset + 16, sizeof(record));
        }
        const bool whole = record > 19 && record <= remaining;
        const uint8_t* name = whole ? buffer + offset + 19 : NULL;
        const uint32_t capacity = whole ? (uint32_t)record - 19 : 0;
        const uint32_t length = whole ? (uint32_t)strnlen((const char*)name, capacity) : 0;
        result = length != 0 && length < capacity && length <= BUSTER_FILE_CACHE_NAME_MAX;
        for (uint32_t i = 0; result && i < length; i += 1)
        {
            result = name[i] < 0x80;
        }
        *names = result && 2 * ((uint64_t)*count + 1) > *slots ? file_cache_grow_names(cache, *names, *count, slots) : *names;
        result = result && *names != 0;
        if (result)
        {
            uint32_t* table = *names;
            const uint32_t key = file_cache_name_key(name, length);
            uint32_t slot = key & (*slots - 1);
            while (table[slot] != 0 && table[slot] != key)
            {
                slot = (slot + 1) & (*slots - 1);
            }
            *count += table[slot] == 0;
            table[slot] = key;
            offset += record;
        }
    }
    return result;
}

// Reads a directory's names into its listing: their keys (file_cache_name_key) in a table at least twice as large. The
// directory is then `unlisted` instead if a listing can't answer for it exactly: its file system's lookups don't match
// its listings (file_cache_exact_names), it has a name with a byte past ASCII, or it can't be read whole.
//
// A directory's first look reads one small buffer (BUSTER_FILE_CACHE_SMALL_LISTING, about 25 names) and, unless that is
// filled to within a record of its end, finds the directory's end with one more call. A directory with more names is
// `deferred`, unread, and read whole at BUSTER_FILE_CACHE_LARGE_LISTING times the failed probes (256). On the 9700X (Btrfs) a
// failed probe took 0.24 to 0.31 us, and the calls of a small directory's listing (open, two getdents64, fstatfs, close)
// about 1.2 us, which the probes it saves next repay; a first look at clang's resource directory or /usr/include took
// 2.3 to 3.5 us in its getdents64 alone, and reading the 263 names of the former whole about 20 us in the compiler.
// One lane at a time. The names are read once; a directory that changes during the run is no concern, as files that
// change aren't (the cache keeps every path's answer for the whole run).
static void file_cache_list_directory(FileCache* cache, FileCacheDirectory* directory, FileCacheCounts* counts)
{
    const int fd = open(directory->path, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NONBLOCK | O_NOCTTY);
    counts->listings += 1;
    // On the stack, whose pages a lookup's callers have touched, rather than in an arena's fresh ones.
    uint8_t buffer[BUSTER_FILE_CACHE_LISTING_BUFFER] __attribute__((aligned(8)));
    const bool first = !directory->deferred;
    const long got = fd >= 0 ? file_cache_read_directory(fd, buffer, first ? BUSTER_FILE_CACHE_SMALL_LISTING : BUSTER_FILE_CACHE_LISTING_BUFFER) : -1;
    const bool full = first && got > (long)(BUSTER_FILE_CACHE_SMALL_LISTING - BUSTER_FILE_CACHE_RECORD_MAX);
    const long end = first && got > 0 && !full ? file_cache_read_directory(fd, buffer + BUSTER_FILE_CACHE_SMALL_LISTING, BUSTER_FILE_CACHE_SMALL_LISTING) : 0;
    const bool more = full || end > 0;
    struct statfs system;
    const bool exact = !more && got >= 0 && end == 0 && fstatfs(fd, &system) == 0 && file_cache_exact_names((uint64_t)system.f_type);
    uint32_t* names = 0;
    uint32_t slots = 0;
    uint32_t count = 0;
    // A first look's one buffer is the whole directory; a whole read goes on to getdents64's end.
    long chunk = exact ? got : -1;
    while (chunk > 0)
    {
        const bool added = file_cache_list_names(cache, buffer, chunk, &names, &slots, &count);
        chunk = !added ? -1 : first ? 0 : file_cache_read_directory(fd, buffer, BUSTER_FILE_CACHE_LISTING_BUFFER);
    }
    if (fd >= 0)
    {
        close(fd);
    }
    // A listing read to its end (getdents64 returned 0) with no name it can't answer for, or a large directory put off.
    const bool listed = exact && chunk == 0 && names != 0;
    directory->deferred = directory->deferred || more;
    cache->listed_directories += listed;
    directory->names = listed ? names : 0;
    directory->name_mask = listed ? slots - 1 : 0;
    directory->unlisted = !listed && !more;
}

// Whether directory `directory`'s listing says no file can be at `path` (`length` bytes), whose first
// `directory->length` bytes are the directory and next a '/': the listing doesn't have the path's next component, with
// ASCII letters' case folded. A component the listing can't rule out is probed: one it has (in any case, or another name
// with its key), one with a byte past ASCII, "." and "..", an empty one, and one longer than a name can be, whose
// lookup fails with ENAMETOOLONG, not ENOENT.
static bool file_cache_listed_absent(const FileCacheDirectory* directory, const char* path, uint32_t length)
{
    const uint32_t* names = directory->names;
    const uint32_t start = directory->length + 1;
    uint32_t end = start;
    bool ascii = true;
    while (end < length && path[end] != '/')
    {
        ascii = ascii && (uint8_t)path[end] < 0x80;
        end += 1;
    }
    const uint32_t component = end > start ? end - start : 0;
    const bool dots = component != 0 && component <= 2 && path[start] == '.' && path[end - 1] == '.';
    bool result = names != 0 && ascii && component != 0 && component <= BUSTER_FILE_CACHE_NAME_MAX && !dots;
    if (result)
    {
        const uint32_t key = file_cache_name_key((const uint8_t*)path + start, component);
        const uint32_t mask = directory->name_mask;
        uint32_t slot = key & mask;
        while (names[slot] != 0 && names[slot] != key)
        {
            slot = (slot + 1) & mask;
        }
        result = names[slot] == 0;
    }
    return result;
}

// After a probe of a path in `directory` (null for none): a path without a file there (ENOENT, which also says the
// directory could be searched) counts towards the directory's listing, which the cache reads at the
// cache->listing_misses-th, or BUSTER_FILE_CACHE_LARGE_LISTING times that for a directory its first look found large
// (file_cache_list_directory). One lane at a time.
static void file_cache_count_miss(FileCache* cache, FileCacheDirectory* directory, const FileProbe* probe, FileCacheCounts* counts)
{
    if (directory != 0 && probe->error == ENOENT)
    {
        directory->misses += 1;
        const uint64_t at = (uint64_t)cache->listing_misses * (directory->deferred ? BUSTER_FILE_CACHE_LARGE_LISTING : 1);
        if (cache->listing_misses != 0 && directory->misses == at && directory->names == 0 && !directory->unlisted)
        {
            file_cache_list_directory(cache, directory, counts);
        }
    }
}

// How a unit with these options prepares files: trigraphs or not, raw string literals or not, the literal prefixes its
// language mode splits, and its kernels. A file cache that units share is made with it (file_cache_initialize).
static PPPrepare pp_prepare_options(const PPOptions* options)
{
    return (PPPrepare){
        .refine_flags = (options->trigraphs ? C_TOKENIZER_TRIGRAPHS : 0) | pp_raw_strings(options) | c_language_lexer_flags(options->language) |
                        (options->language.standard < 2023 ? C_TOKENIZER_NO_DIGIT_SEPARATORS : 0),
        .split_kinds = pp_split_kinds(options->language),
        .dollar_punctuation = pp_dollar_punctuation(options),
        .vectorized = BUSTER_VECTOR_PATH(!options->scalar),
        .window_bytes = options->window_bytes,
    };
}

// What includes a file, for the search of a header it includes: the path the includer was found by (whose directory a
// quoted name is looked for in first) and how it was found (its system level), or `present` false for no file.
typedef struct PPIncluder PPIncluder;
struct PPIncluder
{
    const char* path;
    // The length of the path's directory part, or BUSTER_PP_NONE if it has none (the directory is then ".").
    uint32_t directory_length;
    uint8_t system;
    bool present;
};

// What candidate `index` of an include's search is (pp_include_candidate).
typedef enum PPCandidate
{
    // A path to look for the header at, in `path`.
    PP_CANDIDATE_PATH,
    // No path: the directory and the name together, or an absolute name, are too long for one (ENAMETOOLONG, which the
    // include search reports: pp_find_include).
    PP_CANDIDATE_SKIP,
    // There are no more candidates.
    PP_CANDIDATE_END,
} PPCandidate;

// Candidate `index` (from 0) of the search for the file an include names, `name` in `#include "name"` or (`angled`)
// `#include <name>`: an absolute name itself; else the includer's directory (quoted only), then the search directories
// (the quoted ones only for a quoted name), or for #include_next (`next`) the directories after the one the includer's
// inclusion was found in (`includer_search`). For a path, writes it to `path` (PATH_MAX bytes, not NUL-terminated),
// its length, the system level a file found there has, and the search directory it is in (BUSTER_PP_NONE for the
// includer's directory or an absolute path), which a candidate too long for a path gets too. pp_find_include and the
// run-ahead (c_files.c) both go through the candidates in order, so that they look at the same paths.
static PPCandidate pp_include_candidate(const Preprocessor* pp, const uint8_t* name, uint32_t length, bool angled, bool next, const PPIncluder* includer,
                                        uint32_t includer_search, uint32_t index, char* path, uint32_t* path_length, uint8_t* system, uint32_t* directory)
{
    PPCandidate result = PP_CANDIDATE_END;
    *directory = BUSTER_PP_NONE;
    if (pp->options.no_external_files)
    {
        result = PP_CANDIDATE_END;
    }
    else if (length != 0 && name[0] == '/')
    {
        // An absolute path: its record, if a file is there (a record is kept for a missing one too).
        if (index == 0)
        {
            result = PP_CANDIDATE_SKIP;
            if (length < PATH_MAX)
            {
                memcpy(path, name, length);
                *path_length = length;
                *system = BUSTER_PP_USER_FILE;
                result = PP_CANDIDATE_PATH;
            }
        }
    }
    else if (length != 0)
    {
        const bool resumes = next && includer->present && includer_search != BUSTER_PP_NONE;
        const bool local = !resumes && !angled && includer->present;
        const uint32_t start = resumes ? includer_search + 1 : angled ? pp->angled_first : 0;
        if (local && index == 0)
        {
            // The includer's directory: the directory part of its path as it was found, or "." for a path without one.
            const char* directory_path = includer->directory_length != BUSTER_PP_NONE ? includer->path : ".";
            const uint32_t directory_length = includer->directory_length != BUSTER_PP_NONE ? includer->directory_length : 1;
            result = PP_CANDIDATE_SKIP;
            if ((uint64_t)directory_length + 1 + length < PATH_MAX)
            {
                memcpy(path, directory_path, directory_length);
                path[directory_length] = '/';
                memcpy(path + directory_length + 1, name, length);
                *path_length = directory_length + 1 + length;
                *system = includer->system;
                result = PP_CANDIDATE_PATH;
            }
        }
        else
        {
            const uint64_t i = (uint64_t)start + index - (local ? 1 : 0);
            if (i < pp->directory_count)
            {
                const uint32_t directory_length = pp->directory_lengths[i];
                *directory = (uint32_t)i;
                result = PP_CANDIDATE_SKIP;
                if ((uint64_t)directory_length + 1 + length < PATH_MAX)
                {
                    memcpy(path, pp->directories[i], directory_length);
                    path[directory_length] = '/';
                    memcpy(path + directory_length + 1, name, length);
                    *path_length = directory_length + 1 + length;
                    *system = pp->directory_system[i];
                    result = PP_CANDIDATE_PATH;
                }
            }
        }
    }
    return result;
}

// An include candidate without a regular file is absent; other errors stop the search. The file walk uses the same
// boundary as the preprocessor, since cached probes retain their original error.
static bool pp_include_error_absent(int error)
{
    return error == ENOENT || error == ENOTDIR || error == EISDIR || error == EINVAL || error == -1;
}

// The forms of an include's header name (pp_literal_header_name).
typedef enum PPHeaderForm
{
    // `<...>` with its '>' on the line, or a string literal, first on the line: the name is known without expanding the
    // line.
    PP_HEADER_LITERAL,
    // Anything else, a `<` without its `>` on the line too: the line must be expanded first, and then be one of the
    // literal forms.
    PP_HEADER_COMPUTED,
    // An empty line, a string literal that isn't a plain one, or a name longer than the room for it.
    PP_HEADER_INVALID,
} PPHeaderForm;

// Token `first` of a directive's line [.., end), a '<', read as clang reads a header name from its file
// (Lexer::LexAngledStringLiteral): up to the first '>' on the line that no backslash escapes, through the scalar lexer's
// characters, which skip line splices (translation phase 2 removed them). A backslash takes the character after it into
// the name, a '>' too: `<a\>b.h>` names `a\>b.h`. Writes the name's bytes to `name` as far as `capacity` holds them
// (`*length` of them; `*whole` says whether that is all of them). Returns the offset past the '>', or 0 if the line has
// none.
static uint32_t pp_scan_header_bytes(uint32_t refine_flags, const PPFileData* data, uint32_t first, uint32_t line_end, uint8_t* name, uint32_t capacity,
                                     uint32_t* length, bool* whole)
{
    const ScalarLexer lexer = {
        .source = data->bytes,
        .length = line_end,
        .trigraphs = (refine_flags & C_TOKENIZER_TRIGRAPHS) != 0,
        .cplusplus = false,
    };
    uint64_t p = (uint64_t)first + 1;
    bool closed = false;
    bool line_ended = false;
    bool escaped = false;
    *length = 0;
    *whole = true;
    while (!closed && !line_ended && p < line_end)
    {
        uint64_t size;
        uint64_t char_start;
        const int32_t c = get_char(&lexer, p, &size, &char_start);
        closed = c == '>' && !escaped;
        line_ended = c == '\n' || c == '\r' || c == BUSTER_LEX_END_OF_FILE;
        const bool kept = !closed && !line_ended;
        const bool fits = *length < capacity;
        if (kept && fits)
        {
            name[*length] = (uint8_t)c;
            *length += 1;
        }
        *whole = *whole && (fits || !kept);
        escaped = c == '\\' && !escaped;
        p += size != 0 ? size : 1;
    }
    return closed ? (uint32_t)p : 0;
}

static uint32_t pp_scan_header_name(uint32_t refine_flags, const PPFileData* data, uint32_t first, uint32_t end, uint8_t* name, uint32_t capacity,
                                    uint32_t* length, bool* whole)
{
    const uint32_t line_end = end < data->token_count ? data->starts[end] : data->length;
    return pp_scan_header_bytes(refine_flags, data, data->starts[first], line_end, name, capacity, length, whole);
}

// The header name of directive d, an #include, #include_next or #import of a prepared file: `<...>` read from the
// source, up to the '>' on the line (pp_scan_header_name), or a string literal, both without line splices (translation
// phase 2 removed them). For a literal form, writes the name to `name` (`capacity` bytes) and its length, whether it is
// angled, and where it ends in the file (`*stop`: past its '>' or the string literal; else 0), from which clang lexes
// the line's next token, a warning (CheckEndOfDirective). Without its '>' on the line, a '<' is a computed name: clang's
// LexHeaderName joins the
// tokens up to a '>', so that with a comment that runs on to the next line, `<a/*x` then `*/b.h>` is `a b.h`. With it,
// the name is written out, never a computed one, whose identifiers would expand. `scratch` holds the string literal's
// spelling while its splices are removed (room for the token's bytes).
static PPHeaderForm pp_literal_header_name(const PPFileData* data, uint32_t d, uint32_t refine_flags, uint8_t* name, uint32_t capacity, uint32_t* length, bool* angled,
                                           uint32_t* stop, Arena* scratch)
{
    const uint32_t hash = data->directive_hash[d];
    const uint32_t end = data->directive_end[d];
    const uint32_t first = hash + 2;
    PPHeaderForm result = PP_HEADER_INVALID;
    *length = 0;
    *angled = false;
    *stop = 0;
    if (first < end && (data->kinds[first] & 0x7F) == C_TOKEN_LESS)
    {
        // The lexer doesn't make header names: read <...> from the source.
        bool whole;
        *stop = pp_scan_header_name(refine_flags, data, first, end, name, capacity, length, &whole);
        result = *stop == 0 ? PP_HEADER_COMPUTED : whole ? PP_HEADER_LITERAL : PP_HEADER_INVALID;
        *angled = result == PP_HEADER_LITERAL;
    }
    else if (first < end && (data->kinds[first] & 0x7F) == C_TOKEN_STRING_LITERAL)
    {
        // The string literal without its line splices.
        const uint32_t start = data->starts[first];
        const uint32_t stop_byte = data->values[first];
        *stop = stop_byte;
        const uint64_t mark = scratch->length;
        uint8_t* buffer = arena_push(scratch, (uint64_t)(stop_byte - start) + 1, 1);
        const uint8_t* spelling = 0;
        const uint32_t spelled = buffer != 0 ? pp_remove_splices(data->bytes + start, stop_byte - start, refine_flags, C_TOKEN_STRING_LITERAL, buffer, &spelling) : 0;
        if (spelled >= 2 && spelled - 2 < capacity && spelling[0] == '"')
        {
            *length = spelled - 2;
            memcpy(name, spelling + 1, *length);
            result = PP_HEADER_LITERAL;
        }
        arena_pop_to(scratch, mark);
    }
    else if (first < end)
    {
        result = PP_HEADER_COMPUTED;
    }
    return result;
}

BUSTER_INLINE bool pp_file_space(const PPFileData* data, uint32_t token)
{
    return (data->space[token / 64] >> (token % 64)) & 1;
}

// Whether a replacement list's tokens [begin, end) hold a '##' (`*pastes`), a '#' (`*hashes`) and __VA_OPT__
// (`*va_opt`), 64 kinds at a time: three compares per 64, then one per 16 values that hold identifiers, masked to the list
// (a masked load doesn't touch the bytes it leaves out).

// A large definition's ordered validation and its # checks share this temporary name-to-ordinal index. First
// expansion rebuilds it while retaining the parameter ids; later expansions use PPItems directly. Up to 16 names
// fit inline and are compared in one vector (or by the scalar twin); further names use a half-full hash table,
// grown geometrically even for invalid lists past the parameter limit, so the first diagnostic stays ordered.
#define BUSTER_PP_SMALL_PARAMETERS 16

typedef struct PPParameterIndex PPParameterIndex;
struct PPParameterIndex
{
    IdentId names[BUSTER_PP_SMALL_PARAMETERS];
    uint32_t ordinals[BUSTER_PP_SMALL_PARAMETERS];
    uint64_t* slots; // High half: IdentId; low half: ordinal + 1. Zero is empty.
    uint32_t capacity;
    uint32_t count;
    bool vectorized;
    bool repeated; // The preceding large definition insertion found an earlier ordinal.
    Scratch scratch;
};

// ---------------------------------------------------------------------------------------------------------------------
// The main loop

// Copies a file context's tokens from its position up to `limit` to the output, stopping at the first identifier that
// names a macro; returns where it stopped. 64 tokens at a time: one compare finds the identifiers, whose ids are tested
// against the macro bitset one by one (a gather is 64 uops on Zen 5), then every column goes out with whole-vector stores
// (pp_store_file_tokens).

// Copies a buffer context's tokens to the target, stopping at the first identifier that names an enabled macro and
// isn't painted; an identifier that names a disabled macro is painted on the way (BUSTER_PP_NO_EXPAND). Returns where
// it stopped. 64 tokens at a time, as pp_copy_file_run_vector.

// ---------------------------------------------------------------------------------------------------------------------
// Invocations

// The most commas between arguments pp_collect_arguments notes: an invocation with more arguments is read a token at a time.
#define BUSTER_PP_FAST_SEPARATORS 64
// The deepest nesting of parentheses pp_collect_arguments' 8-bit depths hold.
#define BUSTER_PP_FAST_DEPTH 63
// Small arguments keep the existing collectors; indexing pays when nested invocations would copy long suffixes.
#define BUSTER_PP_ARGUMENT_INDEX_MIN 128


// An invocation's ')' and the commas between its arguments, as pp_scan_arguments finds them in a run.
typedef struct PPScan PPScan;
struct PPScan
{
    uint32_t close; // BUSTER_PP_NONE when the scan didn't find it.
    uint32_t separator_count;
    uint32_t separators[BUSTER_PP_FAST_SEPARATORS + 64];
};

// What a collected token does: nothing more, close the invocation, (a builtin's first token that isn't '(') end it, or
// (an attribute query's '(') start its stream.
typedef enum PPCollected
{
    PP_COLLECTED_MORE,
    PP_COLLECTED_CLOSED,
    PP_COLLECTED_INVALID,
    PP_COLLECTED_STREAM,
} PPCollected;

// The most names of macros a simple invocation's body may have for pp_emit_simple (painted, or expanded straight in):
// one with more takes the main loop.
#define BUSTER_PP_EMIT_NAMES 16

// A name of a macro in a simple invocation's body (pp_emit_simple): its body token, and the macro whose body goes
// straight into the target there, or 0 for a name that is painted.
typedef struct PPEmitName PPEmitName;
struct PPEmitName
{
    uint32_t token;
    PPMacroId expands;
};

// What pp_emit_simple_plan found a simple invocation's expansion to be: its body's names of macros, and its size.
typedef struct PPEmitPlan PPEmitPlan;
struct PPEmitPlan
{
    PPEmitName names[BUSTER_PP_EMIT_NAMES];
    uint32_t name_count;
    uint32_t size;
};

// How an invocation of a template with ## and # is finished (pp_plan_operators): not by the templates' path, with its
// expansion empty, written into the output, or written into the expansions for the main loop to read again.
#define BUSTER_PP_OPERATORS_NONE 0
#define BUSTER_PP_OPERATORS_EMPTY 1
#define BUSTER_PP_OPERATORS_EMIT 2
#define BUSTER_PP_OPERATORS_SUBSTITUTE 3


// ---------------------------------------------------------------------------------------------------------------------
// Built-in macros

// The presumed line of a location (after any #line), and its presumed file's name.
static uint32_t pp_presumed(Preprocessor* pp, uint32_t location, const char** name, uint32_t* name_length)
{
    const uint32_t range = pp_find_range(pp, location);
    uint32_t result = 1;
    *name = "<scratch space>";
    *name_length = (uint32_t)strlen(*name);
    if (range != BUSTER_PP_NONE && pp->ranges[range].file != BUSTER_PP_NONE)
    {
        result = pp_line_of(pp, range, location, 0);
        const PPFile* file = &pp->files[pp->ranges[range].file];
        *name = file->path;
        *name_length = file->path_length;
        for (uint32_t i = pp->line_directive_count; i > 0; i -= 1)
        {
            const PPLineDirective* line = &pp->line_directives[i - 1];
            if (line->range == range && line->location <= location)
            {
                result = (uint32_t)((int64_t)result + line->delta);
                if (line->name != 0)
                {
                    *name = line->name;
                    *name_length = line->name_length;
                }
                break;
            }
        }
    }
    return result;
}

// The answer of a feature query (__has_attribute and the like) about `name` (a scoped one as "scope::name").
static uint32_t pp_feature(const PPOptions* options, PPBuiltin builtin, const uint8_t* name, uint32_t length);

// A pragma's string (or __has_warning's), as clang's FinishLexStringLiteral reads one; defined with the pragmas.

// Expands a builtin macro whose name was just read, from the context it was read in (`expansion_end` from
// pp_expansion_end, for __LINE__ and __FILE__ in a token a macro made).
// The last component of presumed file name `path` (`length` bytes), as clang's processPathToFileName takes it for
// __FILE_NAME__ and __builtin_FILE_NAME (llvm::sys::path::filename, POSIX style): the bytes past its last '/'; "." for a
// name that ends in '/' past its root directory ("/foo/", "a/"); the root directory for a name that is only that ("/",
// "//"), and a "//net" root name whole; and the whole name where the component is empty. Its bytes are `*component`.
static __attribute__((unused)) uint32_t pp_path_file_name(const char* path, uint32_t length, const char** component)
{
    // The root directory's position (root_dir_start): a "//net" root name's separator after it, or a leading '/'.
    uint32_t root = UINT32_MAX;
    if (length > 3 && path[0] == '/' && path[1] == '/' && path[2] != '/')
    {
        for (uint32_t i = length; i-- > 2;)
        {
            root = path[i] == '/' ? i : root;
        }
    }
    else if (length > 0 && path[0] == '/')
    {
        root = 0;
    }
    // The component's end, past trailing separators but the root directory's.
    uint32_t end = length;
    while (end > 0 && end - 1 != root && path[end - 1] == '/')
    {
        end -= 1;
    }
    const char* bytes = path;
    uint32_t size = 0;
    if (length != 0 && path[length - 1] == '/' && (root == UINT32_MAX || end == 0 || end - 1 > root))
    {
        bytes = ".";
        size = 1;
    }
    else
    {
        // The component's start (filename_pos): the separator a name ends in, or past its last one, but a "//net"'s.
        uint32_t start = 0;
        if (end > 0 && path[end - 1] == '/')
        {
            start = end - 1;
        }
        else
        {
            uint32_t separator = UINT32_MAX;
            for (uint32_t i = 0; i + 1 < end; i += 1)
            {
                separator = path[i] == '/' ? i : separator;
            }
            start = separator == UINT32_MAX || (separator == 1 && path[0] == '/') ? 0 : separator + 1;
        }
        bytes = path + start;
        size = end - start;
    }
    *component = size != 0 ? bytes : path;
    return size != 0 ? size : length;
}

// ---------------------------------------------------------------------------------------------------------------------
// Steps of the main loop

// The output's tokens pp_step_file resolves the keywords of at once, at a directive (pp_resolve_keywords_range).
#define BUSTER_PP_KEYWORD_CHUNK 16384


// The main loop: runs the top context until the stack falls to `stop_depth` contexts (an argument's barrier that ends
// goes on with its invocation).

// ---------------------------------------------------------------------------------------------------------------------
// Directives

// The most macros a name in a #if line expands through for pp_condition_direct (X to Y to 1), and the longest line it
// takes.
#define BUSTER_PP_CONDITION_CHAIN 8
#define BUSTER_PP_CONDITION_TOKENS 64


// The escape sequences clang refuses in an unevaluated string literal (check_literal_unevaluated_escapes), which
// pp_decode_file_name diagnoses; c_preprocessor_literal.c includes it again for its constants.
#include "c_check_literal_diagnostic.c"


// What a pragma is to the preprocessor, by its names: clang's handlers (Pragma.cpp's RegisterBuiltinPragmas), and -E's
// printer, which prints every other pragma as it is.
typedef enum PPPragmaKind
{
    PP_PRAGMA_OTHER,
    PP_PRAGMA_ONCE,
    PP_PRAGMA_QUIET, // mark, region and endregion: read, and not printed.
    PP_PRAGMA_PUSH_MACRO,
    PP_PRAGMA_POP_MACRO,
    PP_PRAGMA_MESSAGE, // message, GCC warning and GCC error: a diagnostic, and printed in clang's form.
    PP_PRAGMA_WARNING,
    PP_PRAGMA_ERROR,
    PP_PRAGMA_POISON, // These four in the GCC and clang namespaces.
    PP_PRAGMA_SYSTEM_HEADER,
    PP_PRAGMA_DEPENDENCY,
    PP_PRAGMA_DIAGNOSTIC,
    PP_PRAGMA_OMP, // Printed expanded (-E's printer expands it, as OpenMP says).
    PP_PRAGMA_SEMANTIC, // Expanded only for an opted-in semantic consumer; ordinary -E retains raw operands.
    PP_PRAGMA_IDENT, // #ident and #sccs: not pragmas, but read and printed alike.
    PP_PRAGMA_REGION, // clang assume_nonnull, arc_cf_code_audited and unsafe_buffer_usage: begin and end.
    PP_PRAGMA_MACRO_NOTE, // clang deprecated, final and restrict_expansion, of a macro: not printed.
} PPPragmaKind;

// Whether token i of [.., end) is the identifier `name`.
static bool pp_token_is_name(const Preprocessor* pp, const PPTokens* tokens, uint32_t i, uint32_t end, const char* name)
{
    const uint32_t length = (uint32_t)strlen(name);
    const bool result = i < end && tokens->kinds[i] == C_TOKEN_RAW_IDENTIFIER && intern_length(pp->table, tokens->values[i]) == length &&
                        memcmp(intern_bytes(pp->table, tokens->values[i]), name, length) == 0;
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// #if expressions

// A value of a #if expression (C 6.10.1p4: intmax_t or uintmax_t), and whether evaluating it divided by zero (an error
// only if the value is used: && || and ?: drop it from an unevaluated operand).
typedef struct PPValue PPValue;
struct PPValue
{
    uint64_t bits;
    bool is_unsigned;
    bool division_by_zero;
};

// Operators, with their precedence (higher binds tighter).
typedef enum PPOperator
{
    PP_OPERATOR_PAREN,
    PP_OPERATOR_PLUS,
    PP_OPERATOR_MINUS,
    PP_OPERATOR_NOT,
    PP_OPERATOR_COMPLEMENT,
    PP_OPERATOR_MULTIPLY,
    PP_OPERATOR_DIVIDE,
    PP_OPERATOR_REMAINDER,
    PP_OPERATOR_ADD,
    PP_OPERATOR_SUBTRACT,
    PP_OPERATOR_SHIFT_LEFT,
    PP_OPERATOR_SHIFT_RIGHT,
    PP_OPERATOR_LESS,
    PP_OPERATOR_LESS_EQUAL,
    PP_OPERATOR_GREATER,
    PP_OPERATOR_GREATER_EQUAL,
    PP_OPERATOR_EQUAL,
    PP_OPERATOR_NOT_EQUAL,
    PP_OPERATOR_AND,
    PP_OPERATOR_XOR,
    PP_OPERATOR_OR,
    PP_OPERATOR_LOGICAL_AND,
    PP_OPERATOR_LOGICAL_OR,
    PP_OPERATOR_QUESTION,
    PP_OPERATOR_COLON,
    PP_OPERATOR_COMMA,
} PPOperator;

#define BUSTER_PP_EXPRESSION_DEPTH 1024


// The length of the UTF-8 sequence at `bytes` (`available` long) and its code point, as LLVM's strict conversion reads
// it; 0 if it isn't one (overlong, a surrogate, past U+10FFFF, or cut short).
static uint32_t pp_utf8_sequence(const uint8_t* bytes, uint32_t available, uint32_t* code_point)
{
    const uint8_t lead = bytes[0];
    uint32_t result = lead < 0x80 ? 1 : lead >= 0xC2 && lead <= 0xDF ? 2 : lead >= 0xE0 && lead <= 0xEF ? 3 : lead >= 0xF0 && lead <= 0xF4 ? 4 : 0;
    const uint8_t second_min = lead == 0xE0 ? 0xA0 : lead == 0xF0 ? 0x90 : 0x80;
    const uint8_t second_max = lead == 0xED ? 0x9F : lead == 0xF4 ? 0x8F : 0xBF;
    uint32_t value = result == 1 ? lead : result == 2 ? lead & 0x1Fu : result == 3 ? lead & 0x0Fu : lead & 0x07u;
    result = result <= available && (result < 2 || (bytes[1] >= second_min && bytes[1] <= second_max)) ? result : 0;
    for (uint32_t k = 1; k < result; k += 1)
    {
        value = (value << 6) | (bytes[k] & 0x3Fu);
        result = (bytes[k] & 0xC0) == 0x80 ? result : 0;
    }
    *code_point = value;
    return result;
}

// A character constant's characters so far: how many, the last, the low bytes of all (an ordinary constant's value
// when it has several, its first character highest), and whether that value has lost a character.
typedef struct PPCharacters PPCharacters;
struct PPCharacters
{
    uint32_t count;
    uint32_t last;
    uint32_t packed;
    bool too_long;
};

// The place and enclosing liveness of an operator. Reductions restore this state before another operator is
// read, so &&, || and both arms of ?: suppress evaluation warnings without skipping syntax or losing signedness.
typedef struct PPExpressionContext PPExpressionContext;
struct PPExpressionContext
{
    uint32_t location;
    uint32_t origin;
    bool live;
};

// ---------------------------------------------------------------------------------------------------------------------
// #embed

// The standard embed parameters (C23 6.10.3) and clang's clang::offset, by their bits in PPEmbedParameters.given, with
// the names clang's diagnostics give them.
#define BUSTER_PP_EMBED_LIMIT 0
#define BUSTER_PP_EMBED_PREFIX 1
#define BUSTER_PP_EMBED_SUFFIX 2
#define BUSTER_PP_EMBED_IF_EMPTY 3
#define BUSTER_PP_EMBED_OFFSET 4
#define BUSTER_PP_EMBED_UNKNOWN 5

// An #embed's or a __has_embed's parameters: the ones given (bits by BUSTER_PP_EMBED_LIMIT and the others), limit's and
// clang::offset's values, the tokens of prefix, suffix and if_empty (ranges of the stack they were read from, by the
// parameters' bits), and how many parameters clang doesn't know.
typedef struct PPEmbedParameters PPEmbedParameters;
struct PPEmbedParameters
{
    uint64_t limit;
    uint64_t offset;
    uint32_t begins[BUSTER_PP_EMBED_UNKNOWN];
    uint32_t ends[BUSTER_PP_EMBED_UNKNOWN];
    uint32_t given;
    uint32_t unknown;
};

// ---------------------------------------------------------------------------------------------------------------------
// Feature queries and predefines

// What clang 23 answers nonzero, for x86-64 Linux in C, to the feature queries of the names that clang's, glibc's and
// musl's headers, sqlite and Zig's libc headers ask about, of the GNU attributes (with their gnu:: spellings for
// __has_c_attribute), and for __has_feature and __has_extension of every name in clang's Features.def: generated by
// asking clang in the ten modes from c89 to gnu23 (docs/c-preprocessor.md has the procedure). Only __has_feature's
// answers depend on the mode: C11's features hold from C11 on and c_fixed_enum from C23 (`standard`). Attribute names
// are looked up without leading and trailing "__", as clang normalizes them. Sorted for binary search.
static const PPFeature pp_features_attribute[] = {
    { "alias", 1 }, { "aligned", 1 }, { "alloc_align", 1 }, { "alloc_size", 1 }, { "allocating", 1 }, { "always_inline", 1 }, { "annotate", 1 },
    { "artificial", 1 }, { "assume", 1 }, { "assume_aligned", 1 }, { "availability", 1 }, { "blocking", 1 }, { "btf_decl_tag", 1 },
    { "btf_type_tag", 1 }, { "callback", 1 }, { "cleanup", 1 }, { "cold", 1 }, { "common", 1 }, { "const", 1 }, { "constructor", 1 },
    { "convergent", 1 }, { "counted_by", 1 }, { "cpu_dispatch", 1 }, { "cpu_specific", 1 }, { "deprecated", 1 }, { "destructor", 1 },
    { "diagnose_if", 1 }, { "disable_tail_calls", 1 }, { "enable_if", 1 }, { "enum_extensibility", 1 }, { "error", 1 },
    {
        .name = "ext_vector_type",
        .value = 1,
    },
    { "fallthrough", 1 },
    { "flag_enum", 1 }, { "flatten", 1 }, { "force_align_arg_pointer", 1 }, { "format", 1 }, { "format_arg", 1 }, { "gnu_inline", 1 }, { "hot", 1 },
    { "ifunc", 1 }, { "internal_linkage", 1 }, { "interrupt", 1 }, { "leaf", 1 }, { "lifetimebound", 1 }, { "malloc", 1 }, { "may_alias", 1 },
    { "mig_server_routine", 1 }, { "minsize", 1 }, { "mode", 1 }, { "ms_abi", 1 }, { "musttail", 1 }, { "naked", 1 }, { "no_builtin", 1 },
    { "no_caller_saved_registers", 1 }, { "no_instrument_function", 1 }, { "no_profile_instrument_function", 1 }, { "no_sanitize", 1 },
    { "no_sanitize_address", 1 }, { "no_sanitize_thread", 1 }, { "no_specializations", 1 }, { "no_split_stack", 1 }, { "no_stack_protector", 1 },
    { "nocf_check", 1 }, { "nocommon", 1 }, { "nodebug", 1 }, { "noderef", 1 }, { "noescape", 1 }, { "noinline", 1 }, { "nomerge", 1 }, { "nonallocating", 1 },
    { "nonblocking", 1 }, { "nonnull", 1 }, { "nonstring", 1 }, { "noreturn", 1 }, { "not_tail_called", 1 }, { "nothrow", 1 }, { "nouwtable", 1 },
    { "ns_consumed", 1 }, { "ns_returns_retained", 1 }, { "objc_boxable", 1 }, { "objc_designated_initializer", 1 }, { "objc_independent_class", 1 },
    { "objc_root_class", 1 }, { "objc_runtime_visible", 1 }, { "optnone", 1 }, { "overloadable", 1 }, { "packed", 1 },
    { "patchable_function_entry", 1 }, { "preferred_type", 1 }, { "preserve_all", 1 }, { "preserve_most", 1 }, { "pure", 1 }, { "regcall", 1 },
    { "require_constant_initialization", 1 }, { "retain", 1 }, { "returns_nonnull", 1 }, { "returns_twice", 1 }, { "section", 1 }, { "sentinel", 1 },
    { "swift_name", 1 }, { "swift_private", 1 }, { "swiftcall", 1 }, { "sysv_abi", 1 }, { "target", 1 }, { "target_clones", 1 }, { "tls_model", 1 },
    { "transparent_union", 1 }, { "trivial_abi", 1 }, { "unavailable", 1 }, { "uninitialized", 1 }, { "unsafe_buffer_usage", 1 }, { "unused", 1 },
    { "used", 1 }, { "vector_size", 1 }, { "vectorcall", 1 }, { "visibility", 1 }, { "warn_unused_result", 1 }, { "warning", 1 }, { "weak", 1 },
    { "weakref", 1 }, { "zero_call_used_regs", 1 },
};
static const PPFeature pp_features_builtin[] = {
    { "__atomic_add_fetch", 1 }, { "__atomic_compare_exchange_n", 1 }, { "__atomic_exchange_n", 1 }, { "__atomic_load", 1 }, { "__atomic_load_n", 1 },
    { "__atomic_or_fetch", 1 }, { "__atomic_signal_fence", 1 }, { "__atomic_store_n", 1 }, { "__atomic_sub_fetch", 1 },
    { "__atomic_thread_fence", 1 }, { "__builtin_FILE", 1 }, { "__builtin___memccpy_chk", 1 }, { "__builtin___memcpy_chk", 1 },
    { "__builtin___memmove_chk", 1 }, { "__builtin___memset_chk", 1 }, { "__builtin___snprintf_chk", 1 }, { "__builtin___sprintf_chk", 1 },
    { "__builtin___stpcpy_chk", 1 }, { "__builtin___stpncpy_chk", 1 }, { "__builtin___strcat_chk", 1 }, { "__builtin___strcpy_chk", 1 },
    { "__builtin___strlcat_chk", 1 }, { "__builtin___strlcpy_chk", 1 }, { "__builtin___strncat_chk", 1 }, { "__builtin___strncpy_chk", 1 },
    { "__builtin___vsnprintf_chk", 1 }, { "__builtin___vsprintf_chk", 1 }, { "__builtin_add_overflow", 1 }, { "__builtin_assume", 1 },
    { "__builtin_assume_aligned", 1 }, { "__builtin_available", 1 }, { "__builtin_bit_cast", 1 }, { "__builtin_bitreverse16", 1 },
    { "__builtin_bitreverse32", 1 }, { "__builtin_bitreverse64", 1 }, { "__builtin_bitreverse8", 1 }, { "__builtin_bswap16", 1 },
    { "__builtin_bswap32", 1 }, { "__builtin_bswap64", 1 }, { "__builtin_clear_padding", 1 }, { "__builtin_clz", 1 }, { "__builtin_clzg", 1 },
    { "__builtin_clzll", 1 }, { "__builtin_clzs", 1 }, { "__builtin_constant_p", 1 }, { "__builtin_copysign", 1 }, { "__builtin_ctz", 1 },
    { "__builtin_ctzg", 1 }, { "__builtin_ctzll", 1 }, { "__builtin_ctzs", 1 }, { "__builtin_debugtrap", 1 }, { "__builtin_dynamic_object_size", 1 },
    { "__builtin_elementwise_fshl", 1 }, { "__builtin_expect", 1 }, { "__builtin_frame_address", 1 }, { "__builtin_ia32_pause", 1 },
    { "__builtin_memcpy", 1 }, { "__builtin_mul_overflow", 1 }, { "__builtin_nansf128", 1 }, { "__builtin_nondeterministic_value", 1 },
    { "__builtin_nontemporal_store", 1 }, { "__builtin_offsetof", 1 }, { "__builtin_operator_new", 201802 }, { "__builtin_popcount", 1 },
    { "__builtin_popcountg", 1 }, { "__builtin_popcountll", 1 }, { "__builtin_rotateleft32", 1 }, { "__builtin_round", 1 },
    { "__builtin_sadd_overflow", 1 }, { "__builtin_shufflevector", 1 }, { "__builtin_smul_overflow", 1 }, { "__builtin_sprintf", 1 },
    { "__builtin_ssub_overflow", 1 }, { "__builtin_sub_overflow", 1 }, { "__builtin_thread_pointer", 1 }, { "__builtin_trap", 1 },
    { "__builtin_types_compatible_p", 1 }, { "__builtin_uadd_overflow", 1 }, { "__builtin_umul_overflow", 1 }, { "__builtin_unpredictable", 1 },
    { "__builtin_unreachable", 1 }, { "__is_target_arch", 1 }, { "__is_target_environment", 1 }, { "__is_target_os", 1 },
    { "__is_target_variant_environment", 1 }, { "__is_target_variant_os", 1 }, { "__is_target_vendor", 1 }, { "__sync_synchronize", 1 },
};
static const PPFeature pp_features_feature[] = {
    { "arc_cf_code_audited", 1 }, { "assume_nonnull", 1 }, { "attribute_analyzer_noreturn", 1 }, { "attribute_availability", 1 },
    { "attribute_availability_app_extension", 1 }, { "attribute_availability_driverkit", 1 }, { "attribute_availability_in_templates", 1 },
    { "attribute_availability_swift", 1 }, { "attribute_availability_tvos", 1 }, { "attribute_availability_watchos", 1 },
    { "attribute_availability_with_message", 1 }, { "attribute_availability_with_replacement", 1 }, { "attribute_availability_with_strict", 1 },
    { "attribute_availability_with_version_underscores", 1 }, { "attribute_cf_consumed", 1 }, { "attribute_cf_returns_not_retained", 1 },
    { "attribute_cf_returns_on_parameters", 1 }, { "attribute_cf_returns_retained", 1 }, { "attribute_deprecated_with_message", 1 },
    { "attribute_deprecated_with_replacement", 1 }, { "attribute_diagnose_if_objc", 1 }, { "attribute_ext_vector_type", 1 },
    { "attribute_ns_consumed", 1 }, { "attribute_ns_consumes_self", 1 }, { "attribute_ns_returns_not_retained", 1 },
    { "attribute_ns_returns_retained", 1 }, { "attribute_objc_ivar_unused", 1 }, { "attribute_objc_method_family", 1 },
    { "attribute_overloadable", 1 }, { "attribute_unavailable_with_message", 1 }, { "attribute_unused_on_fields", 1 }, { "c_alignas", 1, 2011 },
    { "c_alignof", 1, 2011 }, { "c_atomic", 1, 2011 }, { "c_fixed_enum", 1, 2023 }, { "c_generic_selections", 1, 2011 },
    { "c_static_assert", 1, 2011 }, { "c_thread_local", 1, 2011 }, { "c_thread_safety_attributes", 1 }, { "clang_atomic_attributes", 1 },
    { "enumerator_attributes", 1 }, { "ext_vector_type_boolean", 1 }, { "nullability", 1 }, { "nullability_nullable_result", 1 },
    { "nullability_on_arrays", 1 }, { "nullability_on_classes", 1 }, { "objc_arc_fields", 1 }, { "objc_bool", 1 }, { "objc_bridge_id", 1 },
    { "objc_bridge_id_on_typedefs", 1 }, { "objc_c_static_assert", 1, 2011 }, { "objc_nonfragile_abi", 1 }, { "objc_property_explicit_atomic", 1 },
    { "objc_protocol_qualifier_mangling", 1 }, { "objc_subscripting", 1 }, { "objc_weak_class", 1 }, { "ownership_holds", 1 },
    { "ownership_returns", 1 }, { "ownership_takes", 1 }, { "pragma_stdc_cx_limited_range", 1 }, { "tls", 1 },
    { "undefined_behavior_sanitizer_finegrained_feature_checks", 1 },
};
static const PPFeature pp_features_extension[] = {
    { "arc_cf_code_audited", 1 }, { "assume_nonnull", 1 }, { "attribute_analyzer_noreturn", 1 }, { "attribute_availability", 1 },
    { "attribute_availability_app_extension", 1 }, { "attribute_availability_driverkit", 1 }, { "attribute_availability_in_templates", 1 },
    { "attribute_availability_swift", 1 }, { "attribute_availability_tvos", 1 }, { "attribute_availability_watchos", 1 },
    { "attribute_availability_with_message", 1 }, { "attribute_availability_with_replacement", 1 }, { "attribute_availability_with_strict", 1 },
    { "attribute_availability_with_version_underscores", 1 }, { "attribute_cf_consumed", 1 }, { "attribute_cf_returns_not_retained", 1 },
    { "attribute_cf_returns_on_parameters", 1 }, { "attribute_cf_returns_retained", 1 }, { "attribute_deprecated_with_message", 1 },
    { "attribute_deprecated_with_replacement", 1 }, { "attribute_diagnose_if_objc", 1 }, { "attribute_ext_vector_type", 1 },
    { "attribute_ns_consumed", 1 }, { "attribute_ns_consumes_self", 1 }, { "attribute_ns_returns_not_retained", 1 },
    { "attribute_ns_returns_retained", 1 }, { "attribute_objc_ivar_unused", 1 }, { "attribute_objc_method_family", 1 },
    { "attribute_overloadable", 1 }, { "attribute_unavailable_with_message", 1 }, { "attribute_unused_on_fields", 1 }, { "c_alignas", 1 },
    { "c_alignof", 1 }, { "c_atomic", 1 }, { "c_attributes", 1 }, { "c_countof", 1 }, { "c_fixed_enum", 1 },
    { "c_generic_selection_with_controlling_type", 1 }, { "c_generic_selections", 1 }, { "c_static_assert", 1 }, { "c_thread_local", 1 },
    { "c_thread_safety_attributes", 1 }, { "clang_atomic_attributes", 1 }, { "cxx_binary_literals", 1 }, { "cxx_fixed_enum", 1 },
    { "cxx_type_aware_allocators", 1 }, { "enumerator_attributes", 1 }, { "ext_vector_type_boolean", 1 }, { "gnu_asm", 1 },
    { "gnu_asm_goto_with_outputs", 1 }, { "gnu_asm_goto_with_outputs_full", 1 }, { "matrix_types_scalar_division", 1 }, { "nullability", 1 },
    { "nullability_nullable_result", 1 }, { "nullability_on_arrays", 1 }, { "nullability_on_classes", 1 }, { "objc_arc_fields", 1 },
    { "objc_bool", 1 }, { "objc_bridge_id", 1 }, { "objc_bridge_id_on_typedefs", 1 }, { "objc_c_static_assert", 1 }, { "objc_nonfragile_abi", 1 },
    { "objc_property_explicit_atomic", 1 }, { "objc_protocol_qualifier_mangling", 1 }, { "objc_subscripting", 1 }, { "objc_weak_class", 1 },
    { "overloadable_unmarked", 1 }, { "ownership_holds", 1 }, { "ownership_returns", 1 }, { "ownership_takes", 1 },
    { "pragma_clang_attribute_external_declaration", 1 }, { "pragma_clang_attribute_namespaces", 1 }, { "pragma_stdc_cx_limited_range", 1 },
    { "statement_attributes_with_gnu_syntax", 1 }, { "swiftasynccc", 1 }, { "swiftcc", 1 }, { "tls", 1 },
    { "undefined_behavior_sanitizer_finegrained_feature_checks", 1 },
};
static const PPFeature pp_features_c_attribute[] = {
    { "_Noreturn", 202202 }, { "deprecated", 201904 }, { "fallthrough", 201910 }, { "gnu::alias", 1 }, { "gnu::aligned", 1 },
    { "gnu::alloc_size", 1 }, { "gnu::always_inline", 1 }, { "gnu::artificial", 1 }, { "gnu::assume_aligned", 1 }, { "gnu::cleanup", 1 },
    { "gnu::cold", 1 }, { "gnu::common", 1 }, { "gnu::const", 1 }, { "gnu::constructor", 1 }, { "gnu::deprecated", 1 }, { "gnu::destructor", 1 },
    { "gnu::error", 1 }, { "gnu::fallthrough", 1 }, { "gnu::flatten", 1 }, { "gnu::force_align_arg_pointer", 1 }, { "gnu::format", 1 },
    { "gnu::format_arg", 1 }, { "gnu::gnu_inline", 1 }, { "gnu::hot", 1 }, { "gnu::ifunc", 1 }, { "gnu::interrupt", 1 }, { "gnu::leaf", 1 },
    { "gnu::malloc", 1 }, { "gnu::may_alias", 1 }, { "gnu::mode", 1 }, { "gnu::ms_abi", 1 }, { "gnu::naked", 1 },
    { "gnu::no_caller_saved_registers", 1 }, { "gnu::no_instrument_function", 1 }, { "gnu::no_profile_instrument_function", 1 },
    { "gnu::no_sanitize", 1 }, { "gnu::no_sanitize_address", 1 }, { "gnu::no_split_stack", 1 }, { "gnu::no_stack_protector", 1 },
    { "gnu::nocf_check", 1 }, { "gnu::nocommon", 1 }, { "gnu::noinline", 1 }, { "gnu::nonnull", 1 }, { "gnu::nonstring", 1 }, { "gnu::noreturn", 1 }, { "gnu::nothrow", 1 },
    { "gnu::packed", 1 }, { "gnu::patchable_function_entry", 1 }, { "gnu::pure", 1 }, { "gnu::regcall", 1 }, { "gnu::retain", 1 },
    { "gnu::returns_nonnull", 1 }, { "gnu::returns_twice", 1 }, { "gnu::section", 1 }, { "gnu::sentinel", 1 }, { "gnu::sysv_abi", 1 },
    { "gnu::target", 1 }, { "gnu::target_clones", 1 }, { "gnu::tls_model", 1 }, { "gnu::transparent_union", 1 }, { "gnu::unused", 1 },
    { "gnu::used", 1 }, { "gnu::vector_size", 1 }, { "gnu::visibility", 1 }, { "gnu::warn_unused_result", 1 }, { "gnu::warning", 1 },
    { "gnu::weak", 1 }, { "gnu::weakref", 1 }, { "gnu::zero_call_used_regs", 1 }, { "maybe_unused", 202106 }, { "nodiscard", 202003 },
    { "noreturn", 202202 },
};
// __has_warning's: every -W option of clang 23.1.3's `clang --print-diagnostic-options` that its __has_warning answers
// 1 for (the other 17 are remark groups), which #pragma clang diagnostic takes too.
static const PPFeature pp_features_warning[] = {
    { "-W#pragma-messages", 1 }, { "-W#warnings", 1 }, { "-WCFString-literal", 1 }, { "-WCL4", 1 }, { "-WIndependentClass-attribute", 1 },
    { "-WNSObject-attribute", 1 }, { "-WTU-local-entity-exposure", 1 }, { "-Waarch64-sme-attributes", 1 }, { "-Wabi", 1 }, { "-Wabsolute-value", 1 },
    { "-Wabstract-final-class", 1 }, { "-Wabstract-vbase-init", 1 }, { "-Waddress", 1 }, { "-Waddress-of-packed-member", 1 }, { "-Waddress-of-temporary", 1 },
    { "-Waggregate-return", 1 }, { "-Waix-compat", 1 }, { "-Walias-template-in-declaration-name", 1 }, { "-Walign-mismatch", 1 }, { "-Wall", 1 },
    { "-Walloc-size", 1 }, { "-Walloca", 1 }, { "-Walloca-with-align-alignof", 1 }, { "-Walways-inline-coroutine", 1 }, { "-Wambiguous-delete", 1 },
    { "-Wambiguous-ellipsis", 1 }, { "-Wambiguous-macro", 1 }, { "-Wambiguous-member-template", 1 }, { "-Wambiguous-reversed-operator", 1 },
    { "-Wamdgpu-unguarded-builtin-usage", 1 }, { "-Wamdgpu-wait-event-mask", 1 }, { "-Wanalyzer-incompatible-plugin", 1 },
    { "-Wandroid-unversioned-fallback", 1 }, { "-Wanon-enum-enum-conversion", 1 }, { "-Wanonymous-pack-parens", 1 }, { "-Wapinotes", 1 }, { "-Warc", 1 },
    { "-Warc-bridge-casts-disallowed-in-nonarc", 1 }, { "-Warc-maybe-repeated-use-of-weak", 1 }, { "-Warc-non-pod-memaccess", 1 },
    { "-Warc-performSelector-leaks", 1 }, { "-Warc-repeated-use-of-weak", 1 }, { "-Warc-retain-cycles", 1 }, { "-Warc-unsafe-retained-assign", 1 },
    { "-Wargument-outside-range", 1 }, { "-Wargument-undefined-behaviour", 1 }, { "-Warm-interrupt-save-fp-no-vfp-unit", 1 },
    { "-Warm-interrupt-vfp-clobber", 1 }, { "-Warray-bounds", 1 }, { "-Warray-bounds-pointer-arithmetic", 1 }, { "-Warray-compare", 1 },
    { "-Warray-compare-cxx26", 1 }, { "-Warray-parameter", 1 }, { "-Wasm", 1 }, { "-Wasm-operand-widths", 1 }, { "-Wassign-enum", 1 }, { "-Wassume", 1 },
    { "-Wat-protocol", 1 }, { "-Watimport-in-framework-header", 1 }, { "-Watomic-access", 1 }, { "-Watomic-alignment", 1 },
    { "-Watomic-implicit-seq-cst", 1 }, { "-Watomic-memory-ordering", 1 }, { "-Watomic-properties", 1 },
    { "-Watomic-property-with-user-defined-accessor", 1 }, { "-Wattribute-alias", 1 }, { "-Wattribute-packed-for-bitfield", 1 },
    { "-Wattribute-preprocessor-tokens", 1 }, { "-Wattribute-statement", 1 }, { "-Wattribute-warning", 1 }, { "-Wattributes", 1 },
    { "-Wauto-decl-extensions", 1 }, { "-Wauto-disable-vptr-sanitizer", 1 }, { "-Wauto-import", 1 }, { "-Wauto-storage-class", 1 }, { "-Wauto-var-id", 1 },
    { "-Wavailability", 1 }, { "-Wavr-rtlib-linking-quirks", 1 }, { "-Wbackend-plugin", 1 }, { "-Wbackslash-newline-escape", 1 },
    { "-Wbad-function-cast", 1 }, { "-Wbinary-literal", 1 }, { "-Wbind-to-temporary-copy", 1 }, { "-Wbit-int-extension", 1 },
    { "-Wbitfield-constant-conversion", 1 }, { "-Wbitfield-enum-conversion", 1 }, { "-Wbitfield-width", 1 }, { "-Wbitwise-conditional-parentheses", 1 },
    { "-Wbitwise-instead-of-logical", 1 }, { "-Wbitwise-op-parentheses", 1 }, { "-Wblock-capture-autoreleasing", 1 }, { "-Wbool-conversion", 1 },
    { "-Wbool-conversions", 1 }, { "-Wbool-operation", 1 }, { "-Wbounds-safety-counted-by-elt-type-unknown-size", 1 }, { "-Wbraced-scalar-init", 1 },
    { "-Wbranch-protection", 1 }, { "-Wbridge-cast", 1 }, { "-Wbuiltin-assume-aligned-alignment", 1 }, { "-Wbuiltin-macro-redefined", 1 },
    { "-Wbuiltin-memcpy-chk-size", 1 }, { "-Wbuiltin-requires-header", 1 }, { "-Wc++-compat", 1 }, { "-Wc++-hidden-decl", 1 }, { "-Wc++-keyword", 1 },
    { "-Wc++-unterminated-string-initialization", 1 }, { "-Wc++0x-compat", 1 }, { "-Wc++0x-extensions", 1 }, { "-Wc++0x-narrowing", 1 },
    { "-Wc++11-compat", 1 }, { "-Wc++11-compat-deprecated-writable-strings", 1 }, { "-Wc++11-compat-pedantic", 1 },
    { "-Wc++11-compat-reserved-user-defined-literal", 1 }, { "-Wc++11-extensions", 1 }, { "-Wc++11-extra-semi", 1 }, { "-Wc++11-inline-namespace", 1 },
    { "-Wc++11-long-long", 1 }, { "-Wc++11-narrowing", 1 }, { "-Wc++11-narrowing-const-reference", 1 }, { "-Wc++14-attribute-extensions", 1 },
    { "-Wc++14-binary-literal", 1 }, { "-Wc++14-compat", 1 }, { "-Wc++14-compat-pedantic", 1 }, { "-Wc++14-extensions", 1 },
    { "-Wc++17-attribute-extensions", 1 }, { "-Wc++17-compat", 1 }, { "-Wc++17-compat-mangling", 1 }, { "-Wc++17-compat-pedantic", 1 },
    { "-Wc++17-extensions", 1 }, { "-Wc++1y-extensions", 1 }, { "-Wc++1z-compat", 1 }, { "-Wc++1z-compat-mangling", 1 }, { "-Wc++1z-extensions", 1 },
    { "-Wc++20-attribute-extensions", 1 }, { "-Wc++20-compat", 1 }, { "-Wc++20-compat-pedantic", 1 }, { "-Wc++20-designator", 1 },
    { "-Wc++20-extensions", 1 }, { "-Wc++23-attribute-extensions", 1 }, { "-Wc++23-compat", 1 }, { "-Wc++23-extensions", 1 },
    { "-Wc++23-lambda-attributes", 1 }, { "-Wc++26-extensions", 1 }, { "-Wc++2a-compat", 1 }, { "-Wc++2a-compat-pedantic", 1 }, { "-Wc++2a-extensions", 1 },
    { "-Wc++2b-extensions", 1 }, { "-Wc++2c-compat", 1 }, { "-Wc++2c-extensions", 1 }, { "-Wc++2d-extensions", 1 }, { "-Wc++98-c++11-c++14-c++17-compat", 1 },
    { "-Wc++98-c++11-c++14-c++17-compat-pedantic", 1 }, { "-Wc++98-c++11-c++14-compat", 1 }, { "-Wc++98-c++11-c++14-compat-pedantic", 1 },
    { "-Wc++98-c++11-compat", 1 }, { "-Wc++98-c++11-compat-binary-literal", 1 }, { "-Wc++98-c++11-compat-pedantic", 1 }, { "-Wc++98-compat", 1 },
    { "-Wc++98-compat-bind-to-temporary-copy", 1 }, { "-Wc++98-compat-extra-semi", 1 }, { "-Wc++98-compat-local-type-template-args", 1 },
    { "-Wc++98-compat-pedantic", 1 }, { "-Wc++98-compat-unnamed-type-template-args", 1 }, { "-Wc-attribute-extension", 1 }, { "-Wc11-extensions", 1 },
    { "-Wc23-compat", 1 }, { "-Wc23-extensions", 1 }, { "-Wc2x-compat", 1 }, { "-Wc2x-extensions", 1 }, { "-Wc2y-extensions", 1 }, { "-Wc99-compat", 1 },
    { "-Wc99-designator", 1 }, { "-Wc99-extensions", 1 }, { "-Wcall-to-pure-virtual-from-ctor-dtor", 1 }, { "-Wcalled-once-parameter", 1 },
    { "-Wcast-align", 1 }, { "-Wcast-calling-convention", 1 }, { "-Wcast-function-type", 1 }, { "-Wcast-function-type-mismatch", 1 },
    { "-Wcast-function-type-strict", 1 }, { "-Wcast-of-sel-type", 1 }, { "-Wcast-qual", 1 }, { "-Wcast-qual-unrelated", 1 }, { "-Wcfi-unchecked-callee", 1 },
    { "-Wchar-align", 1 }, { "-Wchar-subscripts", 1 }, { "-Wcharacter-conversion", 1 }, { "-Wclang-cl-pch", 1 }, { "-Wclass-conversion", 1 },
    { "-Wclass-varargs", 1 }, { "-Wcmse-union-leak", 1 }, { "-Wcomma", 1 }, { "-Wcomment", 1 }, { "-Wcomments", 1 },
    { "-Wcompare-distinct-pointer-types", 1 }, { "-Wcompletion-handler", 1 }, { "-Wcomplex-component-init", 1 }, { "-Wcompound-token-split", 1 },
    { "-Wcompound-token-split-by-macro", 1 }, { "-Wcompound-token-split-by-space", 1 }, { "-Wconditional-type-mismatch", 1 },
    { "-Wconditional-uninitialized", 1 }, { "-Wconfig-macros", 1 }, { "-Wconstant-conversion", 1 }, { "-Wconstant-evaluated", 1 },
    { "-Wconstant-logical-operand", 1 }, { "-Wconstexpr-not-const", 1 }, { "-Wconsumed", 1 }, { "-Wconversion", 1 }, { "-Wconversion-null", 1 },
    { "-Wcoro-non-aligned-allocation-function", 1 }, { "-Wcoro-type-aware-allocation-function", 1 }, { "-Wcoroutine", 1 },
    { "-Wcoroutine-missing-unhandled-exception", 1 }, { "-Wcoroutines-unsupported-target", 1 }, { "-Wcovered-switch-default", 1 }, { "-Wcpp", 1 },
    { "-Wcstring-format-directive", 1 }, { "-Wctad-maybe-unsupported", 1 }, { "-Wctor-dtor-privacy", 1 }, { "-Wctu", 1 }, { "-Wcuda-compat", 1 },
    { "-Wcustom-atomic-properties", 1 }, { "-Wcxx-attribute-extension", 1 }, { "-Wdangling", 1 }, { "-Wdangling-assignment", 1 },
    { "-Wdangling-assignment-gsl", 1 }, { "-Wdangling-capture", 1 }, { "-Wdangling-else", 1 }, { "-Wdangling-field", 1 }, { "-Wdangling-gsl", 1 },
    { "-Wdangling-initializer-list", 1 }, { "-Wdarwin-sdk-settings", 1 }, { "-Wdate-time", 1 }, { "-Wdealloc-in-category", 1 },
    { "-Wdebug-compression-unavailable", 1 }, { "-Wdeclaration-after-statement", 1 }, { "-Wdecls-in-multiple-modules", 1 }, { "-Wdefault-const-init", 1 },
    { "-Wdefault-const-init-field", 1 }, { "-Wdefault-const-init-field-unsafe", 1 }, { "-Wdefault-const-init-unsafe", 1 }, { "-Wdefault-const-init-var", 1 },
    { "-Wdefault-const-init-var-unsafe", 1 }, { "-Wdefaulted-function-deleted", 1 }, { "-Wdelayed-template-parsing-in-cxx20", 1 },
    { "-Wdelegating-ctor-cycles", 1 }, { "-Wdelete-abstract-non-virtual-dtor", 1 }, { "-Wdelete-incomplete", 1 },
    { "-Wdelete-non-abstract-non-virtual-dtor", 1 }, { "-Wdelete-non-virtual-dtor", 1 }, { "-Wdelimited-escape-sequence-extension", 1 },
    { "-Wdeprecate-lax-vec-conv-all", 1 }, { "-Wdeprecated", 1 }, { "-Wdeprecated-altivec-src-compat", 1 }, { "-Wdeprecated-anon-enum-enum-conversion", 1 },
    { "-Wdeprecated-array-compare", 1 }, { "-Wdeprecated-attributes", 1 }, { "-Wdeprecated-builtins", 1 }, { "-Wdeprecated-comma-subscript", 1 },
    { "-Wdeprecated-copy", 1 }, { "-Wdeprecated-copy-dtor", 1 }, { "-Wdeprecated-copy-with-dtor", 1 }, { "-Wdeprecated-copy-with-user-provided-copy", 1 },
    { "-Wdeprecated-copy-with-user-provided-dtor", 1 }, { "-Wdeprecated-coroutine", 1 }, { "-Wdeprecated-declarations", 1 },
    { "-Wdeprecated-declarations-switch-case", 1 }, { "-Wdeprecated-dynamic-exception-spec", 1 }, { "-Wdeprecated-enum-compare", 1 },
    { "-Wdeprecated-enum-compare-conditional", 1 }, { "-Wdeprecated-enum-enum-conversion", 1 }, { "-Wdeprecated-enum-float-conversion", 1 },
    { "-Wdeprecated-implementations", 1 }, { "-Wdeprecated-increment-bool", 1 }, { "-Wdeprecated-literal-operator", 1 },
    { "-Wdeprecated-missing-comma-variadic-parameter", 1 }, { "-Wdeprecated-module-dot-map", 1 }, { "-Wdeprecated-non-prototype", 1 },
    { "-Wdeprecated-objc-isa-usage", 1 }, { "-Wdeprecated-objc-pointer-introspection", 1 }, { "-Wdeprecated-objc-pointer-introspection-performSelector", 1 },
    { "-Wdeprecated-octal-literals", 1 }, { "-Wdeprecated-ofast", 1 }, { "-Wdeprecated-pragma", 1 }, { "-Wdeprecated-redundant-constexpr-static-def", 1 },
    { "-Wdeprecated-register", 1 }, { "-Wdeprecated-this-capture", 1 }, { "-Wdeprecated-type", 1 }, { "-Wdeprecated-volatile", 1 },
    { "-Wdeprecated-writable-strings", 1 }, { "-Wdirect-ivar-access", 1 }, { "-Wdisabled-macro-expansion", 1 }, { "-Wdisabled-optimization", 1 },
    { "-Wdiscard-qual", 1 }, { "-Wdistributed-object-modifiers", 1 }, { "-Wdiv-by-zero", 1 }, { "-Wdivision-by-zero", 1 },
    { "-Wdll-attribute-on-redeclaration", 1 }, { "-Wdllexport-explicit-instantiation", 1 }, { "-Wdllexport-explicit-instantiation-decl", 1 },
    { "-Wdllimport-static-field-def", 1 }, { "-Wdocumentation", 1 }, { "-Wdocumentation-deprecated-sync", 1 }, { "-Wdocumentation-html", 1 },
    { "-Wdocumentation-pedantic", 1 }, { "-Wdocumentation-unknown-command", 1 }, { "-Wdollar-in-identifier-extension", 1 }, { "-Wdouble-promotion", 1 },
    { "-Wdtor-name", 1 }, { "-Wdtor-typedef", 1 }, { "-Wduplicate-decl-specifier", 1 }, { "-Wduplicate-enum", 1 }, { "-Wduplicate-header-ownership", 1 },
    { "-Wduplicate-method-arg", 1 }, { "-Wduplicate-method-match", 1 }, { "-Wduplicate-protocol", 1 }, { "-Wdxil-validation", 1 },
    { "-Wdynamic-class-memaccess", 1 }, { "-Wdynamic-exception-spec", 1 }, { "-Weager-load-cxx-named-modules", 1 }, { "-Weffc++", 1 },
    { "-Welaborated-enum-base", 1 }, { "-Welaborated-enum-class", 1 }, { "-Wembedded-directive", 1 }, { "-Wempty-body", 1 }, { "-Wempty-decomposition", 1 },
    { "-Wempty-init-stmt", 1 }, { "-Wempty-translation-unit", 1 }, { "-Wencode-type", 1 }, { "-Wendif-labels", 1 }, { "-Wenum-compare", 1 },
    { "-Wenum-compare-conditional", 1 }, { "-Wenum-compare-switch", 1 }, { "-Wenum-compare-typo", 1 }, { "-Wenum-conversion", 1 },
    { "-Wenum-enum-conversion", 1 }, { "-Wenum-float-conversion", 1 }, { "-Wenum-too-large", 1 }, { "-Wexceptions", 1 }, { "-Wexcess-initializers", 1 },
    { "-Wexcessive-regsave", 1 }, { "-Wexit-time-destructors", 1 }, { "-Wexpansion-stmt-missing-braces", 1 }, { "-Wexpansion-to-defined", 1 },
    { "-Wexperimental-header-units", 1 }, { "-Wexperimental-option", 1 }, { "-Wexplicit-initialize-call", 1 }, { "-Wexplicit-ownership-type", 1 },
    { "-Wexplicit-specialization-storage-class", 1 }, { "-Wexport-unnamed", 1 }, { "-Wext-cxx-type-aware-allocators", 1 }, { "-Wextern-c-compat", 1 },
    { "-Wextern-initializer", 1 }, { "-Wextra", 1 }, { "-Wextra-qualification", 1 }, { "-Wextra-semi", 1 }, { "-Wextra-semi-stmt", 1 },
    { "-Wextra-tokens", 1 }, { "-Wextractapi-misuse", 1 }, { "-Wextraneous-template-head", 1 }, { "-Wfinal-dtor-non-final-class", 1 }, { "-Wfinal-macro", 1 },
    { "-Wfixed-point-overflow", 1 }, { "-Wflag-enum", 1 }, { "-Wflexible-array-extensions", 1 }, { "-Wfloat-conversion", 1 }, { "-Wfloat-equal", 1 },
    { "-Wfloat-overflow-conversion", 1 }, { "-Wfloat-zero-conversion", 1 }, { "-Wfor-loop-analysis", 1 }, { "-Wformat", 1 }, { "-Wformat-extra-args", 1 },
    { "-Wformat-insufficient-args", 1 }, { "-Wformat-invalid-specifier", 1 }, { "-Wformat-non-iso", 1 }, { "-Wformat-nonliteral", 1 },
    { "-Wformat-overflow", 1 }, { "-Wformat-overflow-non-kprintf", 1 }, { "-Wformat-pedantic", 1 }, { "-Wformat-security", 1 }, { "-Wformat-signedness", 1 },
    { "-Wformat-truncation", 1 }, { "-Wformat-truncation-non-kprintf", 1 }, { "-Wformat-type-confusion", 1 }, { "-Wformat-y2k", 1 },
    { "-Wformat-zero-length", 1 }, { "-Wformat=2", 1 }, { "-Wfortify-source", 1 }, { "-Wfour-char-constants", 1 }, { "-Wframe-address", 1 },
    { "-Wframe-larger-than", 1 }, { "-Wframe-larger-than=", 1 }, { "-Wframework-include-private-from-public", 1 }, { "-Wfree-nonheap-object", 1 },
    { "-Wfriend-enum", 1 }, { "-Wfunction-def-in-objc-container", 1 }, { "-Wfunction-effect-redeclarations", 1 }, { "-Wfunction-effects", 1 },
    { "-Wfunction-multiversion", 1 }, { "-Wfuse-ld-path", 1 }, { "-Wfuture-attribute-extensions", 1 }, { "-Wfuture-compat", 1 }, { "-Wgcc-compat", 1 },
    { "-Wgcc-install-dir-libstdcxx", 1 }, { "-Wglobal-constructors", 1 }, { "-Wglobal-isel", 1 }, { "-Wgnu", 1 }, { "-Wgnu-alignof-expression", 1 },
    { "-Wgnu-anonymous-struct", 1 }, { "-Wgnu-array-member-paren-init", 1 }, { "-Wgnu-auto-type", 1 }, { "-Wgnu-binary-literal", 1 },
    { "-Wgnu-case-range", 1 }, { "-Wgnu-complex-integer", 1 }, { "-Wgnu-compound-literal-initializer", 1 }, { "-Wgnu-conditional-omitted-operand", 1 },
    { "-Wgnu-designator", 1 }, { "-Wgnu-empty-initializer", 1 }, { "-Wgnu-empty-struct", 1 }, { "-Wgnu-flexible-array-initializer", 1 },
    { "-Wgnu-flexible-array-union-member", 1 }, { "-Wgnu-folding-constant", 1 }, { "-Wgnu-imaginary-constant", 1 }, { "-Wgnu-include-next", 1 },
    { "-Wgnu-inline-cpp-without-extern", 1 }, { "-Wgnu-label-as-value", 1 }, { "-Wgnu-line-marker", 1 }, { "-Wgnu-null-pointer-arithmetic", 1 },
    { "-Wgnu-offsetof-extensions", 1 }, { "-Wgnu-pointer-arith", 1 }, { "-Wgnu-redeclared-enum", 1 }, { "-Wgnu-statement-expression", 1 },
    { "-Wgnu-statement-expression-from-macro-expansion", 1 }, { "-Wgnu-static-float-init", 1 }, { "-Wgnu-string-literal-operator-template", 1 },
    { "-Wgnu-union-cast", 1 }, { "-Wgnu-variable-sized-type-not-at-end", 1 }, { "-Wgnu-zero-line-directive", 1 },
    { "-Wgnu-zero-variadic-macro-arguments", 1 }, { "-Wgpu-maybe-wrong-side", 1 }, { "-Wgroupshared-initializer", 1 }, { "-Wheader-guard", 1 },
    { "-Wheader-hygiene", 1 }, { "-Whexagon-reserved-caller-saved", 1 }, { "-Whigher-precision-for-complex-division", 1 }, { "-Whip-deprecated-builtins", 1 },
    { "-Whip-omp-target-directives", 1 }, { "-Whip-only", 1 }, { "-Whlsl-202y-extensions", 1 }, { "-Whlsl-availability", 1 },
    { "-Whlsl-dxc-compatability", 1 }, { "-Whlsl-explicit-binding", 1 }, { "-Whlsl-extensions", 1 }, { "-Whlsl-implicit-binding", 1 },
    { "-Widiomatic-parentheses", 1 }, { "-Wignored-attributes", 1 }, { "-Wignored-availability-without-sdk-settings", 1 },
    { "-Wignored-base-class-qualifiers", 1 }, { "-Wignored-gch", 1 }, { "-Wignored-optimization-argument", 1 }, { "-Wignored-pragma-intrinsic", 1 },
    { "-Wignored-pragma-optimize", 1 }, { "-Wignored-pragmas", 1 }, { "-Wignored-qualifiers", 1 }, { "-Wignored-reference-qualifiers", 1 },
    { "-Wimplicit", 1 }, { "-Wimplicit-atomic-properties", 1 }, { "-Wimplicit-const-int-float-conversion", 1 },
    { "-Wimplicit-conversion-floating-point-to-bool", 1 }, { "-Wimplicit-enum-enum-cast", 1 }, { "-Wimplicit-exception-spec-mismatch", 1 },
    { "-Wimplicit-fallthrough", 1 }, { "-Wimplicit-fallthrough-per-function", 1 }, { "-Wimplicit-fixed-point-conversion", 1 },
    { "-Wimplicit-float-conversion", 1 }, { "-Wimplicit-function-declaration", 1 }, { "-Wimplicit-int", 1 }, { "-Wimplicit-int-conversion", 1 },
    { "-Wimplicit-int-conversion-on-negation", 1 }, { "-Wimplicit-int-enum-cast", 1 }, { "-Wimplicit-int-float-conversion", 1 },
    { "-Wimplicit-overflow-behavior-conversion", 1 }, { "-Wimplicit-overflow-behavior-conversion-assignment", 1 },
    { "-Wimplicit-overflow-behavior-conversion-assignment-pedantic", 1 }, { "-Wimplicit-overflow-behavior-conversion-function-boundary", 1 },
    { "-Wimplicit-overflow-behavior-conversion-function-boundary-pedantic", 1 }, { "-Wimplicit-overflow-behavior-conversion-pedantic", 1 },
    { "-Wimplicit-retain-self", 1 }, { "-Wimplicit-void-ptr-cast", 1 }, { "-Wimplicitly-unsigned-literal", 1 }, { "-Wimport", 1 },
    { "-Wimport-implementation-partition-unit-in-interface-unit", 1 }, { "-Wimport-preprocessor-directive-pedantic", 1 }, { "-Winaccessible-base", 1 },
    { "-Winclude-angled-in-module-purview", 1 }, { "-Winclude-next-absolute-path", 1 }, { "-Winclude-next-outside-header", 1 },
    { "-Wincompatible-exception-spec", 1 }, { "-Wincompatible-function-pointer-types", 1 }, { "-Wincompatible-function-pointer-types-strict", 1 },
    { "-Wincompatible-library-redeclaration", 1 }, { "-Wincompatible-ms-pragma-section", 1 }, { "-Wincompatible-ms-struct", 1 },
    { "-Wincompatible-pointer-types", 1 }, { "-Wincompatible-pointer-types-discards-overflow-behavior", 1 },
    { "-Wincompatible-pointer-types-discards-qualifiers", 1 }, { "-Wincompatible-property-type", 1 }, { "-Wincompatible-sysroot", 1 },
    { "-Wincomplete-framework-module-declaration", 1 }, { "-Wincomplete-implementation", 1 }, { "-Wincomplete-module", 1 },
    { "-Wincomplete-setjmp-declaration", 1 }, { "-Wincomplete-umbrella", 1 }, { "-Winconsistent-dllimport", 1 },
    { "-Winconsistent-missing-destructor-override", 1 }, { "-Winconsistent-missing-override", 1 }, { "-Wincrement-bool", 1 }, { "-Winfinite-recursion", 1 },
    { "-Winit-priority-reserved", 1 }, { "-Winit-self", 1 }, { "-Winitializer-overrides", 1 }, { "-Winjected-class-name", 1 }, { "-Winline", 1 },
    { "-Winline-asm", 1 }, { "-Winline-namespace-reopened-noninline", 1 }, { "-Winline-new-delete", 1 }, { "-Winstallapi-violation", 1 },
    { "-Winstantiation-after-specialization", 1 }, { "-Wint-conversion", 1 }, { "-Wint-conversions", 1 }, { "-Wint-in-bool-context", 1 },
    { "-Wint-to-pointer-cast", 1 }, { "-Wint-to-void-pointer-cast", 1 }, { "-Winteger-overflow", 1 }, { "-Winvalid-command-line-argument", 1 },
    { "-Winvalid-constexpr", 1 }, { "-Winvalid-feature-combination", 1 }, { "-Winvalid-gnu-asm-cast", 1 }, { "-Winvalid-iboutlet", 1 },
    { "-Winvalid-initializer-from-system-header", 1 }, { "-Winvalid-ios-deployment-target", 1 }, { "-Winvalid-no-builtin-names", 1 },
    { "-Winvalid-noreturn", 1 }, { "-Winvalid-offsetof", 1 }, { "-Winvalid-or-nonexistent-directory", 1 }, { "-Winvalid-partial-specialization", 1 },
    { "-Winvalid-pch", 1 }, { "-Winvalid-pp-token", 1 }, { "-Winvalid-source-encoding", 1 }, { "-Winvalid-specialization", 1 },
    { "-Winvalid-static-assert-message", 1 }, { "-Winvalid-token-paste", 1 }, { "-Winvalid-unevaluated-string", 1 }, { "-Winvalid-utf8", 1 },
    { "-Winvalid-version-availability", 1 }, { "-Wjump-misses-init", 1 }, { "-Wjump-seh-finally", 1 }, { "-Wkeyword-compat", 1 }, { "-Wkeyword-macro", 1 },
    { "-Wknr-promoted-parameter", 1 }, { "-Wlanguage-extension-token", 1 }, { "-Wlarge-by-value-copy", 1 }, { "-Wlegacy-constant-register-binding", 1 },
    { "-Wliblto", 1 }, { "-Wlifetime-safety", 1 }, { "-Wlifetime-safety-all", 1 }, { "-Wlifetime-safety-annotation-placement", 1 },
    { "-Wlifetime-safety-annotation-verification", 1 }, { "-Wlifetime-safety-cross-tu-constructor-suggestions", 1 },
    { "-Wlifetime-safety-cross-tu-misplaced-lifetimebound", 1 }, { "-Wlifetime-safety-cross-tu-suggestions", 1 }, { "-Wlifetime-safety-dangling-field", 1 },
    { "-Wlifetime-safety-dangling-field-moved", 1 }, { "-Wlifetime-safety-dangling-global", 1 }, { "-Wlifetime-safety-dangling-global-moved", 1 },
    { "-Wlifetime-safety-inapplicable-lifetimebound", 1 }, { "-Wlifetime-safety-intra-tu-constructor-suggestions", 1 },
    { "-Wlifetime-safety-intra-tu-misplaced-lifetimebound", 1 }, { "-Wlifetime-safety-intra-tu-suggestions", 1 }, { "-Wlifetime-safety-invalidation", 1 },
    { "-Wlifetime-safety-lifetimebound-violation", 1 }, { "-Wlifetime-safety-misplaced-lifetimebound", 1 }, { "-Wlifetime-safety-noescape", 1 },
    { "-Wlifetime-safety-permissive", 1 }, { "-Wlifetime-safety-return-stack-addr", 1 }, { "-Wlifetime-safety-return-stack-addr-moved", 1 },
    { "-Wlifetime-safety-strict", 1 }, { "-Wlifetime-safety-suggestions", 1 }, { "-Wlifetime-safety-use-after-free", 1 },
    { "-Wlifetime-safety-use-after-scope", 1 }, { "-Wlifetime-safety-use-after-scope-moved", 1 }, { "-Wlifetime-safety-validations", 1 },
    { "-Wlinker-warnings", 1 }, { "-Wliteral-conversion", 1 }, { "-Wliteral-range", 1 }, { "-Wlocal-type-template-args", 1 },
    { "-Wlogical-not-parentheses", 1 }, { "-Wlogical-op-parentheses", 1 }, { "-Wlong-long", 1 }, { "-Wloop-analysis", 1 }, { "-Wmacro-redefined", 1 },
    { "-Wmain", 1 }, { "-Wmain-attached-to-named-module", 1 }, { "-Wmain-return-type", 1 }, { "-Wmalformed-warning-check", 1 },
    { "-Wmany-braces-around-scalar-init", 1 }, { "-Wmath-errno-enabled-with-veclib", 1 }, { "-Wmathematical-notation-identifier-extension", 1 },
    { "-Wmatrix-conversion", 1 }, { "-Wmatrix-conversions", 1 }, { "-Wmax-tokens", 1 }, { "-Wmax-unsigned-zero", 1 }, { "-Wmemset-transposed-args", 1 },
    { "-Wmemsize-comparison", 1 }, { "-Wmethod-signatures", 1 }, { "-Wmicrosoft", 1 }, { "-Wmicrosoft-abstract", 1 }, { "-Wmicrosoft-anon-tag", 1 },
    { "-Wmicrosoft-cast", 1 }, { "-Wmicrosoft-charize", 1 }, { "-Wmicrosoft-comment-paste", 1 }, { "-Wmicrosoft-const-init", 1 },
    { "-Wmicrosoft-cpp-macro", 1 }, { "-Wmicrosoft-default-arg-redefinition", 1 }, { "-Wmicrosoft-drectve-section", 1 }, { "-Wmicrosoft-empty-enum", 1 },
    { "-Wmicrosoft-end-of-file", 1 }, { "-Wmicrosoft-enum-forward-reference", 1 }, { "-Wmicrosoft-enum-value", 1 }, { "-Wmicrosoft-exception-spec", 1 },
    { "-Wmicrosoft-exists", 1 }, { "-Wmicrosoft-explicit-constructor-call", 1 }, { "-Wmicrosoft-extra-qualification", 1 }, { "-Wmicrosoft-fixed-enum", 1 },
    { "-Wmicrosoft-flexible-array", 1 }, { "-Wmicrosoft-goto", 1 }, { "-Wmicrosoft-inaccessible-base", 1 }, { "-Wmicrosoft-include", 1 },
    { "-Wmicrosoft-init-from-predefined", 1 }, { "-Wmicrosoft-inline-on-non-function", 1 }, { "-Wmicrosoft-mutable-reference", 1 },
    { "-Wmicrosoft-pure-definition", 1 }, { "-Wmicrosoft-redeclare-static", 1 }, { "-Wmicrosoft-sealed", 1 },
    { "-Wmicrosoft-string-literal-from-predefined", 1 }, { "-Wmicrosoft-template", 1 }, { "-Wmicrosoft-template-shadow", 1 },
    { "-Wmicrosoft-union-member-reference", 1 }, { "-Wmicrosoft-unqualified-friend", 1 }, { "-Wmicrosoft-using-decl", 1 },
    { "-Wmicrosoft-void-pseudo-dtor", 1 }, { "-Wmisexpect", 1 }, { "-Wmisleading-indentation", 1 }, { "-Wmismatched-new-delete", 1 },
    { "-Wmismatched-parameter-types", 1 }, { "-Wmismatched-return-types", 1 }, { "-Wmismatched-tags", 1 }, { "-Wmissing-braces", 1 },
    { "-Wmissing-constinit", 1 }, { "-Wmissing-declarations", 1 }, { "-Wmissing-designated-field-initializers", 1 }, { "-Wmissing-exception-spec", 1 },
    { "-Wmissing-field-initializers", 1 }, { "-Wmissing-format-attribute", 1 }, { "-Wmissing-include-dirs", 1 }, { "-Wmissing-method-return-type", 1 },
    { "-Wmissing-multilib", 1 }, { "-Wmissing-noescape", 1 }, { "-Wmissing-noreturn", 1 }, { "-Wmissing-prototype-for-cc", 1 }, { "-Wmissing-prototypes", 1 },
    { "-Wmissing-selector-name", 1 }, { "-Wmissing-sysroot", 1 }, { "-Wmissing-template-arg-list-after-template-kw", 1 },
    { "-Wmissing-variable-declarations", 1 }, { "-Wmisspelled-assumption", 1 }, { "-Wmix-packoffset", 1 },
    { "-Wmmap-deprecated-symlink-to-modular-header", 1 }, { "-Wmodule-conflict", 1 }, { "-Wmodule-file-config-mismatch", 1 },
    { "-Wmodule-file-extension", 1 }, { "-Wmodule-file-mapping-mismatch", 1 }, { "-Wmodule-import-in-extern-c", 1 },
    { "-Wmodule-map-path-outside-directory", 1 }, { "-Wmodules-ambiguous-internal-linkage", 1 }, { "-Wmodules-import-nested-redundant", 1 }, { "-Wmost", 1 },
    { "-Wmove", 1 }, { "-Wms-bitfield-padding", 1 }, { "-Wmsvc-include", 1 }, { "-Wmsvc-not-found", 1 }, { "-Wmulti-gpu", 1 }, { "-Wmultichar", 1 },
    { "-Wmultilib-not-found", 1 }, { "-Wmultiple-move-vbase", 1 }, { "-Wnan-infinity-disabled", 1 }, { "-Wnarrowing", 1 }, { "-Wnested-anon-types", 1 },
    { "-Wnested-externs", 1 }, { "-Wnew-returns-null", 1 }, { "-Wnewline-eof", 1 }, { "-Wnoderef", 1 }, { "-Wnoexcept-type", 1 },
    { "-Wnon-c-typedef-for-linkage", 1 }, { "-Wnon-gcc", 1 }, { "-Wnon-literal-null-conversion", 1 }, { "-Wnon-modular-include-in-framework-module", 1 },
    { "-Wnon-modular-include-in-module", 1 }, { "-Wnon-pod-varargs", 1 }, { "-Wnon-power-of-two-alignment", 1 }, { "-Wnon-virtual-dtor", 1 },
    { "-Wnonnull", 1 }, { "-Wnonportable-cfstrings", 1 }, { "-Wnonportable-include-path", 1 }, { "-Wnonportable-include-path-separator", 1 },
    { "-Wnonportable-private-apinotes-path", 1 }, { "-Wnonportable-private-system-apinotes-path", 1 }, { "-Wnonportable-sycl", 1 },
    { "-Wnonportable-system-include-path", 1 }, { "-Wnonportable-vector-initialization", 1 }, { "-Wnontrivial-memaccess", 1 }, { "-Wnontrivial-memcall", 1 },
    { "-Wnrvo", 1 }, { "-Wnsconsumed-mismatch", 1 }, { "-Wnsreturns-mismatch", 1 }, { "-Wnull-arithmetic", 1 }, { "-Wnull-character", 1 },
    { "-Wnull-conversion", 1 }, { "-Wnull-dereference", 1 }, { "-Wnull-pointer-arithmetic", 1 }, { "-Wnull-pointer-subtraction", 1 }, { "-Wnullability", 1 },
    { "-Wnullability-completeness", 1 }, { "-Wnullability-completeness-on-arrays", 1 }, { "-Wnullability-declspec", 1 }, { "-Wnullability-extension", 1 },
    { "-Wnullability-inferred-on-nested-type", 1 }, { "-Wnullable-to-nonnull-conversion", 1 }, { "-Wnvcc-compat", 1 },
    { "-Wobjc-autosynthesis-property-ivar-name-match", 1 }, { "-Wobjc-bool-constant-conversion", 1 }, { "-Wobjc-boxing", 1 },
    { "-Wobjc-circular-container", 1 }, { "-Wobjc-cocoa-api", 1 }, { "-Wobjc-designated-initializers", 1 }, { "-Wobjc-dictionary-duplicate-keys", 1 },
    { "-Wobjc-duplicate-category-definition", 1 }, { "-Wobjc-flexible-array", 1 }, { "-Wobjc-forward-class-redefinition", 1 },
    { "-Wobjc-interface-ivars", 1 }, { "-Wobjc-literal-compare", 1 }, { "-Wobjc-literal-conversion", 1 }, { "-Wobjc-macro-redefinition", 1 },
    { "-Wobjc-messaging-id", 1 }, { "-Wobjc-method-access", 1 }, { "-Wobjc-missing-property-synthesis", 1 }, { "-Wobjc-missing-super-calls", 1 },
    { "-Wobjc-multiple-method-names", 1 }, { "-Wobjc-noncopy-retain-block-property", 1 }, { "-Wobjc-nonunified-exceptions", 1 },
    { "-Wobjc-property-assign-on-object-type", 1 }, { "-Wobjc-property-implementation", 1 }, { "-Wobjc-property-implicit-mismatch", 1 },
    { "-Wobjc-property-matches-cocoa-ownership-rule", 1 }, { "-Wobjc-property-no-attribute", 1 }, { "-Wobjc-property-synthesis", 1 },
    { "-Wobjc-protocol-method-implementation", 1 }, { "-Wobjc-protocol-property-synthesis", 1 }, { "-Wobjc-protocol-qualifiers", 1 },
    { "-Wobjc-readonly-with-setter-property", 1 }, { "-Wobjc-redundant-api-use", 1 }, { "-Wobjc-redundant-literal-use", 1 }, { "-Wobjc-root-class", 1 },
    { "-Wobjc-signed-char-bool", 1 }, { "-Wobjc-signed-char-bool-implicit-float-conversion", 1 }, { "-Wobjc-signed-char-bool-implicit-int-conversion", 1 },
    { "-Wobjc-string-compare", 1 }, { "-Wobjc-string-concatenation", 1 }, { "-Wobjc-unsafe-perform-selector", 1 }, { "-Woctal-prefix-extension", 1 },
    { "-Wodr", 1 }, { "-Wold-style-cast", 1 }, { "-Wold-style-definition", 1 }, { "-Wopenacc", 1 }, { "-Wopenacc-cache-var-inside-loop", 1 },
    { "-Wopenacc-confusing-routine-name", 1 }, { "-Wopenacc-deprecated-clause-alias", 1 }, { "-Wopenacc-extension", 1 },
    { "-Wopenacc-self-if-potential-conflict", 1 }, { "-Wopenacc-var-lacks-operation", 1 }, { "-Wopenacc-var-non-const-array", 1 },
    { "-Wopencl-unsupported-rgba", 1 }, { "-Wopenmp", 1 }, { "-Wopenmp-51-extensions", 1 }, { "-Wopenmp-clauses", 1 }, { "-Wopenmp-extensions", 1 },
    { "-Wopenmp-future", 1 }, { "-Wopenmp-loop-form", 1 }, { "-Wopenmp-mapping", 1 }, { "-Wopenmp-target", 1 }, { "-Wopenmp-target-exception", 1 },
    { "-Woption-ignored", 1 }, { "-Wordered-compare-function-pointers", 1 }, { "-Wout-of-line-declaration", 1 }, { "-Wout-of-scope-function", 1 },
    { "-Wover-aligned", 1 }, { "-Woverflow", 1 }, { "-Woverflow-behavior-attribute-ignored", 1 }, { "-Woverflow-behavior-conversion", 1 },
    { "-Woverlength-strings", 1 }, { "-Woverloaded-shift-op-parentheses", 1 }, { "-Woverloaded-virtual", 1 }, { "-Woverride-init", 1 },
    { "-Woverride-module", 1 }, { "-Woverriding-complex-range", 1 }, { "-Woverriding-deployment-version", 1 }, { "-Woverriding-method-mismatch", 1 },
    { "-Woverriding-option", 1 }, { "-Wpacked", 1 }, { "-Wpacked-non-pod", 1 }, { "-Wpadded", 1 }, { "-Wpadded-bitfield", 1 }, { "-Wparentheses", 1 },
    { "-Wparentheses-equality", 1 }, { "-Wpartial-availability", 1 }, { "-Wpass-failed", 1 }, { "-Wpch-date-time", 1 }, { "-Wpch-vfs-diff", 1 },
    { "-Wpedantic", 1 }, { "-Wpedantic-core-features", 1 }, { "-Wpedantic-macros", 1 }, { "-Wperf-constraint-implies-noexcept", 1 },
    { "-Wpessimizing-move", 1 }, { "-Wpgo-coverage", 1 }, { "-Wpointer-arith", 1 }, { "-Wpointer-bool-conversion", 1 }, { "-Wpointer-compare", 1 },
    { "-Wpointer-integer-compare", 1 }, { "-Wpointer-sign", 1 }, { "-Wpointer-to-enum-cast", 1 }, { "-Wpointer-to-int-cast", 1 },
    { "-Wpointer-type-mismatch", 1 }, { "-Wpoison-system-directories", 1 }, { "-Wpotentially-direct-selector", 1 },
    { "-Wpotentially-evaluated-expression", 1 }, { "-Wpragma-clang-attribute", 1 }, { "-Wpragma-once-outside-header", 1 }, { "-Wpragma-pack", 1 },
    { "-Wpragma-pack-suspicious-include", 1 }, { "-Wpragma-system-header-outside-header", 1 }, { "-Wpragmas", 1 }, { "-Wpre-c++14-compat", 1 },
    { "-Wpre-c++14-compat-pedantic", 1 }, { "-Wpre-c++17-compat", 1 }, { "-Wpre-c++17-compat-pedantic", 1 }, { "-Wpre-c++20-compat", 1 },
    { "-Wpre-c++20-compat-pedantic", 1 }, { "-Wpre-c++23-compat", 1 }, { "-Wpre-c++23-compat-pedantic", 1 }, { "-Wpre-c++26-compat", 1 },
    { "-Wpre-c++26-compat-pedantic", 1 }, { "-Wpre-c++2c-compat", 1 }, { "-Wpre-c++2c-compat-pedantic", 1 }, { "-Wpre-c++2d-compat", 1 },
    { "-Wpre-c++2d-compat-pedantic", 1 }, { "-Wpre-c11-compat", 1 }, { "-Wpre-c11-compat-pedantic", 1 }, { "-Wpre-c23-compat", 1 },
    { "-Wpre-c23-compat-pedantic", 1 }, { "-Wpre-c2x-compat", 1 }, { "-Wpre-c2x-compat-pedantic", 1 }, { "-Wpre-c2y-compat", 1 },
    { "-Wpre-c2y-compat-pedantic", 1 }, { "-Wpre-openmp-51-compat", 1 }, { "-Wpredefined-identifier-outside-function", 1 },
    { "-Wpreferred-type-bitfield-enum-conversion", 1 }, { "-Wpreferred-type-bitfield-width", 1 }, { "-Wprivate-extern", 1 }, { "-Wprivate-header", 1 },
    { "-Wprivate-module", 1 }, { "-Wprofile-instr-missing", 1 }, { "-Wprofile-instr-out-of-date", 1 }, { "-Wprofile-instr-unprofiled", 1 },
    { "-Wproperty-access-dot-syntax", 1 }, { "-Wproperty-attribute-mismatch", 1 }, { "-Wprotocol", 1 }, { "-Wprotocol-property-synthesis-ambiguity", 1 },
    { "-Wpsabi", 1 }, { "-Wptrauth-null-pointers", 1 }, { "-Wqualified-void-return-type", 1 }, { "-Wquoted-include-in-framework-header", 1 },
    { "-Wrange-loop-analysis", 1 }, { "-Wrange-loop-bind-reference", 1 }, { "-Wrange-loop-construct", 1 }, { "-Wread-only-types", 1 },
    { "-Wreadonly-iboutlet-property", 1 }, { "-Wreceiver-expr", 1 }, { "-Wreceiver-forward-class", 1 }, { "-Wredeclared-class-member", 1 },
    { "-Wreduced-bmi-output-overrided", 1 }, { "-Wredundant-attribute", 1 }, { "-Wredundant-consteval-if", 1 }, { "-Wredundant-decls", 1 },
    { "-Wredundant-move", 1 }, { "-Wredundant-parens", 1 }, { "-Wreference-tu-local-entity-in-other-tu", 1 }, { "-Wregister", 1 },
    { "-Wreinterpret-base-class", 1 }, { "-Wreorder", 1 }, { "-Wreorder-ctor", 1 }, { "-Wreorder-init-list", 1 }, { "-Wrequires-super-attribute", 1 },
    { "-Wreserved-attribute-identifier", 1 }, { "-Wreserved-id-macro", 1 }, { "-Wreserved-identifier", 1 }, { "-Wreserved-macro-identifier", 1 },
    { "-Wreserved-module-identifier", 1 }, { "-Wreserved-user-defined-literal", 1 }, { "-Wrestrict-expansion", 1 }, { "-Wretained-language-linkage", 1 },
    { "-Wreturn-local-addr", 1 }, { "-Wreturn-mismatch", 1 }, { "-Wreturn-stack-address", 1 }, { "-Wreturn-std-move", 1 }, { "-Wreturn-type", 1 },
    { "-Wreturn-type-c-linkage", 1 }, { "-Wrewrite-not-bool", 1 }, { "-Wriscv-be-experimental", 1 }, { "-Wrtti", 1 }, { "-Wsarif-format-unstable", 1 },
    { "-Wscalable-static-analysis-framework", 1 }, { "-Wsection", 1 }, { "-Wselector", 1 }, { "-Wselector-type-mismatch", 1 }, { "-Wself-assign", 1 },
    { "-Wself-assign-field", 1 }, { "-Wself-assign-overloaded", 1 }, { "-Wself-move", 1 }, { "-Wsemicolon-before-method-body", 1 }, { "-Wsentinel", 1 },
    { "-Wsequence-point", 1 }, { "-Wserialized-diagnostics", 1 }, { "-Wshadow", 1 }, { "-Wshadow-all", 1 }, { "-Wshadow-field", 1 },
    { "-Wshadow-field-in-constructor", 1 }, { "-Wshadow-field-in-constructor-modified", 1 }, { "-Wshadow-header", 1 }, { "-Wshadow-ivar", 1 },
    { "-Wshadow-uncaptured-local", 1 }, { "-Wshift-bool", 1 }, { "-Wshift-count-negative", 1 }, { "-Wshift-count-overflow", 1 },
    { "-Wshift-negative-value", 1 }, { "-Wshift-op-parentheses", 1 }, { "-Wshift-overflow", 1 }, { "-Wshift-sign-overflow", 1 }, { "-Wshorten-64-to-32", 1 },
    { "-Wsign-compare", 1 }, { "-Wsign-conversion", 1 }, { "-Wsign-promo", 1 }, { "-Wsigned-enum-bitfield", 1 }, { "-Wsigned-unsigned-wchar", 1 },
    { "-Wsimdlen", 1 }, { "-Wsingle-bit-bitfield-constant-conversion", 1 }, { "-Wsizeof-array-argument", 1 }, { "-Wsizeof-array-decay", 1 },
    { "-Wsizeof-array-div", 1 }, { "-Wsizeof-pointer-div", 1 }, { "-Wsizeof-pointer-memaccess", 1 }, { "-Wslash-u-filename", 1 }, { "-Wslh-asm-goto", 1 },
    { "-Wsometimes-uninitialized", 1 }, { "-Wsource-mgr", 1 }, { "-Wsource-uses-openacc", 1 }, { "-Wsource-uses-openmp", 1 }, { "-Wspir-compat", 1 },
    { "-Wspirv-compat", 1 }, { "-Wspirv-validation", 1 }, { "-Wstack-exhausted", 1 }, { "-Wstack-protector", 1 }, { "-Wstatic-float-init", 1 },
    { "-Wstatic-in-inline", 1 }, { "-Wstatic-inline-explicit-instantiation", 1 }, { "-Wstatic-local-in-inline", 1 }, { "-Wstatic-self-init", 1 },
    { "-Wstdlibcxx-not-found", 1 }, { "-Wstrict-aliasing", 1 }, { "-Wstrict-aliasing=0", 1 }, { "-Wstrict-aliasing=1", 1 }, { "-Wstrict-aliasing=2", 1 },
    { "-Wstrict-overflow", 1 }, { "-Wstrict-overflow=0", 1 }, { "-Wstrict-overflow=1", 1 }, { "-Wstrict-overflow=2", 1 }, { "-Wstrict-overflow=3", 1 },
    { "-Wstrict-overflow=4", 1 }, { "-Wstrict-overflow=5", 1 }, { "-Wstrict-potentially-direct-selector", 1 }, { "-Wstrict-primary-template-shadow", 1 },
    { "-Wstrict-prototypes", 1 }, { "-Wstrict-selector-match", 1 }, { "-Wstring-compare", 1 }, { "-Wstring-concatenation", 1 }, { "-Wstring-conversion", 1 },
    { "-Wstring-plus-char", 1 }, { "-Wstring-plus-int", 1 }, { "-Wstrlcpy-strlcat-size", 1 }, { "-Wstrncat-size", 1 }, { "-Wsuggest-destructor-override", 1 },
    { "-Wsuggest-override", 1 }, { "-Wsuper-class-method-mismatch", 1 }, { "-Wsuspicious-bzero", 1 }, { "-Wsuspicious-memaccess", 1 },
    { "-Wswift-name-attribute", 1 }, { "-Wswitch", 1 }, { "-Wswitch-bool", 1 }, { "-Wswitch-default", 1 }, { "-Wswitch-enum", 1 }, { "-Wsync-alignment", 1 },
    { "-Wsync-fetch-and-nand-semantics-changed", 1 }, { "-Wsynth", 1 }, { "-Wtarget-clones-mixed-specifiers", 1 }, { "-Wtautological-bitwise-compare", 1 },
    { "-Wtautological-compare", 1 }, { "-Wtautological-constant-compare", 1 }, { "-Wtautological-constant-in-range-compare", 1 },
    { "-Wtautological-constant-out-of-range-compare", 1 }, { "-Wtautological-negation-compare", 1 }, { "-Wtautological-objc-bool-compare", 1 },
    { "-Wtautological-overlap-compare", 1 }, { "-Wtautological-pointer-compare", 1 }, { "-Wtautological-type-limit-compare", 1 },
    { "-Wtautological-undefined-compare", 1 }, { "-Wtautological-unsigned-char-zero-compare", 1 }, { "-Wtautological-unsigned-enum-zero-compare", 1 },
    { "-Wtautological-unsigned-zero-compare", 1 }, { "-Wtautological-value-range-compare", 1 }, { "-Wtcb-enforcement", 1 },
    { "-Wtemplate-in-declaration-name", 1 }, { "-Wtentative-definition-array", 1 }, { "-Wtentative-definition-compat", 1 },
    { "-Wtentative-definition-incomplete-type", 1 }, { "-Wthread-safety", 1 }, { "-Wthread-safety-analysis", 1 }, { "-Wthread-safety-attributes", 1 },
    { "-Wthread-safety-beta", 1 }, { "-Wthread-safety-negative", 1 }, { "-Wthread-safety-pointer", 1 }, { "-Wthread-safety-precise", 1 },
    { "-Wthread-safety-reference", 1 }, { "-Wthread-safety-reference-return", 1 }, { "-Wthread-safety-verbose", 1 }, { "-Wtrigraphs", 1 },
    { "-Wtrivial-auto-var-init", 1 }, { "-Wtype-limits", 1 }, { "-Wtype-safety", 1 }, { "-Wtypedef-redefinition", 1 }, { "-Wtypename-missing", 1 },
    { "-Wunable-to-open-stats-file", 1 }, { "-Wunaligned-access", 1 }, { "-Wunaligned-qualifier-implicit-cast", 1 }, { "-Wunavailable-declarations", 1 },
    { "-Wundeclared-selector", 1 }, { "-Wundef", 1 }, { "-Wundef-prefix", 1 }, { "-Wundef-true", 1 }, { "-Wundefined-arm-za", 1 },
    { "-Wundefined-arm-zt0", 1 }, { "-Wundefined-bool-conversion", 1 }, { "-Wundefined-func-template", 1 }, { "-Wundefined-inline", 1 },
    { "-Wundefined-internal", 1 }, { "-Wundefined-internal-type", 1 }, { "-Wundefined-reinterpret-cast", 1 }, { "-Wundefined-var-template", 1 },
    { "-Wunderaligned-exception-object", 1 }, { "-Wunderlying-atomic-qualifier-ignored", 1 }, { "-Wunderlying-cv-qualifier-ignored", 1 },
    { "-Wunevaluated-expression", 1 }, { "-Wunguarded-availability", 1 }, { "-Wunguarded-availability-new", 1 }, { "-Wunicode", 1 },
    { "-Wunicode-homoglyph", 1 }, { "-Wunicode-whitespace", 1 }, { "-Wunicode-zero-width", 1 }, { "-Wuninitialized", 1 },
    { "-Wuninitialized-const-pointer", 1 }, { "-Wuninitialized-const-reference", 1 }, { "-Wuninitialized-explicit-init", 1 },
    { "-Wunique-object-duplication", 1 }, { "-Wunknown-acc-extension-clause", 1 }, { "-Wunknown-argument", 1 }, { "-Wunknown-assumption", 1 },
    { "-Wunknown-attributes", 1 }, { "-Wunknown-cuda-version", 1 }, { "-Wunknown-directives", 1 }, { "-Wunknown-escape-sequence", 1 },
    { "-Wunknown-pragmas", 1 }, { "-Wunknown-sanitizers", 1 }, { "-Wunknown-warning-option", 1 }, { "-Wunnamed-type-template-args", 1 },
    { "-Wunnecessary-virtual-specifier", 1 }, { "-Wunneeded-internal-declaration", 1 }, { "-Wunneeded-member-function", 1 },
    { "-Wunqualified-std-cast-call", 1 }, { "-Wunreachable-code", 1 }, { "-Wunreachable-code-aggressive", 1 }, { "-Wunreachable-code-break", 1 },
    { "-Wunreachable-code-fallthrough", 1 }, { "-Wunreachable-code-generic-assoc", 1 }, { "-Wunreachable-code-loop-increment", 1 },
    { "-Wunreachable-code-return", 1 }, { "-Wunsafe-buffer-usage", 1 }, { "-Wunsafe-buffer-usage-in-container", 1 },
    { "-Wunsafe-buffer-usage-in-format-attr-call", 1 }, { "-Wunsafe-buffer-usage-in-libc-call", 1 }, { "-Wunsafe-buffer-usage-in-static-sized-array", 1 },
    { "-Wunsafe-buffer-usage-in-unique-ptr-array-access", 1 }, { "-Wunsequenced", 1 }, { "-Wunsupported-abi", 1 }, { "-Wunsupported-abs", 1 },
    { "-Wunsupported-availability-guard", 1 }, { "-Wunsupported-cb", 1 }, { "-Wunsupported-dll-base-class-template", 1 },
    { "-Wunsupported-floating-point-opt", 1 }, { "-Wunsupported-friend", 1 }, { "-Wunsupported-gpopt", 1 }, { "-Wunsupported-nan", 1 },
    { "-Wunsupported-target-opt", 1 }, { "-Wunsupported-visibility", 1 }, { "-Wunterminated-string-initialization", 1 },
    { "-Wunusable-partial-specialization", 1 }, { "-Wunused", 1 }, { "-Wunused-argument", 1 }, { "-Wunused-but-set-global", 1 },
    { "-Wunused-but-set-parameter", 1 }, { "-Wunused-but-set-variable", 1 }, { "-Wunused-command-line-argument", 1 }, { "-Wunused-comparison", 1 },
    { "-Wunused-const-variable", 1 }, { "-Wunused-exception-parameter", 1 }, { "-Wunused-function", 1 }, { "-Wunused-getter-return-value", 1 },
    { "-Wunused-label", 1 }, { "-Wunused-lambda-capture", 1 }, { "-Wunused-local-typedef", 1 }, { "-Wunused-local-typedefs", 1 }, { "-Wunused-macros", 1 },
    { "-Wunused-member-function", 1 }, { "-Wunused-parameter", 1 }, { "-Wunused-private-field", 1 }, { "-Wunused-property-ivar", 1 },
    { "-Wunused-result", 1 }, { "-Wunused-template", 1 }, { "-Wunused-value", 1 }, { "-Wunused-variable", 1 }, { "-Wunused-volatile-lvalue", 1 },
    { "-Wused-but-marked-unused", 1 }, { "-Wuser-defined-literals", 1 }, { "-Wuser-defined-warnings", 1 }, { "-Wvarargs", 1 },
    { "-Wvariadic-macro-arguments-omitted", 1 }, { "-Wvariadic-macros", 1 }, { "-Wvec-elem-size", 1 }, { "-Wvector-conversion", 1 },
    { "-Wvector-conversions", 1 }, { "-Wvexing-parse", 1 }, { "-Wvisibility", 1 }, { "-Wvla", 1 }, { "-Wvla-cxx-extension", 1 }, { "-Wvla-extension", 1 },
    { "-Wvla-extension-static-assert", 1 }, { "-Wvoid-pointer-to-enum-cast", 1 }, { "-Wvoid-pointer-to-int-cast", 1 }, { "-Wvoid-ptr-dereference", 1 },
    { "-Wvolatile-register-var", 1 }, { "-Wwasm-exception-spec", 1 }, { "-Wweak-template-vtables", 1 }, { "-Wweak-vtables", 1 }, { "-Wwritable-strings", 1 },
    { "-Wwrite-strings", 1 }, { "-Wxor-used-as-pow", 1 }, { "-Wzero-as-null-pointer-constant", 1 }, { "-Wzero-length-array", 1 },
};

// clang 23's nonzero answers to __has_constexpr_builtin for the builtins pp_features_builtin lists (the corpora ask about
// none), in gnu17 and c23 alike.
static const PPFeature pp_features_constexpr_builtin[] = {
    { "__builtin_add_overflow", 1 }, { "__builtin_assume", 1 }, { "__builtin_assume_aligned", 1 }, { "__builtin_bitreverse16", 1 },
    { "__builtin_bitreverse32", 1 }, { "__builtin_bitreverse64", 1 }, { "__builtin_bitreverse8", 1 }, { "__builtin_bswap16", 1 },
    { "__builtin_bswap32", 1 }, { "__builtin_bswap64", 1 }, { "__builtin_clz", 1 }, { "__builtin_clzg", 1 }, { "__builtin_clzll", 1 },
    { "__builtin_clzs", 1 }, { "__builtin_constant_p", 1 }, { "__builtin_copysign", 1 }, { "__builtin_ctz", 1 }, { "__builtin_ctzg", 1 },
    { "__builtin_ctzll", 1 }, { "__builtin_ctzs", 1 }, { "__builtin_dynamic_object_size", 1 }, { "__builtin_elementwise_fshl", 1 },
    { "__builtin_expect", 1 }, { "__builtin_memcpy", 1 }, { "__builtin_mul_overflow", 1 }, { "__builtin_nansf128", 1 },
    { "__builtin_operator_new", 1 }, { "__builtin_popcount", 1 }, { "__builtin_popcountg", 1 }, { "__builtin_popcountll", 1 },
    { "__builtin_rotateleft32", 1 }, { "__builtin_sadd_overflow", 1 }, { "__builtin_shufflevector", 1 }, { "__builtin_smul_overflow", 1 },
    { "__builtin_ssub_overflow", 1 }, { "__builtin_sub_overflow", 1 }, { "__builtin_uadd_overflow", 1 }, { "__builtin_umul_overflow", 1 },
};

// Implemented source capabilities, independent of the frozen Clang oracle. Attribute acceptance alone is not
// implementation: attributes without an implemented consumer and unpublished linkage obligations remain zero. These
// entries have declaration/type, diagnostic, control, symbol or executable lowering consumers. C23 date codes describe the
// standardized attribute, not the language mode (the spelling is also accepted as an extension in earlier modes).
// Feature dates are language-mode gates; __has_extension asks about the same implemented syntax in older modes.
// Sorted for binary search. __has_warning remains the diagnostic vocabulary, not a promise to emit each warning.
static const PPFeature pp_implemented_attribute[] = {
    { "alias", 1 },
    { "aligned", 1 },
    { "cleanup", 1 },
    { "common", 1 },
    { "constructor", 1 },
    { "counted_by", 1 },
    { "counted_by_or_null", 1 },
    { "destructor", 1 },
    { "ext_vector_type", 1 },
    { "fallthrough", 1 },
    { "gnu_inline", 1 },
    { "ifunc", 1 },
    { "mode", 1 },
    { "naked", 1 },
    { "nocommon", 1 },
    { "overloadable", 1 },
    { "packed", 1 },
    { "retain", 1 },
    { "section", 1 },
    { "sized_by", 1 },
    { "sized_by_or_null", 1 },
    { "target", 1 },
    { "transparent_union", 1 },
    { "used", 1 },
    { "vector_size", 1 },
    { "visibility", 1 },
    { "weak", 1 },
    { "weakref", 1 },
};
static const PPFeature pp_implemented_c_attribute[] = {
    { "clang::counted_by", 1 },
    { "clang::counted_by_or_null", 1 },
    { "clang::ext_vector_type", 1 },
    { "clang::overloadable", 1 },
    { "clang::sized_by", 1 },
    { "clang::sized_by_or_null", 1 },
    { "deprecated", 201904 },
    { "fallthrough", 201910 },
    { "gnu::alias", 1 },
    { "gnu::aligned", 1 },
    { "gnu::cleanup", 1 },
    { "gnu::common", 1 },
    { "gnu::constructor", 1 },
    { "gnu::destructor", 1 },
    { "gnu::fallthrough", 1 },
    { "gnu::gnu_inline", 1 },
    { "gnu::ifunc", 1 },
    { "gnu::mode", 1 },
    { "gnu::naked", 1 },
    { "gnu::nocommon", 1 },
    { "gnu::packed", 1 },
    { "gnu::retain", 1 },
    { "gnu::section", 1 },
    { "gnu::target", 1 },
    { "gnu::transparent_union", 1 },
    { "gnu::used", 1 },
    { "gnu::vector_size", 1 },
    { "gnu::visibility", 1 },
    { "gnu::weak", 1 },
    { "gnu::weakref", 1 },
    { "maybe_unused", 202106 },
    { "nodiscard", 202003 },
};
static const PPFeature pp_implemented_feature[] = {
    { "c_alignas", 1, 2011 },
    { "c_alignof", 1, 2011 },
    { "c_atomic", 1, 2011 },
    { "c_fixed_enum", 1, 2023 },
    { "c_generic_selections", 1, 2011 },
    { "c_static_assert", 1, 2011 },
    { "c_thread_local", 1, 2011 },
};
static const PPFeature pp_implemented_builtin[] = {
    // The keywords of builtins the checker works out where it reads them (#148, c_check_input.c's
    // check_input_source_builtin), in the table's byte order.
    { "__builtin_COLUMN", 1 }, { "__builtin_FILE", 1 }, { "__builtin_FILE_NAME", 1 }, { "__builtin_FUNCTION", 1 }, { "__builtin_LINE", 1 },
    { "__builtin_omp_required_simd_align", 1 }, { "__builtin_ptrauth_type_discriminator", 1 }, { "__builtin_vectorelements", 1 },
    { "__is_bitwise_cloneable", 1 }, { "__is_destructible", 1 }, { "__is_nothrow_destructible", 1 },
    { "__is_target_arch", 1 }, { "__is_target_environment", 1 }, { "__is_target_os", 1 }, { "__is_target_variant_environment", 1 },
    { "__is_target_variant_os", 1 }, { "__is_target_vendor", 1 },
};

static int pp_compare_feature(const uint8_t* name, uint32_t length, const char* entry)
{
    const uint32_t entry_length = (uint32_t)strlen(entry);
    const int compared = memcmp(name, entry, length < entry_length ? length : entry_length);
    return compared != 0 ? compared : (int)length - (int)entry_length;
}

static uint32_t pp_feature_lookup(const PPFeature* table, uint32_t count, const uint8_t* name, uint32_t length, uint32_t standard)
{
    uint32_t low = 0;
    uint32_t high = count;
    uint32_t result = 0;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        const int compared = pp_compare_feature(name, length, table[middle].name);
        if (compared == 0)
        {
            result = table[middle].standard <= standard ? table[middle].value : 0;
            low = high;
        }
        else if (compared < 0)
        {
            high = middle;
        }
        else
        {
            low = middle + 1;
        }
    }
    return result;
}

#include "c_preprocessor_warning_options.c"

// Whether `name` is `text`'s `size` bytes (with `prefix`, whether it starts with them), ASCII case aside, as clang's
// __is_target_* builtins compare names.
static bool pp_name_matches(const uint8_t* name, uint32_t length, const char* text, uint32_t size, bool prefix)
{
    bool result = prefix ? length >= size : length == size;
    for (uint32_t i = 0; i < size && result; i += 1)
    {
        const uint8_t a = name[i];
        const uint8_t b = (uint8_t)text[i];
        result = (a >= 'A' && a <= 'Z' ? a | 0x20 : a) == (b >= 'A' && b <= 'Z' ? b | 0x20 : b);
    }
    return result;
}

// Whether clang's triple parser reads `name` as the GNU environment: gnu and anything after it, but the names of its
// other ABIs (gnux32, gnueabi, ...) and what follows them.
static bool pp_target_gnu(const uint8_t* name, uint32_t length)
{
    static const char* const others[] = { "gnuabin32", "gnuabi64", "gnueabi", "gnuf32", "gnuf64", "gnusf", "gnux32", "gnu_ilp32", "gnut64" };
    bool result = pp_name_matches(name, length, "gnu", 3, true);
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(others) && result; i += 1)
    {
        result = !pp_name_matches(name, length, others[i], (uint32_t)strlen(others[i]), true);
    }
    return result;
}

// Whether clang 23 makes a name a builtin of the unit by its row's language modes (`languages`, the token of clang's
// table: Builtin::Context's builtinIsSupported): ALL_LANGUAGES in every C mode, ALL_GNU_LANGUAGES in GNU's, C23_LANG
// from C23 on; never the modes buster has no option for (Microsoft's extensions, C2y, coroutines, Objective-C, C++,
// OpenCL, HLSL, CUDA).
static bool pp_builtin_language(const PPOptions* options, const char* languages)
{
    const bool result = strcmp(languages, "ALL_LANGUAGES") == 0 || (strcmp(languages, "ALL_GNU_LANGUAGES") == 0 && options->language.gnu) ||
                        (strcmp(languages, "C23_LANG") == 0 && options->language.standard >= 2023);
    return result;
}

// Whether a builtin's required target features hold in the unit, which clang's __has_builtin answers by
// (Builtin::evaluateRequiredTargetFeatures): `features`, an expression of clang's feature names (',' and, '|' or),
// against the target's features as clang resolves them from -march and the -m flags (predefines_resolve), each of
// c_predefines.c's names available or not, as clang's feature map has them. An empty expression requires nothing.
// Only an x86 builtin's has features, and headers query none, so the table is made for each query rather than kept.
static bool pp_builtin_features(const PPOptions* options, const char* features)
{
    bool result = features[0] == 0;
    if (!result)
    {
        PredefinesFeatures resolved;
        int32_t cpu;
        // The options resolved when they were parsed (pp_parse_options' predefines_target), so they do again.
        predefines_resolve(&options->predefines, &resolved, &cpu);
        CBuiltinIntrinsicFeature table[BUSTER_ARRAY_LENGTH(predefines_features)];
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(predefines_features); i += 1)
        {
            table[i] = (CBuiltinIntrinsicFeature){
                .name = predefines_features[i].name,
                .name_length = (uint32_t)strlen(predefines_features[i].name),
                .available = predefines_has(resolved, i),
            };
        }
        CBuiltinIntrinsicFeatureFrame frames[8];
        result = c_builtin_intrinsic_features_evaluate(features, (uint32_t)strlen(features), table, BUSTER_ARRAY_LENGTH(table), frames,
                                                       BUSTER_ARRAY_LENGTH(frames)) == CBUILTIN_INTRINSIC_FEATURE_AVAILABLE;
    }
    return result;
}

// Whether clang makes a plain library name that buster implements (c_builtin_library.c's plain list: sin, strlen,
// printf) a builtin of the unit (#148): unless -fno-builtin or -ffreestanding says otherwise (Builtin::Context's
// builtinIsSupported) or -fno-builtin-NAME names it (its NoBuiltinFuncs), one of ALL_GNU_LANGUAGES in GNU's modes alone,
// and one of C23_LANG (<stdbit.h>'s) in C23 and later. The checker's implicit library declarations decide the same from
// the catalog (check_input_library_enabled).
static bool pp_library_builtin(const PPOptions* options, const uint8_t* name, uint32_t length)
{
    const CBuiltinLibraryPlain* plain = cbuiltin_library_plain((const char*)name, length);
    bool result = plain != NULL && !options->no_builtin && !options->freestanding && (plain->modes != BUSTER_CBUILTIN_LIBRARY_GNU || options->language.gnu) &&
                  (plain->modes != BUSTER_CBUILTIN_LIBRARY_C23 || options->language.standard >= 2023);
    for (uint32_t i = 0; result && i < options->no_builtin_function_count; i += 1)
    {
        const char* excluded = options->no_builtin_functions[i];
        result = strlen(excluded) != length || memcmp(excluded, name, length) != 0;
    }
    return result;
}

// buster's __has_builtin of a name (#806, #148): 1 for a builtin it implements, its source checking and Tier 0 lowering
// shipped (the catalog's source capability, the x86 intrinsics' rows and leaves, the fortified builtins, the library
// builtins compiled as calls of their functions and its own queries), or whose source checking shipped and whose code -c
// refuses by name (docs/c-builtins-integration.md's "Checked, refused in code"), that clang makes a builtin of the unit by its
// language modes and whose required target features hold there, as clang answers (pp_builtin_language,
// pp_builtin_features). Every __builtin_ library builtin is one of all of clang's languages without features, and a plain
// one is a builtin where pp_library_builtin says so. The answer is 1, but __builtin_operator_new's and
// __builtin_operator_delete's: 201802 in every language, as clang's EvaluateHasBuiltin answers them, the date of the change
// that let them call any usual allocation and deallocation function, which libc++ asks about.
static uint32_t pp_implemented_builtin_query(const PPOptions* options, const uint8_t* name, uint32_t length)
{
    const CBuiltinSourceScope* scope = cbuiltin_source_scope(name, length);
    const bool catalog = cbuiltin_has_capability(name, length, BUSTER_CBUILTIN_CAP_SOURCE) && pp_builtin_language(options, cbuiltin_languages(name, length));
    const bool intrinsic =
        cbuiltin_source_intrinsic(name, length) && pp_builtin_language(options, scope->languages) && pp_builtin_features(options, scope->features);
    const uint32_t answer =
        pp_feature_lookup(pp_implemented_builtin, BUSTER_ARRAY_LENGTH(pp_implemented_builtin), name, length, options->language.standard) | catalog |
        intrinsic | cbuiltin_fortify_source(name, length) | cbuiltin_library_source(name, length) | pp_library_builtin(options, name, length);
    const CBuiltinId id = catalog ? cbuiltin_resolve((const char*)name, length) : CBUILTIN_NONE;
    const uint32_t result = answer != 0 && (id == CBUILTIN_OPERATOR_NEW || id == CBUILTIN_OPERATOR_DELETE) ? 201802 : answer;
    return result;
}

// buster's __has_constexpr_builtin of a name: 1 for a builtin it folds as clang's constant evaluator does, where it is a
// builtin of the unit (pp_implemented_builtin_query's conditions): the catalog's constant capability, and the library
// builtins' folds (#148, c_builtin_library.c: cbuiltin_library_fold's, the plain ones' and C23's <stdbit.h> functions',
// where pp_library_builtin says the name is a builtin).
static uint32_t pp_implemented_constexpr_query(const PPOptions* options, const uint8_t* name, uint32_t length)
{
    bool wide = false;
    const CBuiltinLibraryId library = cbuiltin_library_id((const char*)name, length);
    const bool catalog = cbuiltin_has_capability(name, length, BUSTER_CBUILTIN_CAP_CONSTEXPR) && pp_builtin_language(options, cbuiltin_languages(name, length));
    const bool folded = library != CBUILTIN_LIBRARY_COUNT && cbuiltin_library_fold(library, &wide) != CBUILTIN_LIBRARY_FOLD_NONE;
    const uint32_t plain_fold = cbuiltin_library_plain_fold((const char*)name, length);
    // The plain entries of no fold (fmax and its kin, #967) are computed inline, not folded.
    const bool plain = ((plain_fold != BUSTER_CBUILTIN_LIBRARY_PLAIN_FOLDS && cbuiltin_library_plain_folds[plain_fold].fold != CBUILTIN_LIBRARY_FOLD_NONE) ||
                        cbuiltin_stdc_library_resolve((const char*)name, length) != CBUILTIN_NONE) &&
                       pp_library_builtin(options, name, length);
    const uint32_t result = catalog | folded | plain;
    return result;
}

// A feature query's answer for `name`: clang's (`options->clang_features`, the oracle's mode) or what buster implements.
// The sanitizer features follow the sanitizers the options leave on (c_predefines.c's predefines_sanitizer_feature). The
// target queries answer for the target triple in both modes, as clang's read a name: case aside, and the operating
// system and the environment as its triple parser reads a triple's (linux2 is Linux; gnux32 isn't GNU).
static uint32_t pp_feature(const PPOptions* options, PPBuiltin builtin, const uint8_t* name, uint32_t length)
{
    const uint32_t standard = options->language.standard;
    const bool clang = options->clang_features;
    const bool feature = builtin == PP_BUILTIN_HAS_FEATURE || builtin == PP_BUILTIN_HAS_EXTENSION;
    const uint32_t sanitizer = clang && feature ? predefines_sanitizer_feature(&options->predefines, name, length) : 0;
    uint32_t vendor_length;
    const char* vendor = predefines_target_vendor(&options->predefines, &vendor_length);
    uint32_t result = 0;
    switch (builtin)
    {
        case PP_BUILTIN_HAS_ATTRIBUTE:
            result = clang ? pp_feature_lookup(pp_features_attribute, BUSTER_ARRAY_LENGTH(pp_features_attribute), name, length, standard)
                           : pp_feature_lookup(pp_implemented_attribute, BUSTER_ARRAY_LENGTH(pp_implemented_attribute), name, length, standard);
            break;
        case PP_BUILTIN_HAS_BUILTIN:
            result = clang ? pp_feature_lookup(pp_features_builtin, BUSTER_ARRAY_LENGTH(pp_features_builtin), name, length, standard)
                           : pp_implemented_builtin_query(options, name, length);
            break;
        case PP_BUILTIN_HAS_CONSTEXPR_BUILTIN:
            result = clang ? pp_feature_lookup(pp_features_constexpr_builtin, BUSTER_ARRAY_LENGTH(pp_features_constexpr_builtin), name, length, standard)
                           : pp_implemented_constexpr_query(options, name, length);
            break;
        case PP_BUILTIN_HAS_FEATURE:
            result = clang ? pp_feature_lookup(pp_features_feature, BUSTER_ARRAY_LENGTH(pp_features_feature), name, length, standard) | sanitizer
                           : pp_feature_lookup(pp_implemented_feature, BUSTER_ARRAY_LENGTH(pp_implemented_feature), name, length, standard);
            break;
        case PP_BUILTIN_HAS_EXTENSION:
            // An extension is anything that is a feature, or in the extension table.
            result = clang ? pp_feature_lookup(pp_features_feature, BUSTER_ARRAY_LENGTH(pp_features_feature), name, length, standard) |
                                 pp_feature_lookup(pp_features_extension, BUSTER_ARRAY_LENGTH(pp_features_extension), name, length, standard) | sanitizer
                           : pp_feature_lookup(pp_implemented_feature, BUSTER_ARRAY_LENGTH(pp_implemented_feature), name, length, UINT32_MAX);
            break;
        case PP_BUILTIN_HAS_C_ATTRIBUTE:
            result = clang ? pp_feature_lookup(pp_features_c_attribute, BUSTER_ARRAY_LENGTH(pp_features_c_attribute), name, length, standard)
                           : pp_feature_lookup(pp_implemented_c_attribute, BUSTER_ARRAY_LENGTH(pp_implemented_c_attribute), name, length, standard);
            break;
        case PP_BUILTIN_HAS_WARNING: result = pp_feature_lookup(pp_features_warning, BUSTER_ARRAY_LENGTH(pp_features_warning), name, length, standard); break;
        case PP_BUILTIN_IS_TARGET_ARCH:
            // The names the triple parser reads as x86-64 without a subarchitecture (x86_64_lfi has one).
            result = pp_name_matches(name, length, "x86_64", 6, false) || pp_name_matches(name, length, "amd64", 5, false) ||
                     pp_name_matches(name, length, "x86_64h", 7, false);
            break;
        case PP_BUILTIN_IS_TARGET_VENDOR: result = pp_name_matches(name, length, vendor, vendor_length, false); break;
        case PP_BUILTIN_IS_TARGET_OS: result = pp_name_matches(name, length, "linux", 5, true); break;
        case PP_BUILTIN_IS_TARGET_ENVIRONMENT: result = pp_target_gnu(name, length); break;
        default: break;
    }
    return result;
}

// The most bytes pp_predefines writes.
#define BUSTER_PP_PREDEFINES_BYTES (sizeof(pp_predefines_base) + sizeof(pp_predefines_c23) + 512 + BUSTER_PREDEFINES_TEXT_BYTES)


// ---------------------------------------------------------------------------------------------------------------------
// Setting up and running

#define BUSTER_PP_ARENA_BYTES (64ull << 30)
#define BUSTER_PP_FILE_SLOTS (2u * BUSTER_PP_MAX_FILES)
#define BUSTER_PP_INITIAL_FILE_SLOTS 1024u


// The unit's tables, which pp_initialize_once reserves as one arena of tables (arena_reserve_tables, #1128): each a
// table of its own of `count` entries of `size` bytes, all of it base pages, that `*field` points to once they are
// reserved, and stays null if they can't be or it has no entries.
//
// Base pages throughout (#652): huge pages past a table's first pages took an advice call for each table and split the
// mapping around them, 38 of an empty file's 50 madvise calls, and none of these tables grows large enough for base
// pages to cost much: on sqlite3.c the macros touch the most, 368 KB, and on this compiler's main.c the items 1.9 MB
// and the macros 1.1 MB, where the first megabyte in base pages costs about as much as the huge pages it replaces
// (BUSTER_PP_TABLE_SMALL_BYTES).
typedef struct PPTables PPTables;
struct PPTables
{
    uint32_t count;
    bool full;
    ArenaTable tables[BUSTER_ARENA_TABLES];
    void** fields[BUSTER_ARENA_TABLES];
};

// The full shape, and once more in the compact one (arena_capacity_shift) if the first was refused: by the address
// space, which switches the thread to compact reservations (arena_compact), or by commit charge, which sets
// arena_commit_refused and leaves the thread's other reservations as they were (#1521). Either stays the thread's, so
// that its later units start in the compact shape. A retry that succeeds leaves the caller's last failure as it was, so
// that no later report reads the first shape's.
BUSTER_PREPROCESSOR_ENTRY bool pp_initialize(Preprocessor* pp, const PPOptions* options, InternTable* table);

static void pp_release_unit(Preprocessor* pp)
{
    pp_tokens_release(&pp->output);
    pp_tokens_release(&pp->arguments);
    pp_tokens_release(&pp->captures);
    pp_tokens_release(&pp->expansions);
    arena_release(&pp->argument_link_arena);
    // The cache's records own the files' mappings, and a unit's own cache is in its arena.
    if (pp->owns_cache)
    {
        file_cache_release(pp->cache);
    }
    arena_release(&pp->scratch[0]);
    arena_release(&pp->scratch[1]);
    arena_release(&pp->history_initial);
    arena_release(&pp->arena);
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(pp->columns); i += 1)
    {
        arena_release(&pp->columns[i]);
    }
    for (uint32_t i = 0; i < pp->array_count; i += 1)
    {
        arena_release(&pp->arrays[i]);
    }
    *pp = (Preprocessor){ 0 };
}

// Releases a unit, and the replay that recorded its deferred history (PPOptions.defer_history), which shares its file
// cache and has no replay of its own.
static void pp_release(Preprocessor* pp)
{
    if (pp->history_replay != 0)
    {
        pp_release_unit(pp->history_replay);
    }
    arena_release(&pp->history_replay_arena);
    pp_release_unit(pp);
}

// The unit's expansion history, recorded now if PPOptions.defer_history put it off. A reader of the history
// (pp_history_at, the history's records) given a unit whose history may be deferred calls this first, as
// pp_diagnose_output and parse_print_diagnostics do. Returns whether there is a history: a unit with
// PPOptions.no_history, run otherwise than by pp_run or pp_run_source, or whose replay fails has none. An entry point:
// the replay is the whole preprocessor, which the units that read a history (the checker's) call in its home.
BUSTER_PREPROCESSOR_ENTRY bool pp_history_ensure(Preprocessor* pp);

// Once a unit has run, its files' token columns have no reader left: the parser, the checker and the diagnostics read
// the output, the ranges and the files' bytes and line tables. A unit with a file cache of its own gives the columns'
// pages back to the kernel (the kinds, starts and values, 9 bytes a token, and the leading-space words: 11.5 MB of
// sqlite3.c's, which stayed to the backend's peak, #628) and marks its records unprepared, so that a replay of the unit
// for its history (pp_history_ensure), which enters the files through the same cache, prepares each again. A unit whose
// replay has run keeps them, as the replay's files read them; and a cache that units share (PPOptions.files) keeps them
// for the units after this one. With `poison` (the driver's --poison-arenas), the pages stay mapped but unreadable, and
// the columns' next pushes go past them, so that any reader left faults.
BUSTER_PREPROCESSOR_ENTRY void pp_release_file_columns(Preprocessor* pp, bool poison);

// Once the checker has checked the program and given its warnings, only a diagnostic (a token's place and spelling) and
// -g's line table read the unit's output, 10 bytes a token (12.5 MB of sqlite3.c's, at the backend's peak, #628). A unit
// whose history is still deferred gives the columns back, keeping the count. A later reader asks for them first
// (pp_output_ensure), and a replay of the unit, which is the run again, gives them back: a diagnostic's, which records
// the history (pp_history_record), or one without history. A unit whose history is recorded keeps its output, which only
// a replay could give back. With `poison` (the driver's --poison-arenas), the pages stay mapped but unreadable, so that
// any reader that doesn't ask for the output first faults.
BUSTER_PREPROCESSOR_ENTRY void pp_release_output(Preprocessor* pp, bool poison);

// The unit's output, given back if pp_release_output released it, by preprocessing the unit again as pp_history_record
// does but without history, quietly, into a preprocessor of its own whose output the unit then takes (pp_output_adopt):
// so that a reader that doesn't record the history (pp_history_at, unrecorded, gives no origins) finds it as it was. The
// replay must give the run's result, errors, fatal error and count of tokens; one that doesn't, or can't be made, leaves
// columns of zeros (pp_output_restore_empty). A diagnostic, which records the history anyway, has the output from that
// replay instead (pp_history_record). Returns whether the output was there or came back.
BUSTER_PREPROCESSOR_ENTRY bool pp_output_ensure(Preprocessor* pp);

// A source expansion may have no output token (#if), so its retained location/origin is the diagnostic source.
// Group its lexical error with the active pragma's own source history. The caller supplies the final boundary
// after preceding captured pragmas and that pragma's retained `fp` output token, including for macro _Pragma.
BUSTER_PREPROCESSOR_ENTRY void pp_diagnose_eval_method_use(Preprocessor* pp, uint32_t use, uint32_t position, uint32_t pragma_source);

// Preprocesses the main file (pp_preprocess_file); with PPOptions.defer_history, then prints the diagnostics the run
// counted (pp_history_report). The output is pp->output, with keywords' kinds. Returns false if there were errors, the
// replay's fatal error among them.
BUSTER_PREPROCESSOR_ENTRY bool pp_run(Preprocessor* pp, const char* main_path);

// pp_run for a main file in memory, `length` bytes at `bytes`, named `name` (which, like the bytes, must outlive the
// run and its replay), as clang names its standard input <stdin>: test-preprocessor's fuzz runs give it the text clang
// reads.
BUSTER_PREPROCESSOR_ENTRY bool pp_run_source(Preprocessor* pp, const char* name, const uint8_t* bytes, uint32_t length);

// The driver has read stdin, and any non-regular primary input, once before starting unit lanes. Those bytes also
// outlive diagnostic replay. A stream that no command cached is read here, into this unit's arena, which the replay
// still holds. Include probes stay on the non-blocking file cache and are unchanged.
BUSTER_PREPROCESSOR_ENTRY bool pp_run_input(Preprocessor* pp, const char* path);

// Reads each non-regular input once into `arena` and records it on `options`. `-` is stdin and is left to
// driver_read_stdin. A regular file, a directory, and a missing path are left for the file cache. A failed stream
// read fails the command; driver_read_nonregular has already said why.
static bool driver_cache_streams(Arena* arena, char** inputs, uint32_t count, PPOptions* options)
{
    bool result = true;
    options->streams = NULL;
    options->stream_count = 0;
    if (count != 0)
    {
        options->streams = BUSTER_ARENA_PUSH_ARRAY(arena, PPStreamInput, count);
        result = options->streams != NULL;
    }
    for (uint32_t i = 0; result && i < count; i += 1)
    {
        bool seen = strcmp(inputs[i], "-") == 0;
        for (uint32_t earlier = 0; !seen && earlier < options->stream_count; earlier += 1)
        {
            seen = strcmp(options->streams[earlier].path, inputs[i]) == 0;
        }
        String8 bytes = { 0 };
        const int kind = seen ? 0 : driver_read_nonregular(arena, inputs[i], &bytes);
        result = kind >= 0;
        if (result && kind > 0)
        {
            options->streams[options->stream_count] = (PPStreamInput){
                .path = inputs[i],
                .bytes = bytes,
            };
            options->stream_count += 1;
        }
    }
    return result;
}

// Build only, never publish, a unit's dependency output before releasing its source/cache. Remove the main source
// by identity, including an alias seen in the prelude; the serializer places its command-line spelling first.
BUSTER_PREPROCESSOR_ENTRY bool pp_dependencies(Preprocessor* pp, const char* source, const char* primary, Arena* arena, DependencyOutput* output, FILE* errors);

// ---------------------------------------------------------------------------------------------------------------------
// System headers

// A place in the ranges and their line notes for locations that mostly increase: the range found last, its next line
// note, the location the level was last set at, and that level (pp_system_level_next).
typedef struct PPLevelCursor PPLevelCursor;
struct PPLevelCursor
{
    uint32_t range;
    uint32_t note;
    uint32_t applied;
    uint8_t level;
};

// The system level (BUSTER_PP_USER_FILE, BUSTER_PP_SYSTEM_FILE or BUSTER_PP_EXTERN_C_FILE) at `location`: its range's at
// its inclusion's start, then its line notes' up to it; a place in no range (0) is a user file's. The cursor starts from
// where it was when `location` is in its range at or after the last note it applied, and looks the range up otherwise.
static uint8_t pp_system_level_next(const Preprocessor* pp, PPLevelCursor* cursor, uint32_t location)
{
    const PPRange* range = cursor->range < pp->range_count ? &pp->ranges[cursor->range] : 0;
    const bool inside = range != 0 && location >= range->base && location - range->base < range->length;
    if (!inside || location < cursor->applied)
    {
        cursor->range = location != 0 ? pp_find_range(pp, location) : BUSTER_PP_NONE;
        const bool found = cursor->range < pp->range_count;
        cursor->level = found ? pp->ranges[cursor->range].system : BUSTER_PP_USER_FILE;
        cursor->note = found ? pp->ranges[cursor->range].first_line_directive : BUSTER_PP_NONE;
        cursor->applied = found ? pp->ranges[cursor->range].base : 0;
    }
    while (cursor->note < pp->line_directive_count && pp->line_directives[cursor->note].location <= location)
    {
        cursor->level = pp->line_directives[cursor->note].system;
        cursor->applied = pp->line_directives[cursor->note].location;
        cursor->note = pp->line_directives[cursor->note].next_in_range;
    }
    return cursor->range < pp->range_count ? cursor->level : BUSTER_PP_USER_FILE;
}

// pp_system_level_next for one location.
static uint8_t pp_system_level_at(const Preprocessor* pp, uint32_t location)
{
    PPLevelCursor cursor = {
        .range = BUSTER_PP_NONE,
        .note = BUSTER_PP_NONE,
    };
    return pp_system_level_next(pp, &cursor, location);
}

// Expansion location, including argument forwarding and generated tokens, determines where a definition was written: a
// macro defined in a system header but invoked in ordinary source isn't in a system header. `origin` is output token
// `token`'s (pp_history_at's): the place is the begin of the outermost expansion it came from, or the anchor of a paste
// or stringification, or for an argument read from a file, the token's own location. Returns 0 when its history doesn't
// lead to a location.
static uint32_t pp_expansion_place(const Preprocessor* pp, uint32_t location, uint32_t origin)
{
    bool valid = true;
    const uint64_t limit = (uint64_t)pp->history_count + pp->substitution_count + pp->generated_count;
    for (uint64_t step = 0; valid && origin != 0 && step <= limit; step += 1)
    {
        const uint32_t index = (origin & BUSTER_PP_ORIGIN_INDEX) - 1;
        const uint32_t kind = origin & ~BUSTER_PP_ORIGIN_INDEX;
        if (kind == BUSTER_PP_ORIGIN_ARGUMENT)
        {
            valid = index < pp->substitution_count;
            origin = valid ? pp->substitutions[index].source : 0;
        }
        else if (kind == BUSTER_PP_ORIGIN_GENERATED)
        {
            valid = index < pp->generated_count;
            location = valid ? pp->generated[index].anchor : location;
            origin = valid ? pp->generated[index].expansion : 0;
        }
        else
        {
            valid = kind == 0 && index < pp->history_count;
            location = valid ? pp->history[index].begin : location;
            origin = valid ? pp->history[index].parent : 0;
        }
    }
    valid &= origin == 0;
    return valid ? location : 0;
}

static uint32_t pp_expansion_location(const Preprocessor* pp, uint32_t token, uint32_t origin)
{
    const bool valid = token < pp->output.count && pp->output.locations != 0;
    return valid ? pp_expansion_place(pp, pp->output.locations[token], origin) : 0;
}

// Either boundary of output token `token`'s outermost expansion. An argument or a generated token follows the
// expansion that substituted or made it; a file token keeps its own location. `origin` is pp_history_at's, after
// pp_history_ensure. Unlike pp_expansion_place, an argument reaches its invocation rather than its spelling place.
static uint32_t pp_output_expansion_boundary(const Preprocessor* pp, uint32_t token, uint32_t origin, bool end)
{
    bool valid = token < pp->output.count && pp->output.locations != 0;
    uint32_t location = valid ? pp->output.locations[token] : 0;
    const uint64_t limit = (uint64_t)pp->history_count + pp->substitution_count + pp->generated_count;
    for (uint64_t step = 0; valid && origin != 0 && step <= limit; step += 1)
    {
        const uint32_t index = (origin & BUSTER_PP_ORIGIN_INDEX) - 1;
        const uint32_t kind = origin & ~BUSTER_PP_ORIGIN_INDEX;
        if (kind == BUSTER_PP_ORIGIN_ARGUMENT)
        {
            valid = index < pp->substitution_count;
            origin = valid ? pp->substitutions[index].expansion : 0;
        }
        else if (kind == BUSTER_PP_ORIGIN_GENERATED)
        {
            valid = index < pp->generated_count;
            origin = valid ? pp->generated[index].expansion : 0;
        }
        else
        {
            valid = kind == 0 && index < pp->history_count;
            location = valid ? (end ? pp->history[index].end : pp->history[index].begin) : location;
            origin = valid ? (end ? pp->history[index].end_origin : pp->history[index].parent) : 0;
        }
    }
    valid &= origin == 0;
    return valid ? location : 0;
}

// The begin of the outermost invocation, as clang's getExpansionLoc: the invocation's name even when its first
// output token is an argument. Statement indentation uses this place, not the argument's spelling column (#1339).
static __attribute__((unused)) uint32_t pp_output_expansion_begin(const Preprocessor* pp, uint32_t token, uint32_t origin)
{
    return pp_output_expansion_boundary(pp, token, origin, false);
}

// The end, as clang's SourceManager::getExpansionRange(...).getEnd() (#148's source-location builtins, #1126's
// review): a function-like invocation's closing parenthesis or an object-like macro's name, followed while that
// end was itself made by an expansion (PPExpansion.end_origin). Returns 0 for a history that leads to no location.
static __attribute__((unused)) uint32_t pp_output_expansion_end(const Preprocessor* pp, uint32_t token, uint32_t origin)
{
    return pp_output_expansion_boundary(pp, token, origin, true);
}

// How far pp_output_invocation_place looks: the output tokens on either side of the one asked about, and the file's
// bytes between the file tokens it finds.
#define BUSTER_PP_INVOCATION_TOKENS 256u
#define BUSTER_PP_INVOCATION_BYTES 2048u

// Whether the identifier at `bytes` is spelled plainly (no line splice, no trigraph, no UCN) and isn't an operator the
// preprocessor reads in text, _Pragma or __pragma: pp_output_invocation_place's name and the identifiers among its
// arguments.
static bool pp_plain_name(const uint8_t* bytes, uint32_t length)
{
    bool result = (length != 7 || memcmp(bytes, "_Pragma", 7) != 0) && (length != 8 || memcmp(bytes, "__pragma", 8) != 0);
    for (uint32_t i = 0; result && i < length; i += 1)
    {
        result = bytes[i] != '\\' && bytes[i] != '?';
    }
    return result;
}

// pp_output_expansion_begin's place for output token `token`, without the expansion history, where the file alone
// says it: the token comes from a macro, and between the file tokens the output has before and after the expansion
// it is in, the file has one invocation, a plainly spelled name and at most one parenthesized argument list, with no
// directive and no other token. All the output between those file tokens is then that invocation's, and its place is
// the name's, as clang's getExpansionLoc gives it. Returns 0 where the file can't say so, for the history to answer:
// -Wempty-body's indentation check asks this first (#628: on sqlite3.c, an `assert` after a `for (...);` made the
// check replay the whole unit for its history).
static __attribute__((unused)) uint32_t pp_output_invocation_place(const Preprocessor* pp, uint32_t token)
{
    uint32_t result = 0;
    const PPTokens* output = &pp->output;
    const bool from_macro = token < output->count && output->locations != NULL && (output->flags[token] & BUSTER_PP_FROM_MACRO) != 0;
    uint32_t before = token;
    uint32_t after = token;
    while (from_macro && before > 0 && token - before < BUSTER_PP_INVOCATION_TOKENS && (output->flags[before] & BUSTER_PP_FROM_MACRO) != 0)
    {
        before -= 1;
    }
    while (from_macro && after < output->count && after - token < BUSTER_PP_INVOCATION_TOKENS && (output->flags[after] & BUSTER_PP_FROM_MACRO) != 0)
    {
        after += 1;
    }
    const bool bounded = from_macro && !pp_dollar_punctuation(&pp->options) && (output->flags[before] & BUSTER_PP_FROM_MACRO) == 0 && after < output->count &&
                         (output->flags[after] & BUSTER_PP_FROM_MACRO) == 0;
    const uint32_t first = bounded ? output->locations[before] : 0;
    const uint32_t last = bounded ? output->locations[after] : 0;
    const uint32_t range = first != 0 ? pp_find_range(pp, first) : BUSTER_PP_NONE;
    const PPRange* place = range != BUSTER_PP_NONE ? &pp->ranges[range] : NULL;
    const bool spanned = place != NULL && place->file != BUSTER_PP_NONE && last > first && last - place->base < place->length && last - first <= BUSTER_PP_INVOCATION_BYTES;
    if (spanned)
    {
        // The file's tokens from the one before to the one after, which the lexer stops before: the bytes end there.
        uint8_t kinds[BUSTER_PP_INVOCATION_BYTES + BUSTER_C_TOKENIZER_OUTPUT_SLACK];
        uint32_t starts[BUSTER_PP_INVOCATION_BYTES + BUSTER_C_TOKENIZER_OUTPUT_SLACK];
        uint32_t ends[BUSTER_PP_INVOCATION_BYTES + BUSTER_C_TOKENIZER_OUTPUT_SLACK];
        uint64_t resume = 0;
        const uint32_t begin = first - place->base;
        const uint32_t end = last - place->base;
        const uint64_t count = c_tokenize_scalar_range(place->bytes, end, begin, false, end, kinds, starts, ends, 0, pp->refine_flags, &resume);
        // The token before, a name, and nothing or a parenthesized list that ends where the token after starts.
        bool shaped = count >= 2 && starts[0] == begin && (kinds[1] & ~BUSTER_C_TOKEN_START_OF_LINE) == C_TOKEN_RAW_IDENTIFIER &&
                      pp_plain_name(place->bytes + starts[1], ends[1] - starts[1]) &&
                      (count == 2 || (kinds[2] & ~BUSTER_C_TOKEN_START_OF_LINE) == C_TOKEN_L_PAREN);
        uint32_t depth = 0;
        for (uint64_t i = 2; shaped && i < count; i += 1)
        {
            const uint8_t kind = kinds[i] & ~BUSTER_C_TOKEN_START_OF_LINE;
            depth += kind == C_TOKEN_L_PAREN;
            shaped = (kind != C_TOKEN_R_PAREN || depth != 0) && (kind != C_TOKEN_HASH || (kinds[i] & BUSTER_C_TOKEN_START_OF_LINE) == 0) &&
                     (kind != C_TOKEN_RAW_IDENTIFIER || pp_plain_name(place->bytes + starts[i], ends[i] - starts[i])) && kind != C_TOKEN_UNKNOWN;
            depth -= kind == C_TOKEN_R_PAREN;
            // The list closes at the last token.
            shaped = shaped && (depth != 0) == (i + 1 < count);
        }
        result = shaped ? place->base + starts[1] : 0;
    }
    return result;
}

// Every output token's system level, in `levels` (room for pp->output.count), by the expansion history: its expansion
// location's (pp_expansion_location, followed once per run of one origin), or its own location's for a token no
// expansion made. The history must be recorded (pp_history_ensure). This is pp_system_levels' twin, and the judgment
// where the runs can't make it (pp->system_runs_inexact).
static __attribute__((unused)) void pp_system_levels_history(const Preprocessor* pp, uint8_t* levels)
{
    PPLevelCursor cursor = {
        .range = BUSTER_PP_NONE,
        .note = BUSTER_PP_NONE,
    };
    uint32_t run = 0;
    uint32_t origin = 0;
    uint32_t expansion = 0;
    for (uint32_t i = 0; i < pp->output.count; i += 1)
    {
        while (run < pp->history_run_count && pp->history_runs[run].token <= i)
        {
            run += 1;
        }
        const uint32_t run_origin = run != 0 ? pp->history_runs[run - 1].origin : 0;
        expansion = run_origin != 0 && run_origin != origin ? pp_expansion_location(pp, i, run_origin) : expansion;
        origin = run_origin;
        levels[i] = pp_system_level_next(pp, &cursor, origin != 0 ? expansion : pp->output.locations[i]);
    }
}

// Every output token's system level, in `levels` (room for pp->output.count), from the runs the main loop kept
// (PPSystemRun), without the history. A token a macro made (BUSTER_PP_FROM_MACRO) has its run's: where the main loop read
// the invocation it came from, which is its expansion location. Another has its own location's, its run's when it is in
// the run's range at or after the run's start, as the file's text is, and otherwise looked up (pp_system_level_at). 64
// tokens at a time: one compare finds the tokens a macro made, four the locations in the run's part of its range, and
// the run's level is stored to all of them. Only for runs the main loop could make exactly (!pp->system_runs_inexact);
// pp_system_levels_history is the twin.
static __attribute__((unused)) void pp_system_levels(const Preprocessor* pp, uint8_t* levels)
{
    const PPTokens* out = &pp->output;
    const __m512i made = _mm512_set1_epi8(BUSTER_PP_FROM_MACRO);
    for (uint32_t r = 0; r <= pp->system_run_count; r += 1)
    {
        // The tokens before the first run, then each run's.
        const PPSystemRun* run = r != 0 ? &pp->system_runs[r - 1] : 0;
        const uint32_t begin = run != 0 ? run->token : 0;
        const uint32_t end = r < pp->system_run_count ? pp->system_runs[r].token : out->count;
        const bool ranged = run != 0 && run->range < pp->range_count;
        const uint32_t from = ranged ? run->from : 0;
        const uint32_t span = ranged ? pp->ranges[run->range].base + pp->ranges[run->range].length - from : 0;
        const __m512i froms = _mm512_set1_epi32((int)from);
        const __m512i spans = _mm512_set1_epi32((int)span);
        const __m512i level = _mm512_set1_epi8((char)(run != 0 ? run->level : BUSTER_PP_USER_FILE));
        for (uint32_t i = begin; i < end; i += 64)
        {
            const __mmask64 lanes = _bzhi_u64(~0ull, end - i < 64 ? end - i : 64);
            const __mmask64 macro = _mm512_mask_test_epi8_mask(lanes, _mm512_maskz_loadu_epi8(lanes, out->flags + i), made);
            uint64_t inside = 0;
            for (uint32_t g = 0; g < 4; g += 1)
            {
                const __mmask16 group = (__mmask16)(lanes >> (16 * g));
                const __m512i offsets = _mm512_sub_epi32(_mm512_maskz_loadu_epi32(group, out->locations + i + 16 * g), froms);
                inside |= (uint64_t)_mm512_mask_cmplt_epu32_mask(group, offsets, spans) << (16 * g);
            }
            _mm512_mask_storeu_epi8(levels + i, lanes, level);
            uint64_t others = lanes & ~macro & ~inside;
            while (others != 0)
            {
                const uint32_t k = (uint32_t)_tzcnt_u64(others);
                levels[i + k] = pp_system_level_at(pp, out->locations[i + k]);
                others &= others - 1;
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// The output

// The whole output's keywords by pp_resolve_keywords_range: the parser's handoff tests and benchmark (#335) time it and the
// conversion after it against pp_resolve_keywords_scalar.

// Can `c` go on an identifier or a pp-number? '$' can unless it is punctuation (pp_dollar_punctuation).
static bool pp_word_character(uint8_t c, bool dollar_punctuation)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || (c == '$' && !dollar_punctuation) || c >= 0x80 || c == '\\';
}

// Prints the output as `-E` does: tokens with a space where whitespace was before them (or where they would otherwise
// lex as one), each line of the source on a line of its own, pragmas on lines of their own, and unless `-P`
// (`markers` false) a line marker (`# 12 "file.h" 2`) wherever the output moves to another file or skips lines. A
// token at a time through stdio: the scalar kernels' printer, and the oracle of pp_print_vector.
// -E's line markers, kept the same way by both printers as clang's PrintPPOutputPPCallbacks keeps them: the include
// stack's top (a range), and the presumed line, #line offset, name and system level of the output's current line. The
// main file's range roots the stack, since clang includes its <built-in> buffer, and with it the forced includes, from
// the main file. Per range, the latest of its line notes passed so far (`cursors`, BUSTER_PP_NONE before the first);
// the next expansion start to pass; and the text of the markers one step writes, in the printer's scratch arena.
typedef struct PPMarkers PPMarkers;
struct PPMarkers
{
    uint32_t range;
    uint32_t line;
    int64_t delta;
    const char* name;
    uint32_t name_length;
    uint8_t system;
    bool keep_line;
    uint32_t root;
    uint32_t* cursors;
    uint32_t expansion;
    uint32_t token;
    const PPIncludeExit* include_exit;
    Arena* arena;
    char* text;
    uint64_t length;
    uint64_t capacity;
};

// A position as a range's line notes give it.
typedef struct PPPresumed PPPresumed;
struct PPPresumed
{
    uint32_t line;
    int64_t delta;
    const char* name;
    uint32_t name_length;
    uint8_t system;
};

// The text -E prints for an #embed's bytes (BUSTER_PP_KIND_EMBED), clang's: their values in decimal, separated by ", ",
// and with `separated` a ", " before the first (the bytes go on from earlier ones). 16 bytes at a time: the digits from
// each value / 10 and / 100, by multiplications in 16-bit lanes, with a leading zero digit as a zero byte; then the
// separator and the three digits of each byte in an 8-byte slot of their own, by one vpermb per 8 bytes from the digits
// in 16-byte quarters (hundreds, tens, ones, separator); then the slots' nonzero bytes packed together by vpcompressb and
// stored whole. Writes at most 5 bytes per byte, and stores up to 64 bytes past them (the output needs that slack).
// Returns how many it wrote. pp_embed_text_scalar is its twin.

// The input bytes of an #embed the printers format at a time: their text fits in the printers' buffers with the vector
// kernel's slack.
#define BUSTER_PP_EMBED_PRINT_BYTES 4096u


// The printer's directory of the location space: for each page of 2^BUSTER_PP_DIRECTORY_SHIFT locations (256: a small
// header's range is a few pages), the first range that reaches into it. Ranges are in the order of their bases, so a
// location's range is a load and seldom a step forward (pp_directory_range) rather than pp_find_range's binary search,
// for every token printed.
#define BUSTER_PP_DIRECTORY_SHIFT 8


// The classes pp_print_vector's separation table reads, of a token's first character (the token after) and of a
// token's last one (the token before), by character for ASCII; every byte past 0x7F is a word character. After: other,
// word, digit, '.', '\'', '"', '+' or '-', another character that joins punctuators. Before: other, word (digits too),
// '.', a joining character, and (from the kind, not the character) a pp-number.
#define BUSTER_PP_AFTER_WORD 1
#define BUSTER_PP_BEFORE_WORD 1
#define BUSTER_PP_BEFORE_NUMBER 4


// The physical line of `location` in its range's file, for the printer: counted from the last line the printer asked
// about when that was earlier in the same range (pp_count_lines over the bytes between), else from pp_line_of's cursor.
// The printer asks in order of the output, which is mostly the order of each file's lines.
typedef struct PPPrintLines PPPrintLines;
struct PPPrintLines
{
    uint32_t range;
    uint32_t offset;
    uint32_t line;
};

// The output offsets of the spellings of a batch of at most 64 nonempty spellings of at most 64 bytes, each after its
// gap: one byte for each bit of `ones` (a space, or with -P a line start's break), and with `breaks` not null its own
// count of bytes, at most BUSTER_PP_PRINT_GAP (with line markers, a line start's breaks). A gap ends where its spelling
// starts. Two 32-word prefix sums remove the dependence between successive stores. Inactive input lanes are never read
// and only `count` offsets are written. The bounded batch needs at most 64 * (64 + 1 + 8) bytes, so its word sums
// cannot overflow. Returns the batch's bytes, or zero when a spelling needs the general printer; the scalar printer
// remains the independent output oracle.
#define BUSTER_PP_PRINT_GAP 8

// The most ranges pp_print_spellings places a chunk's tokens in. On an AVX-512 Xeon, chunks of 64 tokens in 8 ranges took
// 180 ns (the general loop's lookups 220 to 250), in 16 ranges 330 to 360 ns (245 to 265), and in 64 1,200 to 1,320 ns
// (270 to 290); no chunk of main.c's or sqlite3.c's has more than 7.
#define BUSTER_PP_PRINT_RANGES 8


// Prints the output as `-E` does: pp_print_vector, or pp_print_scalar for the scalar kernels.
BUSTER_PREPROCESSOR_ENTRY bool pp_print(Preprocessor* pp, FILE* stream, bool markers);

// Orders two macro names as clang's -dM does: bytewise, a prefix first.
static int pp_compare_names(const Preprocessor* pp, IdentId a, IdentId b)
{
    const uint32_t a_length = intern_length(pp->table, a);
    const uint32_t b_length = intern_length(pp->table, b);
    const int compared = memcmp(intern_bytes(pp->table, a), intern_bytes(pp->table, b), a_length < b_length ? a_length : b_length);
    return compared != 0 ? compared : a_length < b_length ? -1 : a_length > b_length ? 1 : 0;
}

// -dM: every macro defined at the end of the translation unit but the builtins, as `clang -dM -E` prints them
// (PrintMacroDefinition): sorted by name, `#define NAME`, a function-like macro's parameters (`(a,b,...)`, GNU's
// `x...`), a space, and the replacement list's tokens, each after a space where whitespace was before it. A token's
// spelling is clang's: an identifier's interned name (UTF-8), or its spelling as written when that has a UCN, and any
// other token's bytes without their line splices. Returns false if memory runs out or the stream fails.
static __attribute__((unused)) bool pp_print_macros(Preprocessor* pp, FILE* stream)
{
    Scratch scratch = scratch_begin(0);
    // The names, then sorted by a bottom-up merge sort (between `names` and `other`).
    // The macros are the set bits of is_macro, over the ids the intern table has.
    const uint32_t words = (pp->table->count + 63) / 64;
    uint32_t count = 0;
    for (uint32_t w = 0; w < words; w += 1)
    {
        count += (uint32_t)__builtin_popcountll(pp->is_macro[w]);
    }
    IdentId* names = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, IdentId, (uint64_t)count + 1);
    IdentId* other = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, IdentId, (uint64_t)count + 1);
    bool result = names != 0 && other != 0;
    count = 0;
    for (uint32_t w = 0; result && w < words; w += 1)
    {
        for (uint64_t bits = pp->is_macro[w]; bits != 0; bits &= bits - 1)
        {
            const IdentId id = w * 64 + (uint32_t)__builtin_ctzll(bits);
            names[count] = id;
            count += !(pp->macros[pp->macro_of[id]].flags & BUSTER_PP_MACRO_BUILTIN);
        }
    }
    for (uint32_t width = 1; result && width < count; width *= 2)
    {
        for (uint32_t begin = 0; begin < count; begin += 2 * width)
        {
            const uint32_t middle = begin + width < count ? begin + width : count;
            const uint32_t end = begin + 2 * width < count ? begin + 2 * width : count;
            uint32_t i = begin;
            uint32_t j = middle;
            for (uint32_t k = begin; k < end; k += 1)
            {
                const bool left = i < middle && (j == end || pp_compare_names(pp, names[i], names[j]) <= 0);
                other[k] = left ? names[i] : names[j];
                i += left;
                j += !left;
            }
        }
        IdentId* swap = names;
        names = other;
        other = swap;
    }
    for (uint32_t m = 0; result && m < count; m += 1)
    {
        const PPMacro* macro = &pp->macros[pp->macro_of[names[m]]];
        const PPFileData* data = &pp->datas[macro->data];
        fprintf(stream, "#define %.*s", (int)intern_length(pp->table, names[m]), intern_bytes(pp->table, names[m]));
        if (macro->flags & BUSTER_PP_MACRO_FUNCTION)
        {
            // The parameter list as written, without its spaces: names, commas and `...`, up to the ')'.
            fputc('(', stream);
            for (uint32_t t = macro->parameters_begin; t + 1 < macro->body_begin; t += 1)
            {
                const uint8_t kind = data->kinds[t] & 0x7F;
                if (kind == C_TOKEN_RAW_IDENTIFIER)
                {
                    fprintf(stream, "%.*s", (int)intern_length(pp->table, data->values[t]), intern_bytes(pp->table, data->values[t]));
                }
                else
                {
                    fputs(kind == C_TOKEN_ELLIPSIS ? "..." : ",", stream);
                }
            }
            fputc(')', stream);
        }
        if (macro->body_begin == macro->body_end || !pp_file_space(data, macro->body_begin))
        {
            fputc(' ', stream);
        }
        for (uint32_t t = macro->body_begin; result && t < macro->body_end; t += 1)
        {
            const uint8_t kind = data->kinds[t] & 0x7F;
            const uint32_t location = macro->base + data->starts[t];
            const uint32_t end = kind == C_TOKEN_RAW_IDENTIFIER ? location + refine_identifier_end(data->bytes, data->length, data->starts[t], pp->refine_flags) - data->starts[t]
                                                                : macro->base + data->values[t];
            const uint64_t mark = scratch.arena->length;
            uint8_t* buffer = arena_push(scratch.arena, (uint64_t)(end - location) + 24, 1);
            result = buffer != 0;
            const uint8_t* spelling = 0;
            uint32_t length = 0;
            if (result && kind == C_TOKEN_RAW_IDENTIFIER)
            {
                length = pp_identifier_as_written(pp, location, buffer, &spelling);
                if (memchr(spelling, '\\', length) == 0)
                {
                    length = pp_spelling(pp, kind, data->values[t], location, buffer, &spelling);
                }
            }
            else if (result)
            {
                length = pp_spelling(pp, kind, macro->base + data->values[t], location, buffer, &spelling);
            }
            if (result)
            {
                fprintf(stream, "%s%.*s", pp_file_space(data, t) ? " " : "", (int)length, spelling);
            }
            arena_pop_to(scratch.arena, mark);
        }
        fputc('\n', stream);
    }
    scratch_end(scratch);
    return result && !ferror(stream);
}

// ---------------------------------------------------------------------------------------------------------------------
// The -E command

// The language mode of a -std= value, or false if it isn't one.
static bool pp_parse_standard(const char* value, CLanguage* language)
{
    // C94 (iso9899:199409) is c89 with digraphs, as clang has it (CDigraphs).
    static const struct
    {
        const char* name;
        uint32_t standard;
        bool gnu;
        uint8_t digraphs;
    } standards[] = {
        { "c89", 1989, false }, { "c90", 1989, false }, { "iso9899:1990", 1989, false }, { "iso9899:199409", 1989, false, C_DIGRAPHS_ON },
        { "gnu89", 1989, true }, { "gnu90", 1989, true },
        { "c99", 1999, false }, { "iso9899:1999", 1999, false }, { "gnu99", 1999, true }, { "c11", 2011, false }, { "iso9899:2011", 2011, false },
        { "gnu11", 2011, true }, { "c17", 2017, false }, { "c18", 2017, false }, { "iso9899:2017", 2017, false }, { "iso9899:2018", 2017, false },
        { "gnu17", 2017, true }, { "gnu18", 2017, true }, { "c23", 2023, false }, { "c2x", 2023, false }, { "iso9899:2024", 2023, false },
        { "gnu23", 2023, true }, { "gnu2x", 2023, true }, { "c9x", 1999, false }, { "iso9899:199x", 1999, false }, { "gnu9x", 1999, true },
        { "c1x", 2011, false }, { "iso9899:201x", 2011, false }, { "gnu1x", 2011, true },
    };
    bool result = false;
    for (uint32_t i = 0; !result && i < BUSTER_ARRAY_LENGTH(standards); i += 1)
    {
        if (strcmp(value, standards[i].name) == 0)
        {
            *language = (CLanguage){ .standard = standards[i].standard, .gnu = standards[i].gnu, .digraphs = standards[i].digraphs };
            result = true;
        }
    }
    return result;
}

// The -f options that change what clang's -E gives other than its macros, in families of an option and its -fno- form,
// of which clang's driver reads only the last: the keyword sets (TokenKinds.def's KEYGNU, KEYMS, KEYBORLAND,
// KEYZVECTOR, KEYZOS, KEYFIXEDPOINT and KEYDEFERTS, and __declspec), the lexing of '$', digraphs and raw strings, -E's
// own modes, and modules. pp_take_family records each family's last option, and pp_settle_families models GNU's
// keywords, '$' and digraphs, and refuses the other families where their last option turns them on. Issue #281 has the
// audit of clang 23.1.3's -f options that found them: every other one changes only macros (predefines_option), or
// nothing that -E gives, but char's signedness and wchar_t's width, which predefines_option reads and #if follows
// (pp_parse_character).
typedef enum PPFamily
{
    PP_FAMILY_GNU_KEYWORDS,
    PP_FAMILY_ASM,
    PP_FAMILY_DOLLARS,
    PP_FAMILY_DIGRAPHS,
    PP_FAMILY_RAW_STRINGS,
    PP_FAMILY_MS_EXTENSIONS,
    PP_FAMILY_MS_COMPATIBILITY,
    PP_FAMILY_BORLAND,
    PP_FAMILY_DECLSPEC,
    PP_FAMILY_ZVECTOR,
    PP_FAMILY_ZOS,
    PP_FAMILY_FIXED_POINT,
    PP_FAMILY_DEFER_TS,
    PP_FAMILY_DIRECTIVES_ONLY,
    PP_FAMILY_KEEP_SYSTEM_INCLUDES,
    PP_FAMILY_MINIMIZE_WHITESPACE,
    PP_FAMILY_LINE_DIRECTIVES,
    PP_FAMILY_REWRITE_INCLUDES,
    PP_FAMILY_REWRITE_IMPORTS,
    PP_FAMILY_MODULES,
    PP_FAMILY_IMPLICIT_MODULE_MAPS,
    PP_FAMILY_COUNT,
} PPFamily;

#define BUSTER_PP_KEYWORDS_REFUSED(whose) whose ", which buster doesn't follow"
#define BUSTER_PP_OUTPUT_REFUSED "it changes what clang -E prints, which buster doesn't follow"
#define BUSTER_PP_MODULES_REFUSED "clang's modules, which buster doesn't have"

// Each family's option, its -fno- form (none for -fzvector's: clang's driver never passes -fno-zvector on, and warns
// that it is unused), by their IDs (c_option_table.c), and why buster refuses the option, null where pp_settle_families
// models the family or decides.
static const struct
{
    uint16_t on;
    uint16_t off;
    const char* refused;
} pp_families[PP_FAMILY_COUNT] = {
    [PP_FAMILY_GNU_KEYWORDS] = { C_OPTION_FGNU_KEYWORDS, C_OPTION_FNO_GNU_KEYWORDS, 0 },
    [PP_FAMILY_ASM] = { C_OPTION_FASM, C_OPTION_FNO_ASM, 0 },
    [PP_FAMILY_DOLLARS] = { C_OPTION_FDOLLARS_IN_IDENTIFIERS, C_OPTION_FNO_DOLLARS_IN_IDENTIFIERS, 0 },
    [PP_FAMILY_DIGRAPHS] = { C_OPTION_FDIGRAPHS, C_OPTION_FNO_DIGRAPHS, 0 },
    [PP_FAMILY_RAW_STRINGS] = { C_OPTION_FRAW_STRING_LITERALS, C_OPTION_FNO_RAW_STRING_LITERALS, 0 },
    [PP_FAMILY_MS_EXTENSIONS] = { C_OPTION_FMS_EXTENSIONS, C_OPTION_FNO_MS_EXTENSIONS, BUSTER_PP_KEYWORDS_REFUSED("Microsoft's keywords, lexing and directives") },
    [PP_FAMILY_MS_COMPATIBILITY] = { C_OPTION_FMS_COMPATIBILITY, C_OPTION_FNO_MS_COMPATIBILITY, BUSTER_PP_KEYWORDS_REFUSED("Microsoft's keywords, lexing and directives") },
    [PP_FAMILY_BORLAND] = { C_OPTION_FBORLAND_EXTENSIONS, C_OPTION_FNO_BORLAND_EXTENSIONS, BUSTER_PP_KEYWORDS_REFUSED("Borland's keywords and __declspec") },
    [PP_FAMILY_DECLSPEC] = { C_OPTION_FDECLSPEC, C_OPTION_FNO_DECLSPEC, BUSTER_PP_KEYWORDS_REFUSED("the __declspec keyword and __has_declspec_attribute") },
    [PP_FAMILY_ZVECTOR] = { C_OPTION_FZVECTOR, C_OPTION_NONE, BUSTER_PP_KEYWORDS_REFUSED("System z's vector keywords") },
    [PP_FAMILY_ZOS] = { C_OPTION_FZOS_EXTENSIONS, C_OPTION_FNO_ZOS_EXTENSIONS, BUSTER_PP_KEYWORDS_REFUSED("z/OS's __ptr32 keyword") },
    [PP_FAMILY_FIXED_POINT] = { C_OPTION_FFIXED_POINT, C_OPTION_FNO_FIXED_POINT, BUSTER_PP_KEYWORDS_REFUSED("the fixed-point keywords and macros") },
    [PP_FAMILY_DEFER_TS] = { C_OPTION_FDEFER_TS, C_OPTION_FNO_DEFER_TS, BUSTER_PP_KEYWORDS_REFUSED("the _Defer keyword") },
    [PP_FAMILY_DIRECTIVES_ONLY] = { C_OPTION_FDIRECTIVES_ONLY, C_OPTION_FNO_DIRECTIVES_ONLY, BUSTER_PP_OUTPUT_REFUSED },
    [PP_FAMILY_KEEP_SYSTEM_INCLUDES] = { C_OPTION_FKEEP_SYSTEM_INCLUDES, C_OPTION_FNO_KEEP_SYSTEM_INCLUDES, BUSTER_PP_OUTPUT_REFUSED },
    [PP_FAMILY_MINIMIZE_WHITESPACE] = { C_OPTION_FMINIMIZE_WHITESPACE, C_OPTION_FNO_MINIMIZE_WHITESPACE, BUSTER_PP_OUTPUT_REFUSED },
    [PP_FAMILY_LINE_DIRECTIVES] = { C_OPTION_FUSE_LINE_DIRECTIVES, C_OPTION_FNO_USE_LINE_DIRECTIVES, BUSTER_PP_OUTPUT_REFUSED },
    [PP_FAMILY_REWRITE_INCLUDES] = { C_OPTION_FREWRITE_INCLUDES, C_OPTION_FNO_REWRITE_INCLUDES, BUSTER_PP_OUTPUT_REFUSED },
    [PP_FAMILY_REWRITE_IMPORTS] = { C_OPTION_FREWRITE_IMPORTS, C_OPTION_FNO_REWRITE_IMPORTS, BUSTER_PP_OUTPUT_REFUSED },
    [PP_FAMILY_MODULES] = { C_OPTION_FMODULES, C_OPTION_FNO_MODULES, BUSTER_PP_MODULES_REFUSED },
    [PP_FAMILY_IMPLICIT_MODULE_MAPS] = { C_OPTION_FIMPLICIT_MODULE_MAPS, C_OPTION_FNO_IMPLICIT_MODULE_MAPS, BUSTER_PP_MODULES_REFUSED },
};

// What pp_take_family records for pp_settle_families: each family's last option (1 for its option, 0 for its -fno- form,
// -1 for neither), and the last -fexec-charset= and -finput-charset=.
typedef struct PPFamilies PPFamilies;
struct PPFamilies
{
    int8_t last[PP_FAMILY_COUNT];
    const char* charsets[2];
};

// Takes an argument that is a family's option (PPFamily), or -fexec-charset= or -finput-charset=, or one that buster
// refuses wherever it comes, as it changes clang's -E with no -fno- form: modules', and __FILE__'s remapping (by prefix,
// which takes the value too). `match` is the argument's IDs (c_option_find). `refused` is set for those. Returns false
// for any other argument.
static bool pp_take_family(PPFamilies* families, const char* argument, COptionMatch match, bool* refused)
{
    // Each refusal's prefix: the option through its '=', by ID, or else its text.
    static const struct
    {
        uint16_t joined;
        const char* prefix;
        const char* refused;
    } refusals[] = {
        { C_OPTION_NONE, "-fbuiltin-module-map", BUSTER_PP_MODULES_REFUSED },
        { C_OPTION_FMODULE_NAME_EQ, 0, BUSTER_PP_MODULES_REFUSED },
        { C_OPTION_FMODULE_MAP_FILE_EQ, 0, BUSTER_PP_MODULES_REFUSED },
        { C_OPTION_NONE, "-fdriver-only", "it stops clang before it preprocesses" },
        { C_OPTION_NONE, "-fproc-stat-report", "clang's process statistics, which buster doesn't report" },
        { C_OPTION_FMACRO_PREFIX_MAP_EQ, 0, "a remapping of __FILE__ and __BASE_FILE__, which buster doesn't follow" },
        { C_OPTION_FFILE_PREFIX_MAP_EQ, 0, "a remapping of __FILE__ and __BASE_FILE__, which buster doesn't follow" },
    };
    bool result = false;
    for (uint32_t f = 0; !result && f < PP_FAMILY_COUNT; f += 1)
    {
        const bool on = match.exact == pp_families[f].on;
        const bool off = pp_families[f].off != C_OPTION_NONE && match.exact == pp_families[f].off;
        families->last[f] = on ? 1 : off ? 0 : families->last[f];
        result = on || off;
    }
    for (uint32_t c = 0; !result && c < BUSTER_ARRAY_LENGTH(families->charsets); c += 1)
    {
        result = match.joined == (c == 0 ? C_OPTION_FEXEC_CHARSET_EQ : C_OPTION_FINPUT_CHARSET_EQ);
        families->charsets[c] = result ? argument : families->charsets[c];
    }
    for (uint32_t r = 0; !result && r < BUSTER_ARRAY_LENGTH(refusals); r += 1)
    {
        result = refusals[r].prefix != 0 ? c_option_starts(argument, refusals[r].prefix) : match.joined == refusals[r].joined;
        if (result)
        {
            fprintf(stderr, "error: unsupported option '%s' (%s)\n", argument, refusals[r].refused);
            *refused = true;
        }
    }
    return result;
}

// Settles the families pp_take_family recorded, after every argument, as clang's driver does. GNU's keywords follow the
// last -f[no-]gnu-keywords, or else the last of GCC's -f[no-]asm (which the driver passes on before them), or else the
// mode; '$' and digraphs follow their last options (-fdigraphs makes c89 C94, and -fno-digraphs makes C94 c89:
// CDigraphs). The refusals: a family buster doesn't follow whose last option turns it on, but -fzvector in assembly,
// where clang ignores it; and a last -fexec-charset= or -finput-charset= other than UTF-8 in any case, which clang's
// driver refuses too. Returns false after a refusal.
static bool pp_settle_families(const PPFamilies* families, PPOptions* options)
{
    bool result = true;
    const int8_t* last = families->last;
    const int8_t gnu = last[PP_FAMILY_GNU_KEYWORDS] >= 0 ? last[PP_FAMILY_GNU_KEYWORDS] : last[PP_FAMILY_ASM];
    options->language.gnu_keywords = (uint8_t)(gnu < 0 ? C_GNU_KEYWORDS_MODE : gnu != 0 ? C_GNU_KEYWORDS_ON : C_GNU_KEYWORDS_OFF);
    options->language.digraphs = (uint8_t)(last[PP_FAMILY_DIGRAPHS] < 0 ? options->language.digraphs : last[PP_FAMILY_DIGRAPHS] != 0 ? C_DIGRAPHS_ON : C_DIGRAPHS_OFF);
    options->dollars = (uint8_t)(last[PP_FAMILY_DOLLARS] < 0 ? PP_DOLLARS_MODE : last[PP_FAMILY_DOLLARS] != 0 ? PP_DOLLARS_IDENTIFIER : PP_DOLLARS_PUNCTUATION);
    options->raw_strings = (uint8_t)(last[PP_FAMILY_RAW_STRINGS] < 0 ? PP_RAW_STRINGS_MODE : last[PP_FAMILY_RAW_STRINGS] != 0 ? PP_RAW_STRINGS_ON : PP_RAW_STRINGS_OFF);
    for (uint32_t f = 0; f < PP_FAMILY_COUNT; f += 1)
    {
        const bool refuse = pp_families[f].refused != 0 && last[f] == 1 && !(f == PP_FAMILY_ZVECTOR && options->assembly);
        if (refuse)
        {
            fprintf(stderr, "error: unsupported option '%s' (%s)\n", c_option_spelling(pp_families[f].on), pp_families[f].refused);
        }
        result &= !refuse;
    }
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(families->charsets); c += 1)
    {
        const char* charset = families->charsets[c];
        const char* value = charset != 0 ? strchr(charset, '=') + 1 : "utf-8";
        bool utf8 = strlen(value) == 5;
        for (uint32_t i = 0; utf8 && i < 5; i += 1)
        {
            utf8 = (value[i] >= 'A' && value[i] <= 'Z' ? value[i] | 0x20 : value[i]) == "utf-8"[i];
        }
        if (!utf8)
        {
            fprintf(stderr, "error: invalid value '%s' in '%s'\n", value, charset);
        }
        result &= utf8;
    }
    return result;
}

// Is `argument` one of our options that clang doesn't have and that take the next argument as their value
// (--predefines FILE, --location-limit N, --window-bytes N)? The commands that split -E's options from their files take
// the value with the option, and those that give the options to clang too (test-preprocessor's files and
// --compare-clang, benchmark-preprocessor --clang) leave both out of clang's arguments.
static bool pp_own_option_id_with_value(COption option)
{
    return option == C_OPTION_PREDEFINES || option == C_OPTION_LOCATION_LIMIT || option == C_OPTION_WINDOW_BYTES;
}

static bool pp_own_option_with_value(const char* argument)
{
    return pp_own_option_id_with_value(c_option_exact(argument));
}

// Clang's unsigned integral option grammar accepts decimal, leading-zero octal and 0x/0b/0o prefixes, but no sign.
// Only the final command-line occurrence is passed here, including when an earlier value was malformed.
static bool pp_parse_error_limit(const char* argument, uint32_t* output)
{
    const char* value = argument + 14;
    const char* first = value;
    uint32_t base = *first == '0' ? 8 : 10;
    if (first[0] == '0' && (first[1] == 'x' || first[1] == 'X' || first[1] == 'b' || first[1] == 'B' || first[1] == 'o' || first[1] == 'O'))
    {
        base = first[1] == 'x' || first[1] == 'X' ? 16 : first[1] == 'b' || first[1] == 'B' ? 2 : 8;
        first += 2;
    }
    uint32_t limit = 0;
    bool valid = *first != 0;
    for (const char* at = first; valid && *at != 0; at += 1)
    {
        const uint32_t letter = (uint32_t)(*at | 0x20);
        const uint32_t digit = *at >= '0' && *at <= '9' ? (uint32_t)(*at - '0') : letter >= 'a' && letter <= 'f' ? letter - 'a' + 10 : UINT32_MAX;
        valid = digit < base && limit <= (UINT32_MAX - digit) / base;
        limit = valid ? limit * base + digit : limit;
    }
    if (!valid)
    {
        fprintf(stderr, "error: invalid integral value '%s' in '-ferror-limit %s'\n", value, value);
    }
    *output = limit;
    return valid;
}

// One option grammar serves the single-input API and -E's ordered input collection. A non-null `inputs` has room for
// count entries and retains each input's language at that operand; option values and names after -- are never flags.
// All other options apply to the whole command. Their language-sensitive refusals must hold for every active input.
static bool pp_parse_options_inputs(int count, char** arguments, PPOptions* options, Arena* arena, const char** input, const char** output, bool* markers, PPDriverInput* inputs, uint32_t* input_count)
{
    bool result = true;
    const char** quote = BUSTER_ARENA_PUSH_ARRAY(arena, const char*, (uint64_t)count + 1);
    const char** include = BUSTER_ARENA_PUSH_ARRAY(arena, const char*, (uint64_t)count + 1);
    const char** system = BUSTER_ARENA_PUSH_ARRAY(arena, const char*, (uint64_t)count + 1);
    const char** after = BUSTER_ARENA_PUSH_ARRAY(arena, const char*, (uint64_t)count + 1);
    const char** macros = BUSTER_ARENA_PUSH_ARRAY(arena, const char*, (uint64_t)count + 1);
    const char** includes = BUSTER_ARENA_PUSH_ARRAY(arena, const char*, (uint64_t)count + 1);
    const char** embeds = BUSTER_ARENA_PUSH_ARRAY(arena, const char*, (uint64_t)count + 1);
    const char** warnings = BUSTER_ARENA_PUSH_ARRAY(arena, const char*, (uint64_t)count + 1);
    *options = (PPOptions){ .language = { .standard = 2017, .gnu = true }, .error_limit = BUSTER_PP_DEFAULT_ERROR_LIMIT };
    *input = 0;
    *output = 0;
    *markers = true;
    // -trigraphs, -ftrigraphs and -fno-trigraphs override the mode's default (-1 for none).
    int trigraphs = -1;
    // Fold this diagnostic's option mappings in source order for both possible language defaults. The final -std
    // chooses one: disabling then enabling a default error leaves a warning, while enabling an error preserves it.
    uint8_t implicit_before_c99 = 1;
    uint8_t implicit_library_before_c99 = 2;
    uint8_t implicit_since_c99 = 3;
    bool implicit_mapped = false;
    bool implicit_no_error = false;
    bool implicit_everything = false;
    bool warning_errors = false;
    bool end_options = false;
    CheckDriverInputKind language = CHECK_DRIVER_INPUT_AUTO;
    bool assembly_only = inputs != NULL;
    uint32_t dependency_modes = 0;
    // The last -std= or -ansi, the only one clang's driver reads.
    const char* standard = 0;
    const char* error_limit = 0;
    // The -f options that change clang's keywords, lexing or -E output (pp_settle_families reads them after the loop).
    PPFamilies families = { .charsets = { 0, 0 } };
    memset(families.last, -1, sizeof(families.last));
    bool refused = false;
    result = quote != 0 && include != 0 && system != 0 && after != 0 && macros != 0 && includes != 0 && embeds != 0 && warnings != NULL;
    options->semantic_warning_options = warnings;
    result = result && predefines_options_initialize(&options->predefines, arena, (uint32_t)count);
    for (int i = 0; result && i < count; i += 1)
    {
        // The argument's option IDs (c_option_table.c), which every test below compares; none past --.
        const COptionMatch written = end_options ? (COptionMatch){ .exact = C_OPTION_NONE, .joined = C_OPTION_NONE } : c_option_find(arguments[i]);
        if (!end_options && written.exact == C_OPTION_END_OF_OPTIONS)
        {
            end_options = true;
            continue;
        }
        if (end_options || arguments[i][0] != '-' || written.exact == C_OPTION_STANDARD_INPUT)
        {
            result = inputs != NULL || *input == NULL;
            if (result)
            {
                const CheckDriverInput classified = check_driver_classify(arguments[i], language);
                if (inputs != NULL)
                {
                    inputs[(*input_count)++] = (PPDriverInput){ .path = arguments[i], .kind = classified.kind, .explicit_language = language != CHECK_DRIVER_INPUT_AUTO };
                    assembly_only &= classified.kind == CHECK_DRIVER_INPUT_ASSEMBLY_CPP || classified.kind == CHECK_DRIVER_INPUT_ASSEMBLY;
                }
                if (*input == NULL)
                {
                    *input = arguments[i];
                    options->explicit_language = language != CHECK_DRIVER_INPUT_AUTO;
                    options->assembly = language == CHECK_DRIVER_INPUT_ASSEMBLY_CPP;
                }
            }
            continue;
        }
        // An alias of clang's (-mpascal-strings) is read as the option it stands for (-fpascal-strings), by its IDs.
        const char* argument = predefines_alias(arguments[i], written.exact);
        const COptionMatch match = argument != arguments[i] ? c_option_find(argument) : written;
        const bool malformed_linker = match.exact == C_OPTION_WL || match.joined == C_OPTION_WL_EQ;
        if ((argument[1] == 'W' && !pp_warning_transport(argument) && !malformed_linker) || match.exact == C_OPTION_W || match.exact == C_OPTION_PEDANTIC ||
            match.exact == C_OPTION_PEDANTIC_ERRORS)
        {
            warnings[options->semantic_warning_option_count++] = argument;
        }
        const char* next = i + 1 < count ? arguments[i + 1] : 0;
        // The target's options and the others that change the predefined macros (c_predefines.c) come first.
        const PredefinesParse predefined = predefines_option_match(&options->predefines, argument, match, next);
        if (match.exact == C_OPTION_M || match.exact == C_OPTION_MM || match.exact == C_OPTION_MD || match.exact == C_OPTION_MMD)
        {
            dependency_modes |= match.exact == C_OPTION_M ? 1u : match.exact == C_OPTION_MM ? 2u : match.exact == C_OPTION_MD ? 4u : 8u;
        }
        else if (match.exact == C_OPTION_MP)
        {
            options->dependencies.phony = true;
        }
        else if (argument[1] == 'M' && (argument[2] == 'F' || argument[2] == 'T' || argument[2] == 'Q'))
        {
            const char* value = argument[3] != 0 ? argument + 3 : next;
            i += argument[3] == 0;
            result = value != NULL;
            if (!result)
            {
                fprintf(stderr, "error: missing argument to '%s'\n", argument);
            }
            else if (argument[2] == 'F')
            {
                options->dependencies.path = value;
            }
            else
            {
                if (options->dependencies.targets == NULL)
                {
                    options->dependencies.targets = BUSTER_ARENA_PUSH_ARRAY(arena, DependencyTarget, (uint64_t)count + 1);
                }
                result = options->dependencies.targets != NULL;
                if (result)
                {
                    options->dependencies.targets[options->dependencies.target_count++] = (DependencyTarget){ .text = { .pointer = (char*)value, .length = strlen(value) }, .quote = argument[2] == 'Q' };
                }
            }
        }
        else if (match.exact == C_OPTION_V)
        {
            options->verbose = true;
        }
        else if (match.exact == C_OPTION_STATIC || match.exact == C_OPTION_STATIC_PIE || match.exact == C_OPTION_PIE)
        {
            // Linker modes the C driver selects; preprocessing ignores them, as clang's driver does with -E and -M.
        }
        else if (predefined != PREDEFINES_PARSE_UNKNOWN)
        {
            result = predefined != PREDEFINES_PARSE_ERROR;
            i += predefined == PREDEFINES_PARSE_TAKEN_TWO;
        }
        else if (match.exact == C_OPTION_XLINKER || match.exact == C_OPTION_RPATH)
        {
            // -Xlinker VALUE and -rpath DIRECTORY (clang's and GCC's linker option, #1854): no linker runs for -E.
            result = next != NULL;
            i += result;
            if (!result)
            {
                fprintf(stderr, "error: missing argument to linker option '%s'\n", argument);
            }
        }
        else if (argument[1] == 'W' && argument[2] == 'l' && argument[3] == ',')
        {
            // No linker runs for -E. Do not interpret its payload as a warning option.
        }
        else if (malformed_linker)
        {
            fprintf(stderr, "error: malformed linker option '%s' (use '-Wl,OPTION')\n", argument);
            result = false;
        }
        else if (match.exact == C_OPTION_E || match.exact == C_OPTION_C || match.exact == C_OPTION_W || match.exact == C_OPTION_FHOSTED || match.exact == C_OPTION_PIPE)
        {
            options->freestanding &= match.exact != C_OPTION_FHOSTED;
            options->suppress_warnings |= match.exact == C_OPTION_W;
        }
        else if (match.exact == C_OPTION_PEDANTIC || match.exact == C_OPTION_PEDANTIC_ERRORS)
        {
            // The semantic warning consumer reads these from the ordered options above.
        }
        else if (match.joined == C_OPTION_FERROR_LIMIT_EQ)
        {
            error_limit = argument;
        }
        else if (match.exact == C_OPTION_FERROR_LIMIT)
        {
            fprintf(stderr, "error: unknown argument '-ferror-limit'; use '-ferror-limit=N'\n");
            result = false;
        }
        else if (match.exact == C_OPTION_FNO_CARET_DIAGNOSTICS || match.exact == C_OPTION_FCARET_DIAGNOSTICS)
        {
            options->no_diagnostic_summary = match.exact == C_OPTION_FNO_CARET_DIAGNOSTICS;
        }
        else if (match.exact == C_OPTION_P)
        {
            *markers = false;
        }
        else if (match.exact == C_OPTION_WSYSTEM_HEADERS || match.exact == C_OPTION_WNO_SYSTEM_HEADERS)
        {
            options->system_header_warnings = match.exact == C_OPTION_WSYSTEM_HEADERS;
            options->implicit_function_system = options->system_header_warnings;
        }
        else if (match.exact == C_OPTION_WFATAL_ERRORS || match.exact == C_OPTION_WNO_FATAL_ERRORS)
        {
            options->fatal_errors = match.exact == C_OPTION_WFATAL_ERRORS;
        }
        else if (match.exact == C_OPTION_DM)
        {
            options->dump_macros = true;
        }
        else if (match.exact == C_OPTION_H)
        {
            options->show_includes = true;
        }
        else if (match.exact == C_OPTION_CLANG_FEATURES)
        {
            options->clang_features = true;
        }
        else if (match.exact == C_OPTION_FNO_BUILTIN || match.exact == C_OPTION_FBUILTIN)
        {
            options->no_builtin = match.exact == C_OPTION_FNO_BUILTIN;
        }
        else if (c_option_starts(argument, "-fno-builtin-"))
        {
            const char** names = (const char**)options->no_builtin_functions;
            if (names == NULL)
            {
                names = BUSTER_ARENA_PUSH_ARRAY(arena, const char*, (uint64_t)count + 1);
                options->no_builtin_functions = names;
            }
            result = names != NULL;
            if (result)
            {
                names[options->no_builtin_function_count++] = argument + 13;
            }
        }
        else if (match.exact == C_OPTION_FFREESTANDING)
        {
            options->freestanding = true;
        }
        else if (match.exact == C_OPTION_NOSTDINC)
        {
            options->no_standard_includes = true;
        }
        else if (match.exact == C_OPTION_NOBUILTININC)
        {
            options->no_builtin_includes = true;
        }
        else if (match.exact == C_OPTION_BUSTER_HEADERS)
        {
            options->buster_headers = true;
        }
        else if (match.exact == C_OPTION_UNDEF)
        {
            options->undefine = true;
        }
        else if (match.exact == C_OPTION_SCALAR)
        {
            options->scalar = true;
        }
        else if (match.exact == C_OPTION_LOCATION_LIMIT && next != 0)
        {
            // A test option: a location space this many bytes long, to run out of it (#124).
            char* rest;
            const unsigned long long limit = strtoull(next, &rest, 10);
            result = *rest == 0 && limit != 0 && limit <= UINT32_MAX;
            options->location_limit = (uint32_t)limit;
            i += 1;
        }
        else if (match.exact == C_OPTION_WINDOW_BYTES && next != 0)
        {
            // A test option: the windows a large file is lexed and refined in (pp_prepare_windows), this many bytes, so
            // that small ones send every file of two windows or more through them.
            char* rest;
            const unsigned long long bytes = strtoull(next, &rest, 10);
            result = *rest == 0 && bytes != 0 && bytes <= UINT32_MAX / 2;
            options->window_bytes = (uint32_t)bytes;
            i += 1;
        }
        else if (match.exact == C_OPTION_TRIGRAPHS || match.exact == C_OPTION_FTRIGRAPHS || match.exact == C_OPTION_FNO_TRIGRAPHS)
        {
            trigraphs = match.exact != C_OPTION_FNO_TRIGRAPHS;
        }
        else if (argument[1] == 'm')
        {
            // predefines_option takes every -m option clang uses for x86-64 Linux: this one clang doesn't know, or
            // refuses or ignores for the target.
            fprintf(stderr, "error: unknown or unsupported option '%s' for x86-64 Linux\n", argument);
            result = false;
        }
        else if (pp_take_family(&families, argument, match, &refused))
        {
            result = !refused;
        }
        else if (match.joined == C_OPTION_EMBED_DIR_EQ)
        {
            embeds[options->embed_directory_count] = argument + 12;
            options->embed_directory_count += 1;
        }
        else if (match.exact == C_OPTION_WNO_IMPLICIT_FUNCTION_DECLARATION || match.exact == C_OPTION_WNO_IMPLICIT ||
                 match.exact == C_OPTION_WNO_ALL || match.exact == C_OPTION_WNO_EVERYTHING)
        {
            implicit_before_c99 = 1;
            implicit_library_before_c99 = 1;
            implicit_since_c99 = 1;
            implicit_mapped = true;
            implicit_everything &= match.exact != C_OPTION_WNO_EVERYTHING;
        }
        else if (match.exact == C_OPTION_WNO_ERROR_EQ_IMPLICIT_FUNCTION_DECLARATION || match.exact == C_OPTION_WNO_ERROR_EQ_IMPLICIT ||
                 match.exact == C_OPTION_WNO_ERROR_IMPLICIT_FUNCTION_DECLARATION || match.exact == C_OPTION_WNO_ERROR_EQ_ALL)
        {
            implicit_before_c99 = implicit_before_c99 == 3 ? 2 : implicit_before_c99;
            implicit_library_before_c99 = implicit_library_before_c99 == 3 ? 2 : implicit_library_before_c99;
            implicit_since_c99 = implicit_since_c99 == 3 ? 2 : implicit_since_c99;
            implicit_no_error = true;
        }
        else if (match.exact == C_OPTION_WERROR_EQ_IMPLICIT_FUNCTION_DECLARATION || match.exact == C_OPTION_WERROR_EQ_IMPLICIT ||
                 match.exact == C_OPTION_WERROR_IMPLICIT_FUNCTION_DECLARATION || match.exact == C_OPTION_WERROR_EQ_ALL)
        {
            implicit_before_c99 = 3;
            implicit_library_before_c99 = 3;
            implicit_since_c99 = 3;
            implicit_mapped = true;
        }
        else if (match.exact == C_OPTION_WIMPLICIT_FUNCTION_DECLARATION || match.exact == C_OPTION_WIMPLICIT || match.exact == C_OPTION_WALL)
        {
            implicit_before_c99 = implicit_before_c99 == 1 ? 2 : implicit_before_c99;
            implicit_library_before_c99 = implicit_library_before_c99 == 1 ? 2 : implicit_library_before_c99;
            implicit_since_c99 = implicit_since_c99 == 1 ? 2 : implicit_since_c99;
            implicit_mapped = true;
        }
        else if (match.exact == C_OPTION_WEVERYTHING)
        {
            implicit_everything = true;
        }
        else if (match.exact == C_OPTION_WERROR || match.exact == C_OPTION_WNO_ERROR)
        {
            warning_errors = match.exact == C_OPTION_WERROR;
        }
        else if (match.exact == C_OPTION_FPREPROCESSED)
        {
            // Rejected as an option here, where operands of value options were already skipped, rather than by scanning
            // the filtered vector, which keeps operands and post-`--` inputs (#1552's review).
            fprintf(stderr, "error: unsupported option '-fpreprocessed' (already-preprocessed input is not read that way)\n");
            result = false;
        }
        else if (argument[1] == 'f' || argument[1] == 'W')
        {
            // The other code generation and warning options: none changes what -E gives (predefines_option takes the
            // ones that change macros, and refuses those buster doesn't follow, and pp_take_family those that change
            // clang's keywords, lexing or -E output).
        }
        else if (match.joined == C_OPTION_STD_EQ || match.exact == C_OPTION_ANSI)
        {
            standard = argument;
        }
        else if (argument[1] == 'x')
        {
            const char* value = argument[2] != 0 ? argument + 2 : next;
            i += argument[2] == 0;
            language = value != NULL ? check_driver_language(value) : CHECK_DRIVER_INPUT_INVALID;
            result = language != CHECK_DRIVER_INPUT_INVALID;
            if (!result)
            {
                fprintf(stderr, "error: unsupported or missing input language '%s'\n", value != NULL ? value : "");
            }
        }
        else if ((match.exact == C_OPTION_O || match.exact == C_OPTION_PREDEFINES || match.exact == C_OPTION_INCLUDE || match.exact == C_OPTION_ISYSTEM ||
                  match.exact == C_OPTION_IQUOTE || match.exact == C_OPTION_IDIRAFTER) && next != 0)
        {
            if (argument[1] == 'o')
            {
                *output = next;
            }
            else if (argument[2] == 'p')
            {
                options->predefines_path = next;
            }
            else if (argument[2] == 'n')
            {
                includes[options->include_count] = next;
                options->include_count += 1;
            }
            else if (argument[2] == 's')
            {
                system[options->system_directory_count] = next;
                options->system_directory_count += 1;
            }
            else if (argument[2] == 'q')
            {
                quote[options->quote_directory_count] = next;
                options->quote_directory_count += 1;
            }
            else
            {
                after[options->after_directory_count] = next;
                options->after_directory_count += 1;
            }
            i += 1;
        }
        else if (argument[0] == '-' && (argument[1] == 'I' || argument[1] == 'D' || argument[1] == 'U'))
        {
            const char* value = argument[2] != 0 ? argument + 2 : next;
            i += argument[2] == 0;
            result = value != 0;
            if (result && argument[1] == 'I' && strcmp(value, "-") == 0)
            {
                // The obsolete -I- split. Clang rejects it; a directory literally named "-" is -I./- .
                fprintf(stderr, "error: '-I-' not supported, please use -iquote instead\n");
                result = false;
            }
            else if (result && argument[1] == 'I')
            {
                include[options->include_directory_count] = value;
                options->include_directory_count += 1;
            }
            else if (result)
            {
                // "D<macro>" or "U<macro>" for pp_run.
                char* macro = arena_push(arena, strlen(value) + 2, 1);
                result = macro != 0;
                if (result)
                {
                    macro[0] = argument[1];
                    memcpy(macro + 1, value, strlen(value) + 1);
                    macros[options->macro_count] = macro;
                    options->macro_count += 1;
                }
            }
        }
        else
        {
            fprintf(stderr, "unknown argument: %s\n", argument);
            result = false;
        }
    }
    // Clang's driver tests presence, not last occurrence: side effects beat dependency-only output, -MM beats -M,
    // and an explicit -M/-MM controls system-header inclusion even alongside -MD/-MMD.
    options->dependencies.enabled = dependency_modes != 0;
    options->dependencies.only = dependency_modes != 0 && (dependency_modes & 12u) == 0;
    options->dependencies.include_system = (dependency_modes & 3u) != 0 ? (dependency_modes & 2u) == 0 : (dependency_modes & 8u) == 0;
    options->suppress_warnings |= (dependency_modes & 3u) != 0;
    options->negative_conversion_error = warning_errors;
    result = result && (error_limit == 0 || pp_parse_error_limit(error_limit, &options->error_limit));
    // -ansi is -std=c89 in C.
    const char* mode = standard == 0 ? 0 : standard[1] == 'a' ? "c89" : standard + 5;
    if (result && mode != 0 && !pp_parse_standard(mode, &options->language))
    {
        fprintf(stderr, "error: invalid value '%s' in '%s'\n", mode, standard);
        result = false;
    }
    uint8_t implicit = options->language.standard >= 1999 ? implicit_since_c99 : implicit_before_c99;
    implicit = implicit == 1 && !implicit_mapped && implicit_everything ? 2 : implicit;
    options->implicit_function_diagnostic = implicit == 2 && !implicit_no_error && warning_errors ? 3 : implicit;
    const uint8_t library = implicit_library_before_c99 == 2 && !implicit_no_error && warning_errors ? 3 : implicit_library_before_c99;
    options->implicit_library_diagnostic = options->language.standard >= 1999 ? options->implicit_function_diagnostic : library;
    options->implicit_builtin_diagnostic = implicit_since_c99 == 2 && !implicit_no_error && warning_errors ? 3 : implicit_since_c99;
    options->quote_directories = quote;
    options->include_directories = include;
    options->system_directories = system;
    options->after_directories = after;
    options->trigraphs = trigraphs >= 0 ? trigraphs != 0 : !options->language.gnu && options->language.standard < 2023;
    options->macros = macros;
    options->includes = includes;
    options->assembly = inputs != NULL ? assembly_only : options->assembly;
    // The -f families, after the language mode (pp_settle_families).
    result = result && pp_settle_families(&families, options);
    options->embed_directories = embeds;
    // What clang's driver decides from the whole command line (the sanitizers, -flto and -fvisibility='s values), and the
    // generated code's instruction set, from the target options as clang resolves them; each reports what doesn't
    // resolve (an incompatible pair of sanitizers, an unknown CPU, -mfpmath against the features), as clang's driver does.
    result = result && predefines_finish(&options->predefines);
    result = result && predefines_target(&options->predefines, &options->target, &options->tune);
    return result && *input != 0;
}

// The preprocessor's single-input API. Later -x options are validated but do not change the selected input's language.
BUSTER_PREPROCESSOR_ENTRY bool pp_parse_options(int count, char** arguments, PPOptions* options, Arena* arena, const char** input, const char** output, bool* markers);

static bool pp_option_id_takes_value(COption option);
static bool pp_option_takes_value(const char* argument);

// A failed run's -o output removed, as clang removes it: a regular file, or a symbolic link (the link alone, not what it
// names); never a device, a FIFO or a socket, which the run never wrote. So `-E bad.c -o /dev/null` leaves /dev/null
// alone, which run as root it would otherwise delete (#1622).
static void pp_driver_remove_output(const char* output)
{
    struct stat status;
    const bool removable = lstat(output, &status) == 0 && (S_ISREG(status.st_mode) || S_ISLNK(status.st_mode));
    if (removable)
    {
        unlink(output);
    }
}

// Verbose output describes the integrated stages rather than inventing external cc1/as/ld subprocesses. It goes to
// stderr, so preprocessing, macro queries, dependency rules and generated stdout remain machine-readable.
static void pp_driver_verbose(const PPOptions* options, const char* input)
{
    if (options->verbose)
    {
        char target[256];
        predefines_target_triple(&options->predefines, target, sizeof(target));
        fprintf(stderr, "buster (development build)\nTarget: %s\nbuster: preprocessing %s\n", target, strcmp(input, "-") == 0 ? "<stdin>" : input);
    }
}

// `compiler -E/-M/-MM`: option operands are never inputs or actions. Each input has a private preprocessor and rule;
// explicit -o requires a single input, while shared -MF publication follows argv order after all units succeed.
// `.i` with no explicit -x is already preprocessed. `-x c file.i` stays ordinary C; -E's one -x applies to every input.
static bool pp_suffix_preprocessed(const char* path, bool explicit_language)
{
    const char* slash = path != NULL ? strrchr(path, '/') : NULL;
    const char* name = slash != NULL ? slash + 1 : path;
    const char* dot = name != NULL ? strrchr(name, '.') : NULL;
    return !explicit_language && dot != NULL && strcmp(dot, ".i") == 0;
}

static __attribute__((unused)) int run_preprocessor_command_mode(int count, char** arguments, bool release)
{
    arena_failure = (ArenaFailure){ 0 };
    int result = 1;
    Arena arena = arena_reserve(1ull << 30);
    PPOptions options = { 0 };
    const char* input = NULL;
    const char* output = NULL;
    bool markers = true;
    PPDriverInput* inputs = BUSTER_ARENA_PUSH_ARRAY(&arena, PPDriverInput, (uint64_t)count + 1);
    char** paths = BUSTER_ARENA_PUSH_ARRAY(&arena, char*, (uint64_t)count + 1);
    DependencyOutput* dependencies = BUSTER_ARENA_PUSH_ARRAY(&arena, DependencyOutput, (uint64_t)count + 1);
    uint32_t input_count = 0;
    uint32_t active_count = 0;
    uint32_t stdin_count = 0;
    bool parsed = inputs != NULL && paths != NULL && dependencies != NULL &&
                  pp_parse_options_inputs(count, arguments, &options, &arena, &input, &output, &markers, inputs, &input_count);
    for (uint32_t i = 0; parsed && i < input_count; i += 1)
    {
        // Plain assembly has no preprocessing action, including on stdin. Do not read or publish it as C text.
        if (inputs[i].kind != CHECK_DRIVER_INPUT_ASSEMBLY)
        {
            inputs[active_count] = inputs[i];
            paths[active_count++] = inputs[i].path;
            stdin_count += strcmp(inputs[i].path, "-") == 0;
        }
    }
    input_count = active_count;
    options.expansion_starts = markers;
    options.defer_history = true;
    if (parsed && ((output != NULL && input_count > 1) || stdin_count > 1))
    {
        fprintf(stderr, "error: -o and standard input each require a single output/input respectively\n");
        parsed = false;
    }
    parsed = parsed && dependency_check_destinations(&options.dependencies, paths, input_count, output, false, false, &arena);
    if (parsed && stdin_count != 0)
    {
        parsed = driver_read_stdin(&arena, &options.stdin_source);
        options.stdin_ready = parsed;
    }
    parsed = parsed && driver_cache_streams(&arena, paths, input_count, &options);
    if (arena.base == NULL || (!parsed && arena_failure.bytes != 0))
    {
        arena_report_failure(stderr, "preprocessing", BUSTER_PP_ARENA_BYTES);
    }
    else if (!parsed)
    {
        fprintf(stderr, "usage: compiler -E/-M/-MM [-MD/-MMD] [-MF file] [-MT target] [-MQ target] [-MP] [-P] [-o file] [-x c] [-I dir] [-isystem dir] [-D name[=value]] [-U name] [-include file] [-dM] [-H] [options] file...\n");
    }
    else
    {
        static InternTable table;
        static Preprocessor pp;
        result = 0;
        for (uint32_t unit = 0; unit < input_count; unit += 1)
        {
            input = inputs[unit].path;
            options.assembly = inputs[unit].kind == CHECK_DRIVER_INPUT_ASSEMBLY_CPP;
            options.explicit_language = inputs[unit].explicit_language;
            dependencies[unit] = (DependencyOutput){ 0 };
            options.preprocessed = pp_suffix_preprocessed(input, options.explicit_language);
            pp_driver_verbose(&options, input);
            if (!intern_initialize(&table) || !pp_initialize(&pp, &options, &table))
            {
                if (!pp.fatal)
                {
                    arena_report_failure(stderr, "preprocessing", BUSTER_PP_ARENA_BYTES);
                }
                result = 1;
            }
            else
            {
                const bool preprocessed = pp_run_input(&pp, input);
                bool printed = true;
                const bool file_output = output != NULL && strcmp(output, "-") != 0;
                // Do not create or truncate -o until preprocessing succeeded. A failure removes a previous file,
                // as clang and gcc do, instead of leaving the truncated output (pp_driver_remove_output).
                if (!preprocessed && file_output)
                {
                    pp_driver_remove_output(output);
                }
                if (preprocessed && !options.dependencies.only)
                {
                    FILE* stream = file_output ? fopen(output, "w") : stdout;
                    printed = stream != NULL && (options.dump_macros ? pp_print_macros(&pp, stream) : pp_print(&pp, stream, markers));
                    if (stream != NULL)
                    {
                        printed = (stream == stdout ? fflush(stream) : fclose(stream)) == 0 && printed;
                    }
                    if (!printed)
                    {
                        fprintf(stderr, "error: can't write preprocessor output%s%s\n", output != NULL ? " " : "", output != NULL ? output : "");
                    }
                }
                const bool prepared = preprocessed && printed && pp_dependencies(&pp, input, output, &arena, dependencies + unit, stderr);
                if (!prepared && file_output)
                {
                    pp_driver_remove_output(output);
                }
                result = prepared ? result : 1;
                pp_diagnostic_summary(&pp);
            }
            if (release || unit + 1 < input_count)
            {
                pp_release(&pp);
                intern_release(&table);
            }
        }
        for (uint32_t unit = 0; result == 0 && unit < input_count; unit += 1)
        {
            result = dependency_publish(dependencies + unit, stderr) ? 0 : 1;
        }
    }
    if (release)
    {
        arena_release(&arena);
    }
    return result;
}

// Reusable command callers release all mappings. Main's terminal -E route instead lets process exit tear them down;
// the output file has already been closed, or stdout remains open for libc to flush at exit, in either mode.
static __attribute__((unused)) int run_preprocessor_command(int count, char** arguments)
{
    return run_preprocessor_command_mode(count, arguments, true);
}

// ---------------------------------------------------------------------------------------------------------------------
// clang's arguments for ours
//
// test-preprocessor and benchmark-preprocessor run clang with the options ours reads, to compare with it and to time it.
// clang has no --buster-headers, and no rule like pp_find_include's that keeps its resource directory from supplying a
// name buster's directory has, so it gets ours' whole search instead (pp_clang_arguments), with a directory of links to
// the rest of clang's resource directory in that directory's place.

// Is `option` (an argument's ID, c_option_table.c) an option whose value is the next argument (pp_parse_options)?
static bool pp_option_id_takes_value(COption option)
{
    static const uint16_t options[] = {
        C_OPTION_I,
        C_OPTION_D,
        C_OPTION_U,
        C_OPTION_O,
        C_OPTION_X,
        C_OPTION_INCLUDE,
        C_OPTION_ISYSTEM,
        C_OPTION_IQUOTE,
        C_OPTION_IDIRAFTER,
        C_OPTION_TARGET,
        C_OPTION_MF,
        C_OPTION_MT,
        C_OPTION_MQ,
        C_OPTION_XLINKER,
        C_OPTION_RPATH,
    };
    bool result = pp_own_option_id_with_value(option);
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(options); i += 1)
    {
        result |= options[i] == option;
    }
    return result;
}

// Is `argument` an option whose value is the next argument (pp_parse_options)?
static bool pp_option_takes_value(const char* argument)
{
    return pp_option_id_takes_value(c_option_exact(argument));
}

#if !defined(BUSTER_UNIT) || BUSTER_UNIT == BUSTER_UNIT_FRONTEND || BUSTER_COMPLETE_DRIVER
#include "buster/compiler/c_preprocessor_impl.c"
#else
// Preserve semantic references to shared interfaces without emitting code or disabling unused-function diagnostics.
static __attribute__((unused)) void pp_implementation_interfaces(void)
{
    (void)&x86_target_has_builtin;
    (void)&dependency_prepare;
    (void)&pp_feature;
    (void)&pp_warning_options_diagnose;
    (void)&pp_diagnostic_lines;
    (void)&pp_prepare_options;
    (void)&check_literal_diagnose;
    (void)&check_literal_unevaluated_escapes;
    (void)&pp_token_is_name;
    (void)&pp_utf8_sequence;
    (void)&pp_word_character;
    (void)&pp_option_takes_value;
    (void)&pp_option_id_takes_value;
    (void)&pp_own_option_with_value;
}
#endif
