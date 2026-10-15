#pragma once

// Public compiler-driver API behind `ide cc`: compiler_driver_parse_arguments
// turns argv into a CompilerDriverInvocation, compiler_driver_execute_invocation
// runs it into a CompilerDriverResult (aggregate statistics, diagnostics and
// artifacts), and compiler_driver_metrics_format renders the opt-in per-input
// records (CompilerDriverInputResult, requested by collect_input_metrics /
// -fmetrics-out) that a batched multi-input invocation publishes in input
// order. The embedding facade supplies input_source and retain_object for one
// in-memory C input without publishing an object file. compiler_prewarm /
// compiler_parallel_prewarm prepare shared tables before lanes run. driver.c
// owns the implementation and its layout map.

#include <buster/lib/compiler/diagnostic.h>
#include <buster/lib/compiler/assembly/assembly_unit.h>
#include <buster/lib/compiler/frontend/c/c.h>
#include <buster/lib/compiler/link/link.h>
#include <buster/lib/compiler/gpu/gpu.h>
#include <buster/lib/compiler/wasm/wasm.h>
#include <buster/lib/compiler/llvm/bitcode.h>
#include <buster/lib/compiler/ebpf/ebpf.h>
#include <buster/lib/compiler/spirv/spirv.h>

// A unity C translation unit retains preprocessing, semantic, typed IR, and
// object/debug data through the driver call. The reservation is virtual and
// uses demand-paged commits, so this headroom does not eagerly consume 8 GiB
// of physical memory.
#define COMPILER_DRIVER_C_TRANSLATION_UNIT_RESERVED_SIZE BUSTER_GB(32)
#define COMPILER_DRIVER_SOURCE_CACHE_BYTE_LIMIT BUSTER_MB(16)

// Bounds of `@path` response-file expansion in compiler_driver_parse_arguments:
// the bytes read from all response files of one invocation together, and the
// length of the expanded command line. A command line with no argument that
// begins with '@' is not expanded and neither bound applies to it.
#define COMPILER_DRIVER_RESPONSE_FILE_BYTE_LIMIT BUSTER_MB(4)
#define COMPILER_DRIVER_RESPONSE_FILE_ARGUMENT_LIMIT ((u64)1 << 16)

// Lane gate for -fcompile-jobs translation-unit lanes (#53). Estimated work is
// each eligible input's file size plus a per-input charge for the fixed cost
// of a translation unit, in bytes. Below COMPILER_DRIVER_LANE_MIN_WORK_BYTES a
// link keeps the one-worker path; otherwise the worker count is capped so each
// worker keeps at least COMPILER_DRIVER_LANE_WORK_PER_WORKER_BYTES (never below
// two). Values fitted from the 9700X scaling-v1 receipt of #3943 (check run
// 115115906751, main 10e44760 with the one-family prewarm and lane-safe x86
// fills): two equal TUs already gain 1.20x, so the serial minimum is 0; serial
// time is ~5.6 ms + 0.067 us/byte + 0.035 ms/input, so the per-input charge is
// 513 bytes; tiny TUs lose at 10.8 KB/worker (0.93x, 7 workers) and gain most
// at 37.8 KB/worker (2 workers: 1.15x there, 1.09x in the validation run
// 115276385855, ahead of 4 workers' 1.11x and 1.04x), so the floor is 32 KiB.
// Re-derive all three whenever the lane entry cost changes
// (docs/agents/driver.md, "Opt-in native translation-unit lanes").
#define COMPILER_DRIVER_LANE_MIN_WORK_BYTES ((u64)0)
#define COMPILER_DRIVER_LANE_INPUT_WORK_BYTES ((u64)513)
#define COMPILER_DRIVER_LANE_WORK_PER_WORKER_BYTES ((u64)32 * 1024)

// What the lane gate decided for one invocation. NONE: lanes were not
// requested or cannot apply (one worker, fewer than two eligible inputs, an
// embedding gang or a source cache). LANES: lanes were permitted at the
// requested width. SERIAL: the population was too small, so the one-worker
// path ran. CAPPED: lanes ran with fewer workers than requested so each keeps
// the per-worker minimum.
typedef enum CompilerDriverLaneGate
{
    COMPILER_DRIVER_LANE_GATE_NONE,
    COMPILER_DRIVER_LANE_GATE_LANES,
    COMPILER_DRIVER_LANE_GATE_SERIAL,
    COMPILER_DRIVER_LANE_GATE_CAPPED,
    COMPILER_DRIVER_LANE_GATE_COUNT,
} CompilerDriverLaneGate;

typedef enum CompilerDriverError
{
    COMPILER_DRIVER_ERROR_NONE,
    COMPILER_DRIVER_ERROR_ARGUMENT,
    COMPILER_DRIVER_ERROR_INVALID_INPUT,
    COMPILER_DRIVER_ERROR_FILE_READ,
    COMPILER_DRIVER_ERROR_TOKENIZE,
    COMPILER_DRIVER_ERROR_PARSE,
    COMPILER_DRIVER_ERROR_ANALYSIS,
    COMPILER_DRIVER_ERROR_IR,
    COMPILER_DRIVER_ERROR_LLVM_BITCODE,
    COMPILER_DRIVER_ERROR_CODEGEN,
    COMPILER_DRIVER_ERROR_WASM64,
    COMPILER_DRIVER_ERROR_WASM = COMPILER_DRIVER_ERROR_WASM64,
    COMPILER_DRIVER_ERROR_GPU,
    COMPILER_DRIVER_ERROR_EBPF,
    COMPILER_DRIVER_ERROR_OBJECT,
    COMPILER_DRIVER_ERROR_LINK,
    COMPILER_DRIVER_ERROR_FILE_WRITE,
    COMPILER_DRIVER_ERROR_SPIRV,
    // A required driver arena reservation failed; the source is not invalid.
    COMPILER_DRIVER_ERROR_RESOURCE,
    COMPILER_DRIVER_ERROR_COUNT,
} CompilerDriverError;

typedef enum CompilerDriverLanguage
{
    COMPILER_DRIVER_LANGUAGE_AUTOMATIC,
    COMPILER_DRIVER_LANGUAGE_C,
    COMPILER_DRIVER_LANGUAGE_OPENCL,
    COMPILER_DRIVER_LANGUAGE_CUDA,
    COMPILER_DRIVER_LANGUAGE_HIP,
    COMPILER_DRIVER_LANGUAGE_METAL,
    COMPILER_DRIVER_LANGUAGE_HLSL,
    COMPILER_DRIVER_LANGUAGE_LLVM_IR,
    COMPILER_DRIVER_LANGUAGE_SPIRV_BINARY,
    COMPILER_DRIVER_LANGUAGE_METAL_AIR,
    COMPILER_DRIVER_LANGUAGE_ASSEMBLY,
    // C tokens whose preprocessing directives and macro expansion have
    // already completed (`.i` / `-x cpp-output`). Kept after the existing
    // values so adding the phase distinction does not renumber the API.
    COMPILER_DRIVER_LANGUAGE_CPP_OUTPUT,
    COMPILER_DRIVER_LANGUAGE_COUNT,
} CompilerDriverLanguage;

typedef enum CompilerDriverAction
{
    COMPILER_DRIVER_ACTION_LINK,
    COMPILER_DRIVER_ACTION_PREPROCESS,
    COMPILER_DRIVER_ACTION_ASSEMBLY,
    COMPILER_DRIVER_ACTION_OBJECT,
    COMPILER_DRIVER_ACTION_SYNTAX_ONLY,
    COMPILER_DRIVER_ACTION_COUNT,
} CompilerDriverAction;

// Informational queries answered without any input: --version, -dumpversion,
// -dumpmachine, -print-target-triple/-print-effective-triple, and the native
// linker identity (-Wl,--version or -Xlinker --version).
// compiler_driver_query_text renders the text.
typedef enum CompilerDriverQuery
{
    COMPILER_DRIVER_QUERY_NONE,
    COMPILER_DRIVER_QUERY_VERSION,
    COMPILER_DRIVER_QUERY_DUMP_VERSION,
    COMPILER_DRIVER_QUERY_DUMP_MACHINE,
    COMPILER_DRIVER_QUERY_TARGET_TRIPLE,
    COMPILER_DRIVER_QUERY_LINKER_VERSION,
    COMPILER_DRIVER_QUERY_COUNT,
} CompilerDriverQuery;

typedef enum CompilerDriverCDialect
{
    COMPILER_DRIVER_C_DIALECT_GNU99,
    COMPILER_DRIVER_C_DIALECT_GNU11,
    COMPILER_DRIVER_C_DIALECT_GNU17,
    COMPILER_DRIVER_C_DIALECT_GNU23,
    COMPILER_DRIVER_C_DIALECT_C99,
    COMPILER_DRIVER_C_DIALECT_C11,
    COMPILER_DRIVER_C_DIALECT_C17,
    COMPILER_DRIVER_C_DIALECT_C23,
    COMPILER_DRIVER_C_DIALECT_GNU89,
    COMPILER_DRIVER_C_DIALECT_C89,
    COMPILER_DRIVER_C_DIALECT_COUNT,
} CompilerDriverCDialect;

// -fc-ast-pilot[=layout] (GitHub #3102): build the implicit postorder C syntax
// tree of each C input after preprocessing and before c_parse_ast, inside the
// parse phase, for analysis and lowering to read; a unit the tree builder
// rejects on syntax compiles without a tree
// (CompilerDriverCAstPilotResult.rejections); one that cannot reserve its
// storage fails with the parse error class. compiler_driver_parse_arguments
// defaults to IMPLICIT; -fno-c-ast-pilot selects OFF, the tree-free reference
// path, until #4058 deletes it. A zero-initialized invocation is OFF.
typedef enum CompilerDriverCAstPilot
{
    COMPILER_DRIVER_C_AST_PILOT_OFF,
    COMPILER_DRIVER_C_AST_PILOT_IMPLICIT,
    COMPILER_DRIVER_C_AST_PILOT_HYBRID,
    COMPILER_DRIVER_C_AST_PILOT_EXPLICIT,
    COMPILER_DRIVER_C_AST_PILOT_COUNT,
} CompilerDriverCAstPilot;

typedef enum CompilerDriverLinkOperationKind
{
    COMPILER_DRIVER_LINK_OPERATION_FILE,
    COMPILER_DRIVER_LINK_OPERATION_LIBRARY,
    COMPILER_DRIVER_LINK_OPERATION_COUNT,
} CompilerDriverLinkOperationKind;

typedef struct CompilerDriverLinkOperation CompilerDriverLinkOperation;
struct CompilerDriverLinkOperation
{
    u32 index;
    CompilerDriverLinkOperationKind kind;
    // Linux ELF driver policy for this library occurrence.
    bool as_needed;
};

// The -W<name> groups that name a warning the driver publishes today. Other
// -W spellings are accepted and ignored (-Wall, -Wextra, unknown names).
typedef enum CompilerDriverWarningGroup
{
    // #warning (GCC -Wcpp, Clang -W#warnings).
    COMPILER_DRIVER_WARNING_GROUP_CPP,
    // Extra tokens after #else, #endif, #ifdef, #include, ... (Clang
    // -Wextra-tokens, GCC -Wendif-labels for the first two).
    COMPILER_DRIVER_WARNING_GROUP_EXTRA_TOKENS,
    // GNU `member: value` designator in a strict ISO dialect (Clang
    // -Wgnu-designator).
    COMPILER_DRIVER_WARNING_GROUP_GNU_DESIGNATOR,
    // Implicit pointer conversions whose pointees differ only in integer
    // signedness (GCC/Clang -Wpointer-sign), published by semantic analysis.
    COMPILER_DRIVER_WARNING_GROUP_POINTER_SIGN,
    COMPILER_DRIVER_WARNING_GROUP_COUNT,
} CompilerDriverWarningGroup;

// -Werror=<g> and -Wno-error=<g> set an explicit promotion that outranks the
// global -Werror/-Wno-error whatever their order, as in GCC and Clang.
typedef enum CompilerDriverWarningPromotion
{
    COMPILER_DRIVER_WARNING_PROMOTION_DEFAULT,
    COMPILER_DRIVER_WARNING_PROMOTION_ERROR,
    COMPILER_DRIVER_WARNING_PROMOTION_WARNING,
} CompilerDriverWarningPromotion;

// Options apply left to right and the last one that names a group or the
// global flag wins. -w (CompilerDriverInvocation.suppress_warnings) outranks
// all of it.
typedef struct CompilerDriverWarningPolicy CompilerDriverWarningPolicy;
struct CompilerDriverWarningPolicy
{
    // -Werror / -Wno-error without a group.
    bool werror;
    // -Wno-<g>; -W<g> and -Werror=<g> clear it.
    bool disabled[COMPILER_DRIVER_WARNING_GROUP_COUNT];
    // -Wno-everything was given; a later -Weverything then enables nothing.
    bool everything_off;
    CompilerDriverWarningPromotion promotion[COMPILER_DRIVER_WARNING_GROUP_COUNT];
};

typedef struct CompilerDriverInvocation CompilerDriverInvocation;
struct CompilerDriverInvocation
{
    String8* input_paths;
    // Parsed command lines snapshot the active -x selection beside
    // every input. When this pointer is non-null it is authoritative
    // and contains exactly input_count entries. API-built legacy
    // invocations leave it null and continue to use language globally.
    CompilerDriverLanguage* input_languages;
    // Parsed native links apply files and libraries in this CLI order. Each
    // index selects the next entry of input_paths or libraries. A zero count
    // retains the API's legacy file-then-library order.
    CompilerDriverLinkOperation* link_operations;
    String8* include_paths;
    String8* system_include_paths;
    // -include FILE names in command-line order; every C translation unit
    // processes them as #include "FILE" before its first line.
    String8* forced_includes;
    // Parsed command lines populate only this ordered stream. The separate
    // arrays remain an execution compatibility path for API-built invocations
    // and are used only when macro_operation_count is zero.
    CPreprocessorOperation* macro_operations;
    // -fmacro-prefix-map=OLD=NEW, in argv order; only builtin spellings change.
    CMacroPrefixMap* macro_prefix_maps;
    String8* definitions;
    String8* undefinitions;
    String8* library_paths;
    String8* libraries;
    String8* framework_paths;
    String8* frameworks;
    String8* linker_arguments;
    String8* gpu_arguments;
    String8 output_path;
    // -MF writes the depfile here; otherwise it is derived from the object path.
    String8 dependency_path;
    // Ordered, space-joined -MT/-MQ targets; empty uses an -MQ-quoted object path.
    String8 dependency_targets;
    String8 entry_symbol;
    String8 sysroot;
    // Source text of the `-` input. A parsed command line names it with the
    // path `-`; the cc command reads standard input into it before execution
    // and embedding callers supply it themselves. A null pointer means it was
    // not supplied; an empty translation unit has a nonnull pointer.
    String8 standard_input;
    // API-only in-memory C input. When non-null, input_paths[0] names the
    // virtual source path for diagnostics, __FILE__, and quoted includes; it
    // is not opened. The source has no filesystem identity.
    String8 input_source;
    // API-only: retain the single -c object's ObjectFile in the result and
    // suppress writing an object file.
    bool retain_object;
    // Where to write the source measurement as key=value text. `-v` prints the
    // same numbers as a table for a human; this is the form another program
    // reads, so a build driver can divide its own instruction count by them.
    String8 source_metrics_path;
    // API-only opt-out from retaining structured records. Legacy diagnostic
    // text and warnings remain available; clean compilation allocates neither.
    bool suppress_diagnostic_records;
    // API opt-out from summing preprocessed.bytes, which only the `-v` source
    // report and the source-metrics file read. The field is then zero; every
    // other preprocessed count is still gathered. The cc command sets it when
    // it prints neither report.
    bool omit_spelled_bytes;
    // API-only (`ide run`): with COMPILER_DRIVER_ACTION_LINK, stop after
    // link_objects on either the single-unit or multi-input path and return
    // the merged in-memory object in CompilerDriverResult.object; do not look
    // up dynamic libraries, write an executable or create an output file.
    bool retain_linked_object;
    // `-dM`: with -E, print the macros defined at the end of preprocessing as
    // `#define` lines instead of the preprocessed text. Ignored without -E.
    bool dump_macros;
    // Opt-in, checked token / canonical IR / selected MIR evidence.
    String8 bootstrap_trace_prefix;
    // Optional one-function native ELF investigation. CLI parsing snapshots
    // length-framed expanded arguments; API callers supply their configuration.
    String8 investigation_path;
    String8 investigation_function;
    String8 investigation_configuration;
    String8 gpu_architecture;
    String8 gpu_entry_point;
    String8 gpu_stage;
    String8 gpu_shader_model;
    String8 metal_sdk;
    String8 cuda_path;
    String8 rocm_path;
    String8 diagnostic;
    GpuToolchain gpu_tools;
    GpuTarget gpu_target;
    Target target;
    TargetPlainCharPolicy plain_char_policy;
    u32 input_count;
    // Zero when input_languages is null; otherwise exactly input_count.
    u32 input_language_count;
    // A nonzero stream covers every input and library occurrence exactly once.
    u32 link_operation_count;
    // -fcompile-jobs=N: opt-in lanes for consecutive native C link inputs.
    // Zero/default is one. The caller owns its total process/thread budget;
    // this does not infer available RAM from the TU's virtual reservation.
    u32 compile_jobs;
    // API-only raw lex reuse across serial units/invocations. Caller owns the
    // cache, exclusively on this thread. Presence clamps TU workers to one.
    CSourceCache* source_cache;
    // -fsource-cache creates an invocation-local cache when the API pointer
    // is null; -fno-source-cache cancels that request. Default is disabled.
    bool enable_source_cache;
    u32 include_path_count;
    u32 system_include_path_count;
    u32 forced_include_count;
    u32 macro_operation_count;
    u32 macro_prefix_map_count;
    u32 definition_count;
    u32 undefinition_count;
    u32 library_path_count;
    u32 library_count;
    u32 framework_path_count;
    u32 framework_count;
    u32 linker_argument_count;
    u32 gpu_argument_count;
    CompilerDriverLanguage language;
    CompilerDriverAction action;
    CompilerDriverCDialect c_dialect;
    CompilerDriverCAstPilot c_ast_pilot;
    CompilerDriverError error;
    AssemblySyntax assembly_syntax;
    bool emit_llvm_bitcode;
    // -MD/-MMD enable depfile emission; the last one selects system filtering.
    bool dependency_file;
    bool dependency_exclude_system;
    // Exact -Wp,-MD,PATH / -Wp,-MMD,PATH: one C input, also named native -S.
    bool dependency_passthrough;
    // -MP emits phony rules for header prerequisites.
    bool dependency_phony_targets;
    bool verbose;
    bool linker_verbose;
    // -w: the driver publishes no warning text or warning records.
    bool suppress_warnings;
    // -Werror, -Wno-error and the per-group spellings (see CompilerDriverWarningPolicy).
    CompilerDriverWarningPolicy warning_policy;
    CompilerDriverQuery query;
    bool no_standard_includes;
    // -nostdlibinc keeps only the compiler resource include directory.
    bool no_standard_library_includes;
    // Last -std=, -trigraphs, -ftrigraphs or -fno-trigraphs wins.
    CTrigraphMode trigraph_mode;
    // -working-directory: relative input paths resolve against it.
    String8 working_directory;
    bool debug_info;
    // -ffreestanding controls __STDC_HOSTED__ in the C frontend.
    bool freestanding;
    // Accepted only for I386, where native artifact generation is refused.
    bool no_asynchronous_unwind_tables;
    bool disable_direct_ssa;
    // -fno-canonical-intern-constants: CIRLowerOptions.disable_block_constant_interning.
    bool disable_block_constant_interning;
    // -fcanonical-construction-fold: CIRLowerOptions.construction_fold.
    bool construction_fold;
    bool disable_local_promotion;
    bool disable_target_local_promotion;
    // -fpinned-debug-locals / -fno-pinned-debug-locals override the default.
    // With -g, named scalar locals are pinned by default for readable locations.
    bool enable_pinned_debug_locals;
    // -fvisibility=: a CSymbolVisibility, UNSPECIFIED (0) when not given. It is
    // the visibility of definitions that carry no attribute or pragma.
    u8 default_visibility;
    bool pinned_debug_locals_explicit;
    // Effective optional pass mask: no-flag/positive -O use IR_FAST_ALL,
    // explicit -O/-O0 use zero, and explicit pass controls override either.
    u32 fast_passes;
    bool measure_fast_passes;
    // -fcanonical-fast-cse-strategy=window selects the CSE census's linear
    // arm; =table, the default, selects the hash table (#3011).
    bool fast_cse_window;
    // Tiny direct-call inlining defaults off. An explicit optimization level
    // above -O0 enables it; -O0 disables it. The last -O* or explicit
    // -fcanonical-inline/-fno-canonical-inline option wins. always_inline
    // requests remain semantic requirements when tiny is false.
    IrInlineOptions inline_options;
    bool verify_codegen;
    bool sysv_unnamed_bitfields_integer;
    bool sysv_bitfield_abi_explicit;
    // A CodegenRegisterAllocatorMode value. FAST is the driver default;
    // -fregister-allocator= accepts fast or quality.
    u8 register_allocator;
    // -fPIC/-fpic/-fPIE/-fpie, cleared by -fno-pic (and -fno-pie after a PIE
    // spelling), and implied by linking a position-independent image. The
    // code generator reads it as a code model: it picks the thread-local
    // model, and a symbol another object could interpose is addressed through
    // the GOT and called through the PLT, which are the references `ld
    // -shared` will place. A PIE takes the same model; the image writer
    // relaxes the GOT loads of the definitions it binds.
    bool position_independent;
    // 0: none, 1: lowercase PIC spelling, 2: uppercase PIC spelling.
    u8 position_independent_level;
    // The selected position-independent spelling was a PIE flag.
    bool position_independent_executable;
    // -shared or -pie: the NativeImageKind a link produces. Accepted for a
    // link only on x86-64 Linux, the one target with a writer for it.
    NativeImageKind image_kind;
    u8 optimization_level;
    bool has_gpu_target;
    bool save_gpu_temporaries;
    bool register_allocator_explicit;
    bool plain_char_policy_explicit;
    // Compatibility state for -fno-machine-fallback/-fmachine-fallback.
    // Native production codegen is unconditionally MIR-only; neither flag
    // can re-enable the retired direct canonical fallback.
    bool reject_machine_fallback;
    // Compatibility state for the retired native fallback census. Current
    // production generation returns no fallback records.
    bool record_codegen_fallbacks;
    bool c_dialect_explicit;
    bool debug_info_explicit;
    // Per-input records for batched compiles; docs/agents/driver.md
    // ("Per-input records and continue-on-failure") is the contract and the
    // -fmetrics-out schema. collect_input_metrics (-fmetrics-out) measures
    // each input; collect_function_sizes (-fmetrics-functions) requires it
    // and adds function sizes; keep_going (-fkeep-going) continues past a
    // failed translation unit. Without them no clock is read and nothing is
    // allocated. Metrics reject suppress_diagnostic_records.
    bool collect_input_metrics;
    bool collect_function_sizes;
    bool keep_going;
    // The clock origin of every per-input start/end offset and of the
    // process wall time. `ide cc` sets it right after argument parsing; the
    // driver takes its own at entry when has_metrics_origin is false.
    bool has_metrics_origin;
    TimeDataType metrics_origin;
    // Where `ide cc` writes the records; the driver never writes it.
    String8 metrics_output_path;
    // -fearly-exit (off by default; -fno-early-exit restores it): `ide cc`
    // forks before any lane exists and reports its status once every
    // artifact and report is out, leaving teardown to the child (#3686,
    // os_early_exit_arm). The driver itself never reads it.
    bool early_exit;
};

// Contiguous intervals of compiler_driver_execute_c_single, named after the
// CompilerDriverError stage that fails inside each (see driver.md).
typedef enum CompilerDriverPhase
{
    COMPILER_DRIVER_PHASE_READ,
    COMPILER_DRIVER_PHASE_PREPROCESS,
    COMPILER_DRIVER_PHASE_PARSE,
    COMPILER_DRIVER_PHASE_ANALYSIS,
    COMPILER_DRIVER_PHASE_IR,
    COMPILER_DRIVER_PHASE_CODEGEN,
    COMPILER_DRIVER_PHASE_OBJECT,
    COMPILER_DRIVER_PHASE_EMIT,
    COMPILER_DRIVER_PHASE_COUNT,
} CompilerDriverPhase;

typedef enum CompilerDriverInputStatus
{
    COMPILER_DRIVER_INPUT_STATUS_NOT_RUN,
    COMPILER_DRIVER_INPUT_STATUS_OK,
    // Refused with source diagnostics: TOKENIZE, PARSE, ANALYSIS, INVALID_INPUT.
    COMPILER_DRIVER_INPUT_STATUS_REJECTED,
    // Any other failure, including an unreadable prebuilt input.
    COMPILER_DRIVER_INPUT_STATUS_FAILED,
    // An object or archive link input that was read, not compiled.
    COMPILER_DRIVER_INPUT_STATUS_PREBUILT,
    COMPILER_DRIVER_INPUT_STATUS_COUNT,
} CompilerDriverInputStatus;

// Object-model section bytes grouped by ObjectSectionKind (driver.md).
typedef enum CompilerDriverSectionClass
{
    COMPILER_DRIVER_SECTION_CLASS_TEXT,
    COMPILER_DRIVER_SECTION_CLASS_READ_ONLY_DATA,
    COMPILER_DRIVER_SECTION_CLASS_DATA,
    COMPILER_DRIVER_SECTION_CLASS_ZERO,
    COMPILER_DRIVER_SECTION_CLASS_THREAD_LOCAL_DATA,
    COMPILER_DRIVER_SECTION_CLASS_THREAD_LOCAL_ZERO,
    COMPILER_DRIVER_SECTION_CLASS_INITIALIZER,
    COMPILER_DRIVER_SECTION_CLASS_UNWIND,
    COMPILER_DRIVER_SECTION_CLASS_DEBUG,
    COMPILER_DRIVER_SECTION_CLASS_COUNT,
} CompilerDriverSectionClass;

// Function records kept per input. The record text truncates only messages
// and function names, at COMPILER_DRIVER_METRICS_TEXT_LIMIT bytes, and then
// carries their full length and a truncation flag; paths and diagnostic codes
// are written whole.
#define COMPILER_DRIVER_INPUT_FUNCTION_LIMIT 65536u
#define COMPILER_DRIVER_METRICS_TEXT_LIMIT 1024u
#define COMPILER_DRIVER_METRICS_VERSION 1u

typedef struct CompilerDriverFunctionSize CompilerDriverFunctionSize;
struct CompilerDriverFunctionSize
{
    String8 name;
    u64 code_bytes;
};

// One record per input, in input order, owned by the result arena; present
// only with collect_input_metrics or keep_going. driver.md defines every field.
typedef struct CompilerDriverInputResult CompilerDriverInputResult;
struct CompilerDriverInputResult
{
    String8 path;
    String8 message;
    String8 diagnostic_code;
    String8 diagnostic_path;
    // Lowercase hex SHA-256 over this input's diagnostic records.
    String8 diagnostic_digest;
    CompilerDriverFunctionSize* functions;
    CodegenStatistics codegen;
    // Offsets from metrics_origin; the phases partition end - start.
    u64 start_nanoseconds;
    u64 end_nanoseconds;
    u64 phase_nanoseconds[COMPILER_DRIVER_PHASE_COUNT];
    u64 arena_peak_bytes;
    u64 arena_retained_bytes;
    u64 object_file_bytes;
    u64 section_bytes[COMPILER_DRIVER_SECTION_CLASS_COUNT];
    u64 source_bytes;
    u64 preprocessed_tokens;
    u32 index;
    u32 diagnostic_line;
    u32 diagnostic_column;
    u32 diagnostic_record_count;
    u32 error_count;
    u32 warning_count;
    u32 function_count;
    u32 functions_omitted;
    u32 fallback_record_count;
    CompilerDriverError error;
    CompilerDriverInputStatus status;
    bool measured;
};

// What only the process knows, for the header record.
typedef struct CompilerDriverProcessMetrics CompilerDriverProcessMetrics;
struct CompilerDriverProcessMetrics
{
    u64 wall_nanoseconds;
    u64 peak_resident_bytes;
    u32 exit_status;
};

typedef struct CompilerDriverFallbackRecord CompilerDriverFallbackRecord;
struct CompilerDriverFallbackRecord
{
    String8 source;
    String8 function;
    CodegenFallbackRecord codegen;
    u32 line;
    u32 column;
};

// What the -fc-ast-pilot hook measured, summed over the inputs that built a
// tree. `units` is zero when the hook did not run; `rejections` counts the
// units whose tree the builder refused on syntax, which compiled without one. The walk, scan and
// children rows are diagnostic passes over the finished tree and are timed
// only under -v: one full c_ast_walk (`walk_steps` events), one linear pass
// over the kinds column counting CALL nodes (`scan_calls`), and
// c_ast_children over every node into a scratch buffer (`child_entries` is
// the sum of the child counts it returned).
typedef struct CompilerDriverCAstPilotResult CompilerDriverCAstPilotResult;
struct CompilerDriverCAstPilotResult
{
    u64 units;
    u64 rejections;
    u64 nodes;
    u64 tokens;
    u64 build_nanoseconds;
    u64 retained_bytes;
    u64 transient_high_water;
    u64 column_copy_bytes;
    u64 parenthesis_copy_bytes;
    u64 parenthesis_committed_bytes;
    u64 finalize_child_entries;
    u64 walk_nanoseconds;
    u64 walk_steps;
    u64 scan_nanoseconds;
    u64 scan_calls;
    u64 children_nanoseconds;
    u64 child_entries;
    // What the tree expression typer did during semantic analysis; see
    // CAstTypeStatistics. Zero when the analysis had no tree to use.
    CAstTypeStatistics types;
    // The declaration split read from the tree (c_parse_ast_from_tree):
    // records published, and units handed to the token walker and why.
    CParserTreeStatistics split;
    // The statement plans the body binder followed (c_parse.c): bodies,
    // segments and hints read from the tree, and bodies left to the token
    // walk and why. Zero when the analysis had no tree to use.
    CAstStatementStatistics statements;
    // The declaration specifier runs read from the tree (c_parse.c): runs
    // answered, and runs left to the type machine and why. Zero when the
    // analysis had no tree to use.
    CAstSpecifierStatistics specifiers;
    // What lowering read from the tree (CAstLowerStatistics), on every
    // compile that lowers: lowering builds the unit's tree itself when the
    // pilot did not (GitHub #3102 step 1). Zero for -fsyntax-only.
    CAstLowerStatistics lower;
    // The facts analysis retained for lowering (CAstFactStatistics, GitHub
    // #4062). Zero for -fsyntax-only, which does not lower.
    CAstFactStatistics facts;
};

typedef struct CompilerDriverResult CompilerDriverResult;
struct CompilerDriverResult
{
    IrLocalPromotionStatistics local_promotion;
    IrFastStatistics fast;
    IrInlineStatistics inlining;
    CIRDirectSsaStatistics direct_ssa;
    CIRConstantInternStatistics constant_interning;
    CIRConstructionFoldStatistics construction_folds;
    CTypeLayoutStatistics type_layout;
    CompilerDriverCAstPilotResult c_ast;
    String8 diagnostic;
    String8 warning;
    // Published in input/stage order, owned by the result arena. Empty on a
    // diagnostic-free compile; grammar-specific construction stays upstream.
    CompilerDiagnostic* diagnostics;
    u32 diagnostic_count;
    String8 output;
    String8 linker_report;
    NativeExecutableLinkResult native_link;
    WasmArtifact wasm;
    // Compatibility mirror; both artifacts reference the same arena-owned
    // bytes when WebAssembly output is produced.
    Wasm64Artifact wasm64;
    GpuArtifact gpu;
    LlvmBitcodeArtifact llvm_bitcode;
    EbpfArtifact ebpf;
    SpirvArtifact spirv;
    ObjectFile object;
    // What serializing `object` cost, for -c; zero when no object was written.
    ObjectWriteStatistics object_write_statistics;
    CodegenStatistics codegen_statistics;
    CompilerDriverFallbackRecord* fallback_records;
    u32 fallback_record_count;
    // Per-input records (see CompilerDriverInputResult); null unless
    // collect_input_metrics or keep_going was requested.
    CompilerDriverInputResult* inputs;
    u32 input_result_count;
    // Inputs whose status is REJECTED or FAILED.
    u32 failed_input_count;
    // What the C frontend consumed, per inclusion and per distinct file, and
    // what preprocessing made of it. `lexed_files` attributes the difference
    // between the two aggregates: across several inputs it keeps only the
    // rows lexed more than once within their own unit, since each unit
    // legitimately lexes a shared header once.
    CSourceMetrics source_lexed;
    CSourceMetrics source_unique;
    CSourceFileMetrics* lexed_files;
    u32 lexed_file_count;
    u32 lexed_files_reserved;
    CPreprocessedMetrics preprocessed;
    // Cumulative cache counters at invocation exit, separate from SOURCE metrics.
    CSourceCacheStats source_cache;
    CompilerDriverError error;
    CodegenError codegen_error;
    ObjectError object_error;
    u32 tokenizer_error_count;
    u32 tokenizer_warning_count;
    u32 parser_diagnostic_count;
    u32 analysis_diagnostic_count;
    // Published semantic-analysis warnings (-Wpointer-sign), less any that
    // -Werror promoted; like tokenizer_warning_count it still counts one -w
    // or -Wno-pointer-sign dropped.
    u32 analysis_warning_count;
    // Maximum lanes actually participating in a native C input batch, or
    // one on serial/unsupported paths. This is not a physical-core count.
    u32 compilation_workers;
    CompilerDriverLaneGate lane_gate;
    bool has_object;
    bool has_wasm;
    bool has_wasm64;
    bool has_gpu;
    bool has_llvm_bitcode;
    bool has_ebpf;
    bool has_spirv;
    // Physical C source/header files kept alive for the enclosing native-link
    // output guard. Virtual builtin headers are absent; the system bit only
    // classifies depfile text and never filters this safety closure.
    CPreprocessDependency* physical_inputs;
    u32 physical_input_count;
};

// Prepare the target-independent frontend, ABI and opcode tables. Serial
// native emission also prepares its target's metadata on demand.
BUSTER_F_DECL void compiler_prewarm(void);
// Before an embedding caller launches a gang that may compile native C,
// prepare both native families' serially built tables. The x86 per-form
// caches, exact plans and machine-encoder tables are not among them: lanes
// fill the entries they reach, under once-claims. Idle persistent workers are
// still live threads; preparing a new target after the first gang starts is
// too late. The opt-in multi-TU driver does not call this: it prepares only
// its cohort's family and releases its own parked gang (lane_release) before
// preparing another. Ordinary serial compilation does neither.
BUSTER_F_DECL void compiler_parallel_prewarm(void);
// When true, an absent -march keeps the host CPU (the test runners set it);
// when false, the default is the portable baseline like Clang and GCC (#1557).
BUSTER_V_DECL bool compiler_driver_host_cpu_default;
BUSTER_F_DECL CompilerDriverInvocation compiler_driver_parse_arguments(Arena* arena, SliceString8 arguments);
// The CPU an absent -march leaves behind: baseline by default, the host CPU
// under compiler_driver_host_cpu_default (#1557).
BUSTER_F_DECL Target compiler_driver_default_target(bool host_cpu_default);
// The layout spelling of -fc-ast-pilot=<layout> ("implicit", "hybrid",
// "explicit"); "off" for COMPILER_DRIVER_C_AST_PILOT_OFF.
BUSTER_F_DECL String8 compiler_driver_c_ast_pilot_name(CompilerDriverCAstPilot pilot);
// The output of a parsed --version/-dumpversion/-dumpmachine query. The version
// is the one the C frontend presents in __clang_major__/__clang_minor__/
// __clang_patchlevel__; the machine is the invocation's effective target.
// The linker query reports Buster's own identity, without a GNU/LLVM version.
BUSTER_F_DECL String8 compiler_driver_query_text(Arena* arena, CompilerDriverInvocation const* invocation);
// Resolves an input against -working-directory with the host's path rules, as
// LLVM's make_absolute does. On Windows a rooted path without a drive ("\\x.c")
// takes the root name ("C:" or "\\\\server") of the absolute directory, and a
// drive-relative "C:x.c" takes its root-relative part. Empty when the
// directory has no root name.
BUSTER_F_DECL String8 compiler_driver_working_directory_path(Arena* arena, String8 directory, String8 input);
BUSTER_F_DECL CompilerDriverResult compiler_driver_execute_invocation(Arena* arena, CompilerDriverInvocation invocation);
// The -fmetrics-out record text; docs/agents/driver.md is the schema.
BUSTER_F_DECL String8 compiler_driver_metrics_format(Arena* arena, CompilerDriverInvocation const* invocation, CompilerDriverResult const* result,
                                                     CompilerDriverProcessMetrics process);
