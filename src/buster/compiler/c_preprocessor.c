// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// The predefined macros and the feature tables are clang 23.1.3's answers, generated as docs/c-preprocessor.md
// describes (THIRD_PARTY_NOTICES.md).
//
// The C preprocessor: translation phase 4 for one translation unit, from the refined tokens of each file it reads
// (c_lexer.c, then c_refine.c) to one token stream, and `-E`'s text from that stream. docs/c-preprocessor.md has the
// design and its 9700X measurements.
//
// Files. Every file is read (mapped), lexed, refined and given a directive table once (pp_prepare_into), and kept for
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
// counts (pp_count_lines), the keyword pass over the output (pp_resolve_keywords_vector) and -E's printer
// (pp_print_vector). docs/c-preprocessor.md ("Data-parallel design, piece by piece") has each one's 9700X comparison.
//
// main.c includes this file after c_lexer.c, lane.c, c_intern.c and c_refine.c; every function is static. Layout, in
// this order:
//   - API: PPOptions, PPTokens, the flags and internal kinds, PPStatistics, Preprocessor and the entry points.
//   - Diagnostics, token stacks, location ranges and scratch spellings.
//   - Files: PPFileData, PPFile, the file cache's records and paths (FileCache), the directive scan
//     (pp_scan_directives_vector, pp_finish_directives, pp_find_guard), pp_prepare_into, pp_count_lines and pp_line_of,
//     the cache's functions (file_cache_lookup_path, file_cache_prepare and the ones they're made of), and a translation
//     unit's paths (pp_lookup_path) and preparation (pp_prepare_data).
//   - Include search: pp_include_candidate and pp_find_include (pp_dedupe_directories drops missing and repeated
//     directories), and the literal header names (pp_literal_header_name).
//   - Macros: PPMacro, pp_define, pp_same_definition, pp_compile_macro.
//   - The main loop: contexts, the token copies, pp_read_token, substitution (pp_paste, pp_stringize, pp_paste_in_place,
//     pp_substitute),
//     invocations (pp_collect_arguments, pp_invoke, PPInvocation), pp_expand_object, the built-in macros
//     (pp_expand_builtin), pp_expand_current and pp_run_contexts.
//   - Directives: pp_directive and the handlers (pp_condition, pp_include, pp_line, pp_pragma).
//   - #if expressions (pp_evaluate), and the feature tables the __has_* builtins read (pp_feature).
//   - The predefines, pp_initialize, pp_release and pp_run.
//   - Output: pp_resolve_keywords and pp_print, and the -E command (pp_parse_options).

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>
#include <dirent.h>
#include <sys/stat.h>
#include <immintrin.h>
#include "buster/compiler/x86_target.c"

// ---------------------------------------------------------------------------------------------------------------------
// API

// Token flags, in a buffer or the output.
#define BUSTER_PP_SPACE 0x01 // Whitespace or a comment before it.
#define BUSTER_PP_START_OF_LINE 0x02 // It starts a line of the output (for -E).
#define BUSTER_PP_NO_EXPAND 0x04 // It names a macro that was disabled when it was read: it never expands.
#define BUSTER_PP_FROM_MACRO 0x08 // A macro's expansion made it (for __LINE__ and __FILE__).

// Kinds that only buffers hold, and the end of a pragma's tokens in the output (its '#' starts a line).
#define BUSTER_PP_KIND_PLACEMARKER 0xFE // An empty argument next to ##, while substituting.
#define BUSTER_PP_KIND_INTEGER 0xFD // An integer whose value is the token's value (spelled in decimal).
#define BUSTER_PP_KIND_PRAGMA_END 0xFC // After the tokens of a #pragma (or _Pragma, or #ident) in the output.
// In the output, keyword kinds follow the token kinds: id `k` (1 to C_NAME_KEYWORD_COUNT) is BUSTER_PP_KIND_KEYWORD + k - 1.
#define BUSTER_PP_KIND_KEYWORD C_TOKEN_KIND_COUNT
_Static_assert(BUSTER_PP_KIND_KEYWORD + C_NAME_KEYWORD_COUNT <= BUSTER_PP_KIND_PRAGMA_END, "keyword kinds must fit below the internal kinds");

// The ids pp_resolve_keywords looks up (the keywords' are below it): two vpermi2b tables, by an id's low byte.
#define BUSTER_PP_KEYWORD_TABLE 256
_Static_assert(C_NAME_KEYWORD_COUNT < BUSTER_PP_KEYWORD_TABLE, "the keywords must have ids below the keyword table's size");
_Static_assert(BUSTER_PP_KEYWORD_TABLE == 256, "pp_resolve_keywords_vector looks up an id's low byte in four 64-byte quarters");
// Limits: tokens in the output and in each stack, contexts, nested inclusions, a directive's line.
#define BUSTER_PP_MAX_OUTPUT_TOKENS (1u << 28)
#define BUSTER_PP_MAX_STACK_TOKENS (1u << 26)
#define BUSTER_PP_MAX_CONTEXTS (1u << 20)
#define BUSTER_PP_MAX_INCLUDE_DEPTH 200
#define BUSTER_PP_MAX_PARAMETERS 4096
// Token arrays get whole-vector stores past their last token.
#define BUSTER_PP_TOKEN_SLACK 64
// The arrays of records, each reserved in an arena of its own.
#define BUSTER_PP_ARRAYS 32
// Records: location ranges, files, macros, compiled body items and parameters.
#define BUSTER_PP_MAX_RANGES (1u << 22)
#define BUSTER_PP_MAX_FILES (1u << 20)
#define BUSTER_PP_MAX_MACROS (1u << 24)
#define BUSTER_PP_MAX_ITEMS (1u << 26)
#define BUSTER_PP_MAX_LINE_DIRECTIVES (1u << 20)
#define BUSTER_PP_MAX_PUSHED (1u << 16)

typedef uint32_t PPMacroId;

// The phases PPOptions.timing measures (in TSC ticks), for benchmark-preprocessor: reading files, lexing, refining,
// making directive tables; the rest of a run is the main loop with its directives.
// What a run did, counted when PPOptions.timing is set (benchmark-preprocessor prints it).
typedef struct PPStatistics PPStatistics;
struct PPStatistics
{
    uint64_t file_run_tokens; // Tokens files' runs copied to the output,
    uint64_t file_runs; // in this many runs.
    uint64_t buffer_run_tokens; // Tokens buffers' runs copied,
    uint64_t buffer_runs; // in this many runs.
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
    uint64_t skipped_includes; // #include of a file whose guard is defined, or after #pragma once.
};

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

typedef struct PPOptions PPOptions;
struct PPOptions
{
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
    // The target's and the other options' predefined macros (c_predefines.c): -march, the -m feature flags, -O, -f...,
    // -g. All zero is clang's defaults with no option.
    PredefinesOptions predefines;
    // The scalar twins of the vectorized kernels, for test-preprocessor's differential checks.
    bool scalar;
    bool no_standard_includes;
    // Opt-in assembler-with-cpp input: '$' is punctuation, and unknown # lines are assembly comments.
    bool assembly;
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
    // A file of #define lines that replaces the built-in predefines (test-preprocessor gives clang's own).
    const char* predefines_path;
    // Measure the phases (PPPhase) in Preprocessor.ticks.
    bool timing;
    // Keep where the expansions that can start a line start in the output (PPExpansionStart), which -E's line markers
    // need to place them: the printer can't print markers for a run without them.
    bool expansion_starts;
    // Replace trigraphs (translation phase 1): clang's default in the ISO modes before C23, and -trigraphs,
    // -ftrigraphs or -fno-trigraphs.
    bool trigraphs;
    // -dM: -E prints the macros defined at the end (pp_print_macros) instead of the tokens.
    bool dump_macros;
    // --clang-features: the feature queries (__has_builtin, __has_attribute, __has_feature, __has_extension,
    // __has_c_attribute, __has_constexpr_builtin) answer as clang 23 does, from pp_features_*: the oracle's mode
    // (#125). By default they answer what buster implements (pp_implemented_*; #123, #148, #154).
    bool clang_features;
    // Recording is on by default; the benchmark's control disables it without changing preprocessing.
    bool no_history;
    // Null means stderr. Tests use a memory stream for the same renderer as the command line.
    FILE* diagnostics;
};

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
    // A column per arena, so that each one's first pages are base pages (lane.c): a stack that holds a few tokens touches
    // a few kilobytes, where columns side by side would each start in a 2 MB huge page the kernel clears.
    Arena arenas[4];
    // Temporary buffers only. The final output has no origin column, just PPHistoryRun records.
    uint32_t* origins;
    uint32_t origin_capacity;
    Arena origin_arena;
};

typedef struct Preprocessor Preprocessor;

static __attribute__((unused)) bool pp_initialize(Preprocessor* pp, const PPOptions* options, InternTable* table);
static __attribute__((unused)) bool pp_run(Preprocessor* pp, const char* main_path);
static __attribute__((unused)) void pp_release(Preprocessor* pp);

// ---------------------------------------------------------------------------------------------------------------------
// The state

#define BUSTER_PP_NONE UINT32_MAX
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

// A file's contents and everything prepared from them, shared by the paths that name it (by device and inode). The file
// cache's record of a file is one, and so is a translation unit's copy of it, which also has the unit's own state about
// the file: `once`, the line cursor, and `prepared`, which says the unit has copied the record's prepared fields (a
// copy's other fields are the record's).
typedef struct PPFileData PPFileData;
struct PPFileData
{
    uint64_t device;
    uint64_t inode;
    // The file's modification time (for __TIMESTAMP__), and whether it has one (a file the preprocessor made has none).
    int64_t modified;
    bool has_modified;
    const uint8_t* bytes;
    uint32_t length;
    // The bytes are a mapping of the file, which the record's cache unmaps (file_cache_release), not arena memory.
    bool mapped;
    // A translation unit's copy: the file cache's record it copies, or BUSTER_PP_NONE for a file the unit made (the
    // predefines, the command line), which isn't in the cache.
    uint32_t record;
    uint32_t token_count;
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
    // The include guard's macro (0 if the file has none), and whether #pragma once (or #import) read it.
    IdentId guard;
    bool once;
    bool prepared;
    // A place whose line is known, from which pp_line_of counts lines forward: the preprocessor asks about later places
    // as it goes (__LINE__, diagnostics), so each file's lines are counted about once.
    uint32_t line_cursor;
    uint32_t line_cursor_line;
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

// The system calls lookups made, counted for whoever made them: the opens (one per path probed, whether or not a file
// is there), and the files mapped and the files read.
typedef struct FileCacheCounts FileCacheCounts;
struct FileCacheCounts
{
    uint64_t opens;
    uint64_t maps;
    uint64_t reads;
};

// What a probe of a path found (file_cache_probe): whether a regular file can be read there, its identity and
// modification time, and either the record that already had that identity when it looked (BUSTER_PP_NONE if none) or
// its bytes, which the probe mapped (`mapped`) or read into an arena.
typedef struct FileProbe FileProbe;
struct FileProbe
{
    uint64_t device;
    uint64_t inode;
    int64_t modified;
    const uint8_t* bytes;
    uint32_t length;
    uint32_t record;
    bool found;
    bool mapped;
};

// How a probe gets a file's bytes: mapped with MAP_POPULATE (a translation unit's, and one lane's), mapped without it
// (the mapping holds the process's memory map lock for less time, and its pages are faulted in as they are first read),
// or read into an arena. A lane reads small files whatever this says (file_cache_probe's `read_limit`).
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

// The arenas a lane prepares records into: the token columns (kinds, starts, values), each in an arena of its own as a
// translation unit's are, the leading-space words and directive tables, and the bytes of the files it reads.
typedef struct FileCacheArenas FileCacheArenas;
struct FileCacheArenas
{
    Arena columns[3];
    Arena arena;
    Arena bytes;
};

// How files are prepared: the lexer's and refinement's flags (trigraphs), the literal prefixes the language mode splits
// (pp_split_kinds), whether '$' is punctuation (assembly), and whether by the vectorized kernels or by their scalar
// twins, which prepare the same records.
typedef struct PPPrepare PPPrepare;
struct PPPrepare
{
    uint32_t refine_flags;
    uint64_t split_kinds;
    bool assembly;
    bool vectorized;
};

struct FileCache
{
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
    Arena path_arena;
    Arena arrays[4];
    // Each lane's arenas, reserved when the lane first prepares a record.
    FileCacheArenas lanes[BUSTER_LANE_MAX_COUNT];
    // The records prepared, and their tokens (for the tests and the benchmark).
    uint64_t prepared_records;
    uint64_t prepared_tokens;
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
    uint8_t system;
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

// Macro flags.
#define BUSTER_PP_MACRO_FUNCTION 0x01
#define BUSTER_PP_MACRO_VARIADIC 0x02 // Its last parameter is `...` or `name...`.
#define BUSTER_PP_MACRO_COMPILED 0x04 // pp_compile_macro made its body items.
#define BUSTER_PP_MACRO_BUILTIN 0x08
#define BUSTER_PP_MACRO_PASTES 0x10 // Its body has ## (an object-like macro then needs a buffer).

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
    // Bit i: parameter i appears as an operand of neither # nor ##, so it is substituted expanded. Bit 63 stands for
    // every parameter from 63 on.
    uint64_t expanded_parameters;
};

// A body item of a compiled macro.
typedef enum PPItemKind
{
    PP_ITEM_TOKEN, // A body token, copied.
    PP_ITEM_PARAMETER, // A parameter, substituted (expanded, or raw next to ##).
    PP_ITEM_STRINGIZE, // # and a parameter: its raw argument as a string literal.
    PP_ITEM_PASTE, // ## between the items before and after it.
    PP_ITEM_VA_OPT, // __VA_OPT__( ... ): `parameter` is the item count of its content, which follows.
    PP_ITEM_STRINGIZE_VA_OPT, // # __VA_OPT__( ... ), the same way.
} PPItemKind;

typedef struct PPItem PPItem;
struct PPItem
{
    uint8_t kind;
    uint8_t flags; // The body token's BUSTER_PP_SPACE.
    uint16_t parameter;
    uint32_t token; // The body token, in the defining file.
};

// The kinds of context.
#define BUSTER_PP_CONTEXT_FILE 0
#define BUSTER_PP_CONTEXT_BUFFER 1

typedef struct PPContext PPContext;
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
    bool taken;
    bool seen_else;
    uint32_t location;
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
    uint32_t next; // The next argument to expand.
    uint32_t captures_begin; // pp->captures' count before its first expanded argument.
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

typedef struct PPCapture PPCapture;
struct PPCapture
{
    uint8_t kind;
    uint32_t history;
    // The capture's tokens start here in pp->captures.
    uint32_t begin;
    // An argument's: its invocation.
    uint32_t invocation;
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
};

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

struct Preprocessor
{
    PPOptions options;
    InternTable* table;
    bool vectorized;
    uint32_t refine_flags;
    // The prefixed literals the language mode doesn't have, by kind (pp_split_kinds).
    uint64_t split_kinds;
    // Memory for files' leading-space words and directive tables, macro bodies' spellings and paths. Files' token
    // columns (kinds, starts and values) have an arena each, `columns`, and so do the output and the stacks (PPTokens)
    // and each array of records (`arrays`, whose first pages are then base pages).
    Arena arena;
    Arena columns[3];
    Arena arrays[BUSTER_PP_ARRAYS];
    uint32_t array_count;
    // The output, and the three stacks of buffers.
    PPTokens output;
    PPTokens arguments;
    PPTokens captures;
    PPTokens expansions;
    // Files: the cache they're read through (PPOptions.files, or one of the unit's own, in `arena`), this unit's data of
    // each (its copies of the cache's records, and the files it makes) and of each record (the data's index + 1, or 0),
    // and its paths by name, with their table.
    FileCache* cache;
    bool owns_cache;
    PPFileData* datas;
    uint32_t data_count;
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
    uint32_t directory_count;
    uint32_t angled_first; // The first directory <...> searches.
    // The location space.
    PPRange* ranges;
    uint32_t range_count;
    uint32_t location_top;
    PPLineDirective* line_directives;
    uint32_t line_directive_count;
    // The starts of the expansions read from files into the output, by token (PPExpansionStart), and the location of the
    // name of the latest one.
    PPExpansionStart* expansion_starts;
    uint32_t expansion_start_count;
    uint32_t expansion_location;
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
    // Only set around synchronous expansion/substitution work; never inferred from a spelling location.
    uint32_t history_current;
    uint32_t history_anchor;
    uint32_t history_spelling;
    uint32_t diagnostic_origin;
    uint32_t diagnostic_include_range;
    uint32_t origin_peak[3];
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
    // #pragma push_macro: a name and the macro it had, in order.
    IdentId* pushed_names;
    PPMacroId* pushed_macros;
    uint32_t pushed_count;
    // #pragma GCC poison.
    uint64_t* poisoned;
    // The stacks of the main loop.
    PPContext* contexts;
    uint32_t context_count;
    PPConditional* conditionals;
    uint32_t conditional_count;
    PPInvocation* invocations;
    uint32_t invocation_count;
    PPArgument* argument_records;
    uint32_t argument_record_count;
    PPCapture* capture_frames;
    uint32_t capture_count;
    // Built-in state: __COUNTER__, the inclusion depth, the main file, and the spellings "0", "1" and "#".
    uint32_t counter;
    uint32_t zero_location;
    uint32_t one_location;
    uint32_t hash_location;
    uint32_t include_depth;
    uint32_t main_file;
    char date[16];
    char time[16];
    char timestamp[32]; // __TIMESTAMP__'s with SOURCE_DATE_EPOCH (`has_epoch`); else each file's modification time.
    bool has_epoch;
    // The builtin __FLT_EVAL_METHOD__'s value (predefines_text).
    uint32_t flt_eval_method;
    // The phases' TSC ticks, the tokens the prepared files have, and what the run did, when PPOptions.timing is set.
    uint64_t ticks[PP_PHASE_COUNT];
    uint64_t input_tokens;
    PPStatistics statistics;
    // Diagnostics.
    uint32_t errors;
    uint32_t warnings;
    bool fatal;
    // The output kind of each identifier id below BUSTER_PP_KEYWORD_TABLE: its keyword's in this language mode, or an
    // identifier's.
    uint8_t keyword_kinds[BUSTER_PP_KEYWORD_TABLE] __attribute__((aligned(64)));
};

// ---------------------------------------------------------------------------------------------------------------------
// Token stacks

static bool pp_tokens_reserve(PPTokens* tokens, uint32_t capacity)
{
    const uint64_t slots = (uint64_t)capacity + BUSTER_PP_TOKEN_SLACK;
    tokens->arenas[0] = arena_reserve(slots);
    tokens->arenas[1] = arena_reserve(slots);
    tokens->arenas[2] = arena_reserve(slots * sizeof(uint32_t));
    tokens->arenas[3] = arena_reserve(slots * sizeof(uint32_t));
    tokens->kinds = arena_push(&tokens->arenas[0], slots, 64);
    tokens->flags = arena_push(&tokens->arenas[1], slots, 64);
    tokens->values = arena_push_array(&tokens->arenas[2], slots, sizeof(uint32_t), 64);
    tokens->locations = arena_push_array(&tokens->arenas[3], slots, sizeof(uint32_t), 64);
    tokens->count = 0;
    tokens->capacity = capacity;
    return tokens->kinds != 0 && tokens->flags != 0 && tokens->values != 0 && tokens->locations != 0;
}

static void pp_tokens_release(PPTokens* tokens)
{
    arena_release(&tokens->origin_arena);
    for (uint32_t i = 0; i < 4; i += 1)
    {
        arena_release(&tokens->arenas[i]);
    }
    *tokens = (PPTokens){ 0 };
}

BUSTER_INLINE void pp_tokens_set(PPTokens* tokens, uint32_t index, uint8_t kind, uint8_t flags, uint32_t value, uint32_t location)
{
    tokens->kinds[index] = kind;
    tokens->flags[index] = flags;
    tokens->values[index] = value;
    tokens->locations[index] = location;
}

// ---------------------------------------------------------------------------------------------------------------------
// Diagnostics

typedef enum PPSeverity
{
    PP_SEVERITY_WARNING,
    PP_SEVERITY_ERROR,
    PP_SEVERITY_FATAL,
} PPSeverity;

static uint32_t pp_find_range(const Preprocessor* pp, uint32_t location);
static uint32_t pp_line_of(Preprocessor* pp, uint32_t range, uint32_t location, uint32_t* column);
static uint32_t pp_presumed(Preprocessor* pp, uint32_t location, const char** name, uint32_t* name_length);
static void pp_diagnostic_lines(Preprocessor* pp, uint32_t range);
static void pp_render_diagnostic(Preprocessor* pp, PPSeverity severity, uint32_t location, uint32_t origin, const char* format, va_list arguments);

// Prints "file:line:column: error: message" to stderr for `location` (0 for none): the presumed file and line, as #line
// gives them, and the column.
static __attribute__((format(printf, 4, 5))) void pp_diagnose(Preprocessor* pp, PPSeverity severity, uint32_t location, const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    pp_render_diagnostic(pp, severity, location, location != 0 ? pp->diagnostic_origin : 0, format, arguments);
    va_end(arguments);
    pp->warnings += severity == PP_SEVERITY_WARNING;
    pp->errors += severity != PP_SEVERITY_WARNING;
    pp->fatal |= severity == PP_SEVERITY_FATAL;
}

static void* pp_array(Preprocessor* pp, uint64_t count, uint64_t size);

#include "c_preprocessor_history.c"

// Room for `more` tokens in a stack, or a fatal error.
static bool pp_room(Preprocessor* pp, PPTokens* tokens, uint32_t more)
{
    const bool result = (uint64_t)tokens->count + more <= tokens->capacity;
    if (!result && !pp->fatal)
    {
        pp_diagnose(pp, PP_SEVERITY_FATAL, 0, "too many tokens (macro expansion past %u tokens)", tokens->capacity);
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The location space

// A new range of `length` locations (at least 1) after the last one. Returns its index, or BUSTER_PP_NONE (and a fatal
// error) if the location space is full.
static uint32_t pp_add_range(Preprocessor* pp, uint32_t length, uint32_t file, const uint8_t* bytes, uint32_t parent, uint32_t include_location)
{
    uint32_t result = BUSTER_PP_NONE;
    const uint64_t size = (uint64_t)length + 1;
    if ((uint64_t)pp->location_top + size >= UINT32_MAX || pp->range_count == BUSTER_PP_MAX_RANGES)
    {
        pp_diagnose(pp, PP_SEVERITY_FATAL, 0, "the translation unit's sources pass 4 GB");
    }
    else
    {
        result = pp->range_count;
        pp->ranges[result] = (PPRange){
            .base = pp->location_top,
            .length = (uint32_t)size,
            .file = file,
            .bytes = bytes,
            .parent = parent,
            .include_location = include_location,
            .first_line_directive = BUSTER_PP_NONE,
            .last_line_directive = BUSTER_PP_NONE,
        };
        pp->range_count += 1;
        pp->location_top += (uint32_t)size;
    }
    return result;
}

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

// Copies `length` bytes into the scratch space, and returns their location (or 0 and a fatal error).
static uint32_t pp_scratch_in(Preprocessor* pp, const uint8_t* bytes, uint32_t length, bool clean)
{
    uint32_t result = 0;
    uint32_t* current = &pp->scratch_range[clean];
    uint32_t* used = &pp->scratch_used[clean];
    if (*current == BUSTER_PP_NONE || *used + length > pp->ranges[*current].length - 1)
    {
        const uint32_t chunk = length > BUSTER_PP_SCRATCH_CHUNK_BYTES ? length : BUSTER_PP_SCRATCH_CHUNK_BYTES;
        uint8_t* memory = arena_push(&pp->scratch[clean], chunk, 1);
        *current = memory != 0 ? pp_add_range(pp, chunk, BUSTER_PP_NONE, memory, BUSTER_PP_NONE, 0) : BUSTER_PP_NONE;
        *used = 0;
        if (memory == 0)
        {
            pp_diagnose(pp, PP_SEVERITY_FATAL, 0, "out of scratch memory for generated spellings");
        }
    }
    if (*current != BUSTER_PP_NONE)
    {
        const PPRange* range = &pp->ranges[*current];
        memcpy((uint8_t*)range->bytes + *used, bytes, length);
        result = range->base + *used;
        *used += length;
    }
    return result;
}

// Scratch spellings that are read as source again (pastes and _Pragma's text, which clang lexes again): their line
// splices and trigraphs come out when they are read.
static uint32_t pp_scratch(Preprocessor* pp, const uint8_t* bytes, uint32_t length)
{
    return pp_scratch_in(pp, bytes, length, false);
}

// Scratch spellings that are final (stringized strings, the builtins' strings and numbers), which clang makes as clean
// tokens: they are read as they are, so a "??=" a stringized argument spells stays one, even with trigraphs on.
static uint32_t pp_scratch_clean(Preprocessor* pp, const uint8_t* bytes, uint32_t length)
{
    return pp_scratch_in(pp, bytes, length, true);
}

// The bytes of [location, end) in the location space (in one range).
static const uint8_t* pp_location_bytes(const Preprocessor* pp, uint32_t location)
{
    const uint32_t range = pp_find_range(pp, location);
    return range != BUSTER_PP_NONE ? pp->ranges[range].bytes + (location - pp->ranges[range].base) : 0;
}

// `length` bytes of source without the line splices in them (and trigraphs, with C_TOKENIZER_TRIGRAPHS in
// `refine_flags`): the bytes themselves if they have none, else a copy in `buffer`, which needs room for `length` bytes.
// Returns the length.
static uint32_t pp_remove_splices(const uint8_t* bytes, uint32_t length, uint32_t refine_flags, uint8_t* buffer, const uint8_t** spelling)
{
    const bool trigraphs = (refine_flags & C_TOKENIZER_TRIGRAPHS) != 0;
    bool cleaned = false;
    for (uint32_t i = 0; !cleaned && i < length; i += 1)
    {
        cleaned = bytes[i] == '\\' || (bytes[i] == '?' && trigraphs);
    }
    uint32_t result = length;
    *spelling = bytes;
    if (cleaned)
    {
        // Through the scalar lexer's characters, which skip the splices.
        const ScalarLexer lexer = {
            .source = bytes,
            .length = length,
            .trigraphs = trigraphs,
            .cplusplus = false,
        };
        uint64_t p = 0;
        result = 0;
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
        }
        *spelling = buffer;
    }
    return result;
}

// `length` bytes without the line splices in them (pp_remove_splices), unless they are a clean scratch spelling (in its
// arena: pp_scratch_clean), which is final and is the bytes themselves.
static uint32_t pp_without_splices(const Preprocessor* pp, const uint8_t* bytes, uint32_t length, uint8_t* buffer, const uint8_t** spelling)
{
    const Arena* clean = &pp->scratch[1];
    const bool final = bytes >= clean->base && bytes < clean->base + clean->capacity;
    uint32_t result = length;
    *spelling = bytes;
    if (!final)
    {
        result = pp_remove_splices(bytes, length, pp->refine_flags, buffer, spelling);
    }
    return result;
}

// A token's spelling: an identifier's (or a keyword's) interned bytes, an integer's digits, or else its bytes in the
// location space without the line splices in them (or trigraphs, with trigraphs on). `buffer` holds a spelling that
// needs writing; it needs room for the token's bytes, or 24 for an integer. Returns the length.
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
    else
    {
        result = pp_without_splices(pp, pp_location_bytes(pp, location), value - location, buffer, spelling);
    }
    return result;
}

// Recover a refined identifier's end, respecting the assembly adapter's '$' boundary.
static uint32_t pp_identifier_end(const Preprocessor* pp, const uint8_t* bytes, uint32_t length, uint32_t start)
{
    uint32_t result = refine_identifier_end(bytes, length, start, pp->refine_flags);
    if (pp->options.assembly)
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
        result = pp_without_splices(pp, bytes_range->bytes + start, end - start, buffer, spelling);
    }
    return result;
}

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
static uint32_t pp_scan_directives_vector(const uint8_t* kinds, uint32_t count, uint32_t* hashes, uint64_t* lines)
{
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
// continuation that has no open group (an #endif or #else too many), which the stack then pairs.
static bool pp_pair_conditionals_vector(PPFileData* data, uint32_t* scratch_indices)
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
    // Each opener's, #elif's and #else's answer, as directives.
    for (uint32_t i = 0; result && i < n; i += 1)
    {
        if (kinds[i] != PP_DIRECTIVE_ENDIF)
        {
            data->directive_next[conditionals[i]] = answers[i] != BUSTER_PP_NONE ? conditionals[answers[i]] : BUSTER_PP_NONE;
        }
    }
    scratch_end(scratch);
    return result;
}

// Each directive's kind and line end, then the conditional groups' pairing (pp_pair_conditionals_vector, or
// pp_pair_conditionals_scalar for the scalar kernels and for files the vectorized pairing leaves to it).
static bool pp_finish_directives(bool vectorized, PPFileData* data, const uint64_t* lines)
{
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
    }
    Scratch scratch = scratch_begin(0);
    uint32_t* indices = arena_push_array(scratch.arena, (uint64_t)data->directive_count + 32, sizeof(uint32_t), 64);
    const bool result = indices != 0;
    if (result && !(vectorized && pp_pair_conditionals_vector(data, indices)))
    {
        pp_pair_conditionals_scalar(data, indices);
    }
    scratch_end(scratch);
    return result;
}

// The include guard of a prepared file, or 0: its first token starts `#ifndef X` (or `#if !defined X`, or
// `#if !defined(X)`), whose group has no other branch and whose #endif ends the file.
static IdentId pp_find_guard(const PPFileData* data)
{
    IdentId result = 0;
    if (data->directive_count >= 2 && data->directive_hash[0] == 0 && data->directive_next[0] != BUSTER_PP_NONE)
    {
        const uint32_t endif = data->directive_next[0];
        const bool closes = data->directive_kind[endif] == PP_DIRECTIVE_ENDIF && data->directive_end[endif] == data->token_count;
        const uint32_t end = data->directive_end[0];
        const uint8_t* kinds = data->kinds;
        if (closes && data->directive_kind[0] == PP_DIRECTIVE_IFNDEF && end == 3 && (kinds[2] & 0x7F) == C_TOKEN_RAW_IDENTIFIER)
        {
            result = data->values[2];
        }
        else if (closes && data->directive_kind[0] == PP_DIRECTIVE_IF && end >= 5 && (kinds[2] & 0x7F) == C_TOKEN_EXCLAIM &&
                 (kinds[3] & 0x7F) == C_TOKEN_RAW_IDENTIFIER && data->values[3] == C_NAME_DEFINED)
        {
            if (end == 5 && (kinds[4] & 0x7F) == C_TOKEN_RAW_IDENTIFIER)
            {
                result = data->values[4];
            }
            else if (end == 7 && (kinds[4] & 0x7F) == C_TOKEN_L_PAREN && (kinds[5] & 0x7F) == C_TOKEN_RAW_IDENTIFIER && (kinds[6] & 0x7F) == C_TOKEN_R_PAREN)
            {
                result = data->values[5];
            }
        }
    }
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

// How many of `count` tokens a mode splits (`split`, pp_split_kinds), 64 kinds at a time: the mask spread to a byte per
// kind is a vpermi2b table, which reads only a kind's low 7 bits, so the start-of-line bit doesn't matter.
static uint32_t pp_count_splits_vector(const uint8_t* kinds, uint32_t count, uint64_t split)
{
    const __m512i ones = _mm512_set1_epi8(1);
    const __m512i low = _mm512_maskz_mov_epi8((__mmask64)split, ones);
    const __m512i high = _mm512_setzero_si512();
    uint32_t result = 0;
    for (uint32_t i = 0; i < count; i += 64)
    {
        const __mmask64 valid = _bzhi_u64(~0ull, count - i < 64 ? count - i : 64);
        const __m512i kind = _mm512_maskz_loadu_epi8(valid, kinds + i);
        result += (uint32_t)__builtin_popcountll(_mm512_mask_test_epi8_mask(valid, _mm512_permutex2var_epi8(low, kind, high), ones));
    }
    return result;
}

// pp_count_splits_vector's scalar twin.
static uint32_t pp_count_splits_scalar(const uint8_t* kinds, uint32_t count, uint64_t split)
{
    uint32_t result = 0;
    for (uint32_t i = 0; i < count; i += 1)
    {
        result += pp_splits_kind(split, kinds[i]);
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
        if (pp_splits_kind(split, kind))
        {
            uint32_t quote = start;
            uint32_t prefix_end = start;
            while (bytes[quote] != '\'' && bytes[quote] != '"')
            {
                prefix_end = bytes[quote] == 'u' || bytes[quote] == 'U' || bytes[quote] == '8' ? quote + 1 : prefix_end;
                quote += 1;
            }
            kinds[t + remaining] = (kind & 0x7F) <= C_TOKEN_UTF32_CHAR_CONSTANT ? C_TOKEN_CHAR_CONSTANT : C_TOKEN_STRING_LITERAL;
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

// C's GNU identifier grammar includes '$'. Assembly instead needs $MACRO to expand MACRO. Only tokens containing
// '$' take this adapter; literals stay opaque. Re-lex the intervening slices with the shared lexer, including line
// splices and numeric local references. Scratch columns leave the original columns intact on allocation failure.
// A slice can lex into several tokens (the pp-number 1$e+5 into 1, $, e, + and 5), at most one per byte: the bytes of
// the tokens that contain '$' bound the columns, not their dollars.
static int64_t pp_split_dollars(uint32_t refine_flags, const uint8_t* bytes, uint8_t* kinds, uint32_t* starts, uint32_t* ends, uint32_t count)
{
    // Most sources have no '$' at all, which one scan of their extent finds.
    const bool dollars = count != 0 && memchr(bytes + starts[0], '$', ends[count - 1] - starts[0]) != 0;
    uint64_t split_bytes = 0;
    for (uint32_t i = 0; dollars && i < count; i += 1)
    {
        const uint8_t kind = kinds[i] & 0x7F;
        if ((kind == C_TOKEN_RAW_IDENTIFIER || kind == C_TOKEN_NUMERIC_CONSTANT) && memchr(bytes + starts[i], '$', ends[i] - starts[i]) != 0)
        {
            split_bytes += ends[i] - starts[i];
        }
    }
    int64_t result = count;
    if (split_bytes != 0)
    {
        Scratch scratch = scratch_begin(0);
        const uint64_t capacity = (uint64_t)count + split_bytes + BUSTER_C_TOKENIZER_OUTPUT_SLACK;
        uint8_t* split_kinds = arena_push(scratch.arena, capacity, 64);
        uint32_t* split_starts = arena_push_array(scratch.arena, capacity, sizeof(uint32_t), 64);
        uint32_t* split_ends = arena_push_array(scratch.arena, capacity, sizeof(uint32_t), 64);
        result = split_kinds != 0 && split_starts != 0 && split_ends != 0 ? 0 : -1;
        for (uint32_t i = 0; result >= 0 && i < count; i += 1)
        {
            const uint8_t kind = kinds[i] & 0x7F;
            const bool word = kind == C_TOKEN_RAW_IDENTIFIER || kind == C_TOKEN_NUMERIC_CONSTANT;
            if (word && memchr(bytes + starts[i], '$', ends[i] - starts[i]) != 0)
            {
                const uint32_t first = (uint32_t)result;
                for (uint32_t at = starts[i]; result >= 0 && at < ends[i];)
                {
                    if (bytes[at] == '$')
                    {
                        split_kinds[result] = C_TOKEN_UNKNOWN;
                        split_starts[result] = at;
                        split_ends[result] = at + 1;
                        result += 1;
                        at += 1;
                    }
                    else
                    {
                        const uint32_t begin = at;
                        while (at < ends[i] && bytes[at] != '$')
                        {
                            at += 1;
                        }
                        const int64_t added = c_tokenize_scalar(bytes + begin, at - begin, split_kinds + result, split_starts + result, split_ends + result, refine_flags);
                        for (int64_t n = 0; n < added; n += 1)
                        {
                            split_kinds[result + n] &= 0x7F;
                            split_starts[result + n] += begin;
                            split_ends[result + n] += begin;
                        }
                        result = added >= 0 ? result + added : -1;
                    }
                }
                if (result > first)
                {
                    split_kinds[first] |= kinds[i] & BUSTER_C_TOKEN_START_OF_LINE;
                }
            }
            else
            {
                split_kinds[result] = kinds[i];
                split_starts[result] = starts[i];
                split_ends[result] = ends[i];
                result += 1;
            }
        }
        if (result >= 0)
        {
            memcpy(kinds, split_kinds, result);
            memcpy(starts, split_starts, (uint64_t)result * sizeof(uint32_t));
            memcpy(ends, split_ends, (uint64_t)result * sizeof(uint32_t));
        }
        scratch_end(scratch);
    }
    return result;
}

// Splits, among `count` freshly lexed tokens, the prefixed literals that `mode`'s language doesn't have (a scan of the
// kinds, and the rare split: pp_split_prefixes), then in assembly '$' punctuation (pp_split_dollars). Returns the new
// count, or -1 on allocation failure.
static int64_t pp_lex_for_mode(const PPPrepare* mode, const uint8_t* bytes, uint8_t* kinds, uint32_t* starts, uint32_t* ends, uint32_t count, bool vectorized)
{
    int64_t result = count;
    if (mode->split_kinds != 0)
    {
        const uint32_t splits = vectorized ? pp_count_splits_vector(kinds, count, mode->split_kinds) : pp_count_splits_scalar(kinds, count, mode->split_kinds);
        result = splits != 0 ? pp_split_prefixes(bytes, kinds, starts, ends, count, splits, mode->split_kinds) : count;
    }
    if (mode->assembly)
    {
        result = pp_split_dollars(mode->refine_flags, bytes, kinds, starts, ends, (uint32_t)result);
    }
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

// Lexes, refines and indexes a file's bytes, `data`: its tokens, values (its identifiers interned in `table`),
// leading-space bits, directive table and guard, in `columns` (kinds, starts and values, an arena each) and `arena` (the
// directive table). Adds each phase's TSC ticks to `ticks` (by PPPhase) unless it is null.
static PPPrepareStatus pp_prepare_into(const PPPrepare* prepare, InternTable* table, Arena* columns, Arena* arena, PPFileData* data, uint64_t* ticks)
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
        count = prepare->assembly && !prepare->vectorized ? c_tokenize_scalar(data->bytes, data->length, data->kinds, data->starts, data->values, prepare->refine_flags)
                                                           : c_tokenize(data->bytes, data->length, data->kinds, data->starts, data->values, prepare->refine_flags);
    }
    const bool lexed = count >= 0;
    count = count > 0 ? pp_lex_for_mode(prepare, data->bytes, data->kinds, data->starts, data->values, (uint32_t)count, prepare->vectorized) : count;
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
        bool refined = data->space != 0;
        if (refined && prepare->vectorized)
        {
            refined = refine_tokens(table, data->bytes, data->length, data->kinds, data->starts, data->values, data->token_count, data->values, data->space,
                                    prepare->refine_flags);
        }
        else if (refined)
        {
            refined = refine_tokens_scalar(table, data->bytes, data->length, data->kinds, data->starts, data->values, data->token_count, data->values,
                                           data->space, prepare->refine_flags);
        }
        if (ticks != 0)
        {
            const uint64_t now = __rdtsc();
            ticks[PP_PHASE_REFINE] += now - tick;
            tick = now;
        }
        Scratch scratch = scratch_begin(0);
        uint64_t* lines = refined ? arena_push_array(scratch.arena, data->token_count / 64 + 1, sizeof(uint64_t), 64) : 0;
        bool indexed = lines != 0;
        if (indexed)
        {
            const uint32_t directive_count = prepare->vectorized ? pp_scan_directives_vector(data->kinds, data->token_count, 0, lines)
                                                                 : pp_scan_directives_scalar(data->kinds, data->token_count, 0, lines);
            data->directive_count = directive_count;
            data->directive_hash = arena_push_array(arena, directive_count + 1, sizeof(uint32_t), 4);
            data->directive_end = arena_push_array(arena, directive_count + 1, sizeof(uint32_t), 4);
            data->directive_kind = arena_push(arena, directive_count + 1, 1);
            data->directive_next = arena_push_array(arena, directive_count + 1, sizeof(uint32_t), 4);
            indexed = data->directive_hash != 0 && data->directive_end != 0 && data->directive_kind != 0 && data->directive_next != 0;
        }
        if (indexed)
        {
            if (prepare->vectorized)
            {
                pp_scan_directives_vector(data->kinds, data->token_count, data->directive_hash, lines);
            }
            else
            {
                pp_scan_directives_scalar(data->kinds, data->token_count, data->directive_hash, lines);
            }
            indexed = pp_finish_directives(prepare->vectorized, data, lines);
        }
        scratch_end(scratch);
        if (indexed)
        {
            data->guard = pp_find_guard(data);
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

// The line ends in bytes [begin, end) of a file: "\n", "\r\n" (at its '\n') and a '\r' that no '\n' follows, 64 bytes at
// a time with popcounts.
static uint32_t pp_count_lines(const PPFileData* data, uint32_t begin, uint32_t end)
{
    uint32_t result = 0;
    for (uint32_t i = begin; i < end; i += 64)
    {
        const uint32_t remaining = end - i;
        const __mmask64 valid = remaining >= 64 ? ~0ull : _bzhi_u64(~0ull, remaining);
        const __m512i bytes = _mm512_maskz_loadu_epi8(valid, data->bytes + i);
        const uint64_t newlines = _mm512_cmpeq_epi8_mask(bytes, _mm512_set1_epi8('\n'));
        const uint64_t returns = _mm512_cmpeq_epi8_mask(bytes, _mm512_set1_epi8('\r'));
        // The byte after the block's last one, if the counted bytes go on past the block.
        const uint64_t next_newline = remaining > 64 && data->bytes[i + 64] == '\n';
        const uint64_t followed = (newlines >> 1) | (next_newline << 63);
        result += (uint32_t)__builtin_popcountll((newlines | (returns & ~followed)) & valid);
    }
    return result;
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
            for (uint32_t i = 0; i < data->length; i += pp->vectorized ? 64 : 1)
            {
                if (pp->vectorized)
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

// The physical line and column of `location` in its range's file: lines counted from the file's cursor (or from its
// start, for a place before the cursor), and the column from the line's start before it.
static uint32_t pp_line_of(Preprocessor* pp, uint32_t range, uint32_t location, uint32_t* column)
{
    uint32_t result = 1;
    *column = 1;
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
            *column = offset - data->diagnostic_lines[low] + 1;
        }
        else
        {
            if (data->line_cursor_line == 0 || offset < data->line_cursor)
            {
                data->line_cursor = 0;
                data->line_cursor_line = 1;
            }
            result = data->line_cursor_line + pp_count_lines(data, data->line_cursor, offset);
            data->line_cursor = offset;
            data->line_cursor_line = result;
            uint32_t line_start = offset;
            while (line_start > 0 && data->bytes[line_start - 1] != '\n' && data->bytes[line_start - 1] != '\r')
            {
                line_start -= 1;
            }
            *column = offset - line_start + 1;
        }
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

// Reserves a cache's memory, for files prepared as `prepare` says and refined with `table`. Returns false if it can't be
// reserved (file_cache_release then releases what was).
static __attribute__((unused)) bool file_cache_initialize(FileCache* cache, InternTable* table, PPPrepare prepare)
{
    *cache = (FileCache){ 0 };
    cache->prepare = prepare;
    cache->table = table;
    cache->arrays[0] = arena_reserve((uint64_t)BUSTER_PP_MAX_FILES * sizeof(PPFileData) + 64);
    cache->arrays[1] = arena_reserve(2ull * BUSTER_PP_MAX_FILES * sizeof(uint32_t) + 64);
    cache->arrays[2] = arena_reserve((uint64_t)BUSTER_FILE_CACHE_MAX_PATHS * sizeof(FileCachePath) + 64);
    cache->arrays[3] = arena_reserve(2ull * BUSTER_FILE_CACHE_MAX_PATHS * sizeof(uint32_t) + 64);
    cache->path_arena = arena_reserve(BUSTER_FILE_CACHE_PATH_BYTES);
    cache->records = arena_push_array(&cache->arrays[0], BUSTER_PP_MAX_FILES, sizeof(PPFileData), 64);
    cache->identity_slots = arena_push_array(&cache->arrays[1], 2ull * BUSTER_PP_MAX_FILES, sizeof(uint32_t), 64);
    cache->paths = arena_push_array(&cache->arrays[2], BUSTER_FILE_CACHE_MAX_PATHS, sizeof(FileCachePath), 64);
    cache->path_slots = arena_push_array(&cache->arrays[3], 2ull * BUSTER_FILE_CACHE_MAX_PATHS, sizeof(uint32_t), 64);
    cache->identity_slot_mask = BUSTER_FILE_CACHE_INITIAL_SLOTS - 1;
    cache->path_slot_mask = BUSTER_FILE_CACHE_INITIAL_SLOTS - 1;
    return cache->records != 0 && cache->identity_slots != 0 && cache->paths != 0 && cache->path_slots != 0 && cache->path_arena.base != 0;
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
    for (uint32_t lane = 0; lane < BUSTER_LANE_MAX_COUNT; lane += 1)
    {
        FileCacheArenas* arenas = &cache->lanes[lane];
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(arenas->columns); i += 1)
        {
            arena_release(&arenas->columns[i]);
        }
        arena_release(&arenas->arena);
        arena_release(&arenas->bytes);
    }
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(cache->arrays); i += 1)
    {
        arena_release(&cache->arrays[i]);
    }
    arena_release(&cache->path_arena);
    *cache = (FileCache){ 0 };
}

// The arenas lane `lane` prepares records into, reserved the first time. Reserving takes the process's memory map lock,
// which the lanes of a group are better off taking one after another: c_files.c reserves them before its lanes start.
static FileCacheArenas* file_cache_arenas(FileCache* cache, uint32_t lane)
{
    FileCacheArenas* result = &cache->lanes[lane];
    if (result->arena.base == 0)
    {
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(result->columns); i += 1)
        {
            result->columns[i] = arena_reserve(BUSTER_FILE_CACHE_ARENA_BYTES);
        }
        result->arena = arena_reserve(BUSTER_FILE_CACHE_ARENA_BYTES);
        result->bytes = arena_reserve(BUSTER_FILE_CACHE_ARENA_BYTES);
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
    if (cache->record_count < BUSTER_PP_MAX_FILES)
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
// or read whole, is none. The probe reads the identity table only, so lanes probe at once while no lane adds records;
// the system calls go to `counts`.
static void file_cache_probe(const FileCache* cache, const char* path, FileCacheBytes how, uint32_t read_limit, Arena* bytes_arena, FileProbe* probe,
                             FileCacheCounts* counts)
{
    static const uint8_t empty[1] = { 0 };
    *probe = (FileProbe){ .record = BUSTER_PP_NONE };
    // Without blocking: opening a FIFO waits for a writer otherwise, also for the walk's speculative probes of includes
    // no unit reaches. A FIFO, a device or a directory then isn't a regular file, and is none. O_NONBLOCK changes
    // nothing for a regular file, and O_NOCTTY keeps a terminal from becoming the process's controlling one.
    const int fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK | O_NOCTTY);
    counts->opens += 1;
    struct stat stats;
    if (fd >= 0 && fstat(fd, &stats) == 0 && S_ISREG(stats.st_mode) && (uint64_t)stats.st_size <= UINT32_MAX - BUSTER_C_TOKENIZER_OUTPUT_SLACK)
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
    char* copy = cache->path_count < BUSTER_FILE_CACHE_MAX_PATHS ? arena_push(&cache->path_arena, (uint64_t)length + 1, 1) : 0;
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

// The record of the file at `path` (`length` bytes, NUL-terminated), probing the file system only the first time the
// cache is asked about the path; BUSTER_PP_NONE if no file can be read there, or if the cache is full. One lane at a
// time.
static uint32_t file_cache_lookup_path(FileCache* cache, const char* path, uint32_t length, FileCacheCounts* counts)
{
    const uint64_t hash = intern_hash_bytes((const uint8_t*)path, length);
    FileCachePath* entry = file_cache_find_path(cache, path, length, hash);
    if (entry == 0)
    {
        FileProbe probe;
        file_cache_probe(cache, path, FILE_CACHE_MAP_POPULATED, 0, 0, &probe, counts);
        entry = file_cache_add_path(cache, path, length, hash, &probe);
        if (entry != 0)
        {
            file_cache_identify(cache, entry);
        }
        else if (probe.mapped)
        {
            munmap((void*)probe.bytes, probe.length);
        }
    }
    return entry != 0 ? entry->record : BUSTER_PP_NONE;
}

// Prepares record `record` (pp_prepare_into) unless it is prepared, into lane `lane`'s arenas and with the cache's intern
// table, which only one lane at a time may write. Adds each phase's TSC ticks to `ticks` unless it is null.
static PPPrepareStatus file_cache_prepare(FileCache* cache, uint32_t record, uint32_t lane, uint64_t* ticks)
{
    PPFileData* data = &cache->records[record];
    PPPrepareStatus result = PP_PREPARE_DONE;
    if (!data->prepared)
    {
        FileCacheArenas* arenas = file_cache_arenas(cache, lane);
        result = pp_prepare_into(&cache->prepare, cache->table, arenas->columns, &arenas->arena, data, ticks);
        cache->prepared_records += result == PP_PREPARE_DONE;
        cache->prepared_tokens += result == PP_PREPARE_DONE ? data->token_count : 0;
    }
    return result;
}

// How a unit with these options prepares files: trigraphs or not, the literal prefixes its language mode splits, and its
// kernels. A file cache that units share is made with it (file_cache_initialize).
static PPPrepare pp_prepare_options(const PPOptions* options)
{
    return (PPPrepare){
        .refine_flags = options->trigraphs ? C_TOKENIZER_TRIGRAPHS : 0,
        .split_kinds = pp_split_kinds(options->language),
        .assembly = options->assembly,
        .vectorized = !options->scalar,
    };
}

// How this unit prepares files (pp_prepare_options of its options).
static PPPrepare pp_prepare_config(const Preprocessor* pp)
{
    return (PPPrepare){
        .refine_flags = pp->refine_flags,
        .split_kinds = pp->split_kinds,
        .assembly = pp->options.assembly,
        .vectorized = pp->vectorized,
    };
}

// This unit's data of cache record `record`: made the first time the unit names the record's file, with the record's
// bytes but nothing prepared yet (pp_prepare_data copies that when the unit first enters the file). Returns
// BUSTER_PP_NONE if the unit has as many files as it can hold.
static uint32_t pp_data_of_record(Preprocessor* pp, uint32_t record)
{
    uint32_t result = pp->record_data[record] - 1;
    if (pp->record_data[record] == 0 && pp->data_count < BUSTER_PP_MAX_FILES)
    {
        const PPFileData* shared = &pp->cache->records[record];
        result = pp->data_count;
        pp->datas[result] = (PPFileData){
            .device = shared->device,
            .inode = shared->inode,
            .modified = shared->modified,
            .has_modified = shared->has_modified,
            .bytes = shared->bytes,
            .length = shared->length,
            .record = record,
        };
        pp->data_count += 1;
        pp->record_data[record] = result + 1;
    }
    return result;
}

// A unit's data of a cache record takes what the record has prepared.
static void pp_copy_prepared(PPFileData* data, const PPFileData* record)
{
    data->token_count = record->token_count;
    data->kinds = record->kinds;
    data->starts = record->starts;
    data->values = record->values;
    data->space = record->space;
    data->directive_count = record->directive_count;
    data->directive_hash = record->directive_hash;
    data->directive_end = record->directive_end;
    data->directive_kind = record->directive_kind;
    data->directive_next = record->directive_next;
    data->guard = record->guard;
    data->prepared = true;
}

// Prepares a file this unit enters for the first time: one it made (the predefines, the command line) into its own
// arenas, and any other through the file cache, which prepares each file once for every unit (file_cache_prepare) and
// whose record the unit's data then copies. Diagnoses a file that can't be prepared, and returns false.
static bool pp_prepare_data(Preprocessor* pp, PPFileData* data, const char* path)
{
    uint64_t* ticks = pp->options.timing ? pp->ticks : 0;
    PPPrepareStatus status;
    if (data->record == BUSTER_PP_NONE)
    {
        const PPPrepare prepare = pp_prepare_config(pp);
        status = pp_prepare_into(&prepare, pp->table, pp->columns, &pp->arena, data, ticks);
    }
    else
    {
        status = file_cache_prepare(pp->cache, data->record, lane_index(), ticks);
        if (status == PP_PREPARE_DONE)
        {
            pp_copy_prepared(data, &pp->cache->records[data->record]);
        }
    }
    if (status == PP_PREPARE_DONE)
    {
        pp->input_tokens += data->token_count;
    }
    else if (status == PP_PREPARE_NO_MEMORY)
    {
        pp_diagnose(pp, PP_SEVERITY_FATAL, 0, "%s: out of memory", path);
    }
    else
    {
        pp_diagnose(pp, PP_SEVERITY_FATAL, 0, "%s: can't tokenize (%s)", path, status == PP_PREPARE_TOO_BIG ? "too big" : "out of memory");
    }
    return status == PP_PREPARE_DONE;
}

// Doubles the path table and places every path again. The table starts small and doubles when it is half full, so
// that the pages it touches are about the ones it needs: at its largest size, the first paths would each touch a huge
// page of their own, which the kernel clears (2% of the time of sqlite3.c's run).
static void pp_grow_file_slots(Preprocessor* pp)
{
    const uint32_t slots = 2 * (pp->file_slot_mask + 1);
    memset(pp->file_slots, 0, slots * sizeof(uint32_t));
    pp->file_slot_mask = slots - 1;
    for (uint32_t f = 0; f < pp->file_count; f += 1)
    {
        uint32_t slot = (uint32_t)intern_hash_bytes((const uint8_t*)pp->files[f].path, pp->files[f].path_length) & pp->file_slot_mask;
        while (pp->file_slots[slot] != 0)
        {
            slot = (slot + 1) & pp->file_slot_mask;
        }
        pp->file_slots[slot] = f + 1;
    }
}

// This unit's record of `path` (`length` bytes, not NUL-terminated): looked up by name, and the first time through the
// file cache (file_cache_lookup_path), which probes the file system only for a path it never looked up. Its data is
// BUSTER_PP_NONE if no file can be read there. Returns BUSTER_PP_NONE if memory runs out.
static uint32_t pp_lookup_path(Preprocessor* pp, const char* path, uint32_t length, uint8_t system)
{
    const uint64_t hash = intern_hash_bytes((const uint8_t*)path, length);
    uint32_t slot = (uint32_t)hash & pp->file_slot_mask;
    uint32_t result = BUSTER_PP_NONE;
    bool done = false;
    while (!done)
    {
        const uint32_t entry = pp->file_slots[slot];
        if (entry == 0)
        {
            done = true;
        }
        else if (pp->files[entry - 1].path_length == length && memcmp(pp->files[entry - 1].path, path, length) == 0)
        {
            result = entry - 1;
            done = true;
        }
        else
        {
            slot = (slot + 1) & pp->file_slot_mask;
        }
    }
    // The table has room: it is never more than half full, and can grow to twice as large as the most files there can be.
    if (result == BUSTER_PP_NONE && pp->file_count < BUSTER_PP_MAX_FILES)
    {
        char* copy = arena_push(&pp->arena, (uint64_t)length + 1, 1);
        if (copy != 0)
        {
            memcpy(copy, path, length);
            copy[length] = 0;
            uint32_t directory_length = BUSTER_PP_NONE;
            for (uint32_t i = 0; i < length; i += 1)
            {
                directory_length = path[i] == '/' ? i : directory_length;
            }
            const uint64_t start = pp->options.timing ? __rdtsc() : 0;
            const uint32_t record = file_cache_lookup_path(pp->cache, copy, length, &pp->probes);
            const uint32_t data = record != BUSTER_PP_NONE ? pp_data_of_record(pp, record) : BUSTER_PP_NONE;
            pp->ticks[PP_PHASE_READ] += pp->options.timing ? __rdtsc() - start : 0;
            result = pp->file_count;
            pp->files[result] = (PPFile){
                .path = copy,
                .path_length = length,
                .directory_length = directory_length,
                .data = data,
                .system = system,
            };
            pp->file_count += 1;
            pp->file_slots[slot] = result + 1;
            if (2 * pp->file_count > pp->file_slot_mask + 1)
            {
                pp_grow_file_slots(pp);
            }
        }
    }
    return result;
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
    // No path: the directory and the name together are too long for one.
    PP_CANDIDATE_SKIP,
    // There are no more candidates.
    PP_CANDIDATE_END,
} PPCandidate;

// Candidate `index` (from 0) of the search for the file an include names, `name` in `#include "name"` or (`angled`)
// `#include <name>`: an absolute name itself; else the includer's directory (quoted only), then the search directories
// (the quoted ones only for a quoted name), or for #include_next (`next`) the directories after the one the includer's
// inclusion was found in (`includer_search`). For a path, writes it to `path` (PATH_MAX bytes, not NUL-terminated),
// its length, the system level a file found there has, and the search directory it is in (BUSTER_PP_NONE for the
// includer's directory or an absolute path). pp_find_include and the run-ahead (c_files.c) both go through the
// candidates in order, so that they look at the same paths.
static PPCandidate pp_include_candidate(const Preprocessor* pp, const uint8_t* name, uint32_t length, bool angled, bool next, const PPIncluder* includer,
                                        uint32_t includer_search, uint32_t index, char* path, uint32_t* path_length, uint8_t* system, uint32_t* directory)
{
    PPCandidate result = PP_CANDIDATE_END;
    *directory = BUSTER_PP_NONE;
    if (length != 0 && length < PATH_MAX && name[0] == '/')
    {
        // An absolute path: its record, if a file is there (a record is kept for a missing one too).
        if (index == 0)
        {
            memcpy(path, name, length);
            *path_length = length;
            *system = BUSTER_PP_USER_FILE;
            result = PP_CANDIDATE_PATH;
        }
    }
    else if (length != 0 && length < PATH_MAX)
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
                result = PP_CANDIDATE_SKIP;
                if ((uint64_t)directory_length + 1 + length < PATH_MAX)
                {
                    memcpy(path, pp->directories[i], directory_length);
                    path[directory_length] = '/';
                    memcpy(path + directory_length + 1, name, length);
                    *path_length = directory_length + 1 + length;
                    *system = pp->directory_system[i];
                    *directory = (uint32_t)i;
                    result = PP_CANDIDATE_PATH;
                }
            }
        }
    }
    return result;
}

// Finds the file an include names, looking at each of its search's candidates in turn (pp_include_candidate) until a
// file is at one. Returns the PPFile, or BUSTER_PP_NONE if no file has the name; `*found_in` gets the search directory
// it was found in, or BUSTER_PP_NONE (the includer's directory, or an absolute path).
static uint32_t pp_find_include(Preprocessor* pp, const uint8_t* name, uint32_t length, bool angled, bool next, uint32_t includer, uint32_t includer_search,
                                uint32_t* found_in)
{
    char path[PATH_MAX];
    const PPFile* from = includer != BUSTER_PP_NONE ? &pp->files[includer] : 0;
    PPIncluder source = { 0 };
    if (from != 0)
    {
        source = (PPIncluder){ .path = from->path, .directory_length = from->directory_length, .system = from->system, .present = true };
    }
    uint32_t result = BUSTER_PP_NONE;
    *found_in = BUSTER_PP_NONE;
    PPCandidate candidate = PP_CANDIDATE_SKIP;
    for (uint32_t index = 0; result == BUSTER_PP_NONE && candidate != PP_CANDIDATE_END; index += 1)
    {
        uint32_t path_length = 0;
        uint8_t system = 0;
        uint32_t directory = BUSTER_PP_NONE;
        candidate = pp_include_candidate(pp, name, length, angled, next, &source, includer_search, index, path, &path_length, &system, &directory);
        if (candidate == PP_CANDIDATE_PATH)
        {
            const uint32_t file = pp_lookup_path(pp, path, path_length, system);
            result = file != BUSTER_PP_NONE && pp->files[file].data != BUSTER_PP_NONE ? file : BUSTER_PP_NONE;
            *found_in = result != BUSTER_PP_NONE ? directory : BUSTER_PP_NONE;
        }
    }
    return result;
}

// The forms of an include's header name (pp_literal_header_name).
typedef enum PPHeaderForm
{
    // `<...>`, or a string literal alone: the name is known without expanding the line.
    PP_HEADER_LITERAL,
    // Anything else: the line must be expanded first, and then be one of the literal forms.
    PP_HEADER_COMPUTED,
    // An empty line, `<` without its `>`, or a name longer than the room for it.
    PP_HEADER_INVALID,
} PPHeaderForm;

// The header name of directive d, an #include, #include_next or #import of a prepared file: `<...>` read from the
// source, up to the '>' on the line, or a string literal alone, both without line splices (translation phase 2 removed
// them). For a literal form, writes the name to `name` (`capacity` bytes) and its length, and whether it is angled.
// `scratch` holds the string literal's spelling while its splices are removed (room for the token's bytes).
static PPHeaderForm pp_literal_header_name(const PPFileData* data, uint32_t d, uint32_t refine_flags, uint8_t* name, uint32_t capacity, uint32_t* length, bool* angled,
                                           Arena* scratch)
{
    const uint32_t hash = data->directive_hash[d];
    const uint32_t end = data->directive_end[d];
    const uint32_t first = hash + 2;
    PPHeaderForm result = PP_HEADER_INVALID;
    *length = 0;
    *angled = false;
    if (first < end && (data->kinds[first] & 0x7F) == C_TOKEN_LESS)
    {
        // The lexer doesn't make header names: read <...> from the source, up to the '>' on the line, through the scalar
        // lexer's characters, which skip line splices.
        const uint32_t line_end = end < data->token_count ? data->starts[end] : data->length;
        const ScalarLexer lexer = {
            .source = data->bytes,
            .length = line_end,
            .trigraphs = (refine_flags & C_TOKENIZER_TRIGRAPHS) != 0,
            .cplusplus = false,
        };
        uint64_t p = data->starts[first] + 1;
        bool closed = false;
        bool line_ended = false;
        while (!closed && !line_ended && p < line_end && *length < capacity)
        {
            uint64_t size;
            uint64_t char_start;
            const int32_t c = get_char(&lexer, p, &size, &char_start);
            closed = c == '>';
            line_ended = c == '\n' || c == '\r' || c == BUSTER_LEX_END_OF_FILE;
            if (!closed && !line_ended)
            {
                name[*length] = (uint8_t)c;
                *length += 1;
            }
            p += size != 0 ? size : 1;
        }
        result = closed ? PP_HEADER_LITERAL : PP_HEADER_INVALID;
        *angled = closed;
    }
    else if (first + 1 == end && (data->kinds[first] & 0x7F) == C_TOKEN_STRING_LITERAL)
    {
        // The string literal without its line splices.
        const uint32_t start = data->starts[first];
        const uint32_t stop = data->values[first];
        const uint64_t mark = scratch->length;
        uint8_t* buffer = arena_push(scratch, (uint64_t)(stop - start) + 1, 1);
        const uint8_t* spelling = 0;
        const uint32_t spelled = buffer != 0 ? pp_remove_splices(data->bytes + start, stop - start, refine_flags, buffer, &spelling) : 0;
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

// ---------------------------------------------------------------------------------------------------------------------
// Macros

BUSTER_INLINE bool pp_is_macro(const Preprocessor* pp, IdentId id)
{
    return (pp->is_macro[id / 64] >> (id % 64)) & 1;
}

static void pp_set_macro(Preprocessor* pp, IdentId name, PPMacroId macro)
{
    pp->macro_of[name] = macro;
    if (macro != 0)
    {
        pp->is_macro[name / 64] |= 1ull << (name % 64);
    }
    else
    {
        pp->is_macro[name / 64] &= ~(1ull << (name % 64));
    }
}

// A new macro record (id from 1). Returns 0 and a fatal error if there are too many.
static PPMacroId pp_new_macro(Preprocessor* pp, IdentId name)
{
    PPMacroId result = 0;
    if (pp->macro_count < BUSTER_PP_MAX_MACROS)
    {
        result = pp->macro_count;
        pp->macros[result] = (PPMacro){ .name = name };
        pp->macro_count += 1;
    }
    else
    {
        pp_diagnose(pp, PP_SEVERITY_FATAL, 0, "too many macro definitions");
    }
    return result;
}

static void pp_define_builtin(Preprocessor* pp, IdentId name, PPBuiltin builtin, bool function_like)
{
    const PPMacroId macro = pp_new_macro(pp, name);
    if (macro != 0)
    {
        pp->macros[macro].flags = BUSTER_PP_MACRO_BUILTIN | (function_like ? BUSTER_PP_MACRO_FUNCTION : 0);
        pp->macros[macro].builtin = (uint8_t)builtin;
        pp_set_macro(pp, name, macro);
    }
}

// The bytes of a file token (from its start to its end, which an identifier's id replaced: refine_identifier_end
// finds it again).
static uint32_t pp_file_token_end(const Preprocessor* pp, const PPFileData* data, uint32_t token)
{
    uint32_t result = data->values[token];
    if ((data->kinds[token] & 0x7F) == C_TOKEN_RAW_IDENTIFIER)
    {
        result = pp_identifier_end(pp, data->bytes, data->length, data->starts[token]);
    }
    return result;
}

// Do two file tokens have the same spelling? (Identifiers by id; others by their spellings, which differ for 1 and 01
// or <: and [ even when their kinds match, but not for 1\<newline>0 and 10: line splices are removed first.)
static bool pp_same_file_token(const Preprocessor* pp, const PPFileData* a, uint32_t i, const PPFileData* b, uint32_t j)
{
    bool result = (a->kinds[i] & 0x7F) == (b->kinds[j] & 0x7F);
    if (result && (a->kinds[i] & 0x7F) == C_TOKEN_RAW_IDENTIFIER)
    {
        result = a->values[i] == b->values[j];
    }
    else if (result)
    {
        const uint32_t a_length = a->values[i] - a->starts[i];
        const uint32_t b_length = b->values[j] - b->starts[j];
        Scratch scratch = scratch_begin(0);
        uint8_t* a_buffer = arena_push(scratch.arena, (uint64_t)a_length + 1, 1);
        uint8_t* b_buffer = arena_push(scratch.arena, (uint64_t)b_length + 1, 1);
        const uint8_t* a_spelling = a->bytes + a->starts[i];
        const uint8_t* b_spelling = b->bytes + b->starts[j];
        uint32_t a_spelled = a_length;
        uint32_t b_spelled = b_length;
        if (a_buffer != 0 && b_buffer != 0)
        {
            a_spelled = pp_without_splices(pp, a->bytes + a->starts[i], a_length, a_buffer, &a_spelling);
            b_spelled = pp_without_splices(pp, b->bytes + b->starts[j], b_length, b_buffer, &b_spelling);
        }
        result = a_spelled == b_spelled && memcmp(a_spelling, b_spelling, a_spelled) == 0;
        scratch_end(scratch);
    }
    return result;
}

BUSTER_INLINE bool pp_file_space(const PPFileData* data, uint32_t token)
{
    return (data->space[token / 64] >> (token % 64)) & 1;
}

// Are two definitions the same (C 6.10.4p2): the same form, parameters and replacement list, with whitespace in the
// same places? A redefinition that isn't is diagnosed.
static bool pp_same_definition(const Preprocessor* pp, const PPMacro* a, const PPMacro* b)
{
    const PPFileData* a_data = &pp->datas[a->data];
    const PPFileData* b_data = &pp->datas[b->data];
    const uint32_t a_parameters_end = (a->flags & BUSTER_PP_MACRO_FUNCTION) ? a->body_begin : a->parameters_begin;
    const uint32_t b_parameters_end = (b->flags & BUSTER_PP_MACRO_FUNCTION) ? b->body_begin : b->parameters_begin;
    bool result = ((a->flags ^ b->flags) & (BUSTER_PP_MACRO_FUNCTION | BUSTER_PP_MACRO_VARIADIC | BUSTER_PP_MACRO_BUILTIN)) == 0 &&
                  a->parameter_count == b->parameter_count && a->body_end - a->body_begin == b->body_end - b->body_begin &&
                  a_parameters_end - a->parameters_begin == b_parameters_end - b->parameters_begin && (a->flags & BUSTER_PP_MACRO_BUILTIN) == 0;
    for (uint32_t i = 0; result && i < a_parameters_end - a->parameters_begin; i += 1)
    {
        result = pp_same_file_token(pp, a_data, a->parameters_begin + i, b_data, b->parameters_begin + i);
    }
    for (uint32_t i = 0; result && i < a->body_end - a->body_begin; i += 1)
    {
        const uint32_t ai = a->body_begin + i;
        const uint32_t bi = b->body_begin + i;
        result = pp_same_file_token(pp, a_data, ai, b_data, bi) && (i == 0 || pp_file_space(a_data, ai) == pp_file_space(b_data, bi));
    }
    return result;
}

// Whether a replacement list's tokens [begin, end) hold a '##' (`*pastes`) and a '#' (`*hashes`), 64 kinds at a time:
// two compares per 64, masked to the list (a masked load doesn't touch the bytes it leaves out).
static void pp_scan_body_vector(const uint8_t* kinds, uint32_t begin, uint32_t end, bool* pastes, bool* hashes)
{
    uint64_t found_pastes = 0;
    uint64_t found_hashes = 0;
    for (uint32_t t = begin; t < end; t += 64)
    {
        const __mmask64 lanes = _bzhi_u64(~0ull, end - t < 64 ? end - t : 64);
        const __m512i kind = _mm512_and_si512(_mm512_maskz_loadu_epi8(lanes, kinds + t), _mm512_set1_epi8(0x7F));
        found_pastes |= _mm512_mask_cmpeq_epi8_mask(lanes, kind, _mm512_set1_epi8(C_TOKEN_HASHHASH));
        found_hashes |= _mm512_mask_cmpeq_epi8_mask(lanes, kind, _mm512_set1_epi8(C_TOKEN_HASH));
    }
    *pastes = found_pastes != 0;
    *hashes = found_hashes != 0;
}

// pp_scan_body_vector's scalar twin.
static void pp_scan_body_scalar(const uint8_t* kinds, uint32_t begin, uint32_t end, bool* pastes, bool* hashes)
{
    *pastes = false;
    *hashes = false;
    for (uint32_t t = begin; t < end; t += 1)
    {
        *pastes |= (kinds[t] & 0x7F) == C_TOKEN_HASHHASH;
        *hashes |= (kinds[t] & 0x7F) == C_TOKEN_HASH;
    }
}

// Whether `id` names a parameter of a function-like macro being defined: an identifier of its parameter list, or
// __VA_ARGS__ when it is variadic.
static bool pp_names_parameter(const PPFileData* data, const PPMacro* macro, IdentId id)
{
    bool result = id == C_NAME_VA_ARGS && (macro->flags & BUSTER_PP_MACRO_VARIADIC);
    for (uint32_t t = macro->parameters_begin; !result && t < macro->body_begin; t += 1)
    {
        result = (data->kinds[t] & 0x7F) == C_TOKEN_RAW_IDENTIFIER && data->values[t] == id;
    }
    return result;
}

// Checks a function-like macro's replacement list as it is defined, for what pp_compile_macro would reject at its first
// expansion: # not followed by a parameter (or by __VA_OPT__ and its '(' in a variadic macro), and __VA_OPT__ not followed
// by '(', inside another, or left open. Returns false after a diagnostic.
static bool pp_check_replacement_list(Preprocessor* pp, const PPFileData* data, const PPMacro* macro)
{
    const bool variadic = (macro->flags & BUSTER_PP_MACRO_VARIADIC) != 0;
    bool result = true;
    uint32_t va_opt_depth = 0;
    for (uint32_t t = macro->body_begin; result && t < macro->body_end; t += 1)
    {
        const uint8_t kind = data->kinds[t] & 0x7F;
        const bool next_is_identifier = t + 1 < macro->body_end && (data->kinds[t + 1] & 0x7F) == C_TOKEN_RAW_IDENTIFIER;
        const bool va_opt = variadic && kind == C_TOKEN_RAW_IDENTIFIER && data->values[t] == C_NAME_VA_OPT;
        const bool next_va_opt = variadic && next_is_identifier && data->values[t + 1] == C_NAME_VA_OPT;
        if (kind == C_TOKEN_HASH && !(next_is_identifier && pp_names_parameter(data, macro, data->values[t + 1])) && !next_va_opt)
        {
            // At the token after '#', as clang reports it.
            pp_diagnose(pp, PP_SEVERITY_ERROR, macro->base + data->starts[t + (t + 1 < macro->body_end)], "'#' is not followed by a macro parameter");
            result = false;
        }
        else if (va_opt && (va_opt_depth != 0 || t + 1 >= macro->body_end || (data->kinds[t + 1] & 0x7F) != C_TOKEN_L_PAREN))
        {
            pp_diagnose(pp, PP_SEVERITY_ERROR, macro->base + data->starts[t], "__VA_OPT__ must be followed by '(' and can't nest");
            result = false;
        }
        else if (va_opt)
        {
            va_opt_depth = 1;
            t += 1;
        }
        else if (va_opt_depth != 0 && kind == C_TOKEN_L_PAREN)
        {
            va_opt_depth += 1;
        }
        else if (va_opt_depth != 0 && kind == C_TOKEN_R_PAREN)
        {
            va_opt_depth -= 1;
        }
    }
    if (result && va_opt_depth != 0)
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, macro->location, "unterminated __VA_OPT__");
        result = false;
    }
    return result;
}

// #define, on the line [begin, end) of a file's tokens (the name's token first), read in the range at `base`.
static void pp_define(Preprocessor* pp, uint32_t data_index, uint32_t base, uint32_t begin, uint32_t end)
{
    const PPFileData* data = &pp->datas[data_index];
    const uint32_t location = base + (begin < data->token_count ? data->starts[begin] : data->length);
    if (begin >= end || (data->kinds[begin] & 0x7F) != C_TOKEN_RAW_IDENTIFIER)
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, location, begin >= end ? "macro name missing" : "macro name must be an identifier");
    }
    else if (data->values[begin] == C_NAME_DEFINED)
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, location, "'defined' cannot be used as a macro name");
    }
    else
    {
        const IdentId name = data->values[begin];
        PPMacro macro = {
            .name = name,
            .data = data_index,
            .base = base,
            .parameters_begin = begin + 1,
            .body_begin = begin + 1,
            .body_end = end,
            .location = location,
        };
        bool valid = true;
        // Function-like when '(' follows the name with no whitespace or comment between them (a line splice is none).
        if (begin + 1 < end && (data->kinds[begin + 1] & 0x7F) == C_TOKEN_L_PAREN && !pp_file_space(data, begin + 1))
        {
            macro.flags |= BUSTER_PP_MACRO_FUNCTION;
            macro.parameters_begin = begin + 2;
            uint32_t t = begin + 2;
            bool closed = false;
            bool expect_parameter = true;
            while (valid && !closed && t < end)
            {
                const uint8_t kind = data->kinds[t] & 0x7F;
                if (kind == C_TOKEN_R_PAREN && (!expect_parameter || macro.parameter_count == 0))
                {
                    closed = true;
                }
                else if (expect_parameter && kind == C_TOKEN_ELLIPSIS)
                {
                    macro.flags |= BUSTER_PP_MACRO_VARIADIC;
                    macro.parameter_count += 1;
                    expect_parameter = false;
                    valid = t + 1 < end && (data->kinds[t + 1] & 0x7F) == C_TOKEN_R_PAREN;
                }
                else if (expect_parameter && kind == C_TOKEN_RAW_IDENTIFIER)
                {
                    const IdentId parameter = data->values[t];
                    for (uint32_t p = macro.parameters_begin; valid && p < t; p += 1)
                    {
                        valid = !((data->kinds[p] & 0x7F) == C_TOKEN_RAW_IDENTIFIER && data->values[p] == parameter);
                    }
                    valid &= parameter != C_NAME_VA_ARGS;
                    macro.parameter_count += 1;
                    expect_parameter = false;
                    // GNU's named variadic parameter: `name...`.
                    if (valid && t + 1 < end && (data->kinds[t + 1] & 0x7F) == C_TOKEN_ELLIPSIS)
                    {
                        macro.flags |= BUSTER_PP_MACRO_VARIADIC;
                        t += 1;
                        valid = t + 1 < end && (data->kinds[t + 1] & 0x7F) == C_TOKEN_R_PAREN;
                    }
                }
                else if (!expect_parameter && kind == C_TOKEN_COMMA && !(macro.flags & BUSTER_PP_MACRO_VARIADIC))
                {
                    expect_parameter = true;
                }
                else
                {
                    valid = false;
                }
                t += 1;
            }
            valid &= closed && macro.parameter_count <= BUSTER_PP_MAX_PARAMETERS;
            macro.body_begin = t;
            if (!valid)
            {
                pp_diagnose(pp, PP_SEVERITY_ERROR, location, "invalid macro parameter list");
            }
        }
        // ## can't begin or end a replacement list, and in a function-like macro # must precede a parameter and
        // __VA_OPT__ a '(' (pp_check_replacement_list): clang rejects such a definition when it reads it, used or not.
        if (valid && macro.body_begin < macro.body_end)
        {
            const bool starts_pasting = (data->kinds[macro.body_begin] & 0x7F) == C_TOKEN_HASHHASH;
            const bool ends_pasting = (data->kinds[macro.body_end - 1] & 0x7F) == C_TOKEN_HASHHASH;
            if (starts_pasting || ends_pasting)
            {
                pp_diagnose(pp, PP_SEVERITY_ERROR, location, "'##' cannot appear at either end of a macro expansion");
                valid = false;
            }
            bool pastes;
            bool hashes;
            if (pp->vectorized)
            {
                pp_scan_body_vector(data->kinds, macro.body_begin, macro.body_end, &pastes, &hashes);
            }
            else
            {
                pp_scan_body_scalar(data->kinds, macro.body_begin, macro.body_end, &pastes, &hashes);
            }
            macro.flags |= pastes ? BUSTER_PP_MACRO_PASTES : 0;
            // A function-like macro with a '#', or a variadic one (which may have __VA_OPT__), gets the whole check.
            const bool to_check = hashes || (macro.flags & BUSTER_PP_MACRO_VARIADIC) != 0;
            valid = valid && (!to_check || !(macro.flags & BUSTER_PP_MACRO_FUNCTION) || pp_check_replacement_list(pp, data, &macro));
        }
        if (valid)
        {
            const PPMacroId previous = pp->macro_of[name];
            if (previous != 0 && (pp->macros[previous].flags & BUSTER_PP_MACRO_BUILTIN))
            {
                pp_diagnose(pp, PP_SEVERITY_WARNING, location, "redefining builtin macro");
            }
            else if (previous != 0 && !pp_same_definition(pp, &pp->macros[previous], &macro))
            {
                pp_diagnose(pp, PP_SEVERITY_WARNING, location, "'%.*s' macro redefined", (int)intern_length(pp->table, name), intern_bytes(pp->table, name));
            }
            const PPMacroId id = pp_new_macro(pp, name);
            if (id != 0)
            {
                pp->macros[id] = macro;
                pp_set_macro(pp, name, id);
            }
        }
    }
}

// The index of `parameter` among a macro's parameters (__VA_ARGS__ is the variadic one), or BUSTER_PP_NONE.
static uint32_t pp_parameter_index(const Preprocessor* pp, const PPMacro* macro, IdentId id)
{
    uint32_t result = BUSTER_PP_NONE;
    const IdentId* parameters = pp->parameters + macro->parameters;
    for (uint32_t p = 0; result == BUSTER_PP_NONE && p < macro->parameter_count; p += 1)
    {
        result = parameters[p] == id ? p : BUSTER_PP_NONE;
    }
    return result;
}

// Compiles a macro's replacement list into items (PPItem): its parameters by index, # with its parameter, ## and
// __VA_OPT__ with its content. Returns false (with a diagnostic) if the list is invalid.
static bool pp_compile_macro(Preprocessor* pp, PPMacroId id)
{
    PPMacro* macro = &pp->macros[id];
    const PPFileData* data = &pp->datas[macro->data];
    const bool function_like = (macro->flags & BUSTER_PP_MACRO_FUNCTION) != 0;
    const bool variadic = (macro->flags & BUSTER_PP_MACRO_VARIADIC) != 0;
    bool result = pp->item_count + (macro->body_end - macro->body_begin) <= BUSTER_PP_MAX_ITEMS &&
                  pp->parameter_count + macro->parameter_count <= BUSTER_PP_MAX_ITEMS;
    // The parameters' ids: the identifiers of the parameter list, and __VA_ARGS__ for `...`.
    macro->parameters = pp->parameter_count;
    uint32_t parameter = 0;
    for (uint32_t t = macro->parameters_begin; result && function_like && t < macro->body_begin; t += 1)
    {
        const uint8_t kind = data->kinds[t] & 0x7F;
        if (kind == C_TOKEN_RAW_IDENTIFIER)
        {
            pp->parameters[macro->parameters + parameter] = data->values[t];
            parameter += 1;
        }
        else if (kind == C_TOKEN_ELLIPSIS && (data->kinds[t - 1] & 0x7F) != C_TOKEN_RAW_IDENTIFIER)
        {
            pp->parameters[macro->parameters + parameter] = C_NAME_VA_ARGS;
            parameter += 1;
        }
    }
    pp->parameter_count += macro->parameter_count;
    macro->items_begin = pp->item_count;
    macro->expanded_parameters = 0;
    uint32_t va_opt_item = BUSTER_PP_NONE; // The open __VA_OPT__'s item,
    uint32_t va_opt_depth = 0; // and its parentheses' depth.
    for (uint32_t t = macro->body_begin; result && t < macro->body_end; t += 1)
    {
        const uint8_t kind = data->kinds[t] & 0x7F;
        const uint8_t flags = pp_file_space(data, t) ? BUSTER_PP_SPACE : 0;
        PPItem item = { .kind = PP_ITEM_TOKEN, .flags = flags, .token = t };
        const uint32_t index = kind == C_TOKEN_RAW_IDENTIFIER && function_like ? pp_parameter_index(pp, macro, data->values[t]) : BUSTER_PP_NONE;
        const bool va_opt = function_like && variadic && kind == C_TOKEN_RAW_IDENTIFIER && data->values[t] == C_NAME_VA_OPT;
        if (index != BUSTER_PP_NONE)
        {
            item.kind = PP_ITEM_PARAMETER;
            item.parameter = (uint16_t)index;
        }
        else if (kind == C_TOKEN_HASHHASH)
        {
            item.kind = PP_ITEM_PASTE;
        }
        else if (va_opt)
        {
            if (va_opt_item != BUSTER_PP_NONE || t + 1 >= macro->body_end || (data->kinds[t + 1] & 0x7F) != C_TOKEN_L_PAREN)
            {
                pp_diagnose(pp, PP_SEVERITY_ERROR, macro->base + data->starts[t], "__VA_OPT__ must be followed by '(' and can't nest");
                result = false;
            }
            item.kind = PP_ITEM_VA_OPT;
            va_opt_item = pp->item_count;
            va_opt_depth = 1;
            t += 1;
        }
        else if (kind == C_TOKEN_HASH && function_like)
        {
            const uint32_t next = t + 1;
            const bool next_va_opt = next < macro->body_end && variadic && (data->kinds[next] & 0x7F) == C_TOKEN_RAW_IDENTIFIER && data->values[next] == C_NAME_VA_OPT;
            const uint32_t next_index = next < macro->body_end && (data->kinds[next] & 0x7F) == C_TOKEN_RAW_IDENTIFIER ? pp_parameter_index(pp, macro, data->values[next]) : BUSTER_PP_NONE;
            if (next_index != BUSTER_PP_NONE)
            {
                item.kind = PP_ITEM_STRINGIZE;
                item.parameter = (uint16_t)next_index;
                t += 1;
            }
            else if (next_va_opt && va_opt_item == BUSTER_PP_NONE && next + 1 < macro->body_end && (data->kinds[next + 1] & 0x7F) == C_TOKEN_L_PAREN)
            {
                item.kind = PP_ITEM_STRINGIZE_VA_OPT;
                va_opt_item = pp->item_count;
                va_opt_depth = 1;
                t += 2;
            }
            else
            {
                pp_diagnose(pp, PP_SEVERITY_ERROR, macro->base + data->starts[t + (t + 1 < macro->body_end)], "'#' is not followed by a macro parameter");
                result = false;
            }
        }
        else if (va_opt_item != BUSTER_PP_NONE && kind == C_TOKEN_L_PAREN)
        {
            va_opt_depth += 1;
        }
        else if (va_opt_item != BUSTER_PP_NONE && kind == C_TOKEN_R_PAREN)
        {
            va_opt_depth -= 1;
            if (va_opt_depth == 0)
            {
                // The closing ')' isn't an item: the __VA_OPT__ item counts its content.
                pp->items[va_opt_item].parameter = (uint16_t)(pp->item_count - va_opt_item - 1);
                va_opt_item = BUSTER_PP_NONE;
                continue;
            }
        }
        if (result)
        {
            pp->items[pp->item_count] = item;
            pp->item_count += 1;
        }
    }
    if (result && va_opt_item != BUSTER_PP_NONE)
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, macro->location, "unterminated __VA_OPT__");
        result = false;
    }
    // The parameters substituted expanded: those that aren't an operand of ## (or of #, which has its own item). A
    // parameter that ends a __VA_OPT__'s content is no operand of a ## after the __VA_OPT__: the content is expanded first.
    const PPItem* items = pp->items + macro->items_begin;
    const uint32_t count = pp->item_count - macro->items_begin;
    uint32_t content_end = BUSTER_PP_NONE;
    for (uint32_t i = 0; result && i < count; i += 1)
    {
        if (items[i].kind == PP_ITEM_PARAMETER)
        {
            const bool pasted = (i > 0 && items[i - 1].kind == PP_ITEM_PASTE) || (i + 1 < count && i + 1 != content_end && items[i + 1].kind == PP_ITEM_PASTE);
            if (!pasted)
            {
                macro->expanded_parameters |= 1ull << (items[i].parameter < 63 ? items[i].parameter : 63);
            }
        }
        else if (items[i].kind == PP_ITEM_VA_OPT || items[i].kind == PP_ITEM_STRINGIZE_VA_OPT)
        {
            // __VA_OPT__ tests whether the expanded variadic argument has tokens.
            const uint32_t variadic_index = macro->parameter_count - 1U;
            macro->expanded_parameters |= 1ull << (variadic_index < 63 ? variadic_index : 63);
            content_end = i + 1 + items[i].parameter;
        }
    }
    macro->item_count = result ? count : 0;
    macro->flags |= result ? BUSTER_PP_MACRO_COMPILED : 0;
    if (!result)
    {
        pp->item_count = macro->items_begin;
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The main loop

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

// Where tokens go: the innermost capture, or the output.
BUSTER_INLINE PPTokens* pp_target(Preprocessor* pp)
{
    return pp->capture_count != 0 ? &pp->captures : &pp->output;
}

static void pp_emit(Preprocessor* pp, PPToken token)
{
    PPTokens* target = pp_target(pp);
    if (pp_room(pp, target, 1))
    {
        pp_tokens_set(target, target->count, token.kind, token.flags, token.value, token.location);
        pp_history_fill(pp, target, target->count, target->count + 1, token.origin);
        target->count += 1;
    }
}

// Token `token` of a prepared file read in the range at `base`, as buffers hold it.
BUSTER_INLINE PPToken pp_file_token(const PPFileData* data, uint32_t base, uint32_t token)
{
    const uint8_t kind = data->kinds[token] & 0x7F;
    PPToken result = {
        .kind = kind,
        .flags = (uint8_t)((pp_file_space(data, token) ? BUSTER_PP_SPACE : 0) | ((data->kinds[token] & BUSTER_C_TOKEN_START_OF_LINE) ? BUSTER_PP_START_OF_LINE : 0)),
        .value = kind == C_TOKEN_RAW_IDENTIFIER ? data->values[token] : base + data->values[token],
        .location = base + data->starts[token],
    };
    return result;
}

static PPToken pp_context_token(const Preprocessor* pp, const PPContext* context, uint32_t position)
{
    PPToken result;
    if (context->kind == BUSTER_PP_CONTEXT_FILE)
    {
        const PPFileData* data = &pp->datas[pp->files[context->file].data];
        result = pp_file_token(data, pp->ranges[context->range].base, position);
    }
    else
    {
        const PPTokens* tokens = context->tokens;
        result = (PPToken){
            .kind = tokens->kinds[position],
            .flags = tokens->flags[position],
            .value = tokens->values[position],
            .location = tokens->locations[position],
            .origin = tokens->origins != 0 ? tokens->origins[position] : 0,
        };
    }
    return result;
}

static PPContext* pp_push_context(Preprocessor* pp, uint8_t kind)
{
    PPContext* result = 0;
    if (pp->context_count < BUSTER_PP_MAX_CONTEXTS)
    {
        result = &pp->contexts[pp->context_count];
        *result = (PPContext){ .kind = kind };
        pp->context_count += 1;
    }
    else
    {
        pp_diagnose(pp, PP_SEVERITY_FATAL, 0, "macro expansion nests too deeply");
    }
    return result;
}

// A buffer context over [begin, end) of `tokens`. `owned`: popping it gives the tokens back when they are on top.
static PPContext* pp_push_buffer(Preprocessor* pp, PPTokens* tokens, uint32_t begin, uint32_t end, PPMacroId macro, uint32_t expansion_end, bool barrier, bool owned)
{
    PPContext* result = pp_push_context(pp, BUSTER_PP_CONTEXT_BUFFER);
    if (result != 0)
    {
        result->tokens = tokens;
        result->position = begin;
        result->end = end;
        result->begin = owned ? begin : end;
        result->macro = macro;
        result->expansion_end = expansion_end;
        result->barrier = barrier;
        pp->macros[macro].disabled += macro != 0;
    }
    return result;
}

BUSTER_INLINE void pp_pop_context(Preprocessor* pp)
{
    PPContext* context = &pp->contexts[pp->context_count - 1];
    pp->macros[context->macro].disabled -= context->macro != 0;
    if (context->kind == BUSTER_PP_CONTEXT_BUFFER)
    {
        if (context->tokens->count == context->end && context->begin < context->end)
        {
            context->tokens->count = context->begin;
        }
    }
    else
    {
        while (pp->conditional_count > context->conditional_base)
        {
            pp_diagnose(pp, PP_SEVERITY_ERROR, pp->conditionals[pp->conditional_count - 1].location, "unterminated conditional directive");
            pp->conditional_count -= 1;
        }
        pp->include_depth -= pp->include_depth != 0;
    }
    pp->context_count -= 1;
    // An expansion that ends passes its pending flags on to the context it was read from; an argument's or a line's
    // barrier doesn't.
    const uint8_t passed = context->pending_flags | context->trailing_flags;
    if (context->kind == BUSTER_PP_CONTEXT_BUFFER && !context->barrier && passed != 0 && pp->context_count != 0)
    {
        pp->contexts[pp->context_count - 1].pending_flags |= passed;
    }
}

// The token where a file context's run ends: its next directive, or its end.
BUSTER_INLINE uint32_t pp_run_limit(const PPFileData* data, const PPContext* context)
{
    return context->next_directive < data->directive_count ? data->directive_hash[context->next_directive] : data->token_count;
}

// Stores tokens p to p + 63 of a prepared file, read in the range at `base`, at `index` of `out` as buffers hold them,
// with whole-vector stores (the lanes `valid` leaves out get junk, which the slack past a stack's capacity has room
// for): kinds without the start-of-line bit, flags from the leading-space bits and either the start-of-line bits or, for
// a macro's body (`from_macro`), BUSTER_PP_FROM_MACRO, values and locations, the values of non-identifiers and the
// locations offset by the base. `kinds` are the file's kinds p to p + 63, loaded with `valid`.
BUSTER_INLINE void pp_store_file_tokens(PPTokens* out, uint32_t index, const PPFileData* data, uint32_t base, uint32_t p, uint32_t available, __m512i kinds, bool from_macro)
{
    const __mmask64 valid = _bzhi_u64(~0ull, available);
    const __m512i plain = _mm512_and_si512(kinds, _mm512_set1_epi8(0x7F));
    const uint64_t identifiers = _mm512_mask_cmpeq_epi8_mask(valid, plain, _mm512_set1_epi8(C_TOKEN_RAW_IDENTIFIER));
    // The leading-space bits of tokens p to p + 63.
    const uint32_t shift = p % 64;
    uint64_t space = data->space[p / 64] >> shift;
    if (shift != 0)
    {
        // pp_prepare_data gives the bits a word past the last token's.
        space |= data->space[p / 64 + 1] << (64 - shift);
    }
    const __m512i lines = from_macro ? _mm512_set1_epi8(BUSTER_PP_FROM_MACRO) : _mm512_maskz_mov_epi8(_mm512_movepi8_mask(kinds), _mm512_set1_epi8(BUSTER_PP_START_OF_LINE));
    const __m512i flags = _mm512_or_si512(_mm512_maskz_mov_epi8(space, _mm512_set1_epi8(BUSTER_PP_SPACE)), lines);
    _mm512_storeu_si512(out->kinds + index, plain);
    _mm512_storeu_si512(out->flags + index, flags);
    const __m512i bases = _mm512_set1_epi32((int)base);
    // Only the groups of 16 that hold tokens: most runs are short.
    for (uint32_t group = 0; group * 16 < available; group += 1)
    {
        const __mmask16 lanes = (__mmask16)(valid >> (group * 16));
        const __mmask16 others = (__mmask16)~(identifiers >> (group * 16));
        const __m512i values = _mm512_maskz_loadu_epi32(lanes, data->values + p + group * 16);
        const __m512i starts = _mm512_maskz_loadu_epi32(lanes, data->starts + p + group * 16);
        _mm512_storeu_si512(out->values + index + group * 16, _mm512_mask_add_epi32(values, others, values, bases));
        _mm512_storeu_si512(out->locations + index + group * 16, _mm512_add_epi32(starts, bases));
    }
}

// Appends a prepared file's tokens [begin, end), read in the range at `base`, to `out`, 64 at a time
// (pp_store_file_tokens). Returns false, after a fatal error, if `out` has no room.
static bool pp_append_file_tokens(Preprocessor* pp, PPTokens* out, const PPFileData* data, uint32_t base, uint32_t begin, uint32_t end, bool from_macro)
{
    const bool result = pp_room(pp, out, end - begin);
    if (result)
    {
        uint32_t count = out->count;
        for (uint32_t p = begin; p < end; p += 64)
        {
            const uint32_t available = end - p < 64 ? end - p : 64;
            pp_store_file_tokens(out, count, data, base, p, available, _mm512_maskz_loadu_epi8(_bzhi_u64(~0ull, available), data->kinds + p), from_macro);
            count += available;
        }
        pp_history_fill(pp, out, out->count, count, from_macro ? pp->history_current : 0);
        out->count = count;
    }
    return result;
}

// Appends tokens [begin, end) of a stack to `out` as they are, 16 at a time with whole-vector stores. `tokens` may be
// `out` if [begin, end) is below its count. Returns false, after a fatal error, if `out` has no room.
static bool pp_append_stack_tokens(Preprocessor* pp, PPTokens* out, const PPTokens* tokens, uint32_t begin, uint32_t end)
{
    const bool result = pp_room(pp, out, end - begin);
    if (result)
    {
        uint32_t count = out->count;
        for (uint32_t p = begin; p < end; p += 16)
        {
            const __mmask16 valid = (__mmask16)_bzhi_u32(~0u, end - p < 16 ? end - p : 16);
            const __m128i kinds = _mm_maskz_loadu_epi8(valid, tokens->kinds + p);
            const __m128i flags = _mm_maskz_loadu_epi8(valid, tokens->flags + p);
            const __m512i values = _mm512_maskz_loadu_epi32(valid, tokens->values + p);
            const __m512i locations = _mm512_maskz_loadu_epi32(valid, tokens->locations + p);
            _mm_storeu_si128((__m128i*)(out->kinds + count), kinds);
            _mm_storeu_si128((__m128i*)(out->flags + count), flags);
            _mm512_storeu_si512(out->values + count, values);
            _mm512_storeu_si512(out->locations + count, locations);
            count += (uint32_t)__builtin_popcount(valid);
        }
        pp_history_copy(pp, out, out->count, tokens, begin, end);
        out->count = count;
    }
    return result;
}

// Copies a file context's tokens from its position up to `limit` to the output, stopping at the first identifier that
// names a macro; returns where it stopped. 64 tokens at a time: one compare finds the identifiers, whose ids are tested
// against the macro bitset one by one (a gather is 64 uops on Zen 5), then every column goes out with whole-vector stores
// (pp_store_file_tokens).
static uint32_t pp_copy_file_run_vector(Preprocessor* pp, const PPFileData* data, uint32_t base, uint32_t position, uint32_t limit)
{
    PPTokens* target = pp_target(pp);
    uint32_t p = position;
    bool stopped = false;
    while (!stopped && p < limit && pp_room(pp, target, BUSTER_PP_TOKEN_SLACK))
    {
        const uint32_t available = limit - p < 64 ? limit - p : 64;
        const __mmask64 valid = _bzhi_u64(~0ull, available);
        const __m512i kinds = _mm512_maskz_loadu_epi8(valid, data->kinds + p);
        const __m512i plain = _mm512_and_si512(kinds, _mm512_set1_epi8(0x7F));
        const uint64_t identifiers = _mm512_mask_cmpeq_epi8_mask(valid, plain, _mm512_set1_epi8(C_TOKEN_RAW_IDENTIFIER));
        uint32_t take = available;
        for (uint64_t bits = identifiers; bits != 0; bits &= bits - 1)
        {
            const uint32_t lane = (uint32_t)__builtin_ctzll(bits);
            if (pp_is_macro(pp, data->values[p + lane]))
            {
                take = lane;
                stopped = true;
                break;
            }
        }
        pp_store_file_tokens(target, target->count, data, base, p, available, kinds, false);
        target->count += take;
        p += take;
    }
    return p;
}

// The same, a token at a time.
static uint32_t pp_copy_file_run_scalar(Preprocessor* pp, const PPFileData* data, uint32_t base, uint32_t position, uint32_t limit)
{
    PPTokens* target = pp_target(pp);
    uint32_t p = position;
    bool stopped = false;
    while (!stopped && p < limit && pp_room(pp, target, 1))
    {
        const PPToken token = pp_file_token(data, base, p);
        stopped = token.kind == C_TOKEN_RAW_IDENTIFIER && pp_is_macro(pp, token.value);
        if (!stopped)
        {
            pp_tokens_set(target, target->count, token.kind, token.flags, token.value, token.location);
            target->count += 1;
            p += 1;
        }
    }
    return p;
}

// Copies a buffer context's tokens to the target, stopping at the first identifier that names an enabled macro and
// isn't painted; an identifier that names a disabled macro is painted on the way (BUSTER_PP_NO_EXPAND). Returns where
// it stopped. 64 tokens at a time, as pp_copy_file_run_vector.
static uint32_t pp_copy_buffer_run_vector(Preprocessor* pp, const PPTokens* tokens, uint32_t position, uint32_t end)
{
    PPTokens* target = pp_target(pp);
    uint32_t p = position;
    bool stopped = false;
    while (!stopped && p < end && pp_room(pp, target, BUSTER_PP_TOKEN_SLACK))
    {
        const uint32_t available = end - p < 64 ? end - p : 64;
        const __mmask64 valid = _bzhi_u64(~0ull, available);
        const __m512i kinds = _mm512_maskz_loadu_epi8(valid, tokens->kinds + p);
        const __m512i flags = _mm512_maskz_loadu_epi8(valid, tokens->flags + p);
        const uint64_t unpainted = _mm512_mask_testn_epi8_mask(valid, flags, _mm512_set1_epi8(BUSTER_PP_NO_EXPAND));
        const uint64_t identifiers = _mm512_mask_cmpeq_epi8_mask(unpainted, kinds, _mm512_set1_epi8(C_TOKEN_RAW_IDENTIFIER));
        uint32_t take = available;
        uint64_t paint = 0;
        for (uint64_t bits = identifiers; bits != 0; bits &= bits - 1)
        {
            const uint32_t lane = (uint32_t)__builtin_ctzll(bits);
            const IdentId id = tokens->values[p + lane];
            if (pp_is_macro(pp, id))
            {
                if (pp->macros[pp->macro_of[id]].disabled != 0)
                {
                    paint |= 1ull << lane;
                }
                else
                {
                    take = lane;
                    stopped = true;
                    break;
                }
            }
        }
        const uint32_t out = target->count;
        _mm512_storeu_si512(target->kinds + out, kinds);
        _mm512_storeu_si512(target->flags + out, _mm512_mask_mov_epi8(flags, paint, _mm512_or_si512(flags, _mm512_set1_epi8(BUSTER_PP_NO_EXPAND))));
        for (uint32_t group = 0; group * 16 < available; group += 1)
        {
            const __mmask16 lanes = (__mmask16)(valid >> (group * 16));
            _mm512_storeu_si512(target->values + out + group * 16, _mm512_maskz_loadu_epi32(lanes, tokens->values + p + group * 16));
            _mm512_storeu_si512(target->locations + out + group * 16, _mm512_maskz_loadu_epi32(lanes, tokens->locations + p + group * 16));
        }
        target->count += take;
        p += take;
    }
    return p;
}

static uint32_t pp_copy_buffer_run_scalar(Preprocessor* pp, const PPTokens* tokens, uint32_t position, uint32_t end)
{
    PPTokens* target = pp_target(pp);
    uint32_t p = position;
    bool stopped = false;
    while (!stopped && p < end && pp_room(pp, target, 1))
    {
        uint8_t flags = tokens->flags[p];
        const bool candidate = tokens->kinds[p] == C_TOKEN_RAW_IDENTIFIER && !(flags & BUSTER_PP_NO_EXPAND) && pp_is_macro(pp, tokens->values[p]);
        if (candidate && pp->macros[pp->macro_of[tokens->values[p]]].disabled == 0)
        {
            stopped = true;
        }
        else
        {
            flags |= candidate ? BUSTER_PP_NO_EXPAND : 0;
            pp_tokens_set(target, target->count, tokens->kinds[p], flags, tokens->values[p], tokens->locations[p]);
            target->count += 1;
            p += 1;
        }
    }
    return p;
}

// The end of a new expansion's range, for __LINE__: the invocation's last token's location, or if a macro made that
// token, the end of the expansion it came from.
BUSTER_INLINE uint32_t pp_expansion_end(const PPContext* context, PPToken last)
{
    return (last.flags & BUSTER_PP_FROM_MACRO) && context->kind == BUSTER_PP_CONTEXT_BUFFER ? context->expansion_end : last.location;
}

// Is the token after the top context's current one '('? Looks into the contexts below an exhausted buffer, but not
// past a barrier, a directive or the end of a file.
static bool pp_next_is_lparen(const Preprocessor* pp)
{
    bool result = false;
    bool done = false;
    for (uint32_t c = pp->context_count; !done && c > 0; c -= 1)
    {
        const PPContext* context = &pp->contexts[c - 1];
        const uint32_t position = context->position + (c == pp->context_count);
        if (context->kind == BUSTER_PP_CONTEXT_FILE)
        {
            const PPFileData* data = &pp->datas[pp->files[context->file].data];
            result = position < pp_run_limit(data, context) && (data->kinds[position] & 0x7F) == C_TOKEN_L_PAREN;
            done = true;
        }
        else if (position < context->end)
        {
            result = context->tokens->kinds[position] == C_TOKEN_L_PAREN;
            done = true;
        }
        else
        {
            done = context->barrier;
        }
    }
    return result;
}

static void pp_directive(Preprocessor* pp, PPContext* context);

// Reads the next token from the top context on, popping the buffers it exhausts (which enables their macros again).
// Returns false at the end of a barrier, at a directive and at the end of a file. Its callers read the '(' that
// pp_next_is_lparen saw, and `defined`'s operand in a #if line, neither of which can pass a directive: anything that
// can, the main loop collects (pp_collect_step), so that a directive among an invocation's arguments runs there.
// `expansion_end` gets the token's expansion end (pp_expansion_end).
static bool pp_read_token(Preprocessor* pp, PPToken* token, uint32_t* expansion_end)
{
    bool result = false;
    bool done = false;
    while (!done && !pp->fatal)
    {
        PPContext* context = &pp->contexts[pp->context_count - 1];
        if (context->kind == BUSTER_PP_CONTEXT_FILE)
        {
            const PPFileData* data = &pp->datas[pp->files[context->file].data];
            if (context->position < pp_run_limit(data, context))
            {
                *token = pp_file_token(data, pp->ranges[context->range].base, context->position);
                *expansion_end = token->location;
                context->position += 1;
                result = true;
            }
            done = true;
        }
        else if (context->position < context->end)
        {
            *token = pp_context_token(pp, context, context->position);
            *expansion_end = pp_expansion_end(context, *token);
            context->position += 1;
            result = true;
            done = true;
        }
        else if (context->barrier)
        {
            done = true;
        }
        else
        {
            pp_pop_context(pp);
        }
    }
    return result;
}

static bool pp_push_token(Preprocessor* pp, PPTokens* tokens, PPToken token)
{
    const bool result = pp_room(pp, tokens, 1);
    if (result)
    {
        pp_tokens_set(tokens, tokens->count, token.kind, token.flags, token.value, token.location);
        pp_history_fill(pp, tokens, tokens->count, tokens->count + 1, token.origin != 0 ? token.origin : pp->history_current);
        tokens->count += 1;
    }
    return result;
}

BUSTER_INLINE PPToken pp_stack_token(const PPTokens* tokens, uint32_t index)
{
    return (PPToken){
        .kind = tokens->kinds[index],
        .flags = tokens->flags[index],
        .value = tokens->values[index],
        .location = tokens->locations[index],
        .origin = tokens->origins != 0 ? tokens->origins[index] : 0,
    };
}

// A string or character literal: stringizing escapes its quotes and backslashes.
BUSTER_INLINE bool pp_is_literal(uint8_t kind)
{
    return kind >= C_TOKEN_CHAR_CONSTANT && kind <= C_TOKEN_UTF32_STRING_LITERAL;
}

// Lexes `length` bytes of scratch (at `location`) and appends their tokens to `into`, as buffers hold them (identifiers
// interned). Returns the number of tokens, or BUSTER_PP_NONE if they don't fit.
static uint32_t pp_lex_scratch(Preprocessor* pp, uint32_t location, uint32_t length, PPTokens* into, uint8_t first_flags)
{
    const uint8_t* bytes = pp_location_bytes(pp, location);
    Scratch scratch = scratch_begin(0);
    const uint64_t capacity = BUSTER_C_TOKENIZER_CAPACITY((uint64_t)length);
    uint8_t* kinds = arena_push(scratch.arena, capacity, 64);
    uint32_t* starts = arena_push_array(scratch.arena, capacity, sizeof(uint32_t), 64);
    uint32_t* ends = arena_push_array(scratch.arena, capacity, sizeof(uint32_t), 64);
    int64_t count = kinds != 0 && starts != 0 && ends != 0 ? c_tokenize_scalar(bytes, length, kinds, starts, ends, pp->refine_flags) : -1;
    const PPPrepare mode = pp_prepare_config(pp);
    count = count > 0 ? pp_lex_for_mode(&mode, bytes, kinds, starts, ends, (uint32_t)count, false) : count;
    uint32_t result = count >= 0 && pp_room(pp, into, (uint32_t)count) ? (uint32_t)count : BUSTER_PP_NONE;
    for (uint32_t i = 0; result != BUSTER_PP_NONE && i < (uint32_t)count; i += 1)
    {
        const uint8_t kind = kinds[i] & 0x7F;
        uint32_t value = location + ends[i];
        if (kind == C_TOKEN_RAW_IDENTIFIER)
        {
            value = refine_intern_identifier(pp->table, bytes, length, starts[i], ends[i] - starts[i], pp->refine_flags);
        }
        const bool space = i == 0 ? (first_flags & BUSTER_PP_SPACE) != 0 : starts[i] != ends[i - 1];
        const PPToken token = { .kind = kind, .flags = (uint8_t)((space ? BUSTER_PP_SPACE : 0) | BUSTER_PP_FROM_MACRO), .value = value, .location = location + starts[i] };
        pp_tokens_set(into, into->count, token.kind, token.flags, token.value, token.location);
        pp_history_fill(pp, into, into->count, into->count + 1, pp->history_current);
        into->count += 1;
    }
    scratch_end(scratch);
    return result;
}

// Pastes two tokens (C 6.10.5.3): their spellings joined must lex as exactly one token. Returns false with an error if
// they don't, as clang does, and the caller keeps them both.
BUSTER_INLINE bool pp_paste(Preprocessor* pp, PPToken left, PPToken right, PPToken* result)
{
    Scratch scratch = scratch_begin(0);
    uint8_t left_buffer[24];
    uint8_t right_buffer[24];
    const uint8_t* left_spelling;
    const uint8_t* right_spelling;
    uint8_t* left_space = left.kind == C_TOKEN_RAW_IDENTIFIER || left.kind == BUSTER_PP_KIND_INTEGER ? left_buffer : arena_push(scratch.arena, left.value - left.location + 1, 1);
    uint8_t* right_space = right.kind == C_TOKEN_RAW_IDENTIFIER || right.kind == BUSTER_PP_KIND_INTEGER ? right_buffer : arena_push(scratch.arena, right.value - right.location + 1, 1);
    const uint32_t left_length = pp_spelling(pp, left.kind, left.value, left.location, left_space, &left_spelling);
    const uint32_t right_length = pp_spelling(pp, right.kind, right.value, right.location, right_space, &right_spelling);
    uint8_t* joined = arena_push(scratch.arena, (uint64_t)left_length + right_length + 1, 1);
    bool valid = false;
    if (joined != 0)
    {
        memcpy(joined, left_spelling, left_length);
        memcpy(joined + left_length, right_spelling, right_length);
        const uint32_t length = left_length + right_length;
        pp->statistics.pastes += pp->options.timing;
        // The commonest paste, an identifier and an identifier or a number (`prefix_ ## name`, `x ## 1`), makes an
        // identifier whenever both are ASCII identifier characters, whose interned spelling is the joined bytes: no
        // need to lex them. The scalar kernels lex every paste.
        bool identifier = pp->vectorized && left.kind == C_TOKEN_RAW_IDENTIFIER &&
                          (right.kind == C_TOKEN_RAW_IDENTIFIER || right.kind == C_TOKEN_NUMERIC_CONSTANT || right.kind == BUSTER_PP_KIND_INTEGER);
        for (uint32_t k = 0; identifier && k < length; k += 1)
        {
            const uint8_t c = joined[k];
            identifier = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '$';
        }
        uint8_t kind = C_TOKEN_RAW_IDENTIFIER;
        if (identifier)
        {
            valid = true;
        }
        else
        {
            const uint64_t capacity = BUSTER_C_TOKENIZER_CAPACITY((uint64_t)length);
            uint8_t* kinds = arena_push(scratch.arena, capacity, 64);
            uint32_t* starts = arena_push_array(scratch.arena, capacity, sizeof(uint32_t), 64);
            uint32_t* ends = arena_push_array(scratch.arena, capacity, sizeof(uint32_t), 64);
            int64_t count = kinds != 0 && starts != 0 && ends != 0 ? c_tokenize_scalar(joined, length, kinds, starts, ends, pp->refine_flags) : -1;
            // In assembly, '$' is punctuation (pp_split_dollars), so `$ ## x` gives two tokens.
            count = count > 0 && pp->options.assembly ? pp_split_dollars(pp->refine_flags, joined, kinds, starts, ends, (uint32_t)count) : count;
            // A prefixed literal the mode doesn't have lexes as two tokens (pp_split_kinds).
            valid = count == 1 && starts[0] == 0 && ends[0] == length && !pp_splits_kind(pp->split_kinds, kinds[0]);
            kind = valid ? kinds[0] & 0x7F : kind;
        }
        if (valid)
        {
            const uint32_t location = pp_scratch(pp, joined, length);
            uint32_t value = location + length;
            if (identifier)
            {
                value = intern_find_or_add(pp->table, joined, length);
            }
            else if (kind == C_TOKEN_RAW_IDENTIFIER)
            {
                value = refine_intern_identifier(pp->table, joined, length, 0, length, pp->refine_flags);
            }
            *result = (PPToken){ .kind = kind, .flags = (uint8_t)((left.flags & BUSTER_PP_SPACE) | BUSTER_PP_FROM_MACRO), .value = value, .location = location };
            const PPSource sources[] = {
                { .location = left.location, .origin = left.origin },
                { .location = right.location, .origin = right.origin },
            };
            result->origin = pp_history_generated(pp, PP_GENERATED_PASTE, location, pp->history_spelling, sources, 2);
        }
        else if (!pp->options.assembly)
        {
            // Assembler-with-cpp has no invalid paste: as in GCC and Clang, the two tokens stay, so `% ## eax` is
            // `%eax`, and in `$ ## X` the X is rescanned, which expands a macro X.
            const uint32_t saved = pp->diagnostic_origin;
            pp->diagnostic_origin = pp->history_current;
            pp_diagnose(pp, PP_SEVERITY_ERROR, pp->history_anchor, "pasting formed '%.*s', an invalid preprocessing token", (int)length, (const char*)joined);
            pp->diagnostic_origin = saved;
        }
    }
    scratch_end(scratch);
    return valid;
}

// Stringizes tokens [begin, end) of a stack (C 6.10.4.2): their spellings with one space wherever whitespace separated
// them, quotes and backslashes in literals escaped, in a new string literal.
static PPToken pp_stringize(Preprocessor* pp, const PPTokens* tokens, uint32_t begin, uint32_t end, uint8_t flags, uint32_t anchor)
{
    // An identifier written with UCNs may be up to 10 times as long as its interned spelling (\U00000024 is `$`).
    uint64_t room = 3;
    for (uint32_t i = begin; i < end; i += 1)
    {
        room += tokens->kinds[i] == C_TOKEN_RAW_IDENTIFIER ? 10 * (uint64_t)intern_length(pp->table, tokens->values[i]) + 24 : 2 * (uint64_t)(tokens->values[i] - tokens->locations[i]) + 24;
    }
    Scratch scratch = scratch_begin(0);
    uint8_t* text = arena_push(scratch.arena, room, 1);
    uint8_t* spelling_buffer = arena_push(scratch.arena, room, 1);
    PPToken result = { .kind = C_TOKEN_STRING_LITERAL, .flags = (uint8_t)((flags & BUSTER_PP_SPACE) | BUSTER_PP_FROM_MACRO) };
    if (text != 0 && spelling_buffer != 0)
    {
        uint64_t length = 0;
        text[length] = '"';
        length += 1;
        bool first = true;
        for (uint32_t i = begin; i < end; i += 1)
        {
            const uint8_t kind = tokens->kinds[i];
            if (kind != BUSTER_PP_KIND_PLACEMARKER)
            {
                if (!first && (tokens->flags[i] & (BUSTER_PP_SPACE | BUSTER_PP_START_OF_LINE)))
                {
                    text[length] = ' ';
                    length += 1;
                }
                const uint8_t* spelling;
                uint32_t spelling_length = pp_spelling(pp, kind, tokens->values[i], tokens->locations[i], spelling_buffer, &spelling);
                // Only an identifier whose interned spelling has a byte past 0x7F can have been written with a UCN.
                bool non_ascii = false;
                for (uint32_t k = 0; kind == C_TOKEN_RAW_IDENTIFIER && k < spelling_length; k += 1)
                {
                    non_ascii |= spelling[k] >= 0x80;
                }
                if (non_ascii)
                {
                    const uint8_t* written;
                    const uint32_t written_length = pp_identifier_as_written(pp, tokens->locations[i], spelling_buffer, &written);
                    spelling = written_length != 0 ? written : spelling;
                    spelling_length = written_length != 0 ? written_length : spelling_length;
                }
                const bool escape = pp_is_literal(kind);
                for (uint32_t k = 0; k < spelling_length; k += 1)
                {
                    if (escape && (spelling[k] == '"' || spelling[k] == '\\'))
                    {
                        text[length] = '\\';
                        length += 1;
                    }
                    text[length] = spelling[k];
                    length += 1;
                }
                first = false;
            }
        }
        text[length] = '"';
        length += 1;
        result.location = pp_scratch_clean(pp, text, (uint32_t)length);
        result.value = result.location + (uint32_t)length;
        if (!pp->options.no_history)
        {
            PPSource* sources = arena_push_array(scratch.arena, (uint64_t)(end - begin), sizeof(PPSource), 8);
            if (sources != 0 || begin == end)
            {
                for (uint32_t i = begin; i < end; i += 1)
                {
                    sources[i - begin] = (PPSource){ .location = tokens->locations[i], .origin = tokens->origins[i] };
                }
                result.origin = pp_history_generated(pp, PP_GENERATED_STRING, result.location, anchor, sources, end - begin);
            }
            else
            {
                pp_diagnose(pp, PP_SEVERITY_FATAL, 0, "out of memory retaining stringification inputs");
            }
        }
    }
    scratch_end(scratch);
    return result;
}

// The tokens a parameter stands for in an invocation: its expanded argument, or its raw one (next to ##).
static void pp_argument_range(Preprocessor* pp, const PPInvocation* invocation, uint32_t parameter, bool expanded, PPTokens** tokens, uint32_t* begin, uint32_t* end)
{
    const PPArgument* argument = &pp->argument_records[invocation->arguments + parameter];
    if (expanded && argument->expanded_captured)
    {
        *tokens = &pp->captures;
        *begin = argument->expanded_begin;
        *end = argument->expanded_end;
    }
    else
    {
        *tokens = &pp->arguments;
        *begin = expanded ? argument->expanded_begin : argument->raw_begin;
        *end = expanded ? argument->expanded_end : argument->raw_end;
    }
}

// A body token of a macro, as buffers hold it.
BUSTER_INLINE PPToken pp_body_token(const Preprocessor* pp, const PPMacro* macro, const PPItem* item)
{
    PPToken result = pp_file_token(&pp->datas[macro->data], macro->base, item->token);
    result.flags = (uint8_t)((item->flags & BUSTER_PP_SPACE) | BUSTER_PP_FROM_MACRO);
    result.origin = pp->history_current;
    return result;
}

// Appends tokens [begin, end) of a stack to the expansions, all of them from a macro and none starting a line (one that
// did gets a leading space instead). With `replace_first`, the first takes `first_flags`' leading space: an argument's
// first token takes its parameter's.
static void pp_append_tokens(Preprocessor* pp, const PPTokens* tokens, uint32_t begin, uint32_t end, bool replace_first, uint8_t first_flags)
{
    PPTokens* out = &pp->expansions;
    if (pp_room(pp, out, end - begin))
    {
        // The columns in locals: a store through a uint8_t pointer may change any field, which the loop would reload.
        const uint8_t* kinds = tokens->kinds;
        const uint8_t* flags = tokens->flags;
        const uint32_t* values = tokens->values;
        const uint32_t* locations = tokens->locations;
        uint8_t* out_kinds = out->kinds;
        uint8_t* out_flags = out->flags;
        uint32_t* out_values = out->values;
        uint32_t* out_locations = out->locations;
        uint32_t count = out->count;
        for (uint32_t i = begin; i < end; i += 1)
        {
            uint8_t token_flags = flags[i];
            token_flags = (uint8_t)((token_flags & ~BUSTER_PP_START_OF_LINE) | ((token_flags & BUSTER_PP_START_OF_LINE) ? BUSTER_PP_SPACE : 0) | BUSTER_PP_FROM_MACRO);
            if (i == begin && replace_first)
            {
                token_flags = (uint8_t)((token_flags & ~BUSTER_PP_SPACE) | (first_flags & BUSTER_PP_SPACE));
            }
            out_kinds[count] = kinds[i];
            out_flags[count] = token_flags;
            out_values[count] = values[i];
            out_locations[count] = locations[i];
            count += 1;
        }
        pp_history_argument(pp, tokens, begin, end, out->count, pp->history_anchor);
        out->count = count;
    }
}

// Pastes the token at `at` in `out` onto the one before it, as ## does, when ## is on the left of a __VA_OPT__ and `at`
// is the first token of its result: a placemarker on either side gives the other (with the left's leading space), and an
// invalid paste keeps both (after an error). The tokens after `at` move down when the two become one, and so does
// `*space_at` (pp_substitute's pending space), which a token pasted away drops.
static void pp_paste_in_place(Preprocessor* pp, PPTokens* out, uint32_t at, uint32_t* space_at)
{
    const PPToken left = pp_stack_token(out, at - 1);
    const PPToken right = pp_stack_token(out, at);
    PPToken result = right;
    bool joined = true;
    if (left.kind == BUSTER_PP_KIND_PLACEMARKER)
    {
        result.flags = (uint8_t)((right.flags & ~(BUSTER_PP_SPACE | BUSTER_PP_START_OF_LINE)) | (left.flags & BUSTER_PP_SPACE) | BUSTER_PP_FROM_MACRO);
    }
    else if (right.kind == BUSTER_PP_KIND_PLACEMARKER)
    {
        result = left;
    }
    else
    {
        joined = pp_paste(pp, left, right, &result);
    }
    if (joined)
    {
        pp_tokens_set(out, at - 1, result.kind, result.flags, result.value, result.location);
        pp_history_fill(pp, out, at - 1, at, result.origin);
        for (uint32_t i = at + 1; i < out->count; i += 1)
        {
            pp_tokens_set(out, i - 1, out->kinds[i], out->flags[i], out->values[i], out->locations[i]);
            if (out->origins != 0)
            {
                out->origins[i - 1] = out->origins[i];
            }
        }
        out->count -= 1;
        *space_at = *space_at == at ? BUSTER_PP_NONE : *space_at - (*space_at != BUSTER_PP_NONE && *space_at > at);
    }
    else
    {
        out->flags[at] = (uint8_t)((right.flags & ~BUSTER_PP_START_OF_LINE) | BUSTER_PP_FROM_MACRO);
    }
}

// A leading space (an empty argument's or a __VA_OPT__'s) for the next token pushed to `out`: `*space_at` is where that
// token goes, and an earlier one that has its token by now gets it first. pp_substitute applies the last at its end.
static void pp_pend_space(PPTokens* out, uint32_t* space_at)
{
    if (*space_at != BUSTER_PP_NONE && *space_at < out->count)
    {
        out->flags[*space_at] |= BUSTER_PP_SPACE;
    }
    *space_at = out->count;
}

// Substitutes an invocation's arguments into its macro's compiled body (C 6.10.4), appending the result to the
// expansions, where it starts at `*begin`: parameters by their expanded arguments (raw next to ##), # by a string
// literal, ## by pasting (with placemarkers for empty arguments), __VA_OPT__ by its content when the expanded variadic
// argument has tokens, and GNU's `, ## __VA_ARGS__` by the comma and the arguments, or nothing when the variadic argument
// was omitted. Placemarkers are then removed.
//
// A __VA_OPT__ next to ## is a placemarker when the variadic argument is absent or its content gives no tokens (C
// 6.10.5.2), and otherwise ## pastes the content's first token (## on the left) or last token (## on the right). An
// empty argument at either end of the content is a placemarker for that end's ##, as in clang: `__VA_OPT__(a x) ## y`
// gives `a y` for an empty x.
//
// An empty argument's leading space goes to the next token, as clang does (`a x+b` with an empty x gives `a +b`);
// `*trailing` gets the space left over after the last token, which the token after the expansion takes.
static void pp_substitute(Preprocessor* pp, const PPInvocation* invocation, uint32_t* begin, uint8_t* trailing)
{
    const uint32_t saved_history = pp->history_current;
    pp->history_current = invocation->history;
    const PPMacro* macro = &pp->macros[invocation->macro];
    const PPItem* items = pp->items + macro->items_begin;
    const uint32_t count = macro->item_count;
    PPTokens* out = &pp->expansions;
    *begin = out->count;
    const bool variadic = (macro->flags & BUSTER_PP_MACRO_VARIADIC) != 0;
    const uint32_t variadic_index = macro->parameter_count - 1u;
    bool variadic_present = false;
    if (variadic && invocation->argument_count != 0)
    {
        PPTokens* tokens;
        uint32_t first;
        uint32_t last;
        pp_argument_range(pp, invocation, variadic_index, true, &tokens, &first, &last);
        variadic_present = first != last;
    }
    uint32_t va_opt_end = BUSTER_PP_NONE; // The item where the open __VA_OPT__'s content ends,
    uint32_t va_opt_first = BUSTER_PP_NONE; // the content's first item,
    uint32_t va_opt_begin = 0; // where its tokens start in `out`,
    uint32_t va_opt_mark = BUSTER_PP_NONE; // and for # __VA_OPT__, where its tokens start
    uint8_t va_opt_flags = 0;
    uint32_t va_opt_string_location = 0;
    uint32_t va_opt_paste_location = 0;
    uint32_t va_opt_paste_spelling = 0;
    bool va_opt_pasted_after = false; // Whether ## follows the open __VA_OPT__.
    bool va_opt_paste_left = false; // Whether ## precedes the next __VA_OPT__ (set by the ##).
    uint32_t space_at = BUSTER_PP_NONE; // Where the token that takes a pending leading space goes (pp_pend_space).
    bool va_opt_string_space = false; // Whether #__VA_OPT__'s string takes a space pending before it.
    bool placemarkers = false;
    for (uint32_t i = 0; i <= count && !pp->fatal; i += 1)
    {
        if (i == va_opt_end)
        {
            if (va_opt_mark != BUSTER_PP_NONE)
            {
                // A pending space whose token is in the content goes to it before the content becomes a string; one
                // for the token after the content waits after the string.
                if (space_at != BUSTER_PP_NONE && space_at < out->count)
                {
                    out->flags[space_at] |= BUSTER_PP_SPACE;
                    space_at = BUSTER_PP_NONE;
                }
                const bool space_after = space_at != BUSTER_PP_NONE;
                PPToken string = pp_stringize(pp, out, va_opt_mark, out->count, va_opt_flags, va_opt_string_location);
                string.flags |= va_opt_string_space ? BUSTER_PP_SPACE : 0;
                out->count = va_opt_mark;
                pp_push_token(pp, out, string);
                space_at = space_after ? out->count : BUSTER_PP_NONE;
                va_opt_mark = BUSTER_PP_NONE;
                va_opt_string_space = false;
            }
            // Content of placemarkers alone gives no tokens.
            bool only_placemarkers = true;
            for (uint32_t k = va_opt_begin; only_placemarkers && k < out->count; k += 1)
            {
                only_placemarkers = out->kinds[k] == BUSTER_PP_KIND_PLACEMARKER;
            }
            out->count = only_placemarkers ? va_opt_begin : out->count;
            space_at = space_at != BUSTER_PP_NONE && space_at > out->count ? out->count : space_at;
            if (out->count == va_opt_begin)
            {
                // A placemarker, which ## on the right takes as its left operand and ## on the left leaves alone.
                if (va_opt_pasted_after && !va_opt_paste_left)
                {
                    pp_push_token(pp, out, (PPToken){ .kind = BUSTER_PP_KIND_PLACEMARKER, .flags = va_opt_flags });
                    placemarkers = true;
                }
            }
            else if (va_opt_paste_left)
            {
                pp->history_anchor = va_opt_paste_location;
                pp->history_spelling = va_opt_paste_spelling;
                pp_paste_in_place(pp, out, va_opt_begin, &space_at);
            }
            va_opt_paste_left = false;
            va_opt_end = BUSTER_PP_NONE;
            va_opt_first = BUSTER_PP_NONE;
        }
        if (i == count)
        {
            break;
        }
        const PPItem* item = &items[i];
        pp->history_anchor = macro->base + pp->datas[macro->data].starts[item->token];
        pp->history_spelling = item->kind == PP_ITEM_PASTE && i != 0 ? macro->base + pp->datas[macro->data].starts[items[i - 1].token] : pp->history_anchor;
        // ## after the item, in the same __VA_OPT__'s content when it is in one.
        const bool pasted_after = i + 1 < count && i + 1 != va_opt_end && items[i + 1].kind == PP_ITEM_PASTE;
        switch (item->kind)
        {
            case PP_ITEM_TOKEN:
                if (pp->vectorized)
                {
                    // The body tokens up to the next item of another kind (or the end of a __VA_OPT__'s content) are
                    // consecutive tokens of the definition: one copy.
                    uint32_t last = i;
                    while (last + 1 < count && last + 1 != va_opt_end && items[last + 1].kind == PP_ITEM_TOKEN && items[last + 1].token == items[last].token + 1)
                    {
                        last += 1;
                    }
                    pp_append_file_tokens(pp, out, &pp->datas[macro->data], macro->base, item->token, items[last].token + 1, true);
                    i = last;
                }
                else
                {
                    pp_push_token(pp, out, pp_body_token(pp, macro, item));
                }
                break;
            case PP_ITEM_PARAMETER:
            {
                PPTokens* tokens;
                uint32_t first;
                uint32_t last;
                pp_argument_range(pp, invocation, item->parameter, !pasted_after, &tokens, &first, &last);
                if (first != last)
                {
                    pp_append_tokens(pp, tokens, first, last, true, item->flags);
                }
                else
                {
                    // An empty argument: a placemarker next to ##, or at an end of a __VA_OPT__'s content next to ## outside
                    // it; else its leading space goes to the next token.
                    const bool edge = va_opt_end != BUSTER_PP_NONE && ((i == va_opt_first && va_opt_paste_left) || (i + 1 == va_opt_end && va_opt_pasted_after));
                    if (pasted_after || edge)
                    {
                        pp_push_token(pp, out, (PPToken){ .kind = BUSTER_PP_KIND_PLACEMARKER, .flags = item->flags });
                        placemarkers = true;
                    }
                    else if (item->flags & BUSTER_PP_SPACE)
                    {
                        pp_pend_space(out, &space_at);
                    }
                }
                break;
            }
            case PP_ITEM_STRINGIZE:
            {
                PPTokens* tokens;
                uint32_t first;
                uint32_t last;
                pp_argument_range(pp, invocation, item->parameter, false, &tokens, &first, &last);
                pp_push_token(pp, out, pp_stringize(pp, tokens, first, last, item->flags, pp->history_anchor));
                break;
            }
            case PP_ITEM_VA_OPT:
            case PP_ITEM_STRINGIZE_VA_OPT:
            {
                const uint32_t content_end = i + 1 + item->parameter;
                const bool content_pasted_after = content_end < count && items[content_end].kind == PP_ITEM_PASTE;
                // The first token after __VA_OPT__ (its content's first, or the next) takes its leading space, as in clang.
                if (item->kind == PP_ITEM_VA_OPT && (item->flags & BUSTER_PP_SPACE))
                {
                    pp_pend_space(out, &space_at);
                }
                if (variadic_present)
                {
                    va_opt_end = content_end;
                    va_opt_first = i + 1;
                    va_opt_begin = out->count;
                    va_opt_flags = item->flags;
                    va_opt_string_location = pp->history_anchor;
                    va_opt_pasted_after = content_pasted_after;
                    va_opt_mark = item->kind == PP_ITEM_STRINGIZE_VA_OPT ? out->count : BUSTER_PP_NONE;
                    // A space pending before #__VA_OPT__ goes to its string, as clang saves it there, not to the
                    // content's first token, whose leading space a string drops.
                    va_opt_string_space = va_opt_mark != BUSTER_PP_NONE && space_at == out->count;
                    space_at = va_opt_string_space ? BUSTER_PP_NONE : space_at;
                }
                else
                {
                    if (item->kind == PP_ITEM_STRINGIZE_VA_OPT)
                    {
                        pp_push_token(pp, out, pp_stringize(pp, out, out->count, out->count, item->flags, pp->history_anchor));
                        if (va_opt_paste_left)
                        {
                            pp->history_anchor = va_opt_paste_location;
                            pp->history_spelling = va_opt_paste_spelling;
                            pp_paste_in_place(pp, out, out->count - 1, &space_at);
                        }
                    }
                    else if (content_pasted_after && !va_opt_paste_left)
                    {
                        pp_push_token(pp, out, (PPToken){ .kind = BUSTER_PP_KIND_PLACEMARKER, .flags = item->flags });
                        placemarkers = true;
                    }
                    va_opt_paste_left = false;
                    i += item->parameter;
                }
                break;
            }
            case PP_ITEM_PASTE:
            {
                // The left operand is the last token so far (a placemarker for an empty argument); the right one is the
                // next item's first token, whose other tokens follow the result.
                const PPItem* next = &items[i + 1];
                PPToken left = pp_stack_token(out, out->count - 1);
                const bool gnu_comma = variadic && left.kind == C_TOKEN_COMMA && next->kind == PP_ITEM_PARAMETER && next->parameter == variadic_index &&
                                       items[i - 1].kind == PP_ITEM_TOKEN;
                PPTokens* tokens = out;
                uint32_t first = 0;
                uint32_t last = 0;
                PPToken right_token;
                bool right_single = false;
                if (next->kind == PP_ITEM_PARAMETER)
                {
                    pp_argument_range(pp, invocation, next->parameter, false, &tokens, &first, &last);
                }
                else if (next->kind == PP_ITEM_STRINGIZE)
                {
                    PPTokens* argument_tokens;
                    uint32_t argument_first;
                    uint32_t argument_last;
                    pp_argument_range(pp, invocation, next->parameter, false, &argument_tokens, &argument_first, &argument_last);
                    right_token = pp_stringize(pp, argument_tokens, argument_first, argument_last, next->flags, macro->base + pp->datas[macro->data].starts[next->token]);
                    right_single = true;
                }
                else if (next->kind == PP_ITEM_TOKEN)
                {
                    right_token = pp_body_token(pp, macro, next);
                    right_single = true;
                }
                if (gnu_comma)
                {
                    // `, ## __VA_ARGS__`: no paste. An omitted variadic argument drops the comma; any other is placed
                    // after it unexpanded.
                    if (invocation->variadic_omitted)
                    {
                        out->count -= 1;
                    }
                    else
                    {
                        pp->history_anchor = macro->base + pp->datas[macro->data].starts[next->token];
                        pp_append_tokens(pp, tokens, first, last, true, next->flags);
                    }
                    i += 1;
                }
                else if (next->kind == PP_ITEM_VA_OPT || next->kind == PP_ITEM_STRINGIZE_VA_OPT)
                {
                    // `## __VA_OPT__(...)`: the __VA_OPT__ pastes its first token onto the left operand when its tokens
                    // are known (above).
                    va_opt_paste_left = true;
                    va_opt_paste_location = pp->history_anchor;
                    va_opt_paste_spelling = pp->history_spelling;
                }
                else if (next->kind == PP_ITEM_PASTE)
                {
                }
                else
                {
                    const bool right_empty = !right_single && first == last;
                    if (right_empty)
                    {
                        // X ## placemarker is X.
                    }
                    else
                    {
                        const PPToken right = right_single ? right_token : pp_stack_token(tokens, first);
                        if (left.kind == BUSTER_PP_KIND_PLACEMARKER)
                        {
                            out->count -= 1;
                            PPToken moved = right;
                            moved.flags = (uint8_t)((moved.flags & ~(BUSTER_PP_SPACE | BUSTER_PP_START_OF_LINE)) | (left.flags & BUSTER_PP_SPACE) | BUSTER_PP_FROM_MACRO);
                            if (!right_single)
                            {
                                moved.origin = pp_history_substitution(pp, right.origin, macro->base + pp->datas[macro->data].starts[next->token]);
                            }
                            pp_push_token(pp, out, moved);
                        }
                        else
                        {
                            PPToken pasted;
                            if (pp_paste(pp, left, right, &pasted))
                            {
                                out->count -= 1;
                                pp_push_token(pp, out, pasted);
                            }
                            else
                            {
                                PPToken kept = right;
                                kept.flags = (uint8_t)((kept.flags & ~BUSTER_PP_START_OF_LINE) | BUSTER_PP_FROM_MACRO);
                                if (!right_single)
                                {
                                    kept.origin = pp_history_substitution(pp, right.origin, macro->base + pp->datas[macro->data].starts[next->token]);
                                }
                                pp_push_token(pp, out, kept);
                            }
                        }
                        if (!right_single && last - first > 1)
                        {
                            pp->history_anchor = macro->base + pp->datas[macro->data].starts[next->token];
                            pp_append_tokens(pp, tokens, first + 1, last, false, 0);
                        }
                    }
                    i += 1;
                }
                break;
            }
        }
    }
    // The pending space: the token that came after it takes it, or the token after the expansion does.
    *trailing = space_at != BUSTER_PP_NONE && space_at >= out->count ? BUSTER_PP_SPACE : 0;
    if (space_at != BUSTER_PP_NONE && space_at < out->count)
    {
        out->flags[space_at] |= BUSTER_PP_SPACE;
    }
    // Remove the placemarkers.
    if (placemarkers)
    {
        uint32_t kept = *begin;
        for (uint32_t i = *begin; i < out->count; i += 1)
        {
            if (out->kinds[i] != BUSTER_PP_KIND_PLACEMARKER)
            {
                pp_tokens_set(out, kept, out->kinds[i], out->flags[i], out->values[i], out->locations[i]);
                if (out->origins != 0)
                {
                    out->origins[kept] = out->origins[i];
                }
                kept += 1;
            }
        }
        out->count = kept;
    }
    pp->history_current = saved_history;
}

// ---------------------------------------------------------------------------------------------------------------------
// Invocations

static void pp_finish_invocation(Preprocessor* pp);

// Does an argument name an enabled macro that isn't painted? Then expanding it can change it. (A name of a disabled
// macro is painted when the expansion is rescanned anyway, since the macros disabled now still are then.)
static bool pp_needs_expansion(const Preprocessor* pp, const PPTokens* tokens, uint32_t begin, uint32_t end)
{
    bool result = false;
    for (uint32_t i = begin; !result && i < end; i += 1)
    {
        const IdentId id = tokens->values[i];
        result = tokens->kinds[i] == C_TOKEN_RAW_IDENTIFIER && !(tokens->flags[i] & BUSTER_PP_NO_EXPAND) && pp_is_macro(pp, id) &&
                 pp->macros[pp->macro_of[id]].disabled == 0;
    }
    return result;
}

static bool pp_push_capture(Preprocessor* pp, uint8_t kind, uint32_t invocation)
{
    const bool result = pp->capture_count < BUSTER_PP_MAX_CONTEXTS;
    if (result)
    {
        pp->capture_frames[pp->capture_count] = (PPCapture){ .kind = kind, .begin = pp->captures.count, .invocation = invocation };
        pp->capture_count += 1;
    }
    else
    {
        pp_diagnose(pp, PP_SEVERITY_FATAL, 0, "macro expansion nests too deeply");
    }
    return result;
}

// Expands tokens [begin, end) of pp->arguments as a line of their own (a directive's, or a builtin's operand) without
// running the main loop here: pushes `capture` and a barrier context over the tokens, and returns to the main loop,
// which expands them into the capture and, when the barrier ends, runs the capture's continuation (pp_finish_capture).
// So a directive in a macro's arguments, a #if line's builtins and their operands nest in explicit stacks, never in
// calls. Returns false (after a fatal error) if a stack is full.
static bool pp_start_capture(Preprocessor* pp, PPCapture capture, uint32_t begin, uint32_t end, uint32_t expansion_end)
{
    bool result = pp->capture_count < BUSTER_PP_MAX_CONTEXTS;
    if (result)
    {
        capture.begin = pp->captures.count;
        pp->capture_frames[pp->capture_count] = capture;
        pp->capture_count += 1;
        result = pp_push_buffer(pp, &pp->arguments, begin, end, 0, expansion_end, true, false) != 0;
        pp->capture_count -= !result;
    }
    else
    {
        pp_diagnose(pp, PP_SEVERITY_FATAL, 0, "macro expansion nests too deeply");
    }
    return result;
}

// Expands the top invocation's next arguments that need it, one at a time: each becomes a barrier context with a capture,
// which the main loop runs (pp_finish_argument takes it from there). Once none is left, substitutes.
static void pp_continue_invocation(Preprocessor* pp)
{
    const uint32_t index = pp->invocation_count - 1;
    PPInvocation* invocation = &pp->invocations[index];
    const PPMacro* macro = &pp->macros[invocation->macro];
    bool waiting = false;
    while (!waiting && invocation->next < invocation->argument_count && !pp->fatal)
    {
        PPArgument* argument = &pp->argument_records[invocation->arguments + invocation->next];
        const uint32_t parameter = invocation->next;
        const bool expanded = (macro->expanded_parameters >> (parameter < 63 ? parameter : 63)) & 1;
        if (expanded && pp_needs_expansion(pp, &pp->arguments, argument->raw_begin, argument->raw_end))
        {
            pp->statistics.expanded_arguments += pp->options.timing;
            waiting = pp_push_capture(pp, BUSTER_PP_CAPTURE_ARGUMENT, index) &&
                      pp_push_buffer(pp, &pp->arguments, argument->raw_begin, argument->raw_end, 0, invocation->expansion_end, true, false) != 0;
        }
        else
        {
            argument->expanded_begin = argument->raw_begin;
            argument->expanded_end = argument->raw_end;
            argument->expanded_captured = false;
            invocation->next += 1;
        }
    }
    if (!waiting && !pp->fatal)
    {
        pp_finish_invocation(pp);
    }
}

// The barrier of an argument's capture ended: the capture is the expanded argument.
static void pp_finish_argument(Preprocessor* pp)
{
    const PPCapture capture = pp->capture_frames[pp->capture_count - 1];
    pp->capture_count -= 1;
    PPInvocation* invocation = &pp->invocations[capture.invocation];
    PPArgument* argument = &pp->argument_records[invocation->arguments + invocation->next];
    argument->expanded_begin = capture.begin;
    argument->expanded_end = pp->captures.count;
    argument->expanded_captured = true;
    invocation->next += 1;
    pp_continue_invocation(pp);
}

// Substitutes the top invocation, frees its arguments and pushes its expansion, with its macro disabled.
static void pp_finish_invocation(Preprocessor* pp)
{
    const PPInvocation invocation = pp->invocations[pp->invocation_count - 1];
    uint32_t begin;
    uint8_t trailing;
    pp_substitute(pp, &invocation, &begin, &trailing);
    const uint32_t end = pp->expansions.count;
    const uint8_t name_flags = invocation.name_flags & (BUSTER_PP_SPACE | BUSTER_PP_START_OF_LINE);
    if (begin < end)
    {
        uint8_t* flags = &pp->expansions.flags[begin];
        *flags = (uint8_t)((*flags & ~(BUSTER_PP_SPACE | BUSTER_PP_START_OF_LINE)) | name_flags);
    }
    pp->captures.count = invocation.captures_begin;
    pp->arguments.count = invocation.arguments_begin;
    pp->argument_record_count = invocation.arguments;
    pp->invocation_count -= 1;
    if (begin < end)
    {
        PPContext* context = pp_push_buffer(pp, &pp->expansions, begin, end, invocation.macro, invocation.expansion_end, false, true);
        if (context != 0)
        {
            context->trailing_flags = trailing;
        }
    }
    else
    {
        // An empty expansion: the next token read after the invocation's ')' takes its name's flags.
        pp->contexts[pp->context_count - 1].pending_flags |= name_flags | trailing;
    }
}

static bool pp_new_argument(Preprocessor* pp)
{
    const bool result = pp->argument_record_count < BUSTER_PP_MAX_STACK_TOKENS;
    if (result)
    {
        const uint32_t at = pp->arguments.count;
        pp->argument_records[pp->argument_record_count] = (PPArgument){ .raw_begin = at, .raw_end = at };
        pp->argument_record_count += 1;
    }
    else
    {
        pp_diagnose(pp, PP_SEVERITY_FATAL, 0, "too many macro arguments");
    }
    return result;
}

// The most commas between arguments pp_collect_arguments notes: an invocation with more arguments is read a token at a time.
#define BUSTER_PP_FAST_SEPARATORS 64
// The deepest nesting of parentheses pp_collect_arguments' 8-bit depths hold.
#define BUSTER_PP_FAST_DEPTH 63

// The byte indices 0 to 63, for pp_prefix_sum_bytes' permutes.
static const uint8_t pp_byte_indices[64] __attribute__((aligned(64))) = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63 };

// The inclusive prefix sums of 64 signed bytes, wrapping at 8 bits: byte i becomes the sum of bytes 0 to i. Six steps
// of a permute that moves the bytes up by 1, 2, 4, 8, 16 and 32 places, zeroing the places it shifts in (vpermb: one
// uop on Zen 5, latency 4), and an add.
BUSTER_INLINE __m512i pp_prefix_sum_bytes(__m512i x)
{
    const __m512i indices = _mm512_load_si512(pp_byte_indices);
    for (uint32_t shift = 1; shift < 64; shift *= 2)
    {
        const __m512i from = _mm512_sub_epi8(indices, _mm512_set1_epi8((char)shift));
        x = _mm512_add_epi8(x, _mm512_maskz_permutexvar_epi8(~0ull << shift, from, x));
    }
    return x;
}

// Collects an invocation's arguments when its '(' and ')' are both in the top context's run, as most are (an invocation
// in a file's text, or in one macro's expansion), without a branch per token. For 64 kinds at a time: the depth of
// parentheses after each token is a prefix sum of +1 at '(' and -1 at ')' (pp_prefix_sum_bytes) on the depth before
// them; the invocation's ')' is the first token after which the depth is -1 (a compare and a tzcnt); and the commas
// between its arguments are the commas at depth 0 before it (a variadic macro's last argument keeps the commas past its
// other parameters': pdep keeps the first ones). Then the tokens between the parentheses go to pp->arguments in one
// copy (the commas between arguments with them, outside every argument's range) and the arguments' records are set,
// the first of which pp_invoke made. Returns false, having changed nothing, otherwise: pp_invoke then reads the tokens
// one at a time, through the contexts below and any directive on the way, as the scalar kernels always do.
static bool pp_collect_arguments(Preprocessor* pp, PPInvocation* invocation, bool variadic, uint32_t parameter_count)
{
    PPContext* context = &pp->contexts[pp->context_count - 1];
    const bool file = context->kind == BUSTER_PP_CONTEXT_FILE;
    const PPFileData* data = file ? &pp->datas[pp->files[context->file].data] : 0;
    const uint8_t* kinds = file ? data->kinds : context->tokens->kinds;
    const uint32_t limit = file ? pp_run_limit(data, context) : context->end;
    const uint32_t open = context->position;
    uint32_t separators[BUSTER_PP_FAST_SEPARATORS + 64];
    uint32_t separator_count = 0;
    uint32_t close = BUSTER_PP_NONE;
    bool overflow = false;
    if (open < limit && (kinds[open] & 0x7F) == C_TOKEN_L_PAREN)
    {
        // The depth before the block's first token, from 0 inside the invocation's '('.
        uint32_t depth = 0;
        for (uint32_t t = open + 1; close == BUSTER_PP_NONE && !overflow && t < limit; t += 64)
        {
            const __mmask64 valid = _bzhi_u64(~0ull, limit - t < 64 ? limit - t : 64);
            const __m512i plain = _mm512_and_si512(_mm512_maskz_loadu_epi8(valid, kinds + t), _mm512_set1_epi8(0x7F));
            const uint64_t opens = _mm512_mask_cmpeq_epi8_mask(valid, plain, _mm512_set1_epi8(C_TOKEN_L_PAREN));
            const uint64_t closes = _mm512_mask_cmpeq_epi8_mask(valid, plain, _mm512_set1_epi8(C_TOKEN_R_PAREN));
            const uint64_t commas = _mm512_mask_cmpeq_epi8_mask(valid, plain, _mm512_set1_epi8(C_TOKEN_COMMA));
            const __m512i steps = _mm512_mask_mov_epi8(_mm512_maskz_mov_epi8(opens, _mm512_set1_epi8(1)), closes, _mm512_set1_epi8(-1));
            const __m512i depths = _mm512_add_epi8(pp_prefix_sum_bytes(steps), _mm512_set1_epi8((char)depth));
            const uint64_t closing = _mm512_mask_cmpeq_epi8_mask(valid, depths, _mm512_set1_epi8(-1));
            const uint64_t before = closing != 0 ? _blsmsk_u64(closing) >> 1 : ~0ull;
            uint64_t top = commas & before & _mm512_cmpeq_epi8_mask(depths, _mm512_setzero_si512());
            if (variadic)
            {
                const uint32_t room = parameter_count - 1 > separator_count ? parameter_count - 1 - separator_count : 0;
                top = _pdep_u64(_bzhi_u64(~0ull, room), top);
            }
            // A loop per argument, not per token.
            overflow = separator_count + (uint32_t)__builtin_popcountll(top) > BUSTER_PP_FAST_SEPARATORS;
            for (uint64_t bits = overflow ? 0 : top; bits != 0; bits &= bits - 1)
            {
                separators[separator_count] = t + (uint32_t)__builtin_ctzll(bits);
                separator_count += 1;
            }
            close = closing != 0 ? t + (uint32_t)__builtin_ctzll(closing) : BUSTER_PP_NONE;
            depth += (uint32_t)__builtin_popcountll(opens) - (uint32_t)__builtin_popcountll(closes);
            // Past BUSTER_PP_FAST_DEPTH, the next block's 8-bit depths could wrap.
            overflow |= close == BUSTER_PP_NONE && depth > BUSTER_PP_FAST_DEPTH;
        }
    }
    const bool result = close != BUSTER_PP_NONE && !overflow && pp->argument_record_count + separator_count < BUSTER_PP_MAX_STACK_TOKENS &&
                        pp_room(pp, &pp->arguments, close - open);
    if (result)
    {
        // Token t of the context goes to pp->arguments at `at` + t - (open + 1).
        const uint32_t at = pp->arguments.count;
        if (file)
        {
            const uint32_t base = pp->ranges[context->range].base;
            pp_append_file_tokens(pp, &pp->arguments, data, base, open + 1, close, false);
            invocation->expansion_end = base + data->starts[close];
        }
        else
        {
            const PPTokens* tokens = context->tokens;
            pp_append_stack_tokens(pp, &pp->arguments, tokens, open + 1, close);
            invocation->expansion_end = (tokens->flags[close] & BUSTER_PP_FROM_MACRO) ? context->expansion_end : tokens->locations[close];
        }
        PPArgument* records = pp->argument_records;
        uint32_t record = pp->argument_record_count - 1;
        for (uint32_t i = 0; i < separator_count; i += 1)
        {
            records[record].raw_end = at + separators[i] - (open + 1);
            record += 1;
            records[record] = (PPArgument){ .raw_begin = at + separators[i] - open, .raw_end = at + separators[i] - open };
        }
        records[record].raw_end = at + close - (open + 1);
        if (invocation->history != 0)
        {
            pp->history[invocation->history - 1].end = file ? pp->ranges[context->range].base + data->starts[close] : context->tokens->locations[close];
            pp->history[invocation->history - 1].end_origin = !file && context->tokens->origins != 0 ? context->tokens->origins[close] : 0;
        }
        pp->argument_record_count = record + 1;
        context->position = close + 1;
    }
    return result;
}

static void pp_close_builtin(Preprocessor* pp, bool closed);

// Ends the top invocation's collection (`closed`: its ')' was read). A macro's: checks its arguments' count and starts
// expanding them (pp_continue_invocation); an unterminated or invalid invocation is dropped with its tokens. A
// builtin's operand goes to pp_close_builtin.
static void pp_close_invocation(Preprocessor* pp, bool closed)
{
    PPInvocation* invocation = &pp->invocations[pp->invocation_count - 1];
    const uint32_t saved_origin = pp->diagnostic_origin;
    pp->diagnostic_origin = invocation->history != 0 ? pp->history[invocation->history - 1].parent : 0;
    invocation->collecting = false;
    if (invocation->builtin)
    {
        pp_close_builtin(pp, closed);
    }
    else
    {
        const PPMacro* macro = &pp->macros[invocation->macro];
        const bool variadic = (macro->flags & BUSTER_PP_MACRO_VARIADIC) != 0;
        const uint32_t parameter_count = macro->parameter_count;
        uint32_t count = pp->argument_record_count - invocation->arguments;
        bool valid = closed;
        if (!closed && !pp->fatal)
        {
            pp_diagnose(pp, PP_SEVERITY_ERROR, invocation->name_location, "unterminated function-like macro invocation");
        }
        if (valid)
        {
            // f() has no argument when f has no parameter, and one empty argument when it has one.
            const PPArgument* first = &pp->argument_records[invocation->arguments];
            if (parameter_count == 0 && count == 1 && first->raw_begin == first->raw_end)
            {
                count = 0;
            }
            // A variadic macro's variadic argument may be omitted altogether.
            if (variadic && count + 1 == parameter_count)
            {
                invocation->variadic_omitted = true;
                valid = pp_new_argument(pp);
                count += 1;
            }
            // So is it in F() when the variadic parameter is F's only one. GNU's `, ## __VA_ARGS__` then drops its comma,
            // except in C99 and later without GNU extensions, as clang has it.
            else if (variadic && parameter_count == 1)
            {
                invocation->variadic_omitted = count == 1 && first->raw_begin == first->raw_end && (pp->options.language.gnu || pp->options.language.standard < 1999);
            }
            if (valid && count != parameter_count)
            {
                uint32_t where = invocation->name_location;
                if (count < parameter_count && invocation->history != 0)
                {
                    where = pp->history[invocation->history - 1].end;
                    pp->diagnostic_origin = pp->history[invocation->history - 1].end_origin;
                }
                else if (count > parameter_count)
                {
                    const uint32_t first = pp->argument_records[invocation->arguments + parameter_count].raw_begin;
                    if (first < pp->arguments.count)
                    {
                        where = pp->arguments.locations[first];
                        pp->diagnostic_origin = pp->arguments.origins != 0 ? pp->arguments.origins[first] : 0;
                    }
                }
                pp_diagnose(pp, PP_SEVERITY_ERROR, where, "too %s arguments provided to function-like macro invocation",
                            count > parameter_count ? "many" : "few");
                valid = false;
            }
        }
        if (!valid && !pp->fatal)
        {
            FILE* stream = pp->options.diagnostics != 0 ? pp->options.diagnostics : stderr;
            pp_diagnostic_place(pp, stream, macro->location);
            fprintf(stream, "note: macro '%.*s' defined here\n", (int)intern_length(pp->table, macro->name), intern_bytes(pp->table, macro->name));
        }
        pp->diagnostic_origin = saved_origin;
        invocation->argument_count = count;
        if (pp->options.timing)
        {
            pp->statistics.invocations += 1;
            pp->statistics.arguments += count;
        }
        if (valid && (macro->flags & BUSTER_PP_MACRO_COMPILED))
        {
            pp_continue_invocation(pp);
        }
        else
        {
            pp->arguments.count = invocation->arguments_begin;
            pp->argument_record_count = invocation->arguments;
            pp->invocation_count -= 1;
        }
    }
    pp->diagnostic_origin = saved_origin;
}

// What a collected token does: nothing more, close the invocation, or (a builtin's first token that isn't '(') end it.
typedef enum PPCollected
{
    PP_COLLECTED_MORE,
    PP_COLLECTED_CLOSED,
    PP_COLLECTED_INVALID,
} PPCollected;

// Adds one token to the collecting invocation (C 6.10.4: commas separate arguments only outside of nested parentheses,
// and not in the variadic argument; a builtin's operand isn't split).
static PPCollected pp_collect_token(Preprocessor* pp, PPInvocation* invocation, PPToken token, uint32_t expansion_end)
{
    PPCollected result = PP_COLLECTED_MORE;
    const PPMacro* macro = &pp->macros[invocation->macro];
    const bool variadic = !invocation->builtin && (macro->flags & BUSTER_PP_MACRO_VARIADIC) != 0;
    const uint32_t current = pp->argument_record_count - 1 - invocation->arguments;
    if (!invocation->opened)
    {
        invocation->opened = token.kind == C_TOKEN_L_PAREN;
        result = invocation->opened ? PP_COLLECTED_MORE : PP_COLLECTED_INVALID;
    }
    else if (token.kind == C_TOKEN_R_PAREN && invocation->depth == 0)
    {
        invocation->expansion_end = expansion_end;
        if (invocation->history != 0)
        {
            pp->history[invocation->history - 1].end = token.location;
            pp->history[invocation->history - 1].end_origin = token.origin;
        }
        result = PP_COLLECTED_CLOSED;
    }
    else if (!invocation->builtin && token.kind == C_TOKEN_COMMA && invocation->depth == 0 && !(variadic && current + 1 >= macro->parameter_count))
    {
        result = pp_new_argument(pp) ? PP_COLLECTED_MORE : PP_COLLECTED_INVALID;
    }
    else
    {
        invocation->depth += token.kind == C_TOKEN_L_PAREN;
        invocation->depth -= token.kind == C_TOKEN_R_PAREN;
        result = pp_push_token(pp, &pp->arguments, token) ? PP_COLLECTED_MORE : PP_COLLECTED_INVALID;
        pp->argument_records[pp->argument_record_count - 1].raw_end = pp->arguments.count;
    }
    return result;
}

// Is the main loop collecting the top invocation's tokens? Not while a capture started among them (a directive's line)
// is being expanded.
BUSTER_INLINE bool pp_collecting(const Preprocessor* pp)
{
    return pp->invocation_count != 0 && pp->invocations[pp->invocation_count - 1].collecting && pp->capture_count == pp->invocations[pp->invocation_count - 1].capture_mark;
}

// The directives clang rejects among a function-like macro's arguments (Preprocessor::HandleDirective's
// err_embedded_directive): the inclusions, #pragma and #embed. Their names, by kind, or null for the others.
static const char* pp_embedded_directive_name(uint8_t kind)
{
    const char* result = 0;
    switch (kind)
    {
        case PP_DIRECTIVE_INCLUDE: result = "include"; break;
        case PP_DIRECTIVE_INCLUDE_NEXT: result = "include_next"; break;
        case PP_DIRECTIVE_IMPORT: result = "import"; break;
        case PP_DIRECTIVE_PRAGMA: result = "pragma"; break;
        case PP_DIRECTIVE_EMBED: result = "embed"; break;
        default: break;
    }
    return result;
}

// One step of the main loop while it collects the top invocation's arguments (or a builtin's operand) a token at a time:
// the top context's tokens up to its end or its next directive. An exhausted buffer is popped (which enables its macro
// again); a directive runs, as clang runs one among a macro's arguments (its line may start a capture, during which the
// collection waits), but for those clang rejects there (pp_embedded_directive_name), which are an error and skipped;
// the end of a file or of a barrier leaves the invocation unterminated.
static void pp_collect_step(Preprocessor* pp)
{
    PPInvocation* invocation = &pp->invocations[pp->invocation_count - 1];
    PPContext* context = &pp->contexts[pp->context_count - 1];
    PPCollected collected = PP_COLLECTED_MORE;
    bool unterminated = false;
    if (context->kind == BUSTER_PP_CONTEXT_FILE)
    {
        const PPFileData* data = &pp->datas[pp->files[context->file].data];
        const uint32_t base = pp->ranges[context->range].base;
        const uint32_t limit = pp_run_limit(data, context);
        if (context->position < limit)
        {
            while (collected == PP_COLLECTED_MORE && context->position < limit)
            {
                const PPToken token = pp_file_token(data, base, context->position);
                context->position += 1;
                collected = pp_collect_token(pp, invocation, token, token.location);
            }
        }
        else if (context->next_directive < data->directive_count)
        {
            const uint32_t d = context->next_directive;
            const char* rejected = invocation->builtin ? 0 : pp_embedded_directive_name(data->directive_kind[d]);
            if (rejected != 0)
            {
                // At the directive's name, as clang reports it.
                pp_diagnose(pp, PP_SEVERITY_ERROR, base + data->starts[data->directive_hash[d] + 1], "embedding a #%s directive within macro arguments is not supported",
                            rejected);
                context->position = data->directive_end[d];
                context->next_directive = d + 1;
            }
            else
            {
                // Undefined behavior that clang allows, and warns about only with -pedantic (-Wembedded-directive).
                pp_directive(pp, context);
            }
        }
        else
        {
            unterminated = true;
        }
    }
    else if (context->position < context->end)
    {
        while (collected == PP_COLLECTED_MORE && context->position < context->end)
        {
            const PPToken token = pp_context_token(pp, context, context->position);
            const uint32_t expansion_end = pp_expansion_end(context, token);
            context->position += 1;
            collected = pp_collect_token(pp, invocation, token, expansion_end);
        }
    }
    else if (context->barrier)
    {
        unterminated = true;
    }
    else
    {
        pp_pop_context(pp);
    }
    if (collected != PP_COLLECTED_MORE || unterminated)
    {
        pp_close_invocation(pp, collected == PP_COLLECTED_CLOSED);
    }
}

// Invokes a function-like macro whose name was just read and whose '(' is next: collects its arguments, then expands
// and substitutes them. When they are all in the top context, pp_collect_arguments collects them at once; else the main
// loop collects them a token at a time (pp_collect_step), as it does for the scalar kernels.
static void pp_invoke(Preprocessor* pp, PPMacroId id, PPToken name)
{
    PPMacro* macro = &pp->macros[id];
    if (!(macro->flags & BUSTER_PP_MACRO_COMPILED))
    {
        pp_compile_macro(pp, id);
    }
    const bool variadic = (macro->flags & BUSTER_PP_MACRO_VARIADIC) != 0;
    PPInvocation invocation = {
        .macro = id,
        .history = pp_history_expansion(pp, id, name.location, name.origin),
        .arguments = pp->argument_record_count,
        .arguments_begin = pp->arguments.count,
        .captures_begin = pp->captures.count,
        .name_location = name.location,
        .name_flags = name.flags,
        .opened = true,
    };
    bool valid = pp->invocation_count < BUSTER_PP_MAX_CONTEXTS;
    if (!valid)
    {
        pp_diagnose(pp, PP_SEVERITY_FATAL, name.location, "macro expansion nests too deeply");
    }
    valid = valid && pp_new_argument(pp);
    const bool collected = valid && pp->vectorized && pp_collect_arguments(pp, &invocation, variadic, macro->parameter_count);
    if (valid && !collected)
    {
        PPToken token;
        uint32_t expansion_end;
        pp_read_token(pp, &token, &expansion_end); // The '(' pp_next_is_lparen saw.
        invocation.collecting = true;
        invocation.capture_mark = pp->capture_count;
    }
    if (valid)
    {
        pp->invocations[pp->invocation_count] = invocation;
        pp->invocation_count += 1;
    }
    if (collected)
    {
        pp_close_invocation(pp, true);
    }
}

// Expands an object-like macro whose name was just read. One whose body names no macro and has no ## goes straight to the
// target (the main loop's commonest expansion: a constant); any other is built in a buffer and pushed as a context.
static void pp_expand_object(Preprocessor* pp, PPMacroId id, PPToken name, uint32_t expansion_end)
{
    const uint32_t saved_history = pp->history_current;
    pp->history_current = pp_history_expansion(pp, id, name.location, name.origin);
    PPMacro* macro = &pp->macros[id];
    const PPFileData* data = &pp->datas[macro->data];
    bool direct = (macro->flags & BUSTER_PP_MACRO_PASTES) == 0;
    for (uint32_t t = macro->body_begin; direct && t < macro->body_end; t += 1)
    {
        direct = (data->kinds[t] & 0x7F) != C_TOKEN_RAW_IDENTIFIER || !pp_is_macro(pp, data->values[t]);
    }
    PPTokens* tokens = direct ? pp_target(pp) : &pp->expansions;
    const uint32_t begin = tokens->count;
    if (macro->flags & BUSTER_PP_MACRO_PASTES)
    {
        if ((macro->flags & BUSTER_PP_MACRO_COMPILED) || pp_compile_macro(pp, id))
        {
            const PPInvocation invocation = { .macro = id, .history = pp->history_current };
            uint32_t substituted;
            uint8_t trailing;
            pp_substitute(pp, &invocation, &substituted, &trailing);
        }
    }
    else if (pp->vectorized)
    {
        pp_append_file_tokens(pp, tokens, data, macro->base, macro->body_begin, macro->body_end, true);
    }
    else if (pp_room(pp, tokens, macro->body_end - macro->body_begin))
    {
        for (uint32_t t = macro->body_begin; t < macro->body_end; t += 1)
        {
            PPToken token = pp_file_token(data, macro->base, t);
            token.flags = (uint8_t)((token.flags & BUSTER_PP_SPACE) | BUSTER_PP_FROM_MACRO);
            pp_tokens_set(tokens, tokens->count, token.kind, token.flags, token.value, token.location);
            tokens->count += 1;
        }
        pp_history_fill(pp, tokens, begin, tokens->count, pp->history_current);
    }
    const bool empty = begin == tokens->count;
    if (!empty)
    {
        uint8_t* flags = &tokens->flags[begin];
        *flags = (uint8_t)((*flags & ~(BUSTER_PP_SPACE | BUSTER_PP_START_OF_LINE)) | (name.flags & (BUSTER_PP_SPACE | BUSTER_PP_START_OF_LINE)));
    }
    else
    {
        // An empty expansion: the next token read from this context takes the name's flags.
        pp->contexts[pp->context_count - 1].pending_flags |= name.flags & (BUSTER_PP_SPACE | BUSTER_PP_START_OF_LINE);
    }
    if (!direct && !empty)
    {
        pp_push_buffer(pp, &pp->expansions, begin, pp->expansions.count, id, expansion_end, false, true);
    }
    if (pp->options.timing)
    {
        pp->statistics.direct_expansions += direct;
        pp->statistics.buffered_expansions += !direct;
    }
    pp->history_current = saved_history;
}

// ---------------------------------------------------------------------------------------------------------------------
// Built-in macros

static void pp_run_contexts(Preprocessor* pp, uint32_t stop_depth);
static void pp_pragma(Preprocessor* pp, const PPTokens* tokens, uint32_t begin, uint32_t end, uint32_t location, bool from_operator);

// A numeric constant spelled in the scratch space.
static PPToken pp_number_token(Preprocessor* pp, uint64_t value, uint8_t flags)
{
    char digits[24];
    const uint32_t length = (uint32_t)snprintf(digits, sizeof(digits), "%lu", (unsigned long)value);
    const uint32_t location = value == 0 ? pp->zero_location : value == 1 ? pp->one_location : pp_scratch_clean(pp, (const uint8_t*)digits, length);
    return (PPToken){ .kind = C_TOKEN_NUMERIC_CONSTANT, .flags = (uint8_t)(flags | BUSTER_PP_FROM_MACRO), .value = location + length, .location = location,
                      .origin = pp->history_current };
}

// A string literal of `bytes`, with quotes and backslashes escaped, spelled in the scratch space.
static PPToken pp_string_token(Preprocessor* pp, const uint8_t* bytes, uint32_t length, uint8_t flags)
{
    Scratch scratch = scratch_begin(0);
    uint8_t* text = arena_push(scratch.arena, 2 * (uint64_t)length + 2, 1);
    PPToken result = { .kind = C_TOKEN_STRING_LITERAL, .flags = (uint8_t)(flags | BUSTER_PP_FROM_MACRO) };
    if (text != 0)
    {
        uint32_t n = 0;
        text[n] = '"';
        n += 1;
        for (uint32_t i = 0; i < length; i += 1)
        {
            // Quotes and backslashes are escaped, and a line break (\n, \r, \r\n or \n\r) is written \n, as clang's
            // Lexer::Stringify writes them.
            const bool line_break = bytes[i] == '\n' || bytes[i] == '\r';
            const bool pair = line_break && i + 1 < length && (bytes[i + 1] == '\n' || bytes[i + 1] == '\r') && bytes[i + 1] != bytes[i];
            if (bytes[i] == '"' || bytes[i] == '\\' || line_break)
            {
                text[n] = '\\';
                n += 1;
            }
            text[n] = line_break ? 'n' : bytes[i];
            n += 1;
            i += pair ? 1 : 0;
        }
        text[n] = '"';
        n += 1;
        result.location = pp_scratch_clean(pp, text, n);
        result.value = result.location + n;
        result.origin = pp->history_current;
    }
    scratch_end(scratch);
    return result;
}

// The presumed line of a location (after any #line), and its presumed file's name.
static uint32_t pp_presumed(Preprocessor* pp, uint32_t location, const char** name, uint32_t* name_length)
{
    const uint32_t range = pp_find_range(pp, location);
    uint32_t result = 1;
    *name = "<scratch space>";
    *name_length = (uint32_t)strlen(*name);
    if (range != BUSTER_PP_NONE && pp->ranges[range].file != BUSTER_PP_NONE)
    {
        uint32_t column;
        result = pp_line_of(pp, range, location, &column);
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

// Is the innermost capture a #if line's?
BUSTER_INLINE bool pp_in_expression(const Preprocessor* pp)
{
    return pp->capture_count != 0 && pp->capture_frames[pp->capture_count - 1].kind == BUSTER_PP_CAPTURE_CONDITION;
}

// Is a #if line being expanded, here or around the macro arguments and builtin operands being expanded in it? (As in
// clang, __has_include may be in a macro's argument there; `defined` is evaluated in the line itself.)
static bool pp_in_condition(const Preprocessor* pp)
{
    uint32_t c = pp->capture_count;
    while (c != 0 && (pp->capture_frames[c - 1].kind == BUSTER_PP_CAPTURE_ARGUMENT || pp->capture_frames[c - 1].kind == BUSTER_PP_CAPTURE_BUILTIN))
    {
        c -= 1;
    }
    return c != 0 && pp->capture_frames[c - 1].kind == BUSTER_PP_CAPTURE_CONDITION;
}

// The innermost file context (for #include_next, __has_include_next and pragmas), or null.
static const PPContext* pp_current_file_context(const Preprocessor* pp)
{
    const PPContext* result = 0;
    for (uint32_t c = pp->context_count; result == 0 && c > 0; c -= 1)
    {
        result = pp->contexts[c - 1].kind == BUSTER_PP_CONTEXT_FILE ? &pp->contexts[c - 1] : 0;
    }
    return result;
}

// The file of the innermost file context, or BUSTER_PP_NONE.
static uint32_t pp_current_file(const Preprocessor* pp)
{
    const PPContext* context = pp_current_file_context(pp);
    return context != 0 ? context->file : BUSTER_PP_NONE;
}

// A header name from tokens [begin, end) of a stack: a string literal's contents, or the spellings between '<' and '>'
// (with a space where whitespace was, as clang joins a header name that macros made). Returns false if they are neither.
static bool pp_header_name(Preprocessor* pp, const PPTokens* tokens, uint32_t begin, uint32_t end, uint8_t* name, uint32_t capacity, uint32_t* length, bool* angled)
{
    bool result = false;
    *length = 0;
    if (begin < end && tokens->kinds[begin] == C_TOKEN_STRING_LITERAL && end - begin == 1)
    {
        // The literal's spelling without its line splices (translation phase 2), as for a direct #include.
        Scratch scratch = scratch_begin(0);
        uint8_t* buffer = arena_push(scratch.arena, (uint64_t)(tokens->values[begin] - tokens->locations[begin]) + 1, 1);
        const uint8_t* spelling = 0;
        const uint32_t spelled = buffer != 0 ? pp_spelling(pp, tokens->kinds[begin], tokens->values[begin], tokens->locations[begin], buffer, &spelling) : 0;
        result = spelled >= 2 && spelled - 2 < capacity;
        if (result)
        {
            memcpy(name, spelling + 1, spelled - 2);
            *length = spelled - 2;
            *angled = false;
        }
        scratch_end(scratch);
    }
    else if (begin < end && tokens->kinds[begin] == C_TOKEN_LESS && tokens->kinds[end - 1] == C_TOKEN_GREATER)
    {
        result = true;
        for (uint32_t i = begin + 1; result && i + 1 < end; i += 1)
        {
            uint8_t buffer[64];
            const uint8_t* spelling;
            const uint32_t spelled = tokens->kinds[i] == C_TOKEN_RAW_IDENTIFIER ? intern_length(pp->table, tokens->values[i]) : tokens->values[i] - tokens->locations[i];
            const bool space = i > begin + 1 && (tokens->flags[i] & BUSTER_PP_SPACE);
            result = spelled < sizeof(buffer) && *length + spelled + 1 < capacity;
            if (result)
            {
                const uint32_t n = pp_spelling(pp, tokens->kinds[i], tokens->values[i], tokens->locations[i], buffer, &spelling);
                if (space)
                {
                    name[*length] = ' ';
                    *length += 1;
                }
                memcpy(name + *length, spelling, n);
                *length += n;
            }
        }
        *angled = true;
    }
    return result;
}

// The answer of a feature query (__has_attribute and the like) about `name` (a scoped one as "scope::name").
static uint32_t pp_feature(const PPOptions* options, PPBuiltin builtin, const uint8_t* name, uint32_t length);

// Builds the name a feature query's argument holds: an identifier, or `scope::identifier`; names with leading and
// trailing "__" lose them for the attribute, feature and extension queries, as clang normalizes them.
static bool pp_feature_name(Preprocessor* pp, PPBuiltin builtin, const PPTokens* tokens, uint32_t begin, uint32_t end, uint8_t* name, uint32_t* length)
{
    bool result = false;
    *length = 0;
    // Attribute, feature and extension names may be written with "__" around them, which clang drops.
    const bool attribute = builtin == PP_BUILTIN_HAS_ATTRIBUTE || builtin == PP_BUILTIN_HAS_C_ATTRIBUTE || builtin == PP_BUILTIN_HAS_CPP_ATTRIBUTE ||
                           builtin == PP_BUILTIN_HAS_DECLSPEC_ATTRIBUTE || builtin == PP_BUILTIN_HAS_FEATURE || builtin == PP_BUILTIN_HAS_EXTENSION;
    const bool scoped = end - begin == 3 && tokens->kinds[begin] == C_TOKEN_RAW_IDENTIFIER && tokens->kinds[begin + 1] == C_TOKEN_COLONCOLON &&
                        tokens->kinds[begin + 2] == C_TOKEN_RAW_IDENTIFIER;
    if ((end - begin == 1 && (tokens->kinds[begin] == C_TOKEN_RAW_IDENTIFIER || (tokens->kinds[begin] >= BUSTER_PP_KIND_KEYWORD && tokens->kinds[begin] < BUSTER_PP_KIND_PRAGMA_END))) || scoped)
    {
        result = true;
        for (uint32_t i = begin; i < end; i += 1)
        {
            if (tokens->kinds[i] == C_TOKEN_COLONCOLON)
            {
                memcpy(name + *length, "::", 2);
                *length += 2;
            }
            else
            {
                const IdentId id = tokens->values[i];
                const uint8_t* bytes = intern_bytes(pp->table, id);
                uint32_t n = intern_length(pp->table, id);
                if (n > 200)
                {
                    n = 200;
                }
                const bool last = i + 1 == end;
                if (attribute && last && n > 4 && bytes[0] == '_' && bytes[1] == '_' && bytes[n - 1] == '_' && bytes[n - 2] == '_')
                {
                    bytes += 2;
                    n -= 4;
                }
                memcpy(name + *length, bytes, n);
                *length += n;
            }
        }
    }
    else if (builtin == PP_BUILTIN_HAS_WARNING && end - begin >= 1 && tokens->kinds[begin] == C_TOKEN_STRING_LITERAL)
    {
        // __has_warning("-Wname"): the string's contents (adjacent strings joined).
        result = true;
        for (uint32_t i = begin; result && i < end; i += 1)
        {
            const uint8_t* bytes = pp_location_bytes(pp, tokens->locations[i]);
            const uint32_t spelled = tokens->values[i] - tokens->locations[i];
            result = tokens->kinds[i] == C_TOKEN_STRING_LITERAL && spelled >= 2 && *length + spelled < 256;
            if (result)
            {
                memcpy(name + *length, bytes + 1, spelled - 2);
                *length += spelled - 2;
            }
        }
    }
    return result;
}

// _Pragma("..."): the string destringized (C 6.10.9), lexed and handled as a #pragma line's tokens.
static void pp_pragma_string(Preprocessor* pp, PPToken string, uint32_t name_location)
{
    Scratch scratch = scratch_begin(0);
    const uint32_t spelled = string.value - string.location;
    uint8_t* buffer = arena_push(scratch.arena, (uint64_t)spelled + 1, 1);
    uint8_t* text = arena_push(scratch.arena, (uint64_t)spelled + 1, 1);
    if (buffer != 0 && text != 0)
    {
        const uint8_t* spelling;
        const uint32_t length = pp_spelling(pp, string.kind, string.value, string.location, buffer, &spelling);
        uint32_t open_quote = 0;
        while (open_quote < length && spelling[open_quote] != '"')
        {
            open_quote += 1;
        }
        uint32_t n = 0;
        for (uint32_t i = open_quote + 1; i + 1 < length; i += 1)
        {
            if (spelling[i] == '\\' && i + 2 < length && (spelling[i + 1] == '"' || spelling[i + 1] == '\\'))
            {
                i += 1;
            }
            text[n] = spelling[i];
            n += 1;
        }
        const uint32_t location = pp_scratch(pp, text, n);
        const uint32_t begin = pp->arguments.count;
        if (pp_lex_scratch(pp, location, n, &pp->arguments, 0) != BUSTER_PP_NONE)
        {
            pp_pragma(pp, &pp->arguments, begin, pp->arguments.count, name_location, true);
        }
        pp->arguments.count = begin;
    }
    scratch_end(scratch);
}

// A feature query's or __has_include's answer, from its operand's tokens [first, last) of `tokens` (expanded, unless
// __has_include's is a header name as written).
static uint64_t pp_builtin_value(Preprocessor* pp, PPBuiltin builtin, const PPTokens* tokens, uint32_t first, uint32_t last, uint32_t location)
{
    uint64_t result = 0;
    const bool include = builtin == PP_BUILTIN_HAS_INCLUDE || builtin == PP_BUILTIN_HAS_INCLUDE_NEXT;
    uint8_t text[512];
    uint32_t length;
    bool angled;
    if (include && !pp_in_condition(pp))
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, location, "__has_include must be used within a preprocessing directive");
    }
    else if (include && pp_header_name(pp, tokens, first, last, text, sizeof(text), &length, &angled))
    {
        const PPContext* includer = pp_current_file_context(pp);
        uint32_t found_in;
        const uint32_t file = pp_find_include(pp, text, length, angled, builtin == PP_BUILTIN_HAS_INCLUDE_NEXT, includer != 0 ? includer->file : BUSTER_PP_NONE,
                                              includer != 0 ? includer->search_index : BUSTER_PP_NONE, &found_in);
        result = file != BUSTER_PP_NONE;
    }
    else if (include)
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, location, "expected \"FILENAME\" or <FILENAME>");
    }
    else if (builtin == PP_BUILTIN_IS_IDENTIFIER && last == first + 1)
    {
        // 1 for an identifier that isn't a keyword in the language mode (a macro's name too: it isn't expanded), and 0
        // for a keyword or another token, as clang answers.
        result = tokens->kinds[first] == C_TOKEN_RAW_IDENTIFIER && !c_name_is_keyword(tokens->values[first], pp->options.language);
    }
    else if (builtin == PP_BUILTIN_IS_IDENTIFIER)
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, location, "__is_identifier takes one token");
    }
    else if (pp_feature_name(pp, builtin, tokens, first, last, text, &length))
    {
        result = pp_feature(&pp->options, builtin, text, length);
        // The target's builtins (x86_target.c) as clang answers them, in the oracle's mode: buster implements none yet (#148).
        if (builtin == PP_BUILTIN_HAS_BUILTIN && result == 0 && pp->options.clang_features)
        {
            result = x86_target_has_builtin(&pp->options.target, text, length);
        }
    }
    else
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, location, "builtin feature check macro requires a parenthesized identifier");
    }
    return result;
}

// Starts a builtin that takes a parenthesized operand (_Pragma, the feature queries, __has_include): the main loop
// collects the operand (pp_collect_step, as an invocation's arguments, from its '(' on), then pp_close_builtin takes it.
static void pp_start_builtin(Preprocessor* pp, PPMacroId id, PPToken name, uint8_t flags, uint32_t expansion_end)
{
    bool valid = pp->invocation_count < BUSTER_PP_MAX_CONTEXTS;
    if (!valid)
    {
        pp_diagnose(pp, PP_SEVERITY_FATAL, name.location, "macro expansion nests too deeply");
    }
    const PPInvocation invocation = {
        .macro = id,
        .history = pp->history_current,
        .arguments = pp->argument_record_count,
        .arguments_begin = pp->arguments.count,
        .captures_begin = pp->captures.count,
        .expansion_end = expansion_end,
        .name_location = name.location,
        .name_flags = flags,
        .collecting = true,
        .builtin = true,
        .capture_mark = pp->capture_count,
    };
    if (valid && pp_new_argument(pp))
    {
        pp->invocations[pp->invocation_count] = invocation;
        pp->invocation_count += 1;
    }
}

// A builtin's operand is collected (`closed`: its ')' was read): _Pragma takes its string literal; attribute queries
// expand their operand through pp_finish_builtin, name queries read it literally, and __has_include does either.
// An unterminated or malformed operand is an error, and a query's answer then 0.
static void pp_close_builtin(Preprocessor* pp, bool closed)
{
    const PPInvocation invocation = pp->invocations[pp->invocation_count - 1];
    const uint32_t saved_history = pp->history_current;
    const uint32_t saved_origin = pp->diagnostic_origin;
    pp->history_current = invocation.history;
    pp->history_anchor = invocation.name_location;
    pp->diagnostic_origin = invocation.history != 0 ? pp->history[invocation.history - 1].parent : 0;
    pp->invocation_count -= 1;
    pp->argument_record_count = invocation.arguments;
    const PPBuiltin builtin = (PPBuiltin)pp->macros[invocation.macro].builtin;
    const uint32_t begin = invocation.arguments_begin;
    const uint32_t end = pp->arguments.count;
    const IdentId name = pp->macros[invocation.macro].name;
    const PPTokens* tokens = &pp->arguments;
    if (builtin == PP_BUILTIN_PRAGMA)
    {
        const bool string = closed && end == begin + 1 && tokens->kinds[begin] >= C_TOKEN_STRING_LITERAL && tokens->kinds[begin] <= C_TOKEN_UTF32_STRING_LITERAL;
        bool started = false;
        if (string)
        {
            pp_pragma_string(pp, pp_stack_token(tokens, begin), invocation.name_location);
        }
        else if (closed && begin < end)
        {
            // Another operand is expanded first, as clang and GCC do (`_Pragma(P)` with P a string literal), and
            // pp_finish_builtin takes the string literal it must give.
            const PPCapture capture = {
                .kind = BUSTER_PP_CAPTURE_BUILTIN,
                .history = invocation.history,
                .arguments_begin = begin,
                .builtin = (uint8_t)builtin,
                .flags = invocation.name_flags,
                .location = invocation.name_location,
            };
            started = pp_start_capture(pp, capture, begin, end, invocation.expansion_end);
        }
        else
        {
            pp_diagnose(pp, PP_SEVERITY_ERROR, invocation.name_location, "_Pragma takes a parenthesized string literal");
        }
        pp->arguments.count = started ? pp->arguments.count : begin;
    }
    else
    {
        const bool include = builtin == PP_BUILTIN_HAS_INCLUDE || builtin == PP_BUILTIN_HAS_INCLUDE_NEXT;
        // Clang reads builtin, feature and target names literally, even when the name is itself a builtin macro or a
        // macro (__has_constexpr_builtin too). Attribute queries may expand their operand. Header names in <...> or a
        // string literal are already complete.
        const bool raw_name = builtin == PP_BUILTIN_HAS_BUILTIN || builtin == PP_BUILTIN_HAS_CONSTEXPR_BUILTIN || builtin == PP_BUILTIN_HAS_FEATURE ||
                              builtin == PP_BUILTIN_HAS_EXTENSION || builtin == PP_BUILTIN_IS_IDENTIFIER ||
                              (builtin >= PP_BUILTIN_IS_TARGET_ARCH && builtin <= PP_BUILTIN_IS_TARGET_ENVIRONMENT);
        const bool direct = (include && begin < end && (tokens->kinds[begin] == C_TOKEN_LESS || tokens->kinds[begin] == C_TOKEN_STRING_LITERAL)) ||
                            raw_name;
        const PPCapture capture = {
            .kind = BUSTER_PP_CAPTURE_BUILTIN,
            .history = invocation.history,
            .arguments_begin = begin,
            .builtin = (uint8_t)builtin,
            .flags = invocation.name_flags,
            .location = invocation.name_location,
        };
        bool started = false;
        uint64_t value = 0;
        if (!closed)
        {
            pp_diagnose(pp, PP_SEVERITY_ERROR, invocation.name_location, "missing '(' or ')' after '%.*s'", (int)intern_length(pp->table, name), intern_bytes(pp->table, name));
        }
        else if (direct)
        {
            value = pp_builtin_value(pp, builtin, tokens, begin, end, invocation.name_location);
        }
        else
        {
            started = pp_start_capture(pp, capture, begin, end, invocation.expansion_end);
        }
        if (!started)
        {
            pp->arguments.count = begin;
            pp_emit(pp, pp_number_token(pp, value, invocation.name_flags));
        }
    }
    pp->history_current = saved_history;
    pp->diagnostic_origin = saved_origin;
}

// The barrier over a feature query's (or __has_include's) operand ended: the capture is the operand expanded.
static void pp_finish_builtin(Preprocessor* pp)
{
    const PPCapture capture = pp->capture_frames[pp->capture_count - 1];
    const uint32_t saved_history = pp->history_current;
    const uint32_t saved_origin = pp->diagnostic_origin;
    pp->history_current = capture.history;
    pp->history_anchor = capture.location;
    pp->diagnostic_origin = capture.history != 0 ? pp->history[capture.history - 1].parent : 0;
    pp->capture_count -= 1;
    if (capture.builtin == PP_BUILTIN_PRAGMA)
    {
        // _Pragma's operand expanded: it must be one string literal.
        const PPTokens* tokens = &pp->captures;
        const bool string = tokens->count == capture.begin + 1 && tokens->kinds[capture.begin] >= C_TOKEN_STRING_LITERAL &&
                            tokens->kinds[capture.begin] <= C_TOKEN_UTF32_STRING_LITERAL;
        const PPToken token = string ? pp_stack_token(tokens, capture.begin) : (PPToken){ 0 };
        pp->captures.count = capture.begin;
        pp->arguments.count = capture.arguments_begin;
        if (string)
        {
            pp_pragma_string(pp, token, capture.location);
        }
        else
        {
            pp_diagnose(pp, PP_SEVERITY_ERROR, capture.location, "_Pragma takes a parenthesized string literal");
        }
    }
    else
    {
        const uint64_t value = pp_builtin_value(pp, (PPBuiltin)capture.builtin, &pp->captures, capture.begin, pp->captures.count, capture.location);
        pp->captures.count = capture.begin;
        pp->arguments.count = capture.arguments_begin;
        pp_emit(pp, pp_number_token(pp, value, capture.flags));
    }
    pp->history_current = saved_history;
    pp->diagnostic_origin = saved_origin;
}

// __TIMESTAMP__'s text for a time: "Sun Jan  1 00:00:00 2000" (strftime's "%a %b %e %T %Y" in the C locale).
static void pp_format_timestamp(char* text, uint32_t capacity, const struct tm* calendar)
{
    static const char months[12][4] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    static const char days[7][4] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    snprintf(text, capacity, "%s %s %2d %02d:%02d:%02d %d", days[calendar->tm_wday % 7], months[calendar->tm_mon % 12], calendar->tm_mday, calendar->tm_hour,
             calendar->tm_min, calendar->tm_sec, calendar->tm_year + 1900);
}

// Expands a builtin macro whose name was just read, from the context it was read in (`expansion_end` from
// pp_expansion_end, for __LINE__ and __FILE__ in a token a macro made).
static void pp_expand_builtin(Preprocessor* pp, PPMacroId id, PPToken name, uint32_t expansion_end)
{
    const uint32_t saved_history = pp->history_current;
    const uint32_t saved_origin = pp->diagnostic_origin;
    pp->history_current = pp_history_expansion(pp, id, name.location, name.origin);
    pp->history_anchor = name.location;
    pp->diagnostic_origin = name.origin;
    const PPBuiltin builtin = (PPBuiltin)pp->macros[id].builtin;
    const uint8_t flags = name.flags & (BUSTER_PP_SPACE | BUSTER_PP_START_OF_LINE);
    const uint32_t where = (name.flags & BUSTER_PP_FROM_MACRO) ? expansion_end : name.location;
    const char* file_name;
    uint32_t file_name_length;
    switch (builtin)
    {
        case PP_BUILTIN_FILE:
        case PP_BUILTIN_FILE_NAME:
        {
            pp_presumed(pp, where, &file_name, &file_name_length);
            uint32_t skip = 0;
            for (uint32_t i = 0; builtin == PP_BUILTIN_FILE_NAME && i < file_name_length; i += 1)
            {
                skip = file_name[i] == '/' ? i + 1 : skip;
            }
            pp_emit(pp, pp_string_token(pp, (const uint8_t*)file_name + skip, file_name_length - skip, flags));
            break;
        }
        case PP_BUILTIN_BASE_FILE:
        {
            // The presumed name at the root of the include stack, walking up from the name as clang does: #line in the
            // main file changes it, and a forced include's root is the <built-in> file that includes it.
            uint32_t at = where;
            uint32_t range = pp_find_range(pp, at);
            while (range != BUSTER_PP_NONE && pp->ranges[range].parent != BUSTER_PP_NONE)
            {
                at = pp->ranges[range].include_location;
                range = pp->ranges[range].parent;
            }
            pp_presumed(pp, at, &file_name, &file_name_length);
            pp_emit(pp, pp_string_token(pp, (const uint8_t*)file_name, file_name_length, flags));
            break;
        }
        case PP_BUILTIN_LINE:
            pp_emit(pp, pp_number_token(pp, pp_presumed(pp, where, &file_name, &file_name_length), flags));
            break;
        case PP_BUILTIN_COUNTER:
            pp_emit(pp, pp_number_token(pp, pp->counter, flags));
            pp->counter += 1;
            break;
        case PP_BUILTIN_INCLUDE_LEVEL:
            pp_emit(pp, pp_number_token(pp, pp->include_depth, flags));
            break;
        case PP_BUILTIN_FLT_EVAL_METHOD:
            // The translation unit's: under -E, clang's `#pragma clang fp eval_method` doesn't change it.
            pp_emit(pp, pp_number_token(pp, pp->flt_eval_method, flags));
            break;
        case PP_BUILTIN_DATE:
            pp_emit(pp, pp_string_token(pp, (const uint8_t*)pp->date, (uint32_t)strlen(pp->date), flags));
            break;
        case PP_BUILTIN_TIME:
            pp_emit(pp, pp_string_token(pp, (const uint8_t*)pp->time, (uint32_t)strlen(pp->time), flags));
            break;
        case PP_BUILTIN_TIMESTAMP:
        {
            // SOURCE_DATE_EPOCH's time when it is set, else the modification time of the file being read (where a
            // macro's expansion is, the file it is expanded in), as clang does: in local time, or ??? for a file the
            // preprocessor made.
            char text[32];
            const PPContext* file_context = pp_current_file_context(pp);
            const PPFileData* data = file_context != 0 ? &pp->datas[pp->files[file_context->file].data] : 0;
            if (pp->has_epoch)
            {
                snprintf(text, sizeof(text), "%s", pp->timestamp);
            }
            else if (data != 0 && data->has_modified)
            {
                const time_t modified = (time_t)data->modified;
                struct tm calendar;
                localtime_r(&modified, &calendar);
                pp_format_timestamp(text, sizeof(text), &calendar);
            }
            else
            {
                snprintf(text, sizeof(text), "??? ??? ?? ??:??:?? ????");
            }
            pp_emit(pp, pp_string_token(pp, (const uint8_t*)text, (uint32_t)strlen(text), flags));
            break;
        }
        case PP_BUILTIN_PRAGMA:
            pp_start_builtin(pp, id, name, flags, expansion_end);
            break;
        case PP_BUILTIN_POISONED:
            pp_diagnose(pp, PP_SEVERITY_ERROR, name.location, "attempt to use a poisoned identifier");
            pp_emit(pp, (PPToken){ .kind = C_TOKEN_RAW_IDENTIFIER, .flags = name.flags | BUSTER_PP_NO_EXPAND, .value = name.value, .location = name.location, .origin = name.origin });
            break;
        case PP_BUILTIN_DEFINED:
            if (!pp_in_expression(pp))
            {
                pp_emit(pp, (PPToken){ .kind = C_TOKEN_RAW_IDENTIFIER, .flags = name.flags | BUSTER_PP_NO_EXPAND, .value = name.value, .location = name.location, .origin = name.origin });
            }
            else
            {
                // `defined X` or `defined ( X )`: X unexpanded.
                PPToken token;
                uint32_t end;
                bool valid = pp_read_token(pp, &token, &end);
                const bool parenthesized = valid && token.kind == C_TOKEN_L_PAREN;
                valid = parenthesized ? pp_read_token(pp, &token, &end) : valid;
                valid = valid && token.kind == C_TOKEN_RAW_IDENTIFIER;
                const bool defined = valid && pp_is_macro(pp, token.value) && pp->macros[pp->macro_of[token.value]].builtin != PP_BUILTIN_POISONED;
                PPToken close;
                if (valid && parenthesized)
                {
                    valid = pp_read_token(pp, &close, &end) && close.kind == C_TOKEN_R_PAREN;
                }
                if (!valid)
                {
                    pp_diagnose(pp, PP_SEVERITY_ERROR, name.location, "macro name missing or malformed after 'defined'");
                }
                pp_emit(pp, pp_number_token(pp, defined, flags));
            }
            break;
        default:
            // The builtins that take a parenthesized argument and answer with a number.
            pp_start_builtin(pp, id, name, flags, expansion_end);
            break;
    }
    pp->history_current = saved_history;
    pp->diagnostic_origin = saved_origin;
}

// ---------------------------------------------------------------------------------------------------------------------
// Steps of the main loop

// A run of `copied` tokens read from `context` was just copied to the target: its first token takes the context's
// pending flags, which are rare.
BUSTER_INLINE void pp_take_pending_flags(Preprocessor* pp, PPContext* context, uint32_t copied)
{
    if (context->pending_flags != 0 && copied != 0)
    {
        PPTokens* target = pp_target(pp);
        target->flags[target->count - copied] |= context->pending_flags;
        context->pending_flags = 0;
    }
}

// An expansion whose tokens can start a line is read from a file into the output, from the name at `location`: they
// start at the output's end (PPExpansionStart). One that gives none is replaced by the next.
BUSTER_INLINE void pp_note_expansion(Preprocessor* pp, uint32_t location)
{
    const uint32_t token = pp->output.count;
    const uint32_t count = pp->expansion_start_count;
    const bool replaces = count != 0 && pp->expansion_starts[count - 1].token == token;
    pp->expansion_starts[replaces ? count - 1 : count] = (PPExpansionStart){ .token = token, .location = location };
    pp->expansion_start_count = replaces ? count : count + 1;
}

// Expands the macro whose name is the top context's current token.
static void pp_expand_current(Preprocessor* pp)
{
    PPContext* context = &pp->contexts[pp->context_count - 1];
    PPToken name = pp_context_token(pp, context, context->position);
    if (context->pending_flags != 0)
    {
        name.flags |= context->pending_flags;
        context->pending_flags = 0;
    }
    if (pp->options.expansion_starts && context->kind == BUSTER_PP_CONTEXT_FILE && pp->capture_count == 0)
    {
        pp->expansion_location = name.location;
        if (name.flags & BUSTER_PP_START_OF_LINE)
        {
            pp_note_expansion(pp, name.location);
        }
    }
    const PPMacroId id = pp->macro_of[name.value];
    PPMacro* macro = &pp->macros[id];
    const uint32_t expansion_end = pp_expansion_end(context, name);
    if (macro->disabled != 0)
    {
        // A macro disabled while a file is read can only come from a directive in an invocation's arguments.
        context->position += 1;
        name.flags |= BUSTER_PP_NO_EXPAND;
        pp_emit(pp, name);
    }
    else if (macro->flags & BUSTER_PP_MACRO_BUILTIN)
    {
        context->position += 1;
        pp->statistics.builtins += pp->options.timing;
        pp_expand_builtin(pp, id, name, expansion_end);
    }
    else if (!(macro->flags & BUSTER_PP_MACRO_FUNCTION))
    {
        context->position += 1;
        pp_expand_object(pp, id, name, expansion_end);
    }
    else if (pp_next_is_lparen(pp))
    {
        context->position += 1;
        pp_invoke(pp, id, name);
    }
    else
    {
        context->position += 1;
        pp_emit(pp, name);
    }
}

static void pp_step_file(Preprocessor* pp, PPContext* context)
{
    const PPFileData* data = &pp->datas[pp->files[context->file].data];
    const uint32_t limit = pp_run_limit(data, context);
    if (context->position < limit)
    {
        const uint32_t base = pp->ranges[context->range].base;
        const uint32_t stop = pp->vectorized ? pp_copy_file_run_vector(pp, data, base, context->position, limit) : pp_copy_file_run_scalar(pp, data, base, context->position, limit);
        if (pp->capture_count != 0)
        {
            pp_history_fill(pp, &pp->captures, pp->captures.count - (stop - context->position), pp->captures.count, 0);
        }
        pp_take_pending_flags(pp, context, stop - context->position);
        if (pp->options.timing)
        {
            pp->statistics.file_runs += 1;
            pp->statistics.file_run_tokens += stop - context->position;
        }
        context->position = stop;
        if (stop < limit && !pp->fatal)
        {
            pp_expand_current(pp);
        }
    }
    else if (context->next_directive < data->directive_count)
    {
        pp_directive(pp, context);
    }
    else
    {
        pp_pop_context(pp);
    }
}

static void pp_step_buffer(Preprocessor* pp, PPContext* context)
{
    const uint32_t stop = pp->vectorized ? pp_copy_buffer_run_vector(pp, context->tokens, context->position, context->end)
                                         : pp_copy_buffer_run_scalar(pp, context->tokens, context->position, context->end);
    PPTokens* target = pp_target(pp);
    pp_history_copy(pp, target, target->count - (stop - context->position), context->tokens, context->position, stop);
    pp_take_pending_flags(pp, context, stop - context->position);
    if (pp->options.timing)
    {
        pp->statistics.buffer_runs += 1;
        pp->statistics.buffer_run_tokens += stop - context->position;
    }
    context->position = stop;
    if (stop < context->end && !pp->fatal)
    {
        pp_expand_current(pp);
    }
}

// The main loop: runs the top context until the stack falls to `stop_depth` contexts (an argument's barrier that ends
// goes on with its invocation).
static void pp_finish_condition(Preprocessor* pp);
static void pp_finish_include(Preprocessor* pp);
static void pp_finish_line(Preprocessor* pp);

// A barrier ended: its capture's continuation takes the capture (every barrier is pushed with a capture).
static void pp_finish_capture(Preprocessor* pp)
{
    switch (pp->capture_frames[pp->capture_count - 1].kind)
    {
        case BUSTER_PP_CAPTURE_ARGUMENT:
            pp_finish_argument(pp);
            break;
        case BUSTER_PP_CAPTURE_CONDITION:
            pp_finish_condition(pp);
            break;
        case BUSTER_PP_CAPTURE_INCLUDE:
            pp_finish_include(pp);
            break;
        case BUSTER_PP_CAPTURE_LINE:
            pp_finish_line(pp);
            break;
        case BUSTER_PP_CAPTURE_BUILTIN:
            pp_finish_builtin(pp);
            break;
    }
}

// The main loop, the preprocessor's only loop over contexts: it runs the top context until the stack falls to
// `stop_depth` contexts. Everything that nests is a frame on an explicit stack, never a call: a context per file and
// buffer, an invocation per macro whose arguments are collected (here, a token at a time, when they aren't in one
// context: pp_collect_step) and expanded, and a capture per line expanded for a directive or a builtin, whose
// continuation runs when its barrier ends (pp_finish_capture).
static void pp_run_contexts(Preprocessor* pp, uint32_t stop_depth)
{
    while (!pp->fatal && pp->context_count > stop_depth)
    {
        PPContext* context = &pp->contexts[pp->context_count - 1];
        if (pp_collecting(pp))
        {
            pp_collect_step(pp);
        }
        else if (context->kind == BUSTER_PP_CONTEXT_FILE)
        {
            pp_step_file(pp, context);
        }
        else if (context->position < context->end)
        {
            pp_step_buffer(pp, context);
        }
        else
        {
            const bool barrier = context->barrier;
            pp_pop_context(pp);
            if (barrier && pp->capture_count != 0)
            {
                pp_finish_capture(pp);
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Directives

// Copies a file's tokens [begin, end) to pp->arguments, as buffers hold them, for expanding a directive's line.
static bool pp_copy_line(Preprocessor* pp, const PPContext* context, uint32_t begin, uint32_t end)
{
    const PPFileData* data = &pp->datas[pp->files[context->file].data];
    const uint32_t base = pp->ranges[context->range].base;
    bool result = pp_room(pp, &pp->arguments, end - begin);
    for (uint32_t t = begin; result && t < end; t += 1)
    {
        const PPToken token = pp_file_token(data, base, t);
        pp_tokens_set(&pp->arguments, pp->arguments.count, token.kind, (uint8_t)(token.flags & ~BUSTER_PP_START_OF_LINE), token.value, token.location);
        pp_history_fill(pp, &pp->arguments, pp->arguments.count, pp->arguments.count + 1, 0);
        pp->arguments.count += 1;
    }
    return result;
}

// Enters a file (prepared on first use) included at `include_location` from range `parent`, unless its guard is defined
// or #pragma once read it. The inclusion is as much a system header as the file and as the includer there are, as
// clang has it (HandleHeaderIncludeOrImport): a file included from a system header is one too.
static void pp_enter_file(Preprocessor* pp, uint32_t file, uint32_t parent, uint32_t include_location, uint32_t search_index)
{
    PPFileData* data = &pp->datas[pp->files[file].data];
    const bool skipped = data->prepared && (data->once || (data->guard != 0 && pp_is_macro(pp, data->guard)));
    pp->statistics.skipped_includes += skipped && pp->options.timing;
    if (pp->include_depth >= BUSTER_PP_MAX_INCLUDE_DEPTH)
    {
        pp_diagnose(pp, PP_SEVERITY_FATAL, include_location, "#include nested too deeply");
    }
    else if (!skipped && (data->prepared || pp_prepare_data(pp, data, pp->files[file].path)))
    {
        const uint32_t range = pp_add_range(pp, data->length, file, data->bytes, parent, include_location);
        PPContext* context = range != BUSTER_PP_NONE ? pp_push_context(pp, BUSTER_PP_CONTEXT_FILE) : 0;
        if (context != 0)
        {
            const uint32_t note = parent != BUSTER_PP_NONE ? pp->ranges[parent].last_line_directive : BUSTER_PP_NONE;
            const uint8_t includer = parent == BUSTER_PP_NONE ? BUSTER_PP_USER_FILE : note != BUSTER_PP_NONE ? pp->line_directives[note].system : pp->ranges[parent].system;
            pp->ranges[range].system = pp->files[file].system > includer ? pp->files[file].system : includer;
            context->file = file;
            context->search_index = search_index;
            context->range = range;
            context->end = data->token_count;
            context->conditional_base = pp->conditional_count;
            pp->include_depth += pp->context_count > 1;
        }
    }
}

// The bytes of a file's line from token `begin` to the end of token `end - 1` (for #error and #warning).
static void pp_line_text(const Preprocessor* pp, const PPFileData* data, uint32_t begin, uint32_t end, const uint8_t** text, uint32_t* length)
{
    *text = (const uint8_t*)"";
    *length = 0;
    if (begin < end)
    {
        *text = data->bytes + data->starts[begin];
        *length = pp_file_token_end(pp, data, end - 1) - data->starts[begin];
    }
}

static bool pp_evaluate(Preprocessor* pp, const PPTokens* tokens, uint32_t begin, uint32_t end, uint32_t location);

// Skips to the next directive of directive d's conditional group (or to the end of the file if it is unterminated).
static void pp_skip_group(Preprocessor* pp, PPContext* context, uint32_t d)
{
    const PPFileData* data = &pp->datas[pp->files[context->file].data];
    const uint32_t next = data->directive_next[d];
    if (next == BUSTER_PP_NONE)
    {
        context->position = data->token_count;
        context->next_directive = data->directive_count;
    }
    else
    {
        context->position = data->directive_hash[next];
        context->next_directive = next;
    }
}

// A #if's, #ifdef's or #ifndef's group is open (taken or not), or a #elif's, #elifdef's or #elifndef's branch is
// decided, at directive d: a group not taken is skipped to its next directive.
static void pp_apply_condition(Preprocessor* pp, PPContext* context, uint32_t d, uint8_t kind, bool taken, uint32_t location)
{
    if (kind == PP_DIRECTIVE_IF || kind == PP_DIRECTIVE_IFDEF || kind == PP_DIRECTIVE_IFNDEF)
    {
        if (pp->conditional_count < BUSTER_PP_MAX_CONTEXTS)
        {
            pp->conditionals[pp->conditional_count] = (PPConditional){ .taken = taken, .seen_else = false, .location = location };
            pp->conditional_count += 1;
        }
    }
    else
    {
        pp->conditionals[pp->conditional_count - 1].taken = taken;
    }
    if (!taken)
    {
        pp_skip_group(pp, context, d);
    }
}

// A #if or #elif line (tokens [begin, end) of the context's file) is copied to pp->arguments and expanded through a
// capture (pp_start_capture); pp_finish_condition evaluates it and decides the group. An empty line is an error, and
// false at once.
static void pp_start_condition(Preprocessor* pp, PPContext* context, uint32_t d, uint8_t kind, uint32_t begin, uint32_t end, uint32_t location)
{
    const uint32_t line = pp->arguments.count;
    bool started = false;
    if (begin == end)
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, location, "#if with no expression");
    }
    else if (pp_copy_line(pp, context, begin, end))
    {
        const PPCapture capture = {
            .kind = BUSTER_PP_CAPTURE_CONDITION,
            .arguments_begin = line,
            .directive = d,
            .context = (uint32_t)(context - pp->contexts),
            .directive_kind = kind,
            .location = location,
        };
        started = pp_start_capture(pp, capture, line, pp->arguments.count, location);
    }
    if (!started)
    {
        pp->arguments.count = line;
        pp_apply_condition(pp, context, d, kind, false, location);
    }
}

// The barrier over a #if or #elif line ended: the capture is the line expanded, which decides the group.
static void pp_finish_condition(Preprocessor* pp)
{
    const PPCapture capture = pp->capture_frames[pp->capture_count - 1];
    const bool taken = pp_evaluate(pp, &pp->captures, capture.begin, pp->captures.count, capture.location);
    if (pp->options.timing)
    {
        const uint64_t tokens = pp->captures.count - capture.begin;
        pp->statistics.conditions += 1;
        pp->statistics.condition_tokens += tokens;
        pp->statistics.long_conditions += tokens > 32;
        pp->statistics.longest_condition = tokens > pp->statistics.longest_condition ? tokens : pp->statistics.longest_condition;
    }
    pp->capture_count -= 1;
    pp->captures.count = capture.begin;
    pp->arguments.count = capture.arguments_begin;
    pp_apply_condition(pp, &pp->contexts[capture.context], capture.directive, capture.directive_kind, taken, capture.location);
}

// The conditional directives (C 6.10.2), at directive d.
static void pp_conditional(Preprocessor* pp, PPContext* context, uint32_t d, uint8_t kind)
{
    const PPFileData* data = &pp->datas[pp->files[context->file].data];
    const uint32_t hash = data->directive_hash[d];
    const uint32_t end = data->directive_end[d];
    const uint32_t base = pp->ranges[context->range].base;
    const uint32_t location = base + data->starts[hash];
    const bool open = pp->conditional_count > context->conditional_base;
    PPConditional* group = open ? &pp->conditionals[pp->conditional_count - 1] : 0;
    switch (kind)
    {
        case PP_DIRECTIVE_IF:
            pp_start_condition(pp, context, d, kind, hash + 2, end, location);
            break;
        case PP_DIRECTIVE_IFDEF:
        case PP_DIRECTIVE_IFNDEF:
        {
            const bool named = hash + 2 < end && (data->kinds[hash + 2] & 0x7F) == C_TOKEN_RAW_IDENTIFIER;
            if (!named)
            {
                pp_diagnose(pp, PP_SEVERITY_ERROR, location, "macro name missing");
            }
            else if (hash + 3 < end)
            {
                pp_diagnose(pp, PP_SEVERITY_WARNING, location, "extra tokens at end of #%s directive", kind == PP_DIRECTIVE_IFDEF ? "ifdef" : "ifndef");
            }
            const bool defined = named && pp_is_macro(pp, data->values[hash + 2]);
            pp_apply_condition(pp, context, d, kind, named && (kind == PP_DIRECTIVE_IFDEF ? defined : !defined), location);
            break;
        }
        case PP_DIRECTIVE_ELIF:
        case PP_DIRECTIVE_ELIFDEF:
        case PP_DIRECTIVE_ELIFNDEF:
            if (!open)
            {
                pp_diagnose(pp, PP_SEVERITY_ERROR, location, "#elif without #if");
            }
            else if (group->seen_else)
            {
                pp_diagnose(pp, PP_SEVERITY_ERROR, location, "#elif after #else");
                pp_skip_group(pp, context, d);
            }
            else if (group->taken)
            {
                pp_skip_group(pp, context, d);
            }
            else if (kind == PP_DIRECTIVE_ELIF)
            {
                pp_start_condition(pp, context, d, kind, hash + 2, end, location);
            }
            else
            {
                const bool named = hash + 2 < end && (data->kinds[hash + 2] & 0x7F) == C_TOKEN_RAW_IDENTIFIER;
                const bool defined = named && pp_is_macro(pp, data->values[hash + 2]);
                pp_apply_condition(pp, context, d, kind, named && (kind == PP_DIRECTIVE_ELIFDEF ? defined : !defined), location);
            }
            break;
        case PP_DIRECTIVE_ELSE:
            if (!open)
            {
                pp_diagnose(pp, PP_SEVERITY_ERROR, location, "#else without #if");
            }
            else
            {
                if (group->seen_else)
                {
                    pp_diagnose(pp, PP_SEVERITY_ERROR, location, "#else after #else");
                }
                group->seen_else = true;
                if (group->taken)
                {
                    pp_skip_group(pp, context, d);
                }
                group->taken = true;
            }
            break;
        case PP_DIRECTIVE_ENDIF:
            if (!open)
            {
                pp_diagnose(pp, PP_SEVERITY_ERROR, location, "#endif without #if");
            }
            else
            {
                pp->conditional_count -= 1;
            }
            break;
    }
}

// The file a #include, #include_next or #import (`kind`) in the context's file names: entered, unless #import read it.
static void pp_include_file(Preprocessor* pp, PPContext* context, uint8_t kind, const uint8_t* name, uint32_t length, bool angled, uint32_t location)
{
    const bool next = kind == PP_DIRECTIVE_INCLUDE_NEXT && pp->include_depth != 0;
    uint32_t found_in;
    const uint32_t file = pp_find_include(pp, name, length, angled, next, context->file, context->search_index, &found_in);
    if (file == BUSTER_PP_NONE)
    {
        pp_diagnose(pp, PP_SEVERITY_FATAL, location, "'%.*s' file not found", (int)length, (const char*)name);
    }
    else if (kind == PP_DIRECTIVE_IMPORT && pp->datas[pp->files[file].data].prepared && pp->datas[pp->files[file].data].once)
    {
        // Already imported.
    }
    else
    {
        pp_enter_file(pp, file, context->range, location, found_in);
        if (kind == PP_DIRECTIVE_IMPORT)
        {
            pp->datas[pp->files[file].data].once = true;
        }
    }
}

// The barrier over a computed #include's line ended: the capture is the line expanded, a string literal or <...>.
static void pp_finish_include(Preprocessor* pp)
{
    const PPCapture capture = pp->capture_frames[pp->capture_count - 1];
    uint8_t name[PATH_MAX];
    uint32_t length = 0;
    bool angled = false;
    const bool valid = pp_header_name(pp, &pp->captures, capture.begin, pp->captures.count, name, sizeof(name), &length, &angled);
    pp->capture_count -= 1;
    pp->captures.count = capture.begin;
    pp->arguments.count = capture.arguments_begin;
    if (!valid)
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, capture.location, "expected \"FILENAME\" or <FILENAME>");
    }
    else
    {
        pp_include_file(pp, &pp->contexts[capture.context], capture.directive_kind, name, length, angled, capture.location);
    }
}

// #include, #include_next and #import, at directive d. A computed name (the line's macros expanded) goes through a
// capture, which pp_finish_include takes.
static void pp_include(Preprocessor* pp, PPContext* context, uint32_t d, uint8_t kind)
{
    const PPFileData* data = &pp->datas[pp->files[context->file].data];
    const uint32_t hash = data->directive_hash[d];
    const uint32_t end = data->directive_end[d];
    const uint32_t base = pp->ranges[context->range].base;
    const uint32_t location = base + data->starts[hash];
    uint8_t name[PATH_MAX];
    uint32_t length = 0;
    bool angled = false;
    bool started = false;
    Scratch scratch = scratch_begin(0);
    const PPHeaderForm form = pp_literal_header_name(data, d, pp->refine_flags, name, sizeof(name), &length, &angled, scratch.arena);
    scratch_end(scratch);
    if (form == PP_HEADER_COMPUTED)
    {
        // A computed include: the line expanded, then a string literal or <...> from its tokens.
        const uint32_t line = pp->arguments.count;
        const PPCapture capture = {
            .kind = BUSTER_PP_CAPTURE_INCLUDE,
            .arguments_begin = line,
            .directive = d,
            .context = (uint32_t)(context - pp->contexts),
            .directive_kind = kind,
            .location = location,
        };
        started = pp_copy_line(pp, context, hash + 2, end) && pp_start_capture(pp, capture, line, pp->arguments.count, location);
        pp->arguments.count = started ? pp->arguments.count : line;
    }
    if (form != PP_HEADER_LITERAL && !started)
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, location, "expected \"FILENAME\" or <FILENAME>");
    }
    else if (form == PP_HEADER_LITERAL)
    {
        pp_include_file(pp, context, kind, name, length, angled, location);
    }
}

// A #line's or line marker's file name from its string literal's spelling (quotes included, line splices removed), read as
// clang reads an unevaluated string literal: a simple escape or a UCN (as UTF-8) stands for its character, and an octal or
// hex escape is an error. Writes the name to `out` (room for the spelling's length) and returns its length, or
// BUSTER_PP_NONE after a diagnostic.
static uint32_t pp_decode_file_name(Preprocessor* pp, const uint8_t* spelling, uint32_t length, char* out, uint32_t location)
{
    static const char simple[] = "'\"?\\abfnrtv";
    static const char meaning[] = "'\"?\\\a\b\f\n\r\t\v";
    uint32_t result = 0;
    bool valid = length >= 2 && spelling[0] == '"' && spelling[length - 1] == '"';
    bool reported = false;
    for (uint32_t i = 1; valid && i + 1 < length; i += 1)
    {
        uint8_t c = spelling[i];
        if (c == '\\' && i + 2 < length)
        {
            const uint8_t e = spelling[i + 1];
            const char* found = e != 0 ? strchr(simple, e) : 0;
            if (found != 0)
            {
                c = (uint8_t)meaning[found - simple];
                i += 1;
            }
            else if (e == 'u' || e == 'U')
            {
                // A UCN: four or eight hex digits, written as UTF-8.
                const uint32_t digits = e == 'u' ? 4 : 8;
                uint32_t code = 0;
                valid = i + 2 + digits < length;
                for (uint32_t k = 0; valid && k < digits; k += 1)
                {
                    const int32_t v = hex_digit_value(spelling[i + 2 + k]);
                    valid = v >= 0;
                    code = code * 16 + (uint32_t)(v >= 0 ? v : 0);
                }
                valid = valid && code <= 0x10FFFF && (code < 0xD800 || code > 0xDFFF);
                if (valid)
                {
                    uint8_t utf8[4];
                    const uint32_t n = code < 0x80 ? 1 : code < 0x800 ? 2 : code < 0x10000 ? 3 : 4;
                    utf8[0] = (uint8_t)(n == 1 ? code : n == 2 ? 0xC0 | (code >> 6) : n == 3 ? 0xE0 | (code >> 12) : 0xF0 | (code >> 18));
                    for (uint32_t k = 1; k < n; k += 1)
                    {
                        utf8[k] = (uint8_t)(0x80 | ((code >> (6 * (n - 1 - k))) & 0x3F));
                    }
                    memcpy(out + result, utf8, n - 1);
                    result += n - 1;
                    c = utf8[n - 1];
                }
                i += 1 + digits;
            }
            else
            {
                // An octal or hex escape (or another): clang allows none in an unevaluated string.
                pp_diagnose(pp, PP_SEVERITY_ERROR, location, "invalid escape sequence in the file name of a #line directive");
                valid = false;
                reported = true;
            }
        }
        out[result] = (char)c;
        result += 1;
    }
    if (!valid && !reported)
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, location, "invalid file name in a #line directive");
    }
    return valid ? result : BUSTER_PP_NONE;
}

// Where clang's lexer starts a token that starts at `start`: at the first of any line splices right before it (a
// backslash, blanks, and a newline or CRLF, with nothing between them and the token), since it reads them as part of
// the token after them. Blanks between the splice and the token are whitespace it skips first.
static uint32_t pp_start_with_splices(const uint8_t* bytes, uint32_t start)
{
    uint32_t result = start;
    bool moved = true;
    while (moved)
    {
        uint32_t p = result;
        const bool newline = p >= 1 && (bytes[p - 1] == '\n' || bytes[p - 1] == '\r');
        p -= newline ? 1 : 0;
        p -= newline && bytes[p] == '\n' && p >= 1 && bytes[p - 1] == '\r' ? 1 : 0;
        while (newline && p >= 1 && (bytes[p - 1] == ' ' || bytes[p - 1] == '\t'))
        {
            p -= 1;
        }
        moved = newline && p >= 1 && bytes[p - 1] == '\\';
        result = moved ? p - 1 : result;
    }
    return result;
}

// Adds a line note to a range, from `location` on (PPLineDirective): `name` null keeps the presumed name, the name of
// the range's latest note, and `system` BUSTER_PP_NONE keeps the system level (that note's, else the inclusion's).
static void pp_add_line_note(Preprocessor* pp, uint32_t range, uint32_t location, int64_t delta, const char* name, uint32_t name_length, uint32_t system)
{
    PPRange* record = &pp->ranges[range];
    const uint32_t latest = record->last_line_directive;
    if (pp->line_directive_count < BUSTER_PP_MAX_LINE_DIRECTIVES)
    {
        const uint32_t index = pp->line_directive_count;
        const bool inherits = name == 0 && latest != BUSTER_PP_NONE;
        const uint8_t level = latest != BUSTER_PP_NONE ? pp->line_directives[latest].system : record->system;
        pp->line_directives[index] = (PPLineDirective){
            .range = range,
            .location = location,
            .delta = delta,
            .name = inherits ? pp->line_directives[latest].name : name,
            .name_length = inherits ? pp->line_directives[latest].name_length : name_length,
            .next_in_range = BUSTER_PP_NONE,
            .system = system == BUSTER_PP_NONE ? level : (uint8_t)system,
        };
        pp->line_directive_count += 1;
        if (latest != BUSTER_PP_NONE)
        {
            pp->line_directives[latest].next_in_range = index;
        }
        else
        {
            record->first_line_directive = index;
        }
        record->last_line_directive = index;
    }
}

// The lines a #line or a line marker at directive d numbers, from its tokens [begin, stop) of `tokens` (expanded, for
// #line): lines from the next on are numbered from the digits, and the file is named by the string literal if there is
// one.
static void pp_apply_line(Preprocessor* pp, PPContext* context, uint32_t d, const PPTokens* tokens, uint32_t begin, uint32_t stop, uint32_t location)
{
    const PPFileData* data = &pp->datas[pp->files[context->file].data];
    const uint32_t hash = data->directive_hash[d];
    const uint32_t base = pp->ranges[context->range].base;
    uint64_t number = 0;
    bool valid = begin < stop && tokens->kinds[begin] == C_TOKEN_NUMERIC_CONSTANT;
    // The operands' spellings without their line splices (translation phase 2), as clang reads them.
    Scratch scratch = scratch_begin(0);
    if (valid)
    {
        uint8_t* buffer = arena_push(scratch.arena, (uint64_t)(tokens->values[begin] - tokens->locations[begin]) + 1, 1);
        const uint8_t* digits = 0;
        const uint32_t count = buffer != 0 ? pp_spelling(pp, tokens->kinds[begin], tokens->values[begin], tokens->locations[begin], buffer, &digits) : 0;
        valid = count != 0;
        for (uint32_t i = 0; valid && i < count; i += 1)
        {
            valid = digits[i] >= '0' && digits[i] <= '9' && number < (1ull << 31);
            number = number * 10 + (uint64_t)(digits[i] - '0');
        }
    }
    const char* name = 0;
    uint32_t name_length = 0;
    bool name_reported = false;
    if (valid && begin + 1 < stop)
    {
        valid = tokens->kinds[begin + 1] == C_TOKEN_STRING_LITERAL;
        const uint32_t spelled = tokens->values[begin + 1] - tokens->locations[begin + 1];
        uint8_t* buffer = valid ? arena_push(scratch.arena, (uint64_t)spelled + 1, 1) : 0;
        char* copy = valid ? arena_push(&pp->arena, (uint64_t)spelled + 1, 1) : 0;
        const uint8_t* spelling = 0;
        const uint32_t length = buffer != 0 && copy != 0 ? pp_spelling(pp, tokens->kinds[begin + 1], tokens->values[begin + 1], tokens->locations[begin + 1], buffer, &spelling) : 0;
        name_length = length >= 2 ? pp_decode_file_name(pp, spelling, length, copy, location) : BUSTER_PP_NONE;
        name_reported = length >= 2 && name_length == BUSTER_PP_NONE;
        valid = valid && name_length != BUSTER_PP_NONE;
        name = valid ? copy : 0;
        name_length = valid ? name_length : 0;
    }
    scratch_end(scratch);
    if (!valid && !name_reported)
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, location, "#line directive requires a simple digit sequence (and optionally a file name)");
    }
    else if (valid)
    {
        // The line after the number token's is `number`, as clang counts: from where it lexes that token (the digits'
        // line, or for digits right after a line splice, the splice's, since its lexer reads a splice as part of the
        // token after it). The token is the directive's first operand (the macro name for a #line it expands).
        const uint32_t first = (data->kinds[hash + 1] & 0x7F) == C_TOKEN_RAW_IDENTIFIER ? hash + 2 : hash + 1;
        const uint32_t anchor = first < data->directive_end[d] ? pp_start_with_splices(data->bytes, data->starts[first]) : data->starts[hash];
        uint32_t column;
        const uint32_t physical = pp_line_of(pp, context->range, base + anchor, &column) + 1;
        // A #line without a name keeps the presumed one, and any #line keeps the system level (clang's FileKind).
        pp_add_line_note(pp, context->range, location + 1, (int64_t)number - (int64_t)physical, name, name_length, BUSTER_PP_NONE);
    }
}

// The barrier over a #line's line ended: the capture is the line expanded.
static void pp_finish_line(Preprocessor* pp)
{
    const PPCapture capture = pp->capture_frames[pp->capture_count - 1];
    pp_apply_line(pp, &pp->contexts[capture.context], capture.directive, &pp->captures, capture.begin, pp->captures.count, capture.location);
    pp->capture_count -= 1;
    pp->captures.count = capture.begin;
    pp->arguments.count = capture.arguments_begin;
}

// #line digits ["file"], or a GNU line marker (`# digits "file" flags`), at directive d. #line's line is expanded
// through a capture, which pp_finish_line takes; a line marker's is read as it is.
static void pp_line(Preprocessor* pp, PPContext* context, uint32_t d, uint8_t kind)
{
    const PPFileData* data = &pp->datas[pp->files[context->file].data];
    const uint32_t hash = data->directive_hash[d];
    const uint32_t end = data->directive_end[d];
    const uint32_t location = pp->ranges[context->range].base + data->starts[hash];
    const uint32_t first = kind == PP_DIRECTIVE_LINE_MARKER ? hash + 1 : hash + 2;
    const uint32_t line = pp->arguments.count;
    const bool copied = first < end && pp_copy_line(pp, context, first, end);
    const PPCapture capture = {
        .kind = BUSTER_PP_CAPTURE_LINE,
        .arguments_begin = line,
        .directive = d,
        .context = (uint32_t)(context - pp->contexts),
        .directive_kind = kind,
        .location = location,
    };
    const bool started = copied && kind == PP_DIRECTIVE_LINE && pp_start_capture(pp, capture, line, pp->arguments.count, location);
    if (!started)
    {
        pp_apply_line(pp, context, d, &pp->arguments, line, copied ? pp->arguments.count : line, location);
        pp->arguments.count = line;
    }
}

// Emits a #pragma or #ident line to the output: '#' starting a line, the line's tokens, and BUSTER_PP_KIND_PRAGMA_END.
// The name's location is the directive's, or for _Pragma (`from_operator`) the operator's, whose '#' is then one a
// macro made (-E's line markers place it as the expansion the operator is in).
static void pp_emit_pragma(Preprocessor* pp, const PPTokens* tokens, uint32_t begin, uint32_t end, IdentId name, uint32_t location, bool from_operator)
{
    // The '#' isn't in any source here: its spelling is the scratch "#".
    const uint8_t flags = (uint8_t)(BUSTER_PP_START_OF_LINE | (from_operator ? BUSTER_PP_FROM_MACRO : 0));
    if (from_operator && pp->options.expansion_starts)
    {
        pp_note_expansion(pp, pp->expansion_location);
    }
    pp_emit(pp, (PPToken){ .kind = C_TOKEN_HASH, .flags = flags, .value = pp->hash_location + 1, .location = pp->hash_location });
    pp_emit(pp, (PPToken){ .kind = C_TOKEN_RAW_IDENTIFIER, .flags = 0, .value = name, .location = location });
    for (uint32_t i = begin; i < end; i += 1)
    {
        PPToken token = pp_stack_token(tokens, i);
        token.flags = (uint8_t)((token.flags & BUSTER_PP_SPACE) | (i == begin ? BUSTER_PP_SPACE : 0) | BUSTER_PP_NO_EXPAND);
        pp_emit(pp, token);
    }
    pp_emit(pp, (PPToken){ .kind = BUSTER_PP_KIND_PRAGMA_END });
}

// A pragma's tokens [begin, end) of a stack (after `pragma`): the ones the preprocessor acts on (once, GCC
// system_header, push_macro, pop_macro, GCC poison, GCC dependency), and any other goes to the output as clang -E prints
// it. `from_operator` for _Pragma.
static void pp_pragma(Preprocessor* pp, const PPTokens* tokens, uint32_t begin, uint32_t end, uint32_t location, bool from_operator)
{
    const IdentId first = begin < end && tokens->kinds[begin] == C_TOKEN_RAW_IDENTIFIER ? tokens->values[begin] : 0;
    const IdentId second = begin + 1 < end && tokens->kinds[begin + 1] == C_TOKEN_RAW_IDENTIFIER ? tokens->values[begin + 1] : 0;
    const uint32_t file = pp_current_file(pp);
    if (first == C_NAME_ONCE)
    {
        if (file != BUSTER_PP_NONE && file != pp->main_file)
        {
            pp->datas[pp->files[file].data].once = true;
        }
    }
    else if (first == C_NAME_GCC && second == C_NAME_SYSTEM_HEADER)
    {
        // The rest of the file is a system header, as are its later inclusions (clang's MarkFileSystemHeader), and the
        // files it includes; a line note marks where, after the directive, with the presumed line and name kept.
        const PPContext* context = pp_current_file_context(pp);
        if (file != BUSTER_PP_NONE && file != pp->main_file && context != 0)
        {
            pp->files[file].system = BUSTER_PP_SYSTEM_FILE;
            const PPFileData* data = &pp->datas[pp->files[file].data];
            const PPRange* range = &pp->ranges[context->range];
            const uint32_t at = range->base + (context->position < data->token_count ? data->starts[context->position] : data->length);
            const uint32_t latest = range->last_line_directive;
            pp_add_line_note(pp, context->range, at, latest != BUSTER_PP_NONE ? pp->line_directives[latest].delta : 0, 0, 0, BUSTER_PP_SYSTEM_FILE);
        }
    }
    else if (first == C_NAME_GCC && second == C_NAME_DEPENDENCY)
    {
        // Its file's age against this one's: nothing to do here.
    }
    else if (first == C_NAME_GCC && second == C_NAME_POISON)
    {
        for (uint32_t i = begin + 2; i < end; i += 1)
        {
            if (tokens->kinds[i] == C_TOKEN_RAW_IDENTIFIER)
            {
                pp_define_builtin(pp, tokens->values[i], PP_BUILTIN_POISONED, false);
            }
        }
    }
    else if ((first == C_NAME_PUSH_MACRO || first == C_NAME_POP_MACRO) && begin + 3 < end && tokens->kinds[begin + 1] == C_TOKEN_L_PAREN &&
             tokens->kinds[begin + 2] == C_TOKEN_STRING_LITERAL)
    {
        // push_macro("NAME") and pop_macro("NAME").
        const uint8_t* bytes = pp_location_bytes(pp, tokens->locations[begin + 2]);
        const uint32_t spelled = tokens->values[begin + 2] - tokens->locations[begin + 2];
        const IdentId name = spelled >= 2 ? intern_find_or_add(pp->table, bytes + 1, spelled - 2) : 0;
        if (name != 0 && first == C_NAME_PUSH_MACRO && pp->pushed_count < BUSTER_PP_MAX_PUSHED)
        {
            pp->pushed_names[pp->pushed_count] = name;
            pp->pushed_macros[pp->pushed_count] = pp->macro_of[name];
            pp->pushed_count += 1;
        }
        else if (name != 0 && first == C_NAME_POP_MACRO)
        {
            for (uint32_t i = pp->pushed_count; i > 0; i -= 1)
            {
                if (pp->pushed_names[i - 1] == name)
                {
                    pp_set_macro(pp, name, pp->pushed_macros[i - 1]);
                    memmove(pp->pushed_names + i - 1, pp->pushed_names + i, (pp->pushed_count - i) * sizeof(IdentId));
                    memmove(pp->pushed_macros + i - 1, pp->pushed_macros + i, (pp->pushed_count - i) * sizeof(PPMacroId));
                    pp->pushed_count -= 1;
                    break;
                }
            }
        }
    }
    else
    {
        pp_emit_pragma(pp, tokens, begin, end, C_NAME_PRAGMA, location, from_operator);
    }
}

// Runs directive `context->next_directive` of a file context; the context goes on after its line unless a conditional
// skipped elsewhere.
static void pp_directive(Preprocessor* pp, PPContext* context)
{
    const uint32_t d = context->next_directive;
    const PPFileData* data = &pp->datas[pp->files[context->file].data];
    const uint32_t data_index = pp->files[context->file].data;
    const uint32_t hash = data->directive_hash[d];
    const uint32_t end = data->directive_end[d];
    const uint8_t kind = data->directive_kind[d];
    const uint32_t base = pp->ranges[context->range].base;
    const uint32_t location = base + data->starts[hash];
    context->position = end;
    context->next_directive = d + 1;
    pp->statistics.directives += pp->options.timing;
    switch (kind)
    {
        case PP_DIRECTIVE_NULL:
            break;
        case PP_DIRECTIVE_DEFINE:
            pp_define(pp, data_index, base, hash + 2, end);
            break;
        case PP_DIRECTIVE_UNDEF:
            if (hash + 2 < end && (data->kinds[hash + 2] & 0x7F) == C_TOKEN_RAW_IDENTIFIER)
            {
                const IdentId name = data->values[hash + 2];
                if (name == C_NAME_DEFINED)
                {
                    pp_diagnose(pp, PP_SEVERITY_ERROR, location, "'defined' cannot be used as a macro name");
                }
                else
                {
                    pp_set_macro(pp, name, 0);
                }
            }
            else
            {
                pp_diagnose(pp, PP_SEVERITY_ERROR, location, "macro name missing");
            }
            break;
        case PP_DIRECTIVE_INCLUDE:
        case PP_DIRECTIVE_INCLUDE_NEXT:
        case PP_DIRECTIVE_IMPORT:
            pp_include(pp, context, d, kind);
            break;
        case PP_DIRECTIVE_IF:
        case PP_DIRECTIVE_IFDEF:
        case PP_DIRECTIVE_IFNDEF:
        case PP_DIRECTIVE_ELIF:
        case PP_DIRECTIVE_ELIFDEF:
        case PP_DIRECTIVE_ELIFNDEF:
        case PP_DIRECTIVE_ELSE:
        case PP_DIRECTIVE_ENDIF:
            pp_conditional(pp, context, d, kind);
            break;
        case PP_DIRECTIVE_LINE:
        case PP_DIRECTIVE_LINE_MARKER:
            pp_line(pp, context, d, kind);
            break;
        case PP_DIRECTIVE_ERROR:
        case PP_DIRECTIVE_WARNING:
        {
            const uint8_t* text;
            uint32_t length;
            pp_line_text(pp, data, hash + 2, end, &text, &length);
            pp_diagnose(pp, kind == PP_DIRECTIVE_ERROR ? PP_SEVERITY_ERROR : PP_SEVERITY_WARNING, location, "%.*s", (int)length, (const char*)text);
            break;
        }
        case PP_DIRECTIVE_PRAGMA:
        case PP_DIRECTIVE_IDENT:
        {
            const uint32_t line = pp->arguments.count;
            if (pp_copy_line(pp, context, hash + 2, end))
            {
                if (kind == PP_DIRECTIVE_PRAGMA)
                {
                    pp_pragma(pp, &pp->arguments, line, pp->arguments.count, location, false);
                }
                else
                {
                    pp_emit_pragma(pp, &pp->arguments, line, pp->arguments.count, data->values[hash + 1], location, false);
                }
            }
            pp->arguments.count = line;
            break;
        }
        case PP_DIRECTIVE_EMBED:
            pp_diagnose(pp, PP_SEVERITY_ERROR, location, "#embed isn't supported yet");
            break;
        case PP_DIRECTIVE_ASSERT:
        case PP_DIRECTIVE_UNKNOWN:
            if (!pp->options.assembly)
            {
                pp_diagnose(pp, PP_SEVERITY_ERROR, location, "invalid preprocessing directive");
            }
            break;
    }
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

static const uint8_t pp_operator_precedence[] = {
    [PP_OPERATOR_PAREN] = 0,
    [PP_OPERATOR_PLUS] = 14,
    [PP_OPERATOR_MINUS] = 14,
    [PP_OPERATOR_NOT] = 14,
    [PP_OPERATOR_COMPLEMENT] = 14,
    [PP_OPERATOR_MULTIPLY] = 13,
    [PP_OPERATOR_DIVIDE] = 13,
    [PP_OPERATOR_REMAINDER] = 13,
    [PP_OPERATOR_ADD] = 12,
    [PP_OPERATOR_SUBTRACT] = 12,
    [PP_OPERATOR_SHIFT_LEFT] = 11,
    [PP_OPERATOR_SHIFT_RIGHT] = 11,
    [PP_OPERATOR_LESS] = 10,
    [PP_OPERATOR_LESS_EQUAL] = 10,
    [PP_OPERATOR_GREATER] = 10,
    [PP_OPERATOR_GREATER_EQUAL] = 10,
    [PP_OPERATOR_EQUAL] = 9,
    [PP_OPERATOR_NOT_EQUAL] = 9,
    [PP_OPERATOR_AND] = 8,
    [PP_OPERATOR_XOR] = 7,
    [PP_OPERATOR_OR] = 6,
    [PP_OPERATOR_LOGICAL_AND] = 5,
    [PP_OPERATOR_LOGICAL_OR] = 4,
    [PP_OPERATOR_QUESTION] = 3,
    [PP_OPERATOR_COLON] = 3,
    [PP_OPERATOR_COMMA] = 2,
};

#define BUSTER_PP_EXPRESSION_DEPTH 1024

// The binary operator of a token kind, or BUSTER_PP_NONE.
static uint32_t pp_binary_operator(uint8_t kind)
{
    uint32_t result = BUSTER_PP_NONE;
    switch (kind)
    {
        case C_TOKEN_STAR: result = PP_OPERATOR_MULTIPLY; break;
        case C_TOKEN_SLASH: result = PP_OPERATOR_DIVIDE; break;
        case C_TOKEN_PERCENT: result = PP_OPERATOR_REMAINDER; break;
        case C_TOKEN_PLUS: result = PP_OPERATOR_ADD; break;
        case C_TOKEN_MINUS: result = PP_OPERATOR_SUBTRACT; break;
        case C_TOKEN_LESSLESS: result = PP_OPERATOR_SHIFT_LEFT; break;
        case C_TOKEN_GREATERGREATER: result = PP_OPERATOR_SHIFT_RIGHT; break;
        case C_TOKEN_LESS: result = PP_OPERATOR_LESS; break;
        case C_TOKEN_LESSEQUAL: result = PP_OPERATOR_LESS_EQUAL; break;
        case C_TOKEN_GREATER: result = PP_OPERATOR_GREATER; break;
        case C_TOKEN_GREATEREQUAL: result = PP_OPERATOR_GREATER_EQUAL; break;
        case C_TOKEN_EQUALEQUAL: result = PP_OPERATOR_EQUAL; break;
        case C_TOKEN_EXCLAIMEQUAL: result = PP_OPERATOR_NOT_EQUAL; break;
        case C_TOKEN_AMP: result = PP_OPERATOR_AND; break;
        case C_TOKEN_CARET: result = PP_OPERATOR_XOR; break;
        case C_TOKEN_PIPE: result = PP_OPERATOR_OR; break;
        case C_TOKEN_AMPAMP: result = PP_OPERATOR_LOGICAL_AND; break;
        case C_TOKEN_PIPEPIPE: result = PP_OPERATOR_LOGICAL_OR; break;
        case C_TOKEN_QUESTION: result = PP_OPERATOR_QUESTION; break;
        case C_TOKEN_COLON: result = PP_OPERATOR_COLON; break;
        case C_TOKEN_COMMA: result = PP_OPERATOR_COMMA; break;
    }
    return result;
}

// Parses an integer constant's spelling (C 6.4.4.1, with C23's binary prefix and digit separators, and the suffixes
// u, l, ll, z, wb in any case and order). Returns false (with an error) if it isn't one.
static bool pp_parse_integer(Preprocessor* pp, const uint8_t* spelling, uint32_t length, uint32_t location, PPValue* value)
{
    uint32_t i = 0;
    uint32_t radix = 10;
    if (length >= 2 && spelling[0] == '0' && (spelling[1] == 'x' || spelling[1] == 'X'))
    {
        radix = 16;
        i = 2;
    }
    else if (length >= 2 && spelling[0] == '0' && (spelling[1] == 'b' || spelling[1] == 'B'))
    {
        radix = 2;
        i = 2;
    }
    else if (spelling[0] == '0')
    {
        radix = 8;
    }
    unsigned __int128 accumulated = 0;
    bool overflow = false;
    bool digits = false;
    bool valid = true;
    while (valid && i < length)
    {
        const uint8_t c = spelling[i];
        int32_t digit = -1;
        if (c >= '0' && c <= '9')
        {
            digit = c - '0';
        }
        else if (radix == 16 && (c | 0x20) >= 'a' && (c | 0x20) <= 'f')
        {
            digit = (c | 0x20) - 'a' + 10;
        }
        if (c == '\'' && digits)
        {
            i += 1;
        }
        else if (digit >= 0 && (uint32_t)digit < radix)
        {
            accumulated = accumulated * radix + (uint32_t)digit;
            overflow |= accumulated > UINT64_MAX;
            accumulated &= UINT64_MAX;
            digits = true;
            i += 1;
        }
        else
        {
            break;
        }
    }
    // The suffix.
    bool is_unsigned = false;
    uint32_t longs = 0;
    while (valid && i < length)
    {
        const uint8_t c = spelling[i] | 0x20;
        if (c == 'u' && !is_unsigned)
        {
            is_unsigned = true;
            i += 1;
        }
        else if (c == 'l' && longs == 0)
        {
            longs = i + 1 < length && spelling[i + 1] == spelling[i] ? 2 : 1;
            i += longs;
        }
        else if (c == 'z' && longs == 0)
        {
            longs = 3;
            i += 1;
        }
        else if (c == 'w' && i + 1 < length && (spelling[i + 1] | 0x20) == 'b' && longs == 0)
        {
            longs = 4;
            i += 2;
        }
        else
        {
            valid = false;
        }
    }
    valid &= digits || radix == 8;
    if (!valid)
    {
        const bool floating = memchr(spelling, '.', length) != 0 || (radix != 16 && (memchr(spelling, 'e', length) != 0 || memchr(spelling, 'E', length) != 0));
        pp_diagnose(pp, PP_SEVERITY_ERROR, location, floating ? "floating point literal in preprocessor expression" : "invalid integer constant '%.*s' in preprocessor expression",
                    (int)length, (const char*)spelling);
    }
    else if (overflow)
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, location, "integer literal is too large to be represented in any integer type");
    }
    else if (!is_unsigned && accumulated > INT64_MAX)
    {
        is_unsigned = true;
        if (radix == 10)
        {
            pp_diagnose(pp, PP_SEVERITY_WARNING, location, "integer literal is too large to be represented in a signed integer type, interpreting as unsigned");
        }
    }
    *value = (PPValue){ .bits = (uint64_t)accumulated, .is_unsigned = is_unsigned };
    return valid && !overflow;
}

// A character constant's value (C 6.4.4.4): 'c' is an int of the (signed) char's value, a multicharacter one packs its
// characters, and L'', u'', U'' and u8'' have their types' values.
static bool pp_parse_character(Preprocessor* pp, uint8_t kind, const uint8_t* spelling, uint32_t length, uint32_t location, PPValue* value)
{
    uint32_t i = 0;
    while (i < length && spelling[i] != '\'')
    {
        i += 1;
    }
    i += 1;
    int64_t result = 0;
    uint32_t count = 0;
    bool valid = i < length;
    while (valid && i < length && spelling[i] != '\'')
    {
        uint64_t c = spelling[i];
        i += 1;
        if (c == '\\' && i < length)
        {
            const uint8_t e = spelling[i];
            i += 1;
            switch (e)
            {
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case 'r': c = '\r'; break;
                case 'a': c = 7; break;
                case 'b': c = 8; break;
                case 'f': c = 12; break;
                case 'v': c = 11; break;
                case 'e': case 'E': c = 27; break;
                case 'x':
                    c = 0;
                    while (i < length && hex_digit_value(spelling[i]) >= 0)
                    {
                        c = c * 16 + (uint64_t)hex_digit_value(spelling[i]);
                        i += 1;
                    }
                    break;
                case 'u':
                case 'U':
                {
                    const uint32_t digits_wanted = e == 'u' ? 4 : 8;
                    c = 0;
                    for (uint32_t k = 0; k < digits_wanted && i < length && hex_digit_value(spelling[i]) >= 0; k += 1)
                    {
                        c = c * 16 + (uint64_t)hex_digit_value(spelling[i]);
                        i += 1;
                    }
                    break;
                }
                default:
                    if (e >= '0' && e <= '7')
                    {
                        c = e - '0';
                        for (uint32_t k = 1; k < 3 && i < length && spelling[i] >= '0' && spelling[i] <= '7'; k += 1)
                        {
                            c = c * 8 + (uint64_t)(spelling[i] - '0');
                            i += 1;
                        }
                    }
                    else
                    {
                        c = e;
                    }
                    break;
            }
        }
        else if (c >= 0x80 && kind != C_TOKEN_CHAR_CONSTANT && kind != C_TOKEN_UTF8_CHAR_CONSTANT)
        {
            // A UTF-8 sequence: its code point.
            uint32_t extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
            c &= extra == 3 ? 0x07 : extra == 2 ? 0x0F : 0x1F;
            for (; extra != 0 && i < length; extra -= 1)
            {
                c = (c << 6) | (spelling[i] & 0x3F);
                i += 1;
            }
        }
        switch (kind)
        {
            case C_TOKEN_CHAR_CONSTANT:
                result = count == 0 ? (int64_t)(int8_t)(uint8_t)c : (int64_t)(int32_t)(((uint32_t)result << 8) | (uint8_t)c);
                break;
            case C_TOKEN_UTF8_CHAR_CONSTANT:
                result = (uint8_t)c;
                break;
            case C_TOKEN_UTF16_CHAR_CONSTANT:
                result = (uint16_t)c;
                break;
            case C_TOKEN_UTF32_CHAR_CONSTANT:
                result = (uint32_t)c;
                break;
            default:
                result = (int64_t)(int32_t)(uint32_t)c;
                break;
        }
        count += 1;
    }
    valid &= count != 0;
    if (!valid)
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, location, "invalid character constant in preprocessor expression");
    }
    // UTF-8, UTF-16 and UTF-32 character constants are unsigned in C (unsigned char, char16_t, char32_t), as in clang.
    const bool is_unsigned = kind == C_TOKEN_UTF8_CHAR_CONSTANT || kind == C_TOKEN_UTF16_CHAR_CONSTANT || kind == C_TOKEN_UTF32_CHAR_CONSTANT;
    *value = (PPValue){ .bits = (uint64_t)result, .is_unsigned = is_unsigned };
    return valid;
}

// Applies an operator to the values on top of the stack (two for a binary one, three for ?:, which is COLON here).
static bool pp_reduce(Preprocessor* pp, PPValue* values, uint32_t* value_count, uint32_t operator, uint32_t location)
{
    bool result = true;
    const bool unary = operator == PP_OPERATOR_PLUS || operator == PP_OPERATOR_MINUS || operator == PP_OPERATOR_NOT || operator == PP_OPERATOR_COMPLEMENT;
    const uint32_t operands = unary ? 1 : operator == PP_OPERATOR_COLON ? 3 : 2;
    if (*value_count < operands)
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, location, "missing operand in preprocessor expression");
        result = false;
    }
    else if (unary)
    {
        PPValue* v = &values[*value_count - 1];
        switch (operator)
        {
            case PP_OPERATOR_MINUS: v->bits = (uint64_t)0 - v->bits; break;
            case PP_OPERATOR_NOT: v->bits = v->bits == 0; v->is_unsigned = false; break;
            case PP_OPERATOR_COMPLEMENT: v->bits = ~v->bits; break;
        }
    }
    else if (operator == PP_OPERATOR_COLON)
    {
        const PPValue condition = values[*value_count - 3];
        const PPValue yes = values[*value_count - 2];
        const PPValue no = values[*value_count - 1];
        PPValue chosen = condition.bits != 0 ? yes : no;
        chosen.is_unsigned = yes.is_unsigned || no.is_unsigned;
        chosen.division_by_zero |= condition.division_by_zero;
        *value_count -= 2;
        values[*value_count - 1] = chosen;
    }
    else
    {
        const PPValue a = values[*value_count - 2];
        const PPValue b = values[*value_count - 1];
        const bool is_unsigned = a.is_unsigned || b.is_unsigned;
        PPValue r = { .is_unsigned = is_unsigned, .division_by_zero = a.division_by_zero || b.division_by_zero };
        const int64_t sa = (int64_t)a.bits;
        const int64_t sb = (int64_t)b.bits;
        switch (operator)
        {
            case PP_OPERATOR_MULTIPLY: r.bits = a.bits * b.bits; break;
            case PP_OPERATOR_DIVIDE:
            case PP_OPERATOR_REMAINDER:
                if (b.bits == 0)
                {
                    r.division_by_zero = true;
                }
                else if (is_unsigned)
                {
                    r.bits = operator == PP_OPERATOR_DIVIDE ? a.bits / b.bits : a.bits % b.bits;
                }
                else if (sa == INT64_MIN && sb == -1)
                {
                    r.bits = operator == PP_OPERATOR_DIVIDE ? a.bits : 0;
                }
                else
                {
                    r.bits = (uint64_t)(operator == PP_OPERATOR_DIVIDE ? sa / sb : sa % sb);
                }
                break;
            case PP_OPERATOR_ADD: r.bits = a.bits + b.bits; break;
            case PP_OPERATOR_SUBTRACT: r.bits = a.bits - b.bits; break;
            case PP_OPERATOR_SHIFT_LEFT:
                r.is_unsigned = a.is_unsigned;
                r.bits = b.bits >= 64 ? 0 : a.bits << b.bits;
                break;
            case PP_OPERATOR_SHIFT_RIGHT:
                r.is_unsigned = a.is_unsigned;
                r.bits = b.bits >= 64 ? (a.is_unsigned || sa >= 0 ? 0 : ~0ull) : a.is_unsigned ? a.bits >> b.bits : (uint64_t)(sa >> b.bits);
                break;
            case PP_OPERATOR_LESS: r.bits = is_unsigned ? a.bits < b.bits : sa < sb; r.is_unsigned = false; break;
            case PP_OPERATOR_LESS_EQUAL: r.bits = is_unsigned ? a.bits <= b.bits : sa <= sb; r.is_unsigned = false; break;
            case PP_OPERATOR_GREATER: r.bits = is_unsigned ? a.bits > b.bits : sa > sb; r.is_unsigned = false; break;
            case PP_OPERATOR_GREATER_EQUAL: r.bits = is_unsigned ? a.bits >= b.bits : sa >= sb; r.is_unsigned = false; break;
            case PP_OPERATOR_EQUAL: r.bits = a.bits == b.bits; r.is_unsigned = false; break;
            case PP_OPERATOR_NOT_EQUAL: r.bits = a.bits != b.bits; r.is_unsigned = false; break;
            case PP_OPERATOR_AND: r.bits = a.bits & b.bits; break;
            case PP_OPERATOR_XOR: r.bits = a.bits ^ b.bits; break;
            case PP_OPERATOR_OR: r.bits = a.bits | b.bits; break;
            case PP_OPERATOR_LOGICAL_AND:
                r.bits = a.bits != 0 && b.bits != 0;
                r.is_unsigned = false;
                r.division_by_zero = a.division_by_zero || (a.bits != 0 && b.division_by_zero);
                break;
            case PP_OPERATOR_LOGICAL_OR:
                r.bits = a.bits != 0 || b.bits != 0;
                r.is_unsigned = false;
                r.division_by_zero = a.division_by_zero || (a.bits == 0 && b.division_by_zero);
                break;
            case PP_OPERATOR_COMMA:
                r = b;
                r.division_by_zero |= a.division_by_zero;
                break;
            default:
                pp_diagnose(pp, PP_SEVERITY_ERROR, location, "'?' without ':' in preprocessor expression");
                result = false;
                break;
        }
        *value_count -= 1;
        values[*value_count - 1] = r;
    }
    return result;
}

// Evaluates a #if line's expanded tokens (C 6.10.1): a precedence parser with explicit operator and value stacks, where
// an identifier left after expansion is 0 (true is 1 from C23 on). Returns false for zero or an error.
static bool pp_evaluate(Preprocessor* pp, const PPTokens* tokens, uint32_t begin, uint32_t end, uint32_t location)
{
    const uint32_t saved_origin = pp->diagnostic_origin;
    PPValue values[BUSTER_PP_EXPRESSION_DEPTH];
    uint8_t operators[BUSTER_PP_EXPRESSION_DEPTH];
    uint32_t value_count = 0;
    uint32_t operator_count = 0;
    bool valid = true;
    bool expect_operand = true;
    for (uint32_t i = begin; valid && i < end; i += 1)
    {
        const uint8_t kind = tokens->kinds[i];
        const uint32_t token_location = tokens->locations[i];
        pp->diagnostic_origin = tokens->origins != 0 ? tokens->origins[i] : 0;
        valid = value_count < BUSTER_PP_EXPRESSION_DEPTH && operator_count < BUSTER_PP_EXPRESSION_DEPTH;
        if (!valid)
        {
            pp_diagnose(pp, PP_SEVERITY_ERROR, token_location, "preprocessor expression nests too deeply");
        }
        else if (expect_operand)
        {
            if (kind == C_TOKEN_PLUS || kind == C_TOKEN_MINUS || kind == C_TOKEN_EXCLAIM || kind == C_TOKEN_TILDE)
            {
                operators[operator_count] = kind == C_TOKEN_PLUS ? PP_OPERATOR_PLUS : kind == C_TOKEN_MINUS ? PP_OPERATOR_MINUS : kind == C_TOKEN_EXCLAIM ? PP_OPERATOR_NOT : PP_OPERATOR_COMPLEMENT;
                operator_count += 1;
            }
            else if (kind == C_TOKEN_L_PAREN)
            {
                operators[operator_count] = PP_OPERATOR_PAREN;
                operator_count += 1;
            }
            else if (kind == C_TOKEN_NUMERIC_CONSTANT || kind == BUSTER_PP_KIND_INTEGER)
            {
                uint8_t buffer[256];
                const uint8_t* spelling;
                const uint32_t length = tokens->values[i] - token_location;
                if (kind == BUSTER_PP_KIND_INTEGER)
                {
                    values[value_count] = (PPValue){ .bits = tokens->values[i] };
                }
                else if (length < sizeof(buffer))
                {
                    const uint32_t n = pp_spelling(pp, kind, tokens->values[i], token_location, buffer, &spelling);
                    valid = pp_parse_integer(pp, spelling, n, token_location, &values[value_count]);
                }
                else
                {
                    pp_diagnose(pp, PP_SEVERITY_ERROR, token_location, "integer literal is too large to be represented in any integer type");
                    valid = false;
                }
                value_count += 1;
                expect_operand = false;
            }
            else if (kind >= C_TOKEN_CHAR_CONSTANT && kind <= C_TOKEN_UTF32_CHAR_CONSTANT)
            {
                Scratch scratch = scratch_begin(0);
                uint8_t* buffer = arena_push(scratch.arena, tokens->values[i] - token_location + 1, 1);
                const uint8_t* spelling;
                const uint32_t n = buffer != 0 ? pp_spelling(pp, kind, tokens->values[i], token_location, buffer, &spelling) : 0;
                valid = buffer != 0 && pp_parse_character(pp, kind, spelling, n, token_location, &values[value_count]);
                scratch_end(scratch);
                value_count += 1;
                expect_operand = false;
            }
            else if (kind == C_TOKEN_RAW_IDENTIFIER)
            {
                // An identifier left after expansion is 0; in C23, true is 1 (and false 0).
                const bool is_true = tokens->values[i] == C_NAME_TRUE && pp->options.language.standard >= 2023;
                values[value_count] = (PPValue){ .bits = is_true };
                value_count += 1;
                expect_operand = false;
            }
            else
            {
                pp_diagnose(pp, PP_SEVERITY_ERROR, token_location, "invalid token at start of a preprocessor expression");
                valid = false;
            }
        }
        else if (kind == C_TOKEN_R_PAREN)
        {
            while (valid && operator_count != 0 && operators[operator_count - 1] != PP_OPERATOR_PAREN)
            {
                operator_count -= 1;
                valid = pp_reduce(pp, values, &value_count, operators[operator_count], token_location);
            }
            if (valid && operator_count == 0)
            {
                pp_diagnose(pp, PP_SEVERITY_ERROR, token_location, "unbalanced ')' in preprocessor expression");
                valid = false;
            }
            operator_count -= valid;
        }
        else
        {
            const uint32_t operator = pp_binary_operator(kind);
            if (operator == BUSTER_PP_NONE)
            {
                pp_diagnose(pp, PP_SEVERITY_ERROR, token_location, "token is not a valid binary operator in a preprocessor subexpression");
                valid = false;
            }
            else
            {
                const uint8_t precedence = pp_operator_precedence[operator];
                // Left to right, but ?: from right to left: reduce what binds at least as tightly (more tightly for ?).
                while (valid && operator_count != 0 && operators[operator_count - 1] != PP_OPERATOR_PAREN &&
                       (pp_operator_precedence[operators[operator_count - 1]] > precedence ||
                        (pp_operator_precedence[operators[operator_count - 1]] == precedence && operator != PP_OPERATOR_QUESTION && operator != PP_OPERATOR_COLON)))
                {
                    if (operators[operator_count - 1] == PP_OPERATOR_QUESTION)
                    {
                        break;
                    }
                    operator_count -= 1;
                    valid = pp_reduce(pp, values, &value_count, operators[operator_count], token_location);
                }
                if (valid && operator == PP_OPERATOR_COLON)
                {
                    // The ':' completes the innermost '?': it becomes the ternary operator.
                    while (valid && operator_count != 0 && operators[operator_count - 1] != PP_OPERATOR_QUESTION && operators[operator_count - 1] != PP_OPERATOR_PAREN)
                    {
                        operator_count -= 1;
                        valid = pp_reduce(pp, values, &value_count, operators[operator_count], token_location);
                    }
                    if (valid && (operator_count == 0 || operators[operator_count - 1] != PP_OPERATOR_QUESTION))
                    {
                        pp_diagnose(pp, PP_SEVERITY_ERROR, token_location, "':' without '?' in preprocessor expression");
                        valid = false;
                    }
                    if (valid)
                    {
                        operators[operator_count - 1] = PP_OPERATOR_COLON;
                    }
                }
                else if (valid)
                {
                    operators[operator_count] = (uint8_t)operator;
                    operator_count += 1;
                }
                expect_operand = true;
            }
        }
    }
    pp->diagnostic_origin = saved_origin;
    if (valid && expect_operand)
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, location, value_count == 0 && operator_count == 0 ? "#if with no expression" : "expected value in expression");
        valid = false;
    }
    while (valid && operator_count != 0)
    {
        operator_count -= 1;
        if (operators[operator_count] == PP_OPERATOR_PAREN || operators[operator_count] == PP_OPERATOR_QUESTION)
        {
            pp_diagnose(pp, PP_SEVERITY_ERROR, location, operators[operator_count] == PP_OPERATOR_PAREN ? "missing ')' in expression" : "'?' without ':' in preprocessor expression");
            valid = false;
        }
        else
        {
            valid = pp_reduce(pp, values, &value_count, operators[operator_count], location);
        }
    }
    if (valid && value_count != 1)
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, location, "malformed preprocessor expression");
        valid = false;
    }
    if (valid && values[0].division_by_zero)
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, location, "division by zero in preprocessor expression");
        valid = false;
    }
    return valid && values[0].bits != 0;
}

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
    { "nocf_check", 1 }, { "nodebug", 1 }, { "noderef", 1 }, { "noescape", 1 }, { "noinline", 1 }, { "nomerge", 1 }, { "nonallocating", 1 },
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
    { "gnu::nocf_check", 1 }, { "gnu::noinline", 1 }, { "gnu::nonnull", 1 }, { "gnu::nonstring", 1 }, { "gnu::noreturn", 1 }, { "gnu::nothrow", 1 },
    { "gnu::packed", 1 }, { "gnu::patchable_function_entry", 1 }, { "gnu::pure", 1 }, { "gnu::regcall", 1 }, { "gnu::retain", 1 },
    { "gnu::returns_nonnull", 1 }, { "gnu::returns_twice", 1 }, { "gnu::section", 1 }, { "gnu::sentinel", 1 }, { "gnu::sysv_abi", 1 },
    { "gnu::target", 1 }, { "gnu::target_clones", 1 }, { "gnu::tls_model", 1 }, { "gnu::transparent_union", 1 }, { "gnu::unused", 1 },
    { "gnu::used", 1 }, { "gnu::vector_size", 1 }, { "gnu::visibility", 1 }, { "gnu::warn_unused_result", 1 }, { "gnu::warning", 1 },
    { "gnu::weak", 1 }, { "gnu::weakref", 1 }, { "gnu::zero_call_used_regs", 1 }, { "maybe_unused", 202106 }, { "nodiscard", 202003 },
    { "noreturn", 202202 },
};
static const PPFeature pp_features_warning[] = {
    { "-Warray-parameter", 1 }, { "-Wbitwise-instead-of-logical", 1 }, { "-Wc++98-compat-extra-semi", 1 }, { "-Wcharacter-conversion", 1 },
    { "-Wdeprecated-pragma", 1 }, { "-Wenum-compare-switch", 1 }, { "-Wextra-semi-stmt", 1 }, { "-Wformat-insufficient-args", 1 },
    { "-Wimplicit-const-int-float-conversion", 1 }, { "-Wmisleading-indentation", 1 }, { "-Woverriding-option", 1 }, { "-Wreserved-identifier", 1 },
    { "-Wreturn-mismatch", 1 }, { "-Wunnecessary-virtual-specifier", 1 }, { "-Wunused-but-set-variable", 1 }, { "-Wuser-defined-warnings", 1 },
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

// What buster implements, which the feature queries answer outside the oracle's mode (#154: until everything clang
// supports is, they say exactly what is). Only the preprocessor exists yet, so of builtins only its __is_target_*
// (which clang's __has_builtin answers 1 for too), and no attribute, feature or extension: the parser, the checker and
// the code generators add theirs here as they implement them (#127, #133, #147, #148). __has_warning and
// __has_declspec_attribute answer as clang does in both modes. Sorted for binary search.
static const PPFeature pp_implemented_builtin[] = {
    { "__is_target_arch", 1 }, { "__is_target_environment", 1 }, { "__is_target_os", 1 }, { "__is_target_vendor", 1 },
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
            result = clang ? pp_feature_lookup(pp_features_attribute, BUSTER_ARRAY_LENGTH(pp_features_attribute), name, length, standard) : 0;
            break;
        case PP_BUILTIN_HAS_BUILTIN:
            result = clang ? pp_feature_lookup(pp_features_builtin, BUSTER_ARRAY_LENGTH(pp_features_builtin), name, length, standard)
                           : pp_feature_lookup(pp_implemented_builtin, BUSTER_ARRAY_LENGTH(pp_implemented_builtin), name, length, standard);
            break;
        case PP_BUILTIN_HAS_CONSTEXPR_BUILTIN:
            result = clang ? pp_feature_lookup(pp_features_constexpr_builtin, BUSTER_ARRAY_LENGTH(pp_features_constexpr_builtin), name, length, standard) : 0;
            break;
        case PP_BUILTIN_HAS_FEATURE:
            result = clang ? pp_feature_lookup(pp_features_feature, BUSTER_ARRAY_LENGTH(pp_features_feature), name, length, standard) | sanitizer : 0;
            break;
        case PP_BUILTIN_HAS_EXTENSION:
            // An extension is anything that is a feature, or in the extension table.
            result = clang ? pp_feature_lookup(pp_features_feature, BUSTER_ARRAY_LENGTH(pp_features_feature), name, length, standard) |
                                 pp_feature_lookup(pp_features_extension, BUSTER_ARRAY_LENGTH(pp_features_extension), name, length, standard) | sanitizer
                           : 0;
            break;
        case PP_BUILTIN_HAS_C_ATTRIBUTE:
            result = clang ? pp_feature_lookup(pp_features_c_attribute, BUSTER_ARRAY_LENGTH(pp_features_c_attribute), name, length, standard) : 0;
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

// clang 23's predefined macros for x86_64-pc-linux-gnu in gnu17 (`clang -dM -E -x c /dev/null`), without the ones the
// language mode decides (pp_predefines adds those) and the ones the target and the other options decide
// (c_predefines.c's predefines_text).
static const char pp_predefines_base[] =
    "#define _LP64 1\n"
    "#define __ATOMIC_ACQUIRE 2\n"
    "#define __ATOMIC_ACQ_REL 4\n"
    "#define __ATOMIC_CONSUME 1\n"
    "#define __ATOMIC_RELAXED 0\n"
    "#define __ATOMIC_RELEASE 3\n"
    "#define __ATOMIC_SEQ_CST 5\n"
    "#define __BIGGEST_ALIGNMENT__ 16\n"
    "#define __BITINT_MAXWIDTH__ 8388608\n"
    "#define __BOOL_WIDTH__ 1\n"
    "#define __BYTE_ORDER__ __ORDER_LITTLE_ENDIAN__\n"
    "#define __CHAR16_TYPE__ unsigned short\n"
    "#define __CHAR32_TYPE__ unsigned int\n"
    "#define __CHAR_BIT__ 8\n"
    "#define __CLANG_ATOMIC_BOOL_LOCK_FREE 2\n"
    "#define __CLANG_ATOMIC_CHAR16_T_LOCK_FREE 2\n"
    "#define __CLANG_ATOMIC_CHAR32_T_LOCK_FREE 2\n"
    "#define __CLANG_ATOMIC_CHAR_LOCK_FREE 2\n"
    "#define __CLANG_ATOMIC_INT_LOCK_FREE 2\n"
    "#define __CLANG_ATOMIC_LLONG_LOCK_FREE 2\n"
    "#define __CLANG_ATOMIC_LONG_LOCK_FREE 2\n"
    "#define __CLANG_ATOMIC_POINTER_LOCK_FREE 2\n"
    "#define __CLANG_ATOMIC_SHORT_LOCK_FREE 2\n"
    "#define __CLANG_ATOMIC_WCHAR_T_LOCK_FREE 2\n"
    "#define __DBL_DECIMAL_DIG__ 17\n"
    "#define __DBL_DENORM_MIN__ 4.9406564584124654e-324\n"
    "#define __DBL_DIG__ 15\n"
    "#define __DBL_EPSILON__ 2.2204460492503131e-16\n"
    "#define __DBL_HAS_DENORM__ 1\n"
    "#define __DBL_HAS_INFINITY__ 1\n"
    "#define __DBL_HAS_QUIET_NAN__ 1\n"
    "#define __DBL_MANT_DIG__ 53\n"
    "#define __DBL_MAX_10_EXP__ 308\n"
    "#define __DBL_MAX_EXP__ 1024\n"
    "#define __DBL_MAX__ 1.7976931348623157e+308\n"
    "#define __DBL_MIN_10_EXP__ (-307)\n"
    "#define __DBL_MIN_EXP__ (-1021)\n"
    "#define __DBL_MIN__ 2.2250738585072014e-308\n"
    "#define __DBL_NORM_MAX__ 1.7976931348623157e+308\n"
    "#define __DECIMAL_DIG__ __LDBL_DECIMAL_DIG__\n"
    "#define __ELF__ 1\n"
    "#define __FLOAT128__ 1\n"
    "#define __FLT_DECIMAL_DIG__ 9\n"
    "#define __FLT_DENORM_MIN__ 1.40129846e-45F\n"
    "#define __FLT_DIG__ 6\n"
    "#define __FLT_EPSILON__ 1.19209290e-7F\n"
    "#define __FLT_HAS_DENORM__ 1\n"
    "#define __FLT_HAS_INFINITY__ 1\n"
    "#define __FLT_HAS_QUIET_NAN__ 1\n"
    "#define __FLT_MANT_DIG__ 24\n"
    "#define __FLT_MAX_10_EXP__ 38\n"
    "#define __FLT_MAX_EXP__ 128\n"
    "#define __FLT_MAX__ 3.40282347e+38F\n"
    "#define __FLT_MIN_10_EXP__ (-37)\n"
    "#define __FLT_MIN_EXP__ (-125)\n"
    "#define __FLT_MIN__ 1.17549435e-38F\n"
    "#define __FLT_NORM_MAX__ 3.40282347e+38F\n"
    "#define __FLT_RADIX__ 2\n"
    "#define __FPCLASS_NEGINF 0x0004\n"
    "#define __FPCLASS_NEGNORMAL 0x0008\n"
    "#define __FPCLASS_NEGSUBNORMAL 0x0010\n"
    "#define __FPCLASS_NEGZERO 0x0020\n"
    "#define __FPCLASS_POSINF 0x0200\n"
    "#define __FPCLASS_POSNORMAL 0x0100\n"
    "#define __FPCLASS_POSSUBNORMAL 0x0080\n"
    "#define __FPCLASS_POSZERO 0x0040\n"
    "#define __FPCLASS_QNAN 0x0002\n"
    "#define __FPCLASS_SNAN 0x0001\n"
    "#define __GCC_ASM_FLAG_OUTPUTS__ 1\n"
    "#define __GCC_CONSTRUCTIVE_SIZE 64\n"
    "#define __GCC_DESTRUCTIVE_SIZE 64\n"
    "#define __GCC_HAVE_SYNC_COMPARE_AND_SWAP_1 1\n"
    "#define __GCC_HAVE_SYNC_COMPARE_AND_SWAP_2 1\n"
    "#define __GCC_HAVE_SYNC_COMPARE_AND_SWAP_4 1\n"
    "#define __GCC_HAVE_SYNC_COMPARE_AND_SWAP_8 1\n"
    "#define __INT16_C(c) c\n"
    "#define __INT16_C_SUFFIX__\n"
    "#define __INT16_FMTd__ \"hd\"\n"
    "#define __INT16_FMTi__ \"hi\"\n"
    "#define __INT16_MAX__ 32767\n"
    "#define __INT16_TYPE__ short\n"
    "#define __INT32_C(c) c\n"
    "#define __INT32_C_SUFFIX__\n"
    "#define __INT32_FMTd__ \"d\"\n"
    "#define __INT32_FMTi__ \"i\"\n"
    "#define __INT32_MAX__ 2147483647\n"
    "#define __INT32_TYPE__ int\n"
    "#define __INT64_C(c) c##L\n"
    "#define __INT64_C_SUFFIX__ L\n"
    "#define __INT64_FMTd__ \"ld\"\n"
    "#define __INT64_FMTi__ \"li\"\n"
    "#define __INT64_MAX__ 9223372036854775807L\n"
    "#define __INT64_TYPE__ long int\n"
    "#define __INT8_C(c) c\n"
    "#define __INT8_C_SUFFIX__\n"
    "#define __INT8_FMTd__ \"hhd\"\n"
    "#define __INT8_FMTi__ \"hhi\"\n"
    "#define __INT8_MAX__ 127\n"
    "#define __INT8_TYPE__ signed char\n"
    "#define __INTMAX_C(c) c##L\n"
    "#define __INTMAX_C_SUFFIX__ L\n"
    "#define __INTMAX_FMTd__ \"ld\"\n"
    "#define __INTMAX_FMTi__ \"li\"\n"
    "#define __INTMAX_MAX__ 9223372036854775807L\n"
    "#define __INTMAX_TYPE__ long int\n"
    "#define __INTMAX_WIDTH__ 64\n"
    "#define __INTPTR_FMTd__ \"ld\"\n"
    "#define __INTPTR_FMTi__ \"li\"\n"
    "#define __INTPTR_MAX__ 9223372036854775807L\n"
    "#define __INTPTR_TYPE__ long int\n"
    "#define __INTPTR_WIDTH__ 64\n"
    "#define __INT_FAST16_FMTd__ \"hd\"\n"
    "#define __INT_FAST16_FMTi__ \"hi\"\n"
    "#define __INT_FAST16_MAX__ 32767\n"
    "#define __INT_FAST16_TYPE__ short\n"
    "#define __INT_FAST16_WIDTH__ 16\n"
    "#define __INT_FAST32_FMTd__ \"d\"\n"
    "#define __INT_FAST32_FMTi__ \"i\"\n"
    "#define __INT_FAST32_MAX__ 2147483647\n"
    "#define __INT_FAST32_TYPE__ int\n"
    "#define __INT_FAST32_WIDTH__ 32\n"
    "#define __INT_FAST64_FMTd__ \"ld\"\n"
    "#define __INT_FAST64_FMTi__ \"li\"\n"
    "#define __INT_FAST64_MAX__ 9223372036854775807L\n"
    "#define __INT_FAST64_TYPE__ long int\n"
    "#define __INT_FAST64_WIDTH__ 64\n"
    "#define __INT_FAST8_FMTd__ \"hhd\"\n"
    "#define __INT_FAST8_FMTi__ \"hhi\"\n"
    "#define __INT_FAST8_MAX__ 127\n"
    "#define __INT_FAST8_TYPE__ signed char\n"
    "#define __INT_FAST8_WIDTH__ 8\n"
    "#define __INT_LEAST16_FMTd__ \"hd\"\n"
    "#define __INT_LEAST16_FMTi__ \"hi\"\n"
    "#define __INT_LEAST16_MAX__ 32767\n"
    "#define __INT_LEAST16_TYPE__ short\n"
    "#define __INT_LEAST16_WIDTH__ 16\n"
    "#define __INT_LEAST32_FMTd__ \"d\"\n"
    "#define __INT_LEAST32_FMTi__ \"i\"\n"
    "#define __INT_LEAST32_MAX__ 2147483647\n"
    "#define __INT_LEAST32_TYPE__ int\n"
    "#define __INT_LEAST32_WIDTH__ 32\n"
    "#define __INT_LEAST64_FMTd__ \"ld\"\n"
    "#define __INT_LEAST64_FMTi__ \"li\"\n"
    "#define __INT_LEAST64_MAX__ 9223372036854775807L\n"
    "#define __INT_LEAST64_TYPE__ long int\n"
    "#define __INT_LEAST64_WIDTH__ 64\n"
    "#define __INT_LEAST8_FMTd__ \"hhd\"\n"
    "#define __INT_LEAST8_FMTi__ \"hhi\"\n"
    "#define __INT_LEAST8_MAX__ 127\n"
    "#define __INT_LEAST8_TYPE__ signed char\n"
    "#define __INT_LEAST8_WIDTH__ 8\n"
    "#define __INT_MAX__ 2147483647\n"
    "#define __INT_WIDTH__ 32\n"
    "#define __LITTLE_ENDIAN__ 1\n"
    "#define __LLONG_WIDTH__ 64\n"
    "#define __LONG_LONG_MAX__ 9223372036854775807LL\n"
    "#define __LONG_MAX__ 9223372036854775807L\n"
    "#define __LONG_WIDTH__ 64\n"
    "#define __LP64__ 1\n"
    "#define __MEMORY_SCOPE_CLUSTR 5\n"
    "#define __MEMORY_SCOPE_DEVICE 1\n"
    "#define __MEMORY_SCOPE_SINGLE 4\n"
    "#define __MEMORY_SCOPE_SYSTEM 0\n"
    "#define __MEMORY_SCOPE_WRKGRP 2\n"
    "#define __MEMORY_SCOPE_WVFRNT 3\n"
    "#define __NO_MATH_INLINES 1\n"
    "#define __OBJC_BOOL_IS_BOOL 0\n"
    "#define __OPENCL_MEMORY_SCOPE_ALL_SVM_DEVICES 3\n"
    "#define __OPENCL_MEMORY_SCOPE_DEVICE 2\n"
    "#define __OPENCL_MEMORY_SCOPE_SUB_GROUP 4\n"
    "#define __OPENCL_MEMORY_SCOPE_WORK_GROUP 1\n"
    "#define __OPENCL_MEMORY_SCOPE_WORK_ITEM 0\n"
    "#define __ORDER_BIG_ENDIAN__ 4321\n"
    "#define __ORDER_LITTLE_ENDIAN__ 1234\n"
    "#define __ORDER_PDP_ENDIAN__ 3412\n"
    "#define __POINTER_WIDTH__ 64\n"
    "#define __PRAGMA_REDEFINE_EXTNAME 1\n"
    "#define __PTRDIFF_FMTd__ \"ld\"\n"
    "#define __PTRDIFF_FMTi__ \"li\"\n"
    "#define __PTRDIFF_MAX__ 9223372036854775807L\n"
    "#define __PTRDIFF_TYPE__ long int\n"
    "#define __PTRDIFF_WIDTH__ 64\n"
    "#define __REGISTER_PREFIX__\n"
    "#define __SCHAR_MAX__ 127\n"
    "#define __SEG_FS 1\n"
    "#define __SEG_GS 1\n"
    "#define __SHRT_MAX__ 32767\n"
    "#define __SHRT_WIDTH__ 16\n"
    "#define __SIG_ATOMIC_MAX__ 2147483647\n"
    "#define __SIG_ATOMIC_MIN__ (-__SIG_ATOMIC_MAX__ - 1)\n"
    "#define __SIG_ATOMIC_TYPE__ int\n"
    "#define __SIG_ATOMIC_WIDTH__ 32\n"
    "#define __SIZEOF_DOUBLE__ 8\n"
    "#define __SIZEOF_FLOAT128__ 16\n"
    "#define __SIZEOF_FLOAT__ 4\n"
    "#define __SIZEOF_INT128__ 16\n"
    "#define __SIZEOF_INT__ 4\n"
    "#define __SIZEOF_LONG_LONG__ 8\n"
    "#define __SIZEOF_LONG__ 8\n"
    "#define __SIZEOF_POINTER__ 8\n"
    "#define __SIZEOF_PTRDIFF_T__ 8\n"
    "#define __SIZEOF_SHORT__ 2\n"
    "#define __SIZEOF_SIZE_T__ 8\n"
    "#define __SIZEOF_WINT_T__ 4\n"
    "#define __SIZE_FMTX__ \"lX\"\n"
    "#define __SIZE_FMTo__ \"lo\"\n"
    "#define __SIZE_FMTu__ \"lu\"\n"
    "#define __SIZE_FMTx__ \"lx\"\n"
    "#define __SIZE_MAX__ 18446744073709551615UL\n"
    "#define __SIZE_TYPE__ long unsigned int\n"
    "#define __SIZE_WIDTH__ 64\n"
    "#define __STDC_EMBED_EMPTY__ 2\n"
    "#define __STDC_EMBED_FOUND__ 1\n"
    "#define __STDC_EMBED_NOT_FOUND__ 0\n"
    "#define __STDC_UTF_16__ 1\n"
    "#define __STDC_UTF_32__ 1\n"
    "#define __STDC__ 1\n"
    "#define __UINT16_C(c) c\n"
    "#define __UINT16_C_SUFFIX__\n"
    "#define __UINT16_FMTX__ \"hX\"\n"
    "#define __UINT16_FMTo__ \"ho\"\n"
    "#define __UINT16_FMTu__ \"hu\"\n"
    "#define __UINT16_FMTx__ \"hx\"\n"
    "#define __UINT16_MAX__ 65535\n"
    "#define __UINT16_TYPE__ unsigned short\n"
    "#define __UINT32_C(c) c##U\n"
    "#define __UINT32_C_SUFFIX__ U\n"
    "#define __UINT32_FMTX__ \"X\"\n"
    "#define __UINT32_FMTo__ \"o\"\n"
    "#define __UINT32_FMTu__ \"u\"\n"
    "#define __UINT32_FMTx__ \"x\"\n"
    "#define __UINT32_MAX__ 4294967295U\n"
    "#define __UINT32_TYPE__ unsigned int\n"
    "#define __UINT64_C(c) c##UL\n"
    "#define __UINT64_C_SUFFIX__ UL\n"
    "#define __UINT64_FMTX__ \"lX\"\n"
    "#define __UINT64_FMTo__ \"lo\"\n"
    "#define __UINT64_FMTu__ \"lu\"\n"
    "#define __UINT64_FMTx__ \"lx\"\n"
    "#define __UINT64_MAX__ 18446744073709551615UL\n"
    "#define __UINT64_TYPE__ long unsigned int\n"
    "#define __UINT8_C(c) c\n"
    "#define __UINT8_C_SUFFIX__\n"
    "#define __UINT8_FMTX__ \"hhX\"\n"
    "#define __UINT8_FMTo__ \"hho\"\n"
    "#define __UINT8_FMTu__ \"hhu\"\n"
    "#define __UINT8_FMTx__ \"hhx\"\n"
    "#define __UINT8_MAX__ 255\n"
    "#define __UINT8_TYPE__ unsigned char\n"
    "#define __UINTMAX_C(c) c##UL\n"
    "#define __UINTMAX_C_SUFFIX__ UL\n"
    "#define __UINTMAX_FMTX__ \"lX\"\n"
    "#define __UINTMAX_FMTo__ \"lo\"\n"
    "#define __UINTMAX_FMTu__ \"lu\"\n"
    "#define __UINTMAX_FMTx__ \"lx\"\n"
    "#define __UINTMAX_MAX__ 18446744073709551615UL\n"
    "#define __UINTMAX_TYPE__ long unsigned int\n"
    "#define __UINTMAX_WIDTH__ 64\n"
    "#define __UINTPTR_FMTX__ \"lX\"\n"
    "#define __UINTPTR_FMTo__ \"lo\"\n"
    "#define __UINTPTR_FMTu__ \"lu\"\n"
    "#define __UINTPTR_FMTx__ \"lx\"\n"
    "#define __UINTPTR_MAX__ 18446744073709551615UL\n"
    "#define __UINTPTR_TYPE__ long unsigned int\n"
    "#define __UINTPTR_WIDTH__ 64\n"
    "#define __UINT_FAST16_FMTX__ \"hX\"\n"
    "#define __UINT_FAST16_FMTo__ \"ho\"\n"
    "#define __UINT_FAST16_FMTu__ \"hu\"\n"
    "#define __UINT_FAST16_FMTx__ \"hx\"\n"
    "#define __UINT_FAST16_MAX__ 65535\n"
    "#define __UINT_FAST16_TYPE__ unsigned short\n"
    "#define __UINT_FAST32_FMTX__ \"X\"\n"
    "#define __UINT_FAST32_FMTo__ \"o\"\n"
    "#define __UINT_FAST32_FMTu__ \"u\"\n"
    "#define __UINT_FAST32_FMTx__ \"x\"\n"
    "#define __UINT_FAST32_MAX__ 4294967295U\n"
    "#define __UINT_FAST32_TYPE__ unsigned int\n"
    "#define __UINT_FAST64_FMTX__ \"lX\"\n"
    "#define __UINT_FAST64_FMTo__ \"lo\"\n"
    "#define __UINT_FAST64_FMTu__ \"lu\"\n"
    "#define __UINT_FAST64_FMTx__ \"lx\"\n"
    "#define __UINT_FAST64_MAX__ 18446744073709551615UL\n"
    "#define __UINT_FAST64_TYPE__ long unsigned int\n"
    "#define __UINT_FAST8_FMTX__ \"hhX\"\n"
    "#define __UINT_FAST8_FMTo__ \"hho\"\n"
    "#define __UINT_FAST8_FMTu__ \"hhu\"\n"
    "#define __UINT_FAST8_FMTx__ \"hhx\"\n"
    "#define __UINT_FAST8_MAX__ 255\n"
    "#define __UINT_FAST8_TYPE__ unsigned char\n"
    "#define __UINT_LEAST16_FMTX__ \"hX\"\n"
    "#define __UINT_LEAST16_FMTo__ \"ho\"\n"
    "#define __UINT_LEAST16_FMTu__ \"hu\"\n"
    "#define __UINT_LEAST16_FMTx__ \"hx\"\n"
    "#define __UINT_LEAST16_MAX__ 65535\n"
    "#define __UINT_LEAST16_TYPE__ unsigned short\n"
    "#define __UINT_LEAST32_FMTX__ \"X\"\n"
    "#define __UINT_LEAST32_FMTo__ \"o\"\n"
    "#define __UINT_LEAST32_FMTu__ \"u\"\n"
    "#define __UINT_LEAST32_FMTx__ \"x\"\n"
    "#define __UINT_LEAST32_MAX__ 4294967295U\n"
    "#define __UINT_LEAST32_TYPE__ unsigned int\n"
    "#define __UINT_LEAST64_FMTX__ \"lX\"\n"
    "#define __UINT_LEAST64_FMTo__ \"lo\"\n"
    "#define __UINT_LEAST64_FMTu__ \"lu\"\n"
    "#define __UINT_LEAST64_FMTx__ \"lx\"\n"
    "#define __UINT_LEAST64_MAX__ 18446744073709551615UL\n"
    "#define __UINT_LEAST64_TYPE__ long unsigned int\n"
    "#define __UINT_LEAST8_FMTX__ \"hhX\"\n"
    "#define __UINT_LEAST8_FMTo__ \"hho\"\n"
    "#define __UINT_LEAST8_FMTu__ \"hhu\"\n"
    "#define __UINT_LEAST8_FMTx__ \"hhx\"\n"
    "#define __UINT_LEAST8_MAX__ 255\n"
    "#define __UINT_LEAST8_TYPE__ unsigned char\n"
    "#define __USER_LABEL_PREFIX__\n"
    "#define __VERSION__ \"Clang 23.1.3\"\n"
    "#define __WINT_MAX__ 4294967295U\n"
    "#define __WINT_MIN__ 0U\n"
    "#define __WINT_TYPE__ unsigned int\n"
    "#define __WINT_UNSIGNED__ 1\n"
    "#define __WINT_WIDTH__ 32\n"
    "#define __amd64 1\n"
    "#define __amd64__ 1\n"
    "#define __clang__ 1\n"
    "#define __clang_literal_encoding__ \"UTF-8\"\n"
    "#define __clang_major__ 23\n"
    "#define __clang_minor__ 1\n"
    "#define __clang_patchlevel__ 3\n"
    "#define __clang_version__ \"23.1.3 \"\n"
    "#define __gnu_linux__ 1\n"
    "#define __linux 1\n"
    "#define __linux__ 1\n"
    "#define __llvm__ 1\n"
    "#define __seg_fs __attribute__((address_space(257)))\n"
    "#define __seg_gs __attribute__((address_space(256)))\n"
    "#define __unix 1\n"
    "#define __unix__ 1\n"
    "#define __x86_64 1\n"
    "#define __x86_64__ 1\n"
    ;

// What C23 adds.
static const char pp_predefines_c23[] =
    "#define __CHAR8_TYPE__ unsigned char\n"
    "#define __CLANG_ATOMIC_CHAR8_T_LOCK_FREE 2\n"
    "#define __SIZE_FMTB__ \"lB\"\n"
    "#define __SIZE_FMTb__ \"lb\"\n"
    "#define __UINT16_FMTB__ \"hB\"\n"
    "#define __UINT16_FMTb__ \"hb\"\n"
    "#define __UINT32_FMTB__ \"B\"\n"
    "#define __UINT32_FMTb__ \"b\"\n"
    "#define __UINT64_FMTB__ \"lB\"\n"
    "#define __UINT64_FMTb__ \"lb\"\n"
    "#define __UINT8_FMTB__ \"hhB\"\n"
    "#define __UINT8_FMTb__ \"hhb\"\n"
    "#define __UINTMAX_FMTB__ \"lB\"\n"
    "#define __UINTMAX_FMTb__ \"lb\"\n"
    "#define __UINTPTR_FMTB__ \"lB\"\n"
    "#define __UINTPTR_FMTb__ \"lb\"\n"
    "#define __UINT_FAST16_FMTB__ \"hB\"\n"
    "#define __UINT_FAST16_FMTb__ \"hb\"\n"
    "#define __UINT_FAST32_FMTB__ \"B\"\n"
    "#define __UINT_FAST32_FMTb__ \"b\"\n"
    "#define __UINT_FAST64_FMTB__ \"lB\"\n"
    "#define __UINT_FAST64_FMTb__ \"lb\"\n"
    "#define __UINT_FAST8_FMTB__ \"hhB\"\n"
    "#define __UINT_FAST8_FMTb__ \"hhb\"\n"
    "#define __UINT_LEAST16_FMTB__ \"hB\"\n"
    "#define __UINT_LEAST16_FMTb__ \"hb\"\n"
    "#define __UINT_LEAST32_FMTB__ \"B\"\n"
    "#define __UINT_LEAST32_FMTb__ \"b\"\n"
    "#define __UINT_LEAST64_FMTB__ \"lB\"\n"
    "#define __UINT_LEAST64_FMTb__ \"lb\"\n"
    "#define __UINT_LEAST8_FMTB__ \"hhB\"\n"
    "#define __UINT_LEAST8_FMTb__ \"hhb\"\n"
    ;

// The most bytes pp_predefines writes.
#define BUSTER_PP_PREDEFINES_BYTES (sizeof(pp_predefines_base) + sizeof(pp_predefines_c23) + 512 + BUSTER_PREDEFINES_TEXT_BYTES)

// The predefines buffer for the options, into `buffer` (room for BUSTER_PP_PREDEFINES_BYTES): the fixed part, the
// language mode's, the target's and the other options' (predefines_text) and __BUSTER__. *flt_eval_method gets
// __FLT_EVAL_METHOD__'s value. Returns its length, or BUSTER_PP_PREDEFINES_BYTES + 1 after an error in the options,
// reported on stderr.
static uint32_t pp_predefines(const PPOptions* options, char* buffer, uint32_t* flt_eval_method)
{
    uint32_t length = 0;
    memcpy(buffer, pp_predefines_base, sizeof(pp_predefines_base) - 1);
    length += sizeof(pp_predefines_base) - 1;
    // clang pushes these two after defining them, so that a #pragma pop_macro gets them back and -D redefines them
    // without a warning.
    length += (uint32_t)sprintf(buffer + length, "#pragma push_macro(\"__GCC_DESTRUCTIVE_SIZE\")\n#pragma push_macro(\"__GCC_CONSTRUCTIVE_SIZE\")\n");
    const CLanguage language = options->language;
    const char* version = language.standard >= 2023   ? "202311L"
                          : language.standard >= 2017 ? "201710L"
                          : language.standard >= 2011 ? "201112L"
                          : language.standard >= 1999 ? "199901L"
                          : language.standard >= 1994 ? "199409L"
                                                      : 0;
    if (version != 0)
    {
        length += (uint32_t)sprintf(buffer + length, "#define __STDC_VERSION__ %s\n", version);
    }
    if (!language.gnu)
    {
        length += (uint32_t)sprintf(buffer + length, "#define __STRICT_ANSI__ 1\n");
    }
    else
    {
        length += (uint32_t)sprintf(buffer + length, "#define linux 1\n#define unix 1\n");
    }
    length += (uint32_t)sprintf(buffer + length, "#define __STDC_HOSTED__ %d\n", options->freestanding ? 0 : 1);
    if (language.standard >= 2023)
    {
        memcpy(buffer + length, pp_predefines_c23, sizeof(pp_predefines_c23) - 1);
        length += sizeof(pp_predefines_c23) - 1;
    }
    const uint32_t target = predefines_text(&options->predefines, language, options->freestanding, buffer + length, BUSTER_PREDEFINES_TEXT_BYTES, flt_eval_method);
    length = target <= BUSTER_PREDEFINES_TEXT_BYTES ? length + target : BUSTER_PP_PREDEFINES_BYTES + 1;
    // The one macro that isn't clang's (#117: clang's configuration, plus this). --predefines replaces it too, so that
    // test-preprocessor --clang starts from clang's own macros alone.
    length += length <= BUSTER_PP_PREDEFINES_BYTES ? (uint32_t)sprintf(buffer + length, "#define __BUSTER__ 1\n") : 0;
    if (options->assembly && length <= BUSTER_PP_PREDEFINES_BYTES)
    {
        length += (uint32_t)sprintf(buffer + length, "#define __ASSEMBLER__ 1\n");
    }
    return length;
}

// ---------------------------------------------------------------------------------------------------------------------
// Setting up and running

#define BUSTER_PP_ARENA_BYTES (64ull << 30)
#define BUSTER_PP_FILE_SLOTS (2u * BUSTER_PP_MAX_FILES)
#define BUSTER_PP_INITIAL_FILE_SLOTS 1024u

// A file that isn't on disk (the predefines, the command line, the forced includes' lines), at a name in angle brackets,
// and what it is for line markers: clang's predefines are a system header, and the rest of its <built-in> buffer, the
// command line's and the forced includes', isn't. Returns its PPFile.
static uint32_t pp_virtual_file(Preprocessor* pp, const char* name, const char* text, uint32_t length, uint8_t system)
{
    uint32_t result = BUSTER_PP_NONE;
    uint8_t* bytes = arena_push(&pp->arena, (uint64_t)length + 64, 64);
    if (bytes != 0 && pp->data_count < BUSTER_PP_MAX_FILES && pp->file_count < BUSTER_PP_MAX_FILES)
    {
        memcpy(bytes, text, length);
        memset(bytes + length, 0, 64);
        const uint32_t data = pp->data_count;
        pp->datas[data] = (PPFileData){ .device = 0, .inode = UINT64_MAX - data, .bytes = bytes, .length = length, .record = BUSTER_PP_NONE };
        pp->data_count += 1;
        result = pp->file_count;
        pp->files[result] = (PPFile){
            .path = (char*)name,
            .path_length = (uint32_t)strlen(name),
            .directory_length = BUSTER_PP_NONE,
            .data = data,
            .system = system,
        };
        pp->file_count += 1;
    }
    return result;
}

// Runs a file to its end, as the main file or one that runs before it.
static void pp_run_file(Preprocessor* pp, uint32_t file)
{
    const uint32_t depth = pp->context_count;
    pp_enter_file(pp, file, BUSTER_PP_NONE, 0, BUSTER_PP_NONE);
    pp_run_contexts(pp, depth);
}

// The highest version's directory of clang's own headers (clang searches its resource directory first), if any.
static bool pp_clang_resource_directory(char* path, uint32_t capacity)
{
    bool result = false;
    DIR* directory = opendir("/usr/lib/clang");
    uint32_t best = 0;
    if (directory != 0)
    {
        struct dirent* entry;
        while ((entry = readdir(directory)) != 0)
        {
            const uint32_t version = (uint32_t)strtoul(entry->d_name, 0, 10);
            if (version > best)
            {
                best = version;
            }
        }
        closedir(directory);
        if (best != 0)
        {
            snprintf(path, capacity, "/usr/lib/clang/%u/include", best);
            struct stat stats;
            result = stat(path, &stats) == 0 && S_ISDIR(stats.st_mode);
        }
    }
    return result;
}

static bool pp_add_directory(Preprocessor* pp, const char* path, uint8_t system)
{
    uint32_t length = (uint32_t)strlen(path);
    while (length > 1 && path[length - 1] == '/')
    {
        length -= 1;
    }
    char* copy = arena_push(&pp->arena, (uint64_t)length + 1, 1);
    const bool result = copy != 0 && pp->directory_count < BUSTER_PP_MAX_PUSHED;
    if (result)
    {
        memcpy(copy, path, length);
        copy[length] = 0;
        pp->directories[pp->directory_count] = copy;
        pp->directory_lengths[pp->directory_count] = length;
        pp->directory_system[pp->directory_count] = system;
        pp->directory_count += 1;
    }
    return result;
}

// Drops the search directories that don't exist, and the later of two that are the same directory (the same device and
// inode), as clang does (InitHeaderSearch): among the -iquote ones, then among the others, where a directory that is
// both a system one and not drops the one that isn't. So #include_next can't find a file again through a repeated -I.
static void pp_dedupe_directories(Preprocessor* pp)
{
    Scratch scratch = scratch_begin(0);
    const uint32_t count = pp->directory_count;
    uint64_t* devices = arena_push_array(scratch.arena, (uint64_t)count + 1, sizeof(uint64_t), 8);
    uint64_t* inodes = arena_push_array(scratch.arena, (uint64_t)count + 1, sizeof(uint64_t), 8);
    bool* kept = arena_push_array(scratch.arena, (uint64_t)count + 1, sizeof(bool), 8);
    if (devices != 0 && inodes != 0 && kept != 0)
    {
        for (uint32_t i = 0; i < count; i += 1)
        {
            struct stat stats;
            kept[i] = stat(pp->directories[i], &stats) == 0 && S_ISDIR(stats.st_mode);
            devices[i] = kept[i] ? (uint64_t)stats.st_dev : 0;
            inodes[i] = kept[i] ? (uint64_t)stats.st_ino : 0;
        }
        for (uint32_t i = 0; i < count; i += 1)
        {
            const uint32_t group_first = i < pp->angled_first ? 0 : pp->angled_first;
            for (uint32_t j = group_first; kept[i] && j < i; j += 1)
            {
                if (kept[j] && devices[j] == devices[i] && inodes[j] == inodes[i])
                {
                    const bool system_over_user = pp->directory_system[i] && !pp->directory_system[j];
                    kept[j] = !system_over_user;
                    kept[i] = system_over_user;
                }
            }
        }
        uint32_t written = 0;
        uint32_t angled_first = 0;
        for (uint32_t i = 0; i < count; i += 1)
        {
            angled_first += i < pp->angled_first && kept[i];
            if (kept[i])
            {
                pp->directories[written] = pp->directories[i];
                pp->directory_lengths[written] = pp->directory_lengths[i];
                pp->directory_system[written] = pp->directory_system[i];
                written += 1;
            }
        }
        pp->directory_count = written;
        pp->angled_first = angled_first;
    }
    scratch_end(scratch);
}

// An array of `count` records of `size` bytes, in an arena of its own (address space, touched as it is used).
static void* pp_array(Preprocessor* pp, uint64_t count, uint64_t size)
{
    void* result = 0;
    if (pp->array_count < BUSTER_PP_ARRAYS)
    {
        Arena* arena = &pp->arrays[pp->array_count];
        *arena = arena_reserve(count * size + 64);
        result = arena_push_array(arena, count, size, 64);
        pp->array_count += 1;
    }
    return result;
}

static bool pp_initialize(Preprocessor* pp, const PPOptions* options, InternTable* table)
{
    *pp = (Preprocessor){ 0 };
    pp->diagnostic_include_range = BUSTER_PP_NONE;
    pp->options = *options;
    pp->table = table;
    const PPPrepare prepare = pp_prepare_options(options);
    pp->vectorized = prepare.vectorized;
    pp->refine_flags = prepare.refine_flags;
    pp->split_kinds = prepare.split_kinds;
    pp->arena = arena_reserve(BUSTER_PP_ARENA_BYTES);
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(pp->columns); i += 1)
    {
        pp->columns[i] = arena_reserve(BUSTER_PP_ARENA_BYTES);
    }
    pp->scratch[0] = arena_reserve(BUSTER_PP_SCRATCH_BYTES);
    pp->scratch[1] = arena_reserve(BUSTER_PP_SCRATCH_BYTES);
    pp->scratch_range[0] = BUSTER_PP_NONE;
    pp->scratch_range[1] = BUSTER_PP_NONE;
    bool result = pp_tokens_reserve(&pp->output, BUSTER_PP_MAX_OUTPUT_TOKENS) && pp_tokens_reserve(&pp->arguments, BUSTER_PP_MAX_STACK_TOKENS) &&
                  pp_tokens_reserve(&pp->captures, BUSTER_PP_MAX_STACK_TOKENS) && pp_tokens_reserve(&pp->expansions, BUSTER_PP_MAX_STACK_TOKENS);
    pp->ranges = pp_array(pp, BUSTER_PP_MAX_RANGES, sizeof(PPRange));
    pp->datas = pp_array(pp, BUSTER_PP_MAX_FILES, sizeof(PPFileData));
    pp->record_data = pp_array(pp, BUSTER_PP_MAX_FILES, sizeof(uint32_t));
    pp->files = pp_array(pp, BUSTER_PP_MAX_FILES, sizeof(PPFile));
    pp->file_slots = pp_array(pp, BUSTER_PP_FILE_SLOTS, sizeof(uint32_t));
    pp->file_slot_mask = BUSTER_PP_INITIAL_FILE_SLOTS - 1;
    pp->directories = pp_array(pp, BUSTER_PP_MAX_PUSHED, sizeof(char*));
    pp->directory_lengths = pp_array(pp, BUSTER_PP_MAX_PUSHED, sizeof(uint32_t));
    pp->directory_system = pp_array(pp, BUSTER_PP_MAX_PUSHED, sizeof(uint8_t));
    pp->line_directives = pp_array(pp, BUSTER_PP_MAX_LINE_DIRECTIVES, sizeof(PPLineDirective));
    pp->expansion_starts = pp_array(pp, (uint64_t)BUSTER_PP_MAX_OUTPUT_TOKENS + 1, sizeof(PPExpansionStart));
    pp->macro_of = pp_array(pp, BUSTER_INTERN_MAX_IDS, sizeof(PPMacroId));
    pp->is_macro = pp_array(pp, BUSTER_INTERN_MAX_IDS / 64, sizeof(uint64_t));
    pp->macros = pp_array(pp, BUSTER_PP_MAX_MACROS, sizeof(PPMacro));
    pp->items = pp_array(pp, BUSTER_PP_MAX_ITEMS, sizeof(PPItem));
    pp->parameters = pp_array(pp, BUSTER_PP_MAX_ITEMS, sizeof(IdentId));
    pp->pushed_names = pp_array(pp, BUSTER_PP_MAX_PUSHED, sizeof(IdentId));
    pp->pushed_macros = pp_array(pp, BUSTER_PP_MAX_PUSHED, sizeof(PPMacroId));
    pp->contexts = pp_array(pp, BUSTER_PP_MAX_CONTEXTS, sizeof(PPContext));
    pp->conditionals = pp_array(pp, BUSTER_PP_MAX_CONTEXTS, sizeof(PPConditional));
    pp->invocations = pp_array(pp, BUSTER_PP_MAX_CONTEXTS, sizeof(PPInvocation));
    pp->argument_records = pp_array(pp, BUSTER_PP_MAX_STACK_TOKENS, sizeof(PPArgument));
    pp->capture_frames = pp_array(pp, BUSTER_PP_MAX_CONTEXTS, sizeof(PPCapture));
    if (!options->no_history)
    {
        // Five retained tables and three temporary columns share base pages for their first few records. Only a
        // table that outgrows its slice reserves a large arena, avoiding eight mappings/advice calls on small units.
        pp->history_initial = arena_reserve(8 * BUSTER_PP_INITIAL_HISTORY_BYTES);
        pp->history = pp_history_initial(pp, sizeof(PPExpansion), &pp->history_capacity);
        pp->substitutions = pp_history_initial(pp, sizeof(PPSubstitution), &pp->substitution_capacity);
        pp->generated = pp_history_initial(pp, sizeof(PPGenerated), &pp->generated_capacity);
        pp->history_sources = pp_history_initial(pp, sizeof(PPSource), &pp->history_source_capacity);
        pp->history_runs = pp_history_initial(pp, sizeof(PPHistoryRun), &pp->history_run_capacity);
        PPTokens* buffers[] = { &pp->arguments, &pp->captures, &pp->expansions };
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(buffers); i += 1)
        {
            PPTokens* tokens = buffers[i];
            tokens->origins = pp_history_initial(pp, sizeof(uint32_t), &tokens->origin_capacity);
            result = result && tokens->origins != 0;
        }
        result = result && pp->history != 0 && pp->substitutions != 0 && pp->generated != 0 && pp->history_sources != 0 && pp->history_runs != 0;
    }
    // The file cache: the options', made for the same language mode and intern table, or one of the unit's own.
    pp->cache = options->files;
    if (pp->cache == 0)
    {
        pp->cache = arena_push(&pp->arena, sizeof(FileCache), 64);
        pp->owns_cache = pp->cache != 0;
        result = result && pp->cache != 0 && file_cache_initialize(pp->cache, table, pp_prepare_config(pp));
    }
    else if (pp->cache->table != table || pp->cache->prepare.refine_flags != pp->refine_flags || pp->cache->prepare.split_kinds != pp->split_kinds ||
             pp->cache->prepare.assembly != (bool)options->assembly)
    {
        fprintf(stderr, "error: the file cache was made for another language mode or intern table\n");
        result = false;
    }
    result = result && pp->ranges != 0 && pp->datas != 0 && pp->record_data != 0 && pp->files != 0 && pp->file_slots != 0 && pp->directories != 0 && pp->directory_lengths != 0 &&
             pp->directory_system != 0 && pp->line_directives != 0 && pp->expansion_starts != 0 && pp->macro_of != 0 && pp->is_macro != 0 && pp->macros != 0 && pp->items != 0 &&
             pp->parameters != 0 && pp->pushed_names != 0 && pp->pushed_macros != 0 && pp->contexts != 0 && pp->conditionals != 0 && pp->invocations != 0 &&
             pp->argument_records != 0 && pp->capture_frames != 0 && pp->scratch[0].base != 0 && pp->scratch[1].base != 0;
    if (result)
    {
        // Macro 0 is none; location 0 is none.
        pp->macro_count = 1;
        pp->location_top = 1;
        // The search directories: -iquote ones for quoted names only, then -I, -isystem, the standard ones and -idirafter.
        for (uint32_t i = 0; result && i < options->quote_directory_count; i += 1)
        {
            result = pp_add_directory(pp, options->quote_directories[i], BUSTER_PP_USER_FILE);
        }
        pp->angled_first = pp->directory_count;
        for (uint32_t i = 0; result && i < options->include_directory_count; i += 1)
        {
            result = pp_add_directory(pp, options->include_directories[i], BUSTER_PP_USER_FILE);
        }
        for (uint32_t i = 0; result && i < options->system_directory_count; i += 1)
        {
            result = pp_add_directory(pp, options->system_directories[i], BUSTER_PP_SYSTEM_FILE);
        }
        char resource[256];
        if (result && !options->no_standard_includes)
        {
            if (pp_clang_resource_directory(resource, sizeof(resource)))
            {
                result = pp_add_directory(pp, resource, BUSTER_PP_SYSTEM_FILE);
            }
            result = result && pp_add_directory(pp, "/usr/local/include", BUSTER_PP_EXTERN_C_FILE) && pp_add_directory(pp, "/usr/include", BUSTER_PP_EXTERN_C_FILE);
        }
        for (uint32_t i = 0; result && i < options->after_directory_count; i += 1)
        {
            result = pp_add_directory(pp, options->after_directories[i], BUSTER_PP_SYSTEM_FILE);
        }
        if (result)
        {
            pp_dedupe_directories(pp);
        }
        // The builtin macros.
        static const struct
        {
            IdentId name;
            PPBuiltin builtin;
            bool function_like;
        } builtins[] = {
            { C_NAME_FILE_MACRO, PP_BUILTIN_FILE, false },
            { C_NAME_LINE_MACRO, PP_BUILTIN_LINE, false },
            { C_NAME_COUNTER, PP_BUILTIN_COUNTER, false },
            { C_NAME_DATE, PP_BUILTIN_DATE, false },
            { C_NAME_TIME, PP_BUILTIN_TIME, false },
            { C_NAME_TIMESTAMP, PP_BUILTIN_TIMESTAMP, false },
            { C_NAME_BASE_FILE, PP_BUILTIN_BASE_FILE, false },
            { C_NAME_FILE_NAME, PP_BUILTIN_FILE_NAME, false },
            { C_NAME_INCLUDE_LEVEL, PP_BUILTIN_INCLUDE_LEVEL, false },
            { C_NAME_PRAGMA_OPERATOR, PP_BUILTIN_PRAGMA, true },
            { C_NAME_DEFINED, PP_BUILTIN_DEFINED, false },
            { C_NAME_HAS_INCLUDE, PP_BUILTIN_HAS_INCLUDE, true },
            { C_NAME_HAS_INCLUDE_NEXT, PP_BUILTIN_HAS_INCLUDE_NEXT, true },
            { C_NAME_HAS_ATTRIBUTE, PP_BUILTIN_HAS_ATTRIBUTE, true },
            { C_NAME_HAS_BUILTIN, PP_BUILTIN_HAS_BUILTIN, true },
            { C_NAME_HAS_FEATURE, PP_BUILTIN_HAS_FEATURE, true },
            { C_NAME_HAS_EXTENSION, PP_BUILTIN_HAS_EXTENSION, true },
            { C_NAME_HAS_C_ATTRIBUTE, PP_BUILTIN_HAS_C_ATTRIBUTE, true },
            { C_NAME_HAS_DECLSPEC_ATTRIBUTE, PP_BUILTIN_HAS_DECLSPEC_ATTRIBUTE, true },
            { C_NAME_HAS_WARNING, PP_BUILTIN_HAS_WARNING, true },
            { C_NAME_HAS_EMBED, PP_BUILTIN_HAS_EMBED, true },
            { C_NAME_IS_IDENTIFIER, PP_BUILTIN_IS_IDENTIFIER, true },
            { C_NAME_IS_TARGET_ARCH, PP_BUILTIN_IS_TARGET_ARCH, true },
            { C_NAME_IS_TARGET_VENDOR, PP_BUILTIN_IS_TARGET_VENDOR, true },
            { C_NAME_IS_TARGET_OS, PP_BUILTIN_IS_TARGET_OS, true },
            { C_NAME_IS_TARGET_ENVIRONMENT, PP_BUILTIN_IS_TARGET_ENVIRONMENT, true },
            { C_NAME_BUILDING_MODULE, PP_BUILTIN_BUILDING_MODULE, true },
        };
        for (uint32_t i = 0; result && i < BUSTER_ARRAY_LENGTH(builtins); i += 1)
        {
            pp_define_builtin(pp, builtins[i].name, builtins[i].builtin, builtins[i].function_like);
        }
        // Two of clang's builtins that aren't among c_intern.c's predefined names: __FLT_EVAL_METHOD__ (its value
        // follows -ffp-eval-method and SSE) and the __has_constexpr_builtin query.
        static const struct
        {
            const char* name;
            PPBuiltin builtin;
            bool function_like;
        } named_builtins[] = {
            { "__FLT_EVAL_METHOD__", PP_BUILTIN_FLT_EVAL_METHOD, false },
            { "__has_constexpr_builtin", PP_BUILTIN_HAS_CONSTEXPR_BUILTIN, true },
        };
        for (uint32_t i = 0; result && i < BUSTER_ARRAY_LENGTH(named_builtins); i += 1)
        {
            const IdentId id = intern_find_or_add(table, (const uint8_t*)named_builtins[i].name, (uint32_t)strlen(named_builtins[i].name));
            result = id != 0;
            if (result)
            {
                pp_define_builtin(pp, id, named_builtins[i].builtin, named_builtins[i].function_like);
            }
        }
        // __DATE__ and __TIME__ from SOURCE_DATE_EPOCH if it is set (reproducible builds), else now; __TIMESTAMP__ from
        // SOURCE_DATE_EPOCH too, else each file's modification time (pp_expand_builtin).
        static const char months[12][4] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
        const char* epoch = getenv("SOURCE_DATE_EPOCH");
        const time_t now = epoch != 0 ? (time_t)strtoll(epoch, 0, 10) : time(0);
        struct tm calendar;
        if (epoch != 0)
        {
            gmtime_r(&now, &calendar);
        }
        else
        {
            localtime_r(&now, &calendar);
        }
        snprintf(pp->date, sizeof(pp->date), "%s %2d %d", months[calendar.tm_mon % 12], calendar.tm_mday, calendar.tm_year + 1900);
        snprintf(pp->time, sizeof(pp->time), "%02d:%02d:%02d", calendar.tm_hour, calendar.tm_min, calendar.tm_sec);
        pp_format_timestamp(pp->timestamp, sizeof(pp->timestamp), &calendar);
        pp->has_epoch = epoch != 0;
        pp->zero_location = pp_scratch_clean(pp, (const uint8_t*)"0", 1);
        pp->one_location = pp_scratch_clean(pp, (const uint8_t*)"1", 1);
        pp->hash_location = pp_scratch_clean(pp, (const uint8_t*)"#", 1);
        // The output's keyword kinds: a predefined name that is a keyword in this mode, by id (below 128).
        for (uint32_t id = 0; id < BUSTER_PP_KEYWORD_TABLE; id += 1)
        {
            pp->keyword_kinds[id] = id < C_NAME_COUNT && c_name_is_keyword(id, options->language) ? (uint8_t)(BUSTER_PP_KIND_KEYWORD + id - 1) : C_TOKEN_RAW_IDENTIFIER;
        }
        result = !pp->fatal;
    }
    return result;
}

static void pp_release(Preprocessor* pp)
{
    pp_tokens_release(&pp->output);
    pp_tokens_release(&pp->arguments);
    pp_tokens_release(&pp->captures);
    pp_tokens_release(&pp->expansions);
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

static void pp_resolve_keywords(Preprocessor* pp);

// Preprocesses the main file: the predefines (or PPOptions.predefines_path's), the command line's -D, -U and -include,
// then the file. The output is pp->output, with keywords' kinds. Returns false if there were errors.
static bool pp_run(Preprocessor* pp, const char* main_path)
{
    bool result = true;
    // The predefines.
    if (pp->options.predefines_path != 0)
    {
        const uint32_t file = pp_lookup_path(pp, pp->options.predefines_path, (uint32_t)strlen(pp->options.predefines_path), BUSTER_PP_SYSTEM_FILE);
        pp->flt_eval_method = predefines_eval_method(&pp->options.predefines);
        result = file != BUSTER_PP_NONE && pp->files[file].data != BUSTER_PP_NONE;
        if (result)
        {
            pp_run_file(pp, file);
        }
        else
        {
            pp_diagnose(pp, PP_SEVERITY_FATAL, 0, "can't read the predefines file %s", pp->options.predefines_path);
        }
    }
    else
    {
        char* text = arena_push(&pp->arena, BUSTER_PP_PREDEFINES_BYTES, 1);
        const uint32_t length = text != 0 ? pp_predefines(&pp->options, text, &pp->flt_eval_method) : BUSTER_PP_PREDEFINES_BYTES + 1;
        const uint32_t file = length <= BUSTER_PP_PREDEFINES_BYTES ? pp_virtual_file(pp, "<built-in>", text, length, BUSTER_PP_SYSTEM_FILE) : BUSTER_PP_NONE;
        result = file != BUSTER_PP_NONE;
        if (result)
        {
            pp_run_file(pp, file);
        }
        else
        {
            pp_diagnose(pp, PP_SEVERITY_FATAL, 0, "can't make the predefined macros for these options");
        }
    }
    // The command line: -municode's UNICODE, then -D and -U in order, then -include.
    uint64_t room = 32;
    for (uint32_t i = 0; i < pp->options.macro_count; i += 1)
    {
        room += strlen(pp->options.macros[i]) + 16;
    }
    for (uint32_t i = 0; i < pp->options.include_count; i += 1)
    {
        room += strlen(pp->options.includes[i]) + 16;
    }
    char* command_line = result ? arena_push(&pp->arena, room, 1) : 0;
    if (command_line != 0)
    {
        uint64_t length = pp->options.predefines.unicode ? (uint64_t)sprintf(command_line, "#define UNICODE 1\n") : 0;
        for (uint32_t i = 0; i < pp->options.macro_count; i += 1)
        {
            const char* macro = pp->options.macros[i];
            const char* equals = strchr(macro + 1, '=');
            if (macro[0] == 'U')
            {
                length += (uint64_t)sprintf(command_line + length, "#undef %s\n", macro + 1);
            }
            else if (equals != 0)
            {
                length += (uint64_t)sprintf(command_line + length, "#define %.*s %s\n", (int)(equals - macro - 1), macro + 1, equals + 1);
            }
            else
            {
                length += (uint64_t)sprintf(command_line + length, "#define %s 1\n", macro + 1);
            }
        }
        const uint32_t file = pp_virtual_file(pp, "<command line>", command_line, (uint32_t)length, BUSTER_PP_USER_FILE);
        if (file != BUSTER_PP_NONE)
        {
            pp_run_file(pp, file);
        }
        // The -include lines, in a file of their own named as clang's: its forced includes are in its <built-in> buffer,
        // after the command line's macros, which __BASE_FILE__ in a forced header gives.
        length = 0;
        for (uint32_t i = 0; i < pp->options.include_count; i += 1)
        {
            length += (uint64_t)sprintf(command_line + length, "#include \"%s\"\n", pp->options.includes[i]);
        }
        const uint32_t includes = length != 0 ? pp_virtual_file(pp, "<built-in>", command_line, (uint32_t)length, BUSTER_PP_USER_FILE) : BUSTER_PP_NONE;
        if (includes != BUSTER_PP_NONE)
        {
            pp_run_file(pp, includes);
        }
    }
    // The main file.
    const uint32_t file = result && !pp->fatal ? pp_lookup_path(pp, main_path, (uint32_t)strlen(main_path), BUSTER_PP_USER_FILE) : BUSTER_PP_NONE;
    if (file != BUSTER_PP_NONE && pp->files[file].data != BUSTER_PP_NONE)
    {
        pp->main_file = file;
        pp->include_depth = 0;
        pp_run_file(pp, file);
    }
    else if (!pp->fatal)
    {
        pp_diagnose(pp, PP_SEVERITY_FATAL, 0, "no such file: '%s'", main_path);
    }
    pp_resolve_keywords(pp);
    return pp->errors == 0 && !pp->fatal;
}

// ---------------------------------------------------------------------------------------------------------------------
// The output

// Gives each identifier of the output that is a keyword in the language mode its keyword's kind, 64 tokens at a time: one
// compare finds the identifiers, and for each 16, one compare their ids below the table's size, and two vpermi2b look
// their kinds up in pp->keyword_kinds's halves, one picked by bit 7 of the id.
static void pp_resolve_keywords_vector(Preprocessor* pp)
{
    PPTokens* out = &pp->output;
    const __m512i table0 = _mm512_loadu_si512(pp->keyword_kinds);
    const __m512i table1 = _mm512_loadu_si512(pp->keyword_kinds + 64);
    const __m512i table2 = _mm512_loadu_si512(pp->keyword_kinds + 128);
    const __m512i table3 = _mm512_loadu_si512(pp->keyword_kinds + 192);
    for (uint32_t i = 0; i < out->count; i += 64)
    {
        const __mmask64 valid = out->count - i >= 64 ? ~0ull : _bzhi_u64(~0ull, out->count - i);
        const __m512i kinds = _mm512_maskz_loadu_epi8(valid, out->kinds + i);
        const uint64_t identifiers = _mm512_mask_cmpeq_epi8_mask(valid, kinds, _mm512_set1_epi8(C_TOKEN_RAW_IDENTIFIER));
        if (identifiers != 0)
        {
            __m512i looked = _mm512_setzero_si512();
            uint64_t small = 0;
            for (uint32_t group = 0; group < 4; group += 1)
            {
                const __mmask16 lanes = (__mmask16)(identifiers >> (group * 16));
                const __m512i values = _mm512_maskz_loadu_epi32(lanes, out->values + i + group * 16);
                const __mmask16 keywords = _mm512_mask_cmplt_epu32_mask(lanes, values, _mm512_set1_epi32(BUSTER_PP_KEYWORD_TABLE));
                const __m128i indices = _mm512_cvtepi32_epi8(values);
                const __m512i wide = _mm512_zextsi128_si512(indices);
                const __m128i lower = _mm512_castsi512_si128(_mm512_permutex2var_epi8(table0, wide, table1));
                const __m128i upper = _mm512_castsi512_si128(_mm512_permutex2var_epi8(table2, wide, table3));
                const __m128i kinds_here = _mm_mask_blend_epi8(_mm_movepi8_mask(indices), lower, upper);
                looked = group == 0 ? _mm512_inserti32x4(looked, kinds_here, 0)
                       : group == 1 ? _mm512_inserti32x4(looked, kinds_here, 1)
                       : group == 2 ? _mm512_inserti32x4(looked, kinds_here, 2)
                                    : _mm512_inserti32x4(looked, kinds_here, 3);
                small |= (uint64_t)keywords << (group * 16);
            }
            _mm512_mask_storeu_epi8(out->kinds + i, small, looked);
        }
    }
}

static void pp_resolve_keywords_scalar(Preprocessor* pp)
{
    PPTokens* out = &pp->output;
    for (uint32_t i = 0; i < out->count; i += 1)
    {
        if (out->kinds[i] == C_TOKEN_RAW_IDENTIFIER && out->values[i] < BUSTER_PP_KEYWORD_TABLE)
        {
            out->kinds[i] = pp->keyword_kinds[out->values[i]];
        }
    }
}

static void pp_resolve_keywords(Preprocessor* pp)
{
    if (pp->vectorized)
    {
        pp_resolve_keywords_vector(pp);
    }
    else
    {
        pp_resolve_keywords_scalar(pp);
    }
}

// The characters that can join a punctuator before them into a longer one, or start a comment after '/'.
static bool pp_punctuation_joins(uint8_t before, uint8_t after)
{
    static const char joining[] = "+-*/%<>=!&|^#.:?";
    return before != 0 && after != 0 && strchr(joining, before) != 0 && strchr(joining, after) != 0;
}

static bool pp_word_character(uint8_t c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '$' || c >= 0x80 || c == '\\';
}

// Would printing `after` right after `before` (their last and first characters) lex differently? Then a space goes
// between them. It errs towards a space: -E's output only has to lex into the same tokens.
static bool pp_needs_separation(uint8_t before_last, uint8_t before_kind, uint8_t after_first)
{
    bool result = false;
    if (before_kind == C_TOKEN_NUMERIC_CONSTANT)
    {
        // A pp-number goes on with word characters, '.', a digit separator and an exponent's sign.
        result = pp_word_character(after_first) || after_first == '.' || after_first == '\'' || after_first == '+' || after_first == '-';
    }
    else if (pp_word_character(before_last))
    {
        // An identifier goes on with word characters, and a prefix makes a literal with a quote.
        result = pp_word_character(after_first) || after_first == '"' || after_first == '\'';
    }
    else if (before_last == '.')
    {
        result = (after_first >= '0' && after_first <= '9') || pp_punctuation_joins(before_last, after_first);
    }
    else
    {
        result = pp_punctuation_joins(before_last, after_first);
    }
    return result;
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

// The markers' state before the first token: the stack is the main file's, at its first line, which the text gives
// (`# 1 "main.c"`, as clang's first line is), or nothing without a main file.
static bool pp_markers_initialize(const Preprocessor* pp, PPMarkers* markers, Arena* arena)
{
    *markers = (PPMarkers){
        .range = BUSTER_PP_NONE,
        .root = BUSTER_PP_NONE,
        .cursors = arena_push_array(arena, (uint64_t)pp->range_count + 1, sizeof(uint32_t), 4),
        .arena = arena,
        .capacity = 4096,
        .text = arena_push(arena, 4096, 1),
    };
    for (uint32_t r = 0; markers->cursors != 0 && r <= pp->range_count; r += 1)
    {
        markers->cursors[r] = BUSTER_PP_NONE;
    }
    for (uint32_t r = 0; markers->root == BUSTER_PP_NONE && r < pp->range_count; r += 1)
    {
        markers->root = pp->ranges[r].parent == BUSTER_PP_NONE && pp->ranges[r].file == pp->main_file ? r : BUSTER_PP_NONE;
    }
    return markers->cursors != 0 && markers->text != 0;
}

// A range's parent on the stack: its includer, or the main file's range for another root (the command line's and the
// forced includes' files).
static uint32_t pp_print_parent(const Preprocessor* pp, const PPMarkers* markers, uint32_t range)
{
    const uint32_t parent = pp->ranges[range].parent;
    return parent != BUSTER_PP_NONE || range == markers->root ? parent : markers->root;
}

static uint32_t pp_print_depth(const Preprocessor* pp, const PPMarkers* markers, uint32_t range)
{
    uint32_t result = 0;
    for (uint32_t at = pp_print_parent(pp, markers, range); at != BUSTER_PP_NONE; at = pp_print_parent(pp, markers, at))
    {
        result += 1;
    }
    return result;
}

// The presumed position of `location` in `range`, at physical line `physical`: by the range's latest line note at or
// before it, found from the range's cursor (going forward, as the printer mostly asks, else from the range's first).
static PPPresumed pp_print_presumed(const Preprocessor* pp, PPMarkers* markers, uint32_t range, uint32_t location, uint32_t physical)
{
    const PPRange* record = &pp->ranges[range];
    uint32_t at = markers->cursors[range];
    at = at != BUSTER_PP_NONE && pp->line_directives[at].location > location ? BUSTER_PP_NONE : at;
    uint32_t next = at == BUSTER_PP_NONE ? record->first_line_directive : pp->line_directives[at].next_in_range;
    while (next != BUSTER_PP_NONE && pp->line_directives[next].location <= location)
    {
        at = next;
        next = pp->line_directives[next].next_in_range;
    }
    markers->cursors[range] = at;
    const PPFile* file = &pp->files[record->file];
    const PPLineDirective* note = at != BUSTER_PP_NONE ? &pp->line_directives[at] : 0;
    const bool renamed = note != 0 && note->name != 0;
    const int64_t delta = note != 0 ? note->delta : 0;
    return (PPPresumed){
        .line = (uint32_t)((int64_t)physical + delta),
        .delta = delta,
        .name = renamed ? note->name : file->path,
        .name_length = renamed ? note->name_length : file->path_length,
        .system = note != 0 ? note->system : record->system,
    };
}

// The presumed position where a range includes another (`child`): the line after the directive when `after`, for a
// marker that goes back to it.
static PPPresumed pp_print_includer(Preprocessor* pp, PPMarkers* markers, uint32_t range, uint32_t child, bool after)
{
    uint32_t column;
    const uint32_t location = pp->ranges[child].include_location;
    PPPresumed result = pp_print_presumed(pp, markers, range, location, pp_line_of(pp, range, location, &column));
    result.line += after;
    return result;
}

// Adds a line marker to the step's text: `# line "name"`, the flags, the system level's (3, or 3 4) and a line break,
// with a break first when something is on the output's current line. The name is escaped as clang writes it
// (raw_ostream::write_escaped): \\, \", \t and \n, and a byte outside ' ' to '~' as three octal digits.
static bool pp_print_marker_line(PPMarkers* markers, bool* line_start, uint32_t line, const char* name, uint32_t name_length, const char* flags, uint8_t system)
{
    const uint64_t need = markers->length + 4 * (uint64_t)name_length + 64;
    if (need > markers->capacity)
    {
        char* text = arena_push(markers->arena, 2 * need, 1);
        if (text != 0)
        {
            memcpy(text, markers->text, markers->length);
            markers->text = text;
            markers->capacity = 2 * need;
        }
    }
    const bool result = need <= markers->capacity;
    if (result)
    {
        char* out = markers->text + markers->length;
        uint64_t n = 0;
        out[n] = '\n';
        n += !*line_start;
        n += (uint64_t)sprintf(out + n, "# %u \"", line);
        for (uint32_t i = 0; i < name_length; i += 1)
        {
            const uint8_t c = (uint8_t)name[i];
            const char escape = c == '\\' ? '\\' : c == '"' ? '"' : c == '\t' ? 't' : c == '\n' ? 'n' : 0;
            if (escape != 0)
            {
                out[n] = '\\';
                out[n + 1] = escape;
                n += 2;
            }
            else if (c >= ' ' && c <= '~')
            {
                out[n] = (char)c;
                n += 1;
            }
            else
            {
                out[n] = '\\';
                out[n + 1] = (char)('0' + (c >> 6));
                out[n + 2] = (char)('0' + ((c >> 3) & 7));
                out[n + 3] = (char)('0' + (c & 7));
                n += 4;
            }
        }
        n += (uint64_t)sprintf(out + n, "\"%s%s\n", flags, system == BUSTER_PP_EXTERN_C_FILE ? " 3 4" : system == BUSTER_PP_SYSTEM_FILE ? " 3" : "");
        markers->length += n;
        *line_start = true;
    }
    return result;
}

// The first step: `# 1 "main.c"`, the main file's marker, in markers->text.
static bool pp_print_start(const Preprocessor* pp, PPMarkers* markers)
{
    markers->length = 0;
    bool line_start = true;
    bool result = true;
    if (markers->root != BUSTER_PP_NONE)
    {
        const PPRange* root = &pp->ranges[markers->root];
        const PPFile* file = &pp->files[root->file];
        result = pp_print_marker_line(markers, &line_start, 1, file->path, file->path_length, "", root->system);
        markers->range = markers->root;
        markers->line = 1;
        markers->delta = 0;
        markers->name = file->path;
        markers->name_length = file->path_length;
        markers->system = root->system;
    }
    return result;
}

// Clang's assume_nonnull end callback supplies an invalid source location, so -E keeps its current presumed line.
// Recognize that event only in the cold pragma path; the token stream's spelling and expansion locations stay intact.
static __attribute__((noinline)) bool pp_print_keeps_line(const Preprocessor* pp, uint32_t token)
{
    const PPTokens* out = &pp->output;
    bool result = out->count - token >= 6 && out->values[token + 1] == C_NAME_PRAGMA && out->kinds[token + 5] == BUSTER_PP_KIND_PRAGMA_END &&
                  out->kinds[token + 2] == C_TOKEN_RAW_IDENTIFIER && out->kinds[token + 3] == C_TOKEN_RAW_IDENTIFIER && out->kinds[token + 4] == C_TOKEN_RAW_IDENTIFIER;
    if (result)
    {
        const IdentId space = out->values[token + 2];
        const IdentId name = out->values[token + 3];
        const IdentId end = out->values[token + 4];
        result = intern_length(pp->table, space) == 5 && memcmp(intern_bytes(pp->table, space), "clang", 5) == 0 &&
                 intern_length(pp->table, name) == 14 && memcmp(intern_bytes(pp->table, name), "assume_nonnull", 14) == 0 &&
                 intern_length(pp->table, end) == 3 && memcmp(intern_bytes(pp->table, end), "end", 3) == 0;
    }
    return result;
}

// Where -E places a token that starts an output line: at its own location; or for one a macro made, at the start of
// the expansion it came from (the expansion starts are passed in the output's order), as clang places a token by its
// expansion location; or for a #pragma's '#', which is in no source, at the directive (its name's location).
static uint32_t pp_print_location(const Preprocessor* pp, PPMarkers* markers, uint32_t token)
{
    uint32_t at = markers->expansion;
    while (at < pp->expansion_start_count && pp->expansion_starts[at].token <= token)
    {
        at += 1;
    }
    markers->expansion = at;
    const uint8_t flags = pp->output.flags[token];
    const uint32_t own = pp->output.locations[token];
    const bool pragma = pp->output.kinds[token] == C_TOKEN_HASH && own == pp->hash_location;
    const bool directive = pragma && !(flags & BUSTER_PP_FROM_MACRO);
    markers->keep_line = pragma && pp_print_keeps_line(pp, token);
    return (flags & BUSTER_PP_FROM_MACRO) && at != 0 ? pp->expansion_starts[at - 1].location : directive ? pp->output.locations[token + 1] : own;
}

// Places a token that starts an output line, at `location` in `range` (pp_print_location), at physical line
// `physical`, as clang's printer does (FileChanged, MoveToLine): going back to an includer marks each file it goes back
// to with flag 2, and going into an included file each file it goes into with flag 1; a #line, a new name or a new
// system level, a marker; a jump forward of up to 8 lines, that many line breaks; any other jump, a marker. The markers
// go to markers->text (markers->length bytes, ending in a line break); returns the line breaks to put after them, before
// the token, and false in `*written` if memory ran out. pp_print_place takes the common case inline.
static __attribute__((noinline)) uint32_t pp_print_place_marking(Preprocessor* pp, PPMarkers* markers, uint32_t range, uint32_t location, uint32_t physical,
                                                                 bool line_start, bool* written)
{
    markers->length = 0;
    const uint32_t previous_note = markers->cursors[range];
    PPPresumed here = pp_print_presumed(pp, markers, range, location, physical);
    if (markers->keep_line && range == markers->range && previous_note == markers->cursors[range] && here.name == markers->name &&
        here.delta == markers->delta && here.system == markers->system)
    {
        here.line = markers->line;
    }
    uint32_t result = 0;
    bool fits = true;
    if (range != markers->range)
    {
        // The ranges from `range` up to below the stack's common part with the top, bottom up, then the common part.
        uint32_t path[BUSTER_PP_MAX_INCLUDE_DEPTH + 8];
        uint32_t path_count = 0;
        uint32_t common = markers->range;
        uint32_t down = range;
        uint32_t common_depth = common != BUSTER_PP_NONE ? pp_print_depth(pp, markers, common) : 0;
        uint32_t down_depth = pp_print_depth(pp, markers, down);
        while (down != BUSTER_PP_NONE && down_depth > common_depth && path_count < BUSTER_ARRAY_LENGTH(path))
        {
            path[path_count] = down;
            path_count += 1;
            down = pp_print_parent(pp, markers, down);
            down_depth -= 1;
        }
        while (common != BUSTER_PP_NONE && common_depth > down_depth)
        {
            common = pp_print_parent(pp, markers, common);
            common_depth -= 1;
        }
        while (common != down && path_count < BUSTER_ARRAY_LENGTH(path))
        {
            path[path_count] = down;
            path_count += 1;
            down = pp_print_parent(pp, markers, down);
            common = pp_print_parent(pp, markers, common);
        }
        if (common == BUSTER_PP_NONE)
        {
            // No common part (no main file's range): the token's position alone.
            fits = pp_print_marker_line(markers, &line_start, here.line, here.name, here.name_length, "", here.system);
        }
        else
        {
            for (uint32_t at = markers->range; fits && at != common; at = pp_print_parent(pp, markers, at))
            {
                const uint32_t to = pp_print_parent(pp, markers, at);
                const PPPresumed back = to == range ? here : pp_print_includer(pp, markers, to, at, true);
                fits = pp_print_marker_line(markers, &line_start, back.line, back.name, back.name_length, " 2", back.system);
            }
            for (uint32_t k = path_count; fits && k > 0; k -= 1)
            {
                const uint32_t to = path[k - 1];
                const PPPresumed into = to == range ? here : pp_print_includer(pp, markers, to, path[k - 2], false);
                fits = pp_print_marker_line(markers, &line_start, into.line, into.name, into.name_length, " 1", into.system);
            }
        }
    }
    else if (here.name != markers->name || here.delta != markers->delta || here.system != markers->system)
    {
        fits = pp_print_marker_line(markers, &line_start, here.line, here.name, here.name_length, "", here.system);
    }
    else if (here.line > markers->line && here.line - markers->line <= 8)
    {
        result = here.line - markers->line;
    }
    else if (here.line != markers->line || !line_start)
    {
        fits = pp_print_marker_line(markers, &line_start, here.line, here.name, here.name_length, "", here.system);
    }
    markers->range = range;
    markers->line = here.line;
    markers->delta = here.delta;
    markers->name = here.name;
    markers->name_length = here.name_length;
    markers->system = here.system;
    *written = fits;
    return result;
}

// The common case of a line start, inline: in the same range as the line before, past the same line note (none passed
// since, and none ahead of `location`), a jump forward of 1 to 8 lines is that many line breaks and no marker. Anything
// else is pp_print_place_marking's.
BUSTER_INLINE uint32_t pp_print_place(Preprocessor* pp, PPMarkers* markers, uint32_t range, uint32_t location, uint32_t physical, bool line_start, bool* written)
{
    const uint32_t at = markers->cursors[range];
    const uint32_t next = at == BUSTER_PP_NONE ? pp->ranges[range].first_line_directive : pp->line_directives[at].next_in_range;
    const uint32_t line = (uint32_t)((int64_t)physical + markers->delta);
    const bool same = !markers->keep_line && range == markers->range && (at == BUSTER_PP_NONE || pp->line_directives[at].location <= location) &&
                      (next == BUSTER_PP_NONE || pp->line_directives[next].location > location) && line > markers->line && line - markers->line <= 8;
    uint32_t result = line - markers->line;
    markers->length = 0;
    *written = true;
    if (same)
    {
        markers->line = line;
    }
    else
    {
        result = pp_print_place_marking(pp, markers, range, location, physical, line_start, written);
    }
    return result;
}

static bool pp_print_scalar(Preprocessor* pp, FILE* stream, bool markers)
{
    const PPTokens* out = &pp->output;
    Scratch scratch = scratch_begin(0);
    uint32_t buffer_size = 1 << 16;
    uint8_t* buffer = arena_push(scratch.arena, buffer_size, 1);
    uint8_t previous_last = 0;
    uint8_t previous_kind = 0;
    bool line_start = true;
    bool force_line = false;
    PPMarkers marked;
    bool result = buffer != 0 && pp_markers_initialize(pp, &marked, scratch.arena) && (!markers || pp_print_start(pp, &marked));
    if (result && markers)
    {
        fwrite(marked.text, 1, marked.length, stream);
    }
    for (uint32_t i = 0; result && i < out->count; i += 1)
    {
        const uint8_t kind = out->kinds[i];
        const uint8_t flags = out->flags[i];
        if (kind == BUSTER_PP_KIND_PRAGMA_END)
        {
            force_line = true;
            continue;
        }
        if ((flags & BUSTER_PP_START_OF_LINE) || force_line || i == 0)
        {
            uint32_t breaks = !line_start;
            const uint32_t place = markers ? pp_print_location(pp, &marked, i) : 0;
            const uint32_t range = markers ? pp_find_range(pp, place) : BUSTER_PP_NONE;
            if (range != BUSTER_PP_NONE && pp->ranges[range].file != BUSTER_PP_NONE)
            {
                uint32_t column;
                breaks = pp_print_place(pp, &marked, range, place, pp_line_of(pp, range, place, &column), line_start, &result);
                fwrite(marked.text, 1, marked.length, stream);
            }
            for (uint32_t n = 0; n < breaks; n += 1)
            {
                fputc('\n', stream);
            }
            line_start = true;
            force_line = false;
            previous_last = 0;
        }
        const uint32_t room = kind == C_TOKEN_RAW_IDENTIFIER || (kind >= BUSTER_PP_KIND_KEYWORD && kind < BUSTER_PP_KIND_PRAGMA_END) ? 0 : out->values[i] - out->locations[i];
        if (room + 64 > buffer_size)
        {
            buffer_size = room + 64;
            buffer = arena_push(scratch.arena, buffer_size, 1);
            result = buffer != 0;
        }
        if (result)
        {
            const uint8_t* spelling;
            const uint32_t length = pp_spelling(pp, kind, out->values[i], out->locations[i], buffer, &spelling);
            if (!line_start && length != 0 && ((flags & BUSTER_PP_SPACE) || pp_needs_separation(previous_last, previous_kind, spelling[0])))
            {
                fputc(' ', stream);
            }
            fwrite(spelling, 1, length, stream);
            previous_last = length != 0 ? spelling[length - 1] : previous_last;
            previous_kind = kind;
            line_start = false;
        }
    }
    if (!line_start)
    {
        fputc('\n', stream);
    }
    scratch_end(scratch);
    return result && ferror(stream) == 0;
}

// The printer's directory of the location space: for each page of 2^BUSTER_PP_DIRECTORY_SHIFT locations (256: a small
// header's range is a few pages), the first range that reaches into it. Ranges are in the order of their bases, so a
// location's range is a load and seldom a step forward (pp_directory_range) rather than pp_find_range's binary search,
// for every token printed.
#define BUSTER_PP_DIRECTORY_SHIFT 8

static uint32_t* pp_build_directory(const Preprocessor* pp, Arena* arena)
{
    const uint32_t pages = (pp->location_top >> BUSTER_PP_DIRECTORY_SHIFT) + 1;
    uint32_t* result = arena_push_array(arena, pages, sizeof(uint32_t), 64);
    uint32_t page = 0;
    for (uint32_t r = 0; result != 0 && r < pp->range_count; r += 1)
    {
        const uint32_t last = (pp->ranges[r].base + pp->ranges[r].length - 1) >> BUSTER_PP_DIRECTORY_SHIFT;
        for (; page <= last && page < pages; page += 1)
        {
            result[page] = r;
        }
    }
    for (; result != 0 && page < pages; page += 1)
    {
        result[page] = pp->range_count != 0 ? pp->range_count - 1 : 0;
    }
    return result;
}

// The range that holds `location`, or BUSTER_PP_NONE, as pp_find_range gives it.
BUSTER_INLINE uint32_t pp_directory_range(const Preprocessor* pp, const uint32_t* directory, uint32_t location)
{
    uint32_t result = directory[location >> BUSTER_PP_DIRECTORY_SHIFT];
    while (result + 1 < pp->range_count && location >= pp->ranges[result].base + pp->ranges[result].length)
    {
        result += 1;
    }
    const bool inside = pp->range_count != 0 && location >= pp->ranges[result].base && location - pp->ranges[result].base < pp->ranges[result].length;
    return inside ? result : BUSTER_PP_NONE;
}

// The classes pp_print_vector's separation table reads, of a token's first character (the token after) and of a
// token's last one (the token before), by character for ASCII; every byte past 0x7F is a word character. After: other,
// word, digit, '.', '\'', '"', '+' or '-', another character that joins punctuators. Before: other, word (digits too),
// '.', a joining character, and (from the kind, not the character) a pp-number.
#define BUSTER_PP_AFTER_WORD 1
#define BUSTER_PP_BEFORE_WORD 1
#define BUSTER_PP_BEFORE_NUMBER 4
static const uint8_t pp_after_classes[128] __attribute__((aligned(64))) = {
    ['a' ... 'z'] = 1, ['A' ... 'Z'] = 1, ['_'] = 1, ['$'] = 1, ['\\'] = 1, ['0' ... '9'] = 2, ['.'] = 3, ['\''] = 4, ['"'] = 5, ['+'] = 6, ['-'] = 6,
    ['*'] = 7, ['/'] = 7, ['%'] = 7, ['<'] = 7, ['>'] = 7, ['='] = 7, ['!'] = 7, ['&'] = 7, ['|'] = 7, ['^'] = 7, ['#'] = 7, [':'] = 7, ['?'] = 7,
};
static const uint8_t pp_before_classes[128] __attribute__((aligned(64))) = {
    ['a' ... 'z'] = 1, ['A' ... 'Z'] = 1, ['_'] = 1, ['$'] = 1, ['\\'] = 1, ['0' ... '9'] = 1, ['.'] = 2, ['+'] = 3, ['-'] = 3, ['*'] = 3, ['/'] = 3,
    ['%'] = 3, ['<'] = 3, ['>'] = 3, ['='] = 3, ['!'] = 3, ['&'] = 3, ['|'] = 3, ['^'] = 3, ['#'] = 3, [':'] = 3, ['?'] = 3,
};
// Whether a space goes between two tokens, by before-class * 8 + after-class: pp_needs_separation as a table. A word
// goes on with a word, a digit or a quote (a prefix makes a literal); '.' with a digit or a joining character; a joining
// character with one; a pp-number with a word, a digit, '.', '\'' (a digit separator) or an exponent's sign.
static const uint8_t pp_separation_table[64] __attribute__((aligned(64))) = {
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 1, 1, 0, 1, 1, 0, 0,
    0, 0, 1, 1, 0, 0, 1, 1,
    0, 0, 0, 1, 0, 0, 1, 1,
    0, 1, 1, 1, 1, 0, 1, 0,
};

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

static uint32_t pp_print_line(Preprocessor* pp, PPPrintLines* lines, uint32_t range, uint32_t location)
{
    const PPRange* record = &pp->ranges[range];
    const PPFileData* data = &pp->datas[pp->files[record->file].data];
    const uint32_t offset = location - record->base < data->length ? location - record->base : data->length;
    uint32_t result;
    if (range == lines->range && offset >= lines->offset)
    {
        result = lines->line + pp_count_lines(data, lines->offset, offset);
    }
    else
    {
        uint32_t column;
        result = pp_line_of(pp, range, location, &column);
    }
    *lines = (PPPrintLines){ .range = range, .offset = offset, .line = result };
    return result;
}

// Prints the output as pp_print_scalar does, byte for byte, 64 tokens at a time into one buffer that is written as it
// fills. For each chunk: each token's spelling (an identifier's from the intern table, else its bytes in the location
// space through the printer's directory, with masked 64-byte loads checking for the rare backslash), then the spaces
// for all 64 at once: a token at the start of a line gets none, any other one if whitespace was before it or if its
// first character would lex together with the last character of the token before (pp_separation_table, indexed by the
// two characters' classes from vpermi2b lookups), and then the copies, one masked 64-byte store per token (more for a
// longer spelling), with the line breaks and line markers between.
static bool pp_print_vector(Preprocessor* pp, FILE* stream, bool markers)
{
    // The columns and tables in locals: a store through a byte pointer may change any field, which the loops would load
    // again for every token.
    const uint8_t* out_kinds = pp->output.kinds;
    const uint8_t* out_flags = pp->output.flags;
    const uint32_t* out_values = pp->output.values;
    const uint32_t* out_locations = pp->output.locations;
    const uint32_t count = pp->output.count;
    const uint8_t* records = pp->table->records;
    const InternSpan* spans = pp->table->spans;
    const bool trigraphs = (pp->refine_flags & C_TOKENIZER_TRIGRAPHS) != 0;
    Scratch scratch = scratch_begin(0);
    const uint32_t* directory = pp_build_directory(pp, scratch.arena);
    const uint64_t capacity = 1ull << 20;
    uint8_t* buffer = arena_push(scratch.arena, capacity + 64, 64);
    bool result = directory != 0 && buffer != 0;
    uint64_t used = 0;
    // A chunk's spellings, lengths, and first and last bytes.
    const uint8_t* spellings[64];
    uint32_t lengths[64];
    uint8_t firsts[64] __attribute__((aligned(64)));
    uint8_t lasts[64] __attribute__((aligned(64)));
    PPPrintLines lines = { .range = BUSTER_PP_NONE };
    // The state between tokens, as pp_print_scalar keeps it.
    bool line_start = true;
    bool force_line = false;
    PPMarkers marked;
    result = result && pp_markers_initialize(pp, &marked, scratch.arena) && (!markers || pp_print_start(pp, &marked));
    if (result && markers)
    {
        memcpy(buffer, marked.text, marked.length);
        used = marked.length;
    }
    // The last character and kind of the token before the chunk, for its first token's space.
    uint8_t previous_last = 0;
    uint8_t previous_kind = 0;
    const __m512i after_low = _mm512_load_si512(pp_after_classes);
    const __m512i after_high = _mm512_load_si512(pp_after_classes + 64);
    const __m512i before_low = _mm512_load_si512(pp_before_classes);
    const __m512i before_high = _mm512_load_si512(pp_before_classes + 64);
    const __m512i separation = _mm512_load_si512(pp_separation_table);
    const __m512i previous_lanes = _mm512_sub_epi8(_mm512_load_si512(pp_byte_indices), _mm512_set1_epi8(1));
    for (uint32_t c = 0; result && c < count; c += 64)
    {
        const uint32_t available = count - c < 64 ? count - c : 64;
        const __mmask64 valid = _bzhi_u64(~0ull, available);
        // 1. Spellings: an identifier's from the intern table, any other token's bytes at its location. (A branch-free
        // version, which looked up both and picked one, was 1-2% slower on the 9700X: the location lookups it adds for
        // identifiers cost more than the branch's mispredictions.)
        const uint64_t spare_mark = scratch.arena->length;
        for (uint32_t k = 0; result && k < available; k += 1)
        {
            const uint32_t i = c + k;
            const uint8_t kind = out_kinds[i];
            const uint32_t value = out_values[i];
            const uint32_t location = out_locations[i];
            const uint8_t* spelling = 0;
            uint32_t length = 0;
            if (kind == C_TOKEN_RAW_IDENTIFIER || (kind >= BUSTER_PP_KIND_KEYWORD && kind < BUSTER_PP_KIND_KEYWORD + C_NAME_KEYWORD_COUNT))
            {
                spelling = records + spans[value].offset;
                length = spans[value].length;
            }
            else if (kind != BUSTER_PP_KIND_PRAGMA_END && kind != BUSTER_PP_KIND_INTEGER)
            {
                const uint32_t range = pp_directory_range(pp, directory, location);
                spelling = range != BUSTER_PP_NONE ? pp->ranges[range].bytes + (location - pp->ranges[range].base) : (const uint8_t*)"";
                length = range != BUSTER_PP_NONE ? value - location : 0;
                const __m512i bytes = _mm512_maskz_loadu_epi8(_bzhi_u64(~0ull, length < 64 ? length : 64), spelling);
                bool special = _mm512_cmpeq_epi8_mask(bytes, _mm512_set1_epi8('\\')) != 0 || (trigraphs && _mm512_cmpeq_epi8_mask(bytes, _mm512_set1_epi8('?')) != 0);
                for (uint32_t b = 64; !special && b < length; b += 1)
                {
                    special = spelling[b] == '\\' || (trigraphs && spelling[b] == '?');
                }
                if (special)
                {
                    uint8_t* spare = arena_push(scratch.arena, (uint64_t)length + 1, 1);
                    result = spare != 0;
                    length = result ? pp_without_splices(pp, spelling, length, spare, &spelling) : 0;
                }
            }
            else if (kind == BUSTER_PP_KIND_INTEGER)
            {
                uint8_t* spare = arena_push(scratch.arena, 24, 1);
                result = spare != 0;
                length = result ? pp_spelling(pp, kind, value, location, spare, &spelling) : 0;
            }
            spellings[k] = spelling;
            lengths[k] = length;
            firsts[k] = length != 0 ? spelling[0] : 0;
            lasts[k] = length != 0 ? spelling[length - 1] : 0;
        }
        // 2. Spaces, for the 64 tokens at once: line starts (a token that starts a line, or follows a pragma's end, or
        // is the output's first), whitespace, and the separation table on the class of the last character before and
        // of the first character after.
        const __m512i kinds = _mm512_maskz_loadu_epi8(valid, out_kinds + c);
        const __m512i flags = _mm512_maskz_loadu_epi8(valid, out_flags + c);
        const __m512i first_bytes = _mm512_maskz_loadu_epi8(valid, firsts);
        const __m512i last_bytes = _mm512_maskz_loadu_epi8(valid, lasts);
        const uint64_t pragma_ends = _mm512_mask_cmpeq_epi8_mask(valid, kinds, _mm512_set1_epi8((char)BUSTER_PP_KIND_PRAGMA_END));
        const uint64_t starts = _mm512_mask_test_epi8_mask(valid, flags, _mm512_set1_epi8(BUSTER_PP_START_OF_LINE));
        const uint64_t spaces = _mm512_mask_test_epi8_mask(valid, flags, _mm512_set1_epi8(BUSTER_PP_SPACE));
        const __m512i after = _mm512_mask_mov_epi8(_mm512_permutex2var_epi8(after_low, first_bytes, after_high), _mm512_movepi8_mask(first_bytes),
                                                   _mm512_set1_epi8(BUSTER_PP_AFTER_WORD));
        __m512i before = _mm512_mask_mov_epi8(_mm512_permutex2var_epi8(before_low, last_bytes, before_high), _mm512_movepi8_mask(last_bytes),
                                              _mm512_set1_epi8(BUSTER_PP_BEFORE_WORD));
        before = _mm512_mask_mov_epi8(before, _mm512_cmpeq_epi8_mask(kinds, _mm512_set1_epi8(C_TOKEN_NUMERIC_CONSTANT)), _mm512_set1_epi8(BUSTER_PP_BEFORE_NUMBER));
        // Each token's before-class is its previous token's: the classes move up a lane, the chunk's first taking the
        // class of the token before the chunk.
        const uint8_t carried = previous_kind == C_TOKEN_NUMERIC_CONSTANT ? BUSTER_PP_BEFORE_NUMBER
                                : previous_last >= 0x80 ? BUSTER_PP_BEFORE_WORD : pp_before_classes[previous_last];
        before = _mm512_mask_permutexvar_epi8(_mm512_set1_epi8((char)carried), ~1ull, previous_lanes, before);
        const __m512i pair = _mm512_add_epi8(_mm512_slli_epi16(before, 3), after);
        const uint64_t joins = _mm512_test_epi8_mask(_mm512_permutexvar_epi8(pair, separation), _mm512_set1_epi8(1));
        const uint64_t line_starts = starts | (pragma_ends << 1) | (uint64_t)force_line | (uint64_t)(c == 0 && line_start);
        const uint64_t spaced = (spaces | joins) & ~line_starts & valid;
        // 3. Copies, with the line breaks and markers between: without a branch per token but for line markers (which
        // -P doesn't print), the rare line of more than one break, and spellings past 64 bytes.
        for (uint32_t k = 0; result && k < available; k += 1)
        {
            const uint32_t i = c + k;
            const uint32_t length = lengths[k];
            if ((pragma_ends >> k) & 1)
            {
                force_line = true;
                continue;
            }
            // Room for the breaks, a space and the spelling, or a write of what the buffer holds.
            const uint64_t room = (uint64_t)length + 16 + 64;
            if (used + room > capacity)
            {
                result = fwrite(buffer, 1, used, stream) == used;
                used = 0;
            }
            const uint32_t starts_line = (uint32_t)((line_starts >> k) & 1);
            // A line start's break after something on the line; with markers, as many as pp_print_place puts.
            uint32_t breaks = starts_line & (uint32_t)!line_start;
            if (markers && starts_line)
            {
                const uint32_t place = pp_print_location(pp, &marked, i);
                const uint32_t range = pp_directory_range(pp, directory, place);
                if (range != BUSTER_PP_NONE && pp->ranges[range].file != BUSTER_PP_NONE)
                {
                    bool placed = true;
                    breaks = pp_print_place(pp, &marked, range, place, pp_print_line(pp, &lines, range, place), line_start, &placed);
                    // The markers go through the buffer, or past it when they are longer than its room.
                    if (marked.length != 0 && used + marked.length + room > capacity)
                    {
                        result = result && fwrite(buffer, 1, used, stream) == used;
                        used = 0;
                    }
                    if (marked.length != 0 && marked.length + room <= capacity)
                    {
                        memcpy(buffer + used, marked.text, marked.length);
                        used += marked.length;
                    }
                    else if (marked.length != 0)
                    {
                        result = result && fwrite(marked.text, 1, marked.length, stream) == marked.length;
                    }
                    result = result && placed;
                    line_start = line_start || marked.length != 0;
                }
            }
            if (BUSTER_LIKELY(breaks <= 1))
            {
                buffer[used] = '\n';
                used += breaks;
            }
            else
            {
                memset(buffer + used, '\n', breaks);
                used += breaks;
            }
            line_start = line_start || starts_line;
            force_line = false;
            buffer[used] = ' ';
            used += ((spaced >> k) & 1) & (length != 0);
            // The spelling: one masked 64-byte copy, or more for a longer one (written straight out past the buffer's
            // room).
            const uint8_t* spelling = spellings[k];
            const __mmask64 head = _bzhi_u64(~0ull, length < 64 ? length : 64);
            _mm512_mask_storeu_epi8(buffer + used, head, _mm512_maskz_loadu_epi8(head, spelling));
            bool direct = false;
            if (BUSTER_UNLIKELY(length > 64))
            {
                direct = used + length > capacity;
                for (uint32_t b = 64; !direct && b < length; b += 64)
                {
                    const __mmask64 bytes = _bzhi_u64(~0ull, length - b < 64 ? length - b : 64);
                    _mm512_mask_storeu_epi8(buffer + used + b, bytes, _mm512_maskz_loadu_epi8(bytes, spelling + b));
                }
                if (direct)
                {
                    result = fwrite(buffer, 1, used, stream) == used && fwrite(spelling, 1, length, stream) == length;
                    used = 0;
                }
            }
            used += direct ? 0 : length;
            line_start = false;
            previous_last = length != 0 ? lasts[k] : previous_last;
            previous_kind = out_kinds[i];
        }
        arena_pop_to(scratch.arena, spare_mark);
    }
    if (result && !line_start)
    {
        buffer[used] = '\n';
        used += 1;
    }
    result = result && fwrite(buffer, 1, used, stream) == used;
    scratch_end(scratch);
    return result && ferror(stream) == 0;
}

// Prints the output as `-E` does: pp_print_vector, or pp_print_scalar for the scalar kernels.
static bool pp_print(Preprocessor* pp, FILE* stream, bool markers)
{
    return pp->vectorized ? pp_print_vector(pp, stream, markers) : pp_print_scalar(pp, stream, markers);
}

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
    static const struct
    {
        const char* name;
        uint32_t standard;
        bool gnu;
    } standards[] = {
        { "c89", 1989, false }, { "c90", 1989, false }, { "iso9899:1990", 1989, false }, { "gnu89", 1989, true }, { "gnu90", 1989, true },
        { "c99", 1999, false }, { "iso9899:1999", 1999, false }, { "gnu99", 1999, true }, { "c11", 2011, false }, { "iso9899:2011", 2011, false },
        { "gnu11", 2011, true }, { "c17", 2017, false }, { "c18", 2017, false }, { "iso9899:2017", 2017, false }, { "iso9899:2018", 2017, false },
        { "gnu17", 2017, true }, { "gnu18", 2017, true }, { "c23", 2023, false }, { "c2x", 2023, false }, { "iso9899:2024", 2023, false },
        { "gnu23", 2023, true }, { "gnu2x", 2023, true }, { "c9x", 1999, false }, { "iso9899:199x", 1999, false }, { "gnu9x", 1999, true },
        { "c1x", 2011, false }, { "iso9899:201x", 2011, false }, { "gnu1x", 2011, true }, { "iso9899:199409", 1994, false },
    };
    bool result = false;
    for (uint32_t i = 0; !result && i < BUSTER_ARRAY_LENGTH(standards); i += 1)
    {
        if (strcmp(value, standards[i].name) == 0)
        {
            *language = (CLanguage){ .standard = standards[i].standard, .gnu = standards[i].gnu };
            result = true;
        }
    }
    return result;
}

// The preprocessor's options from clang-style arguments (the ones -E needs). `input` gets the source file, `output`
// the -o file (or null), `markers` false for -P. Returns false for an argument it doesn't know.
static bool pp_parse_options(int count, char** arguments, PPOptions* options, Arena* arena, const char** input, const char** output, bool* markers)
{
    bool result = true;
    const char** quote = BUSTER_ARENA_PUSH_ARRAY(arena, const char*, (uint64_t)count + 1);
    const char** include = BUSTER_ARENA_PUSH_ARRAY(arena, const char*, (uint64_t)count + 1);
    const char** system = BUSTER_ARENA_PUSH_ARRAY(arena, const char*, (uint64_t)count + 1);
    const char** after = BUSTER_ARENA_PUSH_ARRAY(arena, const char*, (uint64_t)count + 1);
    const char** macros = BUSTER_ARENA_PUSH_ARRAY(arena, const char*, (uint64_t)count + 1);
    const char** includes = BUSTER_ARENA_PUSH_ARRAY(arena, const char*, (uint64_t)count + 1);
    *options = (PPOptions){ .language = { .standard = 2017, .gnu = true } };
    *input = 0;
    *output = 0;
    *markers = true;
    // -trigraphs, -ftrigraphs and -fno-trigraphs override the mode's default (-1 for none).
    int trigraphs = -1;
    // The last -std= or -ansi, the only one clang's driver reads.
    const char* standard = 0;
    result = quote != 0 && include != 0 && system != 0 && after != 0 && macros != 0 && includes != 0;
    result = result && predefines_options_initialize(&options->predefines, arena, (uint32_t)count);
    for (int i = 0; result && i < count; i += 1)
    {
        // An alias of clang's (-mpascal-strings) is read as the option it stands for (-fpascal-strings).
        const char* argument = predefines_alias(arguments[i]);
        const char* next = i + 1 < count ? arguments[i + 1] : 0;
        // The target's options and the others that change the predefined macros (c_predefines.c) come first.
        const PredefinesParse predefined = predefines_option(&options->predefines, argument, next);
        if (predefined != PREDEFINES_PARSE_UNKNOWN)
        {
            result = predefined != PREDEFINES_PARSE_ERROR;
            i += predefined == PREDEFINES_PARSE_TAKEN_TWO;
        }
        else if (strcmp(argument, "-E") == 0 || strcmp(argument, "-w") == 0 || strcmp(argument, "-fhosted") == 0)
        {
            options->freestanding &= strcmp(argument, "-fhosted") != 0;
        }
        else if (strcmp(argument, "-P") == 0)
        {
            *markers = false;
        }
        else if (strcmp(argument, "-dM") == 0)
        {
            options->dump_macros = true;
        }
        else if (strcmp(argument, "--clang-features") == 0)
        {
            options->clang_features = true;
        }
        else if (strcmp(argument, "-ffreestanding") == 0)
        {
            options->freestanding = true;
        }
        else if (strcmp(argument, "-nostdinc") == 0)
        {
            options->no_standard_includes = true;
        }
        else if (strcmp(argument, "--scalar") == 0)
        {
            options->scalar = true;
        }
        else if (strcmp(argument, "-trigraphs") == 0 || strcmp(argument, "-ftrigraphs") == 0 || strcmp(argument, "-fno-trigraphs") == 0)
        {
            trigraphs = strcmp(argument, "-fno-trigraphs") != 0;
        }
        else if (strncmp(argument, "-m", 2) == 0)
        {
            // predefines_option takes every -m option clang uses for x86-64 Linux: this one clang doesn't know, or
            // refuses or ignores for the target.
            fprintf(stderr, "error: unknown or unsupported option '%s' for x86-64 Linux\n", argument);
            result = false;
        }
        else if (strncmp(argument, "-f", 2) == 0 || strncmp(argument, "-W", 2) == 0)
        {
            // The other code generation and warning options: none changes what -E gives (predefines_option takes the
            // ones that do, and refuses those buster doesn't follow).
        }
        else if (strncmp(argument, "-std=", 5) == 0 || strcmp(argument, "-ansi") == 0)
        {
            standard = argument;
        }
        else if (strcmp(argument, "-x") == 0 && next != 0 && (strcmp(next, "c") == 0 || strcmp(next, "assembler-with-cpp") == 0))
        {
            options->assembly = strcmp(next, "assembler-with-cpp") == 0;
            i += 1;
        }
        else if ((strcmp(argument, "-o") == 0 || strcmp(argument, "--predefines") == 0 || strcmp(argument, "-include") == 0 || strcmp(argument, "-isystem") == 0 ||
                  strcmp(argument, "-iquote") == 0 || strcmp(argument, "-idirafter") == 0) && next != 0)
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
            if (result && argument[1] == 'I')
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
        else if (argument[0] != '-' && *input == 0)
        {
            *input = argument;
        }
        else
        {
            fprintf(stderr, "unknown argument: %s\n", argument);
            result = false;
        }
    }
    // -ansi is -std=c89 in C.
    const char* mode = standard == 0 ? 0 : standard[1] == 'a' ? "c89" : standard + 5;
    if (result && mode != 0 && !pp_parse_standard(mode, &options->language))
    {
        fprintf(stderr, "error: invalid value '%s' in '%s'\n", mode, standard);
        result = false;
    }
    options->quote_directories = quote;
    options->include_directories = include;
    options->system_directories = system;
    options->after_directories = after;
    options->trigraphs = trigraphs >= 0 ? trigraphs != 0 : !options->language.gnu && options->language.standard < 2023;
    options->macros = macros;
    options->includes = includes;
    // What clang's driver decides from the whole command line (the sanitizers, -flto and -fvisibility='s values), and the
    // generated code's instruction set, from the target options as clang resolves them; each reports what doesn't
    // resolve (an incompatible pair of sanitizers, an unknown CPU, -mfpmath against the features), as clang's driver does.
    result = result && predefines_finish(&options->predefines);
    result = result && predefines_target(&options->predefines, &options->target, &options->tune);
    return result && *input != 0;
}

// `compiler -E [options] file.c`: preprocesses the file and prints the result, as clang -E does.
static __attribute__((unused)) int run_preprocessor_command(int count, char** arguments)
{
    int result = 1;
    Arena arena = arena_reserve(1ull << 30);
    PPOptions options;
    const char* input;
    const char* output;
    bool markers;
    const bool parsed = pp_parse_options(count, arguments, &options, &arena, &input, &output, &markers);
    options.expansion_starts = markers;
    if (!parsed)
    {
        fprintf(stderr, "usage: compiler -E [-P] [-o file] [-I dir] [-isystem dir] [-iquote dir] [-idirafter dir] [-D name[=value]] [-U name] "
                        "[-include file] [-dM] [-std=...] [-ansi] [-trigraphs] [-fno-trigraphs] [-ffreestanding] [-march=cpu] [-m[no-]feature] [-O...] [-f...] "
                        "[-g...] [-pthread] [-nostdinc] [--predefines file] [--clang-features] [--scalar] file\n");
    }
    else
    {
        static InternTable table;
        static Preprocessor pp;
        if (!intern_initialize(&table) || !pp_initialize(&pp, &options, &table))
        {
            fprintf(stderr, "can't reserve memory\n");
        }
        else
        {
            const bool preprocessed = pp_run(&pp, input);
            FILE* stream = output != 0 ? fopen(output, "w") : stdout;
            const bool printed = stream != 0 && (options.dump_macros ? pp_print_macros(&pp, stream) : pp_print(&pp, stream, markers));
            if (stream != 0 && stream != stdout)
            {
                fclose(stream);
            }
            if (stream == 0)
            {
                fprintf(stderr, "can't write %s\n", output);
            }
            result = preprocessed && printed ? 0 : 1;
            pp_release(&pp);
            intern_release(&table);
        }
    }
    arena_release(&arena);
    return result;
}
