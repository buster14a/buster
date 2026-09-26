#pragma once

// Opt-in, function-granular reuse of native code generation: a research
// prototype, off unless the driver is given `-fincremental-cache=DIR`
// (docs/incremental-compilation.md is the contract). The reuse boundary is
// one machine-emitted function's contribution to a CodegenModule: its code
// bytes, symbolic relocations, unwind description, line marks, debug
// location seeds, block offsets and statistics. Nothing before canonical IR
// preparation or after code generation is skipped, and a function is reused
// only when its complete canonical dependency record -- its prepared IR with
// every program-level reference relabeled, the referenced type closure and
// symbol attributes, and the compilation context -- is byte-for-byte equal
// to the record stored beside the artifact. A fingerprint only indexes the
// search; equality of the record is the reuse condition.
//
// Entry points, in the order one compilation uses them:
//   incremental_session_open            driver: context-independent setup,
//                                       previous pack read and validated
//   incremental_session_bind_module     codegen: context record, per-function
//                                       records and lookups, before attempts
//   incremental_session_reused          codegen: the decoded artifact to replay
//   incremental_session_capture         codegen: a freshly emitted artifact
//   incremental_session_publish         driver: atomic pack replacement after
//                                       a successful object
//   incremental_session_close           driver: releases the session arena
//
// Record, artifact and pack encodings are explicit little-endian byte
// sequences with bounded counts; readers treat every byte as hostile and
// reject rather than trust. See incremental.c for the layout map.

#include <buster/lib/compiler/codegen/codegen.h>

// Bump when the pack container changes. A pack with another version is
// treated as absent, never reinterpreted.
#define INCREMENTAL_FORMAT_VERSION 1u
// Bump whenever the canonical dependency record gains, loses or reorders a
// field. Records of different schemas never compare equal.
#define INCREMENTAL_KEY_SCHEMA 1u

typedef enum IncrementalPackStatus
{
    INCREMENTAL_PACK_ABSENT,
    INCREMENTAL_PACK_LOADED,
    INCREMENTAL_PACK_CORRUPT,
    INCREMENTAL_PACK_CONTEXT_CHANGED,
    INCREMENTAL_PACK_UNAVAILABLE,
    INCREMENTAL_PACK_STATUS_COUNT,
} IncrementalPackStatus;

// Why a lowered function was or was not reused. Miss reasons name the first
// record section that differs from the previous compilation's function of
// the same name: body, then referenced types, then referenced symbols.
typedef enum IncrementalLookup
{
    INCREMENTAL_LOOKUP_NOT_LOWERED,
    INCREMENTAL_LOOKUP_REUSED,
    INCREMENTAL_LOOKUP_MISS_NO_PACK,
    INCREMENTAL_LOOKUP_MISS_CONTEXT,
    INCREMENTAL_LOOKUP_MISS_NEW,
    INCREMENTAL_LOOKUP_MISS_BODY,
    INCREMENTAL_LOOKUP_MISS_TYPES,
    INCREMENTAL_LOOKUP_MISS_SYMBOLS,
    INCREMENTAL_LOOKUP_MISS_UNCAPTURED,
    INCREMENTAL_LOOKUP_MISS_MALFORMED,
    INCREMENTAL_LOOKUP_INELIGIBLE_INLINE_ASSEMBLY,
    INCREMENTAL_LOOKUP_INELIGIBLE_RECORD,
    INCREMENTAL_LOOKUP_COUNT,
} IncrementalLookup;

typedef enum IncrementalCapture
{
    INCREMENTAL_CAPTURE_NONE,
    INCREMENTAL_CAPTURE_STORED,
    INCREMENTAL_CAPTURE_CANONICAL,
    INCREMENTAL_CAPTURE_FOREIGN_SYMBOL,
    INCREMENTAL_CAPTURE_UNSUPPORTED,
    INCREMENTAL_CAPTURE_COUNT,
} IncrementalCapture;

// Per-function statistics a machine-emitted function adds to
// CodegenStatistics. Replay adds the stored values back so `-v` records are
// identical to a clean compilation. FRAME is the function's frame size, which
// replay folds into maximum_stack_frame_bytes rather than adding.
typedef enum IncrementalStatistic
{
    INCREMENTAL_STATISTIC_STACK_VALUE_BYTES,
    INCREMENTAL_STATISTIC_STACK_FRAME_BYTES,
    INCREMENTAL_STATISTIC_FRAME,
    INCREMENTAL_STATISTIC_NATIVE_VECTOR_OPERATIONS,
    INCREMENTAL_STATISTIC_SPLIT_VECTOR_OPERATIONS,
    INCREMENTAL_STATISTIC_VZEROUPPER,
    INCREMENTAL_STATISTIC_FORWARDED_WIDE_VECTOR_LOADS,
    INCREMENTAL_STATISTIC_SIMD_OPERATIONS,
    INCREMENTAL_STATISTIC_ALLOCATOR_RELOADS,
    INCREMENTAL_STATISTIC_ALLOCATOR_SPILLS,
    INCREMENTAL_STATISTIC_ALLOCATOR_COPIES,
    INCREMENTAL_STATISTIC_ALLOCATOR_BOUNDARY_SPILLS,
    INCREMENTAL_STATISTIC_ALLOCATOR_BOUNDARY_RELOADS,
    INCREMENTAL_STATISTIC_ALLOCATOR_BOUNDARY_COPIES,
    INCREMENTAL_STATISTIC_ALLOCATOR_REMATERIALIZATIONS,
    INCREMENTAL_STATISTIC_ALLOCATOR_PINS,
    INCREMENTAL_STATISTIC_ALLOCATOR_SPLITS,
    INCREMENTAL_STATISTIC_ALLOCATOR_SCHEDULED,
    INCREMENTAL_STATISTIC_ALLOCATOR_SCHEDULE_KEPT,
    INCREMENTAL_STATISTIC_EXACT_ATTEMPTS,
    INCREMENTAL_STATISTIC_EXACT_SUCCESSES,
    INCREMENTAL_STATISTIC_EXACT_FAILURES,
    INCREMENTAL_STATISTIC_MUTABLE_VIRTUAL_REGISTERS,
    INCREMENTAL_STATISTIC_COUNT,
} IncrementalStatistic;

// A code relocation with its symbol replaced by the function's own
// referenced-symbol slot and its offset made function-relative.
typedef struct IncrementalRelocation IncrementalRelocation;
struct IncrementalRelocation
{
    s64 addend;
    u32 offset;
    u32 symbol_slot;
    u8 kind;
    u8 reserved[7];
};

// One machine line mark: a function-relative code offset and the canonical
// instruction whose source it records. Positions are resolved against the
// current compilation's source map on replay, never stored.
typedef struct IncrementalLineMark IncrementalLineMark;
struct IncrementalLineMark
{
    u32 code_offset;
    u32 instruction;
};

typedef struct IncrementalDebugLocation IncrementalDebugLocation;
struct IncrementalDebugLocation
{
    DebugLocation location;
    IrLocalId local;
    u32 start;
    u32 end;
    u8 reserved[4];
};

typedef struct IncrementalFunctionArtifact IncrementalFunctionArtifact;
struct IncrementalFunctionArtifact
{
    ByteSlice code;
    CodegenUnwindAction* unwind_actions;
    u32* epilog_offsets;
    u32* block_offsets;
    IncrementalRelocation* relocations;
    IncrementalLineMark* line_marks;
    IncrementalDebugLocation* debug_locations;
    u64 statistics[INCREMENTAL_STATISTIC_COUNT];
    u32 unwind_action_count;
    u32 epilog_count;
    u32 block_count;
    u32 relocation_count;
    u32 line_mark_count;
    u32 debug_location_count;
    u32 prolog_size;
    u32 symbol_slot_count;
};

typedef struct IncrementalStatistics IncrementalStatistics;
struct IncrementalStatistics
{
    u64 lookups[INCREMENTAL_LOOKUP_COUNT];
    u64 captures[INCREMENTAL_CAPTURE_COUNT];
    // Work carried by reused functions: canonical rows whose selection,
    // allocation, scheduling, encoding and debug-location recording were
    // skipped, and what replay copied instead.
    u64 reused_ir_instructions;
    u64 reused_code_bytes;
    u64 reused_relocations;
    u64 reused_line_marks;
    u64 reused_debug_locations;
    u64 compiled_ir_instructions;
    u64 lowered_ir_instructions;
    u64 record_bytes;
    // How the record bytes divide between the three sections, and how many
    // types the closures held in total: the cost of exactness, in bytes.
    u64 record_body_bytes;
    u64 record_type_bytes;
    u64 record_symbol_bytes;
    u64 record_types;
    u64 record_symbols;
    u64 pack_bytes_read;
    u64 pack_bytes_written;
    u64 pack_entries_read;
    u64 pack_entries_written;
    u64 identity_bytes;
    // Verification mode (-fincremental-verify): hits compiled again and
    // compared artifact-for-artifact with the stored one.
    u64 verified;
    u64 verify_mismatches;
    // Dependency audit (BUSTER_INCREMENTAL_AUDIT builds only; zero otherwise):
    // distinct program ids each compiled function's code generation read,
    // the ids its record covers, and reads the record did not cover.
    u64 audit_functions;
    u64 audit_touched_types;
    u64 audit_closure_types;
    u64 audit_touched_symbols;
    u64 audit_closure_symbols;
    u64 audit_type_violations;
    u64 audit_symbol_violations;
    u64 audit_overflows;
    u64 identity_nanoseconds;
    u64 pack_read_nanoseconds;
    u64 record_nanoseconds;
    u64 replay_nanoseconds;
    u64 pack_write_nanoseconds;
    // One count per translation unit, so a multi-input invocation reports how
    // many packs it found in each state.
    u64 pack_statuses[INCREMENTAL_PACK_STATUS_COUNT];
    u64 units_published;
    IncrementalPackStatus pack_status;
    bool enabled;
    bool published;
    // A code-buffer retry ran after module-level assembly may have changed
    // symbol state, so the session stopped reusing and capturing.
    bool retry_disabled;
    u8 reserved[1];
};

// One lowered function's outcome, retained only when tracing was requested.
typedef struct IncrementalFunctionTrace IncrementalFunctionTrace;
struct IncrementalFunctionTrace
{
    String8 name;
    u64 fingerprint;
    u32 instruction_count;
    u32 code_bytes;
    IncrementalLookup lookup;
    IncrementalCapture capture;
    bool verify_mismatch;
    bool audit_violation;
    u8 reserved[2];
};

// The running compiler, as the bytes it was loaded from. An unidentified
// compiler disables the cache rather than guessing: two compilers that differ
// in any byte never share records.
typedef struct IncrementalCompilerIdentity IncrementalCompilerIdentity;
struct IncrementalCompilerIdentity
{
    u64 image_size;
    u64 image_fingerprint;
    u64 nanoseconds;
    bool identified;
    u8 reserved[7];
};

typedef struct IncrementalSessionOptions IncrementalSessionOptions;
struct IncrementalSessionOptions
{
    String8 cache_directory;
    // Names the pack; the context record, not the path, decides validity.
    String8 input_path;
    IncrementalCompilerIdentity compiler;
    bool verify;
    bool trace;
    u8 reserved[6];
};

typedef struct IncrementalCodegenSession IncrementalCodegenSession;

BUSTER_F_DECL IncrementalCompilerIdentity incremental_compiler_identity(Arena* arena);
BUSTER_F_DECL String8 incremental_pack_status_string(IncrementalPackStatus status);
BUSTER_F_DECL String8 incremental_lookup_string(IncrementalLookup lookup);
BUSTER_F_DECL String8 incremental_capture_string(IncrementalCapture capture);

BUSTER_F_DECL IncrementalCodegenSession* incremental_session_open(Arena* arena, IncrementalSessionOptions options);
// Codegen calls this once per module, after IR preparation and module-level
// label predeclaration and before its first attempt. It returns false when
// the session cannot participate; codegen then proceeds without reuse.
BUSTER_F_DECL bool incremental_session_bind_module(IncrementalCodegenSession* session, IrProgram* program, IrModule* module, Target target,
                                                   CodegenModuleOptions options, CodegenAbi abi, bool position_independent);
// Each code-generation attempt starts from no captures; a retried attempt
// recaptures what it emits.
BUSTER_F_DECL void incremental_session_begin_attempt(IncrementalCodegenSession* session);
BUSTER_F_DECL IncrementalFunctionArtifact const* incremental_session_reused(IncrementalCodegenSession* session, u32 function_index);
// The referenced-symbol table relocations are rebound through: slot i is the
// i-th smallest symbol id the function's record references.
BUSTER_F_DECL IrSymbolId const* incremental_session_symbol_slots(IncrementalCodegenSession* session, u32 function_index, u32* slot_count);
// False once the session cannot participate: unbound, rejected by
// bind_module, or stopped by a retried attempt.
BUSTER_F_DECL bool incremental_session_active(IncrementalCodegenSession* session);
BUSTER_F_DECL bool incremental_session_verifying(IncrementalCodegenSession* session);
BUSTER_F_DECL void incremental_session_capture(IncrementalCodegenSession* session, u32 function_index, IncrementalCapture capture,
                                               IncrementalFunctionArtifact const* artifact);
BUSTER_F_DECL void incremental_session_note_replay(IncrementalCodegenSession* session, u32 function_index, u64 nanoseconds);
BUSTER_F_DECL bool incremental_session_publish(IncrementalCodegenSession* session);
BUSTER_F_DECL IncrementalStatistics incremental_session_statistics(IncrementalCodegenSession* session);
// Sums one unit's statistics into a multi-input total.
BUSTER_F_DECL void incremental_statistics_add(IncrementalStatistics* total, IncrementalStatistics const* unit);
// Trace rows in function order, copied into `arena`; empty unless tracing.
BUSTER_F_DECL IncrementalFunctionTrace* incremental_session_trace(IncrementalCodegenSession* session, Arena* arena, u32* count);
BUSTER_F_DECL void incremental_session_close(IncrementalCodegenSession* session);

#if BUSTER_INCREMENTAL_AUDIT
// Opens the calling thread's access audit for one function's generation and,
// at its end, checks every read against the ids the function's record covers.
BUSTER_F_DECL void incremental_session_audit_open(IncrementalCodegenSession* session, u32 function_index);
BUSTER_F_DECL void incremental_session_audit_close(IncrementalCodegenSession* session, u32 function_index);
#endif
