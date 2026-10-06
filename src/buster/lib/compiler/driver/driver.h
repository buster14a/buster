#pragma once

// Public compiler-driver API behind `ide cc`: compiler_driver_parse_arguments
// turns argv into a CompilerDriverInvocation, compiler_driver_execute_invocation
// runs it into a CompilerDriverResult (aggregate statistics, diagnostics and
// artifacts), and compiler_driver_metrics_format renders the opt-in per-input
// records (CompilerDriverInputResult, requested by collect_input_metrics /
// -fmetrics-out) that a batched multi-input invocation publishes in input
// order. compiler_prewarm / compiler_parallel_prewarm prepare shared tables
// before lanes run. driver.c owns the implementation and its layout map.

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
    COMPILER_DRIVER_C_DIALECT_COUNT,
} CompilerDriverCDialect;

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
    // Parsed command lines populate only this ordered stream. The separate
    // arrays remain an execution compatibility path for API-built invocations
    // and are used only when macro_operation_count is zero.
    CPreprocessorOperation* macro_operations;
    String8* definitions;
    String8* undefinitions;
    String8* library_paths;
    String8* libraries;
    String8* framework_paths;
    String8* frameworks;
    String8* linker_arguments;
    String8* gpu_arguments;
    String8 output_path;
    String8 entry_symbol;
    String8 sysroot;
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
    // Opt-in, checked token / canonical IR / selected MIR evidence.
    String8 bootstrap_trace_prefix;
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
    u32 macro_operation_count;
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
    CompilerDriverError error;
    AssemblySyntax assembly_syntax;
    bool emit_llvm_bitcode;
    bool verbose;
    bool no_standard_includes;
    bool debug_info;
    bool disable_direct_ssa;
    bool disable_local_promotion;
    bool disable_target_local_promotion;
    u32 fast_passes;
    bool measure_fast_passes;
    bool verify_codegen;
    bool sysv_unnamed_bitfields_integer;
    bool sysv_bitfield_abi_explicit;
    // A CodegenRegisterAllocatorMode value. FAST is the driver default;
    // -fregister-allocator= selects another mode and
    // -fno-register-allocator selects NONE.
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

typedef struct CompilerDriverResult CompilerDriverResult;
struct CompilerDriverResult
{
    IrLocalPromotionStatistics local_promotion;
    IrFastStatistics fast;
    CIRDirectSsaStatistics direct_ssa;
    CTypeLayoutStatistics type_layout;
    String8 diagnostic;
    String8 warning;
    // Published in input/stage order, owned by the result arena. Empty on a
    // diagnostic-free compile; grammar-specific construction stays upstream.
    CompilerDiagnostic* diagnostics;
    u32 diagnostic_count;
    String8 output;
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
    // Maximum lanes actually participating in a native C input batch, or
    // one on serial/unsupported paths. This is not a physical-core count.
    u32 compilation_workers;
    bool has_object;
    bool has_wasm;
    bool has_wasm64;
    bool has_gpu;
    bool has_llvm_bitcode;
    bool has_ebpf;
    bool has_spirv;
};

// Prepare the target-independent frontend, ABI and opcode tables. Serial
// native emission also prepares its target's metadata on demand.
BUSTER_F_DECL void compiler_prewarm(void);
// Before an embedding caller launches a gang that may compile native C,
// prepare both native families, including all x86 forms/inline assembly.
// Idle persistent workers are still live threads; preparing a new target
// after the first gang starts is too late. The opt-in multi-TU driver calls
// this before creating its first gang; ordinary serial compilation does not.
BUSTER_F_DECL void compiler_parallel_prewarm(void);
BUSTER_F_DECL CompilerDriverInvocation compiler_driver_parse_arguments(Arena* arena, SliceString8 arguments);
BUSTER_F_DECL CompilerDriverResult compiler_driver_execute_invocation(Arena* arena, CompilerDriverInvocation invocation);
// The -fmetrics-out record text; docs/agents/driver.md is the schema.
BUSTER_F_DECL String8 compiler_driver_metrics_format(Arena* arena, CompilerDriverInvocation const* invocation, CompilerDriverResult const* result,
                                                     CompilerDriverProcessMetrics process);
